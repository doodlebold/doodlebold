#pragma once

#include <QSize>
#include <QString>

#include <array>
#include <cstdint>
#include <span>

namespace doodlebold {

// The Effects menu's entries, in its order.
enum class EffectKind : std::uint8_t {
    BrightnessContrast,
    HueSaturation,
    Levels,
    ColorBalance,
    Invert,
    Desaturate,
    Posterize,
    GaussianBlur,
    MotionBlur,
    RadialBlur,
    UnsharpMask,
    AddNoise,
    ChromaticAberration,
};

// What a slider's range is measured against: its own minimum and maximum, or the canvas, 0 to its width or height, starting
// at its centre (Chromatic Aberration's Center X and Y, in canvas pixels).
enum class SliderSpan : std::uint8_t {
    Fixed,
    CanvasWidth,
    CanvasHeight
};

// One slider of an effect's dialog: its label as the dialog shows it, range, starting value and how it is written.
struct EffectSlider {
    const char* label = "";
    double minimum = 0.0;
    double maximum = 0.0;
    double start = 0.0;
    int decimals = 0;
    const char* suffix = "";
    bool logarithmic = false;  // the bar's position follows the value's logarithm (Gamma: 1.00 in the middle)
    int onlyInMode = -1;       // enabled only while the dialog's Mode is this one; -1, always
    SliderSpan span = SliderSpan::Fixed;
};

// What an effect's dialog offers. No sliders: the effect has no dialog and applies at once (Invert, Desaturate).
struct EffectSpec {
    EffectKind kind;
    const char* id;     // the menu command, "effects.gaussianBlur"
    const char* title;  // the menu label and the dialog's title, without the "…"
    std::span<const EffectSlider> sliders;
    const char* option = nullptr;  // a check box under the sliders ("Colorize"), or none
    bool optionStart = false;
    std::span<const char* const> modes = {};  // a Mode choice above the sliders, starting at the first; or none
};

std::span<const EffectSpec> allEffects();
const EffectSpec& effectSpec(EffectKind kind);

// An effect with its dialog's values: values[i] is slider i of its spec, `mode` the index of its Mode. `seed` makes Add
// Noise's grain; the dialog picks a new one each time it opens.
struct EffectSettings {
    static constexpr int kMaxSliders = 5;

    EffectKind kind = EffectKind::Invert;
    std::array<double, kMaxSliders> values{};
    bool option = false;
    int mode = 0;
    std::uint32_t seed = 1;

    // The effect at its spec's starting values; a slider measured against the canvas starts at the centre of `canvas`.
    static EffectSettings start(EffectKind kind, QSize canvas = {});
    double value(int i) const { return values[static_cast<std::size_t>(i)]; }
    bool operator==(const EffectSettings&) const = default;
};

// Chromatic Aberration's modes and values (EffectSettings::mode and values[]).
namespace chromatic {
enum Mode : int {
    kLinear,
    kRadial
};
enum Value : int {
    kAmount,
    kAngle,
    kCenterX,
    kCenterY
};
}

// True when the effect would leave every pixel as it is (Brightness and Contrast at 0, a blur of nothing).
bool isIdentity(const EffectSettings& s);

// Whether the effect works pixel by pixel, reading nothing around it (the colour effects and Add Noise).
bool isPointEffect(EffectKind kind);

QString effectTitle(EffectKind kind);

}
