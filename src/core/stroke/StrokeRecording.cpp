#include "core/stroke/StrokeRecording.h"

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>

#include <cmath>
#include <numbers>
#include <optional>

namespace doodlebold {

namespace {

InputSample sampleFrom(const QJsonObject& line) {
    InputSample sample;
    sample.pos = QPointF(line["x"].toDouble(), line["y"].toDouble());
    sample.pressure = line["p"].toDouble();
    sample.tiltX = line["tx"].toDouble();
    sample.tiltY = line["ty"].toDouble();
    // A file may give the pen's lean as azimuth (degrees clockwise from the canvas's top, the way it leans) and altitude (90 upright)
    // instead: Qt's tilts are atan(sin az / tan alt) and -atan(cos az / tan alt).
    if (line.contains("az") && line.contains("alt")) {
        constexpr double kDegree = std::numbers::pi / 180.0;
        const double az = line["az"].toDouble() * kDegree, alt = line["alt"].toDouble() * kDegree;
        sample.tiltX = std::atan2(std::sin(az) * std::cos(alt), std::sin(alt)) / kDegree;
        sample.tiltY = -std::atan2(std::cos(az) * std::cos(alt), std::sin(alt)) / kDegree;
    }
    sample.rotation = line["r"].toDouble();
    sample.timestampMs = static_cast<std::uint64_t>(line["t"].toDouble());
    sample.kind = static_cast<PointerKind>(line["kind"].toInt());
    return sample;
}

double lerp(double a, double b, double t) {
    return a + (b - a) * t;
}

InputSample between(const InputSample& a, const InputSample& b, double t, std::uint64_t timestampMs) {
    InputSample sample = a;
    sample.pos = QPointF(lerp(a.pos.x(), b.pos.x(), t), lerp(a.pos.y(), b.pos.y(), t));
    sample.pressure = lerp(a.pressure, b.pressure, t);
    sample.tiltX = lerp(a.tiltX, b.tiltX, t);
    sample.tiltY = lerp(a.tiltY, b.tiltY, t);
    sample.rotation = lerp(a.rotation, b.rotation, t);
    sample.timestampMs = timestampMs;
    return sample;
}

InputSample sampleAt(const std::vector<InputSample>& samples, double timeMs, std::size_t& after) {
    while (after + 1 < samples.size() && static_cast<double>(samples[after].timestampMs) < timeMs)
        ++after;
    const InputSample& a = samples[after - 1];
    const InputSample& b = samples[after];
    const double span = static_cast<double>(b.timestampMs) - static_cast<double>(a.timestampMs);
    const double t = span > 0.0 ? (timeMs - static_cast<double>(a.timestampMs)) / span : 1.0;
    return between(a, b, t, static_cast<std::uint64_t>(std::llround(timeMs)));
}

}

std::vector<RecordedStroke> parseRecording(const QByteArray& log) {
    std::vector<RecordedStroke> strokes;
    std::optional<RecordedStroke> running;
    for (const QByteArray& text : log.split('\n')) {
        const QJsonObject line = QJsonDocument::fromJson(text).object();
        const QString event = line["e"].toString();
        if (line["kind"].toInt() != static_cast<int>(PointerKind::Pen))
            continue;
        if (event == "press") {
            running = RecordedStroke{{sampleFrom(line)}};
        } else if (running && event == "move") {
            running->samples.push_back(sampleFrom(line));
        } else if (running && event == "release") {
            running->samples.push_back(sampleFrom(line));
            strokes.push_back(std::move(*running));
            running.reset();
        }
    }
    return strokes;
}

std::vector<RecordedStroke> readRecording(const QString& path) {
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? parseRecording(file.readAll()) : std::vector<RecordedStroke>{};
}

RecordedStroke resampledTo(const RecordedStroke& stroke, double hz) {
    const std::vector<InputSample>& samples = stroke.samples;
    if (samples.size() < 2)
        return stroke;
    const double stepMs = 1000.0 / hz;
    const double startMs = static_cast<double>(samples.front().timestampMs);
    const double endMs = static_cast<double>(samples.back().timestampMs);
    RecordedStroke result{{samples.front()}};
    std::size_t after = 1;
    // Stepped by adding, as the recordings the brush pixels replay were resampled.
    for (double timeMs = startMs + stepMs; timeMs < endMs; timeMs += stepMs)  // NOLINT(bugprone-float-loop-counter)
        result.samples.push_back(sampleAt(samples, timeMs, after));
    result.samples.push_back(samples.back());
    return result;
}

RecordedStroke withoutFinalZero(const RecordedStroke& stroke) {
    RecordedStroke result = stroke;
    while (result.samples.size() > 1 && result.samples.back().pressure <= 0.0)
        result.samples.pop_back();
    return result;
}

}
