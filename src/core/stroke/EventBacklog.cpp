#include "core/stroke/EventBacklog.h"

#include <algorithm>

namespace doodlebold {

void EventBacklog::eventHandled(std::int64_t arrivedUs, std::int64_t tookUs) {
    const std::int64_t startsUs = m_started ? std::max(arrivedUs, m_freeAtUs) : arrivedUs;
    m_started = true;
    m_waitNowUs = startsUs - arrivedUs;
    m_worstWaitUs = std::max(m_worstWaitUs, m_waitNowUs);
    m_freeAtUs = startsUs + tookUs;
}

}
