#pragma once

#include "core/raster/TiledImage.h"
#include "core/selection/SelectionMask.h"

#include <QPoint>
#include <QPointF>
#include <QRect>
#include <QRectF>
#include <QTransform>

#include <array>

// Transform Layer (Resize, Rotate, Freeform, Deform), Flip Horizontal / Vertical and Rotate 90°: pure functions on
// rasters; the layer and its selection go through the same resampler. Pixel (i, j) has its centre at (i + 0.5, j + 0.5).
namespace doodlebold::transform {

// The Transform tool has a quality for the final result and lower ones for the preview and dragging.
// TODO(reimplement): each quality should give a smoother result than the one before it when a layer is scaled or rotated. For now
// every option gives nearest neighbour.
enum class Interpolation {
    Nearest,
    Bilinear,
    Bicubic,
    Lanczos2,
    Lanczos3
};

Interpolation interactiveQuality(Interpolation final, Interpolation preview, Interpolation dragging, bool isDragging);

// Where the corners of the source land (TL, TR, BR, BL). Resize, Rotate, Freeform and Deform all reduce to four corners
// and one bilinear mapping, so this describes any transform.
struct Quad {
    std::array<QPointF, 4> corner;

    static Quad of(const QRectF& r);
    Quad transformed(const QTransform& t) const;
    bool operator==(const Quad&) const = default;
};

// Each destination pixel whose centre lies inside the quad takes the source pixel at that place (nearest neighbour). `perspective`
// blends the mapping towards a projective one. An identity quad reproduces the source exactly.
TiledImage resample(const TiledImage& src, const QRect& sourceRect, const Quad& dest, Interpolation interpolation,
                    double perspective = 0.0);
SelectionMask resample(const SelectionMask& src, const QRect& sourceRect, const Quad& dest, Interpolation interpolation,
                       double perspective = 0.0);

// Flipped or turned by 90° about the area's centre (a quarter turn keeps the centre, rounded down for an odd side difference),
// then shifted by offset. Turn::None with an offset is Cut & Nudge.
enum class Turn {
    None,
    FlipHorizontal,
    FlipVertical,
    Rotate180,
    Clockwise,
    CounterClockwise
};
QRect movedArea(const QRect& area, Turn turn, const QPoint& offset = {});
TiledImage moveExactly(const TiledImage& src, const QRect& area, Turn turn, const QPoint& offset = {});
SelectionMask moveExactly(const SelectionMask& src, const QRect& area, Turn turn, const QPoint& offset = {});
TiledImage moveExactly(const TiledImage& src, const QRect& area, Turn turn, const QPoint& offset, const QSize& target);
SelectionMask moveExactly(const SelectionMask& src, const QRect& area, Turn turn, const QPoint& offset, const QSize& target);

}
