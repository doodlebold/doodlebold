#include "core/input/TabletStats.h"

#include <algorithm>
#include <vector>

namespace doodlebold {

TabletStats::TabletStats(QObject* parent) : QObject(parent) {}

void TabletStats::record(const InputSample& sample, const InputDeviceInfo& device) {
    m_last = sample;
    m_device = device;
    ++m_total;
    m_peakPressure = std::max(m_peakPressure, sample.pressure);
    m_times.push_back(sample.timestampMs);
    while (!m_times.empty() && sample.timestampMs - m_times.front() > kWindowMs)
        m_times.pop_front();
    emit sampleRecorded();
}

void TabletStats::setProximity(bool inProximity, const InputDeviceInfo& device) {
    m_device = device;
    if (m_inProximity == inProximity)
        return;
    m_inProximity = inProximity;
    emit proximityChanged(inProximity);
}

double TabletStats::eventRateHz() const {
    if (m_times.size() < 2)
        return 0.0;
    const auto span = static_cast<double>(m_times.back() - m_times.front());
    if (span <= 0.0)
        return 0.0;
    return static_cast<double>(m_times.size() - 1) * 1000.0 / span;
}

double TabletStats::medianIntervalMs() const {
    if (m_times.size() < 2)
        return 0.0;
    std::vector<std::uint64_t> d;
    d.reserve(m_times.size() - 1);
    for (std::size_t i = 1; i < m_times.size(); ++i)
        d.push_back(m_times[i] - m_times[i - 1]);
    std::nth_element(d.begin(), d.begin() + static_cast<std::ptrdiff_t>(d.size() / 2), d.end());
    return static_cast<double>(d[d.size() / 2]);
}

void TabletStats::resetPeaks() {
    m_peakPressure = 0.0;
}

}
