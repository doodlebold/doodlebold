#include "core/ffmpeg/ExternalProcess.h"

#include <QProcess>

namespace doodlebold {

ProcessOutput runProcess(const QString& program, const QStringList& arguments, int timeoutMs) {
    ProcessOutput result;
    QProcess process;
    process.setProgram(program);
    process.setArguments(arguments);
    process.setStandardInputFile(QProcess::nullDevice());  // FFmpeg asks on stdin when a file exists; never let it wait
    process.start(QIODevice::ReadOnly);
    if (!process.waitForStarted(timeoutMs))
        return result;
    result.started = true;
    if (!process.waitForFinished(timeoutMs)) {
        process.kill();
        process.waitForFinished(1000);
        result.timedOut = true;
    }
    result.out = process.readAllStandardOutput();
    result.err = process.readAllStandardError();
    result.exitCode = process.exitStatus() == QProcess::NormalExit ? process.exitCode() : -1;
    return result;
}

}
