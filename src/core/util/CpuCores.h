#pragma once

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <sstream>
#include <string>

namespace doodlebold::cpu_cores {

// A CPU quota as cores, rounded up, so half a core's quota still gets a thread. 0 if there's none.
inline int quotaAsCores(double quota, double period) {
    if (quota <= 0.0 || period <= 0.0)
        return 0;
    return std::max(1, static_cast<int>(std::ceil(quota / period)));
}

// A cgroup v2 "cpu.max" line: "max 100000" for no quota, "200000 100000" for two cores. 0 if there's none.
inline int coresFromCpuMax(const std::string& line) {
    std::istringstream in(line);
    std::string quota;
    double period = 0.0;
    if (!(in >> quota >> period) || quota == "max")
        return 0;
    return quotaAsCores(std::strtod(quota.c_str(), nullptr), period);
}

// The machine's cores, narrowed by the process's affinity (which taskset, a container or a sandbox can set) and, on Linux, by its
// cgroup's CPU quota. In WorkerPool.cpp, with the threads.
int countAvailable();

// The cores this process can use, worked out once.
inline int available() {
    static const int cores = countAvailable();
    return cores;
}

inline std::atomic<int>& limit() {
    static std::atomic<int> value{0};
    return value;
}

inline int used() {
    const int n = limit().load();
    return n <= 0 ? available() : std::clamp(n, 1, available());
}

inline void setLimit(int cores) {
    limit().store(cores);
}

}
