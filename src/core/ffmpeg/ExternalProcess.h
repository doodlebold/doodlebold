#pragma once

#include <QByteArray>
#include <QString>
#include <QStringList>

namespace doodlebold {

struct ProcessOutput {
    bool started = false;
    bool timedOut = false;
    int exitCode = -1;
    QByteArray out;
    QByteArray err;

    bool succeeded() const { return started && !timedOut && exitCode == 0; }
};

// Runs program to the end (no shell, so nothing in the arguments is interpreted) and collects its output; killed after
// timeoutMs. For the short FFmpeg calls; long ones stream through pipes.
ProcessOutput runProcess(const QString& program, const QStringList& arguments, int timeoutMs);

}
