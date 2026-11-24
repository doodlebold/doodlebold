#include "core/stroke/StrokeWorker.h"

#include "core/stroke/TimingLog.h"
#include "core/util/WorkerPool.h"
#include "core/layers/Compositor.h"

#include "core/util/ThreadPriority.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <utility>

// docs/brush-engine/stroke-worker.md says what the named values here are for and how they were chosen.
namespace doodlebold {

namespace {

// While the view waits for a stroke still painting (a settle: Undo, a panel click, a gesture), it shows the newest frame at least this
// often, one 60 Hz frame: 8 ms shows nothing sooner, as frames come a go apart; 33 ms shows them up to 18 ms later (738a5f3).
constexpr auto kLongestWaitBetweenFrames = std::chrono::milliseconds(16);
// us: how long the stroke's thread lays for before merging, if a frame's waiting.
constexpr std::int64_t kTimePerGoUs = 30000;
// A go takes at most this many samples off the queue, and a lot holds at most this many: binds only on a backlog past 256
// samples, or on lots of samples under 58.6 us; from 9 up no pixel moves, and 128 to 512 lay a 750-sample backlog within 5 ms of each
// other, the smaller showing more frames.
constexpr std::size_t kMostSamplesPerGo = 256;
// A stroke this wide whose oldest waiting pen event is this old has fallen behind: what's waiting goes down in fewer
// dabs, at most this many samples to one.
constexpr double kBehindFromDiameter = 1000.0;  // px
constexpr std::int64_t kBehindUs = 100000;
constexpr std::size_t kMostSamplesGrouped = 8;  // and one go is one lot of these, so memory is looked at between them

std::atomic<std::int64_t>& timePerGoUs() {
    static std::atomic<std::int64_t> us{kTimePerGoUs};
    return us;
}

}

void StrokeWorker::setTimePerGoForBenches(std::int64_t us) {
    timePerGoUs() = us > 0 ? us : kTimePerGoUs;
}

namespace {

std::atomic<StrokeWorker::Backlog>& backlogMode() {
    static std::atomic<StrokeWorker::Backlog> mode{StrokeWorker::Backlog::ByTheClock};
    return mode;
}

}

void StrokeWorker::setBacklogForTests(Backlog backlog) {
    backlogMode() = backlog;
}

StrokeWorker::StrokeWorker(const RoundTip& tip, StrokePaint paint, TiledImage before, std::optional<SelectionMask> selection,
                           std::function<void()> onFrame, std::function<bool()> stillFits, std::function<bool()> memoryPressed,
                           std::function<bool()> mayGroupDabs)
    : m_evaluator(tip),
      m_paint(std::move(paint)),
      m_before(std::move(before)),
      m_selection(std::move(selection)),
      m_onFrame(std::move(onFrame)),
      m_stillFits(std::move(stillFits)),
      m_memoryPressed(std::move(memoryPressed)),
      m_mayGroupDabs(std::move(mayGroupDabs)),
      m_grouping(tip.radius * 2.0 >= kBehindFromDiameter &&
                 (tip.mixing.kind == TipMixing::Kind::Brush || tip.mixing.kind == TipMixing::Kind::WaterColor)),
      m_thread([this] { run(); }) {}

StrokeWorker::~StrokeWorker() {
    {
        std::scoped_lock lock(m_mutex);
        m_stop = true;
    }
    m_wake.notify_all();
    m_thread.join();
}

std::int64_t StrokeWorker::nowUs() {
    const auto sinceStart = std::chrono::steady_clock::now().time_since_epoch();
    return std::chrono::duration_cast<std::chrono::microseconds>(sinceStart).count();
}

void StrokeWorker::postItem(ItemKind kind, const InputSample& sample) {
    {
        std::scoped_lock lock(m_mutex);
        m_queue.push_back({kind, sample, nowUs()});
    }
    m_wake.notify_all();
}

void StrokeWorker::postBegin(const InputSample& first) {
    postItem(ItemKind::Begin, first);
}

void StrokeWorker::post(const InputSample& sample) {
    postItem(ItemKind::Sample, sample);
}

void StrokeWorker::postEnd() {
    postItem(ItemKind::End, InputSample{});
}

bool StrokeWorker::isDone() const {
    std::scoped_lock lock(m_mutex);
    return m_queue.empty() && !m_working;
}

StrokeFrame StrokeWorker::takeFrame() {
    StrokeFrame frame;
    {
        std::scoped_lock lock(m_mutex);
        frame = std::exchange(m_frame, {});
        m_hasFrame.store(false, std::memory_order_release);
        m_frameWanted = true;
    }
    m_wake.notify_all();
    return frame;
}

void StrokeWorker::waitUntilDone(const std::function<void()>& whileWaiting) {
    std::unique_lock lock(m_mutex);
    const auto done = [this] { return m_queue.empty() && !m_working; };
    while (!done()) {
        if (m_finished.wait_for(lock, kLongestWaitBetweenFrames, done))
            return;
        if (whileWaiting && m_hasFrame.load(std::memory_order_acquire)) {
            lock.unlock();
            whileWaiting();
            lock.lock();
        }
    }
}

void StrokeWorker::run() {
    lowerThisThreadsPriority();
    m_evaluator.readFrom(&m_before, m_paint);
    m_evaluator.setBetweenDabs([this] { showBetweenDabs(); });
    m_evaluator.setMemoryPressed(m_memoryPressed);
    for (;;) {
        std::vector<Item> items;
        {
            std::unique_lock lock(m_mutex);
            const bool wholeStroke = backlogMode().load() == Backlog::Always;
            m_wake.wait(lock, [this, wholeStroke] {
                const bool ready = !m_queue.empty() && (!wholeStroke || m_queue.back().kind == ItemKind::End);
                return m_stop || ready || (m_frameWanted && m_anyUnshown);
            });
            if (m_stop)
                return;
            const std::size_t count = std::min(m_queue.size(), kMostSamplesPerGo);
            items.assign(m_queue.begin(), m_queue.begin() + static_cast<std::ptrdiff_t>(count));
            m_queue.erase(m_queue.begin(), m_queue.begin() + static_cast<std::ptrdiff_t>(count));
            m_working = true;
        }
        leaveOutSamplesIfFull(items);
        lay(items);
        if (!items.empty() && items.back().kind == ItemKind::End)
            m_ended = true;
        bool merge = false;
        {
            std::scoped_lock lock(m_mutex);
            m_anyUnshown = !m_unshownTiles.empty();
            const bool heldWhole = !m_heldBack.empty() && (m_ended || m_viewLevel.load(std::memory_order_relaxed) == 0);
            merge = (m_anyUnshown || heldWhole) && (m_frameWanted || m_queue.empty());
        }
        if (merge)
            mergeAndPublish();
        {
            std::scoped_lock lock(m_mutex);
            m_working = false;
        }
        m_finished.notify_all();
        const bool ended = !items.empty() && items.back().kind == ItemKind::End;
        if (ended && m_onFrame)
            m_onFrame();
    }
}

void StrokeWorker::leaveOutSamplesIfFull(std::vector<Item>& items) {
    if (!m_outOfMemory && m_stillFits && !m_stillFits())
        m_outOfMemory.store(true, std::memory_order_release);
    if (m_outOfMemory)
        std::erase_if(items, [](const Item& item) { return item.kind == ItemKind::Sample; });
}

// How many samples are laid together: as many as the costliest this stroke has had would take half a go. A sample over paint already
// solid is almost free and the next over bare canvas may not be, so what the last few took says nothing about the next.
std::size_t StrokeWorker::samplesPerLot() const {
    if (m_costliestSampleUs <= 0)
        return 1;
    const std::int64_t aim = timePerGoUs().load(std::memory_order_relaxed);
    return static_cast<std::size_t>(std::clamp<std::int64_t>(aim / 2 / m_costliestSampleUs, 1, kMostSamplesPerGo));
}

// Samples next to each other are laid together, so a backlog turns into less waking of the workers. The pieces go down in the same
// order, so the pixels are the same. A backlog mustn't be laid all at once, or nothing new would show until it's done, so once a go
// has taken its time the rest goes back on the queue. `items` is left as what was laid.
void StrokeWorker::lay(std::vector<Item>& items) {
    const std::int64_t aim = timePerGoUs().load(std::memory_order_relaxed);
    const std::int64_t startedUs = nowUs();
    std::vector<InputSample> run;
    for (std::size_t i = 0; i < items.size(); ++i) {
        const Item& item = items[i];
        m_newestLaidUs = item.stampUs;
        if (item.kind == ItemKind::Sample) {
            run.push_back(item.sample);
            const Backlog mode = backlogMode().load();
            const bool waiting = m_grouping && run.size() == 1 && items.size() - i >= 2;
            const bool behind =
                waiting && (mode == Backlog::Always ||
                            (mode == Backlog::ByTheClock && nowUs() - item.stampUs >= kBehindUs && (!m_mayGroupDabs || m_mayGroupDabs())));
            if (behind) {
                // The samples waiting, as they lie: only the End or the next Begin stops a run of them.
                for (std::size_t next = i + 1;
                     next < items.size() && run.size() < kMostSamplesGrouped && items[next].kind == ItemKind::Sample; ++next)
                    run.push_back(items[next].sample);
                i += run.size() - 1;
                m_newestLaidUs = items[i].stampUs;
                layRun(run, true);
            } else if (run.size() < samplesPerLot()) {
                continue;
            } else {
                layRun(run);
            }
            if (i + 1 < items.size() && (behind || nowUs() - startedUs >= aim)) {
                {
                    std::scoped_lock lock(m_mutex);
                    m_queue.insert(m_queue.begin(), items.begin() + static_cast<std::ptrdiff_t>(i + 1), items.end());
                }
                items.resize(i + 1);
                break;
            }
            continue;
        }
        layRun(run);
        if (item.kind == ItemKind::Begin)
            m_evaluator.begin(item.sample, m_buffer);
        else
            m_evaluator.end(m_buffer);
    }
    layRun(run);
    noteLaid();
}

void StrokeWorker::noteLaid() {
    m_unshownArea |= m_evaluator.takeDirty();
    const std::vector<TileCoord> changed = m_evaluator.takeChangedTiles();
    m_unshownTiles.insert(changed.begin(), changed.end());
}

// A single pen event can be several wide dabs, and a stroke's end several more, each a good part of a frame: a frame that's waiting
// shows the finished dabs, rather than nothing until the event is laid.
void StrokeWorker::showBetweenDabs() {
    if (nowUs() - m_lastPublishedUs < timePerGoUs().load(std::memory_order_relaxed))
        return;
    {
        std::scoped_lock lock(m_mutex);
        if (!m_frameWanted)
            return;
    }
    noteLaid();
    if (!m_unshownTiles.empty())
        mergeAndPublish();
}

void StrokeWorker::layRun(std::vector<InputSample>& run, bool groupDabs) {
    if (run.empty())
        return;
    const std::int64_t startedUs = nowUs();
    m_evaluator.addAll(run, m_buffer, groupDabs);
    m_costliestSampleUs = std::max(m_costliestSampleUs, (nowUs() - startedUs) / static_cast<std::int64_t>(run.size()));
    run.clear();
}

void StrokeWorker::mergeAndPublish() {
    // Zoomed out, the view takes blocks at its scale, and the whole tiles are held back for the stroke's end or a view that needs
    // them. At the end, the last blocks go first, so the view shows the finished stroke before the held-back tiles are merged whole.
    const int level = m_viewLevel.load(std::memory_order_relaxed);
    if (level > 0 && m_ended && !m_heldBack.empty() && !m_unshownTiles.empty())
        publish(level, true);
    publish(level, level > 0 && !m_ended);
}

void StrokeWorker::publish(int level, bool holdBack) {
    const std::int64_t startedUs = nowUs();
    // What was held back goes again: whole now, or as blocks at another scale.
    if ((!holdBack || level != m_blocksLevel) && !m_heldBack.empty()) {
        for (const TileCoord coord : m_heldBack) {
            m_unshownTiles.insert(coord);
            m_unshownArea |= coord.pixelRect();
        }
        if (!holdBack)
            m_heldBack.clear();
    }
    if (level > 0)
        m_blocksLevel = level;
    const QRect area = std::exchange(m_unshownArea, QRect()).intersected(m_before.paintArea());
    std::vector<TileMergeJob> jobs, gridOnly;
    jobs.reserve(m_unshownTiles.size());
    for (const TileCoord coord : m_unshownTiles) {
        const TilePtr painted = m_evaluator.paintedTile(coord);
        const auto density = m_buffer.tiles().find(coord);
        if ((!painted && density == m_buffer.tiles().end()) || !area.intersects(coord.pixelRect()))
            continue;
        const SelectionTile* selected = m_selection ? m_selection->tile(coord.x, coord.y) : nullptr;
        if (!m_selection || selected) {
            TileMergeJob job{coord,  painted ? nullptr : density->second.get(), selected, m_before.tileShared(coord.x, coord.y), nullptr,
                             painted};
            if (painted && m_paint.grain && m_paint.edgeBarDepth > 0.0f)
                job.outlineOf = m_evaluator.paintedOutline(coord);  // read between dabs, as the painter rests
            if (!painted && density->second->gridCell > 0)
                if (const auto merged = m_merged.find(coord); merged != m_merged.end())
                    job.previous = merged->second;
            // Held back, a plain grid's tile gives its blocks from its points without being merged; it's merged once, at the end.
            (holdBack && !painted && density->second->gridCell > 0 ? gridOnly : jobs).push_back(std::move(job));
        }
    }
    m_unshownTiles.clear();
    mergeTiles(jobs, m_paint);
    for (const TileMergeJob& job : jobs)
        if (job.density && job.density->gridCell > 0)
            m_merged[job.coord] = job.fresh;
    std::unordered_map<TileCoord, std::vector<float>, TileCoordHash> blocks;
    if (holdBack) {  // tiles going whole need no blocks: the view has what they show
        const std::size_t floats = static_cast<std::size_t>((Tile::kSize >> level) * (Tile::kSize >> level) * 4);
        std::vector<std::vector<float>> reduced(jobs.size() + gridOnly.size(), std::vector<float>(floats));
        WorkerPool::forStrokes().run(static_cast<int>(reduced.size()), [&](int i) {
            const std::size_t k = static_cast<std::size_t>(i);
            if (k < jobs.size()) {
                Compositor::reduceTile(*jobs[k].fresh, level, reduced[k].data());
            } else {
                const TileMergeJob& job = gridOnly[k - jobs.size()];
                reduceGridTile(*job.density, m_paint, job.before.get(), job.selected, job.coord, level, reduced[k].data());
            }
        });
        for (std::size_t k = 0; k < reduced.size(); ++k)
            blocks[k < jobs.size() ? jobs[k].coord : gridOnly[k - jobs.size()].coord] = std::move(reduced[k]);
    }
    std::unordered_map<TileCoord, TilePtr, TileCoordHash> whole;
    QRect wholeArea;
    if (holdBack) {
        for (const TileMergeJob& job : jobs)
            m_heldBack.insert(job.coord);
        for (const TileMergeJob& job : gridOnly)
            m_heldBack.insert(job.coord);
    } else {
        for (TileMergeJob& job : jobs) {
            wholeArea |= job.coord.pixelRect();
            whole[job.coord] = std::move(job.fresh);
        }
    }
    const bool anyPainted = std::any_of(jobs.begin(), jobs.end(), [](const TileMergeJob& job) { return job.painted != nullptr; });
    jobs.clear();  // the painted tiles held back aren't shared with anyone, so the next dabs needn't copy them
    QRect blockArea;
    for (const auto& [coord, block] : blocks)
        blockArea |= coord.pixelRect();
    {
        std::scoped_lock lock(m_mutex);
        for (auto& [coord, tile] : whole)
            m_frame.tiles[coord] = std::move(tile);
        if (m_frame.level != level)
            m_frame.blocks.clear();
        m_frame.level = level;
        for (auto& [coord, block] : blocks)
            m_frame.blocks[coord] = std::move(block);
        m_frame.area |= (area | blockArea | wholeArea).intersected(m_before.paintArea());
        m_frame.newestStampUs = m_newestLaidUs;
        // The next frame is merged while this one is being shown, not after it's taken. One not taken yet gathers the newer tiles,
        // so what's shown is the same paint, sooner. Only where a merge costs: a mixing tool's frame is its painted tiles, and handing
        // those over early only makes the next dabs copy them.
        m_frameWanted = !anyPainted;
        m_anyUnshown = false;
        m_hasFrame.store(true, std::memory_order_release);
    }
    m_lastPublishedUs = nowUs();
    timing_log::note("published", m_lastPublishedUs - startedUs);
    if (m_onFrame)
        m_onFrame();
}

}
