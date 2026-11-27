#include "ui/tools/TransformTools.h"

#include "core/document/TransformSession.h"
#include "ui/Theme.h"

#include <QKeyEvent>
#include <QPainter>
#include <QPen>

namespace doodlebold {

void MoveTool::press(const ToolPointer& p) {
    if (!host().startMoveSession())
        return;
    host().session()->beginDrag(TransformSession::Handle::Inside, p.doc);
    m_dragging = true;
}

// The session can go under a drag: copy, cut, paste and the layer turns commit it before they run. The drag then just ends.
void MoveTool::move(const ToolPointer& p) {
    TransformSession* s = host().session();
    if (!m_dragging || !s)
        return;
    s->dragTo(p.doc, p.modifiers);
    host().toolChanged();
}

void MoveTool::release(const ToolPointer&) {
    if (!m_dragging)
        return;
    m_dragging = false;
    if (TransformSession* s = host().session()) {
        s->endDrag();
        host().finishSession(true);
    }
}

bool MoveTool::keyPress(QKeyEvent* e) {
    if (e->key() == Qt::Key_Escape && m_dragging) {
        cancel();
        return true;
    }
    return false;
}

void MoveTool::cancel() {
    if (!m_dragging)
        return;
    m_dragging = false;
    if (TransformSession* s = host().session())
        s->endDrag();
    host().finishSession(false);
}

void TransformTool::press(const ToolPointer& p) {
    TransformSession* s = host().session();
    if (!s)
        return;
    s->beginDrag(s->hitTest(p.doc, host().docPerPixel()), p.doc);
    m_dragging = true;
}

void TransformTool::move(const ToolPointer& p) {
    TransformSession* s = host().session();
    if (!s || !m_dragging)
        return;
    s->dragTo(p.doc, p.modifiers);
    host().toolChanged();
}

void TransformTool::release(const ToolPointer&) {
    TransformSession* s = host().session();
    if (!m_dragging)
        return;
    m_dragging = false;
    if (s) {
        s->endDrag();
        host().toolChanged();
    }
}

bool TransformTool::keyPress(QKeyEvent* e) {
    if (m_dragging && e->key() != Qt::Key_Escape)
        return false;
    switch (e->key()) {
    case Qt::Key_Return:
    case Qt::Key_Enter: host().finishSession(true); return true;
    case Qt::Key_Escape:
        cancel();
        host().finishSession(false);
        return true;
    default: return false;
    }
}

void TransformTool::cancel() {
    if (TransformSession* s = host().session(); s && m_dragging)
        s->endDrag();
    m_dragging = false;
}

Qt::CursorShape TransformTool::cursor(const QPointF& doc) const {
    const TransformSession* s = host().session();
    if (!s)
        return Qt::ArrowCursor;
    using H = TransformSession::Handle;
    const H h = s->hitTest(doc, host().docPerPixel());
    if (h == H::Inside)
        return Qt::SizeAllCursor;
    if (s->mode() == TransformMode::Rotate)
        return Qt::CrossCursor;
    switch (h) {
    case H::Corner0:
    case H::Corner2: return Qt::SizeFDiagCursor;
    case H::Corner1:
    case H::Corner3: return Qt::SizeBDiagCursor;
    case H::Edge0:
    case H::Edge2: return Qt::SizeVerCursor;
    case H::Edge1:
    case H::Edge3: return Qt::SizeHorCursor;
    default: return Qt::ArrowCursor;
    }
}

void TransformTool::paintOverlay(QPainter& painter, const QTransform& docToWidget) {
    const TransformSession* s = host().session();
    if (!s)
        return;
    using H = TransformSession::Handle;
    QPolygonF box;
    for (const QPointF& c : s->quad().corner)
        box << docToWidget.map(c);
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(Theme::color(Theme::Token::AntDark), 3.0));
    painter.drawPolygon(box);
    painter.setPen(QPen(Theme::color(Theme::Token::Accent), 1.0));
    painter.drawPolygon(box);
    if (s->kind() == TransformSession::Kind::Transform) {
        constexpr double kHalf = 4.0;
        painter.setPen(QPen(Theme::color(Theme::Token::AntDark), 1.0));
        painter.setBrush(Theme::color(Theme::Token::AccentText));
        for (int i = 0; i < 8; ++i) {
            const QPointF at = docToWidget.map(s->handlePosition(H(int(H::Corner0) + i)));
            painter.drawRect(QRectF(at.x() - kHalf, at.y() - kHalf, 2 * kHalf, 2 * kHalf));
        }
    }
    painter.restore();
}

}
