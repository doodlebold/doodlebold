#include "core/tools/SizeList.h"

#include <algorithm>
#include <iterator>

namespace doodlebold {

namespace {

// TODO(reimplement): choose the steps the brush size keys should move through.
constexpr int kSizes[] = {10, 20, 40, 80, 160, 320, 640, 1280, 2560, 5120};  // tenths of a pixel

}

int steppedSize(int size, int direction) {
    if (direction > 0) {
        const auto* const it = std::ranges::upper_bound(kSizes, size);
        return it != std::end(kSizes) ? *it : size;
    }
    if (direction < 0) {
        const auto* const it = std::ranges::lower_bound(kSizes, size);
        return it != std::begin(kSizes) ? *std::prev(it) : size;
    }
    return size;
}

}
