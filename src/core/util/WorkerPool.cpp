#include "core/util/WorkerPool.h"

#include "core/util/ThreadPriority.h"

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <thread>

#ifdef __linux__
#include <sched.h>
#elif defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

namespace doodlebold {

namespace {

// How many cores the affinity mask allows; 0 if it can't say.
int coresInAffinity() {
#ifdef __linux__
    cpu_set_t set;
    CPU_ZERO(&set);
    if (sched_getaffinity(0, sizeof(set), &set) == 0)
        return CPU_COUNT(&set);
#elif defined(_WIN32)
    DWORD_PTR process = 0, system = 0;
    if (GetProcessAffinityMask(GetCurrentProcess(), &process, &system) && process != 0) {
        int count = 0;
        for (; process != 0; process &= process - 1)
            ++count;
        return count;
    }
#endif
    return 0;
}

std::string firstLineOf(const std::string& path) {
    std::ifstream file(path);
    std::string line;
    std::getline(file, line);
    return line;
}

// The process's cgroup v2 path, from "0::/user.slice/..." in /proc/self/cgroup; empty if it isn't in one.
std::string ownCgroup() {
    std::ifstream cgroups("/proc/self/cgroup");
    for (std::string line; std::getline(cgroups, line);)
        if (line.starts_with("0::"))
            return line.substr(3);
    return {};
}

// The least CPU quota from the process's own cgroup up to the root, as a parent's quota holds for its children too; cgroup v1's
// if there's no v2. 0 if there's none.
int coresInQuota() {
#ifdef __linux__
    if (std::string group = ownCgroup(); !group.empty()) {
        int least = 0;
        for (;;) {
            if (const int cores = cpu_cores::coresFromCpuMax(firstLineOf("/sys/fs/cgroup" + group + "/cpu.max")); cores > 0)
                least = least > 0 ? std::min(least, cores) : cores;
            if (group.empty() || group == "/")
                return least;
            group.erase(group.find_last_of('/'));
        }
    }
    const std::string quota = firstLineOf("/sys/fs/cgroup/cpu/cpu.cfs_quota_us");
    const std::string period = firstLineOf("/sys/fs/cgroup/cpu/cpu.cfs_period_us");
    if (!quota.empty() && !period.empty())
        return cpu_cores::quotaAsCores(std::strtod(quota.c_str(), nullptr), std::strtod(period.c_str(), nullptr));
#endif
    return 0;
}

}

int cpu_cores::countAvailable() {
    int cores = static_cast<int>(std::max(1u, std::thread::hardware_concurrency()));
    if (const int affinity = coresInAffinity(); affinity > 0)
        cores = std::min(cores, affinity);
    if (const int quota = coresInQuota(); quota > 0)
        cores = std::min(cores, quota);
    return cores;
}

WorkerPool::WorkerPool(int threads, Priority priority) {
    for (int i = 1; i < threads; ++i) {
        m_threads.emplace_back([this, priority] {
            if (priority == Priority::GivesWay)
                lowerThisThreadsPriority();
            work();
        });
    }
}

WorkerPool::~WorkerPool() {
    {
        std::scoped_lock lock(m_mutex);
        m_stop = true;
    }
    m_wake.notify_all();
    for (std::thread& t : m_threads)
        t.join();
}

WorkerPool& WorkerPool::shared() {
    static WorkerPool pool;
    return pool;
}

WorkerPool& WorkerPool::forStrokes() {
    static WorkerPool pool(std::max(1, cpu_cores::used()), Priority::GivesWay);
    return pool;
}

void WorkerPool::drain() {
    for (int i = m_next++; i < m_count; i = m_next++)
        (*m_job)(i);
}

void WorkerPool::run(int n, const std::function<void(int)>& job) {
    if (n <= 0)
        return;
    // One job at a time: a call made while a job runs (from inside it, or from another thread) runs on its caller.
    bool idle = false;
    if (n == 1 || m_threads.empty() || !m_running.compare_exchange_strong(idle, true)) {
        for (int i = 0; i < n; ++i)
            job(i);
        return;
    }
    {
        std::scoped_lock lock(m_mutex);
        m_job = &job;
        m_count = n;
        m_next = 0;
        m_busy = int(m_threads.size());
        ++m_generation;
    }
    m_wake.notify_all();
    drain();
    std::unique_lock lock(m_mutex);
    m_done.wait(lock, [this] { return m_busy == 0; });
    m_job = nullptr;
    m_running = false;
}

void WorkerPool::work() {
    unsigned seen = 0;
    for (;;) {
        {
            std::unique_lock lock(m_mutex);
            m_wake.wait(lock, [&] { return m_stop || m_generation != seen; });
            if (m_stop)
                return;
            seen = m_generation;
        }
        drain();
        std::scoped_lock lock(m_mutex);
        if (--m_busy == 0)
            m_done.notify_one();
    }
}

}
