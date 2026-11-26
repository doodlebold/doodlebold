#include "ui/animation/AnimationController.h"

#include "core/animation/AnimationOps.h"
#include "core/document/Document.h"
#include "ui/animation/FrameStrip.h"
#include "ui/animation/SoundPlayback.h"
#include "ui/animation/TimelinePanel.h"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QMessageBox>
#include <QProgressDialog>
#include <QPushButton>
#include <QStandardPaths>
#include <QTemporaryDir>

#include <algorithm>
#include <chrono>
#include <optional>

namespace doodlebold {

namespace {

// A copy, not a reference: an edit replaces the document's state, and with it the clip list a reference would point into.
std::optional<SoundClip> soundClip(const Animation& a, int id) {
    for (const SoundClip& c : a.sounds)
        if (c.id == id)
            return c;
    return std::nullopt;
}

// The clips that play and are exported: none of a hidden sound track, and no muted one.
std::vector<SoundClip> heardSounds(const Animation& a) {
    std::vector<SoundClip> out;
    std::ranges::copy_if(a.sounds, std::back_inserter(out), [&](const SoundClip& c) { return a.heard(c); });
    return out;
}

std::vector<double> gainsOf(const Animation& a, const std::vector<SoundClip>& clips) {
    std::vector<double> out;
    out.reserve(clips.size());
    for (const SoundClip& c : clips)
        out.push_back(a.gainOf(c));
    return out;
}

constexpr qint64 kMaxSoundBytes = 512LL << 20;

class WaitCursor {
public:
    WaitCursor() { QApplication::setOverrideCursor(Qt::WaitCursor); }
    ~WaitCursor() { QApplication::restoreOverrideCursor(); }
    WaitCursor(const WaitCursor&) = delete;
    WaitCursor& operator=(const WaitCursor&) = delete;
};

bool sameCache(const VideoFrames& frames, const VideoReference& video, const QSize& canvas) {
    const VideoReference& v = frames.video();
    return frames.canvas() == canvas && v.path == video.path && v.fps == video.fps && v.lossless == video.lossless;
}

}

// The video being read on a thread of its own.
struct AnimationController::Reading {
    QPointer<Document> doc;
    VideoFrames* frames = nullptr;
    std::thread worker;
    std::atomic<int> done{0};
    std::atomic<int> total{0};
    std::atomic<bool> cancel{false};
    std::atomic<bool> finished{false};
    QString error;
    bool ok = false;
};

template <typename T> void AnimationController::report(const T& result) {
    if (!result && result.error() != EditError::NoChange)
        emit refused(describe(result.error()));
}

AnimationController::AnimationController(TimelinePanel* panel, ToolsSource tools, QWidget* dialogParent, QObject* parent)
    : QObject(parent), m_panel(panel), m_tools(std::move(tools)), m_parent(dialogParent), m_sound(std::make_unique<SoundPlayback>()) {
    m_playTimer.setTimerType(Qt::PreciseTimer);
    connect(&m_playTimer, &QTimer::timeout, this, &AnimationController::playTick);
    m_prepareTimer.setSingleShot(true);
    m_prepareTimer.setInterval(0);
    connect(&m_prepareTimer, &QTimer::timeout, this, [this] {
        if (m_doc && !isPlaying())
            m_sound->prepare(m_doc->animation().sounds);
    });
    m_readTimer.setInterval(100);
    connect(&m_readTimer, &QTimer::timeout, this, &AnimationController::pollReading);
    connect(panel, &TimelinePanel::settingsRequested, this, &AnimationController::showSettings);
    connect(panel, &TimelinePanel::previousRequested, this, &AnimationController::previousFrame);
    connect(panel, &TimelinePanel::nextRequested, this, &AnimationController::nextFrame);
    connect(panel, &TimelinePanel::playRequested, this, &AnimationController::togglePlay);
    connect(panel, &TimelinePanel::newFrameRequested, this, &AnimationController::newFrame);
    connect(panel, &TimelinePanel::duplicateRequested, this, [this] { duplicateFrames(); });
    connect(panel, &TimelinePanel::holdSetRequested, this, &AnimationController::setHold);
    connect(panel, &TimelinePanel::addTrackRequested, this, &AnimationController::addTrack);
    connect(panel, &TimelinePanel::deleteTrackRequested, this, &AnimationController::deleteTrack);
    connect(panel, &TimelinePanel::clearTrackRequested, this, &AnimationController::clearTrack);
    connect(panel, &TimelinePanel::trackHiddenRequested, this, &AnimationController::setTrackHidden);
    connect(panel, &TimelinePanel::trackLockedRequested, this, &AnimationController::setTrackLocked);
    connect(panel, &TimelinePanel::clearFramesRequested, this, &AnimationController::clearFrames);
    connect(panel, &TimelinePanel::soundEndRequested, this, &AnimationController::setSoundEnd);
    connect(panel, &TimelinePanel::loopRequested, this, &AnimationController::setLoop);
    connect(panel, &TimelinePanel::soundStartRequested, this, &AnimationController::setSoundStart);
    connect(panel, &TimelinePanel::deleteRequested, this, [this] { deleteFrames(); });
    connect(panel, &TimelinePanel::addSoundRequested, this, &AnimationController::addSound);
    connect(panel, &TimelinePanel::addVideoRequested, this, &AnimationController::addVideo);
    connect(panel, &TimelinePanel::removeSoundRequested, this, [this](int id) {
        if (m_doc)
            report(m_doc->removeSound(id));
    });
    connect(panel, &TimelinePanel::removeSoundsRequested, this, [this](const std::vector<int>& ids) {
        if (m_doc)
            report(m_doc->removeSounds(ids));
    });
    connect(panel, &TimelinePanel::firstRequested, this, [this] {
        stop();
        if (m_doc)
            report(m_doc->goToFrame(0));
    });
    connect(panel, &TimelinePanel::lastRequested, this, [this] {
        stop();
        if (m_doc)
            report(m_doc->goToFrame(m_doc->frameCount() - 1));
    });
    connect(panel, &TimelinePanel::duplicateTrackRequested, this, [this](int track) {
        if (m_doc)
            report(m_doc->duplicateTrack(track));
    });
    connect(panel, &TimelinePanel::exportRequested, this, &AnimationController::exportVideo);
    connect(panel, &TimelinePanel::keepOnlyRequested, this, &AnimationController::keepOnlyThisFrame);
    connect(panel, &TimelinePanel::hideRequested, this, [this] { setTimelineShown(false); });
    connect(panel, &TimelinePanel::refused, this, &AnimationController::refused);
    panel->strip()->setSoundPeaks([this](const SoundClip& clip) { return soundPeaks(clip); });
    panel->strip()->setVideoPictures([this](int index) { return laneVideoPicture(index); });
}

AnimationController::~AnimationController() {
    m_playTimer.stop();
    m_loader.reset();
    for (std::thread& t : m_measurers)
        t.join();
    if (m_reading) {
        m_reading->cancel = true;
        m_reading->worker.join();
    }
}

void AnimationController::setDocument(Document* doc) {
    if (doc == m_doc)
        return;
    stop();
    for (const QMetaObject::Connection& c : m_docConnections)
        disconnect(c);
    m_docConnections.clear();
    m_doc = doc;
    m_panel->setDocument(doc);
    if (doc) {
        m_docConnections = {connect(doc, &Document::animationChanged, this, &AnimationController::syncVideo),
                            connect(doc, &Document::animationChanged, this, &AnimationController::syncSound),
                            connect(doc, &Document::currentFrameChanged, this, [this] {
                                if (!isPlaying())
                                    showBeat(m_doc->beat());
                            })};
        if (!m_known.contains(doc)) {
            m_known.insert(doc);
            connect(doc, &QObject::destroyed, this, [this, doc] { forget(doc); });
        }
    }
    syncVideo();
    syncSound();
    emit timelineShownChanged(timelineShown());
}

bool AnimationController::timelineShown() const {
    return m_doc && m_doc->animation().view.timelineShown;
}

void AnimationController::setTimelineShown(bool shown) {
    setTimelineShown(m_doc, shown);
}

void AnimationController::setTimelineShown(Document* doc, bool shown) {
    if (!doc || doc->animation().view.timelineShown == shown)
        return;
    TimelineView view = doc->animation().view;
    view.timelineShown = shown;
    doc->setTimelineView(view);
    if (doc == m_doc)
        emit timelineShownChanged(shown);
}

std::vector<int> AnimationController::targetFrames() const {
    if (!m_doc)
        return {};
    if (timelineShown())
        return m_panel->selectedFrames();
    return {m_doc->currentFrame()};
}

void AnimationController::newFrame() {
    if (!m_doc)
        return;
    stop();
    report(m_doc->newFrame());
}

void AnimationController::duplicateFrames(bool selection) {
    if (!m_doc)
        return;
    stop();
    const auto copied = m_doc->duplicateFrames(selection ? targetFrames() : std::vector<int>{m_doc->currentFrame()});
    report(copied);
}

void AnimationController::deleteFrames(bool selection) {
    if (!m_doc)
        return;
    stop();
    report(m_doc->deleteFrames(selection ? targetFrames() : std::vector<int>{m_doc->currentFrame()}));
}

void AnimationController::changeHold(int delta) {
    if (m_doc)
        report(m_doc->changeHold(targetFrames(), delta));
}

void AnimationController::addTrack() {
    if (!m_doc)
        return;
    stop();
    report(m_doc->addTrack());
}

void AnimationController::deleteTrack(int track) {
    if (!m_doc)
        return;
    stop();
    report(m_doc->deleteTrack(track));
}

void AnimationController::clearTrack(int track) {
    if (!m_doc)
        return;
    stop();
    report(m_doc->clearTrack(track));
}

void AnimationController::setTrackHidden(int track, bool hidden) {
    if (m_doc)
        report(m_doc->setTrackHidden(track, hidden));
}

void AnimationController::setTrackLocked(int track, bool locked) {
    if (m_doc)
        report(m_doc->setTrackLocked(track, locked));
}

// Delete for the timeline: the selected clip goes, else the selected frames' drawings.
void AnimationController::clearSelected() {
    if (!m_doc)
        return;
    if (const std::vector<int> clips = m_panel->strip()->selectedSounds(); !clips.empty())
        report(m_doc->removeSounds(clips));
    else
        clearFrames();
}

// Del in the timeline: the selected frames' drawings go (all of the track's: Clear Track).
void AnimationController::clearFrames() {
    if (!m_doc)
        return;
    stop();
    report(m_doc->clearFrames(targetFrames()));
}

void AnimationController::setHold(int hold) {
    if (m_doc)
        report(m_doc->setFramesHold(targetFrames(), hold));
}

void AnimationController::setLoop(bool loop) {
    if (m_doc)
        report(m_doc->setPlayback(m_doc->animation().fps, loop));
}

void AnimationController::setSoundStart(int id, double seconds) {
    if (!m_doc)
        return;
    const std::optional<SoundClip> c = soundClip(m_doc->animation(), id);
    if (!c)
        return;
    report(m_doc->moveSound(id, static_cast<int>(std::lround(std::max(0.0, seconds) * m_doc->animation().fps)), c->track));
}

// End At: the clip stops at `seconds` from the first frame (trimming its end, or giving back what was trimmed); at least a
// beat after it starts and no later than the file's end.
void AnimationController::setSoundEnd(int id, double seconds) {
    if (!m_doc)
        return;
    const std::optional<SoundClip> found = soundClip(m_doc->animation(), id);
    if (!found)
        return;
    const SoundClip& c = *found;
    const double fps = m_doc->animation().fps;
    const double played = seconds - c.startSeconds(fps);
    if (played * fps < 1.0 - 1e-6) {
        emit refused(tr("A sound ends at least a beat after it starts (%1 s).").arg(c.startSeconds(fps) + 1.0 / fps, 0, 'f', 2));
        return;
    }
    const double trimEnd = std::max(0.0, c.seconds - c.trimStart - played);
    report(m_doc->trimSound(id, c.startBeat, c.trimStart, trimEnd));
}

bool AnimationController::keepOnlyThisFrame() {
    if (!m_doc || !m_doc->animation().isAnimation())
        return false;
    stop();
    if (!confirmKeepOnly(m_doc->frameCount(), m_doc->currentFrame()))
        return false;
    const auto kept = m_doc->keepOnlyCurrentFrame();
    report(kept);
    return kept.ok();
}

void AnimationController::previousFrame() {
    if (!m_doc)
        return;
    stop();
    const int f = m_doc->currentFrame();
    if (f > 0)
        report(m_doc->goToFrame(f - 1));
    else if (m_doc->animation().loop && m_doc->frameCount() > 1)
        report(m_doc->goToFrame(m_doc->frameCount() - 1));
}

void AnimationController::nextFrame() {
    if (!m_doc)
        return;
    stop();
    const int f = m_doc->currentFrame();
    if (f + 1 < m_doc->frameCount())
        report(m_doc->goToFrame(f + 1));
    else if (m_doc->animation().loop && m_doc->frameCount() > 1)
        report(m_doc->goToFrame(0));
}

void AnimationController::togglePlay() {
    if (isPlaying()) {
        stop();
        return;
    }
    if (!m_doc || m_doc->animation().totalBeats() < 2 || m_doc->isPainting())
        return;
    const Animation& a = m_doc->animation();
    const int first = a.playFirst(), last = a.playLast();
    if (last <= first)
        return;  // a play range of one beat
    m_playStartBeat = a.beat;
    if (a.beat < first || a.beat > last || (!a.loop && a.beat >= last))
        m_playStartBeat = first;  // outside the play range, or at its end without a loop: from its start
    m_beat = m_playStartBeat;
    (void)m_doc->goToBeat(m_playStartBeat);  // at once, not at the first tick
    m_doc->setPlaying(true);
    m_playClock.start();
    m_playTimer.start(std::max(1, static_cast<int>(1000.0 / (a.fps * a.view.speed) / 2)));
    const std::vector<SoundClip> heard = heardSounds(a);
    m_sound->play(heard, gainsOf(a, heard), a.fps, m_playStartBeat, a.view.speed);
    showBeat(m_beat);
    m_panel->setPlaying(true);
    emit playingChanged(true);
}

// Hiding or showing a sound track, muting a clip or a track's volume, while the animation plays, changes what is heard at
// once. While it doesn't play, the clips are made ready to play.
void AnimationController::syncSound() {
    if (!m_doc)
        return;
    if (!isPlaying()) {
        m_prepareTimer.start();
        return;
    }
    const std::vector<SoundClip> heard = heardSounds(m_doc->animation());
    std::vector<int> ids;
    ids.reserve(heard.size());
    for (const SoundClip& c : heard)
        ids.push_back(c.id);
    const std::vector<double> gains = gainsOf(m_doc->animation(), heard);
    if (ids != m_sound->playing())
        m_sound->play(heard, gains, m_doc->animation().fps, m_beat, m_doc->animation().view.speed);
    else
        m_sound->setGains(heard, gains);
}

const std::vector<int>& AnimationController::playingSounds() const {
    return m_sound->playing();
}

void AnimationController::stop() {
    if (!isPlaying())
        return;
    m_playTimer.stop();
    m_sound->stop();
    if (m_doc)
        m_doc->setPlaying(false);
    m_panel->setPlaying(false);
    emit playingChanged(false);
}

// The beat follows the clock, so a slow frame is skipped rather than slowing the animation down.
void AnimationController::playTick() {
    if (!m_doc || m_doc->isPainting()) {
        stop();
        return;
    }
    const Animation& a = m_doc->animation();
    int beat = m_playStartBeat + static_cast<int>(static_cast<double>(m_playClock.elapsed()) * a.fps * a.view.speed / 1000.0);
    if (beat > a.playLast()) {  // past the play range's end
        if (!a.loop) {
            stop();
            return;
        }
        m_playStartBeat = a.playFirst();
        m_playClock.start();
        beat = m_playStartBeat;
        const std::vector<SoundClip> heard = heardSounds(a);
        m_sound->play(heard, gainsOf(a, heard), a.fps, beat, a.view.speed);
    }
    if (beat == m_beat)
        return;
    m_beat = beat;
    (void)m_doc->goToBeat(beat);
    showBeat(beat);
}

// The video picture under the beat (its track's frame's) comes from the loader's thread: at once if it has read it (it reads
// ahead of the playhead), else a moment later, the previous one staying on the canvas meanwhile.
void AnimationController::showBeat(int beat) {
    m_beat = beat;
    if (!m_doc)
        return;
    VideoFrames* frames = videoFrames(m_doc);
    if (!frames) {
        m_loader.reset();
        m_doc->setVideoPicture({});
        return;
    }
    if (!m_loader || !m_loader->video().samePictures(frames->video()) || m_loader->video().lossless != frames->video().lossless ||
        m_loader->canvas() != frames->canvas()) {
        m_loader = std::make_unique<VideoPictureLoader>(
            *frames, [this](int ready) { QMetaObject::invokeMethod(this, [this, ready] { pictureReady(ready); }, Qt::QueuedConnection); });
    }
    const int index = m_doc->animation().videoPictureAt(beat);
    m_wantedPicture = index;
    if (index < 0) {
        m_doc->setVideoPicture({});
        return;
    }
    if (const auto picture = m_loader->picture(index))
        m_doc->setVideoPicture(picture->image, picture->tiles);
    m_loader->want(index);
}

void AnimationController::pictureReady(int index) {
    if (index != m_wantedPicture || !m_loader || !m_doc || !videoFrames(m_doc))
        return;
    if (const auto picture = m_loader->picture(index))
        m_doc->setVideoPicture(picture->image, picture->tiles);
}

QImage AnimationController::laneVideoPicture(int index) {
    VideoFrames* frames = videoFrames(m_doc);
    return frames ? frames->smallPicture(index, FrameStrip::kVideoLaneHeight * 2) : QImage();
}

const SoundPeaks* AnimationController::soundPeaks(const SoundClip& clip) {
    if (!clip.data)
        return nullptr;
    if (const auto it = m_peaks.find(clip.data); it != m_peaks.end())
        return it->second && !it->second->peaks.empty() ? it->second.get() : nullptr;
    const auto tools = m_tools(false);
    m_peaks[clip.data] = nullptr;  // asked: measured once, on a thread
    if (!tools)
        return nullptr;
    ++m_measuring;
    m_measurers.emplace_back([this, data = clip.data, name = clip.name, t = *tools] {
        auto read = readSoundPeaks(t, *data, name);
        auto peaks = std::make_shared<const SoundPeaks>(read ? read.value() : SoundPeaks{});
        QMetaObject::invokeMethod(
            this,
            [this, data, peaks] {
                m_peaks[data] = peaks;
                --m_measuring;
                m_panel->strip()->update();
            },
            Qt::QueuedConnection);
    });
    return nullptr;
}

void AnimationController::waitForPeaks() {
    while (m_measuring > 0)
        QApplication::processEvents(QEventLoop::AllEvents, 20);
}

VideoFrames* AnimationController::videoFrames(Document* doc) {
    if (!doc || !doc->animation().video)
        return nullptr;
    const auto it = m_videos.find(doc);
    if (it == m_videos.end() || !it->second->isCached())
        return nullptr;
    if (m_reading && m_reading->frames == it->second.get())
        return nullptr;
    return it->second.get();
}

void AnimationController::syncVideo() {
    Document* doc = m_doc;
    if (!doc)
        return;
    const auto& video = doc->animation().video;
    auto it = m_videos.find(doc);
    if (!video) {
        dropVideo(doc);
        doc->setVideoPicture({});
        m_panel->setVideoStatus({});
        return;
    }
    if (it == m_videos.end() || !sameCache(*it->second, *video, doc->size())) {
        dropVideo(doc);
        m_videos[doc] = std::make_unique<VideoFrames>(*video, doc->size(), VideoFrames::defaultCacheRoot());
    }
    if (!m_videos[doc]->isCached() && !m_reading && !m_unreadable.contains(m_videos[doc].get()))
        startReading(doc);
    if (!isPlaying())
        showBeat(doc->beat());
}

void AnimationController::startReading(Document* doc) {
    if (!QFileInfo::exists(doc->animation().video->path)) {
        m_panel->setVideoStatus(tr("The video file is missing: %1").arg(doc->animation().video->path));
        return;
    }
    const auto tools = m_tools(false);
    if (!tools) {
        m_panel->setVideoStatus(tr("FFmpeg is needed to show the video."));
        return;
    }
    m_reading = std::make_unique<Reading>();
    Reading* r = m_reading.get();
    r->doc = doc;
    r->frames = m_videos[doc].get();
    r->worker = std::thread([r, t = *tools] {
        const auto done = r->frames->extract(t, [r](int n, int total) {
            r->done = n;
            r->total = total;
            return !r->cancel.load();
        });
        r->ok = done.ok();
        if (!done)
            r->error = done.error();
        r->finished = true;
    });
    m_panel->setVideoStatus(tr("Reading the video …"));
    m_readTimer.start();
}

void AnimationController::pollReading() {
    if (!m_reading)
        return;
    Reading& r = *m_reading;
    if (!r.finished) {
        const int total = std::max(1, r.total.load());
        if (r.doc == m_doc)
            m_panel->setVideoStatus(tr("Reading the video … %1%").arg(100 * r.done.load() / total));
        return;
    }
    r.worker.join();
    m_readTimer.stop();
    const QPointer<Document> doc = r.doc;
    const QString error = r.ok ? QString() : r.error;
    // Not read again at once: syncVideo would start the same failing read over and over, and its status would hide the error.
    if (!r.ok)
        m_unreadable.insert(r.frames);
    m_reading.reset();
    if (doc == m_doc)
        m_panel->setVideoStatus(error.isEmpty() ? QString() : tr("The video could not be read: %1").arg(error));
    syncVideo();  // the current canvas's video, which may have waited for another canvas's to be read
    if (doc == m_doc)
        m_panel->strip()->setVideoPictures([this](int index) { return laneVideoPicture(index); });
}

// A reading into the document's pictures is stopped before they go: its thread writes into them.
void AnimationController::dropVideo(Document* doc) {
    const auto it = m_videos.find(doc);
    if (it == m_videos.end())
        return;
    if (m_reading && m_reading->frames == it->second.get()) {
        m_reading->cancel = true;
        m_reading->worker.join();
        m_reading.reset();
        m_readTimer.stop();
    }
    m_unreadable.erase(it->second.get());
    m_videos.erase(it);
}

void AnimationController::waitForVideo() {
    while (m_reading) {
        if (m_reading->finished)
            pollReading();
        else
            QApplication::processEvents(QEventLoop::AllEvents, 20);
    }
    if (m_loader) {
        m_loader->waitIdle();
        QApplication::processEvents();
    }
}

void AnimationController::forget(Document* doc) {
    dropVideo(doc);
    m_known.erase(doc);
}

bool AnimationController::addVideo() {
    if (!m_doc)
        return false;
    const QString file = askVideoFile();
    return !file.isEmpty() && addVideoFile(m_doc, file);
}

bool AnimationController::addVideoFile(Document* doc, const QString& file) {
    stop();
    const auto tools = m_tools(true);
    if (!tools)
        return false;
    Result<VideoInfo, QString> info = Result<VideoInfo, QString>::failure({});
    {
        WaitCursor wait;
        info = probeVideo(*tools, file);
    }
    if (!info) {
        showError(tr("Add Video"), tr("“%1” could not be used.\n\n%2").arg(QFileInfo(file).fileName(), info.error()));
        return false;
    }
    const auto choice = askAddVideo(file, info.value(), *doc);
    if (!choice)
        return false;
    VideoReference v;
    v.path = QFileInfo(file).absoluteFilePath();
    v.sourceSize = info.value().size;
    v.sourceFps = info.value().fps;
    v.seconds = info.value().seconds;
    v.hasSound = info.value().hasSound;
    v.fps = choice->follow ? choice->fps : doc->animation().fps;
    v.startBeat = choice->atCurrentFrame ? doc->animation().startBeat(doc->currentFrame()) : 0;
    v.opacity = choice->opacity;
    v.lossless = choice->lossless;
    doc->undoStack()->beginMacro(tr("Add Video"));
    const auto added = doc->setVideo(v, choice->follow);
    if (added && choice->keepSound) {
        WaitCursor wait;
        if (const auto sound = extractVideoSound(*tools, file)) {
            SoundClip clip;
            clip.name = QFileInfo(file).completeBaseName() + QStringLiteral(".wav");
            clip.data = std::make_shared<const QByteArray>(sound.value());
            clip.seconds = v.seconds;
            clip.startBeat = v.startBeat;
            report(doc->addSound(clip));
        }
    }
    doc->undoStack()->endMacro();
    report(added);
    if (added)
        setTimelineShown(doc, true);
    return added.ok();
}

void AnimationController::removeVideo() {
    if (!m_doc)
        return;
    stop();
    report(m_doc->removeVideo());
}

bool AnimationController::newFromVideo() {
    const QString file = askVideoFile();
    if (file.isEmpty())
        return false;
    const auto tools = m_tools(true);
    if (!tools)
        return false;
    Result<VideoInfo, QString> info = Result<VideoInfo, QString>::failure({});
    {
        WaitCursor wait;
        info = probeVideo(*tools, file);
    }
    if (!info) {
        showError(tr("New from Video"), tr("“%1” could not be used.\n\n%2").arg(QFileInfo(file).fileName(), info.error()));
        return false;
    }
    auto* doc = new Document(info.value().size, Qt::white);
    doc->setSourceName(QFileInfo(file).completeBaseName());
    emit documentCreated(doc);
    if (m_doc != doc)
        setDocument(doc);
    addVideoFile(doc, file);
    return true;
}

bool AnimationController::addSound() {
    if (!m_doc)
        return false;
    const QString file = askSoundFile();
    if (file.isEmpty())
        return false;
    QFile f(file);
    if (!f.open(QIODevice::ReadOnly) || f.size() > kMaxSoundBytes) {
        showError(tr("Add Sound"), tr("“%1” could not be read.").arg(QFileInfo(file).fileName()));
        return false;
    }
    const QByteArray bytes = f.readAll();
    const auto tools = m_tools(true);
    if (!tools)
        return false;
    Result<double, QString> length = Result<double, QString>::failure({});
    {
        WaitCursor wait;
        length = probeSoundDuration(*tools, file);
    }
    if (!length) {
        showError(tr("Add Sound"), tr("“%1” could not be used as a sound.\n\n%2").arg(QFileInfo(file).fileName(), length.error()));
        return false;
    }
    return addSoundFile(file, bytes, length.value());
}

bool AnimationController::addSoundFile(const QString& file, const QByteArray& bytes, double seconds) {
    if (!m_doc)
        return false;
    SoundClip clip;
    clip.name = QFileInfo(file).fileName();
    clip.data = std::make_shared<const QByteArray>(bytes);
    clip.seconds = seconds;
    clip.startBeat = m_doc->animation().startBeat(m_doc->currentFrame());
    const auto added = m_doc->addSound(clip);
    report(added);
    return added.ok();
}

bool AnimationController::exportVideo() {
    if (!m_doc)
        return false;
    stop();
    const auto ffmpeg = m_tools(true);
    if (!ffmpeg)
        return false;
    const QString encoder = chooseVideoEncoder(*ffmpeg);
    if (encoder.isEmpty()) {
        showError(tr("Export Video"), tr("This FFmpeg (%1) has no H.264 or MPEG-4 encoder.").arg(ffmpeg->ffmpeg));
        return false;
    }
    const QString base = QFileInfo(m_doc->displayName()).completeBaseName();
    const QString dir = !m_doc->filePath().isEmpty() ? QFileInfo(m_doc->filePath()).absolutePath()
                                                     : QStandardPaths::writableLocation(QStandardPaths::MoviesLocation);
    QString path = askExportPath(QDir(dir.isEmpty() ? QDir::homePath() : dir).filePath(base + QStringLiteral(".mp4")));
    if (path.isEmpty())
        return false;
    if (QFileInfo(path).suffix().compare(QLatin1String("mp4"), Qt::CaseInsensitive) != 0)
        path += QStringLiteral(".mp4");

    const DocumentState state = m_doc->state();  // tiles are shared and never change: safe to read on the worker
    const AnimationExportPlan plan = planAnimationExport(state.animation, m_doc->size());
    QTemporaryDir soundDir;
    const std::vector<SoundClip> heard = heardSounds(state.animation);
    const AudioPlan audio = soundClipsPlan(heard, gainsOf(state.animation, heard), state.animation.fps, plan.seconds(), soundDir.path());
    EncodeSettings settings;
    settings.output = path;
    std::atomic<int> done{0};
    std::atomic<bool> cancel{false};
    std::atomic<bool> finished{false};
    AnimationExportResult result;
    const FfmpegTools& tools = *ffmpeg;
    std::thread worker([&] {
        result = exportAnimation(
            tools, plan, settings, audio, [&](int beat) { return animation_ops::exportedBeat(state, beat); },
            [&](int n, int) {
                done = n;
                return !cancel.load();
            });
        finished = true;
    });
    const int total = static_cast<int>(plan.beats.size());
    QProgressDialog progress(tr("Writing the video …"), tr("Cancel"), 0, total, m_parent);
    progress.setWindowTitle(tr("Export Video"));
    progress.setWindowModality(Qt::WindowModal);
    progress.setMinimumDuration(300);
    while (!finished) {
        progress.setValue(std::min(done.load(), std::max(0, total - 1)));
        if (progress.wasCanceled())
            cancel = true;
        QApplication::processEvents(QEventLoop::AllEvents, 30);
        std::this_thread::sleep_for(std::chrono::milliseconds(15));
    }
    worker.join();
    progress.reset();
    if (result.cancelled) {
        emit message(tr("Export cancelled"));
        return false;
    }
    if (!result.ok) {
        showError(tr("Export Video"), tr("The export failed.\n\n%1").arg(result.error));
        return false;
    }
    emit message(tr("Exported “%1”").arg(QFileInfo(path).fileName()));
    return true;
}

void AnimationController::showSettings() {
    if (!m_doc)
        return;
    TimelineSettingsDialog dialog(m_doc->animation(), m_parent);
    if (dialog.exec() != QDialog::Accepted)
        return;
    report(m_doc->setPlayback(dialog.fps(), m_doc->animation().loop));
    m_doc->setTimelineView(dialog.view());
}

bool AnimationController::confirmKeepOnly(int frameCount, int currentFrame) {
    QMessageBox box(QMessageBox::Question, tr("Keep Only This Frame"), keepOnlyQuestion(frameCount, currentFrame), QMessageBox::NoButton,
                    m_parent);
    QPushButton* keep = box.addButton(keepOnlyButtonText(currentFrame), QMessageBox::AcceptRole);
    box.addButton(QMessageBox::Cancel);
    box.setDefaultButton(keep);
    box.exec();
    return box.clickedButton() == keep;
}

QString AnimationController::askVideoFile() {
    return QFileDialog::getOpenFileName(m_parent, tr("Choose a Video"), QStandardPaths::writableLocation(QStandardPaths::MoviesLocation),
                                        tr("Videos (*.mp4 *.mov *.m4v *.mkv *.webm *.avi *.gif);;All files (*)"));
}

std::optional<AddVideoChoice> AnimationController::askAddVideo(const QString& file, const VideoInfo& info, const Document& doc) {
    AddVideoDialog dialog(QFileInfo(file).fileName(), info, doc.animation(), doc.size(), m_parent);
    if (dialog.exec() != QDialog::Accepted)
        return std::nullopt;
    return dialog.choice();
}

QString AnimationController::askSoundFile() {
    return QFileDialog::getOpenFileName(m_parent, tr("Choose a Sound"), QStandardPaths::writableLocation(QStandardPaths::MusicLocation),
                                        tr("Sounds (*.wav *.mp3 *.ogg *.oga *.flac *.m4a *.aac *.opus);;All files (*)"));
}

QString AnimationController::askExportPath(const QString& suggested) {
    return QFileDialog::getSaveFileName(m_parent, tr("Export Video"), suggested, tr("MP4 video (*.mp4)"));
}

void AnimationController::showError(const QString& title, const QString& text) {
    QMessageBox::warning(m_parent, title, text);
}

}
