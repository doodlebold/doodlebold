#pragma once

#include <QImage>

#include <memory>
#include <vector>

// docs/brush-engine/bristles.md says what the named values here are for and how they were chosen.
namespace doodlebold {

// A bristle map's hairs. Each dark pixel of the picture is one hair, a disc in each of the tool's stamps (StampWalk::withHairs).
// Its place is the pixel's, the picture's longest side spanning kHairSpreadShare of the tool's size: `across` to the stroke's right,
// `along` behind its centre, both in halves of that span.
struct Hair {
    double across = 0.0;
    double along = 0.0;
};

struct Bristles {
    std::vector<Hair> hairs;
};

// The hairs of a bristle map, or none if it has no dark pixel.
std::shared_ptr<const Bristles> bristlesOf(const QImage& picture);

// How far a bristle map reaches: its longest side against the tool's size, at the pen's pressure.
inline constexpr double kHairSpreadShare = 1.195;
// A hair's full width against the tool's Size, at Hair 100 % and full pressure; Hair scales it straight.
inline constexpr double kHairWidthShare = 0.180;

}
