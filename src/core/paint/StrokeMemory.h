#pragma once

#include "core/brush/BrushSettings.h"

#include <QSize>

#include <atomic>
#include <cstdint>
#include <optional>

// How much memory a stroke may hold, so a stroke too large for the machine is limited instead of the app being killed mid-stroke
// (the decision of 2026-11-07: warn and limit). It goes by the memory the machine or the process's cgroup has, not by what
// happens to be free, so whether a stroke is limited never depends on what other programs hold at the time.
namespace doodlebold::stroke_memory {

// The user's say (Options › Performance › Memory, the decision of 2026-11-08): an amount of their own in place of the
// machine's, and whether a brush too large for it is kept to the largest size that fits. The defaults, which the reference set
// paints with, are the machine's amount, kept within.
struct Policy {
    std::int64_t customBytes = 0;  // 0 is Automatic
    bool keepBrushesWithin = true;
};
void setPolicy(const Policy& policy);
Policy policy();

// The machine's memory. 0 if it can't say.
std::int64_t machineBytes();
// What Automatic lets the process use: the machine's memory less a share for the system and other programs, or its cgroup's limit
// if that's lower. 0 if it can't say.
std::int64_t automaticBytes();
// What the policy lets it use: its own amount or Automatic's, and a lower cgroup limit still wins, since the system would end the
// process past it.
std::int64_t allowedBytes();
// What a stroke may take the process up to: allowedBytes() less a reserve for what a stroke takes between two looks. 0 if it can't
// say, and then nothing is limited.
std::int64_t usableBytes();
// What this process holds now, in memory (not swapped out). 0 if it can't say.
std::int64_t heldBytes();
// Hands what the C library keeps of freed memory back to the system, as it would otherwise keep it for later (glibc only).
void giveBackFreed();
// The same on a thread of its own, so the caller doesn't wait.
void giveBackFreedSoon();

// In place of usableBytes(), for tests and benches: 0 is the machine's own, below 0 no limit.
inline std::atomic<std::int64_t>& usableOverride() {
    static std::atomic<std::int64_t> value{0};
    return value;
}
std::int64_t usableOrOverride();

// What a stroke with `settings` holds at its most along one crossing of a `canvas` (its diagonal), going by what a stroke holds for
// each pixel it reaches and for the dab being laid. Measured for each tool, a little under, so a stroke that fits isn't limited:
// the stroke itself is watched as it goes, against its budget.
std::int64_t estimateBytes(const BrushSettings& settings, QSize canvas);
// Of that, what the dab being laid holds while it's laid (the Blur's blocks of what's under it), given back after each dab.
std::int64_t dabBytes(const BrushSettings& settings, QSize canvas);
// The largest size, in tenths of a pixel, up to `settings.size`, whose estimate is within `budget`; at least the smallest size.
int largestSizeWithin(const BrushSettings& settings, QSize canvas, std::int64_t budget);
// The size a stroke with `settings` on `canvas` is kept to now, if brushes are kept within memory and `settings.size` doesn't fit.
// What the process holds counts memory freed and kept for later, which a stroke would take anyway, so before saying a size doesn't
// fit it has that given back, once for each stroke ended since (strokeEnded): whether a stroke fits mustn't depend on what the
// strokes before it left.
std::optional<int> sizeLimit(const BrushSettings& settings, QSize canvas);
void strokeEnded();

}
