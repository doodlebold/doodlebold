#pragma once

#include <QSize>

#include <atomic>
#include <cstdint>

// What a file Doodlebold opens may hold, the same for every format, checked before anything is allocated for it. Each writer
// refuses what its reader would refuse, so nothing is saved that won't open again. A format may keep a stricter limit of its own
// (PSD's layers, for Photoshop).
namespace doodlebold {

// The largest canvas side Doodlebold opens or creates, the baseline. A canvas also costs a full-size display
// image (4 bytes per pixel).
inline constexpr int kMaxCanvasSide = 16384;

namespace file_limits {

// Layers and folders in a file, the canvas's and every animation frame's together: far past what a painting uses (Clip Studio
// Paint allows 10,000, Photoshop about 8,000), and a bound on the file's layer list, which is read before any pixel.
inline constexpr int kMaxLayers = 20000;

// The largest layer, in pixels: a layer as large as the largest canvas.
inline constexpr std::int64_t kMaxLayerPixels = std::int64_t{kMaxCanvasSide} * kMaxCanvasSide;

// A decoded pixel: 16 bits each of red, green, blue and alpha.
inline constexpr std::int64_t kPixelBytes = 8;

// What a file's pictures may take once decoded, in bytes: what this process may use (stroke_memory::allowedBytes), as a file past
// it couldn't open anyway and would take the machine's memory first. 0 if it can't say, and then only the limits above hold.
std::int64_t decodeBudgetBytes();

// In place of decodeBudgetBytes(), for tests: 0 is the machine's own.
inline std::atomic<std::int64_t>& decodeBudgetOverride() {
    static std::atomic<std::int64_t> value{0};
    return value;
}

// The decoded bytes of one file as it is read, taken before each is allocated.
class DecodeBudget {
public:
    DecodeBudget() : m_left(decodeBudgetBytes()) {}
    // False once the file's pictures would take more than the budget; nothing is taken then.
    bool take(std::int64_t bytes) {
        if (m_unlimited)
            return true;
        if (bytes > m_left)
            return false;
        m_left -= bytes;
        return true;
    }

private:
    std::int64_t m_left;
    bool m_unlimited = m_left <= 0;
};

}

}
