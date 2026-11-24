#include "core/color/ColorState.h"

#include "core/color/ColorModels.h"

#include <cmath>

namespace doodlebold {

ColorState::ColorState(QObject* parent) : QObject(parent) {}

void ColorState::setColor(const QColor& color) {
    setColor(color, color::hueOf(color, m_hue));
}

void ColorState::setColor(const QColor& color, double hue) {
    const QColor c = color.toRgb();
    hue = std::fmod(std::fmod(hue, 360.0) + 360.0, 360.0);
    if (c == m_color && hue == m_hue)
        return;
    m_color = c;
    m_hue = hue;
    emit changed();
}

}
