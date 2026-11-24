#pragma once

#include "core/util/Result.h"

#include <QString>
#include <QStringList>

namespace doodlebold {

// FFmpeg runs as a separate program: no library is linked, frames pass through pipes.
struct FfmpegTools {
    QString ffmpeg;
    QString ffprobe;
    QString version;
    QStringList encoders;

    bool isValid() const { return !ffmpeg.isEmpty() && !ffprobe.isEmpty(); }
    bool hasEncoder(const QString& name) const { return encoders.contains(name); }
};

enum class FfmpegError {
    NotFound,
    NotRunnable,
    NoProbe,
};
QString describe(FfmpegError error);

// A local file as FFmpeg's input or output argument: "file:" and the absolute path, so a name that starts with "-" or looks
// like a protocol ("http:…") is still read as a plain file.
QString mediaUrl(const QString& localFile);

// Looked for in order: the configured path (Options), Doodlebold's tools folder, the PATH. The first place with an ffmpeg
// decides: if it is broken the error says so instead of silently using another.
class FfmpegLocator {
public:
    static constexpr int kTimeoutMs = 8000;

    static QString defaultToolsFolder();
    static QString executableName(const QString& base);

    static Result<FfmpegTools, FfmpegError> locate(const QString& configured, const QString& toolsFolder);
    static Result<FfmpegTools, FfmpegError> check(const QString& ffmpegPath);

    static QString parseVersion(const QByteArray& versionOutput);
    static QStringList parseEncoders(const QByteArray& encodersOutput);
};

}
