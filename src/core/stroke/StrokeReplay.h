#pragma once

#include "core/raster/TiledImage.h"
#include "core/stroke/StrokeBuffer.h"
#include "core/stroke/StrokeMerge.h"
#include "core/stroke/StrokeRecording.h"

#include <cstdint>
#include <vector>

namespace doodlebold {

// Turns a stroke's samples into density. It may use only those samples: no clock, thread timing or randomness that isn't
// seeded from the stroke, or a replay won't give the same pixels.
class StrokeEvaluator {
public:
    virtual ~StrokeEvaluator() = default;
    virtual void begin(const InputSample& first, StrokeBuffer& buffer) = 0;
    virtual void add(const InputSample& sample, StrokeBuffer& buffer) = 0;
    virtual void end(StrokeBuffer& buffer) = 0;
};

struct ReplayResult {
    TiledImage merged;    // the canvas after the last stroke merged
    TiledImage lastLive;  // the picture just before that merge
};

// Paints the strokes in order onto a copy of `canvas`.
ReplayResult replayStrokes(const std::vector<RecordedStroke>& strokes, StrokeEvaluator& evaluator, const StrokePaint& paint,
                           TiledImage canvas);

// A hash of what the image shows. All-transparent tiles count the same as absent ones.
std::uint64_t pixelHash(const TiledImage& image);

}
