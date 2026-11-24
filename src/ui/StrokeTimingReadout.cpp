#include "ui/StrokeTimingReadout.h"

#include <QCoreApplication>

#include <algorithm>

namespace doodlebold {

StrokeTimingReadout::EventScope::EventScope(StrokeTimingReadout* readout) : m_readout(readout) {
    if (m_readout)
        m_clock.start();
}

StrokeTimingReadout::EventScope::~EventScope() {
    if (m_readout)
        m_readout->eventHandled(m_clock.nsecsElapsed() / 1000);
}

void StrokeTimingReadout::Times::add(std::int64_t us) {
    ++count;
    totalUs += us;
    worstUs = std::max(worstUs, us);
}

QString StrokeTimingReadout::Times::describe() const {
    if (count == 0)
        return QCoreApplication::translate("StrokeTimingReadout", "not yet");
    return QCoreApplication::translate("StrokeTimingReadout", "%1 ms on average, %2 ms at worst")
        .arg(double(totalUs) / double(count) / 1000.0, 0, 'f', 1)
        .arg(double(worstUs) / 1000.0, 0, 'f', 1);
}

void StrokeTimingReadout::startStroke() {
    *this = {};
}

void StrokeTimingReadout::eventHandled(std::int64_t us) {
    ++m_events;
    m_handling.add(us);
}

void StrokeTimingReadout::inkShown(std::int64_t ageUs) {
    ++m_frames;
    m_ink.add(std::max<std::int64_t>(ageUs, 0));
}

QString StrokeTimingReadout::text() const {
    return QCoreApplication::translate("StrokeTimingReadout", "Pen to screen: %1\nEach pen event: %2")
        .arg(m_ink.describe(), m_handling.describe());
}

}
