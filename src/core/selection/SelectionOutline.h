#pragma once

#include "core/selection/SelectionMask.h"

#include <QPainterPath>

namespace doodlebold::selection {

// Marching ants: the boundary between pixels selected at half coverage or more and the rest, as one closed subpath per boundary
// loop (straight runs merged), so a dashed pen runs continuously and does not flicker zoomed out.
QPainterPath outline(const SelectionMask& mask);

inline bool isInside(int coverage) {
    return coverage >= SelectionMask::kOne / 2;
}

}
