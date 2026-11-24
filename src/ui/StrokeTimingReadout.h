#pragma once

#include <QElapsedTimer>
#include <QString>

#include <cstdint>

namespace doodlebold {

// Options > Performance > Show stroke timing: how long the current or last stroke's pen events took, timed where
// DOODLEBOLD_TIMING_LOG times them. Handling a pen event on the main thread, and how old the newest pen event was when a frame
// showed its paint, which takes in the stroke's own thread. Lag that comes from the brush shows in both.
class StrokeTimingReadout {
public:
    // Times one pen event while it's alive; does nothing for a null readout.
    class EventScope {
    public:
        explicit EventScope(StrokeTimingReadout* readout);
        ~EventScope();
        EventScope(const EventScope&) = delete;
        EventScope& operator=(const EventScope&) = delete;

    private:
        StrokeTimingReadout* m_readout;
        QElapsedTimer m_clock;
    };

    void startStroke();
    void eventHandled(std::int64_t us);
    void inkShown(std::int64_t ageUs);
    bool isEmpty() const { return m_events == 0 && m_frames == 0; }
    // Two short lines, in milliseconds.
    QString text() const;

private:
    struct Times {
        std::int64_t count = 0;
        std::int64_t totalUs = 0;
        std::int64_t worstUs = 0;
        void add(std::int64_t us);
        QString describe() const;
    };
    Times m_handling;
    Times m_ink;
    std::int64_t m_events = 0;
    std::int64_t m_frames = 0;
};

}
