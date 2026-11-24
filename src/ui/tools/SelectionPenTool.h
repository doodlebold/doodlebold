#pragma once

#include "core/paint/SelectionStrokeController.h"
#include "ui/tools/CanvasTool.h"

#include <QObject>

#include <utility>

namespace doodlebold {

class SelectionPenTool : public CanvasTool {
public:
    SelectionPenTool(ToolHost& host, Document* doc, SelectionBrush brush);
    ~SelectionPenTool() override;

    void press(const ToolPointer& p) override;
    void move(const ToolPointer& p) override;
    void release(const ToolPointer& p) override;
    bool isActive() const override { return m_stroke.isActive(); }
    void cancel() override;
    const SelectionMask* liveSelection() const override { return m_stroke.isActive() ? &m_stroke.mask() : nullptr; }
    std::optional<QRect> takeLiveSelectionChange() override { return std::exchange(m_changed, QRect()); }
    Qt::CursorShape cursor(const QPointF&) const override { return Qt::BlankCursor; }
    // For tests: until the stroke's thread has painted everything so far, and it's in the selection shown.
    void waitUntilCaughtUp() { changed(m_stroke.catchUp()); }

private:
    void changed(const QRect& area);
    void progress();

    QObject m_context;  // what the painting thread's news is queued to; it outlives the stroke, which stops that thread
    SelectionStrokeController m_stroke;
    SelectionBrush m_brush;
    QRect m_changed;
    QRect m_strokeArea;
};

}
