#pragma once

namespace doodlebold {

// The brush size list that [ and ] step through, in BrushSettings units (0.1 px). It
// grows roughly 15–25 % per step, so a step feels the same at 3 px and at 300 px.
int steppedSize(int size, int direction);

}
