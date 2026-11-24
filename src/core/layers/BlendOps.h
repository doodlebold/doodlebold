#pragma once

#include "core/layers/BlendMode.h"

namespace doodlebold::blend {

// Composites n source pixels onto n destination pixels in place: premultiplied float RGBA in 0..1, interleaved. opacity scales
// the source alpha.
// TODO(reimplement): each blend mode should combine a layer with what lies below it in its own way (see BlendMode.h). For now
// every mode is plain source-over.
void blendSpan(BlendMode mode, const float* src, float opacity, float* dst, int n);

}
