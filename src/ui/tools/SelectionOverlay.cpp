#include "ui/tools/SelectionOverlay.h"

#include "core/selection/SelectionOutline.h"
#include "ui/Theme.h"

#include <QPainter>
#include <QPen>

#include <algorithm>
#include <cstdint>
#include <vector>

namespace doodlebold {

void paintAnts(QPainter& painter, const QPainterPath& path, int phase) {
    if (path.isEmpty())
        return;
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, false);
    painter.setBrush(Qt::NoBrush);
    QPen light(Theme::color(Theme::Token::AntLight), 1.0);
    light.setCosmetic(true);
    painter.setPen(light);
    painter.drawPath(path);
    QPen dark(Theme::color(Theme::Token::AntDark), 1.0);
    dark.setCosmetic(true);
    dark.setDashPattern({kAntPeriod / 2.0, kAntPeriod / 2.0});
    dark.setDashOffset(double(phase % kAntPeriod));
    painter.setPen(dark);
    painter.drawPath(path);
    painter.restore();
}

void SelectionOverlay::paint(QPainter& painter, const QTransform& docToWidget, int phase, const SelectionMask* mask) {
    if (m_dirty) {
        m_outline = mask ? selection::outline(*mask) : QPainterPath();
        m_dirty = false;
    }
    if (!mask)
        return;
    // Cosmetic pens stay one pixel wide under the view transform; the path is mapped so dashes are measured on screen.
    paintAnts(painter, docToWidget.map(m_outline), phase);
}

void SelectionTint::reset(const QRect& document) {
    m_extent = document;
    m_dirty = QRegion();
    m_all = true;
}

void SelectionTint::invalidate(const QRect& docRect) {
    m_dirty += docRect;
    if (m_dirty.rectCount() > 32)  // a long stroke while the tint is not drawn: keep the region simple
        m_dirty = m_dirty.boundingRect();
}

QRegion SelectionTint::takeDirty(const SelectionMask* mask) {
    const QRect bounds = mask && !mask->isEmpty() ? mask->bounds() : QRect();
    QRegion out = m_dirty;
    if (m_all) {
        out += m_extent.united(bounds);
        m_extent = bounds;
    }
    m_extent = m_extent.united(m_dirty.boundingRect());
    m_dirty = QRegion();
    m_all = false;
    return out;
}

QImage SelectionTint::pixels(const SelectionMask* mask, const QRect& rect) {
    return pixels(mask, rect, Theme::color(Theme::Token::SelectionTint));
}

QImage SelectionTint::pixels(const SelectionMask* mask, const QRect& rect, const QColor& tint) {
    QImage image(rect.size(), QImage::Format_RGBA8888_Premultiplied);
    image.fill(Qt::transparent);
    if (!mask)
        return image;
    std::vector<std::uint16_t> row(std::size_t(rect.width()));
    for (int y = 0; y < rect.height(); ++y) {
        mask->row(rect.top() + y, rect.left(), rect.width(), row.data());
        std::uint8_t* out = image.scanLine(y);
        for (int x = 0; x < rect.width(); ++x, out += 4) {
            const int a = tint.alpha() * row[std::size_t(x)] / SelectionMask::kOne;
            out[0] = static_cast<std::uint8_t>(tint.red() * a / 255);
            out[1] = static_cast<std::uint8_t>(tint.green() * a / 255);
            out[2] = static_cast<std::uint8_t>(tint.blue() * a / 255);
            out[3] = static_cast<std::uint8_t>(a);
        }
    }
    return image;
}

}
