#pragma once

#include "core/input/InputSample.h"
#include "core/selection/SelectionMask.h"
#include "core/stroke/RoundTip.h"
#include "core/stroke/StrokeBuffer.h"
#include "core/stroke/StrokeMerge.h"

#include <QRect>

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <optional>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace doodlebold {

// What a stroke has laid and merged since it was last asked for: finished tiles, ready to put on the layer.
struct StrokeFrame {
    std::unordered_map<TileCoord, std::shared_ptr<const Tile>, TileCoordHash> tiles;
    // For a view zoomed out by 2^level, the changed tiles as blocks (Compositor::reduceTile) in place of the tiles, which wait
    // for the stroke's end or a view that needs them.
    int level = 0;
    std::unordered_map<TileCoord, std::vector<float>, TileCoordHash> blocks;
    QRect area;
    std::int64_t newestStampUs = 0;  // when the newest pen event it shows was queued
};

// One stroke's painting, on a thread of its own, so that the pen's thread only has to queue samples. It works from a copy of the
// layer as it was before the stroke, which shares its tiles, so it never touches the document. The thread that owns the layer
// takes finished tiles with takeFrame() when it's ready to show them. While it's keeping up, every batch of samples is merged.
// When it's behind, it only merges again once the last frame has been taken, so it spends its time on the stroke.
class StrokeWorker {
public:
    // `onFrame` is called on the worker's thread, each time there's something new to take. Destroying the worker drops whatever it
    // hasn't got to yet.
    // `stillFits`, if given, is asked before each lot of samples is laid: once it says no, no more samples are laid, though the
    // stroke still ends as usual, so a stroke can't take more memory than there is.
    StrokeWorker(const RoundTip& tip, StrokePaint paint, TiledImage before, std::optional<SelectionMask> selection,
                 std::function<void()> onFrame, std::function<bool()> stillFits = {}, std::function<bool()> memoryPressed = {},
                 std::function<bool()> mayGroupDabs = {});
    ~StrokeWorker();
    StrokeWorker(const StrokeWorker&) = delete;
    StrokeWorker& operator=(const StrokeWorker&) = delete;

    void postBegin(const InputSample& first);
    void post(const InputSample& sample);
    void postEnd();

    bool hasFrame() const { return m_hasFrame.load(std::memory_order_acquire); }
    // Everything posted so far has been laid and merged. `onFrame` is called once more after the stroke's end gets here.
    bool isDone() const;
    StrokeFrame takeFrame();
    // `stillFits` said no: the samples from then on were left out.
    bool ranOutOfMemory() const { return m_outOfMemory.load(std::memory_order_acquire); }
    // Waits for everything posted so far to be laid and merged, calling `whileWaiting` now and then if there's a frame to take.
    void waitUntilDone(const std::function<void()>& whileWaiting);

    static std::int64_t nowUs();
    // How long a lot of samples aims to take, in place of the usual 30 ms (0 puts it back): for benches to compare.
    static void setTimePerGoForBenches(std::int64_t us);
    // How a stroke decides it has fallen behind, so that what's waiting goes down in fewer dabs: by the clock, as in the app,
    // or for the reference set never or always, so that what it paints doesn't depend on timing. Always waits for the stroke's end
    // before laying anything, so that its lots are the same every run.
    enum class Backlog {
        ByTheClock,
        Never,
        Always
    };
    static void setBacklogForTests(Backlog backlog);
    // The scale the view shows the stroke at, 2^level zoomed out, 0 for every pixel. From the thread that shows it.
    void setViewLevel(int level) { m_viewLevel.store(level, std::memory_order_relaxed); }

private:
    enum class ItemKind {
        Begin,
        Sample,
        End
    };
    struct Item {
        ItemKind kind = ItemKind::Sample;
        InputSample sample;
        std::int64_t stampUs = 0;
    };

    void postItem(ItemKind kind, const InputSample& sample);
    void run();
    void lay(std::vector<Item>& items);
    void leaveOutSamplesIfFull(std::vector<Item>& items);
    std::size_t samplesPerLot() const;
    void layRun(std::vector<InputSample>& run, bool groupDabs = false);
    void noteLaid();
    void showBetweenDabs();
    void mergeAndPublish();
    void publish(int level, bool holdBack);

    // The worker's own, untouched by anyone else while it runs.
    RoundTipEvaluator m_evaluator;
    StrokeBuffer m_buffer;
    std::unordered_map<TileCoord, TilePtr, TileCoordHash> m_merged;  // each tile as last merged, for a plain grid's next merge
    StrokePaint m_paint;
    TiledImage m_before;
    std::optional<SelectionMask> m_selection;
    std::function<void()> m_onFrame;
    std::function<bool()> m_stillFits;
    std::function<bool()> m_memoryPressed;
    std::function<bool()> m_mayGroupDabs;
    QRect m_unshownArea;
    std::unordered_set<TileCoord, TileCoordHash> m_unshownTiles;
    std::int64_t m_newestLaidUs = 0;
    std::int64_t m_lastPublishedUs = 0;
    bool m_grouping;  // this tip is wide enough to lay fewer dabs when it falls behind
    std::int64_t m_costliestSampleUs = 0;  // the longest a sample has taken in this stroke, as far as a lot of them says

    mutable std::mutex m_mutex;
    std::condition_variable m_wake;
    std::condition_variable m_finished;
    std::deque<Item> m_queue;
    StrokeFrame m_frame;
    bool m_frameWanted = true;
    bool m_anyUnshown = false;
    bool m_working = false;
    bool m_stop = false;
    std::atomic<bool> m_hasFrame{false};
    std::atomic<bool> m_outOfMemory{false};
    std::atomic<int> m_viewLevel{0};
    // The worker's own: tiles shown only as blocks so far, the level those blocks were at, and whether the stroke's end has
    // been laid, after which every tile goes whole.
    std::unordered_set<TileCoord, TileCoordHash> m_heldBack;
    int m_blocksLevel = 0;
    bool m_ended = false;

    std::thread m_thread;  // last, so everything it uses exists before it starts
};

}
