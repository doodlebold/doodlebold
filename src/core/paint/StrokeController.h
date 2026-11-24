#pragma once

#include "core/document/Document.h"
#include "core/paint/CloneStamp.h"
#include "core/paint/StrokeSmoother.h"
#include "core/stroke/StrokePainter.h"

#include <QColor>

#include <optional>
#include <vector>

namespace doodlebold {

// One brush stroke on a document's current layer or its mask (the colour paints its luma): blocks structural edits while it
// runs; one undo step, or the pixels are put back on cancel. A stroke too large for the memory there is (StrokeMemory) is limited:
// its brush size kept to what fits as it starts (Document::strokeLimited), and if it still grows past it, the rest of it left out
// (Document::strokeStopped).
class StrokeController {
public:
    explicit StrokeController(Document* doc) : m_doc(doc) {}
    ~StrokeController() { cancel(); }
    StrokeController(const StrokeController&) = delete;
    StrokeController& operator=(const StrokeController&) = delete;

    bool isActive() const { return m_layer.isValid(); }
    void setPressureRange(PressureRange range) { m_engine.setPressureRange(range); }
    // For a canvas that draws frames of its own: see RebuiltStroke. A stroke's paint is reported when it's shown.
    void paintInBackground(std::function<void()> onFrame, std::function<void()> whileCatchingUp) {
        m_engine.paintInBackground(std::move(onFrame), std::move(whileCatchingUp));
    }
    bool hasUnshownPaint() const { return m_engine.hasUnshown(); }
    void showPaint();
    std::int64_t shownStampUs() const { return m_engine.shownStampUs(); }
    void waitUntilCaughtUp() { m_engine.waitUntilCaughtUp(); }
    // The scale the view shows the stroke at, 2^level zoomed out (0: every pixel), so its frames come at that scale.
    void setViewLevel(int level) { m_engine.setViewLevel(level); }
    bool isInBackground() const { return m_engine.isInBackground(); }
    // Ends the stroke as finish() does, except that a stroke in the background is left to finish painting. Until finish() is called
    // it's still active and it's still the document's running stroke. finish() then waits if it has to, and keeps the undo step.
    void finishInBackground();
    bool isFinishing() const { return m_finishing; }
    bool isPainted() const { return m_engine.isPainted(); }
    CloneStamp& cloneStamp() { return m_clone; }
    const CloneStamp& cloneStamp() const { return m_clone; }
    // The Clone Stamp is refused on a layer mask and before its source is set. `stabilizerLevel` is how much the pen's path is smoothed.
    EditResult<> begin(const InputSample& first, const QColor& color, const BrushSettings& settings, int stabilizerLevel = 0);
    void add(const InputSample& sample);
    void finish();
    void cancel();
    // The running stroke's input, as it came.
    const std::vector<InputSample>& samples() const { return m_samples; }
    // Repaints the running stroke along `path` with the same tool and colour: its pixels
    // replace what the stroke has painted so far. The stroke stays open until finish() or finishAsDrawn().
    void repaintAlong(const std::vector<InputSample>& path);
    // The layer as it was before the stroke, for as long as nothing is painted again.
    void clearPainted();
    // Puts back what the stroke had painted before the first repaint and ends it as one undo step.
    void finishAsDrawn();

private:
    void end(bool keep);
    void report(const QRect& dirty);
    // The last frame taken: its tiles reported, its blocks given to the document. Whether it showed anything new.
    bool reportShown(const QRect& shown);
    void report(const QRegion& dirty);
    void rollBack();
    void paint(const std::vector<InputSample>& samples);
    void paintRestOfStroke();
    QRect beginEngine(const InputSample& first);
    void fitToMemory();
    bool stillFits();
    void tellStopped();

    Document* m_doc;
    StrokePainter m_engine;
    bool m_finishing = false;
    StrokeSmoother m_smoother;
    bool m_waitingForFirstPoint = false;  // the smoother is holding the pen's first point, so the engine hasn't begun
    CloneStamp m_clone;
    LayerId m_layer;
    bool m_onMask = false;
    TiledImage* m_target = nullptr;
    QColor m_color;
    BrushSettings m_settings;
    bool m_preserveOpacity = false;
    std::int64_t m_usableMemory = 0;  // what the process may hold between dabs, as the stroke began; 0 if there's no limit
    bool m_stopped = false;  // it grew past that, and the rest of it is left out
    std::int64_t m_heldAtStart = 0;  // what the process held as the stroke began
    std::vector<InputSample> m_samples;
    TileMap m_before;  // the tiles as they were before the stroke, gathered over its repaints
    TileMap m_drawn;  // the stroke's tiles before the first repaint
};

}
