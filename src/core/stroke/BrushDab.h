#pragma once

#include <algorithm>
#include <array>
#include <cmath>

// docs/brush-engine/brush-dab.md says what the named values here are for and how they were chosen.
namespace doodlebold {

// The reference's Brush click at 200 px (radius 100 px), full pressure, starting settings, one stamp on an empty layer, every 2 px
// from its centre to 140 px, each over its
// centre: rows the 1st to 5th Shapes with Anti-Ripple on, then the 5th with it off. Read every 2 % of a stamp's radius.
inline constexpr int kBrushDabSteps = 71;  // 0 to 140 % of the radius, a step every 2 %
inline constexpr std::array<std::array<float, kBrushDabSteps>, 6> kBrushDab{{
    {{1.0000f, 0.9990f, 0.9958f, 0.9907f, 0.9837f, 0.9748f, 0.9641f, 0.9518f, 0.9377f, 0.9224f, 0.9055f, 0.8873f, 0.8678f, 0.8473f, 0.8259f,
      0.8034f, 0.7801f, 0.7561f, 0.7315f, 0.7063f, 0.6809f, 0.6549f, 0.6289f, 0.6025f, 0.5762f, 0.5499f, 0.5236f, 0.4975f, 0.4717f, 0.4463f,
      0.4212f, 0.3966f, 0.3724f, 0.3489f, 0.3259f, 0.3037f, 0.2822f, 0.2614f, 0.2414f, 0.2221f, 0.2038f, 0.1863f, 0.1696f, 0.1538f, 0.1390f,
      0.1249f, 0.1118f, 0.0995f, 0.0882f, 0.0776f, 0.0679f, 0.0591f, 0.0511f, 0.0436f, 0.0371f, 0.0312f, 0.0261f, 0.0215f, 0.0175f, 0.0140f,
      0.0110f, 0.0086f, 0.0065f, 0.0048f, 0.0035f, 0.0024f, 0.0016f, 0.0010f, 0.0005f, 0.0002f, 0.0001f}},
    {{1.0000f, 1.0000f, 1.0000f, 1.0000f, 1.0000f, 1.0000f, 1.0000f, 1.0000f, 1.0000f, 1.0000f, 1.0000f, 1.0000f, 1.0000f, 1.0000f, 1.0000f,
      1.0000f, 1.0000f, 1.0000f, 1.0000f, 1.0000f, 1.0000f, 1.0000f, 1.0000f, 1.0000f, 1.0000f, 0.9996f, 0.9940f, 0.9805f, 0.9598f, 0.9328f,
      0.8998f, 0.8620f, 0.8199f, 0.7740f, 0.7257f, 0.6755f, 0.6238f, 0.5716f, 0.5196f, 0.4682f, 0.4183f, 0.3702f, 0.3242f, 0.2810f, 0.2407f,
      0.2035f, 0.1697f, 0.1393f, 0.1124f, 0.0888f, 0.0687f, 0.0518f, 0.0379f, 0.0267f, 0.0180f, 0.0115f, 0.0067f, 0.0037f, 0.0018f, 0.0007f,
      0.0002f, 0.0000f, 0.0000f, 0.0000f, 0.0000f, 0.0000f, 0.0000f, 0.0000f, 0.0000f, 0.0000f, 0.0000f}},
    {{1.0000f, 1.0000f, 1.0000f, 1.0000f, 1.0000f, 1.0000f, 1.0000f, 1.0000f, 1.0000f, 1.0000f, 1.0000f, 1.0000f, 1.0000f, 1.0000f, 1.0000f,
      1.0000f, 1.0000f, 1.0000f, 1.0000f, 1.0000f, 1.0000f, 1.0000f, 1.0000f, 1.0000f, 1.0000f, 1.0000f, 1.0000f, 1.0000f, 1.0000f, 1.0000f,
      1.0000f, 1.0000f, 1.0000f, 1.0000f, 1.0000f, 1.0000f, 1.0000f, 1.0000f, 0.9896f, 0.9511f, 0.8890f, 0.8078f, 0.7146f, 0.6139f, 0.5118f,
      0.4131f, 0.3211f, 0.2394f, 0.1697f, 0.1133f, 0.0702f, 0.0394f, 0.0193f, 0.0075f, 0.0020f, 0.0003f, 0.0000f, 0.0000f, 0.0000f, 0.0000f,
      0.0000f, 0.0000f, 0.0000f, 0.0000f, 0.0000f, 0.0000f, 0.0000f, 0.0000f, 0.0000f, 0.0000f, 0.0000f}},
    {{1.0000f, 1.0000f, 1.0000f, 1.0000f, 1.0000f, 1.0000f, 1.0000f, 1.0000f, 1.0000f, 1.0000f, 1.0000f, 1.0000f, 1.0000f, 1.0000f, 1.0000f,
      1.0000f, 1.0000f, 1.0000f, 1.0000f, 1.0000f, 1.0000f, 1.0000f, 1.0000f, 1.0000f, 1.0000f, 1.0000f, 1.0000f, 1.0000f, 1.0000f, 1.0000f,
      1.0000f, 1.0000f, 1.0000f, 1.0000f, 1.0000f, 1.0000f, 1.0000f, 1.0000f, 1.0000f, 1.0000f, 1.0000f, 1.0000f, 1.0000f, 1.0000f, 1.0000f,
      1.0000f, 0.9814f, 0.7924f, 0.5059f, 0.2434f, 0.0762f, 0.0104f, 0.0000f, 0.0000f, 0.0000f, 0.0000f, 0.0000f, 0.0000f, 0.0000f, 0.0000f,
      0.0000f, 0.0000f, 0.0000f, 0.0000f, 0.0000f, 0.0000f, 0.0000f, 0.0000f, 0.0000f, 0.0000f, 0.0000f}},
    {{1.0000f, 1.0000f, 1.0000f, 1.0000f, 1.0000f, 1.0000f, 1.0000f, 1.0000f, 1.0000f, 1.0000f, 1.0000f, 1.0000f, 1.0000f, 1.0000f, 1.0000f,
      1.0000f, 1.0000f, 1.0000f, 1.0000f, 1.0000f, 1.0000f, 1.0000f, 1.0000f, 1.0000f, 1.0000f, 1.0000f, 1.0000f, 1.0000f, 1.0000f, 1.0000f,
      1.0000f, 1.0000f, 1.0000f, 1.0000f, 1.0000f, 1.0000f, 1.0000f, 1.0000f, 1.0000f, 1.0000f, 1.0000f, 1.0000f, 1.0000f, 1.0000f, 1.0000f,
      1.0000f, 1.0000f, 1.0000f, 1.0000f, 1.0000f, 0.1273f, 0.0000f, 0.0000f, 0.0000f, 0.0000f, 0.0000f, 0.0000f, 0.0000f, 0.0000f, 0.0000f,
      0.0000f, 0.0000f, 0.0000f, 0.0000f, 0.0000f, 0.0000f, 0.0000f, 0.0000f, 0.0000f, 0.0000f, 0.0000f}},
    {{1.0000f, 1.0000f, 1.0000f, 1.0000f, 1.0000f, 1.0000f, 1.0000f, 1.0000f, 1.0000f, 1.0000f, 1.0000f, 1.0000f, 1.0000f, 1.0000f, 1.0000f,
      1.0000f, 1.0000f, 1.0000f, 1.0000f, 1.0000f, 1.0000f, 1.0000f, 1.0000f, 1.0000f, 1.0000f, 1.0000f, 1.0000f, 1.0000f, 1.0000f, 1.0000f,
      1.0000f, 1.0000f, 1.0000f, 1.0000f, 1.0000f, 1.0000f, 1.0000f, 1.0000f, 1.0000f, 1.0000f, 1.0000f, 1.0000f, 1.0000f, 1.0000f, 1.0000f,
      1.0000f, 1.0000f, 1.0000f, 1.0000f, 1.0000f, 0.1610f, 0.0000f, 0.0000f, 0.0000f, 0.0000f, 0.0000f, 0.0000f, 0.0000f, 0.0000f, 0.0000f,
      0.0000f, 0.0000f, 0.0000f, 0.0000f, 0.0000f, 0.0000f, 0.0000f, 0.0000f, 0.0000f, 0.0000f, 0.0000f}},
}};
// Their centres, the stamp's own opacity at full Density and pressure, the same rows.
inline constexpr std::array<float, 6> kBrushDabCentre{0.9557f, 0.5651f, 0.3476f, 0.2206f, 0.2882f, 0.1602f};

// A Brush dab row for its Shape (0 to 4, the 1st to 5th) and Anti-Ripple, and its value at `along` of the radius, read between steps.
inline int brushDabRow(int edge, bool antiRipple) {
    return edge < 4 ? (edge < 0 ? 0 : edge) : antiRipple ? 4 : 5;
}
inline float brushDabAt(int row, float along) {
    const float t = along * 50.0f;  // 2 % of the radius a step
    if (!(t < static_cast<float>(kBrushDabSteps - 1)))
        return 0.0f;
    const int i = static_cast<int>(t);
    const auto& r = kBrushDab[static_cast<std::size_t>(row)];
    return r[static_cast<std::size_t>(i)] +
           (r[static_cast<std::size_t>(i) + 1] - r[static_cast<std::size_t>(i)]) * (t - static_cast<float>(i));
}
// Where a row's dab ends, as a share of the radius: past its last step above none, brushDabAt reads none.
inline float brushDabEnd(int row) {
    static const std::array<float, 6> ends = [] {
        std::array<float, 6> found{};
        for (std::size_t r = 0; r < kBrushDab.size(); ++r) {
            int last = -1;
            for (int i = 0; i < kBrushDabSteps; ++i)
                if (kBrushDab[r][static_cast<std::size_t>(i)] > 0.0f)
                    last = i;
            found[r] = static_cast<float>(std::min(last + 1, kBrushDabSteps - 1)) / 50.0f;
        }
        return found;
    }();
    return ends[static_cast<std::size_t>(row)];
}
// The row of the 5th Shape with Anti-Ripple, laid with the dab Brush's skirts (DabTip.cpp's kBrushPlateEdgeEnd).
inline constexpr int kBrushDabAntiRippleRow = 4;
// Water Color laid in groups, 1st to 5th Shape: its stamp is the Brush's at full pressure times this.
inline constexpr std::array<double, 5> kWaterStampShare{0.952, 1.275, 1.311, 1.303, 1.308};
// The power of pressure's share of Density that Water Color's stamp follows, by Shape.
inline constexpr std::array<double, 5> kWaterShapePressurePower{0.44, 0.85, 1.0, 1.0, 1.0};
inline constexpr float kBrushDabReach = 1.4f;  // of the radius: the reference's clicks are gone by 136 % on the softest Shape

// The reference solves a Brush stamp's opacity so a straight line of them builds to a target: Density times the
// pressure's share, read here against pen pressure. Rows: the 1st to 5th Shapes, then the 5th with Anti-Ripple off.
inline constexpr std::array<float, 7> kBrushPressureSteps{0.0f, 0.1f, 0.25f, 0.375f, 0.5f, 0.6f, 0.625f};
inline constexpr std::array<std::array<float, 7>, 6> kBrushTargetShare{{
    {{0.0f, 0.122f, 0.316f, 0.498f, 0.703f, 0.916f, 1.0f}},
    {{0.0f, 0.1365f, 0.3485f, 0.5395f, 0.7425f, 0.935f, 1.0f}},
    {{0.0f, 0.151f, 0.381f, 0.581f, 0.782f, 0.954f, 1.0f}},
    {{0.0f, 0.148f, 0.3735f, 0.5715f, 0.7735f, 0.95f, 1.0f}},
    {{0.0f, 0.145f, 0.366f, 0.562f, 0.765f, 0.946f, 1.0f}},
    {{0.0f, 0.145f, 0.366f, 0.562f, 0.765f, 0.946f, 1.0f}},
}};
inline double brushTargetShare(int row, double pressure) {
    const auto& t = kBrushTargetShare[static_cast<std::size_t>(row)];
    if (pressure >= kBrushPressureSteps.back())
        return 1.0;
    for (std::size_t i = 1; i < kBrushPressureSteps.size(); ++i)
        if (pressure <= kBrushPressureSteps[i]) {
            const double a = kBrushPressureSteps[i - 1], b = kBrushPressureSteps[i];
            return t[i - 1] + (t[i] - t[i - 1]) * (std::max(pressure, 0.0) - a) / (b - a);
        }
    return 1.0;
}
// How far a straight line of stamps of `opacity`, a step of `spacingShare` of the diameter apart, takes a pixel on its path, each stamp
// its share of what's left there: averaged over kBrushLinePhases places between two stamps.
inline constexpr int kBrushLinePhases = 8;
inline double brushLineBuild(int row, double spacingShare, double opacity) {
    const double step = 2.0 * spacingShare;  // in radii
    double total = 0.0;
    for (int k = 0; k < kBrushLinePhases; ++k) {
        const double phase = step * k / kBrushLinePhases;
        double keep = 1.0;
        for (int j = -static_cast<int>(kBrushDabReach / step) - 2; j * step + phase < kBrushDabReach; ++j)
            keep *= 1.0 - opacity * brushDabAt(row, static_cast<float>(std::abs(j * step + phase)));
        total += 1.0 - keep;
    }
    return total / kBrushLinePhases;
}
// The solve's target read to this fraction (StampTip.cpp): a 16-bit channel's step, below anything a pixel shows.
inline constexpr double kBrushTargetSteps = 65536.0;
// The opacity whose line builds to `targetShare` of the line the full-pressure stamp (kBrushDabCentre) builds: found by halving.
inline constexpr int kBrushSolveHalvings = 30;
inline double brushDabOpacity(int row, double spacingShare, double targetShare) {
    const double target =
        std::clamp(targetShare, 0.0, 1.0) * brushLineBuild(row, spacingShare, kBrushDabCentre[static_cast<std::size_t>(row)]);
    double low = 0.0, high = 1.0;
    for (int i = 0; i < kBrushSolveHalvings; ++i) {
        const double middle = 0.5 * (low + high);
        (brushLineBuild(row, spacingShare, middle) < target ? low : high) = middle;
    }
    return 0.5 * (low + high);
}

}
