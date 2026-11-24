#pragma once

#include "core/layers/CanvasBackground.h"
#include "core/layers/LayerStack.h"
#include "core/timelapse/Timelapse.h"

#include <QImage>
#include <QRect>
#include <QThreadPool>

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace doodlebold {

// Records a canvas: the document reports invalidated areas (markChanged) and hands over the picture after each finished step
// (record); the recorder compares per-tile hashes, copies the tiles that changed and compresses them on its own thread, so the
// GUI thread only pays for the comparison and a copy. Everything but the worker is for the GUI thread.
class TimelapseRecorder {
public:
    TimelapseRecorder(const LayerStack& layers, const CanvasBackground& background);
    // Goes on with a recording read from a file (`recording` must not be empty). The next step is marked as the first after
    explicit TimelapseRecorder(const Timelapse& recording);
    ~TimelapseRecorder();
    TimelapseRecorder(const TimelapseRecorder&) = delete;
    TimelapseRecorder& operator=(const TimelapseRecorder&) = delete;

    bool isPaused() const { return m_paused; }
    void setPaused(bool paused) { m_paused = paused; }
    int stepCount() const { return m_stepCount; }
    std::int64_t startedMs() const { return m_startedMs; }
    std::int64_t paintedMs() const { return m_activeMs; }
    // The compressed pictures so far (the steps still waiting for the worker count their raw size once it has them).
    std::int64_t byteSize() const { return m_shared->bytes.load(); }

    void markChanged(const QRect& area);
    void markResized(const QSize& size);
    void beginStroke();
    void abandonStroke();
    // Stores the tiles of `picture` (opaque RGBA8888_Premultiplied) that differ from the last step. Nothing is stored and false
    // is returned if no pixel changed, except that the step ending a stroke is stored when frames of the stroke were.
    // `over`: tiles to take in place of picture's, each the tile's own size (a stroke's frame from a zoomed-out view);
    // they count as changed.
    using TileImages = std::unordered_map<TileCoord, QImage, TileCoordHash>;
    bool record(TimelapseStep::Kind kind, const QImage& picture, const TileImages* over = nullptr);

    // The recording as it stands; waits until the worker has compressed every step.
    Timelapse snapshot();
    void flush();

    // Painting time is counted between steps and a pause longer than this counts as this much.
    static constexpr std::int64_t kIdleCapMs = 3 * 60 * 1000;
    // For tests: where the time comes from (milliseconds on any steady scale).
    void setClock(std::function<std::int64_t()> clock);

private:
    struct Shared {
        std::atomic<int> pending{0};
        std::atomic<std::int64_t> bytes{0};
        std::mutex mutex;
        std::condition_variable done;
    };
    class EncodeJob;
    friend class EncodeJob;

    void resizeGrids(const QSize& size);
    std::int64_t now() const;
    void spendTime();
    void enqueue(std::shared_ptr<TimelapseStep> step, std::vector<QByteArray> raw);

    std::shared_ptr<Shared> m_shared = std::make_shared<Shared>();
    std::vector<std::shared_ptr<TimelapseStep>> m_steps;
    QSize m_size;
    int m_tilesX = 0;
    int m_tilesY = 0;
    std::vector<std::uint64_t> m_hash;  // of the last stored picture's tiles; 0: not known
    std::vector<std::uint8_t> m_dirty;
    QRect m_dirtyTiles;
    int m_stepCount = 0;
    int m_partials = 0;
    std::unordered_map<std::size_t, std::uint64_t> m_hashBeforeStroke;  // of the tiles those frames stored, as they were before
    bool m_strokeFirst = false;
    bool m_needFull = false;
    bool m_resumed = false;
    bool m_paused = false;
    std::int64_t m_activeMs = 0;
    std::int64_t m_startedMs = 0;
    std::int64_t m_lastTime = 0;
    std::function<std::int64_t()> m_clock;
    QThreadPool m_pool;
};

}
