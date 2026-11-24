#pragma once

#include "core/ffmpeg/ExternalProcess.h"
#include "core/ffmpeg/Ffmpeg.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSize>
#include <QStringList>

#include <optional>

namespace doodlebold::test {

inline const std::optional<FfmpegTools>& systemFfmpeg() {
    static const std::optional<FfmpegTools> tools = [] {
        const auto found = FfmpegLocator::locate({}, {});
        return found.ok() ? std::optional<FfmpegTools>(found.value()) : std::nullopt;
    }();
    return tools;
}

// What ffprobe says about a written file.
struct ProbedClip {
    QSize size;
    double fps = 0.0;
    double duration = 0.0;
    bool hasAudio = false;
};

inline std::optional<ProbedClip> probeClip(const FfmpegTools& tools, const QString& file) {
    const ProcessOutput out =
        runProcess(tools.ffprobe,
                   {QStringLiteral("-v"), QStringLiteral("error"), QStringLiteral("-print_format"), QStringLiteral("json"),
                    QStringLiteral("-show_streams"), QStringLiteral("-show_format"), mediaUrl(file)},
                   30000);
    if (!out.succeeded())
        return std::nullopt;
    const QJsonObject root = QJsonDocument::fromJson(out.out).object();
    ProbedClip clip;
    clip.duration = root.value(QStringLiteral("format")).toObject().value(QStringLiteral("duration")).toString().toDouble();
    for (const auto& v : root.value(QStringLiteral("streams")).toArray()) {
        const QJsonObject stream = v.toObject();
        if (stream.value(QStringLiteral("codec_type")).toString() == QLatin1String("audio")) {
            clip.hasAudio = true;
        } else if (stream.value(QStringLiteral("codec_type")).toString() == QLatin1String("video")) {
            clip.size = QSize(stream.value(QStringLiteral("width")).toInt(), stream.value(QStringLiteral("height")).toInt());
            const QStringList rate = stream.value(QStringLiteral("r_frame_rate")).toString().split(QLatin1Char('/'));
            if (rate.size() == 2 && rate[1].toDouble() > 0.0)
                clip.fps = rate[0].toDouble() / rate[1].toDouble();
        }
    }
    return clip;
}

}
