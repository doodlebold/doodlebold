#pragma once

#include "core/stroke/RebuiltStroke.h"

namespace doodlebold {

// Paints a stroke: begin(), add() per sample, lift(), end(). Every tool that paints strokes paints through it;
// a tool that paints none, the Bucket, lays nothing. In the background it paints on a thread of its own (StrokeWorker) and
// its frames are shown with showNow().
class StrokePainter {
public:
    void setPressureRange(PressureRange range) { m_rebuilt.setPressureRange(range); }
    // The Clone Stamp's next stroke copies from `picture`, the pixel `offset` back from each it paints.
    void setCloneSource(const std::shared_ptr<const TiledImage>& picture, QPoint offset) { m_rebuilt.setCloneSource(picture, offset); }
    void paintInBackground(std::function<void()> onFrame, std::function<void()> whileCatchingUp) {
        m_rebuilt.paintInBackground(std::move(onFrame), std::move(whileCatchingUp));
    }
    void setMemoryGuard(std::function<bool()> stillFits) { m_rebuilt.setMemoryGuard(std::move(stillFits)); }
    void setMemoryPressed(std::function<bool()> pressed) { m_rebuilt.setMemoryPressed(std::move(pressed)); }
    void setMayGroupDabs(std::function<bool()> mayGroup) { m_rebuilt.setMayGroupDabs(std::move(mayGroup)); }
    void waitUntilRetired() const { m_rebuilt.waitUntilRetired(); }
    bool ranOutOfMemory() const { return m_painting && m_rebuilt.ranOutOfMemory(); }
    bool hasUnshown() const { return m_painting && m_rebuilt.active() && m_rebuilt.hasUnshown(); }
    QRect showNow() { return m_painting ? m_rebuilt.showNow() : QRect(); }
    std::unordered_map<TileCoord, std::vector<float>, TileCoordHash> takeShownBlocks(int& level) {
        level = 0;
        return m_painting ? m_rebuilt.takeShownBlocks(level) : std::unordered_map<TileCoord, std::vector<float>, TileCoordHash>{};
    }
    // Whether the last frame shown put tiles on the layer, rather than only blocks for a zoomed-out view.
    bool shownAnyTiles() const { return !m_painting || !m_rebuilt.shownTiles().isEmpty(); }
    void setViewLevel(int level) {
        if (m_painting)
            m_rebuilt.setViewLevel(level);
    }
    // What the last showNow() changed, tile by tile; the area it returned if it can't say.
    QRegion shownTiles(const QRect& area) const {
        if (area.isEmpty())
            return {};
        return m_painting && !m_rebuilt.shownTiles().isEmpty() ? m_rebuilt.shownTiles() : QRegion(area);
    }
    std::int64_t shownStampUs() const { return m_rebuilt.shownStampUs(); }
    void waitUntilCaughtUp() { m_rebuilt.waitUntilCaughtUp(); }
    bool isInBackground() const { return m_painting && m_rebuilt.isInBackground(); }
    bool liftInBackground() { return m_painting && m_rebuilt.liftInBackground(); }
    bool isPainted() const { return !m_painting || m_rebuilt.isPainted(); }
    void catchUp() {
        if (m_painting)
            m_rebuilt.catchUp();
    }
    TileMap abandon() { return m_painting ? m_rebuilt.abandon() : TileMap{}; }
    QRect begin(TiledImage* target, const QColor& color, const BrushSettings& settings, const InputSample& first,
                bool preserveOpacity = false, const SelectionMask* selection = nullptr) {
        m_painting = RebuiltStroke::canPaint(settings);
        return m_painting ? m_rebuilt.begin(target, color, settings, first, preserveOpacity, selection) : QRect();
    }
    QRect add(const InputSample& sample) { return m_painting ? m_rebuilt.add(sample) : QRect(); }
    QRect lift() { return m_painting ? m_rebuilt.lift() : QRect(); }
    TileMap end() { return m_painting ? m_rebuilt.end() : TileMap{}; }
    bool active() const { return m_painting && m_rebuilt.active(); }
    // Finishes the path as it stands with no fall in width, so a Quick Shape stays exactly as it's shown.
    QRect layWholePath() { return m_painting ? m_rebuilt.layWholePath() : QRect(); }

private:
    RebuiltStroke m_rebuilt;
    bool m_painting = false;  // the last begin() was a tool that paints strokes
};

}
