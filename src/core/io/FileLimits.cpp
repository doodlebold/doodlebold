#include "core/io/FileLimits.h"

#include "core/paint/StrokeMemory.h"

namespace doodlebold::file_limits {

std::int64_t decodeBudgetBytes() {
    if (const std::int64_t own = decodeBudgetOverride().load(); own != 0)
        return own;
    return stroke_memory::allowedBytes();
}

}
