#pragma once

#include <QPointF>
#include <QSizeF>
#include <QTransform>

namespace doodlebold {

// Per-document view state (each tab keeps its own). Maps document coordinates to widget coordinates:
// widget = centre + R(rotation) * F(flip) * S(zoom) * (doc - docCentre) + pan.
struct ViewState {
    double zoom = 1.0;
    double rotationDeg = 0.0;
    bool flipH = false;
    QPointF pan;               // widget pixels, applied after rotation

    QTransform docToWidget(const QSizeF& widgetSize, const QSizeF& docSize) const {
        QTransform t;
        t.translate(widgetSize.width() / 2.0 + pan.x(), widgetSize.height() / 2.0 + pan.y());
        t.rotate(rotationDeg);
        t.scale(flipH ? -zoom : zoom, zoom);
        t.translate(-docSize.width() / 2.0, -docSize.height() / 2.0);
        return t;
    }
};

}
