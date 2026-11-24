#include "ui/color/ColorWheelView.h"

#include "core/color/ColorModels.h"
#include "core/color/ColorState.h"

#include <QMouseEvent>
#include <QPainter>

#include <algorithm>
#include <cmath>
#include <tuple>

namespace doodlebold {

namespace {

constexpr double kRingWidth = 0.24;
constexpr double kDiscGap = 0.06;

// The square (-1 … 1 each way) into the disc and back, by the elliptical grid mapping: smooth everywhere, the square's edges
// become the rim.
QPointF squareToDisc(double x, double y) {
    return {x * std::sqrt(1.0 - y * y / 2.0), y * std::sqrt(1.0 - x * x / 2.0)};
}
double squareCoordinate(double along, double across) {
    const double common = 2.0 + along * along - across * across, twice = 2.0 * std::numbers::sqrt2 * along;
    return 0.5 * std::sqrt(std::max(0.0, common + twice)) - 0.5 * std::sqrt(std::max(0.0, common - twice));
}
QPointF discToSquare(double u, double v) {
    if (const double r = std::hypot(u, v); r > 1.0)  // outside the disc: the nearest point of the rim
        u /= r, v /= r;
    return {std::clamp(squareCoordinate(u, v), -1.0, 1.0), std::clamp(squareCoordinate(v, u), -1.0, 1.0)};
}

double hueAt(double angle) {
    return std::fmod(std::fmod(150.0 - angle, 360.0) + 360.0, 360.0);
}
double angleOf(double hue) {
    return 150.0 - hue;
}

}

ColorWheelView::ColorWheelView(ColorState* state, QWidget* parent) : QWidget(parent), m_state(state) {
    setObjectName(QStringLiteral("colorWheel"));
    QSizePolicy policy(QSizePolicy::Preferred, QSizePolicy::Preferred);
    policy.setHeightForWidth(true);
    setSizePolicy(policy);
    connect(state, &ColorState::changed, this, &ColorWheelView::syncFromState);
    syncFromState();
}

QPointF ColorWheelView::centre() const {
    return QRectF(rect()).center();
}

double ColorWheelView::outerRadius() const {
    return std::min(width(), height()) / 2.0 - 8.0;
}

double ColorWheelView::innerRadius() const {
    return outerRadius() * (1.0 - kRingWidth);
}

double ColorWheelView::discRadius() const {
    return innerRadius() - outerRadius() * kDiscGap;
}

QPointF ColorWheelView::discPoint(double chroma, double value) const {
    const QPointF d = squareToDisc(2.0 * chroma - 1.0, 1.0 - 2.0 * value);
    return centre() + d * discRadius();
}

std::pair<double, double> ColorWheelView::chromaValueAt(const QPointF& pos) const {
    const QPointF d = (pos - centre()) / discRadius();
    const QPointF sq = discToSquare(d.x(), d.y());
    return {(sq.x() + 1.0) / 2.0, (1.0 - sq.y()) / 2.0};
}

QColor ColorWheelView::discColor(double hue, double chroma, double value) {
    return color::fromHsv(hue, value > 0.0 ? std::min(1.0, chroma / value) : 0.0, value);
}

QPointF ColorWheelView::ringPoint(double hue) const {
    const double a = angleOf(hue) * std::numbers::pi / 180.0;
    const double r = (outerRadius() + innerRadius()) / 2.0;
    return centre() + QPointF(r * std::cos(a), -r * std::sin(a));
}

void ColorWheelView::syncFromState() {
    if (!m_state)
        return;
    const QColor c = m_state->color();
    if (c != m_shown) {
        m_v = color::value(c);
        m_chroma = color::hsvSaturation(c) * m_v;
        m_shown = c;
    }
    update();
}

void ColorWheelView::resizeEvent(QResizeEvent* e) {
    m_ring = QImage();
    QWidget::resizeEvent(e);
}

QImage ColorWheelView::discImage() const {
    // At the screen's resolution: drawn at 1x it would be scaled up, blocky at 125 % (COSMIC's default here).
    const qreal dpr = devicePixelRatioF();
    const double radius = discRadius() * dpr;
    const int side = int(std::ceil(2.0 * radius)) + 2;
    QImage img(side, side, QImage::Format_ARGB32_Premultiplied);
    img.setDevicePixelRatio(dpr);
    img.fill(Qt::transparent);
    const double hue = m_state ? m_state->hue() : 0.0;
    const double mid = side / 2.0;
    for (int y = 0; y < side; ++y) {
        auto* line = reinterpret_cast<QRgb*>(img.scanLine(y));
        for (int x = 0; x < side; ++x) {
            const double dx = x + 0.5 - mid, dy = y + 0.5 - mid;
            const double cover = std::clamp(radius - std::hypot(dx, dy) + 0.5, 0.0, 1.0);
            if (cover <= 0.0)
                continue;
            const QPointF sq = discToSquare(dx / radius, dy / radius);
            const QColor c = discColor(hue, (sq.x() + 1.0) / 2.0, (1.0 - sq.y()) / 2.0);
            const int a = int(std::lround(cover * 255));
            line[x] = qRgba(c.red() * a / 255, c.green() * a / 255, c.blue() * a / 255, a);
        }
    }
    return img;
}

void ColorWheelView::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    const QPointF c = centre();
    const double outer = outerRadius(), inner = innerRadius();
    const qreal dpr = devicePixelRatioF();
    if (m_ring.size() != size() * dpr || m_ringFor != std::pair(outer, inner)) {
        m_ringFor = {outer, inner};
        m_ring = QImage(size() * dpr, QImage::Format_ARGB32_Premultiplied);
        m_ring.setDevicePixelRatio(dpr);
        m_ring.fill(Qt::transparent);
        const QPointF cd = c * dpr;
        const double outerD = outer * dpr, innerD = inner * dpr;
        for (int y = 0; y < m_ring.height(); ++y) {
            auto* line = reinterpret_cast<QRgb*>(m_ring.scanLine(y));
            for (int x = 0; x < m_ring.width(); ++x) {
                const double dx = x + 0.5 - cd.x(), dy = y + 0.5 - cd.y();
                const double r = std::hypot(dx, dy);
                const double cover = std::clamp(r - innerD + 0.5, 0.0, 1.0) * std::clamp(outerD - r + 0.5, 0.0, 1.0);
                if (cover <= 0.0)
                    continue;
                const double angle = std::atan2(-dy, dx) * 180.0 / std::numbers::pi;
                const QColor hue = color::fromHsv(hueAt(angle), 1.0, 1.0);
                const int a = int(std::lround(cover * 255));
                line[x] = qRgba(hue.red() * a / 255, hue.green() * a / 255, hue.blue() * a / 255, a);
            }
        }
    }
    p.drawImage(QPointF(0, 0), m_ring);

    const QImage disc = discImage();
    const QSizeF discSize = QSizeF(disc.size()) / dpr;
    p.drawImage(QRectF(c - QPointF(discSize.width(), discSize.height()) / 2.0, discSize), disc);

    const double hue = m_state ? m_state->hue() : 0.0;
    // The knobs sit on the user's colours, so they are black and white in every theme (as the canvas's marching ants are).
    const auto knob = [&p](const QPointF& at, double radius, const QColor& fill) {
        p.setPen(QPen(QColor(0, 0, 0, 120), 1.0));
        p.setBrush(Qt::NoBrush);
        p.drawEllipse(at, radius + 1.5, radius + 1.5);
        p.setPen(QPen(Qt::white, 2.5));
        p.setBrush(fill);
        p.drawEllipse(at, radius, radius);
    };
    knob(ringPoint(hue), (outer - inner) / 2.0 + 1.0, color::fromHsv(hue, 1.0, 1.0));
    knob(discPoint(m_chroma, m_v), 6.0, discColor(hue, m_chroma, m_v));
}

void ColorWheelView::mousePressEvent(QMouseEvent* e) {
    if (e->button() != Qt::LeftButton) {
        QWidget::mousePressEvent(e);
        return;
    }
    const QPointF pos = e->position();
    const QPointF d = pos - centre();
    const double r = std::hypot(d.x(), d.y());
    if (r <= discRadius() + 1.0)
        m_drag = Drag::Disc;
    else if (r >= innerRadius() - 2 && r <= outerRadius() + 2)
        m_drag = Drag::Ring;
    else
        return;
    pick(pos);
}

void ColorWheelView::mouseMoveEvent(QMouseEvent* e) {
    if (m_drag != Drag::None)
        pick(e->position());
}

void ColorWheelView::mouseReleaseEvent(QMouseEvent* e) {
    if (m_drag != Drag::None && e->button() == Qt::LeftButton)
        m_drag = Drag::None;
}

void ColorWheelView::pick(const QPointF& pos) {
    if (!m_state)
        return;
    double hue = m_state->hue();
    if (m_drag == Drag::Ring) {
        const QPointF d = pos - centre();
        hue = hueAt(std::atan2(-d.y(), d.x()) * 180.0 / std::numbers::pi);
    } else {
        std::tie(m_chroma, m_v) = chromaValueAt(pos);
    }
    m_shown = discColor(hue, m_chroma, m_v);
    m_state->setColor(m_shown, hue);
    update();
}

}
