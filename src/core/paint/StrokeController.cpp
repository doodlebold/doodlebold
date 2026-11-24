#include "core/paint/StrokeController.h"

#include "core/document/LayerOps.h"
#include "core/paint/StrokeMemory.h"
#include "core/stroke/TimingLog.h"

#include <QCoreApplication>
#include <QElapsedTimer>

namespace doodlebold {

namespace {

// A stroke that grew the process by this much gives what it freed back to the system as it ends (a 4000 px Marker on a 3000 px
// canvas held 2.4 GB after it otherwise). Not after every stroke: the next one would have to be given the memory again, which costs
// more than keeping it (a 1500 px stroke takes about 0.8 GB there).
constexpr std::int64_t kGiveBackFrom = std::int64_t(1) << 30;

// The Pencil's line narrows to a point where the pen lifted as its pressure falls to 0, and every
// other tool the engine paints ends the same way, its pressure easing out through the smoothing. The Marker's
// pressure also falls a little slower than it rises.
StrokeSmoother::Ends strokeEndsFor(const BrushSettings& settings) {
    if (!RebuiltStroke::canPaint(settings))
        return {};
    return {true};
}

}

EditResult<> StrokeController::begin(const InputSample& first, const QColor& color, const BrushSettings& settings, int stabilizerLevel) {
    if (isActive())
        return EditResult<>::failure(EditError::StrokeInProgress);
    const LayerId id = m_doc->currentLayerId();
    const bool onMask = m_doc->maskTarget();
    const bool cloning = settings.tool == ToolType::Clone;
    if (cloning && onMask)
        return EditResult<>::failure(EditError::CloneOnMask);
    const auto target = onMask ? m_doc->maskPaintTarget(id) : m_doc->paintTarget(id);
    if (!target)
        return EditResult<>::failure(target.error());
    const std::optional<QPoint> cloneOffset = cloning ? m_clone.beginStroke(first.pos, settings.cloneAligned) : std::nullopt;
    if (cloning && !cloneOffset)
        return EditResult<>::failure(EditError::NoCloneSource);
    // The Clone Stamp copies from the picture as the stroke starts, so what it has just painted isn't copied again: the current
    // layer, or every visible layer flattened over transparency.
    if (cloning)
        m_engine.setCloneSource(std::make_shared<const TiledImage>(settings.cloneAllLayers ? m_doc->mergedImage() : *target.value()),
                                *cloneOffset);
    m_layer = id;
    m_onMask = onMask;
    m_doc->setPainting(true);
    const int grey = layer_ops::maskValueOf(color);
    const LayerProps& layerProps = m_doc->layer(id)->props;
    m_target = target.value();
    m_color = onMask ? QColor(grey, grey, grey) : color;
    m_settings = settings;
    m_settings.stabilizer = StabilizerLevel{stabilizerLevel};  // the level it's smoothed at: with any, its path is a curve
    fitToMemory();
    m_preserveOpacity = !onMask && layerProps.preserveOpacity;
    m_samples.assign(1, first);
    m_smoother = StrokeSmoother(stabilizerLevel, strokeEndsFor(settings));
    m_waitingForFirstPoint = true;
    paint(m_smoother.feed(first));
    return {};
}

// A stroke that wouldn't fit gets the largest brush size that does, unless brushes aren't kept within memory. Strokes that fit are
// painted exactly as they would be anyway.
void StrokeController::fitToMemory() {
    m_stopped = false;
    m_engine.waitUntilRetired();  // the last stroke's records are freed before this one counts what it may use
    if (const std::optional<int> size = stroke_memory::sizeLimit(m_settings, m_target->size())) {
        emit m_doc->strokeLimited(m_settings.size, *size);
        m_settings.size = *size;
    }
    m_heldAtStart = stroke_memory::heldBytes();
    m_usableMemory = stroke_memory::usableOrOverride();
    if (m_usableMemory <= 0) {
        m_engine.setMemoryGuard({});
        m_engine.setMemoryPressed({});
        m_engine.setMayGroupDabs({});
        return;
    }
    // While it's painted, what it holds between dabs must leave room for the next dab's own.
    m_usableMemory = std::max<std::int64_t>(m_usableMemory - stroke_memory::dabBytes(m_settings, m_target->size()), 1);
    m_engine.setMemoryGuard([usable = m_usableMemory] { return stroke_memory::heldBytes() <= usable; });
    // A long wide stroke's record of what it's laid is packed once it's near what it may hold, so it needn't stop. The Brush can start
    // sooner: it reads no blur round its dab, so packing costs it little; for the others most of the record would be read again.
    const std::int64_t packFrom = m_settings.tool == ToolType::Brush ? m_usableMemory / 4 * 3 : m_usableMemory / 16 * 15;
    m_engine.setMemoryPressed([packFrom] { return stroke_memory::heldBytes() > packFrom; });
    // A dab grouped from what's waiting reaches many new tiles at once, which the looks between lots don't see coming: with memory to spare only.
    m_engine.setMayGroupDabs([usable = m_usableMemory] { return stroke_memory::heldBytes() < usable / 4 * 3; });
}

bool StrokeController::stillFits() {
    if (!m_stopped && m_usableMemory > 0 && (m_engine.ranOutOfMemory() || stroke_memory::heldBytes() > m_usableMemory))
        tellStopped();
    return !m_stopped;
}

void StrokeController::tellStopped() {
    m_stopped = true;
    emit m_doc->strokeStopped(m_settings.size);
}

void StrokeController::showPaint() {
    if (reportShown(m_engine.showNow()))
        timing_log::paintShown();
    if (!m_stopped && m_engine.ranOutOfMemory())
        tellStopped();
}

QRect StrokeController::beginEngine(const InputSample& first) {
    const std::optional<SelectionMask>& selection = m_doc->selection();
    return m_engine.begin(m_target, m_color, m_settings, first, m_preserveOpacity, selection ? &*selection : nullptr);
}

void StrokeController::paint(const std::vector<InputSample>& samples) {
    QRect dirty;
    for (const InputSample& sample : samples) {
        dirty |= m_waitingForFirstPoint ? beginEngine(sample) : m_engine.add(sample);
        m_waitingForFirstPoint = false;
    }
    report(dirty);
}

void StrokeController::paintRestOfStroke() {
    paint(m_smoother.finish());
    if (m_engine.active())
        report(m_engine.lift());
}

// A frame's tiles, each part of the region on its own, so a display redraws only those.
bool StrokeController::reportShown(const QRect& shown) {
    if (shown.isEmpty())
        return false;
    int level = 0;
    auto blocks = m_engine.takeShownBlocks(level);
    const QRegion tiles = m_engine.shownTiles(shown);
    if (!blocks.empty()) {
        // Blocks for the zoomed-out view; tiles, if any came too, go on the layer as well.
        m_doc->showStrokeBlocks(m_layer, level, std::move(blocks), shown);
        if (m_engine.shownAnyTiles())
            report(tiles);
        return true;
    }
    report(tiles);
    // Zoomed out, whole tiles alone are the stroke's held-back ones going onto the layer: the view already shows them.
    return level == 0;
}

void StrokeController::report(const QRegion& dirty) {
    for (const QRect& rect : dirty)
        report(rect);
}

void StrokeController::report(const QRect& dirty) {
    if (dirty.isEmpty())
        return;
    if (m_onMask)
        m_doc->notifyMaskPainted(m_layer, dirty);
    else
        m_doc->notifyPixelsChanged(m_layer, dirty);
}

void StrokeController::add(const InputSample& sample) {
    if (!isActive() || (!m_engine.active() && !m_waitingForFirstPoint) || !stillFits())
        return;
    m_samples.push_back(sample);
    paint(m_smoother.feed(sample));
}

// Takes the last painted run's pixels off the target.
void StrokeController::rollBack() {
    paintRestOfStroke();
    if (!m_engine.active())
        return;
    TileMap touched = m_engine.end();
    const bool first = m_before.empty() && m_drawn.empty();
    QRect area;
    for (const auto& [coord, tile] : touched) {
        if (first)
            m_drawn[coord] = m_target->tileShared(coord.x, coord.y);
        m_target->setTile(coord.x, coord.y, tile);
        area |= coord.pixelRect();
    }
    m_before.merge(touched);
    report(area);
}

void StrokeController::repaintAlong(const std::vector<InputSample>& path) {
    if (!isActive() || path.empty())
        return;
    rollBack();
    m_settings.stabilizer = StabilizerLevel{};  // a Quick Shape's corners stay corners
    QRect dirty = beginEngine(path.front());
    for (std::size_t i = 1; i < path.size(); ++i)
        dirty |= m_engine.add(path[i]);
    dirty |= m_engine.layWholePath();
    report(dirty);
}

void StrokeController::clearPainted() {
    if (isActive())
        rollBack();
}

void StrokeController::finishAsDrawn() {
    if (!isActive())
        return;
    rollBack();
    QRect area;
    for (const auto& [coord, tile] : m_before) {
        const auto drawn = m_drawn.find(coord);
        m_target->setTile(coord.x, coord.y, drawn != m_drawn.end() ? drawn->second : tile);
        area |= coord.pixelRect();
    }
    report(area);
    end(true);
}

void StrokeController::finish() {
    if (m_finishing) {
        m_engine.catchUp();
        if (reportShown(m_engine.showNow()))
            timing_log::paintShown();
    } else if (isActive()) {
        paintRestOfStroke();
    }
    end(true);
}

void StrokeController::finishInBackground() {
    if (!isActive())
        return;
    paint(m_smoother.finish());
    if (m_engine.active() && m_engine.liftInBackground()) {
        m_finishing = true;
        return;
    }
    finish();
}

void StrokeController::cancel() {
    end(false);
}

void StrokeController::end(bool keep) {
    if (!isActive())
        return;
    m_waitingForFirstPoint = false;
    m_finishing = false;
    QElapsedTimer timed;
    timed.start();
    TileMap last = keep ? m_engine.end() : m_engine.abandon();
    timing_log::note("stroke end", timed.nsecsElapsed() / 1000);
    timed.restart();
    TileMap before = std::exchange(m_before, {});
    before.merge(last);
    m_drawn.clear();
    m_samples.clear();
    m_target = nullptr;
    const LayerId layer = m_layer;
    m_layer = kNoLayer;
    m_doc->setPainting(false);
    m_doc->clearStrokeBlocks();
    const QString text = QCoreApplication::translate("StrokeController", "Pen stroke");
    if (std::exchange(m_onMask, false)) {
        m_doc->endMaskPaint(layer, keep, text);
    } else if (keep) {
        m_doc->commitPixelEdit(layer, before, text);
        timing_log::note("commit", timed.nsecsElapsed() / 1000);
    } else {
        m_doc->dropStrokeFrames();
        m_doc->restoreTiles(layer, before);
    }
    // Only after a stroke that took a lot: giving it back looks through all the C library keeps, a few ms even with nothing to give.
    if (stroke_memory::heldBytes() - m_heldAtStart >= kGiveBackFrom)
        stroke_memory::giveBackFreedSoon();
    stroke_memory::strokeEnded();
}

}
