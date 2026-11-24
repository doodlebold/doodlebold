#pragma once

#include "core/input/InputSample.h"

#include <QPointF>

#include <array>
#include <vector>

namespace doodlebold {

// What pressure comes to for each setting that follows it, as shares of their full values. It's worked out at each pen event and
// interpolated between them, so the path carries the mapped values and not the raw pressure.
struct PressureShares {
    double size = 1.0;
    double density = 1.0;
};

// The centripetal Catmull-Rom segment from p[1] to p[2], with p[0] and p[3] the samples either side, as a line of points about half a
// pixel apart. The first is p[1] and the last is p[2].
std::vector<QPointF> curveBetween(const std::array<QPointF, 4>& p);

}
