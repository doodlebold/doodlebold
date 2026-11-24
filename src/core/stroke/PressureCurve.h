#pragma once

namespace doodlebold {

// The part of the pen's pressure the painter uses: `low` and under counts as none, and `high` and over as full.
struct PressureRange {
    double low = 0.0;
    double high = 1.0;
};

// How a setting follows pressure, both 0 to 1, at a sensitivity from 0 to 200 %. Below 100 % it bends into a curve, so a light touch
// barely registers. At 100 % it's a straight line. Above 100 % it's a gain that reaches full early and stays there.
double pressureResponse(double pressure, int sensitivity);

// What a setting with a pressure switch and a minimum comes to at this pressure, as a share of its full value: 1 with the switch off.
double pressureShare(double pressure, bool followsPressure, double least, int sensitivity);

}
