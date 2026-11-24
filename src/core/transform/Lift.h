#pragma once

#include "core/raster/TiledImage.h"
#include "core/selection/SelectionMask.h"

namespace doodlebold::transform {

// A layer split by a selection (Transform Layer): each pixel divides by its coverage s, part takes s of every channel and
// rest the remainder, so part + rest is exactly the original. Tiles wholly inside or outside are shared.
struct Lifted {
    TiledImage part;
    TiledImage rest;
};
Lifted liftSelection(const TiledImage& layer, const SelectionMask& selection);

// top over base (premultiplied source-over). Not the inverse of liftSelection with a soft selection (the halves overlay instead
// of adding), which is why an unmoved lift is never written back.
TiledImage composeOver(const TiledImage& base, const TiledImage& top);

}
