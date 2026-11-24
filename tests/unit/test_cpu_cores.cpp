#include "core/util/CpuCores.h"
#include "core/util/ParallelFor.h"
#include "core/util/WorkerPool.h"

#include <gtest/gtest.h>

#include <mutex>
#ifdef __linux__
#include <sched.h>
#endif
#include <set>
#include <thread>

using namespace doodlebold;

namespace {

struct LimitGuard {
    ~LimitGuard() { cpu_cores::setLimit(0); }
};

std::size_t threadsUsed() {
    std::mutex mutex;
    std::set<std::thread::id> ids;
    parallelFor(
        2000,
        [&](int) {
            std::this_thread::yield();
            std::scoped_lock lock(mutex);
            ids.insert(std::this_thread::get_id());
        },
        1);
    return ids.size();
}

}

TEST(CpuCores, NoLimitMeansEveryCore) {
    LimitGuard guard;
    cpu_cores::setLimit(0);
    EXPECT_EQ(cpu_cores::used(), cpu_cores::available());
    EXPECT_GE(cpu_cores::available(), 1);
}

TEST(CpuCores, AvailableNeverExceedsTheMachinesCores) {
    EXPECT_LE(cpu_cores::available(), static_cast<int>(std::max(1u, std::thread::hardware_concurrency())));
}

#ifdef __linux__
// taskset, a container or a sandbox narrows the affinity: the app should start only as many threads as it may run on.
TEST(CpuCores, AvailableFollowsTheAffinityMask) {
    cpu_set_t set;
    CPU_ZERO(&set);
    ASSERT_EQ(sched_getaffinity(0, sizeof(set), &set), 0);
    EXPECT_LE(cpu_cores::available(), CPU_COUNT(&set));
}
#endif

TEST(CpuCores, ACgroupQuotaCountsAsCoresRoundedUp) {
    EXPECT_EQ(cpu_cores::coresFromCpuMax("max 100000"), 0);
    EXPECT_EQ(cpu_cores::coresFromCpuMax("200000 100000"), 2);
    EXPECT_EQ(cpu_cores::coresFromCpuMax("150000 100000"), 2);
    EXPECT_EQ(cpu_cores::coresFromCpuMax("50000 100000"), 1);
    EXPECT_EQ(cpu_cores::coresFromCpuMax(""), 0);
    EXPECT_EQ(cpu_cores::quotaAsCores(-1, 100000), 0);  // cgroup v1's "no quota"
    EXPECT_EQ(cpu_cores::quotaAsCores(400000, 100000), 4);
}

TEST(CpuCores, LimitIsClampedIntoTheAvailableCores) {
    LimitGuard guard;
    cpu_cores::setLimit(1);
    EXPECT_EQ(cpu_cores::used(), 1);
    cpu_cores::setLimit(1000000);
    EXPECT_EQ(cpu_cores::used(), cpu_cores::available());
    cpu_cores::setLimit(-3);
    EXPECT_EQ(cpu_cores::used(), cpu_cores::available());
}

TEST(CpuCores, ParallelForRunsOnASingleThreadWhenLimitedToOne) {
    LimitGuard guard;
    cpu_cores::setLimit(1);
    EXPECT_EQ(threadsUsed(), 1u);
}

TEST(CpuCores, ParallelForNeverUsesMoreThreadsThanTheLimit) {
    LimitGuard guard;
    cpu_cores::setLimit(2);
    EXPECT_LE(threadsUsed(), 2u);
}

TEST(CpuCores, WorkerPoolSizeFollowsTheLimit) {
    LimitGuard guard;
    cpu_cores::setLimit(1);
    EXPECT_EQ(WorkerPool().size(), 1);
    cpu_cores::setLimit(0);
    EXPECT_EQ(WorkerPool().size(), cpu_cores::available());
}

TEST(WorkerPool, NestedAndConcurrentRunsComplete) {
    WorkerPool pool(4);
    std::atomic<int> total{0};
    pool.run(8, [&](int) { pool.run(5, [&](int) { ++total; }); });
    EXPECT_EQ(total.load(), 40);
    total = 0;
    std::thread other([&] { pool.run(100, [&](int) { ++total; }); });
    pool.run(100, [&](int) { ++total; });
    other.join();
    EXPECT_EQ(total.load(), 200);
}
