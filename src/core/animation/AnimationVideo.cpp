#include "core/animation/AnimationVideo.h"

#include "core/ffmpeg/ExternalProcess.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPainter>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>

#include <algorithm>
#include <cmath>
#include <numeric>

namespace doodlebold {

namespace {

QString tr(const char* text) {
    return QCoreApplication::translate("AnimationVideo", text);
}

double rateOf(const QString& fraction) {
    const QStringList parts = fraction.split(QLatin1Char('/'));
    const double num = parts.value(0).toDouble();
    const double den = parts.size() > 1 ? parts[1].toDouble() : 1.0;
    return den > 0 ? num / den : 0.0;
}

QString number(double v) {
    return QString::number(v, 'f', 6);
}

constexpr int kExtractTimeoutMs = 30 * 60 * 1000;

}

Result<VideoInfo, QString> probeVideo(const FfmpegTools& tools, const QString& file) {
    using R = Result<VideoInfo, QString>;
    const ProcessOutput out =
        runProcess(tools.ffprobe,
                   {QStringLiteral("-v"), QStringLiteral("error"), QStringLiteral("-print_format"), QStringLiteral("json"),
                    QStringLiteral("-show_streams"), QStringLiteral("-show_format"), mediaUrl(file)},
                   FfmpegLocator::kTimeoutMs * 2);
    if (!out.succeeded())
        return R::failure(tr("The file could not be read as a video."));
    const QJsonObject root = QJsonDocument::fromJson(out.out).object();
    VideoInfo info;
    for (const auto& v : root.value(QStringLiteral("streams")).toArray()) {
        const QJsonObject s = v.toObject();
        const QString type = s.value(QStringLiteral("codec_type")).toString();
        if (type == QLatin1String("audio"))
            info.hasSound = true;
        if (type != QLatin1String("video") || info.size.isValid())
            continue;
        if (s.value(QStringLiteral("disposition")).toObject().value(QStringLiteral("attached_pic")).toInt() == 1)
            continue;  // a cover picture, not the video
        info.size = QSize(s.value(QStringLiteral("width")).toInt(), s.value(QStringLiteral("height")).toInt());
        info.fps = rateOf(s.value(QStringLiteral("avg_frame_rate")).toString());
        if (info.fps <= 0.0)
            info.fps = rateOf(s.value(QStringLiteral("r_frame_rate")).toString());
        info.seconds = s.value(QStringLiteral("duration")).toString().toDouble();
    }
    if (!info.size.isValid() || info.size.isEmpty())
        return R::failure(tr("The file has no video."));
    if (info.seconds <= 0.0)
        info.seconds = root.value(QStringLiteral("format")).toObject().value(QStringLiteral("duration")).toString().toDouble();
    if (info.seconds <= 0.0)
        return R::failure(tr("The video's length could not be read."));
    return info;
}

QSize fittedSize(const QSize& source, const QSize& box) {
    if (source.isEmpty() || box.isEmpty())
        return {};
    QSize s = source.scaled(box, Qt::KeepAspectRatio);
    return s.expandedTo(QSize(1, 1));
}

VideoFrames::VideoFrames(VideoReference video, QSize canvas, QString cacheRoot)
    : m_video(std::move(video)), m_canvas(canvas), m_root(std::move(cacheRoot)) {}

QString VideoFrames::defaultCacheRoot() {
    return QDir(QStandardPaths::writableLocation(QStandardPaths::CacheLocation)).filePath(QStringLiteral("video-frames"));
}

QString VideoFrames::folder() const {
    const QFileInfo file(m_video.path);
    QCryptographicHash hash(QCryptographicHash::Sha1);
    hash.addData(file.absoluteFilePath().toUtf8());
    hash.addData(QByteArray::number(file.lastModified().toMSecsSinceEpoch()));
    hash.addData(QByteArray::number(file.size()));
    hash.addData(number(m_video.fps).toUtf8());
    hash.addData(QByteArray::number(m_canvas.width()) + 'x' + QByteArray::number(m_canvas.height()));
    hash.addData(m_video.lossless ? "png" : "jpg");
    return QDir(m_root).filePath(QString::fromLatin1(hash.result().toHex().left(20)));
}

QString VideoFrames::picturePath(int index) const {
    return QDir(folder()).filePath(QStringLiteral("%1.%2")
                                       .arg(index + 1, 6, 10, QLatin1Char('0'))
                                       .arg(m_video.lossless ? QStringLiteral("png") : QStringLiteral("jpg")));
}

bool VideoFrames::isCached() const {
    return QFileInfo::exists(QDir(folder()).filePath(QStringLiteral("complete")));
}

QStringList videoExtractArguments(const VideoReference& video, const QSize& box, const QString& folder) {
    const QSize fit = fittedSize(video.sourceSize, box);
    QStringList args = {QStringLiteral("-hide_banner"),
                        QStringLiteral("-v"),
                        QStringLiteral("error"),
                        QStringLiteral("-y"),
                        QStringLiteral("-nostdin"),
                        QStringLiteral("-progress"),
                        QStringLiteral("pipe:1"),
                        QStringLiteral("-i"),
                        mediaUrl(video.path),
                        QStringLiteral("-an"),
                        QStringLiteral("-vf"),
                        QStringLiteral("fps=%1,scale=%2:%3:flags=bicubic").arg(number(video.fps)).arg(fit.width()).arg(fit.height())};
    if (!video.lossless)
        args << QStringLiteral("-q:v") << QStringLiteral("3");
    args << QStringLiteral("-start_number") << QStringLiteral("1")
         << mediaUrl(QDir(folder).filePath(video.lossless ? QStringLiteral("%06d.png") : QStringLiteral("%06d.jpg")));
    return args;
}

Result<int, QString> VideoFrames::extract(const FfmpegTools& tools, const VideoProgress& progress) const {
    using R = Result<int, QString>;
    const QString dir = folder();
    if (!QDir().mkpath(dir))
        return R::failure(tr("The folder for the video's pictures could not be made: %1").arg(dir));
    QProcess process;
    process.setProgram(tools.ffmpeg);
    process.setArguments(videoExtractArguments(m_video, m_canvas, dir));
    process.start(QIODevice::ReadOnly);
    if (!process.waitForStarted(FfmpegLocator::kTimeoutMs))
        return R::failure(tr("FFmpeg could not be started."));
    const int total = m_video.pictureCount();
    QByteArray pending;
    int done = 0;
    QElapsedTimer clock;
    clock.start();
    while (process.state() != QProcess::NotRunning) {
        process.waitForReadyRead(100);
        pending += process.readAllStandardOutput();
        int nl;
        while ((nl = static_cast<int>(pending.indexOf('\n'))) >= 0) {
            const QByteArray line = pending.left(nl).trimmed();
            pending.remove(0, nl + 1);
            if (line.startsWith("frame="))
                done = line.mid(6).toInt();
        }
        if ((progress && !progress(std::min(done, total), total)) || clock.elapsed() > kExtractTimeoutMs) {
            process.kill();
            process.waitForFinished(2000);
            QDir(dir).removeRecursively();
            return R::failure(QString());
        }
    }
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        const QString err = QString::fromUtf8(process.readAllStandardError()).trimmed();
        QDir(dir).removeRecursively();
        return R::failure(err.isEmpty() ? tr("FFmpeg could not read the video.") : err.section(QLatin1Char('\n'), -3));
    }
    QFile mark(QDir(dir).filePath(QStringLiteral("complete")));
    if (mark.open(QIODevice::WriteOnly))
        mark.write(QByteArray::number(total));
    return total;
}

QImage VideoFrames::picture(int index) {
    if (index < 0)
        return {};
    for (auto it = m_recent.begin(); it != m_recent.end(); ++it)
        if (it->first == index) {
            m_recent.splice(m_recent.begin(), m_recent, it);
            return it->second;
        }
    QImage fitted(picturePath(index));
    if (fitted.isNull())
        return {};
    QImage out(m_canvas, QImage::Format_RGBA8888_Premultiplied);
    out.fill(Qt::transparent);
    {
        QPainter p(&out);
        p.setRenderHint(QPainter::SmoothPixmapTransform);
        const QSize fit = fittedSize(fitted.size(), m_canvas);
        p.drawImage(QRect(QPoint((m_canvas.width() - fit.width()) / 2, (m_canvas.height() - fit.height()) / 2), fit), fitted);
    }
    m_recent.emplace_front(index, out);
    if (m_recent.size() > std::size_t(kMemoryPictures))
        m_recent.pop_back();
    return out;
}

QImage VideoFrames::smallPicture(int index, int height) const {
    if (index < 0)
        return {};
    QImageReader reader(picturePath(index));
    const QSize full = reader.size();
    if (full.isEmpty())
        return reader.read();
    reader.setScaledSize(QSize(std::max(1, full.width() * height / full.height()), height));
    return reader.read();
}

Result<QByteArray, QString> extractVideoSound(const FfmpegTools& tools, const QString& file) {
    using R = Result<QByteArray, QString>;
    QTemporaryDir dir;
    if (!dir.isValid())
        return R::failure(tr("No temporary folder for the video's sound."));
    const QString wav = dir.filePath(QStringLiteral("sound.wav"));
    const ProcessOutput out =
        runProcess(tools.ffmpeg,
                   {QStringLiteral("-hide_banner"), QStringLiteral("-v"), QStringLiteral("error"), QStringLiteral("-y"),
                    QStringLiteral("-i"), mediaUrl(file), QStringLiteral("-vn"), QStringLiteral("-ac"), QStringLiteral("2"),
                    QStringLiteral("-ar"), QStringLiteral("48000"), QStringLiteral("-c:a"), QStringLiteral("pcm_s16le"), mediaUrl(wav)},
                   kExtractTimeoutMs);
    QFile f(wav);
    if (!out.succeeded() || !f.open(QIODevice::ReadOnly))
        return R::failure(tr("The video's sound could not be read."));
    return f.readAll();
}

quint8 SoundPeaks::loudest(double from, double to) const {
    const auto index = [](double t) { return static_cast<std::ptrdiff_t>(std::floor(t * kPerSecond)); };
    const std::ptrdiff_t first = std::max<std::ptrdiff_t>(0, index(from));
    const std::ptrdiff_t last = std::min<std::ptrdiff_t>(std::ssize(peaks) - 1, std::max(first, index(to)));
    quint8 out = 0;
    for (std::ptrdiff_t i = first; i <= last; ++i)
        out = std::max(out, peaks[static_cast<std::size_t>(i)]);
    return out;
}

SoundPeaks peaksOf(const QByteArray& samples, int rate) {
    SoundPeaks out;
    const auto* sample = reinterpret_cast<const qint16*>(samples.constData());
    const qsizetype count = samples.size() / 2;
    const qsizetype per = std::max<qsizetype>(1, rate / SoundPeaks::kPerSecond);
    for (qsizetype start = 0; start < count; start += per) {
        int loudest = 0;
        for (qsizetype i = start; i < std::min(count, start + per); ++i)
            loudest = std::max(loudest, std::abs(int(sample[i])));
        out.peaks.push_back(static_cast<quint8>(std::min(255, loudest * 255 / 32767)));
    }
    return out;
}

Result<SoundPeaks, QString> readSoundPeaks(const FfmpegTools& tools, const QByteArray& data, const QString& name) {
    using R = Result<SoundPeaks, QString>;
    constexpr int kRate = 8000;
    QTemporaryDir dir;
    const QString suffix = QFileInfo(name).suffix();
    const QString in = dir.filePath(QStringLiteral("sound.") + (suffix.isEmpty() ? QStringLiteral("wav") : suffix));
    const QString raw = dir.filePath(QStringLiteral("sound.raw"));
    QFile f(in);
    if (!dir.isValid() || !f.open(QIODevice::WriteOnly) || f.write(data) != data.size())
        return R::failure(tr("No temporary folder for the sound."));
    f.close();
    const ProcessOutput out =
        runProcess(tools.ffmpeg,
                   {QStringLiteral("-hide_banner"), QStringLiteral("-v"), QStringLiteral("error"), QStringLiteral("-y"),
                    QStringLiteral("-i"), mediaUrl(in), QStringLiteral("-vn"), QStringLiteral("-ac"), QStringLiteral("1"),
                    QStringLiteral("-ar"), QString::number(kRate), QStringLiteral("-f"), QStringLiteral("s16le"), mediaUrl(raw)},
                   kExtractTimeoutMs);
    QFile samples(raw);
    if (!out.succeeded() || !samples.open(QIODevice::ReadOnly))
        return R::failure(tr("The sound could not be read."));
    return peaksOf(samples.readAll(), kRate);
}

FrameRate frameRateOf(double fps) {
    // Rates like 23.976 as a fraction over 1000; whole rates over 1.
    if (std::abs(fps - std::round(fps)) < 1e-6)
        return {static_cast<int>(std::lround(fps)), 1};
    const int num = static_cast<int>(std::lround(fps * 1000.0));
    const int g = std::gcd(num, 1000);
    return {num / g, 1000 / g};
}

AnimationExportPlan planAnimationExport(const Animation& animation, const QSize& canvas) {
    AnimationExportPlan plan;
    plan.size = canvas;
    plan.rate = frameRateOf(animation.fps);
    // A beat that shows the same frames as the one before it reuses its picture.
    std::vector<int> shown, before;
    for (int beat = 0; beat < animation.totalBeats(); ++beat) {
        shown.clear();
        for (const AnimationTrack& t : animation.tracks)
            shown.push_back(t.hidden ? -1 : t.frameAtBeat(beat));
        plan.beats.push_back(beat > 0 && shown == before ? plan.beats.back() : beat);
        std::swap(shown, before);
    }
    return plan;
}

namespace {

// The part of a trimmed clip that plays, as the start of its filter chain; nothing for a clip that isn't trimmed.
QString trimFilter(const SoundClip& clip) {
    if ((clip.trimStart <= 0.0 && clip.trimEnd <= 0.0) || clip.seconds <= 0.0)
        return {};
    return QStringLiteral("atrim=start=%1:duration=%2,asetpts=PTS-STARTPTS,").arg(number(clip.trimStart)).arg(number(clip.playedSeconds()));
}

}

// A clip's volume as FFmpeg's filter, before its other filters; nothing at full volume, so an export without a volume set is
// made as before.
QString volumeFilter(double gain) {
    return gain >= 1.0 ? QString() : QStringLiteral("volume=%1,").arg(number(gain));
}

AudioPlan soundClipsPlan(const std::vector<SoundClip>& clips, const std::vector<double>& gains, double fps, double seconds,
                         const QString& folder) {
    AudioPlan plan;
    QStringList parts, labels;
    for (const SoundClip& clip : clips) {
        const std::size_t index = static_cast<std::size_t>(&clip - clips.data());
        const double gain = index < gains.size() ? gains[index] : 1.0;
        const double start = clip.startBeat / fps;
        if (!clip.data || clip.data->isEmpty() || clip.muted || start >= seconds)
            continue;
        const QString suffix = QFileInfo(clip.name).suffix();
        const QString path =
            QDir(folder).filePath(QStringLiteral("clip%1.%2").arg(clip.id).arg(suffix.isEmpty() ? QStringLiteral("wav") : suffix));
        QFile f(path);
        if (!f.open(QIODevice::WriteOnly) || f.write(*clip.data) != clip.data->size())
            continue;
        f.close();
        plan.inputs << path;
        const int input = static_cast<int>(plan.inputs.size());  // input 0 is the picture
        const QString label = QStringLiteral("s%1").arg(input);
        const auto ms = std::llround(start * 1000.0);
        parts << QStringLiteral("[%1:a]%2aresample=48000,aformat=sample_fmts=fltp:channel_layouts=stereo,adelay=%3|%3[%4]")
                     .arg(input)
                     .arg(trimFilter(clip) + volumeFilter(gain))
                     .arg(ms)
                     .arg(label);
        labels << QStringLiteral("[%1]").arg(label);
    }
    if (plan.inputs.isEmpty())
        return plan;
    parts << QStringLiteral("%1amix=inputs=%2:duration=longest:normalize=0,apad=whole_dur=%3,atrim=end=%3[mix]")
                 .arg(labels.join(QString()))
                 .arg(labels.size())
                 .arg(number(seconds));
    plan.filterGraph = parts.join(QLatin1Char(';'));
    plan.output = QStringLiteral("mix");
    return plan;
}

AnimationExportResult exportAnimation(const FfmpegTools& tools, const AnimationExportPlan& plan, EncodeSettings settings,
                                      const AudioPlan& audio, const std::function<QImage(int frame)>& picture,
                                      const VideoProgress& progress) {
    AnimationExportResult result;
    settings.size = plan.size;
    settings.rate = plan.rate;
    settings.duration = plan.seconds();
    FrameEncoder encoder(tools);
    if (!encoder.start(settings, audio)) {
        result.error = encoder.errorText();
        return result;
    }
    const int total = static_cast<int>(plan.beats.size());
    int shownFrame = -1;
    QImage frame;
    for (int i = 0; i < total; ++i) {
        if (plan.beats[std::size_t(i)] != shownFrame) {
            shownFrame = plan.beats[std::size_t(i)];
            frame = picture(shownFrame).convertToFormat(QImage::Format_RGBA8888);
        }
        if (!encoder.write(frame)) {
            result.error = encoder.errorText();
            encoder.cancel();
            return result;
        }
        if (progress && !progress(i + 1, total)) {
            encoder.cancel();
            result.cancelled = true;
            return result;
        }
    }
    if (!encoder.finish()) {
        result.error = encoder.errorText();
        return result;
    }
    result.ok = true;
    return result;
}

}
