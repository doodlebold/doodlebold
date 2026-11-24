#pragma once

#include "ui/tools/CanvasTool.h"

namespace doodlebold {

class MoveTool : public CanvasTool {
public:
    using CanvasTool::CanvasTool;
    void press(const ToolPointer& p) override;
    void move(const ToolPointer& p) override;
    void release(const ToolPointer& p) override;
    bool keyPress(QKeyEvent* e) override;
    bool isActive() const override { return m_dragging; }
    void cancel() override;
    Qt::CursorShape cursor(const QPointF&) const override { return Qt::SizeAllCursor; }

private:
    bool m_dragging = false;
};

// Its handles take the pointer whatever the active tool is.
class TransformTool : public CanvasTool {
public:
    using CanvasTool::CanvasTool;
    void press(const ToolPointer& p) override;
    void move(const ToolPointer& p) override;
    void release(const ToolPointer& p) override;
    bool keyPress(QKeyEvent* e) override;
    bool isActive() const override { return m_dragging; }
    void cancel() override;
    void paintOverlay(QPainter& painter, const QTransform& docToWidget) override;
    Qt::CursorShape cursor(const QPointF& doc) const override;

private:
    bool m_dragging = false;
};

}
