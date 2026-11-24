#pragma once

#include "core/view/ViewState.h"

#include <QPolygonF>
#include <QSizeF>
#include <QTransform>

namespace doodlebold {

class ViewNavigator {
public:
    static constexpr double kMinZoom = 1.0 / 128.0;  // 0.78 %
    static constexpr double kFitMargin = 10.0;       // px in all left by Fit View on the limiting side
    static constexpr double kMaxZoom = 32.0;         // 3200 %

    // Zoom buttons and Ctrl+= / Ctrl+- go to the next level of the reference's ladder strictly above or below, so a zoom
    // between levels snaps. A wheel notch goes to the next level of 2^(k/4) that way.
    static double nextLadderZoom(double zoom, int direction);
    static double nextWheelZoom(double zoom, int direction);

    ViewNavigator(ViewState& view, const QSizeF& widgetSize, const QSizeF& docSize) : m_v(view), m_widget(widgetSize), m_doc(docSize) {}

    QTransform docToWidget() const { return m_v.docToWidget(m_widget, m_doc); }
    QTransform widgetToDoc() const { return docToWidget().inverted(); }
    QPointF centre() const { return QPointF(m_widget.width() / 2.0, m_widget.height() / 2.0); }

    void zoomTo(double zoom, const QPointF& anchor);
    void zoomBy(double factor, const QPointF& anchor) { zoomTo(m_v.zoom * factor, anchor); }
    void rotateTo(double degrees, const QPointF& anchor);  // normalised to (−180, 180]
    void flip(const QPointF& anchor);
    void resetOrientation() {
        m_v.rotationDeg = 0.0;
        m_v.flipH = false;
    }
    // Reset View (Home): angle 0, flip off, the canvas centred at 100 % (or fitted to the window, as the options choose).
    void home(bool fitToWindow);
    void fit();
    void scrollBy(const QPointF& widgetDelta) { m_v.pan += widgetDelta; }
    void centreOn(const QPointF& docPoint);
    QPolygonF visibleDocumentArea() const;

private:
    template <typename F> void keepAnchor(const QPointF& anchor, F&& change);

    ViewState& m_v;
    QSizeF m_widget;
    QSizeF m_doc;
};

}
