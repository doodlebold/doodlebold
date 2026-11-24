#pragma once

#include "core/effects/EffectRender.h"

#include <QRect>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace doodlebold {

// One tile the job has finished: its new pixels, and whether they differ from the layer's.
struct EffectTile {
    TileCoord coord;
    TilePtr pixels;
    bool changed = false;
};

// An effect worked out on threads of its own, which give way to the interface's, so the window stays responsive and painting
// smooth while it runs. Finished tiles are collected with take(), in the order of orderTiles(focus). Cancelling stops it
// between tiles; the destructor cancels and waits.
class EffectJob {
public:
    EffectJob(std::shared_ptr<const EffectSource> source, const EffectSettings& settings, const QRect& focus);
    ~EffectJob();
    EffectJob(const EffectJob&) = delete;
    EffectJob& operator=(const EffectJob&) = delete;

    const EffectSettings& settings() const { return m_settings; }
    void cancel();
    bool isCancelled() const { return m_cancel.load(); }
    // Every tile is done (or the job was cancelled and has stopped).
    bool isFinished() const { return m_finished.load(); }
    // Waits until the job is finished or `timeout` passes; returns isFinished().
    bool wait(std::chrono::milliseconds timeout);
    void wait();
    // 0 … 1, the share of tiles done.
    double progress() const;
    std::vector<EffectTile> take();

private:
    void run(QRect focus);
    void parallel(int n, const std::function<void(int)>& item);

    std::shared_ptr<const EffectSource> m_source;
    EffectSettings m_settings;
    std::atomic<bool> m_cancel{false};
    std::atomic<bool> m_finished{false};
    std::atomic<int> m_total{-1};
    std::atomic<int> m_done{0};
    std::mutex m_mutex;
    std::condition_variable m_finishedSignal;
    std::vector<EffectTile> m_ready;
    std::thread m_thread;
};

}
