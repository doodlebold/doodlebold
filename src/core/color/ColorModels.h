#pragma once

#include <QColor>

namespace doodlebold::color {

enum class SliderMode {
    Hsv,
    Hsl
};

// TODO(reimplement): the colour wheel's square should offer colours in the way that feels best for painting. For now it is plain HSV.
QColor fromHsv(double hue, double s, double v);
QColor fromHsl(double hue, double s, double l);
double hsvSaturation(const QColor& c);
double hslSaturation(const QColor& c);
double value(const QColor& c);
double lightness(const QColor& c);

// The hue of a colour; `fallback` for greys, white and black, which have none (the last hue is kept then).
double hueOf(const QColor& c, double fallback);

struct SliderValues {
    double hue = 0.0;
    double second = 0.0;
    double third = 0.0;
};
SliderValues slidersFor(SliderMode mode, const QColor& c, double fallbackHue);
QColor fromSliders(SliderMode mode, const SliderValues& v);

}
