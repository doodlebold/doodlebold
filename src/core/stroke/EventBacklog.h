#pragma once

#include <cstdint>

namespace doodlebold {

// Works out whether handling pen events keeps up with them arriving. Events arrive at set times and are handled one at a time,
// so an event that takes too long makes the next one wait. Times are in microseconds and come from the caller, so nothing here
// reads a clock.
class EventBacklog {
public:
    void eventHandled(std::int64_t arrivedUs, std::int64_t tookUs);
    std::int64_t worstWaitUs() const { return m_worstWaitUs; }
    // How long the last event had to wait before it could start.
    std::int64_t waitNowUs() const { return m_waitNowUs; }
    // True if the last event waited more than `oneEventUs`: work is piling up instead of catching up between events.
    bool fellBehind(std::int64_t oneEventUs) const { return m_waitNowUs > oneEventUs; }

private:
    std::int64_t m_freeAtUs = 0;
    std::int64_t m_waitNowUs = 0;
    std::int64_t m_worstWaitUs = 0;
    bool m_started = false;
};

}
