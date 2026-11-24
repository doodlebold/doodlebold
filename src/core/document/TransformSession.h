#pragma once

#include "core/document/Document.h"
#include "core/document/FloatingPreview.h"
#include "core/document/Projection.h"
#include "core/tools/FixedTool.h"
#include "core/transform/Lift.h"
#include "core/transform/Resample.h"

#include <QImage>
#include <QPointF>
#include <QRect>
#include <Qt>

#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

namespace doodlebold {

// One interactive Transform Layer (Ctrl+T) or Move: the floating, not yet applied state between the first handle drag and OK /
// Cancel. The document is untouched until commit(), which applies the result as ONE undo step through Document::transformLayer
// / moveLayerExactly. The session keeps its own copy of the layer tree with the transformed pixels in place of the layer and
// composites that for the display, so the preview shows blend modes, opacity and the layers above exactly as the result will.
// The geometry is four corner points (Quad). What dragging a handle does depends on the mode (the handle rules):
//   Freeform  a corner moves alone; an edge handle stretches the box perpendicular to that edge
//   Resize    scales in the box's own frame about the opposite corner/edge (Shift on a corner keeps the ratio)
//   Deform    a corner moves alone; an edge handle moves its two corners together (skews the box)
//   Rotate    dragging anywhere but inside rotates about the centre (Shift snaps to 15°)
// Dragging inside moves everything, in every mode. Move sessions have only that: an exact whole-pixel move.
class TransformSession {
public:
    enum class Kind {
        Transform,
        Move
    };
    enum class Handle {
        None,
        Inside,
        Outside,
        Corner0,
        Corner1,
        Corner2,
        Corner3,
        Edge0,
        Edge1,
        Edge2,
        Edge3
    };

    // Starts on layer id of doc, or says why it cannot be transformed: a folder (NotARasterLayer), a locked layer
    // (LayerLocked), nothing to transform (NoChange). The session must not outlive doc and is only valid while the document
    // does not change under it (the owner cancels it then).
    static EditResult<std::shared_ptr<TransformSession>> beginTransform(Document& doc, LayerId id, TransformMode mode,
                                                                        const MarqueeOptions& options);
    static EditResult<std::shared_ptr<TransformSession>> beginMove(Document& doc, LayerId id, const MarqueeOptions& options);
    // Several whole layers, or what is inside a folder, moved together. The preview is the CPU's: one floating layer cannot show it.
    static EditResult<std::shared_ptr<TransformSession>> beginMoveLayers(Document& doc, const std::vector<LayerId>& selected,
                                                                         const MarqueeOptions& options);

    Kind kind() const { return m_kind; }
    LayerId layer() const { return m_layer; }
    TransformMode mode() const { return m_mode; }
    void setMode(TransformMode mode) { m_mode = mode; }
    QRect sourceBounds() const { return m_box; }
    const transform::Quad& quad() const { return m_quad; }
    QPoint offset() const { return m_offset; }  // Move sessions
    double perspective() const { return m_perspective; }
    void setPerspective(double p);
    const MarqueeOptions& options() const { return m_options; }
    void setOptions(const MarqueeOptions& options);
    bool isIdentity() const;
    // Back to the untransformed box (the perspective setting stays).
    void reset();

    // docPerPixel is the size of a screen pixel in document units: handles are a few screen pixels wide.
    static constexpr double kHandleRadius = 7.0;
    Handle hitTest(const QPointF& doc, double docPerPixel) const;
    QPointF handlePosition(Handle h) const;

    // A drag starts at start on handle h and follows the pointer. Modifiers: Shift (keep ratio, snap angle).
    void beginDrag(Handle h, const QPointF& start);
    void dragTo(const QPointF& pos, Qt::KeyboardModifiers mods);
    void endDrag();
    bool isDragging() const { return m_dragging; }

    // Valid for the current shape, at the interactive quality (min of Preview and, while dragging, Dragging). takeDirty() is
    // the document area that differs from the last call (to upload).
    const QImage& image();
    QRect takeDirty();
    const std::optional<SelectionMask>& selection() const { return m_selection; }
    QRect coveredArea() const;

    // GPU drag preview (opt-in; the owner of a GL canvas turns it on). While a handle is dragged the CPU does not resample:
    // dragTo() only moves the quad and the owner draws the lifted pixels itself through floatingQuad() over floatingPreview().
    // The CPU preview is brought up to date, at Preview quality, when the drag pauses (settlePreview()) or ends.
    void setGpuPreview(bool on);
    bool gpuPreview() const { return m_gpuEnabled; }
    // True from the first drag step that changed the shape until the CPU preview catches up: image() is stale then.
    bool gpuPreviewActive() const { return m_gpuEnabled && m_gpuStale; }
    // Built on first use (compositing the document once), then kept for the session.
    const FloatingPreview& floatingPreview();
    bool floatingPreviewReady() const { return m_floating.has_value(); }
    transform::Quad floatingQuad() const;
    // Bumped by every drag step that only moved the GPU preview; and how many times the CPU resampled the layer for the
    std::uint64_t gpuPreviewSteps() const { return m_gpuSteps; }
    std::uint64_t cpuPreviewCount() const { return m_cpuPreviews; }
    void settlePreview();

    // Applies the result with the Final quality as one undo step; the session is spent afterwards. NoChange when nothing moved.
    EditResult<> commit();

private:
    TransformSession(Document& doc, LayerId id, Kind kind, TransformMode mode, const MarqueeOptions& options);
    void begin();
    void changed();
    void refreshCpu(bool dragQuality);
    QPointF centre() const;

    Document& m_doc;
    LayerId m_layer;
    Kind m_kind;
    TransformMode m_mode;
    MarqueeOptions m_options;
    double m_perspective = 0.0;
    QRect m_box;
    transform::Quad m_quad;
    QPoint m_offset;

    struct MovedLayer {
        LayerId id;
        TiledImage pixels;
        std::optional<SelectionMask> mask;  // linked only: an unlinked mask stays
        QRect box;
    };
    std::vector<LayerId> m_selected;
    std::vector<MovedLayer> m_moved;  // beginMoveLayers: these instead of one floating part

    // The floating part and what stays behind (without a selection `rest` is empty).
    TiledImage m_part;
    TiledImage m_rest;
    TiledImage m_original;
    std::optional<SelectionMask> m_originalSelection;
    std::optional<SelectionMask> m_originalMask;  // the layer's mask before the transform (it moves with the layer)
    std::optional<SelectionMask> m_selection;

    LayerStack m_layers;
    Projection m_projection;
    QRect m_dirty;
    QRect m_lastArea;

    bool m_gpuEnabled = false;
    bool m_gpuStale = false;
    std::uint64_t m_gpuSteps = 0;
    std::uint64_t m_cpuPreviews = 0;
    std::optional<FloatingPreview> m_floating;

    bool m_dragging = false;
    Handle m_handle = Handle::None;
    QPointF m_dragStart;
    transform::Quad m_dragQuad;
    QPoint m_dragOffset;
};

}
