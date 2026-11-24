#include "ui/animation/SoundPlayback.h"

#ifdef DOODLEBOLD_HAS_MULTIMEDIA
#include <QAudioOutput>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QMediaPlayer>
#include <QTemporaryDir>
#include <QTimer>
#include <QUrl>

#include <algorithm>
#include <cmath>
#include <functional>
#include <map>
#include <memory>
#endif

namespace doodlebold {

#ifdef DOODLEBOLD_HAS_MULTIMEDIA

namespace {

// A player kept from an earlier play may have played to its end: its file is still loaded.
bool isLoaded(const QMediaPlayer* p) {
    return p->mediaStatus() == QMediaPlayer::LoadedMedia || p->mediaStatus() == QMediaPlayer::BufferedMedia ||
           p->mediaStatus() == QMediaPlayer::BufferingMedia || p->mediaStatus() == QMediaPlayer::EndOfMedia;
}

// Seeks to `position()` and plays, once the player has loaded its file. Returns what waits for it to load, if anything, so
// that a stop before then can cancel it.
QMetaObject::Connection startWhenLoaded(QMediaPlayer* p, const std::function<qint64()>& position) {
    if (isLoaded(p)) {
        p->setPosition(position());
        p->play();
        return {};
    }
    auto once = std::make_shared<QMetaObject::Connection>();
    *once = QObject::connect(p, &QMediaPlayer::mediaStatusChanged, p, [p, position, once] {
        if (!isLoaded(p))
            return;
        QObject::disconnect(*once);
        p->setPosition(position());
        p->play();
    });
    return *once;
}

}

// Qt Multimedia's FFmpeg backend looks for hardware video decoders and encoders as it starts: half a second to a second on
// the window's thread on a typical machine, against about 50 ms without. Only sound is played through it (the
// video's pictures are read by FFmpeg itself), so it looks for none, unless they were asked for.
void startWithoutVideoHardware() {
    for (const char* name : {"QT_FFMPEG_DECODING_HW_DEVICE_TYPES", "QT_FFMPEG_ENCODING_HW_DEVICE_TYPES"})
        if (!qEnvironmentVariableIsSet(name))
            qputenv(name, ",");
}

struct SoundPlayback::Players {
    // A clip's player, opened on its file ahead of Play and kept between plays.
    struct Clip {
        std::shared_ptr<const QByteArray> data;
        std::unique_ptr<QAudioOutput> output;
        std::unique_ptr<QMediaPlayer> player;  // goes first: it plays through `output`
    };
    QElapsedTimer clock;  // since play()
    QTemporaryDir dir;
    std::map<const QByteArray*, QString> files;  // a clip's bytes, written once
    std::map<int, Clip> clips;                   // by clip id
    std::vector<std::unique_ptr<QTimer>> timers;
    std::vector<QMetaObject::Connection> waiting;  // clips started before their file had loaded

    // Runs `act` in `seconds` (at once for 0).
    void after(double seconds, std::function<void()> act) {
        auto timer = std::make_unique<QTimer>();
        timer->setSingleShot(true);
        timer->setTimerType(Qt::PreciseTimer);
        QObject::connect(timer.get(), &QTimer::timeout, std::move(act));
        timer->start(static_cast<int>(std::lround(seconds * 1000.0)));
        timers.push_back(std::move(timer));
    }

    QString fileOf(const SoundClip& clip) {
        const auto it = files.find(clip.data.get());
        if (it != files.end())
            return it->second;
        const QString suffix = QFileInfo(clip.name).suffix();
        QString path = dir.filePath(QStringLiteral("clip%1.%2").arg(files.size()).arg(suffix.isEmpty() ? QStringLiteral("wav") : suffix));
        QFile f(path);
        if (!f.open(QIODevice::WriteOnly) || f.write(*clip.data) != clip.data->size())
            return {};
        files[clip.data.get()] = path;
        return path;
    }

    // The clip's player, opened on its file if it has none yet; nullptr when the file can't be written.
    QMediaPlayer* playerOf(const SoundClip& clip, int& made) {
        if (const auto it = clips.find(clip.id); it != clips.end() && it->second.data == clip.data)
            return it->second.player.get();
        const QString file = clip.data ? fileOf(clip) : QString();
        if (file.isEmpty())
            return nullptr;
        startWithoutVideoHardware();
        Clip c;
        c.data = clip.data;
        c.output = std::make_unique<QAudioOutput>();
        c.player = std::make_unique<QMediaPlayer>();
        c.player->setAudioOutput(c.output.get());
        c.player->setSource(QUrl::fromLocalFile(file));
        ++made;
        Clip& kept = clips[clip.id] = std::move(c);
        return kept.player.get();
    }
};

SoundPlayback::SoundPlayback() : m_players(std::make_unique<Players>()) {}
SoundPlayback::~SoundPlayback() = default;

bool SoundPlayback::isAvailable() {
    return true;
}

void SoundPlayback::prepare(const std::vector<SoundClip>& clips) {
    if (!m_playing.empty())
        return;  // its timers hold the players; Play prepares what it plays
    std::erase_if(m_players->clips, [&](const auto& kept) {
        return std::none_of(clips.begin(), clips.end(),
                            [&](const SoundClip& c) { return c.id == kept.first && c.data == kept.second.data; });
    });
    for (const SoundClip& clip : clips)
        (void)m_players->playerOf(clip, m_playersMade);
}

void SoundPlayback::setGains(const std::vector<SoundClip>& clips, const std::vector<double>& gains) {
    for (std::size_t i = 0; i < clips.size() && i < gains.size(); ++i)
        if (const auto it = m_players->clips.find(clips[i].id); it != m_players->clips.end())
            it->second.output->setVolume(static_cast<float>(gains[i]));
}

// Times: `now` and a clip's start and end are the animation's seconds; the clock's are the room's, `speed` times slower.
void SoundPlayback::play(const std::vector<SoundClip>& clips, const std::vector<double>& gains, double fps, int fromBeat, double speed) {
    stop();
    for (const SoundClip& c : clips)
        m_playing.push_back(c.id);
    m_players->clock.start();
    const double now = fromBeat / fps;
    for (const SoundClip& clip : clips) {
        const double start = clip.startSeconds(fps);
        if (!clip.data || clip.endSeconds(fps) <= now)
            continue;
        QMediaPlayer* p = m_players->playerOf(clip, m_playersMade);
        if (!p)
            continue;
        const std::size_t index = static_cast<std::size_t>(&clip - clips.data());
        m_players->clips[clip.id].output->setVolume(static_cast<float>(index < gains.size() ? gains[index] : 1.0));
        p->setPlaybackRate(speed);
        // Where in the file to start: what the playhead has gone past of the clip's played part, measured when it starts.
        // Seeking before the file is loaded is lost (it then plays from its start), so that waits for the file.
        Players* players = m_players.get();
        const auto position = [players, now, start, speed, trim = clip.trimStart] {
            const double into = std::max(0.0, now + speed * static_cast<double>(players->clock.elapsed()) / 1000.0 - start);
            return static_cast<qint64>(std::lround((trim + into) * 1000.0));
        };
        m_players->after(std::max(0.0, start - now) / speed,
                         [players, p, position] { players->waiting.push_back(startWhenLoaded(p, position)); });
        if (clip.trimEnd > 0.0)
            m_players->after((clip.endSeconds(fps) - now) / speed, [p] { p->pause(); });  // its trimmed end
    }
}

// The players stay open on their files for the next Play.
void SoundPlayback::stop() {
    m_playing.clear();
    m_players->timers.clear();
    for (const QMetaObject::Connection& c : m_players->waiting)
        QObject::disconnect(c);
    m_players->waiting.clear();
    for (auto& [id, clip] : m_players->clips)
        clip.player->pause();
}

#else

struct SoundPlayback::Players {};

SoundPlayback::SoundPlayback() = default;
SoundPlayback::~SoundPlayback() = default;

bool SoundPlayback::isAvailable() {
    return false;
}

void SoundPlayback::prepare(const std::vector<SoundClip>&) {}

void SoundPlayback::setGains(const std::vector<SoundClip>&, const std::vector<double>&) {}

void SoundPlayback::play(const std::vector<SoundClip>& clips, const std::vector<double>&, double, int, double) {
    m_playing.clear();
    for (const SoundClip& c : clips)
        m_playing.push_back(c.id);
}

void SoundPlayback::stop() {
    m_playing.clear();
}

#endif

}
