#pragma once

#include <QString>

#include <cstdint>
#include <optional>
#include <vector>

namespace doodlebold {

// The modes a layer can have. Files store blendModeInfo().key, so the numbers are only seen by this program. All of them composite
// as Normal for now (BlendOps).
enum class BlendMode : std::uint8_t {
    PassThrough,      // TODO(reimplement): folders only; the folder's layers blend directly with what lies below the folder
    Normal,           // the layer's colours cover what lies below
    // Darkening
    Multiply,         // TODO(reimplement): darkens, like laying coloured glass over the picture
    Darken,           // TODO(reimplement): keeps the darker of the two colours, channel by channel
    DarkerColor,      // TODO(reimplement): keeps whichever whole colour is darker
    Deepen,           // TODO(reimplement): darkens strongly, more where the layer is more solid
    Burn,             // TODO(reimplement): darkens with more contrast, more where the layer is more solid
    LinearBurn,       // TODO(reimplement): darkens strongly by an even amount
    ColorBurn,        // TODO(reimplement): darkens with more contrast
    // Lightening
    Screen,           // TODO(reimplement): lightens, like projecting two pictures onto one wall
    Lighten,          // TODO(reimplement): keeps the lighter of the two colours, channel by channel
    LighterColor,     // TODO(reimplement): keeps whichever whole colour is lighter
    Brighten,         // TODO(reimplement): lightens strongly, more where the layer is more solid
    Dodge,            // TODO(reimplement): lightens with a glow, more where the layer is more solid
    LinearDodge,      // TODO(reimplement): lightens strongly by an even amount
    ColorDodge,       // TODO(reimplement): lightens with a glow
    // Contrast
    Overlay,          // TODO(reimplement): adds contrast, following the picture below
    SoftLight,        // TODO(reimplement): adds gentle contrast, like a soft spotlight
    HardLight,        // TODO(reimplement): adds strong contrast, following the layer
    PinLight,         // TODO(reimplement): replaces only colours much lighter or darker than the layer
    DeepenBrighten,   // TODO(reimplement): dark colours deepen and light colours brighten, more where the layer is more solid
    BurnDodge,        // TODO(reimplement): dark colours burn and light colours dodge, more where the layer is more solid
    HardMix,          // TODO(reimplement): reduces the result to flat, poster-like colours, more where the layer is more solid
    LinearLight,      // TODO(reimplement): dark colours deepen and light colours brighten, by an even amount
    VividLight,       // TODO(reimplement): dark colours burn and light colours dodge
    HardMixShape,     // TODO(reimplement): reduces the result to flat, poster-like colours
    // Comparison
    Difference,       // TODO(reimplement): shows how far apart the colours are, more where the layer is more solid
    DifferenceShape,  // TODO(reimplement): shows how far apart the colours are
    Exclude,          // TODO(reimplement): like Difference, with less contrast
    Subtract,         // TODO(reimplement): takes the layer's colour away from the picture below
    Divide,           // TODO(reimplement): divides the picture below by the layer's colour
    // Colour
    Hue,              // TODO(reimplement): takes the layer's hue and keeps the rest from below
    Saturation,       // TODO(reimplement): takes the layer's saturation and keeps the rest from below
    Color,            // TODO(reimplement): takes the layer's hue and saturation and keeps the brightness from below
    Luminosity,       // TODO(reimplement): takes the layer's brightness and keeps the colour from below
};

constexpr int kBlendModeCount = 35;

struct BlendModeInfo {
    BlendMode mode;
    const char* key;       // the name files store for this mode; it never changes
    const char* uiName;    // the label in the layer panel
    const char* psdKey;    // 4-character PSD blend key written for this mode
};

const BlendModeInfo& blendModeInfo(BlendMode m);
QString blendModeName(BlendMode m);
// The layer panel's list of modes; an empty entry is a separator.
const std::vector<std::optional<BlendMode>>& blendModeMenuOrder();
std::optional<BlendMode> blendModeFromKey(const QString& key);

}
