#include "core/stroke/PressureCurve.h"

#include <algorithm>
#include <cmath>

namespace doodlebold {

namespace {

// Every this many % below 100 doubles the curve's power. Fitted on 2026-11-13 to the reference's readings
// (the Pencil's width by pressure at Size sensitivity 100, 75, 50 and 25 %): 50 matches the 50 % column within 0.3 px and gives the
// least mean difference over the three below 100, 2.7 px; no one value fits 75 and 25 % at once.
constexpr double kDoublingPercent = 50.0;

}

double pressureResponse(double pressure, int sensitivity) {
    const double p = std::clamp(pressure, 0.0, 1.0);
    if (sensitivity >= 100)  // above 100 % it's a plain gain, sensitivity / 100
        return std::min(p * sensitivity / 100.0, 1.0);
    return std::pow(p, std::pow(2.0, (100 - sensitivity) / kDoublingPercent));
}

double pressureShare(double pressure, bool followsPressure, double least, int sensitivity) {
    if (!followsPressure)
        return 1.0;
    return least + (1.0 - least) * pressureResponse(pressure, sensitivity);
}

}
