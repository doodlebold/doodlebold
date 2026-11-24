#pragma once

#include "core/raster/TiledImage.h"
#include "core/selection/SelectionMask.h"

#include <QPolygonF>
#include <QRectF>
#include <QSize>

#include <optional>

// Pure functions from masks to masks: no document, no undo. Every function returns a squeezed mask; shapes outside the canvas
// are clipped.
namespace doodlebold::selection {

// How a new shape combines with the current selection (plain, Shift = add, Alt = subtract, Alt+Shift = intersect).
enum class CombineMode {
    Replace,
    Add,
    Subtract,
    Intersect
};

SelectionMask all(const QSize& canvas);
SelectionMask invert(const SelectionMask& m);

// Anti-aliased: each pixel's coverage is the exact overlap area. Not anti-aliased: selected when its centre is inside (left and
// top edges inclusive).
SelectionMask rectangle(const QSize& canvas, const QRectF& r, bool antialias);
// Marquee: dragging from pixel `a` to pixel `b` selects the inclusive pixel rectangle between them, hard-edged (a drag
// (100,80) → (300,200) selects 201 × 121 pixels).
SelectionMask marquee(const QSize& canvas, const QPoint& a, const QPoint& b);
// Lasso: hard-edged, a pixel is selected when the polygon covers at least half of it.
// TODO(reimplement): the lasso's Smooth Edge option should give it a soft edge.
SelectionMask polygon(const QSize& canvas, const QPolygonF& p, Qt::FillRule rule = Qt::OddEvenFill);
// Each pixel's coverage is the area of it inside the polygon.
SelectionMask polygonCoverage(const QSize& canvas, const QPolygonF& p, Qt::FillRule rule = Qt::OddEvenFill);

// Coverage = the layer's alpha.
SelectionMask fromLayerAlpha(const TiledImage& layer);

// Coverage by coverage: Add a + b, up to full; Subtract a − b, down to none; Intersect the smaller of the two.
SelectionMask combine(const std::optional<SelectionMask>& current, const SelectionMask& shape, CombineMode mode);

// Grow / Shrink Selection: the largest / smallest value within a disc of radius n. Outside the canvas counts as selected, so
// the border does not erode.
SelectionMask dilate(const SelectionMask& m, int n);
SelectionMask erode(const SelectionMask& m, int n);

}
