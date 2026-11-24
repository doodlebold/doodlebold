#include "ui/tools/QuickShapeTool.h"

#include "ui/Theme.h"

#include <QKeyEvent>
#include <QPainter>
#include <QPen>

#include <cmath>
#include <numbers>

namespace doodlebold {

QuickShapeTool::QuickShapeTool(ToolHost& host, const QuickShape& shape, std::function<void()> changed)
    : CanvasTool(host), m_edit(shape), m_changed(std::move(changed)) {}

bool QuickShapeTool::takes(const QPointF& doc) const {
    return m_edit.hitTest(doc, host().docPerPixel()).grip != QuickShapeEdit::Grip::None;
}

void QuickShapeTool::press(const ToolPointer& p) {
    m_edit.beginDrag(m_edit.hitTest(p.doc, host().docPerPixel()), p.doc);
}

void QuickShapeTool::move(const ToolPointer& p) {
    if (!m_edit.isDragging())
        return;
    m_edit.dragTo(p.doc, p.modifiers & Qt::ShiftModifier);
    m_changed();
}

void QuickShapeTool::release(const ToolPointer&) {
    m_edit.endDrag();
}

bool QuickShapeTool::wantsKey(const QKeyEvent* e) const {
    return e->key() == Qt::Key_Escape || e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter;
}

Qt::CursorShape QuickShapeTool::cursor(const QPointF& doc) const {
    if (!m_adjusting)
        return Qt::BlankCursor;
    using Grip = QuickShapeEdit::Grip;
    const QuickShapeEdit::Hit hit = m_edit.hitTest(doc, host().docPerPixel());
    switch (hit.grip) {
    case Grip::None: return Qt::BlankCursor;  // the brush outline: a press here starts the next stroke
    case Grip::Inside:
    case Grip::ShapeCorner: return Qt::SizeAllCursor;
    case Grip::Turn: return Qt::CrossCursor;
    case Grip::BoxCorner:
    case Grip::BoxEdge: break;
    }
    const QPointF away = m_edit.boxHandle(hit.grip, hit.index) - m_edit.shape().centre;
    const double degrees = std::atan2(away.y(), away.x()) * 180.0 / std::numbers::pi;
    switch (int(std::floor(std::fmod(degrees + 382.5, 180.0) / 45.0))) {
    case 0: return Qt::SizeHorCursor;
    case 1: return Qt::SizeFDiagCursor;
    case 2: return Qt::SizeVerCursor;
    default: return Qt::SizeBDiagCursor;
    }
}

void QuickShapeTool::paintOverlay(QPainter& painter, const QTransform& docToWidget) {
    if (!m_adjusting)
        return;
    using Grip = QuickShapeEdit::Grip;
    using Token = Theme::Token;
    const QuickShape& shape = m_edit.shape();
    QPolygonF box;
    for (int i = 0; i < 4; ++i)
        box << docToWidget.map(m_edit.boxHandle(Grip::BoxCorner, i));
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(Theme::color(Token::AntDark), 3.0));
    painter.drawPolygon(box);
    painter.setPen(QPen(Theme::color(Token::Accent), 1.0));
    painter.drawPolygon(box);
    constexpr double kHalf = 4.0;
    painter.setPen(QPen(Theme::color(Token::AntDark), 1.0));
    painter.setBrush(Theme::color(Token::AccentText));
    for (const Grip grip : {Grip::BoxCorner, Grip::BoxEdge})
        for (int i = 0; i < 4; ++i) {
            const QPointF at = docToWidget.map(m_edit.boxHandle(grip, i));
            painter.drawRect(QRectF(at.x() - kHalf, at.y() - kHalf, 2 * kHalf, 2 * kHalf));
        }
    if (shape.hasOwnCorners()) {
        constexpr double kDot = 5.5;
        painter.setPen(QPen(Theme::color(Token::Accent), 2.0));
        for (const QPointF& corner : shape.corners)
            painter.drawEllipse(docToWidget.map(shape.toDocument(corner)), kDot, kDot);
    }
    painter.restore();
}

}
