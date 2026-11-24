#pragma once

#include "core/effects/EffectSettings.h"

#include <cstdint>

namespace doodlebold {

// A straight (not premultiplied) colour, each channel 0 … 1.
struct Rgb {
    float r = 0, g = 0, b = 0;
};

// The colour effects and Add Noise: each pixel's colour changes on its own, and its alpha stays as it is.
class ColorEffect {
public:
    explicit ColorEffect(const EffectSettings& settings);

    // x, y: the pixel, which only Add Noise's grain depends on.
    Rgb operator()(Rgb c, int x, int y) const;

private:
    Rgb brightnessContrast(Rgb c) const;
    Rgb hueSaturation(Rgb c) const;
    Rgb colorize(Rgb c) const;
    Rgb levels(Rgb c) const;
    Rgb colorBalance(Rgb c) const;
    Rgb posterize(Rgb c) const;
    Rgb noise(Rgb c, int x, int y) const;

    EffectSettings m_s;
    float m_a = 0, m_b = 0, m_c = 0, m_d = 0, m_e = 0;  // the settings as the formulas use them
};

// Luma by Rec. 601's weights: what Desaturate keeps.
float luma(Rgb c);

// A hue in degrees [0, 360), saturation and lightness 0 … 1, as HSL defines them.
struct Hsl {
    float h = 0, s = 0, l = 0;
};
Hsl toHsl(Rgb c);
Rgb fromHsl(Hsl c);

// A number from a standard normal distribution, the same every time for the same pixel, channel and seed.
float gaussianNoise(int x, int y, int channel, std::uint32_t seed);

}
