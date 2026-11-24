#pragma once

#include "core/util/CpuCores.h"

#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

namespace doodlebold {

// Fixed worker threads for jobs too frequent for parallelFor's per-call threads (a frame of a stroke). Not reentrant: one run() at a
// time.
class WorkerPool {
public:
    enum class Priority {
        Normal,
        GivesWay
    };

    explicit WorkerPool(int threads = cpu_cores::used(), Priority priority = Priority::Normal);
    ~WorkerPool();
    WorkerPool(const WorkerPool&) = delete;
    WorkerPool& operator=(const WorkerPool&) = delete;

    int size() const { return int(m_threads.size()) + 1; }
    // Callable from inside a job or from other threads: only one job uses the pool, the others run on their caller.
    void run(int n, const std::function<void(int)>& job);

    static WorkerPool& shared();
    // For painting a stroke, which can run at the same time as a frame is being composited on shared(). It paints on every core, as the
    // reference does, its workers giving way to the interface.
    static WorkerPool& forStrokes();

private:
    void work();
    void drain();

    std::vector<std::thread> m_threads;
    std::mutex m_mutex;
    std::condition_variable m_wake, m_done;
    const std::function<void(int)>* m_job = nullptr;
    int m_count = 0;
    std::atomic<int> m_next{0};
    int m_busy = 0;
    unsigned m_generation = 0;
    std::atomic<bool> m_running{false};  // a job is being run: further calls run inline instead of replacing it
    bool m_stop = false;
};

}
