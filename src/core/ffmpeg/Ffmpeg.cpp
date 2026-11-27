#include "core/ffmpeg/Ffmpeg.h"

#include "core/ffmpeg/ExternalProcess.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>
#include <QStandardPaths>

namespace doodlebold {

QString describe(FfmpegError error) {
    switch (error) {
    case FfmpegError::NotFound:
        return QCoreApplication::translate("Ffmpeg", "FFmpeg was not found on this computer or in Doodlebold's own folder.");
    case FfmpegError::NotRunnable: return QCoreApplication::translate("Ffmpeg", "The FFmpeg program that was found does not run.");
    case FfmpegError::NoProbe:
        return QCoreApplication::translate("Ffmpeg", "FFmpeg was found, but its companion program ffprobe is missing.");
    case FfmpegError::NotApproved:
        return QCoreApplication::translate("Ffmpeg", "FFmpeg was found on this computer's PATH, and Doodlebold may not run it yet.");
    }
    return {};
}

QString mediaUrl(const QString& localFile) {
    return QStringLiteral("file:") + QDir::toNativeSeparators(QFileInfo(localFile).absoluteFilePath());
}

QString FfmpegLocator::defaultToolsFolder() {
    return QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)).filePath(QStringLiteral("ffmpeg"));
}

QString FfmpegLocator::executableName(const QString& base) {
#ifdef Q_OS_WIN
    return base + QStringLiteral(".exe");
#else
    return base;
#endif
}

namespace {

// The program in `place`: `place` itself if it is a file, else `name` inside it (or inside its bin/ folder, as the FFmpeg
QString programIn(const QString& place, const QString& name) {
    const QFileInfo info(place);
    if (info.isFile())
        return info.absoluteFilePath();
    if (!info.isDir())
        return {};
    for (const QString& candidate : {QDir(place).filePath(name), QDir(place).filePath(QStringLiteral("bin/") + name)})
        if (QFileInfo(candidate).isFile())
            return QFileInfo(candidate).absoluteFilePath();
    return {};
}

}

QString FfmpegLocator::parseVersion(const QByteArray& versionOutput) {
    static const QRegularExpression re(QStringLiteral("^ffmpeg version (\\S+)"), QRegularExpression::MultilineOption);
    const auto m = re.match(QString::fromUtf8(versionOutput));
    return m.hasMatch() ? m.captured(1) : QString();
}

QStringList FfmpegLocator::parseEncoders(const QByteArray& encodersOutput) {
    // Each encoder is a line " V....D libx264   libx264 H.264 …": six flag characters (the first V, A or S), then the name.
    // The legend above the list (" V..... = Video") has the same flags but "=" where the name would be.
    static const QRegularExpression re(QStringLiteral("^ [VAS][F.][S.][X.][B.][D.] ([^=\\s]\\S*)"), QRegularExpression::MultilineOption);
    QStringList names;
    auto it = re.globalMatch(QString::fromUtf8(encodersOutput));
    while (it.hasNext())
        names << it.next().captured(1);
    return names;
}

Result<FfmpegTools, FfmpegError> FfmpegLocator::check(const QString& ffmpegPath, bool probeOnPath) {
    using R = Result<FfmpegTools, FfmpegError>;
    const ProcessOutput version = runProcess(ffmpegPath, {QStringLiteral("-hide_banner"), QStringLiteral("-version")}, kTimeoutMs);
    FfmpegTools tools;
    tools.ffmpeg = ffmpegPath;
    tools.version = parseVersion(version.out);
    if (!version.succeeded() || tools.version.isEmpty())
        return R::failure(FfmpegError::NotRunnable);
    const ProcessOutput encoders = runProcess(ffmpegPath, {QStringLiteral("-hide_banner"), QStringLiteral("-encoders")}, kTimeoutMs);
    tools.encoders = parseEncoders(encoders.out);

    const QString probeName = executableName(QStringLiteral("ffprobe"));
    QString probe = QDir(QFileInfo(ffmpegPath).absolutePath()).filePath(probeName);
    if (!QFileInfo(probe).isFile() && probeOnPath)
        probe = QStandardPaths::findExecutable(QStringLiteral("ffprobe"));
    if (probe.isEmpty() || !runProcess(probe, {QStringLiteral("-version")}, kTimeoutMs).succeeded())
        return R::failure(FfmpegError::NoProbe);
    tools.ffprobe = probe;
    return tools;
}

QString FfmpegLocator::onPath() {
    return QStandardPaths::findExecutable(QStringLiteral("ffmpeg"));
}

Result<FfmpegTools, FfmpegError> FfmpegLocator::locate(const QString& configured, const QString& toolsFolder, const QString& approvedOnPath) {
    const QString name = executableName(QStringLiteral("ffmpeg"));
    for (const QString& place : {configured, toolsFolder}) {
        if (place.isEmpty())
            continue;
        if (const QString program = programIn(place, name); !program.isEmpty())
            return check(program);
    }
    if (const QString found = onPath(); !found.isEmpty()) {
        if (approvedOnPath.isEmpty() || QFileInfo(found).canonicalFilePath() != QFileInfo(approvedOnPath).canonicalFilePath())
            return Result<FfmpegTools, FfmpegError>::failure(FfmpegError::NotApproved);
        return check(found, true);
    }
    return Result<FfmpegTools, FfmpegError>::failure(FfmpegError::NotFound);
}

}
