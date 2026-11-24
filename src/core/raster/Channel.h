#pragma once

#include <algorithm>
#include <array>
#include <cstdint>

namespace doodlebold {

// A layer pixel is premultiplied RGBA in 16-bit words: 0 … 65535, where 65535 is 1.0. A colour channel never exceeds its alpha.
using Channel = std::uint16_t;
inline constexpr int kChannelOne = 65535;
inline constexpr float kChannelToFloat = 1.0f / kChannelOne;

constexpr Channel fromUnorm8(int c) {
    return static_cast<Channel>(std::clamp(c, 0, 255) * 257);
}

constexpr std::uint8_t toUnorm8(int v) {
    return static_cast<std::uint8_t>((std::clamp(v, 0, kChannelOne) * 255 + kChannelOne / 2) / kChannelOne);
}

constexpr std::array<std::uint8_t, 4> toUnorm8(const std::array<Channel, 4>& p) {
    return {toUnorm8(p[0]), toUnorm8(p[1]), toUnorm8(p[2]), toUnorm8(p[3])};
}

inline Channel fromUnit(float v) {
    return static_cast<Channel>(std::clamp(v, 0.0f, 1.0f) * kChannelOne + 0.5f);
}

inline Channel fromUnit(double v) {
    return static_cast<Channel>(std::clamp(v, 0.0, 1.0) * kChannelOne + 0.5);
}

// a · b, where kChannelOne is 1.0, rounded.
constexpr int mulChannel(int a, int b) {
    return static_cast<int>((static_cast<std::int64_t>(a) * b + kChannelOne / 2) / kChannelOne);
}

// From dst towards target by weight (kChannelOne: all the way), rounded.
constexpr int lerpChannel(int dst, int target, int weight) {
    const std::int64_t product = static_cast<std::int64_t>(target - dst) * weight;
    return dst + static_cast<int>((product + (product >= 0 ? kChannelOne / 2 : -kChannelOne / 2)) / kChannelOne);
}

}
