#include "core/paint/StrokeMemory.h"

#include "core/brush/BrushResources.h"

#include "core/raster/TiledImage.h"
#include "core/stroke/RebuiltStroke.h"
#include "core/util/ThreadPriority.h"

#include <algorithm>
#include <cstdlib>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <string>
#include <system_error>
#include <thread>

#ifdef __linux__
#include <fcntl.h>
#include <unistd.h>
#endif
#ifdef __GLIBC__
#include <malloc.h>
#endif
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <psapi.h>
#endif

namespace doodlebold::stroke_memory {

namespace {

constexpr std::int64_t kMiB = 1024 * 1024;
constexpr std::int64_t kGiB = 1024 * kMiB;

// What a stroke holds for each tile it reaches, and for each pixel of the block round the dab being laid (twice its size across),
// by how it's painted. Measured with doodlebold_big_canvas_bench and set a little over what was measured: a stroke
// given a size it can't hold is stopped partway, which is worse for the painter than a smaller size.
struct Footprint {
    double perTile;
    double perDabPixel;
};

// The mixing tools keep a record of every pixel they reach (DabPainter's Reach, besides their own copy of the
// tile). Measured at the stroke's peak over paint, where the record is largest (what's empty is shared), on the recorded C stroke
// across a 16000 px canvas at 1500 to 4000 px, and counted against the tiles of one straight crossing, which that C reaches 1.24 to
// 1.3 times (the Smudge and the Blur, with what's under the dab, 1.6 to 1.75): the Brush 0.18 to 0.19 MiB a tile, Water Color and the
// Marker 0.24 to 0.25, the Blur 0.29 to 0.30 besides its blocks of what's under the dab, the Smudge 0.34 to 0.36 (more than the 0.34
// it's given, which is left as it was). The tools that lay stamps keep a density and a shape per pixel: 0.065 MiB a tile. A tool
// that paints no strokes is counted at 0.075.
Footprint toolFootprintOf(const BrushSettings& settings) {
    if (!RebuiltStroke::canPaint(settings))
        return {0.075 * kMiB, 0.0};
    switch (settings.tool) {
    case ToolType::Brush: return {0.2 * kMiB, 0.0};
    case ToolType::WaterColor:
    case ToolType::Marker: return {0.265 * kMiB, 0.0};
    case ToolType::Smudge: return {0.34 * kMiB, 0.0};
    case ToolType::Blur: return {0.32 * kMiB, 18.0};
    default: return {0.065 * kMiB, 0.0};
    }
}

// A texture's grain is kept for each tile the stroke reaches, a float a pixel (PaperGrain::keptSharesOf): 64 × 64 × 4 bytes.
constexpr double kGrainPerTile = Tile::kSize * Tile::kSize * sizeof(float);

// With a texture, its kept grain too. The Pixel Art Pen reads none, and `Layer paper` has no picture of its own.
Footprint footprintOf(const BrushSettings& settings) {
    Footprint footprint = toolFootprintOf(settings);
    const bool grained = settings.tool != ToolType::PixelArtPen && settings.texture != BrushResourceLibrary::kNoTexture &&
                         settings.texture != BrushResourceLibrary::kLayerPaper;
    if (grained)
        footprint.perTile += settings.tool == ToolType::Brush ? 2 * kGrainPerTile : kGrainPerTile;  // the Brush keeps its whole coat too
    return footprint;
}

std::string firstLineOf(const std::string& path) {
    std::ifstream file(path);
    std::string line;
    std::getline(file, line);
    return line;
}

#ifdef __linux__
std::int64_t totalFromMeminfo() {
    std::ifstream meminfo("/proc/meminfo");
    for (std::string key; meminfo >> key;) {
        std::int64_t kB = 0;
        meminfo >> kB;
        if (key == "MemTotal:")
            return kB * 1024;
        meminfo.ignore(64, '\n');
    }
    return 0;
}

// The least memory limit from the process's own cgroup up to the root (cgroup v2), or cgroup v1's. 0 if there's none.
std::int64_t cgroupLimit() {
    std::ifstream cgroups("/proc/self/cgroup");
    std::string group;
    for (std::string line; std::getline(cgroups, line);)
        if (line.starts_with("0::"))
            group = line.substr(3);
    if (group.empty()) {
        const std::string v1 = firstLineOf("/sys/fs/cgroup/memory/memory.limit_in_bytes");
        return v1.empty() ? 0 : std::strtoll(v1.c_str(), nullptr, 10);
    }
    std::int64_t least = 0;
    for (;;) {
        const std::string max = firstLineOf("/sys/fs/cgroup" + group + "/memory.max");
        if (!max.empty() && max != "max") {
            const std::int64_t limit = std::strtoll(max.c_str(), nullptr, 10);
            least = least > 0 ? std::min(least, limit) : limit;
        }
        if (group.empty() || group == "/")
            return least;
        group.erase(group.find_last_of('/'));
    }
}
#endif

std::int64_t machineTotal() {
#ifdef __linux__
    return totalFromMeminfo();
#elif defined(_WIN32)
    MEMORYSTATUSEX status{};
    status.dwLength = sizeof(status);
    return GlobalMemoryStatusEx(&status) ? static_cast<std::int64_t>(status.ullTotalPhys) : 0;
#else
    return 0;
#endif
}

// A cgroup's limit, where it's lower than `bytes` (or `bytes` is 0, unknown).
std::int64_t withinCgroup(std::int64_t bytes) {
#ifdef __linux__
    if (const std::int64_t limit = cgroupLimit(); limit > 0)
        return bytes > 0 ? std::min(bytes, limit) : limit;
#endif
    return bytes;
}

std::atomic<std::int64_t>& customBytes() {
    static std::atomic<std::int64_t> bytes{0};
    return bytes;
}

std::atomic<bool>& keepBrushesWithin() {
    static std::atomic<bool> keep{true};
    return keep;
}

// A stroke ended since memory was last given back, so there may be some to give.
std::atomic<bool>& freedSinceGivenBack() {
    static std::atomic<bool> freed{true};
    return freed;
}

// The tiles a stroke `diameter` across reaches going once across `canvas`, corner to corner, within the area it can paint.
double tilesOfCrossing(double diameter, QSize canvas) {
    const double across = diameter + Tile::kSize;
    const double along = std::hypot(canvas.width(), canvas.height()) + diameter;
    const double paintable = double(canvas.width() + 2 * TiledImage::kOffCanvas) * (canvas.height() + 2 * TiledImage::kOffCanvas);
    return std::min(across * along, paintable) / (Tile::kSize * Tile::kSize);
}

// The pixels of the block round one dab, within the area it can paint.
double dabBlockPixels(double diameter, QSize canvas) {
    const double side = 2.0 * diameter;
    const double width = std::min(side, double(canvas.width() + 2 * TiledImage::kOffCanvas));
    const double height = std::min(side, double(canvas.height() + 2 * TiledImage::kOffCanvas));
    return width * height;
}

}

// A share is left for the system and other programs, and a cgroup's limit (whose page cache can be reclaimed) is counted whole. Then
// a sixteenth is kept back: the stroke is looked at between lots of samples on its own thread (between pen events when it's painted
// on the caller's), and a wide stroke can take a few hundred MB in one (a 2973 px Marker on a 16384 px canvas went 238 MB past the
// line before it stopped).
std::int64_t usableBytes() {
    const std::int64_t allowed = allowedBytes();
    return std::max<std::int64_t>(allowed - std::max(256 * kMiB, allowed / 16), 0);
}

void setPolicy(const Policy& policy) {
    customBytes() = std::max<std::int64_t>(policy.customBytes, 0);
    keepBrushesWithin() = policy.keepBrushesWithin;
}

Policy policy() {
    return {customBytes().load(), keepBrushesWithin().load()};
}

std::int64_t machineBytes() {
    return machineTotal();
}

std::int64_t automaticBytes() {
    const std::int64_t total = machineTotal();
    return withinCgroup(total > 0 ? total - std::max(kGiB, total / 8) : 0);
}

std::int64_t allowedBytes() {
    const std::int64_t custom = customBytes().load();
    return custom > 0 ? withinCgroup(custom) : automaticBytes();
}

std::int64_t usableOrOverride() {
    const std::int64_t value = usableOverride().load();
    if (value < 0)
        return 0;
    return value > 0 ? value : usableBytes();
}

// It's asked at every pen event, so the file stays open: reading it again from the start is a microsecond.
std::int64_t heldBytes() {
#ifdef __linux__
    static const int statm = open("/proc/self/statm", O_RDONLY | O_CLOEXEC);
    char text[128];
    const ssize_t length = statm >= 0 ? pread(statm, text, sizeof(text) - 1, 0) : -1;
    long long size = 0, resident = 0;
    if (length > 0) {
        text[length] = '\0';
        // NOLINTNEXTLINE(bugprone-unchecked-string-to-number-conversion): the count it returns is checked
        if (std::sscanf(text, "%lld %lld", &size, &resident) == 2)
            return resident * sysconf(_SC_PAGESIZE);
    }
#elif defined(_WIN32)
    PROCESS_MEMORY_COUNTERS counters{};
    if (K32GetProcessMemoryInfo(GetCurrentProcess(), &counters, sizeof(counters)))
        return static_cast<std::int64_t>(counters.WorkingSetSize);
#endif
    return 0;
}

void giveBackFreed() {
#ifdef __GLIBC__
    malloc_trim(0);
#endif
}

// Giving back 1.9 GB took 120 ms, which the lift shouldn't wait for, so it's done on a thread of its own, at a low priority.
void giveBackFreedSoon() {
#ifdef __GLIBC__
    try {
        std::thread([] {
            lowerThisThreadsPriority();
            malloc_trim(0);
        }).detach();
    } catch (const std::system_error&) {
        // No thread to spare: the memory stays kept, as it would have anyway.
    }
#endif
}

std::int64_t estimateBytes(const BrushSettings& settings, QSize canvas) {
    const double tileBytes = footprintOf(settings).perTile * tilesOfCrossing(settings.size / 10.0, canvas);
    return static_cast<std::int64_t>(tileBytes) + dabBytes(settings, canvas);
}

std::int64_t dabBytes(const BrushSettings& settings, QSize canvas) {
    return static_cast<std::int64_t>(footprintOf(settings).perDabPixel * dabBlockPixels(settings.size / 10.0, canvas));
}

std::optional<int> sizeLimit(const BrushSettings& settings, QSize canvas) {
    const std::int64_t usable = usableOrOverride();
    if (usable <= 0 || !keepBrushesWithin().load())
        return std::nullopt;
    int size = largestSizeWithin(settings, canvas, usable - heldBytes());
    if (size < settings.size && freedSinceGivenBack().exchange(false)) {
        giveBackFreed();
        size = largestSizeWithin(settings, canvas, usable - heldBytes());
    }
    return size < settings.size ? std::optional(size) : std::nullopt;
}

void strokeEnded() {
    freedSinceGivenBack() = true;
}

int largestSizeWithin(const BrushSettings& settings, QSize canvas, std::int64_t budget) {
    BrushSettings trial = settings;
    if (estimateBytes(trial, canvas) <= budget)
        return settings.size;
    // The estimate grows with the size, so the largest that fits is found by halving the range.
    int fits = 1, tooBig = settings.size;
    while (tooBig - fits > 1) {
        trial.size = fits + (tooBig - fits) / 2;
        (estimateBytes(trial, canvas) <= budget ? fits : tooBig) = trial.size;
    }
    return fits;
}

}
