#include "core/view/ViewNavigator.h"

#include <algorithm>
#include <cmath>
#include <ranges>

namespace doodlebold {

namespace {

double normaliseAngle(double deg) {
    deg = std::fmod(deg, 360.0);
    if (deg <= -180.0)
        deg += 360.0;
    else if (deg > 180.0)
        deg -= 360.0;
    return deg;
}

// The zoom buttons' levels, in ascending order: the reference's, as its zoom readout shows them stepping through (observed
// 2026-11-19).
constexpr double kLadder[] = {1.0 / 128, 1.0 / 96, 1.0 / 64, 1.0 / 48, 1.0 / 32, 1.0 / 24, 1.0 / 16, 1.0 / 12, 1.0 / 8, 1.0 / 6,
                              1.0 / 4,   1.0 / 3,  1.0 / 2,  0.75,     1.0,      1.5,      2.0,      3.0,      4.0,     5.0,
                              6.0,       8.0,      10.0,     12.0,     16.0,     20.0,     26.0,     32.0};
// A wheel notch goes to the next level of 2^(k/4), a quarter of the way to double in steps of size, as the reference's: 100, 119, 141,
// 168 and 200 %, four a doubling (observed 2026-11-19).
constexpr int kWheelStepsPerDoubling = 4;
constexpr double kSnapTolerance = 1e-6;  // relative: a zoom this close to a level counts as on it

}

double ViewNavigator::nextLadderZoom(double zoom, int direction) {
    if (direction > 0) {
        for (double level : kLadder)
            if (level > zoom * (1.0 + kSnapTolerance))
                return level;
        return kMaxZoom;
    }
    for (double it : std::views::reverse(kLadder))
        if (it < zoom * (1.0 - kSnapTolerance))
            return it;
    return kMinZoom;
}

double ViewNavigator::nextWheelZoom(double zoom, int direction) {
    // The zoom's place on the levels, and the next level strictly beyond it that way: a zoom between levels snaps onto one.
    const double at = std::log2(std::max(zoom, 1e-12)) * kWheelStepsPerDoubling;
    const double next = direction > 0 ? std::floor(at + kSnapTolerance * kWheelStepsPerDoubling) + 1.0
                                      : std::ceil(at - kSnapTolerance * kWheelStepsPerDoubling) - 1.0;
    return std::clamp(std::exp2(next / kWheelStepsPerDoubling), kMinZoom, kMaxZoom);
}

template <typename F> void ViewNavigator::keepAnchor(const QPointF& anchor, F&& change) {
    const QPointF docAnchor = widgetToDoc().map(anchor);
    change();
    m_v.zoom = std::clamp(m_v.zoom, kMinZoom, kMaxZoom);
    m_v.rotationDeg = normaliseAngle(m_v.rotationDeg);
    m_v.pan += anchor - docToWidget().map(docAnchor);
}

void ViewNavigator::zoomTo(double zoom, const QPointF& anchor) {
    keepAnchor(anchor, [&] { m_v.zoom = zoom; });
}

void ViewNavigator::rotateTo(double degrees, const QPointF& anchor) {
    keepAnchor(anchor, [&] { m_v.rotationDeg = degrees; });
}

void ViewNavigator::flip(const QPointF& anchor) {
    // ViewState applies the flip before the rotation; negating the angle as well turns that into a mirror about the screen's
    // vertical axis, which is what the user sees as "flip".
    keepAnchor(anchor, [&] {
        m_v.flipH = !m_v.flipH;
        m_v.rotationDeg = -m_v.rotationDeg;
    });
}

void ViewNavigator::fit() {
    QTransform r;
    r.rotate(m_v.rotationDeg);
    const QRectF box = r.mapRect(QRectF(QPointF(0, 0), m_doc));
    if (box.isEmpty() || m_widget.isEmpty())
        return;
    // 10 px in all are left on the side that limits the zoom: a 400 × 300 canvas in a 1160 × 850 view fits at exactly 280 %
    // ((850 − 10) / 300).
    const double w = std::max(1.0, m_widget.width() - kFitMargin), h = std::max(1.0, m_widget.height() - kFitMargin);
    m_v.zoom = std::clamp(std::min(w / box.width(), h / box.height()), kMinZoom, kMaxZoom);
    m_v.pan = QPointF();
}

void ViewNavigator::home(bool fitToWindow) {
    resetOrientation();
    if (fitToWindow) {
        fit();
    } else {
        m_v.zoom = 1.0;
        m_v.pan = QPointF();
    }
}

void ViewNavigator::centreOn(const QPointF& docPoint) {
    m_v.pan += centre() - docToWidget().map(docPoint);
}

QPolygonF ViewNavigator::visibleDocumentArea() const {
    return widgetToDoc().map(QPolygonF(QRectF(QPointF(0, 0), m_widget)));
}

}
