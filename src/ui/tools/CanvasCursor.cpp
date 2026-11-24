#include "ui/tools/CanvasCursor.h"

#include "ui/Theme.h"

#include <QCursor>
#include <QPainter>
#include <QPixmap>
#include <QPainterPath>

#include <map>
#include <numbers>

namespace doodlebold {

namespace {

void outlined(QPainter& p, const QPainterPath& path, bool fill = false) {
    QPen light(Theme::color(Theme::Token::AntLight), 3.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    QPen dark(Theme::color(Theme::Token::AntDark), 1.2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    p.setBrush(Qt::NoBrush);
    p.setPen(light);
    p.drawPath(path);
    if (fill) {
        p.setPen(Qt::NoPen);
        p.setBrush(Theme::color(Theme::Token::AntLight));
        p.drawPath(path);
    }
    p.setBrush(Qt::NoBrush);
    p.setPen(dark);
    p.drawPath(path);
}

QPainterPath hand(bool closed) {
    QPainterPath h;
    const double finger = closed ? 3.0 : 7.0;
    h.addRoundedRect(QRectF(-6, -2, 11, 9), 3, 3);
    for (int i = 0; i < 4; ++i)
        h.addRoundedRect(QRectF(-6 + i * 2.8, -2 - finger, 2.6, finger + 2), 1.3, 1.3);
    h.addRoundedRect(QRectF(4, 0, 4, 2.6), 1.3, 1.3);
    return h.simplified();
}

QPainterPath magnifier() {
    QPainterPath m;
    m.addEllipse(QPointF(-2, -2), 5.5, 5.5);
    m.moveTo(2, 2);
    m.lineTo(7, 7);
    m.moveTo(-4.5, -2);
    m.lineTo(0.5, -2);
    m.moveTo(-2, -4.5);
    m.lineTo(-2, 0.5);
    return m;
}

QPainterPath rotateArrow() {
    QPainterPath r;
    const QRectF arc(-9, -4, 18, 14);
    r.arcMoveTo(arc, 165);
    r.arcTo(arc, 165, -150);
    const QPointF right = r.currentPosition();
    r.arcMoveTo(arc, 165);
    const QPointF left = r.currentPosition();
    r.moveTo(left + QPointF(0.5, -4.0));
    r.lineTo(left);
    r.lineTo(left + QPointF(4.0, 1.0));
    r.moveTo(right + QPointF(-0.5, -4.0));
    r.lineTo(right);
    r.lineTo(right + QPointF(-4.0, 1.0));
    return r;
}

QPainterPath crosshair() {
    QPainterPath c;
    for (const QLineF& l : {QLineF(-9, 0, -3, 0), QLineF(3, 0, 9, 0), QLineF(0, -9, 0, -3), QLineF(0, 3, 0, 9)}) {
        c.moveTo(l.p1());
        c.lineTo(l.p2());
    }
    return c;
}

QPainterPath fourArrows() {
    QPainterPath a;
    a.moveTo(-9, 0);
    a.lineTo(9, 0);
    a.moveTo(0, -9);
    a.lineTo(0, 9);
    for (const auto& [tip, back1, back2] :
         {std::tuple{QPointF(-9, 0), QPointF(-6, -3), QPointF(-6, 3)}, std::tuple{QPointF(9, 0), QPointF(6, -3), QPointF(6, 3)},
          std::tuple{QPointF(0, -9), QPointF(-3, -6), QPointF(3, -6)}, std::tuple{QPointF(0, 9), QPointF(-3, 6), QPointF(3, 6)}}) {
        a.moveTo(back1);
        a.lineTo(tip);
        a.lineTo(back2);
    }
    return a;
}

QPainterPath doubleArrow() {
    QPainterPath a;
    a.moveTo(-9, 0);
    a.lineTo(9, 0);
    a.moveTo(-6, -3);
    a.lineTo(-9, 0);
    a.lineTo(-6, 3);
    a.moveTo(6, -3);
    a.lineTo(9, 0);
    a.lineTo(6, 3);
    return a;
}

}

CanvasGlyph glyphForToolCursor(Qt::CursorShape shape) {
    switch (shape) {
    case Qt::CrossCursor: return CanvasGlyph::Cross;
    case Qt::SizeAllCursor:
    case Qt::DragMoveCursor: return CanvasGlyph::Move;
    case Qt::SizeHorCursor: return CanvasGlyph::ResizeH;
    case Qt::SizeVerCursor: return CanvasGlyph::ResizeV;
    case Qt::SizeBDiagCursor: return CanvasGlyph::ResizeB;
    case Qt::SizeFDiagCursor: return CanvasGlyph::ResizeF;
    case Qt::OpenHandCursor: return CanvasGlyph::OpenHand;
    case Qt::ClosedHandCursor: return CanvasGlyph::ClosedHand;
    default: return CanvasGlyph::None;
    }
}

QCursor canvasCursor(CanvasGlyph glyph) {
    // Made once per glyph; drawn at twice the size for sharp edges on scaled screens. The hot spot is the glyph's
    // centre.
    static std::map<CanvasGlyph, QCursor> made;
    if (const auto it = made.find(glyph); it != made.end())
        return it->second;
    constexpr int kSize = 32;
    QPixmap pixmap(kSize * 2, kSize * 2);
    pixmap.setDevicePixelRatio(2.0);
    pixmap.fill(Qt::transparent);
    {
        QPainter p(&pixmap);
        paintCanvasCursor(p, glyph, QPointF(kSize / 2.0, kSize / 2.0));
    }
    return made.emplace(glyph, QCursor(pixmap, kSize / 2, kSize / 2)).first->second;
}

void paintCanvasCursor(QPainter& painter, CanvasGlyph glyph, const QPointF& at) {
    QPainterPath path;
    bool fill = false;
    double angle = 0.0;
    switch (glyph) {
    case CanvasGlyph::None: return;
    case CanvasGlyph::OpenHand: path = hand(false), fill = true; break;
    case CanvasGlyph::ClosedHand: path = hand(true), fill = true; break;
    case CanvasGlyph::Zoom: path = magnifier(); break;
    case CanvasGlyph::Rotate: path = rotateArrow(); break;
    case CanvasGlyph::Cross: path = crosshair(); break;
    case CanvasGlyph::Move: path = fourArrows(); break;
    case CanvasGlyph::ResizeH: path = doubleArrow(); break;
    case CanvasGlyph::ResizeV: path = doubleArrow(), angle = 90; break;
    case CanvasGlyph::ResizeB: path = doubleArrow(), angle = -45; break;
    case CanvasGlyph::ResizeF: path = doubleArrow(), angle = 45; break;
    }
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing);
    painter.translate(at);
    painter.rotate(angle);
    outlined(painter, path, fill);
    painter.restore();
}

}
