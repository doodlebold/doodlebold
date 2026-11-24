#pragma once

#include "core/layers/CanvasBackground.h"
#include "core/layers/LayerStack.h"

#include <QColor>
#include <QImage>
#include <QRect>

namespace doodlebold {

// What a GPU can draw a dragged Transform / Move layer from without per-frame CPU work, computed once per session from the
// layer tree as it is when the session starts.
//   below   the canvas as displayed with the transformed layer holding only what the lift leaves behind (its pixels
//           outside the selection; nothing without a selection) and every layer above it hidden: the picture up to and
//           including the transformed layer's place in the stack. Opaque, document size.
//   above   the layers that are painted after the transformed layer (later siblings of it and of its folders),
//           composited over transparency; null when there are none. Premultiplied, document size.
//   source  the lifted pixels inside the transform box, premultiplied, 8 bits per channel (the box size).
// The floating pixels are drawn between `below` and `above`. Approximations during the drag (the exact composite is
// computed on pause / release): the layer's blend mode is Normal (only its opacity applies), `above` is a plain
// source-over of its layers rather than their blend modes against the real backdrop, and a layer clipped to the
// transformed one is drawn unclipped.
struct FloatingPreview {
    QImage below;
    QImage above;
    QImage source;
    QRect box;           // where `source` sits in the document before the transform
    double opacity = 1;  // the layer's opacity, 0 when the layer is hidden
};

// `layers` is the session's tree (the layer `id` in it holds the pixels shown; they are replaced here by `rest`).
FloatingPreview buildFloatingPreview(const LayerStack& layers, LayerId id, const TiledImage& rest, const TiledImage& part, const QRect& box,
                                     const CanvasBackground& background);

}
