#pragma once

#include "core/document/Document.h"
#include "core/stroke/StrokePainter.h"

#include <functional>
#include <memory>
#include <optional>

namespace doodlebold {

enum class SelectionBrush {
    Pen,
    Eraser
};

// Selection Pen / Eraser: the selection is lent to the brush engine as a picture (coverage becomes white alpha), so the pens draw
// and erase it with the engine's plain round tip, with no stabilizer; the document is untouched until the stroke
// ends, then one undo step. Blocks other edits and undo while it runs; not limited by the selection or layer locks. With
// paintInBackground() the engine paints on a thread of its own: takeChanges() brings what it has painted into the mask, and a stroke
// whose pen has lifted finishes once it's painted.
class SelectionStrokeController {
public:
    explicit SelectionStrokeController(Document* doc) : m_doc(doc) {}
    ~SelectionStrokeController() { cancel(); }
    SelectionStrokeController(const SelectionStrokeController&) = delete;
    SelectionStrokeController& operator=(const SelectionStrokeController&) = delete;

    // `onFrame` is called on the painting thread whenever there's something new for takeChanges().
    void paintInBackground(std::function<void()> onFrame);
    // Options' pressure range, for the next stroke, as painting strokes have it.
    void setPressureRange(PressureRange range) { m_painter.setPressureRange(range); }
    bool isActive() const { return m_active; }
    EditResult<QRect> begin(const InputSample& first, SelectionBrush brush, const BrushSettings& settings);
    QRect add(const InputSample& sample);
    QRect takeChanges();
    // The pen has lifted: in the background the stroke goes on painting, and finish() is for once isPainted() says so.
    void finishInBackground();
    bool isFinishing() const { return m_finishing; }
    bool isPainted() const { return !m_inBackground || m_painter.isPainted(); }
    // Until the stroke's thread has painted everything posted so far, taking it all into the mask: where it changed.
    QRect catchUp();
    QRect finish();
    void cancel();

    const SelectionMask& mask() const { return m_mask; }

private:
    QRect sync(const QRect& dirty);
    QRect takeFrame();
    void end(bool keep);

    Document* m_doc;
    StrokePainter m_painter;
    bool m_inBackground = false;
    QRect m_takenWhileCatchingUp;  // what frames taken while the painter caught up changed, not reported yet
    bool m_finishing = false;
    TiledImage m_proxy;
    SelectionMask m_mask;
    SelectionBrush m_brush = SelectionBrush::Pen;
    bool m_active = false;
};

}
