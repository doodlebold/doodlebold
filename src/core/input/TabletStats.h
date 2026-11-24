#pragma once

#include "core/input/InputSample.h"

#include <QObject>
#include <QString>

#include <deque>

namespace doodlebold {

struct InputDeviceInfo {
    QString name;
    QString type;
    QString pointerType;
    QString capabilities;
    qint64 uniqueId = -1;
    int buttonCount = 0;
};

class TabletStats : public QObject {
    Q_OBJECT
public:
    explicit TabletStats(QObject* parent = nullptr);

    void record(const InputSample& sample, const InputDeviceInfo& device);
    void setProximity(bool inProximity, const InputDeviceInfo& device);

    const InputSample& lastSample() const { return m_last; }
    const InputDeviceInfo& lastDevice() const { return m_device; }
    bool inProximity() const { return m_inProximity; }
    std::uint64_t totalEvents() const { return m_total; }
    double peakPressure() const { return m_peakPressure; }

    double eventRateHz() const;
    double medianIntervalMs() const;
    void resetPeaks();

    static constexpr std::uint64_t kWindowMs = 1000;

signals:
    void sampleRecorded();
    void proximityChanged(bool inProximity);

private:
    InputSample m_last;
    InputDeviceInfo m_device;
    std::deque<std::uint64_t> m_times;
    std::uint64_t m_total = 0;
    double m_peakPressure = 0.0;
    bool m_inProximity = false;
};

}
