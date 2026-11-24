#include "ui/animation/FrameStrip.h"

#include "core/animation/AnimationOps.h"
#include "core/animation/AnimationVideo.h"
#include "core/document/Document.h"
#include "ui/Theme.h"
#include "ui/animation/TimeRuler.h"
#include "ui/animation/TrackLabels.h"

#include <QApplication>
#include <QContextMenuEvent>
#include <QDialog>
#include <QDateTime>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>

#include <algorithm>
#include <cmath>
#include <numeric>

namespace doodlebold {

namespace {

using Theme::Token;

constexpr int kVideoPicturesPerTurn = 4;
constexpr double kHiddenOpacity = 0.35;
// The frames' outlines, px: the current frame's, a selected frame's, and a frame its track shows at the playhead. The
// redesign asked for stronger selection borders: they were 2, 1 and 1, and a selected frame looked like any other.
constexpr double kCurrentBorder = 3.0;  // kept at 3's request above, a step past Theme::kBorderStrong (component metric)
constexpr double kSelectedBorder = Theme::kBorderStrong;
constexpr double kShownBorder = Theme::kBorder;
// The playhead's line across the lanes, px, and its band's alpha over the playhead's beat (0-255) with several tracks. With
// one track no line was drawn, and a more visible playhead was asked for; the band's 36 is as it was.
constexpr double kPlayheadWidth = Theme::kBorderStrong;
constexpr int kPlayheadBandAlpha = 36;  // component metric, described above
// A sound lane's own ground, alpha over the timeline's (0-255), so the lane reads as one even where it has no clip.
constexpr int kSoundLaneAlpha = 60;  // component metric, described above
// A clip shorter than this, px (a folded lane's), shows only its body: no waveform, name or trim handles.
constexpr int kClipDetailHeight = 18;  // component metric, described above
// A folded lane's frames and clips, px: kFoldedInset from the lane's top and bottom (kClipInset's 3 would leave a hairline of
// kCollapsedLaneHeight), with corners of kFoldedRadius, a thin bar's.
constexpr int kFoldedInset = 1;
constexpr double kFoldedRadius = Theme::kRadiusXs;
// A labelled track's lane tint, alpha 0-255: enough to tell the colour, faint enough not to colour the drawings' white. By
// eye: 34 read as a coloured lane rather than a hint of one; 18 was faint beside a row that is its colour whole (feedback:
// "color the entire block"), so 26, to match it.
constexpr int kLabelTintAlpha = 26;  // component metric, described above
// The dark ground of a hold's ×N chip on a thumbnail and its text: readable on any drawing, so not themed (data colours).
constexpr QRgb kChipGround = qRgba(30, 30, 34, 200);
constexpr QRgb kChipText = 0xffffffff;
// The faint line between a held frame's repeated pictures, over the drawing: black at this alpha (component metric).
constexpr int kHoldLineAlpha = 40;
// A clip's name chip over its waveform: black at these alphas, selected and not (component metrics).
constexpr int kClipChipAlpha = 90;  // component metric, described above
constexpr int kClipChipAlphaIdle = 60;  // component metric, described above

int nextMergeKey() {
    static int key = 0x4e1d0000;
    return ++key;
}

quint64 mix(quint64 h, quint64 v) {
    return (h ^ v) * 0x100000001b3ULL;
}

quint64 hashNode(quint64 h, const LayerNode& n) {
    const LayerProps& p = n.props;
    h = mix(h, p.id.value());
    h = mix(h, quint64(p.opacity) << 8 | quint64(p.visible) << 1 | quint64(p.clipping));
    h = mix(h, quint64(p.mode));
    for (const auto& [coord, tile] : n.pixels.tiles())
        h = mix(h, reinterpret_cast<quintptr>(tile.get()) ^ (quint64(quint32(coord.x)) << 32 | quint32(coord.y)));
    if (n.mask)
        h = mix(h, quint64(n.mask->tiles().size()) | quint64(p.maskEnabled) << 40);
    for (const LayerNode& c : n.children)
        h = hashNode(h, c);
    return h;
}

bool isDelete(const QKeyEvent* e) {
    return e->key() == Qt::Key_Delete || e->key() == Qt::Key_Backspace;
}

bool isPlay(const QKeyEvent* e) {
    return e->key() == Qt::Key_Space && e->modifiers() == Qt::NoModifier;
}

bool isPress(QEvent::Type type) {
    return type == QEvent::MouseButtonPress || type == QEvent::MouseButtonDblClick || type == QEvent::TabletPress ||
           type == QEvent::TouchBegin;
}

// A menu or a dialog belongs to what opened it: a press there doesn't say what was used last.
bool inMenuOrDialog(const QWidget* w) {
    const QWidget* window = w->window();
    return window->windowType() == Qt::Popup || qobject_cast<const QDialog*>(window) != nullptr;
}

// The part of `image` that fills `target` keeping its proportions, cut from its middle.
QRectF coverSource(const QImage& image, const QRect& target) {
    const double scale = std::max(double(target.width()) / image.width(), double(target.height()) / image.height());
    const QSizeF size(target.width() / scale, target.height() / scale);
    return QRectF(QPointF((image.width() - size.width()) / 2, (image.height() - size.height()) / 2), size);
}

int frameIdOf(const Animation& a, int track, int frame) {
    if (track < 0 || track >= a.trackCount())
        return 0;
    const AnimationTrack& t = a.tracks[static_cast<std::size_t>(track)];
    return frame >= 0 && frame < t.frameCount() ? t.frames[static_cast<std::size_t>(frame)].id : 0;
}

bool trackHidden(const Animation& a, int track) {
    return track >= 0 && track < a.trackCount() && a.tracks[static_cast<std::size_t>(track)].hidden;
}

}

FrameStrip::FrameStrip(QWidget* parent) : QWidget(parent) {
    setObjectName(QStringLiteral("frameStrip"));
    setMouseTracking(true);
    setFocusPolicy(Qt::ClickFocus);
    m_thumbTimer.setSingleShot(true);
    m_thumbTimer.setInterval(0);
    connect(&m_thumbTimer, &QTimer::timeout, this, &FrameStrip::makeNextThumbnail);
    // While pictures are read, the loading signs turn; it stops when nothing is left to read.
    m_spinTimer.setInterval(60);
    connect(&m_spinTimer, &QTimer::timeout, this, [this] {
        if (m_wanted.empty() && m_wantedVideo.empty())
            m_spinTimer.stop();
        update();
    });
    placeFrames();
    qApp->installEventFilter(this);
}

bool FrameStrip::eventFilter(QObject* watched, QEvent* event) {
    if (isPress(event->type()) && watched->isWidgetType())
        noteUse(static_cast<QWidget*>(watched));
    return QWidget::eventFilter(watched, event);
}

// Which was used last, the timeline or the rest of the window, follows every press: the mouse's, and the pen's and a finger's,
// which reach the canvas without a mouse press (with only the mouse's, a pen's stroke on the canvas left Delete with the
// timeline). A press anywhere in the timeline gives it the keyboard too, for Space and Delete,
// as its buttons don't take it.
void FrameStrip::noteUse(QWidget* w) {
    QWidget* scope = m_scope ? m_scope.data() : this;
    if (!isVisible() || inMenuOrDialog(w))
        return;
    m_selectionActive = w == scope || scope->isAncestorOf(w);
    // A part of the timeline that takes the keyboard itself (the track rows, for Del on their tracks) keeps it.
    const bool takesKeys = w != this && (w->focusPolicy() & Qt::ClickFocus);
    if (m_selectionActive && !takesKeys && QApplication::focusWidget() != this && !scope->isAncestorOf(QApplication::focusWidget()))
        setFocus(Qt::MouseFocusReason);
}

void FrameStrip::hideEvent(QHideEvent* event) {
    m_selectionActive = false;
    QWidget::hideEvent(event);
}

FrameStrip::~FrameStrip() {
    if (m_thumbWorker.joinable())
        m_thumbWorker.join();
}

void FrameStrip::setDocument(Document* doc) {
    if (m_doc == doc)
        return;
    if (m_doc)
        m_doc->disconnect(this);
    m_doc = doc;
    ++m_thumbGeneration;
    m_thumbs.clear();
    m_videoThumbs.clear();
    m_thumbsVideo = doc ? doc->animation().video : std::nullopt;
    m_selected.clear();
    m_selectedSounds.clear();
    m_selectionActive = false;  // the timeline now shows another canvas
    m_wanted.clear();
    m_wantedSet.clear();
    m_wantedVideo.clear();
    if (doc) {
        connect(doc, &Document::animationChanged, this, [this] {
            placeFrames();
            pruneSelection();
            relayout();
            videoPicturesChanged();
        });
        connect(doc, &Document::currentFrameChanged, this, [this] {
            pruneSelection();
            const int id = m_doc->animation().frameId(m_doc->currentFrame());
            if (!m_selected.contains(id)) {
                selectFrames({m_doc->currentFrame()});
                m_framesPicked = false;
            }
            update();
        });
        // The current frame's thumbnail follows the painting, a moment after it.
        connect(doc, &Document::imageChanged, this, [this] {
            if (!m_thumbTimer.isActive()) {
                m_thumbTimer.setInterval(kThumbDelayMs);
                m_thumbTimer.start();
            }
        });
    }
    placeFrames();
    pruneSelection();
    relayout();
}

void FrameStrip::setVideoPictures(std::function<QImage(int index)> pictures) {
    m_videoPictures = std::move(pictures);
    m_videoThumbs.clear();
    m_wantedVideo.clear();
    update();
}

void FrameStrip::videoPicturesChanged() {
    const auto& video = m_doc ? m_doc->animation().video : std::optional<VideoReference>{};
    if (video != m_thumbsVideo) {
        m_videoThumbs.clear();
        m_thumbsVideo = video;
    }
    update(videoLaneRect());
}

void FrameStrip::setSoundPeaks(PeaksSource peaks) {
    m_peaks = std::move(peaks);
    update();
}

void FrameStrip::setLaneScale(int percent) {
    percent = std::clamp(percent, kMinLaneScale, 100);
    if (percent == m_laneScale)
        return;
    m_laneScale = percent;
    placeFrames();
    relayout();
}

int FrameStrip::cellHeight() const {
    return kCellHeight * m_laneScale / 100;
}

// The beat numbers under the frames, left out when the lanes are made small.
int FrameStrip::labelHeight() const {
    return m_laneScale >= 70 ? kLabelHeight : 0;
}

int FrameStrip::mediaHeight() const {
    return std::max(22, kVideoLaneHeight * m_laneScale / 100);
}

void FrameStrip::setBeatWidth(int px) {
    px = std::clamp(px, kMinBeatWidth, kMaxBeatWidth);
    if (px == m_beatWidth)
        return;
    m_beatWidth = px;
    relayout();
}

int FrameStrip::fitBeatWidth(int width) const {
    const int beats = m_doc ? m_doc->animation().totalBeats() : 1;
    return std::clamp((width - 2 * kMargin) / std::max(1, beats), kMinBeatWidth, kMaxBeatWidth);
}

std::vector<int> FrameStrip::selectedFrames() const {
    std::vector<int> out;
    if (!m_doc)
        return out;
    const Animation& a = m_doc->animation();
    for (int i = 0; i < a.frameCount(); ++i)
        if (m_selected.contains(a.frameId(i)))
            out.push_back(i);
    if (out.empty())
        out.push_back(m_doc->currentFrame());
    return out;
}

std::vector<FrameStrip::Cell> FrameStrip::selectedCells() const {
    std::vector<Cell> out;
    if (!m_doc)
        return out;
    const Animation& a = m_doc->animation();
    for (int t = 0; t < a.trackCount(); ++t)
        for (int i = 0; i < a.tracks[static_cast<std::size_t>(t)].frameCount(); ++i)
            if (m_selected.contains(frameIdOf(a, t, i)))
                out.push_back({t, i});
    if (out.empty())
        out.push_back({currentTrack(), m_doc->currentFrame()});
    return out;
}

void FrameStrip::selectFrames(const std::vector<int>& frames) {
    m_framesPicked = true;
    m_selected.clear();
    if (m_doc)
        for (int f : frames)
            m_selected.insert(m_doc->animation().frameId(f));
    update();
    emit selectionChanged();
}

void FrameStrip::selectAllFrames() {
    if (!m_doc)
        return;
    std::vector<int> all(static_cast<std::size_t>(m_doc->frameCount()));
    std::iota(all.begin(), all.end(), 0);
    selectFrames(all);
}

std::vector<int> FrameStrip::selectedSounds() const {
    std::vector<int> out;
    if (m_doc)
        for (const SoundClip& c : m_doc->animation().sounds)
            if (m_selectedSounds.contains(c.id))
                out.push_back(c.id);
    return out;
}

void FrameStrip::selectSounds(std::set<int> ids) {
    if (ids == m_selectedSounds)
        return;
    m_selectedSounds = std::move(ids);
    update();
    emit soundSelectionChanged();
}

// Frames that are gone are let go; with none left, the current frame is selected.
void FrameStrip::pruneSelection() {
    if (!m_doc)
        return;
    const Animation& a = m_doc->animation();
    std::set<int> alive;
    for (const AnimationTrack& t : a.tracks)
        for (const AnimationFrame& f : t.frames)
            alive.insert(f.id);
    std::erase_if(m_selected, [&](int id) { return !alive.contains(id); });
    if (m_selected.empty() || !a.isAnimation()) {
        m_selected = {a.frameId(a.currentFrame())};
        m_framesPicked = false;
    }
    std::set<int> sounds = m_selectedSounds;
    std::erase_if(sounds, [this](int id) { return !sound(id); });
    selectSounds(std::move(sounds));
    emit selectionChanged();
}

int FrameStrip::trackCount() const {
    return m_doc ? std::max(1, m_doc->animation().trackCount()) : 1;
}

int FrameStrip::currentTrack() const {
    return m_doc && !m_doc->animation().tracks.empty() ? m_doc->animation().currentTrack : 0;
}

void FrameStrip::placeFrames() {
    const int count = trackCount();
    m_starts.assign(static_cast<std::size_t>(count), std::vector<int>{0, 1});
    if (m_doc && !m_doc->animation().tracks.empty()) {
        const Animation& a = m_doc->animation();
        for (std::size_t t = 0; t < a.tracks.size(); ++t) {
            std::vector<int>& s = m_starts[t];
            s.assign(1, 0);
            s.reserve(a.tracks[t].frames.size() + 1);
            for (const AnimationFrame& f : a.tracks[t].frames)
                s.push_back(s.back() + f.hold);
        }
    }
    // The top track first, down to the bottom one.
    m_laneTops.assign(static_cast<std::size_t>(count), 0);
    int y = kMargin;
    for (int t = count - 1; t >= 0; --t) {
        m_laneTops[static_cast<std::size_t>(t)] = y;
        y += laneHeight(t) + kTrackGap;
    }
    emit layoutChanged();
}

bool FrameStrip::hasCells(int track) const {
    return kindOf(track) != TrackKind::Sound;
}

TrackKind FrameStrip::kindOf(int track) const {
    if (!m_doc || track < 0 || track >= m_doc->animation().trackCount())
        return TrackKind::Frames;
    return m_doc->animation().tracks[static_cast<std::size_t>(track)].kind;
}

int FrameStrip::laneHeight(int track) const {
    if (collapsed(track))
        return kCollapsedLaneHeight;
    return kindOf(track) == TrackKind::Frames ? cellHeight() + labelHeight() : mediaHeight();
}

void FrameStrip::setShowTime(bool asTime) {
    m_showTime = asTime;
    update();
}

bool FrameStrip::collapsed(int track) const {
    if (!m_doc || track < 0 || track >= m_doc->animation().trackCount())
        return false;
    return m_doc->animation().view.collapsed.contains(m_doc->animation().tracks[static_cast<std::size_t>(track)].id);
}

int FrameStrip::snapBeat(int beat, const std::set<int>& skipClips, int skipTrack) const {
    return snapTarget(beat, skipClips, skipTrack).value_or(beat);
}

// Moves and holds go by whole beats, so snapping changes them only where kSnapDistance is more than half a beat: zoomed out,
// where a beat is too narrow to hit. A trimmed end goes to the pixel, and snaps at any zoom.
std::optional<int> FrameStrip::snapTarget(double beat, const std::set<int>& skipClips, int skipTrack) const {
    if (!m_doc)
        return std::nullopt;
    const Animation& a = m_doc->animation();
    std::optional<int> best;
    double nearest = double(kSnapDistance) / m_beatWidth;  // in beats
    const auto consider = [&](int target) {
        if (const double d = std::abs(target - beat); d <= nearest) {
            nearest = d;
            best = target;
        }
    };
    for (int t = 0; t < static_cast<int>(m_starts.size()); ++t)
        if (hasCells(t) && t != skipTrack)
            for (int start : m_starts[static_cast<std::size_t>(t)])
                consider(start);
    for (const SoundClip& c : a.sounds) {
        if (skipClips.contains(c.id))
            continue;
        consider(c.startBeat);
        consider(static_cast<int>(std::lround(c.endSeconds(a.fps) * a.fps)));
    }
    consider(a.beat);
    return best;
}

QRect FrameStrip::slotRect(int track) const {
    return QRect(0, laneTop(track) - kTrackGap / 2, width(), laneHeight(track) + kTrackGap);
}

const std::vector<int>& FrameStrip::starts(int track) const {
    return m_starts[static_cast<std::size_t>(std::clamp(track, 0, static_cast<int>(m_starts.size()) - 1))];
}

std::pair<int, int> FrameStrip::framesBetween(int track, int x0, int x1) const {
    const std::vector<int>& s = starts(track);
    const int count = static_cast<int>(s.size()) - 1;
    const auto frameOf = [&](int x) {
        const int beat = static_cast<int>(std::floor(double(x - kMargin) / m_beatWidth));
        return static_cast<int>(std::ranges::upper_bound(s, beat) - s.begin()) - 1;
    };
    return {std::max(0, frameOf(x0)), std::min(count - 1, frameOf(x1))};
}

int FrameStrip::laneTop(int track) const {
    if (track < 0 || track >= static_cast<int>(m_laneTops.size()))
        return kMargin;
    return m_laneTops[static_cast<std::size_t>(track)];
}

int FrameStrip::trackAtY(int y) const {
    for (int t = 0; t < static_cast<int>(m_laneTops.size()); ++t) {
        const QRect slot = slotRect(t);
        if (y >= slot.top() && y <= slot.bottom())
            return t;
    }
    return -1;
}

QSize FrameStrip::sizeHint() const {
    int beats = 1;
    for (const std::vector<int>& s : m_starts)
        beats = std::max(beats, s.back());
    int right = xOfBeat(beats + 1);
    if (m_doc)
        for (const SoundClip& c : m_doc->animation().sounds)
            right = std::max(right, soundRect(c.id).right() + kMargin);
    const int bottom = m_laneTops.empty() ? kMargin : laneTop(0) + laneHeight(0);
    return {right + kMargin, bottom + kMargin};
}

QSize FrameStrip::minimumSizeHint() const {
    return {kMargin * 2 + m_beatWidth, kMargin * 2 + cellHeight()};
}

void FrameStrip::relayout() {
    resize(sizeHint().expandedTo(parentWidget() ? parentWidget()->size() : QSize()));
    updateGeometry();
    update();
    emit layoutChanged();
}

int FrameStrip::cellHeightOf(int track) const {
    if (collapsed(track))
        return kCollapsedLaneHeight;
    return kindOf(track) == TrackKind::Video ? mediaHeight() - 2 * kClipInset : cellHeight();
}

QRect FrameStrip::cellRect(int track, int frame) const {
    const std::vector<int>& s = starts(track);
    if (!m_doc || frame < 0 || frame + 1 >= static_cast<int>(s.size()))
        return {};
    const auto f = static_cast<std::size_t>(frame);
    const int gap = m_beatWidth >= 24 ? kGap : 1;
    const int width = (s[f + 1] - s[f]) * m_beatWidth - gap;
    const int top = laneTop(track) + (kindOf(track) == TrackKind::Video && !collapsed(track) ? kClipInset : 0);
    return QRect(xOfBeat(s[f]), top, std::max(2, width), cellHeightOf(track));
}

QRect FrameStrip::cellRect(int frame) const {
    return cellRect(currentTrack(), frame);
}

QRect FrameStrip::labelRect(int track, int frame) const {
    const QRect cell = cellRect(track, frame);
    return QRect(cell.left(), cell.bottom() + 1, cell.width(), collapsed(track) ? 0 : labelHeight());
}

QRect FrameStrip::labelRect(int frame) const {
    return labelRect(currentTrack(), frame);
}

QRect FrameStrip::videoLaneRect() const {
    const int track = m_doc ? m_doc->animation().videoTrack() : -1;
    if (track < 0)
        return {};
    return QRect(kMargin, laneTop(track), std::max(0, width() - 2 * kMargin), laneHeight(track));
}

const SoundClip* FrameStrip::sound(int id) const {
    if (!m_doc)
        return nullptr;
    for (const SoundClip& c : m_doc->animation().sounds)
        if (c.id == id)
            return &c;
    return nullptr;
}

QRect FrameStrip::soundRect(int id) const {
    const SoundClip* c = sound(id);
    if (!c)
        return {};
    const Animation& a = m_doc->animation();
    const int track = a.indexOfTrack(c->track);
    const int w = std::max(10, static_cast<int>(std::lround(c->playedSeconds() * a.fps * m_beatWidth)) - 2);
    const int inset = collapsed(track) ? kFoldedInset : kClipInset;
    return QRect(xOfBeat(c->startBeat), laneTop(track) + inset, w, laneHeight(track) - 2 * inset);
}

FrameStrip::Cell FrameStrip::cellAt(const QPoint& pos) const {
    const int track = m_doc ? trackAtY(pos.y()) : -1;
    if (track < 0 || !hasCells(track))
        return {};
    const auto [first, last] = framesBetween(track, pos.x(), pos.x());
    if (first > last || !cellRect(track, first).adjusted(0, 0, kGap, 0).contains(pos))
        return {};
    return {track, first};
}

bool FrameStrip::onRightEdge(int track, int frame, const QPoint& pos) const {
    const QRect r = cellRect(track, frame);
    return frame >= 0 && pos.y() >= r.top() && pos.y() <= r.bottom() && std::abs(pos.x() - r.right()) <= kEdgeGrip;
}

bool FrameStrip::onRightEdge(int frame, const QPoint& pos) const {
    return onRightEdge(currentTrack(), frame, pos);
}

int FrameStrip::soundAt(const QPoint& pos) const {
    if (!m_doc)
        return 0;
    for (const SoundClip& c : m_doc->animation().sounds)
        if (soundRect(c.id).contains(pos))
            return c.id;
    return 0;
}

FrameStrip::SoundPart FrameStrip::soundPartAt(int id, const QPoint& pos) const {
    const QRect r = soundRect(id);
    if (!r.contains(pos))
        return SoundPart::None;
    if (r.width() < 4 * kEdgeGrip || r.height() < kClipDetailHeight)
        return SoundPart::Body;
    if (pos.x() - r.left() < kEdgeGrip)
        return SoundPart::Start;
    if (r.right() - pos.x() < kEdgeGrip)
        return SoundPart::End;
    return SoundPart::Body;
}

int FrameStrip::dropIndex(int track, int x, const std::vector<int>& moving) const {
    if (!m_doc)
        return 0;
    // The frames that stay, up to the one whose middle is past x.
    const int count = static_cast<int>(starts(track).size()) - 1;
    const auto [first, last] = framesBetween(track, x, x);
    int upTo = first > last ? (x < kMargin ? 0 : count) : first;
    if (upTo < count && x >= cellRect(track, upTo).center().x())
        ++upTo;
    const auto movingBefore = std::ranges::lower_bound(moving, upTo) - moving.begin();
    return upTo - static_cast<int>(movingBefore);
}

bool FrameStrip::event(QEvent* event) {
    // Ctrl+A and Space belong to the timeline while it has the keyboard, not to the menus, and so does Delete while the
    // timeline was used last; after a press elsewhere Delete goes to the window's Clear, as it does wherever the keyboard is.
    if (event->type() == QEvent::ShortcutOverride) {
        const auto* key = static_cast<QKeyEvent*>(event);
        if (key->matches(QKeySequence::SelectAll) || isPlay(key) || (isDelete(key) && m_selectionActive)) {
            event->accept();
            return true;
        }
    }
    return QWidget::event(event);
}

void FrameStrip::keyPressEvent(QKeyEvent* event) {
    if (event->matches(QKeySequence::SelectAll)) {
        m_selectionActive = true;
        selectAllFrames();
    } else if (isPlay(event)) {
        if (!event->isAutoRepeat())
            emit playRequested();
    } else if (!m_selectedSounds.empty() && isDelete(event) && m_selectionActive) {
        emit removeSoundsRequested(selectedSounds());
    } else if (isDelete(event) && m_selectionActive) {
        emit clearFramesRequested();
    } else {
        QWidget::keyPressEvent(event);
    }
}

void FrameStrip::mousePressEvent(QMouseEvent* event) {
    if (!m_doc || event->button() != Qt::LeftButton)
        return;
    m_selectionActive = true;
    const QPoint pos = event->position().toPoint();
    const Qt::KeyboardModifiers mods = event->modifiers();
    m_pressPos = m_dragPos = pos;
    m_mergeKey = nextMergeKey();
    m_dragFrames = false;
    m_dragGroup.clear();
    m_dragSound = 0;
    m_dragTrackShift = 0;
    if (const int id = soundAt(pos)) {
        pressSound(id, soundPartAt(id, pos), mods);
        return;
    }
    const Cell cell = cellAt(pos);
    m_dragCell = cell;
    if (cell.valid() && onRightEdge(cell.track, cell.frame, pos)) {
        selectSound(0);
        makeTrackCurrent(cell.track);
        m_drag = Drag::Hold;
    } else if (cell.valid()) {
        pressFrame(cell, mods);
    } else {
        // A box picks frames and clips on every track; with Ctrl, added to those selected.
        makeTrackCurrent(trackAtY(pos.y()));
        m_drag = Drag::Box;
        const bool add = mods & Qt::ControlModifier;
        m_boxBase = add && m_framesPicked ? m_selected : std::set<int>{};
        m_boxBaseSounds = add ? m_selectedSounds : std::set<int>{};
        if (!add)
            selectSound(0);
    }
}

// A plain press on a selected frame, while more than it is selected (frames of any track, clips), drags all of them: a click
// without a drag leaves it alone selected, on the release. Ctrl+click and Shift+click keep the clips selected.
void FrameStrip::pressFrame(Cell cell, Qt::KeyboardModifiers mods) {
    const int id = frameIdOf(m_doc->animation(), cell.track, cell.frame);
    const bool group = mods == Qt::NoModifier && m_framesPicked && m_selected.contains(id) && groupChosen();
    if (mods == Qt::NoModifier && !group)
        selectSound(0);
    clickFrame(cell, mods);
    m_drag = mods == Qt::NoModifier ? Drag::Pending : Drag::None;
    m_dragFrames = true;
    if (group)
        startClipGroup();
}

bool FrameStrip::groupChosen() const {
    return (m_framesPicked ? m_selected.size() : 0) + m_selectedSounds.size() > 1;
}

// The selected clips where the drag starts, to move them by as much as it goes.
void FrameStrip::startClipGroup() {
    m_dragGroup.clear();
    m_dragTrackShift = 0;
    for (const int chosen : selectedSounds())
        if (const SoundClip* o = sound(chosen))
            m_dragGroup.push_back({o->id, o->startBeat, o->track});
}

// The current frame stays selected, as it always is, but not picked: it doesn't go with the clips.
void FrameStrip::unpickFrames() {
    if (!m_doc)
        return;
    m_selected = {m_doc->animation().frameId(m_doc->currentFrame())};
    m_framesPicked = false;
    update();
    emit selectionChanged();
}

// Ctrl+click adds or removes a clip and Shift+click adds one, without dragging; the frames picked stay. A plain press on a
// selected clip, while more is selected, drags all of it (a click without a drag leaves it alone selected, on the release);
// on any other clip, or on a clip's end, it is that clip alone.
void FrameStrip::pressSound(int id, SoundPart part, Qt::KeyboardModifiers mods) {
    if (mods & (Qt::ControlModifier | Qt::ShiftModifier)) {
        std::set<int> ids = m_selectedSounds;
        if ((mods & Qt::ControlModifier) && ids.contains(id))
            ids.erase(id);
        else
            ids.insert(id);
        selectSounds(std::move(ids));
        m_drag = Drag::None;
        return;
    }
    const bool group = part == SoundPart::Body && m_selectedSounds.contains(id) && groupChosen();
    if (!group) {
        selectSound(id);
        unpickFrames();
    }
    const SoundClip* c = sound(id);
    m_dragSound = id;
    m_dragStartBeat = c->startBeat;
    m_dragTrimStart = c->trimStart;
    m_dragTrimEnd = c->trimEnd;
    startClipGroup();
    m_dragFrames = group && m_framesPicked;
    if (group)
        m_drag = Drag::Sounds;
    else
        m_drag = part == SoundPart::Start ? Drag::TrimStart : part == SoundPart::End ? Drag::TrimEnd : Drag::Sound;
}

// Another track becomes the current one without moving the playhead.
void FrameStrip::makeTrackCurrent(int track) {
    if (track < 0 || track == currentTrack() || !m_doc || m_doc->animation().tracks.empty() || !hasCells(track))
        return;
    const int frame = m_doc->animation().tracks[static_cast<std::size_t>(track)].frameAtBeat(m_doc->beat());
    if (const auto went = m_doc->goToCell(track, frame); !went && went.error() != EditError::NoChange)
        emit refused(describe(went.error()));
}

// A plain click on a frame selects it alone, unless it is selected already (a drag may follow); Ctrl+click adds or removes a
// frame of any track; Shift+click selects the frames from the last one clicked up to it, along their track (another track's:
// it is added). The frame's track becomes the current one and the playhead goes to it.
void FrameStrip::clickFrame(Cell cell, Qt::KeyboardModifiers mods) {
    const Animation& a = m_doc->animation();
    const int id = frameIdOf(a, cell.track, cell.frame);
    if (!m_framesPicked && (mods & (Qt::ControlModifier | Qt::ShiftModifier)))
        m_selected.clear();  // the current frame was selected, not picked
    if (mods & Qt::ControlModifier) {
        if (m_selected.contains(id) && m_selected.size() > 1)
            m_selected.erase(id);
        else
            m_selected.insert(id);
    } else if (mods & Qt::ShiftModifier) {
        const int from = a.tracks.empty() ? -1 : a.tracks[static_cast<std::size_t>(cell.track)].indexOfFrame(m_anchor);
        if (from < 0) {
            m_selected.insert(id);
        } else {
            m_selected.clear();
            for (int i = std::min(from, cell.frame); i <= std::max(from, cell.frame); ++i)
                m_selected.insert(frameIdOf(a, cell.track, i));
        }
    } else if (!m_selected.contains(id)) {
        m_selected = {id};
    }
    m_framesPicked = true;
    if (!(mods & Qt::ShiftModifier))
        m_anchor = id;
    if (a.tracks.empty()) {
        update();
        emit selectionChanged();
        return;
    }
    const std::set<int> chosen = m_selected;
    if (const auto went = m_doc->goToCell(cell.track, cell.frame); !went && went.error() != EditError::NoChange)
        emit refused(describe(went.error()));
    m_selected = chosen;  // going to the frame may have pruned it to the frame alone
    m_framesPicked = true;
    update();
    emit selectionChanged();
}

void FrameStrip::mouseMoveEvent(QMouseEvent* event) {
    const QPoint pos = event->position().toPoint();
    if (m_drag == Drag::None) {
        updateCursor(pos);
        return;
    }
    m_dragPos = pos;
    switch (m_drag) {
    case Drag::Pending:
        if ((pos - m_pressPos).manhattanLength() >= 6)
            m_drag = Drag::Move;
        update();
        break;
    case Drag::Move:
        if (!m_dragGroup.empty())
            updateSounds(pos);  // the clips selected with the frames go along
        update();
        break;
    case Drag::Box: updateBox(pos); break;
    case Drag::Hold: updateHold(pos); break;
    case Drag::Sound: updateSound(pos); break;
    case Drag::Sounds:
        updateSounds(pos);
        update();
        break;
    case Drag::TrimStart:
    case Drag::TrimEnd: updateTrim(pos); break;
    case Drag::None: break;
    }
}

void FrameStrip::updateCursor(const QPoint& pos) {
    if (!m_doc)
        return;
    if (const int id = soundAt(pos)) {
        const SoundPart part = soundPartAt(id, pos);
        setCursor(part == SoundPart::Body ? Qt::OpenHandCursor : Qt::SizeHorCursor);
        return;
    }
    const Cell cell = cellAt(pos);
    setCursor(cell.valid() && onRightEdge(cell.track, cell.frame, pos) ? Qt::SizeHorCursor : Qt::ArrowCursor);
}

void FrameStrip::mouseReleaseEvent(QMouseEvent* event) {
    const QPoint pos = event->position().toPoint();
    const Drag drag = std::exchange(m_drag, Drag::None);
    if (drag == Drag::Pending && m_doc && m_dragCell.valid()) {
        // A plain click on a frame that was one of several selected leaves only that one selected.
        m_selected = {frameIdOf(m_doc->animation(), m_dragCell.track, m_dragCell.frame)};
        if (m_doc->animation().tracks.empty())
            m_selected = {0};
        selectSound(0);
        emit selectionChanged();
    } else if (drag == Drag::Move) {  // NOLINT(bugprone-branch-clone): see the Sounds branch below
        finishMove(pos);
    } else if (drag == Drag::Sounds && (pos - m_pressPos).manhattanLength() < 4) {
        selectSound(m_dragSound);  // a click on one of several selected clips leaves it alone selected
        unpickFrames();
    } else if (drag == Drag::Sounds && m_dragFrames) {  // after the click above, so not merged with Move
        finishMove(pos);
    } else if (drag == Drag::Box && (pos - m_pressPos).manhattanLength() < 4) {
        // A click on a beat number is a click on its frame.
        const int track = trackAtY(pos.y());
        const auto [first, last] = track >= 0 ? framesBetween(track, pos.x(), pos.x()) : std::pair{0, -1};
        if (first <= last && labelRect(track, first).adjusted(0, 0, kGap, 0).contains(pos))
            clickFrame({track, first}, event->modifiers());
    }
    updateCursor(pos);
    update();
}

void FrameStrip::contextMenuEvent(QContextMenuEvent* event) {
    if (!m_doc)
        return;
    m_selectionActive = true;
    if (const int id = soundAt(event->pos())) {
        if (!m_selectedSounds.contains(id))
            selectSound(id);
        emit soundMenuRequested(id, event->globalPos());
        return;
    }
    const Cell cell = cellAt(event->pos());
    if (!cell.valid())
        return;
    if (cell.track != currentTrack() || !m_selected.contains(frameIdOf(m_doc->animation(), cell.track, cell.frame)))
        clickFrame(cell, Qt::NoModifier);
    emit frameMenuRequested(cell.frame, event->globalPos());
}

// Each track's selected frames go as far along it as the pointer went: from where it was pressed, kept within that track's
// selected frames (on the frame pressed, that is where the pointer is).
std::vector<animation_ops::FrameMove> FrameStrip::frameMoves(int dx) const {
    std::map<int, std::vector<int>> byTrack;
    for (const Cell& c : selectedCells())
        byTrack[c.track].push_back(c.frame);
    std::vector<animation_ops::FrameMove> out;
    for (const auto& [track, frames] : byTrack) {
        const int x = std::clamp(m_pressPos.x(), cellRect(track, frames.front()).left(), cellRect(track, frames.back()).right()) + dx;
        out.push_back({track, frames, dropIndex(track, x, frames)});
    }
    return out;
}

// One undo step with the clips the drag moved (the same merge key).
void FrameStrip::finishMove(const QPoint& pos) {
    const auto moved = m_doc->moveFrameGroups(frameMoves(pos.x() - m_pressPos.x()), m_mergeKey);
    if (!moved && moved.error() != EditError::NoChange)
        emit refused(describe(moved.error()));
}

void FrameStrip::updateHold(const QPoint& pos) {
    const QRect r = cellRect(m_dragCell.track, m_dragCell.frame);
    const int start = starts(m_dragCell.track)[static_cast<std::size_t>(m_dragCell.frame)];
    const int held = std::max(1, static_cast<int>(std::lround(double(pos.x() - r.left() + kGap) / m_beatWidth)));
    const int hold = std::max(1, snapBeat(start + held, {}, m_dragCell.track) - start);
    if (m_dragCell.track == currentTrack() && hold != m_doc->animation().holdOf(m_dragCell.frame))
        (void)m_doc->setFrameHold(m_dragCell.frame, hold, m_mergeKey);
}

// Over another sound track the clip goes onto it; over any other track it stays on its own.
void FrameStrip::updateSound(const QPoint& pos) {
    setCursor(Qt::ClosedHandCursor);
    const Animation& a = m_doc->animation();
    const SoundClip* c = sound(m_dragSound);
    if (!c)
        return;
    const int over = trackAtY(pos.y());
    const int track = over >= 0 && kindOf(over) == TrackKind::Sound ? a.tracks[static_cast<std::size_t>(over)].id : c->track;
    const int dragged = m_dragStartBeat + static_cast<int>(std::lround(double(pos.x() - m_pressPos.x()) / m_beatWidth));
    const int beat = std::max(0, snapClipStart(*c, dragged, {c->id}));
    (void)m_doc->moveSound(m_dragSound, beat, track, m_mergeKey);
}

// A clip moved to start at `start` snaps by its start, or else by its end, whichever has a target nearer. The `moving` clips'
// edges (its own, and the others dragged with it) are no targets.
int FrameStrip::snapClipStart(const SoundClip& clip, int start, const std::set<int>& moving) const {
    const double fps = m_doc->animation().fps;
    const double length = clip.playedSeconds() * fps;  // beats
    const std::optional<int> byStart = snapTarget(start, moving);
    const std::optional<int> byEnd = snapTarget(start + length, moving);
    if (byStart && (!byEnd || std::abs(*byStart - start) <= std::abs(*byEnd - (start + length))))
        return *byStart;
    if (byEnd)
        return static_cast<int>(std::lround(*byEnd - length));
    return start;
}

std::vector<int> FrameStrip::soundTrackIds() const {
    std::vector<int> out;
    if (m_doc)
        for (const AnimationTrack& t : m_doc->animation().tracks)
            if (t.kind == TrackKind::Sound)
                out.push_back(t.id);
    return out;
}

// How many sound tracks up (+) or down the dragged clips go: as many as the pointer is from the grabbed clip's track, so long
// as all of them stay on sound tracks. Over another kind of track it is what it last was.
int FrameStrip::groupTrackShift(const QPoint& pos, const std::vector<int>& ids) {
    const auto row = [&](int id) { return static_cast<int>(std::ranges::find(ids, id) - ids.begin()); };
    const int over = trackAtY(pos.y());
    const auto grabbed = std::ranges::find_if(m_dragGroup, [&](const auto& p) { return p.id == m_dragSound; });
    if (over >= 0 && kindOf(over) == TrackKind::Sound && grabbed != m_dragGroup.end())
        m_dragTrackShift = row(m_doc->animation().tracks[static_cast<std::size_t>(over)].id) - row(grabbed->track);
    int lowest = static_cast<int>(ids.size()), highest = -1;
    for (const animation_ops::SoundPlace& p : m_dragGroup) {
        lowest = std::min(lowest, row(p.track));
        highest = std::max(highest, row(p.track));
    }
    return std::clamp(m_dragTrackShift, -lowest, static_cast<int>(ids.size()) - 1 - highest);
}

// Several clips move together, by the same beats and sound tracks from where the drag started, none before the first beat.
// Where any of them would overlap another clip they stay where they last fitted.
void FrameStrip::updateSounds(const QPoint& pos) {
    setCursor(Qt::ClosedHandCursor);
    const std::vector<int> ids = soundTrackIds();
    const int shift = groupTrackShift(pos, ids);
    int beats = static_cast<int>(std::lround(double(pos.x() - m_pressPos.x()) / m_beatWidth));
    // The clip held by the pointer snaps; the others go along by as much.
    const auto grabbed = std::ranges::find_if(m_dragGroup, [&](const auto& p) { return p.id == m_dragSound; });
    std::set<int> moving;
    for (const animation_ops::SoundPlace& p : m_dragGroup)
        moving.insert(p.id);
    if (const SoundClip* c = sound(m_dragSound); c && grabbed != m_dragGroup.end())
        beats = snapClipStart(*c, grabbed->startBeat + beats, moving) - grabbed->startBeat;
    for (const animation_ops::SoundPlace& p : m_dragGroup)
        beats = std::max(beats, -p.startBeat);
    std::vector<animation_ops::SoundPlace> places = m_dragGroup;
    for (animation_ops::SoundPlace& p : places) {
        p.startBeat += beats;
        p.track = ids[static_cast<std::size_t>(std::ranges::find(ids, p.track) - ids.begin() + shift)];
    }
    (void)m_doc->placeSounds(places, m_mergeKey);
}

// Dragging the start trims whole beats off (or back on), so the sound stays where it was under the frames; dragging the end
// trims to the pixel.
void FrameStrip::updateTrim(const QPoint& pos) {
    const SoundClip* c = sound(m_dragSound);
    if (!c)
        return;
    const double fps = m_doc->animation().fps;
    const int dx = pos.x() - m_pressPos.x();
    if (m_drag == Drag::TrimStart) {
        const int backToFileStart = -static_cast<int>(std::floor(m_dragTrimStart * fps + 1e-6));
        const int dragged = snapBeat(m_dragStartBeat + static_cast<int>(std::lround(double(dx) / m_beatWidth)), {c->id}) - m_dragStartBeat;
        const int beats = std::max({dragged, backToFileStart, -m_dragStartBeat});
        const double trim = std::max(0.0, m_dragTrimStart + beats / fps);
        (void)m_doc->trimSound(m_dragSound, m_dragStartBeat + beats, trim, c->trimEnd, m_mergeKey);
    } else {
        // The end goes to the pixel, or onto a beat it snaps to.
        const double untrimmedEnd = c->startBeat / fps + c->seconds - c->trimStart;  // seconds, with nothing cut off the end
        double end = untrimmedEnd - m_dragTrimEnd + double(dx) / m_beatWidth / fps;
        if (const std::optional<int> snapped = snapTarget(end * fps, {c->id}))
            end = *snapped / fps;
        const double trim = std::clamp(untrimmedEnd - end, 0.0, c->seconds);
        (void)m_doc->trimSound(m_dragSound, c->startBeat, c->trimStart, trim, m_mergeKey);
    }
}

// The box picks every frame and clip it touches, on every track.
void FrameStrip::updateBox(const QPoint& pos) {
    const QRect box = QRect(m_pressPos, pos).normalized();
    const Animation& a = m_doc->animation();
    std::set<int> clips = m_boxBaseSounds;
    for (const SoundClip& c : a.sounds)
        if (soundRect(c.id).intersects(box))
            clips.insert(c.id);
    selectSounds(std::move(clips));
    std::set<int> picked = m_boxBase;
    for (int t = 0; t < trackCount(); ++t) {
        const QRect lane(0, cellRect(t, 0).top(), width(), cellHeightOf(t));
        if (!hasCells(t) || !box.intersects(lane))
            continue;
        const auto [first, last] = framesBetween(t, box.left(), box.right());
        for (int i = first; i <= last; ++i)
            if (cellRect(t, i).intersects(box))
                picked.insert(frameIdOf(a, t, i));
    }
    if (a.tracks.empty() && !picked.empty())
        picked = {0};
    m_framesPicked = !picked.empty();
    if (picked.empty())
        picked = {a.frameId(m_doc->currentFrame())};
    if (picked != m_selected) {
        m_selected = std::move(picked);
        emit selectionChanged();
    }
    update();
}

void FrameStrip::paintEvent(QPaintEvent* event) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QRect exposed = event->rect();
    p.fillRect(exposed, Theme::color(Token::Base));
    if (!m_doc)
        return;
    for (int t = 0; t < trackCount(); ++t) {
        switch (kindOf(t)) {
        case TrackKind::Frames:
        case TrackKind::Video: paintLane(p, t, exposed); break;
        case TrackKind::Sound: paintSoundLane(p, t, exposed); break;
        }
    }
    paintPlayhead(p);
    if (m_drag == Drag::Move || (m_drag == Drag::Sounds && m_dragFrames))
        paintDropMark(p);
    if (m_drag == Drag::Box) {
        const QRect box = QRect(m_pressPos, m_dragPos).normalized();
        QColor fill = Theme::color(Token::Accent);
        fill.setAlpha(60);
        p.setPen(QPen(Theme::color(Token::Accent), Theme::kBorder));
        p.setBrush(fill);
        p.drawRect(QRectF(box).adjusted(0.5, 0.5, -0.5, -0.5));
    }
    if ((!m_wanted.empty() || !m_wantedVideo.empty()) && !m_thumbTimer.isActive()) {
        m_thumbTimer.setInterval(0);
        m_thumbTimer.start();
    }
    if ((!m_wanted.empty() || !m_wantedVideo.empty()) && !m_spinTimer.isActive())
        m_spinTimer.start();
}

// A labelled track's lane, faintly in its colour, so its frames and clips read as its (as asked).
void FrameStrip::paintLabelTint(QPainter& p, int track) {
    if (!m_doc || track >= m_doc->animation().trackCount())
        return;
    QColor tint = track_labels::colour(m_doc->animation().tracks[static_cast<std::size_t>(track)].label);
    if (!tint.isValid())
        return;
    tint.setAlpha(kLabelTintAlpha);
    p.fillRect(slotRect(track), tint);
}

void FrameStrip::paintLane(QPainter& p, int track, const QRect& exposed) {
    const QRect lane(0, laneTop(track), width(), laneHeight(track));
    if (!lane.intersects(exposed))
        return;
    paintLabelTint(p, track);
    p.save();
    if (trackCount() > 1 && track == currentTrack()) {
        QColor tint = Theme::color(Token::Hover);
        tint.setAlpha(110);
        p.fillRect(lane.adjusted(0, -kTrackGap / 2, 0, kTrackGap / 2), tint);
    }
    if (trackHidden(m_doc->animation(), track))
        p.setOpacity(kHiddenOpacity);
    const auto [first, last] = framesBetween(track, exposed.left() - kGap, exposed.right() + kGap);
    const bool folded = collapsed(track);
    const AnimationTrack* t = m_doc->animation().tracks.empty() ? nullptr : &m_doc->animation().tracks[static_cast<std::size_t>(track)];
    for (int i = first; i <= last; ++i) {
        if (folded)
            paintCollapsedCell(p, cellRect(track, i), track == currentTrack() && t && t->frameAtBeat(m_doc->beat()) == i,
                               m_selected.contains(frameIdOf(m_doc->animation(), track, i)));
        else
            paintCell(p, track, i);
    }
    paintHeldEnd(p, track);
    if (kindOf(track) == TrackKind::Frames && !folded)
        paintLabels(p, track, exposed);
    p.restore();
}

void FrameStrip::paintCell(QPainter& p, int track, int frame) {
    const QRect r = cellRect(track, frame);
    const Animation& a = m_doc->animation();
    const bool isCurrentTrack = track == currentTrack();
    const AnimationTrack* t = a.tracks.empty() ? nullptr : &a.tracks[static_cast<std::size_t>(track)];
    const bool shown = t ? t->frameAtBeat(a.beat) == frame : true;  // its track shows it at the playhead
    const bool current = isCurrentTrack && shown;
    const bool selected = m_selected.contains(t ? frameIdOf(a, track, frame) : 0);
    // A video frame shows its picture (none: an empty frame); a drawn frame its thumbnail.
    const bool video = t && t->kind == TrackKind::Video;
    const int picture = video ? t->frames[static_cast<std::size_t>(frame)].picture : -1;
    const QImage thumb = video ? (picture >= 0 ? videoThumbnail(picture) : QImage()) : thumbnail(track, frame);
    const bool loading =
        video ? picture >= 0 && thumb.isNull() && m_videoPictures : thumb.isNull() || m_wantedSet.contains(frameIdOf(a, track, frame));
    QPainterPath shape;
    shape.addRoundedRect(QRectF(r).adjusted(0.5, 0.5, -0.5, -0.5), Theme::kRadiusXs, Theme::kRadiusXs);
    p.save();
    p.setClipPath(shape);
    p.fillRect(r, Theme::color(Token::Window));
    const std::vector<int>& s = starts(track);
    const int hold = s[static_cast<std::size_t>(frame) + 1] - s[static_cast<std::size_t>(frame)];
    const int slot = r.width() / hold;
    for (int b = 0; b < hold && !thumb.isNull(); ++b) {
        const QRect cell(r.left() + b * slot, r.top(), b == hold - 1 ? r.right() - (r.left() + b * slot) + 1 : slot, r.height());
        p.drawImage(QRectF(cell), thumb, coverSource(thumb, cell));
        if (b > 0) {
            p.setPen(QPen(QColor(0, 0, 0, kHoldLineAlpha), Theme::kBorder));
            p.drawLine(QPointF(cell.left() + 0.5, cell.top() + 4), QPointF(cell.left() + 0.5, cell.bottom() - 4));
        }
    }
    if (selected && a.isAnimation() && (m_selected.size() > 1 || (m_framesPicked && !m_selectedSounds.empty()))) {
        QColor tint = Theme::color(Token::Accent);
        tint.setAlpha(110);
        p.fillRect(r, tint);
    }
    p.restore();
    if (thumb.isNull() && loading)
        paintLoading(p, r);
    if (video && r.width() >= 8 && animation_ops::frameHasDrawing(m_doc->state(), track, frame)) {
        // Drawn on: a dot in its corner, as a drawn frame's beat number has.
        p.setPen(QPen(Theme::color(Token::Base), Theme::kBorder));
        p.setBrush(Theme::color(Token::Accent));
        p.drawEllipse(QPointF(r.right() - 5.5, r.top() + 5.5), 3, 3);
    }
    if (hold > 1 && r.width() >= 30) {
        const QString text = QStringLiteral("×%1").arg(hold);
        const QFont f = Theme::font(Theme::TextRole::Caption);
        p.setFont(f);
        const int w = QFontMetrics(f).horizontalAdvance(text);
        const QRect badge(r.right() - w - 12, r.bottom() - 17, w + 8, 14);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor::fromRgba(kChipGround));
        p.drawRoundedRect(badge, Theme::kRadiusSm, Theme::kRadiusSm);
        p.setPen(QColor::fromRgba(kChipText));
        p.drawText(badge, Qt::AlignCenter, text);
    }
    if (current || selected || (shown && trackCount() > 1)) {
        p.setBrush(Qt::NoBrush);
        const QColor ink = Theme::color(current ? Token::AccentHover : selected ? Token::Accent : Token::BorderStrong);
        const double border = current ? kCurrentBorder : selected ? kSelectedBorder : kShownBorder;
        p.setPen(QPen(ink, border));
        const double in = border / 2;
        p.drawRoundedRect(QRectF(r).adjusted(in, in, -in, -in), Theme::kRadiusXs, Theme::kRadiusXs);
    }
}

// A folded track's frame: a thin bar, lit when it is the current frame or selected.
void FrameStrip::paintCollapsedCell(QPainter& p, const QRect& r, bool current, bool selected) {
    p.setPen(current ? QPen(Theme::color(Token::AccentHover), kSelectedBorder) : Qt::NoPen);
    p.setBrush(Theme::color(current || selected ? Token::Accent : Token::BorderStrong));
    p.drawRoundedRect(QRectF(r).adjusted(0.5, kFoldedInset + 0.5, -0.5, -kFoldedInset - 0.5), kFoldedRadius, kFoldedRadius);
}

// A frame or video picture still being read: a turning arc, so it doesn't just look blank.
void FrameStrip::paintLoading(QPainter& p, const QRect& r) {
    const int side = std::min({18, r.width() - 4, r.height() - 4});
    if (side < 6)
        return;
    const QRectF ring(r.center().x() - side / 2.0 + 0.5, r.center().y() - side / 2.0 + 0.5, side, side);
    const int turn = static_cast<int>((QDateTime::currentMSecsSinceEpoch() / 3) % 360);
    p.save();
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QPen(Theme::color(Token::BorderStrong), Theme::kBorderStrong));
    p.setBrush(Qt::NoBrush);
    p.drawEllipse(ring);
    p.setPen(QPen(Theme::color(Token::Accent), Theme::kBorderStrong, Qt::SolidLine, Qt::RoundCap));
    p.drawArc(ring, -turn * 16, 100 * 16);
    p.restore();
}

// Past a shorter track's last frame, a faint bar to the animation's end: that frame stays.
void FrameStrip::paintHeldEnd(QPainter& p, int track) {
    const int end = starts(track).back();
    const int total = m_doc->animation().totalBeats();
    if (end >= total)
        return;
    const QRect bar(xOfBeat(end), cellRect(track, 0).center().y() - 3, (total - end) * m_beatWidth - kGap, 6);
    QColor fill = Theme::color(Token::BorderStrong);
    fill.setAlpha(120);
    p.setPen(Qt::NoPen);
    p.setBrush(fill);
    p.drawRoundedRect(bar, Theme::kRadiusXs, Theme::kRadiusXs);
}

void FrameStrip::paintLabels(QPainter& p, int track, const QRect& exposed) {
    const int top = laneTop(track) + cellHeight() + 2;
    if (labelHeight() == 0 || exposed.bottom() < top - 2 || exposed.top() > top + labelHeight())
        return;
    QFont f = Theme::font(Theme::TextRole::Caption);
    p.setFont(f);
    const QFontMetrics fm(f);
    const bool perFrame = m_beatWidth >= 34;
    const std::vector<int>& s = starts(track);
    const int shownFrame =
        m_doc->animation().tracks.empty() ? 0 : m_doc->animation().tracks[static_cast<std::size_t>(track)].frameAtBeat(m_doc->beat());
    const auto [first, last] = framesBetween(track, exposed.left() - 40, exposed.right() + 40);
    for (int i = first; i <= last; ++i) {
        const QRect r = cellRect(track, i);
        if (!perFrame) {
            // Narrow cells: a tick per frame under the strip, and the beat's number where there is room.
            p.setPen(QPen(Theme::color(Token::Accent), Theme::kBorderStrong));
            p.drawLine(QPoint(r.left(), top - 1), QPoint(r.right(), top - 1));
            continue;
        }
        const int firstBeat = s[static_cast<std::size_t>(i)];
        const int lastBeat = s[static_cast<std::size_t>(i) + 1] - 1;
        const double fps = m_doc->animation().fps;
        const QString text = firstBeat == lastBeat ? TimeRuler::beatLabel(firstBeat, fps, m_showTime)
                                                   : TimeRuler::beatLabel(firstBeat, fps, m_showTime) + QStringLiteral("–") +
                                                         TimeRuler::beatLabel(lastBeat, fps, m_showTime);
        const bool drawn = animation_ops::frameHasDrawing(m_doc->state(), track, i);
        const int w = fm.horizontalAdvance(text) + (drawn ? 8 : 0);
        const int x = r.center().x() - w / 2;
        p.setPen(Theme::color(i == shownFrame && track == currentTrack() ? Token::Text : Token::TextMuted));
        p.drawText(QRect(x, top, fm.horizontalAdvance(text) + 1, fm.height()), Qt::AlignLeft | Qt::AlignVCenter, text);
        if (drawn) {
            p.setPen(Qt::NoPen);
            p.setBrush(Theme::color(Token::Accent));
            p.drawEllipse(QPointF(x + w - 2.5, top + fm.height() / 2.0), 2.5, 2.5);
        }
    }
    if (perFrame)
        return;
    const int every = std::max(1, static_cast<int>(std::ceil(30.0 / m_beatWidth)));
    const int step = every <= 2 ? 2 : every <= 5 ? 5 : ((every + 9) / 10) * 10;
    const int from = std::max(0, ((exposed.left() - kMargin) / m_beatWidth) / step * step);
    const int to = std::min(s.back() - 1, (exposed.right() - kMargin) / m_beatWidth + step);
    p.setPen(Theme::color(Token::TextMuted));
    for (int beat = from; beat <= to; beat += step) {
        const int x = xOfBeat(beat) + m_beatWidth / 2;
        const QString text = TimeRuler::beatLabel(beat, m_doc->animation().fps, m_showTime);
        p.drawText(QRect(x - fm.horizontalAdvance(text), top + 2, 2 * fm.horizontalAdvance(text), fm.height()), Qt::AlignCenter, text);
    }
}

// The playhead: a line at its beat's start from the ruler's chip down through every lane (with one track too: the
// redesign asked for a more visible playhead), and with several tracks its beat lit faintly across them, to see which frame
// of each is shown. With one, the current frame's outline says so, and a band would dim the drawing being worked on.
void FrameStrip::paintPlayhead(QPainter& p) {
    const int x = xOfBeat(m_doc->beat());
    const int top = 0;
    const int bottom = std::max(slotRect(0).bottom(), height());
    if (trackCount() > 1) {
        QColor fill = Theme::color(Token::Accent);
        fill.setAlpha(kPlayheadBandAlpha);
        p.fillRect(QRect(x, top, std::max(2, m_beatWidth - (m_beatWidth >= 24 ? kGap : 1)), bottom - top), fill);
    }
    p.setPen(QPen(Theme::color(Token::AccentHover), kPlayheadWidth));
    p.drawLine(QPointF(x, top), QPointF(x, bottom));
}

void FrameStrip::paintSoundLane(QPainter& p, int track, const QRect& exposed) {
    if (!slotRect(track).intersects(exposed))
        return;
    QFont f = Theme::font(Theme::TextRole::Caption);
    p.setFont(f);
    paintLabelTint(p, track);
    p.save();
    const AnimationTrack& t = m_doc->animation().tracks[static_cast<std::size_t>(track)];
    // The sound's own lane, across the timeline, its middle marked as a waveform's.
    QColor ground = Theme::color(Token::Window);
    ground.setAlpha(kSoundLaneAlpha);
    const QRect lane(0, laneTop(track), width(), laneHeight(track));
    p.fillRect(lane, ground);
    p.setPen(QPen(Theme::color(Token::Border), Theme::kBorder));
    p.drawLine(QPointF(lane.left(), lane.center().y() + 0.5), QPointF(lane.right(), lane.center().y() + 0.5));
    if (t.hidden)
        p.setOpacity(kHiddenOpacity);
    for (const SoundClip& c : m_doc->animation().sounds)
        if (c.track == t.id && soundRect(c.id).intersects(exposed))
            paintSound(p, c, exposed);
    p.restore();
}

// An unselected clip is grey with a muted waveform; the selected one is the accent colour with a light outline. A muted clip
// is faint and says so.
void FrameStrip::paintSound(QPainter& p, const SoundClip& clip, const QRect& exposed) {
    const QRect r = soundRect(clip.id);
    const bool selected = m_selectedSounds.contains(clip.id);
    const QColor ink = Theme::color(selected ? Token::AccentText : Token::TextMuted);
    QColor body = Theme::color(selected ? Token::Accent : Token::Hover);
    if (selected && (m_drag == Drag::Sound || m_drag == Drag::Sounds))
        body = Theme::color(Token::AccentHover);
    const double opacity = p.opacity();
    if (clip.muted)
        p.setOpacity(opacity * 0.5);
    p.setPen(selected ? QPen(Theme::color(Token::Text), Theme::kBorderStrong) : QPen(Theme::color(Token::BorderStrong), Theme::kBorder));
    p.setBrush(body);
    p.drawRoundedRect(QRectF(r).adjusted(1, 1, -1, -1), Theme::kRadiusXs, Theme::kRadiusXs);
    if (r.height() < kClipDetailHeight) {  // a folded lane's
        p.setOpacity(opacity);
        return;
    }
    paintWaveform(p, clip, r.adjusted(kEdgeGrip, 3, -kEdgeGrip, -3), exposed, ink);
    // The ends, which trim the clip when dragged.
    p.setPen(QPen(selected ? Theme::color(Token::AccentText) : Theme::color(Token::TextMuted), Theme::kBorderStrong, Qt::SolidLine,
                  Qt::RoundCap));
    p.drawLine(QPoint(r.left() + 4, r.top() + 7), QPoint(r.left() + 4, r.bottom() - 7));
    if (r.width() > 30)
        p.drawLine(QPoint(r.right() - 4, r.top() + 7), QPoint(r.right() - 4, r.bottom() - 7));
    const QFontMetrics fm(p.font());
    const QRect name = r.adjusted(12, 4, -12, 0);
    const QString label = clip.muted ? tr("%1 · muted").arg(clip.name) : clip.name;
    const QString text = fm.elidedText(label, Qt::ElideRight, name.width() - 8);
    if (!text.isEmpty()) {
        const QRect chip(name.left(), name.top(), fm.horizontalAdvance(text) + 8, fm.height() + 2);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0, 0, 0, selected ? kClipChipAlpha : kClipChipAlphaIdle));
        p.drawRoundedRect(chip, Theme::kRadiusXs, Theme::kRadiusXs);
        p.setPen(Theme::color(selected ? Token::AccentText : Token::Text));
        p.drawText(chip, Qt::AlignCenter, text);
    }
    p.setOpacity(opacity);
}

// One line per pixel, as tall as the loudest sound under it.
void FrameStrip::paintWaveform(QPainter& p, const SoundClip& clip, const QRect& body, const QRect& exposed, const QColor& ink) {
    const SoundPeaks* peaks = m_peaks ? m_peaks(clip) : nullptr;
    if (!peaks || body.width() <= 0)
        return;
    const double pxPerSecond = m_doc->animation().fps * m_beatWidth;
    const double clipLeft = xOfBeat(clip.startBeat);
    const int x0 = std::max(body.left(), exposed.left());
    const int x1 = std::min(body.right(), exposed.right());
    const double middle = body.center().y() + 0.5;
    const double half = body.height() / 2.0;
    QVector<QLineF> lines;
    lines.reserve(std::max(0, x1 - x0 + 1));
    for (int x = x0; x <= x1; ++x) {
        const double t = clip.trimStart + (x - clipLeft) / pxPerSecond;
        const double h = std::max(0.5, half * peaks->loudest(t, t + 1.0 / pxPerSecond) / 255.0);
        lines.append(QLineF(x + 0.5, middle - h, x + 0.5, middle + h));
    }
    p.setPen(QPen(ink, Theme::kBorder));
    p.drawLines(lines);
}

// Where each track's dragged frames would go.
void FrameStrip::paintDropMark(QPainter& p) {
    p.setPen(QPen(Theme::color(Token::AccentHover), kCurrentBorder, Qt::SolidLine, Qt::RoundCap));
    for (const animation_ops::FrameMove& m : frameMoves(m_dragPos.x() - m_pressPos.x())) {
        int x = kMargin - kGap / 2;
        int index = 0;
        for (int i = 0; i + 1 < static_cast<int>(starts(m.track).size()); ++i) {
            if (std::ranges::binary_search(m.frames, i))
                continue;
            if (index == m.to)
                break;
            x = cellRect(m.track, i).right() + kGap / 2;
            ++index;
        }
        const QRect cells = cellRect(m.track, 0);
        p.drawLine(QPoint(x, cells.top() - 2), QPoint(x, cells.bottom() + 2));
    }
}

QImage FrameStrip::shownThumbnail(int track) const {
    if (!m_doc || track < 0 || track >= m_doc->animation().trackCount())
        return {};
    const AnimationTrack& t = m_doc->animation().tracks[static_cast<std::size_t>(track)];
    if (!t.hasFrames())
        return {};
    const AnimationFrame& f = t.frames[static_cast<std::size_t>(t.frameAtBeat(m_doc->beat()))];
    if (t.kind == TrackKind::Video) {
        const auto it = m_videoThumbs.find(f.picture);
        return it != m_videoThumbs.end() ? it->second : QImage();
    }
    const auto it = m_thumbs.find(f.id);
    return it != m_thumbs.end() ? it->second.image : QImage();
}

quint64 FrameStrip::contentKey(int track, int frame) const {
    const DocumentState& s = m_doc->state();
    quint64 h = 0xcbf29ce484222325ULL;
    h = mix(h, quint64(s.background.mode) << 32 | s.background.color.rgb());
    if (track == 0)
        for (const LayerNode& n : s.layers.root().children)
            if (s.animation.isShared(n.props.id))
                h = hashNode(h, n);
    for (const LayerNode* n : animation_ops::ownLayers(s, track, frame))
        h = hashNode(h, *n);
    return h;
}

QImage FrameStrip::thumbnail(int track, int frame) {
    const int id = frameIdOf(m_doc->animation(), track, frame);
    const auto it = m_thumbs.find(id);
    const bool stale = it == m_thumbs.end() || it->second.key != contentKey(track, frame);
    const auto same = [&](const Cell& c) { return c.track == track && c.frame == frame; };
    if (stale && std::ranges::none_of(m_wanted, same)) {
        m_wanted.push_back({track, frame});
        m_wantedSet.insert(id);
    }
    return it != m_thumbs.end() ? it->second.image : QImage();
}

// The video's pictures are read from the disk cache a few at a time after painting, so scrolling never waits for them.
QImage FrameStrip::videoThumbnail(int index) {
    const auto it = m_videoThumbs.find(index);
    if (it != m_videoThumbs.end())
        return it->second;
    if (m_videoPictures && std::ranges::find(m_wantedVideo, index) == m_wantedVideo.end())
        m_wantedVideo.push_back(index);
    return {};
}

// One frame's thumbnail per turn of the event loop, so a long animation fills in without holding up the window.
void FrameStrip::makeNextThumbnail() {
    if (!m_doc || m_makingThumb)
        return;  // the one being made asks for the next
    for (int n = 0; n < kVideoPicturesPerTurn && !m_wantedVideo.empty(); ++n) {
        const int index = m_wantedVideo.front();
        m_wantedVideo.erase(m_wantedVideo.begin());
        const QImage small = m_videoPictures ? m_videoPictures(index) : QImage();
        if (small.isNull())
            continue;  // not read yet: asked again when the lane is painted after the video is read
        m_videoThumbs[index] = small;
        update(videoLaneRect());
    }
    if (m_wanted.empty()) {
        if (!m_wantedVideo.empty())
            m_thumbTimer.start(0);
        else
            update(cellRect(m_doc->currentFrame()));  // a repaint finds whether the current frame changed
        return;
    }
    const Cell cell = m_wanted.front();
    m_wanted.erase(m_wanted.begin());
    m_wantedSet.erase(frameIdOf(m_doc->animation(), cell.track, cell.frame));
    const Animation& a = m_doc->animation();
    const bool gone = cell.track >= trackCount() || cell.frame + 1 >= static_cast<int>(starts(cell.track).size());
    const int id = gone ? 0 : frameIdOf(a, cell.track, cell.frame);
    if (!gone && !(m_thumbs.contains(id) && m_thumbs[id].key == contentKey(cell.track, cell.frame))) {
        makeThumbnailAway(id, cell);
        return;
    }
    if (!m_wanted.empty() || !m_wantedVideo.empty())
        m_thumbTimer.start(0);
}

// A big canvas's thumbnail takes milliseconds, so it's made from a copy of the frame's layers on a thread, and the window goes
// on meanwhile: at the end of a stroke it would otherwise hold up the frames that follow the pen.
void FrameStrip::makeThumbnailAway(int id, Cell cell) {
    if (m_thumbWorker.joinable())
        m_thumbWorker.join();  // the last one is made: it said so before it ended
    m_makingThumb = true;
    m_thumbWorker = std::thread([this, id, cell, key = contentKey(cell.track, cell.frame), generation = m_thumbGeneration,
                                 make = m_doc->cellThumbnailMaker(cell.track, cell.frame, kCellHeight * 2)] {
        const QImage image = make();
        // Run on the window's thread; the strip waits for this thread before it goes, so it's still here.
        QMetaObject::invokeMethod(
            this, [this, id, cell, key, image, generation] { thumbnailMade(id, cell, key, image, generation); }, Qt::QueuedConnection);
    });
}

void FrameStrip::thumbnailMade(int id, Cell cell, quint64 key, const QImage& image, int generation) {
    m_makingThumb = false;
    if (generation == m_thumbGeneration) {
        m_thumbs[id] = Thumb{key, image};
        update(cellRect(cell.track, cell.frame).adjusted(-2, -2, 2, 2));
        emit thumbnailsChanged();
    }
    if (m_doc && (!m_wanted.empty() || !m_wantedVideo.empty()))
        m_thumbTimer.start(0);
}

}
