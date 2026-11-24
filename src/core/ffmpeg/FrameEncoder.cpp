#include "core/ffmpeg/FrameEncoder.h"

#include "core/ffmpeg/ExternalProcess.h"

#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace doodlebold {

namespace {

constexpr int kWriteTimeoutMs = 60000;
constexpr int kFinishTimeoutMs = 30 * 60 * 1000;  // the end of a long encode (FFmpeg flushes its buffered frames)

QString rateText(const FrameRate& rate) {
    return QStringLiteral("%1/%2").arg(rate.num).arg(rate.den);
}

QString seconds(double s) {
    return QString::number(s, 'f', 6);
}

QString lastLines(const QByteArray& err) {
    const QString text = QString::fromUtf8(err).trimmed();
    const QStringList lines = text.split(QLatin1Char('\n'));
    return lines.mid(std::max<qsizetype>(0, lines.size() - 3)).join(QLatin1Char('\n'));
}

}

QString chooseVideoEncoder(const FfmpegTools& tools) {
    for (const char* name : {"libx264", "libopenh264", "mpeg4"})
        if (tools.hasEncoder(QLatin1String(name)))
            return QLatin1String(name);
    return {};
}

int bitrateForFileSize(double maxMegabytes, double seconds, int audioKbps) {
    if (seconds <= 0.0)
        return 0;
    constexpr double kContainerShare = 0.97;  // MP4's own bytes and the encoder's misses
    const double totalKbps = maxMegabytes * 1000.0 * 8.0 * kContainerShare / seconds;  // 1 MB = 10⁶ bytes: the stricter reading
    return std::max(0, static_cast<int>(std::floor(totalKbps - std::max(audioKbps, 0))));
}

QStringList encoderArguments(const EncodeSettings& s, const AudioPlan& audio, const QString& videoEncoder) {
    QStringList args = {QStringLiteral("-hide_banner"), QStringLiteral("-v"),
                        QStringLiteral("error"),        QStringLiteral("-y"),
                        QStringLiteral("-f"),           QStringLiteral("rawvideo"),
                        QStringLiteral("-pix_fmt"),     QStringLiteral("rgba"),
                        QStringLiteral("-s"),           QStringLiteral("%1x%2").arg(s.size.width()).arg(s.size.height()),
                        QStringLiteral("-framerate"),   rateText(s.rate),
                        QStringLiteral("-i"),           QStringLiteral("pipe:0")};
    for (const QString& input : audio.inputs)
        args << QStringLiteral("-i") << mediaUrl(input);
    if (!audio.isEmpty())
        args << QStringLiteral("-filter_complex") << audio.filterGraph;
    args << QStringLiteral("-map") << QStringLiteral("0:v");
    if (!audio.isEmpty())
        args << QStringLiteral("-map") << QStringLiteral("[%1]").arg(audio.output);

    // 4:2:0 needs even sides: an odd canvas loses its last column or row.
    if (s.size.width() % 2 != 0 || s.size.height() % 2 != 0)
        args << QStringLiteral("-vf") << QStringLiteral("crop=%1:%2:0:0").arg(s.size.width() & ~1).arg(s.size.height() & ~1);

    const int keyframes = std::max(1, static_cast<int>(std::lround(2.0 * s.rate.fps())));  // a key frame every 2 s
    const auto rate = [](int kbps) { return QString::number(kbps) + QLatin1Char('k'); };
    args << QStringLiteral("-c:v") << videoEncoder;
    if (videoEncoder == QLatin1String("libx264")) {
        args << QStringLiteral("-preset") << QStringLiteral("medium") << QStringLiteral("-profile:v") << QStringLiteral("high");
        if (s.targetBitrateKbps > 0)
            args << QStringLiteral("-b:v") << rate(s.targetBitrateKbps);
        else
            args << QStringLiteral("-crf") << QString::number(std::clamp(s.crf, 0, 51));
        const int cap = s.maxBitrateKbps > 0 ? s.maxBitrateKbps : 0;
        if (cap > 0)
            args << QStringLiteral("-maxrate") << rate(cap) << QStringLiteral("-bufsize") << rate(2 * cap);
    } else {
        // libopenh264 and mpeg4 have no constant-quality mode: an average bitrate, the cap if there is one, else one that suits
        // the size (about 0.1 bit per pixel per frame).
        int kbps = s.targetBitrateKbps > 0 ? s.targetBitrateKbps
                                           : static_cast<int>(double(s.size.width()) * s.size.height() * s.rate.fps() * 0.1 / 1000.0);
        if (s.maxBitrateKbps > 0)
            kbps = std::min(kbps, s.maxBitrateKbps);
        args << QStringLiteral("-b:v") << rate(std::max(kbps, 100));
        if (s.maxBitrateKbps > 0)
            args << QStringLiteral("-maxrate") << rate(s.maxBitrateKbps) << QStringLiteral("-bufsize") << rate(2 * s.maxBitrateKbps);
    }
    args << QStringLiteral("-g") << QString::number(keyframes) << QStringLiteral("-pix_fmt") << QStringLiteral("yuv420p");
    if (!audio.isEmpty())
        args << QStringLiteral("-c:a") << QStringLiteral("aac") << QStringLiteral("-b:a") << rate(std::max(s.audioKbps, 32));
    if (s.duration > 0.0)
        args << QStringLiteral("-t") << seconds(s.duration);
    args << QStringLiteral("-movflags") << QStringLiteral("+faststart") << QStringLiteral("-f") << QStringLiteral("mp4")
         << mediaUrl(s.output);
    return args;
}

FrameEncoder::FrameEncoder(FfmpegTools tools) : m_tools(std::move(tools)) {}

FrameEncoder::~FrameEncoder() {
    if (m_process && m_process->state() != QProcess::NotRunning)
        cancel();
}

bool FrameEncoder::start(const EncodeSettings& settings, const AudioPlan& audio) {
    m_settings = settings;
    m_error.clear();
    const QString encoder = chooseVideoEncoder(m_tools);
    if (encoder.isEmpty()) {
        m_error = QCoreApplication::translate("FramePipes", "This FFmpeg has no H.264 or MPEG-4 encoder.");
        return false;
    }
    m_process = std::make_unique<QProcess>();
    m_process->setProgram(m_tools.ffmpeg);
    m_process->setArguments(encoderArguments(settings, audio, encoder));
    m_process->setStandardOutputFile(QProcess::nullDevice());
    m_process->start(QIODevice::WriteOnly);
    if (!m_process->waitForStarted(FfmpegLocator::kTimeoutMs)) {
        m_error = QCoreApplication::translate("FramePipes", "FFmpeg could not be started.");
        m_process = nullptr;
        return false;
    }
    return true;
}

bool FrameEncoder::write(const QImage& frame) {
    if (!m_process || m_process->state() == QProcess::NotRunning) {
        if (m_process && m_error.isEmpty())
            m_error = lastLines(m_process->readAllStandardError());
        return false;
    }
    const QImage rgba = frame.format() == QImage::Format_RGBA8888 ? frame : frame.convertToFormat(QImage::Format_RGBA8888);
    if (rgba.size() != m_settings.size) {
        m_error = QStringLiteral("frame size mismatch");
        return false;
    }
    const qsizetype rowBytes = qsizetype(rgba.width()) * 4;
    for (int y = 0; y < rgba.height(); ++y)
        m_process->write(reinterpret_cast<const char*>(rgba.constScanLine(y)), rowBytes);
    while (m_process->bytesToWrite() > 0) {
        if (!m_process->waitForBytesWritten(kWriteTimeoutMs)) {
            m_error = m_process->state() == QProcess::NotRunning ? lastLines(m_process->readAllStandardError())
                                                                 : QCoreApplication::translate("FramePipes", "FFmpeg stopped answering.");
            return false;
        }
    }
    return true;
}

bool FrameEncoder::finish() {
    if (!m_process)
        return false;
    m_process->closeWriteChannel();
    const bool done = m_process->waitForFinished(kFinishTimeoutMs);
    const bool ok = done && m_process->exitStatus() == QProcess::NormalExit && m_process->exitCode() == 0;
    if (!ok && m_error.isEmpty())
        m_error = done ? lastLines(m_process->readAllStandardError()) : QCoreApplication::translate("FramePipes", "FFmpeg did not finish.");
    if (!done)
        m_process->kill();
    m_process = nullptr;
    if (!ok)
        QFile::remove(m_settings.output);
    return ok;
}

void FrameEncoder::cancel() {
    if (m_process) {
        m_process->kill();
        m_process->waitForFinished(2000);
        m_process = nullptr;
    }
    QFile::remove(m_settings.output);
}

Result<double, QString> probeSoundDuration(const FfmpegTools& tools, const QString& file) {
    using R = Result<double, QString>;
    const ProcessOutput out =
        runProcess(tools.ffprobe,
                   {QStringLiteral("-v"), QStringLiteral("error"), QStringLiteral("-print_format"), QStringLiteral("json"),
                    QStringLiteral("-show_streams"), QStringLiteral("-show_format"), mediaUrl(file)},
                   FfmpegLocator::kTimeoutMs * 2);
    if (!out.succeeded())
        return R::failure(QCoreApplication::translate("FrameEncoder", "The file could not be read."));
    const QJsonObject root = QJsonDocument::fromJson(out.out).object();
    bool hasSound = false;
    for (const auto& s : root.value(QStringLiteral("streams")).toArray())
        hasSound = hasSound || s.toObject().value(QStringLiteral("codec_type")).toString() == QLatin1String("audio");
    if (!hasSound)
        return R::failure(QCoreApplication::translate("FrameEncoder", "The file has no sound."));
    const double duration = root.value(QStringLiteral("format")).toObject().value(QStringLiteral("duration")).toString().toDouble();
    if (duration <= 0.0)
        return R::failure(QCoreApplication::translate("FrameEncoder", "The sound's length could not be read."));
    return duration;
}

AudioPlan soundFilePlan(const QString& file, double fileDuration, double duration) {
    AudioPlan plan;
    const double heard = std::min(fileDuration, duration);
    if (heard <= 1e-6)
        return plan;
    plan.inputs << file;
    plan.filterGraph =
        QStringLiteral("[1:a]atrim=start=0:end=%1,asetpts=PTS-STARTPTS,aresample=48000,aformat=sample_fmts=fltp:channel_layouts=stereo,"
                       "apad=whole_dur=%2,atrim=end=%2[mix]")
            .arg(seconds(heard), seconds(duration));
    plan.output = QStringLiteral("mix");
    return plan;
}

}
