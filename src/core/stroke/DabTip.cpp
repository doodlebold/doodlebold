#include <cstdio>
#include "core/stroke/DabTip.h"
#include "core/stroke/BrushDab.h"
#include <optional>

#ifdef __GLIBC__
#include <malloc.h>
#endif

#include "core/stroke/PressureCurve.h"
#include "core/util/WorkerPool.h"

#include <algorithm>
#include <tuple>
#include <atomic>
#include <bit>
#include <cstring>
#include <emmintrin.h>
#include <immintrin.h>
// Doodlebold needs AVX2 on x86-64 (src/CMakeLists.txt builds the painting files with it); the app refuses other processors at
// start-up (platform::processorIsSupported).
#ifndef __AVX2__
#error "built without AVX2: the painting files need -mavx2 (GCC, Clang) or /arch:AVX2 (MSVC)"
#endif
#include <cstdint>
#include <cmath>
#include <cstdlib>
#include <functional>
#include <limits>
#include <numbers>
#include <utility>

// docs/brush-engine/dab-tip.md says what the named values here are for and how they were chosen.
namespace doodlebold {

// DabPainter's code, part by part in the order it was written. The parts stay one translation unit on purpose: the hot loops
// depend on inlining across all of it, which separate .cpp files would stop, and even taking unreachable code out of this file
// once moved strokes by 0.8 to 2.4 % of their instructions (tools/compare_brush_speed.py). Together they compile to the same
// object code as the single file did.
#include "core/stroke/DabTipValues.inc"
#include "core/stroke/DabTipCore.inc"
#include "core/stroke/DabTipLanes.inc"
#include "core/stroke/DabTipEvents.inc"
#include "core/stroke/DabTipBrushGroups.inc"
#include "core/stroke/DabTipRecords.inc"
#include "core/stroke/DabTipMixing.inc"
#include "core/stroke/DabTipLaying.inc"
#include "core/stroke/DabTipGrids.inc"
#include "core/stroke/DabTipStamps.inc"
#include "core/stroke/DabTipSmudge.inc"
#include "core/stroke/DabTipBlur.inc"

}
