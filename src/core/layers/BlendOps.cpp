#include "core/layers/BlendOps.h"

namespace doodlebold::blend {

void blendSpan(BlendMode, const float* src, float opacity, float* dst, int n) {
    for (int i = 0; i < n; ++i, src += 4, dst += 4) {
        const float through = 1.0f - src[3] * opacity;
        for (int c = 0; c < 4; ++c)
            dst[c] = src[c] * opacity + dst[c] * through;
    }
}

}
