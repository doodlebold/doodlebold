#include "core/input/InputLog.h"

#include <QFile>
#include <QString>

#include <cstdio>

namespace doodlebold::input_log {

namespace {

QFile* file() {
    static QFile* f = []() -> QFile* {
        const QString path = qEnvironmentVariable("DOODLEBOLD_INPUT_LOG");
        if (path.isEmpty())
            return nullptr;
        auto* out = new QFile(path);
        if (!out->open(QIODevice::Append | QIODevice::Text)) {
            std::fprintf(stderr, "DOODLEBOLD_INPUT_LOG: cannot open %s\n", qPrintable(path));
            delete out;
            return nullptr;
        }
        return out;
    }();
    return f;
}

}

bool enabled() {
    return file() != nullptr;
}

void write(const char* event, const InputSample& s) {
    QFile* f = file();
    if (!f)
        return;
    char line[256];
    const int n = std::snprintf(
        line, sizeof line, "{\"e\":\"%s\",\"t\":%llu,\"x\":%.3f,\"y\":%.3f,\"p\":%.5f,\"tx\":%.2f,\"ty\":%.2f,\"r\":%.2f,\"kind\":%d}\n",
        event, static_cast<unsigned long long>(s.timestampMs), s.pos.x(), s.pos.y(), s.pressure, s.tiltX, s.tiltY, s.rotation,
        static_cast<int>(s.kind));
    f->write(line, n);
    if (event[0] != 'm')
        f->flush();
}

}
