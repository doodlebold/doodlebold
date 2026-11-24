#pragma once

#include "core/brush/BrushSettings.h"
#include "core/input/InputSample.h"
#include "core/raster/TiledImage.h"
#include "core/stroke/PressureCurve.h"
#include "core/stroke/RoundTip.h"
#include "core/stroke/StrokeBuffer.h"
#include "core/stroke/StrokeMerge.h"
#include "core/stroke/StrokeWorker.h"

#include <QColor>
#include <QRect>
#include <QRegion>

#include <memory>
#include <functional>
#include <future>
#include <optional>
#include <vector>

namespace doodlebold {

class BrushResourceLibrary;

// One stroke painted by the engine: begin() with the first sample, add() per further
// sample, lift() when the pen lifts, end() for the tiles as they were before the stroke. The first three return the area they
// changed. Each changed tile is worked out afresh from how it was before the stroke, so nothing painted can drift.
class RebuiltStroke {
public:
    // A plain round tip on a tool that lays colour or erases. Only the Brush mixes colours, and other tools that read the canvas can't
    // be painted here.
    static bool canPaint(const BrushSettings& settings);
    // The round tip with no tool's shaping or mixing, for the tests of the tip itself; the app never offers it.
    static inline const QString kPlainForm = QStringLiteral("Plain");
    // Where textures are found. Without a library no stroke has a texture's grain.
    static void setResources(const BrushResourceLibrary* library);

    void setPressureRange(PressureRange range) { m_range = range; }
    // The Clone Stamp's next stroke copies from `picture`, the pixel `offset` back from each it paints.
    void setCloneSource(std::shared_ptr<const TiledImage> picture, QPoint offset) {
        m_cloneSource = std::move(picture);
        m_cloneOffset = offset;
    }
    // From now on, strokes paint on a thread of their own, and what they've laid reaches the target when showNow() asks, or when the
    // pen lifts. `onFrame` says there's something to show, from that thread. `whileCatchingUp` runs now and then, on the caller's
    // thread, while a lift waits for painting to catch up. The final pixels are the same either way.
    void paintInBackground(std::function<void()> onFrame, std::function<void()> whileCatchingUp);
    // In the background, `stillFits` is asked before each lot of samples is laid, and once it says no the rest are left out
    // (StrokeWorker). Painting on the caller's thread, the caller asks it itself.
    void setMemoryGuard(std::function<bool()> stillFits) { m_stillFits = std::move(stillFits); }
    void setMemoryPressed(std::function<bool()> pressed) { m_memoryPressed = std::move(pressed); }
    void setMayGroupDabs(std::function<bool()> mayGroup) { m_mayGroupDabs = std::move(mayGroup); }
    bool ranOutOfMemory() const { return m_worker && m_worker->ranOutOfMemory(); }
    bool hasUnshown() const { return m_worker && m_worker->hasFrame(); }
    QRect showNow();
    // The blocks the last frame shown had in place of tiles (StrokeFrame), and their level; taking them leaves none.
    std::unordered_map<TileCoord, std::vector<float>, TileCoordHash> takeShownBlocks(int& level) {
        level = m_shownFrameLevel;
        return std::exchange(m_shownBlocks, {});
    }
    void setViewLevel(int level) {
        if (m_worker)
            m_worker->setViewLevel(level);
    }
    // When the newest pen event in what was last shown was queued, by StrokeWorker::nowUs().
    std::int64_t shownStampUs() const { return m_shownStampUs; }
    // The tiles the last showNow() changed, within the area it returned: a frame needs to draw only these.
    const QRegion& shownTiles() const { return m_shownTiles; }
    void waitUntilCaughtUp();
    bool isInBackground() const { return m_worker != nullptr; }
    // Ends the stroke without waiting for its painting, if it's in the background. It's for the caller to come back for end() once
    // isPainted() says it's done.
    bool liftInBackground();
    bool isPainted() const { return !m_worker || m_worker->isDone(); }
    // Until the last stroke's thread and records are freed: before the next stroke counts the memory it may use.
    void waitUntilRetired() const {
        if (m_retiring.valid())
            m_retiring.wait();
    }
    // Waits, running `whileCatchingUp`, instead of coming back.
    void catchUp();
    // Throws away what hasn't been shown. The caller restores what has, from the tiles returned.
    TileMap abandon();

    QRect begin(TiledImage* target, const QColor& color, const BrushSettings& given, const InputSample& first, bool preserveOpacity = false,
                const SelectionMask* selection = nullptr);
    QRect add(const InputSample& sample);
    QRect lift();
    TileMap end();
    bool active() const { return m_target != nullptr; }
    // Lays everything received so far and ends the path as it stands, with no fall in width at the end: for a Quick Shape,
    // which has to stay exactly as it's shown.
    QRect layWholePath();

private:
    InputSample inRange(InputSample sample) const;
    QRect show();
    void listTileToShow(TileCoord coord, std::vector<TileMergeJob>& jobs);
    void post(const InputSample& sample);
    void postPressureZero();
    void dropEmptyNewTiles();
    void retireWorker();

    TiledImage* m_target = nullptr;
    const SelectionMask* m_selection = nullptr;
    StrokePaint m_paint;
    std::shared_ptr<const TiledImage> m_cloneSource;
    QPoint m_cloneOffset;
    std::optional<RoundTipEvaluator> m_evaluator;
    StrokeBuffer m_buffer;
    std::optional<TiledImage> m_layerBefore;  // what a mixing tip reads, when not on a thread of its own
    TileMap m_before;
    bool m_inBackground = false;
    std::function<void()> m_onFrame;
    std::function<bool()> m_stillFits;
    std::function<bool()> m_memoryPressed;
    std::function<bool()> m_mayGroupDabs;
    std::function<void()> m_whileCatchingUp;
    std::unique_ptr<StrokeWorker> m_worker;
    // The last stroke's thread and records, being freed on a thread of their own once it ended (end()).
    std::shared_future<void> m_retiring;
    std::int64_t m_shownStampUs = 0;
    QRegion m_shownTiles;
    std::unordered_map<TileCoord, std::vector<float>, TileCoordHash> m_shownBlocks;
    int m_shownBlocksLevel = 0;
    int m_shownFrameLevel = 0;  // the level the last frame taken was made for: above 0, whole tiles alone in it show nothing new
    InputSample m_last;
    PressureRange m_range;
    bool m_lifted = false;
};

}
