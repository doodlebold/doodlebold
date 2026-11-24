#include "core/effects/EffectSettings.h"

#include <QCoreApplication>

#include <algorithm>

namespace doodlebold {

namespace {

// The sliders each dialog offers: the reference's, from its Hue/Saturation/Lightness and Levels dialogs. The other dialogs'
// sliders and every range and starting value are this version's own.
constexpr EffectSlider kBrightnessContrast[] = {
    {"Brightness :", -100, 100, 0},
    {"Contrast :", -100, 100, 0},
};
constexpr EffectSlider kHueSaturation[] = {
    {"Hue :", -180, 180, 0, 0, "°"},
    {"Saturation :", -100, 100, 0},
    {"Lightness :", -100, 100, 0},
};
constexpr EffectSlider kLevels[] = {
    {"Input Black :", 0, 255, 0},  {"Input White :", 0, 255, 255},  {"Gamma :", 0.1, 9.99, 1.0, 2, "", true},
    {"Output Black :", 0, 255, 0}, {"Output White :", 0, 255, 255},
};
constexpr EffectSlider kColorBalance[] = {
    {"Cyan – Red :", -100, 100, 0},
    {"Magenta – Green :", -100, 100, 0},
    {"Yellow – Blue :", -100, 100, 0},
};
constexpr EffectSlider kPosterize[] = {
    {"Levels :", 2, 64, 4},
};
constexpr EffectSlider kGaussianBlur[] = {
    {"Radius :", 0.1, 200, 2.0, 1, " px"},
};
constexpr EffectSlider kMotionBlur[] = {
    {"Angle :", -180, 180, 0, 0, "°"},
    {"Distance :", 1, 200, 10, 0, " px"},
};
constexpr EffectSlider kRadialBlur[] = {
    {"Amount :", 1, 100, 10, 0, "%"},
    {"Center X :", 0, 100, 50, 0, "%"},
    {"Center Y :", 0, 100, 50, 0, "%"},
};
constexpr EffectSlider kUnsharpMask[] = {
    {"Amount :", 0, 500, 100, 0, "%"},
    {"Radius :", 0.1, 100, 1.0, 1, " px"},
    {"Threshold :", 0, 255, 0},
};
constexpr EffectSlider kAddNoise[] = {
    {"Amount :", 0, 100, 10, 0, "%"},
};
// Chromatic Aberration's controls and their ranges follow the reference's dialog.
constexpr const char* kChromaticModes[] = {"Linear", "Radial"};
constexpr EffectSlider kChromaticAberration[] = {
    {"Amount :", 0, 100, 4, 0, " px"},
    {"Angle :", -180, 180, 0, 0, "°", false, chromatic::kLinear},
    {"Center X :", 0, 0, 0, 0, " px", false, chromatic::kRadial, SliderSpan::CanvasWidth},
    {"Center Y :", 0, 0, 0, 0, " px", false, chromatic::kRadial, SliderSpan::CanvasHeight},
};

constexpr EffectSpec kEffects[] = {
    {EffectKind::BrightnessContrast, "effects.brightnessContrast", "Brightness/Contrast", kBrightnessContrast},
    {EffectKind::HueSaturation, "effects.hueSaturation", "Hue/Saturation/Lightness", kHueSaturation, "Colorize", false},
    {EffectKind::Levels, "effects.levels", "Levels", kLevels},
    {EffectKind::ColorBalance, "effects.colorBalance", "Color Balance", kColorBalance, "Preserve Luminosity", true},
    {EffectKind::Invert, "effects.invert", "Invert", {}},
    {EffectKind::Desaturate, "effects.desaturate", "Desaturate", {}},
    {EffectKind::Posterize, "effects.posterize", "Posterize", kPosterize},
    {EffectKind::GaussianBlur, "effects.gaussianBlur", "Gaussian Blur", kGaussianBlur},
    {EffectKind::MotionBlur, "effects.motionBlur", "Motion Blur", kMotionBlur},
    {EffectKind::RadialBlur, "effects.radialBlur", "Radial Blur", kRadialBlur},
    {EffectKind::UnsharpMask, "effects.unsharpMask", "Sharpen/Unsharp Mask", kUnsharpMask},
    {EffectKind::AddNoise, "effects.addNoise", "Add Noise", kAddNoise, "Monochrome", false},
    {EffectKind::ChromaticAberration, "effects.chromaticAberration", "Chromatic Aberration", kChromaticAberration, nullptr, false,
     kChromaticModes},
};

}

std::span<const EffectSpec> allEffects() {
    return kEffects;
}

const EffectSpec& effectSpec(EffectKind kind) {
    return kEffects[static_cast<std::size_t>(kind)];
}

EffectSettings EffectSettings::start(EffectKind kind, QSize canvas) {
    EffectSettings s;
    s.kind = kind;
    const EffectSpec& spec = effectSpec(kind);
    for (std::size_t i = 0; i < spec.sliders.size(); ++i) {
        const EffectSlider& slider = spec.sliders[i];
        switch (slider.span) {
        case SliderSpan::Fixed: s.values[i] = slider.start; break;
        case SliderSpan::CanvasWidth: s.values[i] = std::max(0, canvas.width()) / 2.0; break;
        case SliderSpan::CanvasHeight: s.values[i] = std::max(0, canvas.height()) / 2.0; break;
        }
    }
    s.option = spec.optionStart;
    return s;
}

bool isIdentity(const EffectSettings& s) {
    switch (s.kind) {
    case EffectKind::BrightnessContrast: return s.value(0) == 0 && s.value(1) == 0;
    case EffectKind::HueSaturation: return !s.option && s.value(0) == 0 && s.value(1) == 0 && s.value(2) == 0;
    case EffectKind::Levels: return s.value(0) == 0 && s.value(1) == 255 && s.value(2) == 1.0 && s.value(3) == 0 && s.value(4) == 255;
    case EffectKind::ColorBalance: return s.value(0) == 0 && s.value(1) == 0 && s.value(2) == 0;
    case EffectKind::RadialBlur:
    case EffectKind::UnsharpMask:
    case EffectKind::AddNoise:
    case EffectKind::ChromaticAberration: return s.value(0) <= 0;
    case EffectKind::MotionBlur: return s.value(1) < 1;
    case EffectKind::Invert:
    case EffectKind::Desaturate:
    case EffectKind::Posterize:
    case EffectKind::GaussianBlur: return false;
    }
    return false;
}

bool isPointEffect(EffectKind kind) {
    switch (kind) {
    case EffectKind::GaussianBlur:
    case EffectKind::MotionBlur:
    case EffectKind::RadialBlur:
    case EffectKind::UnsharpMask:
    case EffectKind::ChromaticAberration: return false;
    default: return true;
    }
}

QString effectTitle(EffectKind kind) {
    return QCoreApplication::translate("Effects", effectSpec(kind).title);
}

}
