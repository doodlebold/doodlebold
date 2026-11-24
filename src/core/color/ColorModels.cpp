#include "core/color/ColorModels.h"

#include <algorithm>
#include <cmath>

namespace doodlebold::color {

namespace {

int channel(double v) {
    return std::clamp(int(std::lround(v)), 0, 255);
}

QColor fromHueRange(double hue, double max, double min) {
    hue = std::fmod(std::fmod(hue, 360.0) + 360.0, 360.0);
    const double c = max - min;
    const double h = hue / 60.0;
    const double x = c * (1.0 - std::abs(std::fmod(h, 2.0) - 1.0));
    double r = 0, g = 0, b = 0;
    switch (int(h)) {
    case 0: r = c, g = x; break;
    case 1: r = x, g = c; break;
    case 2: g = c, b = x; break;
    case 3: g = x, b = c; break;
    case 4: r = x, b = c; break;
    default: r = c, b = x; break;
    }
    return QColor(channel(r + min), channel(g + min), channel(b + min));
}

struct Range {
    int max, min;
};

Range rangeOf(const QColor& c) {
    return {std::max({c.red(), c.green(), c.blue()}), std::min({c.red(), c.green(), c.blue()})};
}

}

QColor fromHsv(double hue, double s, double v) {
    const double max = std::clamp(v, 0.0, 1.0) * 255.0;
    return fromHueRange(hue, max, max * (1.0 - std::clamp(s, 0.0, 1.0)));
}

QColor fromHsl(double hue, double s, double l) {
    l = std::clamp(l, 0.0, 1.0);
    const double chroma = (1.0 - std::abs(2.0 * l - 1.0)) * std::clamp(s, 0.0, 1.0) * 255.0;
    const double min = l * 255.0 - chroma / 2.0;
    return fromHueRange(hue, min + chroma, min);
}

double hsvSaturation(const QColor& c) {
    const auto [max, min] = rangeOf(c);
    return max == 0 ? 0.0 : double(max - min) / max;
}

double hslSaturation(const QColor& c) {
    const auto [max, min] = rangeOf(c);
    const double denom = 255.0 - std::abs(max + min - 255.0);
    return denom <= 0.0 ? 0.0 : (max - min) / denom;
}

double value(const QColor& c) {
    return rangeOf(c).max / 255.0;
}

double lightness(const QColor& c) {
    const auto [max, min] = rangeOf(c);
    return (max + min) / 510.0;
}

double hueOf(const QColor& c, double fallback) {
    const auto [max, min] = rangeOf(c);
    const double chroma = max - min;
    if (chroma <= 0.0)
        return fallback;
    double h;
    if (max == c.red())
        h = std::fmod((c.green() - c.blue()) / chroma, 6.0);
    else if (max == c.green())
        h = (c.blue() - c.red()) / chroma + 2.0;
    else
        h = (c.red() - c.green()) / chroma + 4.0;
    h *= 60.0;
    return h < 0.0 ? h + 360.0 : h;
}

SliderValues slidersFor(SliderMode mode, const QColor& c, double fallbackHue) {
    SliderValues v;
    v.hue = hueOf(c, fallbackHue);
    switch (mode) {
    case SliderMode::Hsv: v.second = hsvSaturation(c), v.third = value(c); break;
    case SliderMode::Hsl: v.second = hslSaturation(c), v.third = lightness(c); break;
    }
    return v;
}

QColor fromSliders(SliderMode mode, const SliderValues& v) {
    return mode == SliderMode::Hsl ? fromHsl(v.hue, v.second, v.third) : fromHsv(v.hue, v.second, v.third);
}

}
