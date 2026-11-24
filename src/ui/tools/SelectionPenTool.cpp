#include "ui/tools/SelectionPenTool.h"

#include "core/tools/ToolBox.h"

namespace doodlebold {

SelectionPenTool::SelectionPenTool(ToolHost& host, Document* doc, SelectionBrush brush) : CanvasTool(host), m_stroke(doc), m_brush(brush) {
    m_stroke.paintInBackground([this] { QMetaObject::invokeMethod(&m_context, [this] { progress(); }, Qt::QueuedConnection); });
}

void SelectionPenTool::press(const ToolPointer& p) {
    if (m_stroke.isFinishing())
        changed(m_stroke.finish());  // the last stroke is still painting: it ends first
    ToolBox* tools = host().toolBox();
    m_strokeArea = QRect();
    m_stroke.setPressureRange(host().pressureRange());
    if (const auto started = m_stroke.begin(p.sample, m_brush, tools->settings()); !started)
        host().toolMessage(describe(started.error()));
    else
        changed(started.value());
    host().toolChanged();
}

void SelectionPenTool::move(const ToolPointer& p) {
    if (m_stroke.isActive()) {
        changed(m_stroke.add(p.sample));
        host().toolChanged();
    }
}

void SelectionPenTool::release(const ToolPointer&) {
    if (!m_stroke.isActive() || m_stroke.isFinishing())
        return;
    m_stroke.finishInBackground();
    progress();
}

// What the stroke's thread has painted comes into the selection shown; once a lifted stroke is all painted, it's finished.
void SelectionPenTool::progress() {
    if (!m_stroke.isActive())
        return;
    changed(m_stroke.takeChanges());
    if (m_stroke.isFinishing() && m_stroke.isPainted())
        changed(m_stroke.finish());
    host().toolChanged();
}

SelectionPenTool::~SelectionPenTool() {
    if (m_stroke.isFinishing())
        m_stroke.finish();  // the pen has lifted: the stroke is kept, not dropped with the tool
}

void SelectionPenTool::cancel() {
    if (m_stroke.isFinishing()) {  // the pen has lifted: what's drawn is kept
        changed(m_stroke.finish());
        host().toolChanged();
        return;
    }
    if (m_stroke.isActive())
        changed(m_strokeArea);
    m_stroke.cancel();
    host().toolChanged();
}

void SelectionPenTool::changed(const QRect& area) {
    m_changed |= area;
    m_strokeArea |= area;
}

}
