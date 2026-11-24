#pragma once

#include "core/input/InputSample.h"

namespace doodlebold::input_log {

// Debugging aid: DOODLEBOLD_INPUT_LOG=<file> appends every input sample as a JSON line, to replay strokes through the engine.
bool enabled();
void write(const char* event, const InputSample& s);

}
