#pragma once

#include "ui/tools/CanvasTool.h"

#include <QPoint>
#include <QPolygonF>

namespace doodlebold {

// A press inside the selection moves the selected pixels instead (with the option on, or Ctrl+Shift at any time).
class MarqueeTool : public CanvasTool {
public:
    using CanvasTool::CanvasTool;
    void press(const ToolPointer& p) override;
    void move(const ToolPointer& p) override;
    void release(const ToolPointer& p) override;
    bool keyPress(QKeyEvent* e) override;
    bool isActive() const override { return m_state != State::Idle; }
    void cancel() override;
    void paintOverlay(QPainter& painter, const QTransform& docToWidget) override;
    Qt::CursorShape cursor(const QPointF& doc) const override;

    QRect dragRect() const;

private:
    enum class State {
        Idle,
        Selecting,
        Moving
    };
    State m_state = State::Idle;
    QPoint m_anchor, m_current;
    bool m_dragged = false;
    Qt::KeyboardModifiers m_mods;
};

class LassoTool : public CanvasTool {
public:
    using CanvasTool::CanvasTool;
    void press(const ToolPointer& p) override;
    void move(const ToolPointer& p) override;
    void release(const ToolPointer& p) override;
    void doubleClick(const ToolPointer& p) override;
    bool keyPress(QKeyEvent* e) override;
    bool isActive() const override { return m_started; }
    void cancel() override;
    void paintOverlay(QPainter& painter, const QTransform& docToWidget) override;

    const QPolygonF& path() const { return m_path; }

private:
    // Mouse positions snap to the pixel centre (the lasso vertices); pen positions keep
    // their sub-pixel precision.
    void addPoint(const QPointF& docPos, PointerKind kind);
    void close();

    QPolygonF m_path;
    bool m_freehand = false;
    PointerKind m_kind = PointerKind::Mouse;
    QPointF m_cursor;
    bool m_started = false;
    bool m_down = false;
    bool m_dragged = false;
    Qt::KeyboardModifiers m_mods;
};

class WandTool : public CanvasTool {
public:
    using CanvasTool::CanvasTool;
    void press(const ToolPointer& p) override;
    void move(const ToolPointer&) override {}
    void release(const ToolPointer&) override {}
    bool isActive() const override { return false; }
};

}
