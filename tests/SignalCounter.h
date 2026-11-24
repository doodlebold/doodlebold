#pragma once

#include <QObject>

namespace doodlebold::test {

// Counts how often a signal was emitted (a minimal QSignalSpy, which needs the Qt Test module).
class SignalCounter : public QObject {
public:
    template <typename Sender, typename Signal> SignalCounter(Sender* sender, Signal signal) {
        connect(sender, signal, this, [this] { ++m_count; });
    }
    int count() const { return m_count; }
    void reset() { m_count = 0; }

private:
    int m_count = 0;
};

}
