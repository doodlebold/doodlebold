#include "core/layers/BlendMode.h"

#include <QCoreApplication>

namespace doodlebold {

namespace {

// In the order of the enum.
constexpr BlendModeInfo kInfo[kBlendModeCount] = {
    {BlendMode::PassThrough, "passThrough", "Pass Through", "pass"},
    {BlendMode::Normal, "normal", "Normal", "norm"},
    {BlendMode::Multiply, "multiply", "Multiply", "mul "},
    {BlendMode::Darken, "darken", "Darken", "dark"},
    {BlendMode::DarkerColor, "darkerColor", "Darken Color", "dkCl"},
    {BlendMode::Deepen, "deepen", "Deepen", "lbrn"},
    {BlendMode::Burn, "burn", "Burn", "idiv"},
    {BlendMode::LinearBurn, "linearBurn", "Linear Burn", "lbrn"},
    {BlendMode::ColorBurn, "colorBurn", "Color Burn", "idiv"},
    {BlendMode::Screen, "screen", "Screen", "scrn"},
    {BlendMode::Lighten, "lighten", "Lighten", "lite"},
    {BlendMode::LighterColor, "lighterColor", "Lighten Color", "lgCl"},
    {BlendMode::Brighten, "brighten", "Brighten", "lddg"},
    {BlendMode::Dodge, "dodge", "Dodge", "div "},
    {BlendMode::LinearDodge, "linearDodge", "Linear Dodge", "lddg"},
    {BlendMode::ColorDodge, "colorDodge", "Color Dodge", "div "},
    {BlendMode::Overlay, "overlay", "Overlay", "over"},
    {BlendMode::SoftLight, "softLight", "Soft Light", "sLit"},
    {BlendMode::HardLight, "hardLight", "Hard Light", "hLit"},
    {BlendMode::PinLight, "pinLight", "Pin Light", "pLit"},
    {BlendMode::DeepenBrighten, "deepenBrighten", "Deepen/Brighten", "lLit"},
    {BlendMode::BurnDodge, "burnDodge", "Burn/Dodge", "vLit"},
    {BlendMode::HardMix, "hardMix", "Hard Mix", "hMix"},
    {BlendMode::LinearLight, "linearLight", "Linear Light", "lLit"},
    {BlendMode::VividLight, "vividLight", "Vivid Light", "vLit"},
    {BlendMode::HardMixShape, "hardMixShape", "Hard Mix (Shape)", "hMix"},
    {BlendMode::Difference, "difference", "Difference", "diff"},
    {BlendMode::DifferenceShape, "differenceShape", "Difference (Shape)", "diff"},
    {BlendMode::Exclude, "exclude", "Exclude", "smud"},
    {BlendMode::Subtract, "subtract", "Subtract", "fsub"},
    {BlendMode::Divide, "divide", "Divide", "fdiv"},
    {BlendMode::Hue, "hue", "Hue", "hue "},
    {BlendMode::Saturation, "saturation", "Saturation", "sat "},
    {BlendMode::Color, "color", "Color", "colr"},
    {BlendMode::Luminosity, "luminosity", "Luminosity", "lum "},
};

}

const BlendModeInfo& blendModeInfo(BlendMode m) {
    const int i = static_cast<int>(m);
    return kInfo[(i >= 0 && i < kBlendModeCount) ? i : static_cast<int>(BlendMode::Normal)];
}

QString blendModeName(BlendMode m) {
    return QCoreApplication::translate("BlendMode", blendModeInfo(m).uiName);
}

const std::vector<std::optional<BlendMode>>& blendModeMenuOrder() {
    using enum BlendMode;
    static const std::vector<std::optional<BlendMode>> order = {
        Normal,    std::nullopt, Multiply,       Deepen,    Burn,         Darken,       DarkerColor, std::nullopt,
        Screen,    Brighten,     Dodge,          Lighten,   LighterColor, std::nullopt, Overlay,     SoftLight,
        HardLight, PinLight,     DeepenBrighten, BurnDodge, HardMix,      std::nullopt, Difference,  Exclude,
        Subtract,  Divide,       std::nullopt,   Hue,       Saturation,   Color,        Luminosity,
    };
    return order;
}

std::optional<BlendMode> blendModeFromKey(const QString& key) {
    for (const BlendModeInfo& info : kInfo)
        if (key == QLatin1String(info.key))
            return info.mode;
    return std::nullopt;
}

}
