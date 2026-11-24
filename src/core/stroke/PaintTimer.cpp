#include "core/stroke/PaintTimer.h"

#include <algorithm>
#include <utility>

namespace doodlebold {

namespace {

// The timer keeps at most this many frames' times a stroke (65536), so a stroke's record stays bounded, reserved up front so timing
// never allocates: 18 minutes of one stroke at 60 Hz, 2.6 MB; the longest recorded stroke has 324 events.
constexpr std::size_t kMostFrames = 1 << 16;

std::int64_t percentile(const std::vector<std::int64_t>& sorted, double share) {
    const auto index = static_cast<std::size_t>(share * static_cast<double>(sorted.size() - 1));
    return sorted[index];
}

// Leaves the two at 0 if nothing was timed.
void medianAndWorst(std::vector<std::int64_t>& times, std::int64_t& median, std::int64_t& worst) {
    if (times.empty())
        return;
    std::sort(times.begin(), times.end());
    median = percentile(times, 0.5);
    worst = times.back();
}

}

PaintTimer::PaintTimer() {
    m_waits.reserve(kMostFrames);
    m_draws.reserve(kMostFrames);
    m_handles.reserve(kMostFrames);
    m_swaps.reserve(kMostFrames);
    m_inkAges.reserve(kMostFrames);
}

void PaintTimer::eventReceived(std::int64_t nowUs) {
    ++m_events;
    m_latestEvent = nowUs;
    m_liftedAt.reset();
}

void PaintTimer::pixelsChanged() {
    if (m_latestEvent && !m_oldestWaiting)
        m_oldestWaiting = m_latestEvent;
    m_latestEvent.reset();
}

void PaintTimer::frameStarted(std::int64_t nowUs) {
    m_frameStart = nowUs;
}

// A frame counts from the oldest event it shows, so the figures err towards the longer wait.
void PaintTimer::frameFinished(std::int64_t nowUs) {
    if (m_frameStart && m_draws.size() < kMostFrames)
        m_draws.push_back(nowUs - *m_frameStart);
    m_frameStart.reset();
    m_frameEnd = nowUs;
    if (!m_oldestWaiting)
        return;
    if (m_waits.size() < kMostFrames)
        m_waits.push_back(nowUs - *m_oldestWaiting);
    m_oldestWaiting.reset();
}

void PaintTimer::frameSwapped(std::int64_t nowUs) {
    if (m_frameEnd && m_swaps.size() < kMostFrames)
        m_swaps.push_back(nowUs - *m_frameEnd);
    m_frameEnd.reset();
}

void PaintTimer::eventHandled(std::int64_t tookUs) {
    if (m_handles.size() < kMostFrames)
        m_handles.push_back(tookUs);
}

void PaintTimer::inkShown(std::int64_t ageUs) {
    if (m_inkAges.size() < kMostFrames)
        m_inkAges.push_back(ageUs);
}

void PaintTimer::mainThreadTick(std::int64_t nowUs) {
    // us: the main thread's stalls ending this soon after a lift, before the next press, count as the lift's (7bbe3a5): its own work
    // ends by 0.5 s on 4 cores; a 2-core 1500 px Pencil's settle runs to 1.5 s and isn't counted.
    constexpr std::int64_t kAfterLiftUs = 1'000'000;
    if (m_lastTick) {
        m_mainStallWorst = std::max(m_mainStallWorst, nowUs - *m_lastTick);
        if (m_liftedAt && nowUs - *m_liftedAt <= kAfterLiftUs)
            m_afterLiftWorst = std::max(m_afterLiftWorst, nowUs - *m_lastTick);
    }
    m_lastTick = nowUs;
}

void PaintTimer::lifted(std::int64_t nowUs) {
    m_lastLiftWorst = std::exchange(m_afterLiftWorst, 0);
    m_liftedAt = nowUs;
}

void PaintTimer::eventStamped(std::uint64_t sentMs, std::int64_t nowUs) {
    const std::int64_t stamp = nowUs - static_cast<std::int64_t>(sentMs) * 1000;
    if (!m_quickestStamp || stamp < *m_quickestStamp)
        m_quickestStamp = stamp;
    m_backlogWorst = std::max(m_backlogWorst, stamp - *m_quickestStamp);
}

void PaintTimer::paintShown(std::int64_t nowUs) {
    ++m_updates;
    if (m_lastShown)
        m_updateGapWorst = std::max(m_updateGapWorst, nowUs - *m_lastShown);
    m_lastShown = nowUs;
    if (m_liftedAt)
        m_afterLift = nowUs - *m_liftedAt;
}

PaintTimer::Summary PaintTimer::takeSummary() {
    Summary summary;
    summary.updates = std::exchange(m_updates, 0);
    summary.updateGapWorstUs = std::exchange(m_updateGapWorst, 0);
    summary.afterLiftUs = std::exchange(m_afterLift, 0);
    m_lastShown.reset();
    summary.events = m_events;
    summary.frames = m_waits.size();
    if (!m_waits.empty()) {
        std::sort(m_waits.begin(), m_waits.end());
        summary.medianUs = percentile(m_waits, 0.5);
        summary.p99Us = percentile(m_waits, 0.99);
        summary.p999Us = percentile(m_waits, 0.999);
        summary.worstUs = m_waits.back();
    }
    medianAndWorst(m_draws, summary.drawMedianUs, summary.drawWorstUs);
    medianAndWorst(m_handles, summary.handleMedianUs, summary.handleWorstUs);
    medianAndWorst(m_swaps, summary.swapMedianUs, summary.swapWorstUs);
    medianAndWorst(m_inkAges, summary.inkMedianUs, summary.inkWorstUs);
    summary.backlogWorstUs = m_backlogWorst;
    summary.mainStallWorstUs = m_mainStallWorst;
    summary.lastLiftStallWorstUs = std::exchange(m_lastLiftWorst, 0);
    m_waits.clear();
    m_draws.clear();
    m_handles.clear();
    m_swaps.clear();
    m_inkAges.clear();
    m_frameEnd.reset();
    m_quickestStamp.reset();
    m_backlogWorst = 0;
    m_mainStallWorst = 0;
    m_latestEvent.reset();
    m_oldestWaiting.reset();
    m_frameStart.reset();
    m_events = 0;
    return summary;
}

}
