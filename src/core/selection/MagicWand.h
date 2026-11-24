#pragma once

#include "core/raster/TiledImage.h"
#include "core/selection/SelectionMask.h"
#include "core/selection/WandMode.h"

#include <QPoint>

namespace doodlebold::selection {

struct WandOptions {
    WandMode mode = WandMode::EnclosedTransparent;
    int colourThreshold = 0;    // 0…255: how far a pixel's colour may be from the clicked one's, in its most different channel
    int transparencyRange = 0;  // 0…255: how solid a pixel may be and still count as transparent
    int gapTolerance = 0;       // px: gaps in the lines about this wide are closed
    bool antialias = true;      // a soft edge, one pixel wide, outside the area
    bool eightConnected = false;
};

// The area around `seed` in `source`: the Magic Wand selects it and the Bucket fills it. Pixels are compared by their
// premultiplied colour, so every fully transparent pixel is the same colour. A seed off the image finds nothing.
SelectionMask magicWand(const TiledImage& source, const QPoint& seed, const WandOptions& options);

}
