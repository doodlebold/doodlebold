#pragma once

#include "core/paint/QuickShape.h"
#include "ui/tools/CanvasTool.h"

#include <functional>

namespace doodlebold {

// The handles of a snapped stroke. It takes over from the snap on; the handles show and answer once the pen is lifted.
class QuickShapeTool : public CanvasTool {
public:
    QuickShapeTool(ToolHost& host, const QuickShape& shape, std::function<void()> changed);

    QuickShapeEdit& edit() { return m_edit; }
    const QuickShapeEdit& edit() const { return m_edit; }
    bool adjusting() const { return m_adjusting; }
    void setAdjusting(bool on) { m_adjusting = on; }
    // Whether a press here is for the handles, not the next stroke.
    bool takes(const QPointF& doc) const;

    void press(const ToolPointer& p) override;
    void move(const ToolPointer& p) override;
    void release(const ToolPointer& p) override;
    bool wantsKey(const QKeyEvent* e) const override;
    bool isActive() const override { return m_edit.isDragging(); }
    void paintOverlay(QPainter& painter, const QTransform& docToWidget) override;
    Qt::CursorShape cursor(const QPointF& doc) const override;

private:
    QuickShapeEdit m_edit;
    std::function<void()> m_changed;
    bool m_adjusting = false;
};

}
