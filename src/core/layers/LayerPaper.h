#pragma once

#include <QString>

#include <algorithm>

namespace doodlebold {

// Layer Effect > Paper. The settings a layer keeps; the effect itself does nothing yet (PaperEffect.h).
struct LayerPaper {
    static constexpr int kMinIntensity = 0, kMaxIntensity = 100, kDefaultIntensity = 100;
    static constexpr int kMinScale = 10, kMaxScale = 500, kDefaultScale = 100;  // percent

    QString texture;  // name in the paper library; empty is the `None` entry
    int intensity = kDefaultIntensity;
    int scale = kDefaultScale;

    bool isSet() const { return !texture.isEmpty(); }
    bool shows() const { return isSet() && intensity > 0; }

    static int clampIntensity(int v) { return std::clamp(v, kMinIntensity, kMaxIntensity); }
    static int clampScale(int v) { return std::clamp(v, kMinScale, kMaxScale); }

    bool operator==(const LayerPaper&) const = default;
};

}
