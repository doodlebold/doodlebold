#include "core/effects/EffectJob.h"

#include "core/util/CpuCores.h"
#include "core/util/ThreadPriority.h"

#include <algorithm>
#include <utility>

namespace doodlebold {

EffectJob::EffectJob(std::shared_ptr<const EffectSource> source, const EffectSettings& settings, const QRect& focus)
    : m_source(std::move(source)), m_settings(settings) {
    m_thread = std::thread([this, focus] { run(focus); });
}

EffectJob::~EffectJob() {
    cancel();
    if (m_thread.joinable())
        m_thread.join();
}

void EffectJob::cancel() {
    m_cancel.store(true);
}

bool EffectJob::wait(std::chrono::milliseconds timeout) {
    std::unique_lock lock(m_mutex);
    return m_finishedSignal.wait_for(lock, timeout, [this] { return m_finished.load(); });
}

void EffectJob::wait() {
    std::unique_lock lock(m_mutex);
    m_finishedSignal.wait(lock, [this] { return m_finished.load(); });
}

double EffectJob::progress() const {
    const int total = m_total.load();
    if (total < 0)
        return 0;
    return total == 0 ? 1.0 : static_cast<double>(m_done.load()) / total;
}

std::vector<EffectTile> EffectJob::take() {
    std::scoped_lock lock(m_mutex);
    return std::exchange(m_ready, {});
}

// On threads of lower priority, leaving the interface a core: the job's own plus helpers, each taking the next item.
void EffectJob::parallel(int n, const std::function<void(int)>& item) {
    std::atomic<int> next{0};
    const auto work = [&] {
        for (int i = next++; i < n && !m_cancel.load(); i = next++)
            item(i);
    };
    const int helpers = std::clamp(cpu_cores::used() - 2, 0, std::max(0, n - 1));
    std::vector<std::thread> pool;
    pool.reserve(static_cast<std::size_t>(helpers));
    for (int t = 0; t < helpers; ++t)
        pool.emplace_back([&] {
            lowerThisThreadsPriority();
            work();
        });
    work();
    for (std::thread& t : pool)
        t.join();
}

void EffectJob::run(QRect focus) {
    lowerThisThreadsPriority();
    EffectRenderer renderer(*m_source, m_settings);
    std::vector<TileCoord> tiles = renderer.tiles();
    orderTiles(tiles, focus);
    m_total.store(static_cast<int>(tiles.size()));
    renderer.prepare([this](int n, const std::function<void(int)>& item) { parallel(n, item); });
    parallel(static_cast<int>(tiles.size()), [&](int i) {
        const TileCoord c = tiles[static_cast<std::size_t>(i)];
        const TilePtr pixels = renderer.render(c);
        const bool changed = pixels != m_source->pixels.tileShared(c.x, c.y);
        {
            std::scoped_lock lock(m_mutex);
            m_ready.push_back({c, pixels, changed});
        }
        ++m_done;
    });
    {
        std::scoped_lock lock(m_mutex);
        m_finished.store(true);
    }
    m_finishedSignal.notify_all();
}

}
