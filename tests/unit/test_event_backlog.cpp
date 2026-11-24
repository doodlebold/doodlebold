#include "core/stroke/EventBacklog.h"

#include <gtest/gtest.h>

using namespace doodlebold;

namespace {

constexpr std::int64_t kOneEventUs = 8000;

EventBacklog handled(int events, std::int64_t tookUs) {
    EventBacklog backlog;
    for (int i = 0; i < events; ++i)
        backlog.eventHandled(i * kOneEventUs, tookUs);
    return backlog;
}

}

TEST(EventBacklog, EventsHandledInTimeNeverWait) {
    const EventBacklog backlog = handled(100, 5000);
    EXPECT_EQ(backlog.worstWaitUs(), 0);
    EXPECT_FALSE(backlog.fellBehind(kOneEventUs));
}

TEST(EventBacklog, EventsTakingTooLongPileUp) {
    const EventBacklog backlog = handled(100, 10000);
    EXPECT_EQ(backlog.waitNowUs(), 99 * 2000);
    EXPECT_TRUE(backlog.fellBehind(kOneEventUs));
}

TEST(EventBacklog, OneSlowEventIsCaughtUpWith) {
    EventBacklog backlog;
    backlog.eventHandled(0, 12000);
    backlog.eventHandled(kOneEventUs, 1000);
    EXPECT_EQ(backlog.waitNowUs(), 4000);
    backlog.eventHandled(2 * kOneEventUs, 1000);
    EXPECT_EQ(backlog.waitNowUs(), 0);
    EXPECT_EQ(backlog.worstWaitUs(), 4000);
    EXPECT_FALSE(backlog.fellBehind(kOneEventUs));
}
