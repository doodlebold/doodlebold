#include "core/input/TabletStats.h"

#include <gtest/gtest.h>

using namespace doodlebold;

TEST(TabletStats, RateAndMedianIntervalOverWindow) {
    TabletStats stats;
    InputDeviceInfo dev;
    dev.name = QStringLiteral("Test Pen");
    InputSample s;
    for (int i = 0; i <= 200; ++i) {
        s.timestampMs = 10'000 + std::uint64_t(i) * 5;
        s.pressure = i / 200.0;
        stats.record(s, dev);
    }
    EXPECT_NEAR(stats.eventRateHz(), 200.0, 1.0);
    EXPECT_DOUBLE_EQ(stats.medianIntervalMs(), 5.0);
    EXPECT_DOUBLE_EQ(stats.peakPressure(), 1.0);
    EXPECT_EQ(stats.totalEvents(), 201u);
    EXPECT_EQ(stats.lastDevice().name, QStringLiteral("Test Pen"));
}

TEST(TabletStats, WindowDropsOldEvents) {
    TabletStats stats;
    InputSample s;
    s.timestampMs = 0;
    stats.record(s, {});
    s.timestampMs = 5000;
    stats.record(s, {});
    s.timestampMs = 5008;
    stats.record(s, {});
    EXPECT_NEAR(stats.eventRateHz(), 125.0, 0.01);
}

TEST(TabletStats, ProximitySignalOnlyOnChange) {
    TabletStats stats;
    int changes = 0;
    QObject::connect(&stats, &TabletStats::proximityChanged, [&](bool) { ++changes; });
    stats.setProximity(true, {});
    stats.setProximity(true, {});
    stats.setProximity(false, {});
    EXPECT_EQ(changes, 2);
    EXPECT_FALSE(stats.inProximity());
}
