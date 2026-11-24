#pragma once

#include <cstdint>
#include <optional>
#include <vector>

namespace doodlebold {

// Times the app's own part of the delay: from receiving a pen event to finishing the frame that shows it. The desktop and
// the display add more after that, so this isn't the whole delay the painter feels. Times are in microseconds and come from
// the caller, so nothing here reads a clock.
class PaintTimer {
public:
    struct Summary {
        std::size_t events = 0;
        std::size_t frames = 0;
        std::int64_t medianUs = 0;
        std::int64_t p99Us = 0;
        std::int64_t p999Us = 0;
        std::int64_t worstUs = 0;
        std::int64_t drawMedianUs = 0;  // drawing a frame, on its own
        std::int64_t drawWorstUs = 0;
        std::int64_t handleMedianUs = 0;  // handling a pen event, from arriving at the canvas to done
        std::int64_t handleWorstUs = 0;
        std::int64_t swapMedianUs = 0;  // from a frame being drawn to the window system having it
        std::int64_t swapWorstUs = 0;
        std::int64_t inkMedianUs = 0;  // how old the newest pen event was, when a frame showed it
        std::int64_t inkWorstUs = 0;
        std::int64_t mainStallWorstUs = 0;  // the longest the main thread went without getting back to its event loop
        // The same, in the second after the last stroke's lift, until this stroke began: a summary is taken as its stroke ends,
        // before most of what follows its own lift has run.
        std::int64_t lastLiftStallWorstUs = 0;
        std::int64_t backlogWorstUs = 0;  // the most any event was held up on its way to the canvas, against the stroke's quickest
        std::size_t updates = 0;  // how often the canvas took new paint from the stroke
        std::int64_t updateGapWorstUs = 0;  // and the longest it went between two of those
        std::int64_t afterLiftUs = 0;  // from the pen's lift to the last of those
    };

    PaintTimer();
    void eventReceived(std::int64_t nowUs);
    // Only an event that changed something waits for a frame: holding the pen still isn't a delay.
    void pixelsChanged();
    void frameStarted(std::int64_t nowUs);
    void frameFinished(std::int64_t nowUs);
    void frameSwapped(std::int64_t nowUs);
    void eventHandled(std::int64_t tookUs);
    void inkShown(std::int64_t ageUs);
    void mainThreadTick(std::int64_t nowUs);
    // The pen lifted: the main thread's stalls in the second after it, until the next stroke's first event, go to the summary
    // taken at the next stroke's end.
    void lifted(std::int64_t nowUs);
    // `sentMs` is the system's own stamp on the event, from another clock, so only differences between events mean anything.
    void eventStamped(std::uint64_t sentMs, std::int64_t nowUs);
    // The canvas took new paint from the stroke.
    void paintShown(std::int64_t nowUs);
    // What has been timed since the last call, which then starts afresh.
    Summary takeSummary();

private:
    std::vector<std::int64_t> m_waits;  // reserved up front, so timing a stroke doesn't allocate
    std::vector<std::int64_t> m_draws;
    std::vector<std::int64_t> m_handles;
    std::vector<std::int64_t> m_swaps;
    std::vector<std::int64_t> m_inkAges;
    std::optional<std::int64_t> m_frameEnd;
    std::optional<std::int64_t> m_quickestStamp;
    std::int64_t m_backlogWorst = 0;
    std::optional<std::int64_t> m_lastShown;
    std::size_t m_updates = 0;
    std::int64_t m_updateGapWorst = 0;
    std::int64_t m_afterLift = 0;
    std::int64_t m_mainStallWorst = 0;
    std::optional<std::int64_t> m_lastTick;
    std::optional<std::int64_t> m_liftedAt;
    std::int64_t m_afterLiftWorst = 0;
    std::int64_t m_lastLiftWorst = 0;
    std::optional<std::int64_t> m_latestEvent;
    std::optional<std::int64_t> m_oldestWaiting;
    std::optional<std::int64_t> m_frameStart;
    std::size_t m_events = 0;
};

}
