#pragma once

#include "core/util/CpuCores.h"

#include <algorithm>
#include <atomic>
#include <thread>
#include <vector>

namespace doodlebold {

// Hands out one index at a time, so uneven items balance themselves; small jobs run inline.
template <typename F> void parallelFor(int n, F&& f, int minParallel = 8) {
    const int threads = std::min<int>(n, cpu_cores::used());
    if (n < minParallel || threads <= 1) {
        for (int i = 0; i < n; ++i)
            f(i);
        return;
    }
    std::atomic<int> next{0};
    auto worker = [&] {
        for (int i = next++; i < n; i = next++)
            f(i);
    };
    std::vector<std::thread> pool;
    pool.reserve(static_cast<std::size_t>(threads - 1));
    for (int t = 1; t < threads; ++t)
        pool.emplace_back(worker);
    worker();
    for (std::thread& t : pool)
        t.join();
}

}
