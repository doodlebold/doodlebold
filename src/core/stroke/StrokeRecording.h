#pragma once

#include "core/input/InputSample.h"

#include <QByteArray>
#include <QString>

#include <vector>

namespace doodlebold {

// The samples of one stroke, from press to release, as DOODLEBOLD_INPUT_LOG wrote them.
struct RecordedStroke {
    std::vector<InputSample> samples;
};

// The finished pen strokes in a log. Hovering, other pointers and a stroke with no release are left out.
std::vector<RecordedStroke> parseRecording(const QByteArray& log);
std::vector<RecordedStroke> readRecording(const QString& path);

// The same stroke as a pen reporting at `hz` would send it: evenly timed, interpolated, with its first and last samples
// kept. This only simulates another pen, since a real one has its own noise and timing.
RecordedStroke resampledTo(const RecordedStroke& stroke, double hz);

// The stroke without the pressure-0 samples at its end, for systems that don't send one.
RecordedStroke withoutFinalZero(const RecordedStroke& stroke);

}
