#pragma once

#include <QPointF>
#include <Qt>

class QCursor;
class QPainter;

namespace doodlebold {

// Cursors the canvas draws itself for the pen. Qt's shapes are reused loosely by the gestures (SizeHor for
// zoom, SizeAll for rotate), so the drawn cursor names the action itself.
enum class CanvasGlyph {
    None,
    OpenHand,
    ClosedHand,
    Zoom,
    Rotate,
    Cross,
    Move,
    ResizeH,
    ResizeV,
    ResizeB,
    ResizeF
};

CanvasGlyph glyphForToolCursor(Qt::CursorShape shape);

void paintCanvasCursor(QPainter& painter, CanvasGlyph glyph, const QPointF& at);
QCursor canvasCursor(CanvasGlyph glyph);

}
