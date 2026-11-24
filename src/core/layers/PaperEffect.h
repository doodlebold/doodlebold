#pragma once

#include "core/layers/LayerPaper.h"

namespace doodlebold::paper {

// TODO(reimplement): a layer's paper should show the chosen texture through the layer's colours, as if painted on that surface,
// with Intensity setting how strongly it shows and Scale how large its grain is. For now it does nothing.
inline void applyToTile(const LayerPaper&, int /*originX*/, int /*originY*/, int /*tileSize*/, float* /*rgba*/) {}

}
