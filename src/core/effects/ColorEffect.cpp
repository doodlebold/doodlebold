#include "core/effects/ColorEffect.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace doodlebold {

namespace {

constexpr float kLumaR = 0.299f, kLumaG = 0.587f, kLumaB = 0.114f;
constexpr float kColorizeSaturation = 0.25f;  // Colorize's saturation with the Saturation slider at 0
constexpr float kNoiseAtFull = 0.5f;          // Add Noise's standard deviation at Amount 100 %
constexpr float kSteepestContrast = 0.995f;   // Contrast 100 stops short of a vertical curve

float clamp01(float v) {
    return std::clamp(v, 0.0f, 1.0f);
}

Rgb clamped(Rgb c) {
    return {clamp01(c.r), clamp01(c.g), clamp01(c.b)};
}

template <typename F> Rgb each(Rgb c, F&& f) {
    return {f(c.r), f(c.g), f(c.b)};
}

// Towards 1 by t when t > 0, towards 0 by -t when t < 0: the Lightness, Saturation and Brightness sliders.
float pushed(float v, float t) {
    return t >= 0 ? v + (1.0f - v) * t : v * (1.0f + t);
}

float hueToChannel(float p, float q, float t) {
    if (t < 0)
        t += 1;
    if (t > 1)
        t -= 1;
    if (t < 1.0f / 6)
        return p + (q - p) * 6 * t;
    if (t < 0.5f)
        return q;
    if (t < 2.0f / 3)
        return p + (q - p) * (2.0f / 3 - t) * 6;
    return p;
}

std::uint32_t mix(std::uint32_t h) {
    h ^= h >> 16;
    h *= 0x7feb352dU;
    h ^= h >> 15;
    h *= 0x846ca68bU;
    h ^= h >> 16;
    return h;
}

float uniform(std::uint32_t h) {
    return static_cast<float>(h >> 8) * (1.0f / 16777216.0f);
}

}

float luma(Rgb c) {
    return kLumaR * c.r + kLumaG * c.g + kLumaB * c.b;
}

Hsl toHsl(Rgb c) {
    const float hi = std::max({c.r, c.g, c.b}), lo = std::min({c.r, c.g, c.b});
    Hsl out;
    out.l = (hi + lo) / 2;
    const float span = hi - lo;
    if (span <= 0)
        return out;
    out.s = out.l > 0.5f ? span / (2 - hi - lo) : span / (hi + lo);
    float h = 0;
    if (hi == c.r)
        h = (c.g - c.b) / span + (c.g < c.b ? 6 : 0);
    else if (hi == c.g)
        h = (c.b - c.r) / span + 2;
    else
        h = (c.r - c.g) / span + 4;
    out.h = h * 60;
    return out;
}

Rgb fromHsl(Hsl c) {
    if (c.s <= 0)
        return {c.l, c.l, c.l};
    const float q = c.l < 0.5f ? c.l * (1 + c.s) : c.l + c.s - c.l * c.s;
    const float p = 2 * c.l - q;
    const float h = c.h / 360;
    return {hueToChannel(p, q, h + 1.0f / 3), hueToChannel(p, q, h), hueToChannel(p, q, h - 1.0f / 3)};
}

// The sum of four uniform numbers, centred and scaled to a variance of 1: close enough to a normal distribution for grain.
float gaussianNoise(int x, int y, int channel, std::uint32_t seed) {
    std::uint32_t h = mix(static_cast<std::uint32_t>(x) * 0x9e3779b1U ^ mix(static_cast<std::uint32_t>(y) + seed * 0x85ebca77U) ^
                          static_cast<std::uint32_t>(channel) * 0xc2b2ae3dU);
    float sum = 0;
    for (int i = 0; i < 4; ++i) {
        sum += uniform(h);
        h = mix(h + 0x27d4eb2fU);
    }
    return (sum - 2.0f) * std::numbers::sqrt3_v<float>;
}

ColorEffect::ColorEffect(const EffectSettings& settings) : m_s(settings) {
    const auto v = [&](int i) { return static_cast<float>(settings.value(i)); };
    switch (settings.kind) {
    case EffectKind::BrightnessContrast:
        m_a = v(0) / 100;
        m_b = std::tan((std::min(v(1) / 100, kSteepestContrast) + 1) * std::numbers::pi_v<float> / 4);
        break;
    case EffectKind::HueSaturation:
        m_a = v(0);
        m_b = v(1) / 100;
        m_c = v(2) / 100;
        break;
    case EffectKind::Levels:
        m_a = v(0) / 255;
        m_b = v(1) / 255;
        m_c = 1 / std::max(v(2), 0.01f);
        m_d = v(3) / 255;
        m_e = v(4) / 255;
        break;
    case EffectKind::ColorBalance:
        m_a = std::exp2(-v(0) / 100);
        m_b = std::exp2(-v(1) / 100);
        m_c = std::exp2(-v(2) / 100);
        break;
    case EffectKind::Posterize: m_a = std::max(2.0f, std::round(v(0))) - 1; break;
    case EffectKind::AddNoise: m_a = v(0) / 100 * kNoiseAtFull; break;
    default: break;
    }
}

Rgb ColorEffect::operator()(Rgb c, int x, int y) const {
    switch (m_s.kind) {
    case EffectKind::BrightnessContrast: return brightnessContrast(c);
    case EffectKind::HueSaturation: return m_s.option ? colorize(c) : hueSaturation(c);
    case EffectKind::Levels: return levels(c);
    case EffectKind::ColorBalance: return colorBalance(c);
    case EffectKind::Invert: return {1 - c.r, 1 - c.g, 1 - c.b};
    case EffectKind::Desaturate: {
        const float y601 = luma(c);
        return {y601, y601, y601};
    }
    case EffectKind::Posterize: return posterize(c);
    case EffectKind::AddNoise: return noise(c, x, y);
    default: return c;
    }
}

// Brightness pushes every channel towards white or black; Contrast then turns the line through middle grey, steeper or
// flatter: 45° turned by Contrast's share of the 45° left either way.
Rgb ColorEffect::brightnessContrast(Rgb c) const {
    return each(c, [&](float v) { return clamp01(0.5f + (pushed(v, m_a) - 0.5f) * m_b); });
}

Rgb ColorEffect::hueSaturation(Rgb c) const {
    Hsl hsl = toHsl(c);
    hsl.h = std::fmod(hsl.h + m_a + 360.0f, 360.0f);
    hsl.s = clamp01(pushed(hsl.s, m_b));
    hsl.l = clamp01(pushed(hsl.l, m_c));
    return clamped(fromHsl(hsl));
}

// Colorize: every pixel takes the Hue slider's hue (0° red) at a quarter saturation, which the Saturation slider moves, and
// keeps its luma as its lightness, which the Lightness slider moves.
Rgb ColorEffect::colorize(Rgb c) const {
    Hsl hsl;
    hsl.h = std::fmod(m_a + 360.0f, 360.0f);
    hsl.s = clamp01(pushed(kColorizeSaturation, m_b));
    hsl.l = clamp01(pushed(luma(c), m_c));
    return clamped(fromHsl(hsl));
}

Rgb ColorEffect::levels(Rgb c) const {
    const float span = m_b - m_a;
    return each(c, [&](float v) {
        float t = span > 0 ? clamp01((v - m_a) / span) : (v >= m_a ? 1.0f : 0.0f);
        t = std::pow(t, m_c);
        return clamp01(m_d + t * (m_e - m_d));
    });
}

// Each pair's slider bends its channel's middle tones (a gamma curve, black and white stay); Preserve Luminosity then moves
// all three channels together so the pixel keeps its luma.
Rgb ColorEffect::colorBalance(Rgb c) const {
    Rgb out{std::pow(c.r, m_a), std::pow(c.g, m_b), std::pow(c.b, m_c)};
    if (m_s.option) {
        const float shift = luma(c) - luma(out);
        out = each(out, [&](float v) { return v + shift; });
    }
    return clamped(out);
}

Rgb ColorEffect::posterize(Rgb c) const {
    return each(c, [&](float v) { return std::round(v * m_a) / m_a; });
}

Rgb ColorEffect::noise(Rgb c, int x, int y) const {
    if (m_s.option) {
        const float n = gaussianNoise(x, y, 0, m_s.seed) * m_a;
        return clamped(each(c, [&](float v) { return v + n; }));
    }
    return clamped({c.r + gaussianNoise(x, y, 0, m_s.seed) * m_a, c.g + gaussianNoise(x, y, 1, m_s.seed) * m_a,
                    c.b + gaussianNoise(x, y, 2, m_s.seed) * m_a});
}

}
