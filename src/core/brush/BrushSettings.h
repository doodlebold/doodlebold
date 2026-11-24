#pragma once

#include "core/selection/WandMode.h"

#include <QString>

#include <cstdint>
#include <optional>

namespace doodlebold {

enum class ToolType : std::uint8_t {
    Pencil,
    Brush,
    WaterColor,
    Marker,
    Eraser,
    Blur,
    Smudge,
    PixelArtPen,
    Clone,
    SelectionPen,
    SelectionEraser,
    Bucket,  // fills the area around the click; it paints no strokes
};

QString toolTypeName(ToolType t);
// The tool's name in tools.json.
QString toolTypeKey(ToolType t);
std::optional<ToolType> toolTypeFromKey(const QString& key);

// The value of the stabilizer controls. Painting tools use it through StrokeSmoother.
// The Stabilizer's levels are 0 to 20: 0 to 15 average more events each, and 16 to 20
// are the strong levels the reference's panel calls S-1 to S-5. Its S-6 and S-7 are gone; a saved one is 20. The
// global level starts at 4, a design choice; it was 3, as the reference cases assume.
struct StabilizerLevel {
    static constexpr int kLastAveraged = 15;  // the last level that only averages; the strong levels follow it
    static constexpr int kMax = 20;
    static constexpr int kSpecStrongLevels = 7;  // S-1 to S-7 in the reference's and the reference cases' names
    static constexpr int kGlobalStart = 4;
    int value = 0;
    bool operator==(const StabilizerLevel&) const = default;
    QString label() const;
    static std::optional<StabilizerLevel> fromLabel(const QString& label);
};

// The options the quick bar and Edit > Options set for every tool.
struct BrushOptions {
    double minimumPressure = 0.0;  // 0 … 1
    StabilizerLevel stabilizer{StabilizerLevel::kGlobalStart};  // used unless the tool overrides it
};

// The Bucket's choices: how it finds the area around the click, as the Magic Wand does, and whether a selection
// bounds the fill. The brush engine never reads them.
struct BucketSettings {
    selection::WandMode mode = selection::WandMode::SimilarColour;
    int colourThreshold = 24;  // 0 … 255; in Enclosed Transparent Area, how solid a pixel may be and still count as transparent
    int gapTolerance = 0;      // px, 0 … 100
    bool mergedSource = true;  // Merged Image; off, the current layer
    bool antialias = true;
    bool ignoreSelection = false;
    bool operator==(const BucketSettings&) const = default;
};

// The edge shapes a tool can choose between, from soft to firm.
inline constexpr int kEdgePresets = 5;

// The bristle box's Direction, in the panel's order.
inline constexpr int kBristleDirectionNone = 0;
inline constexpr int kBristleDirectionAuto = 1;
inline constexpr int kBristleDirectionPen = 2;
// The bristle box's Hair starts here, %: the reference's start.
inline constexpr int kHairStart = 50;
// The bristle box's Randomize goes from 0 to this, as the reference's.
inline constexpr int kBristleRandomizeMost = 20;

// What the tool panel edits and tools.json saves. A saved tool's keys for the old mixing rates (Pick Up, Lay Down, Refill) are
// ignored. Taper, Taper Curve, Start Ramp, Flick and Size Curve are gone:
// a saved tool's keys for them are ignored. Every tool starts from one neutral set, and
// the Pencil and Brush have their own
// starting values: see defaultSettings(). The engine doesn't read the blur and shape-variation values yet; the bristle box's it does.
struct BrushSettings {
    // The largest size of every tool, in tenths of a pixel: 4000 px, the baseline.
    static constexpr int kMaxSize = 40000;

    ToolType tool = ToolType::Pencil;
    int size = 200;  // tenths of a pixel, 1 … kMaxSize
    int minSize = 0;
    int density = 100;
    int minDensity = 0;
    int edge = 0;
    int sharpness = 100;
    int dilution = 0;  // how far paint a mixing tool picks up keeps its own transparency, rather than counting as opaque, %
    // How the painting tools mix, once per dab. All %.
    int blending = 0;  // how much of the brush colour goes into each dab, through a curve
    int persistence = 0;  // how much of the colour it carries lingers from one dab to the next
    int blurPressure = 0;  // below this pressure, it adds no brush colour and only pushes paint around
    int blurWidth = 0;
    bool pressureSize = false;
    bool pressureDensity = false;
    bool pressureBlending = false;  // Blending is reached at full pressure, and a lighter touch mixes less
    bool pressureDilution = false;  // Dilution is reached at full pressure, and a lighter touch dilutes less
    int sensitivitySize = 100;
    int sensitivityDensity = 100;
    int sensitivityBlending = 100;
    std::optional<StabilizerLevel> stabilizer;
    QString form = QStringLiteral("Round");
    int formStrength = 100;
    int formScale = 100;
    bool formInvert = false;
    // Curve Interpolation: on, the stamps follow a curve through the samples where the tool does (the Stabilizer on, or the Pencil's
    // tip); off, the path is straight from sample to sample. On at the start, as the reference's on every tool.
    bool curveInterpolation = true;
    // Disable Density Control Assist: off, a line's tone follows pressure's share of Density, each stamp laying what makes it so; on, a
    // stamp lays its own strength times that share and a line builds up past it. On at the start for Water Color and the
    // Marker, off for the rest, as the reference's.
    bool disableDensityAssist = false;
    // Anti-Ripple: on, a hard Shape's edge stays flat and solid to its last pixel or two, as the 5th Shape's skirt lays it; off,
    // it falls away over several pixels. On at the start, every tool.
    bool antiRipple = true;
    // Amp. Density, 0 to 100 %: stored; the reference's readings show no change from it. 50 % at the start for Water Color
    // and the Marker, 0 % for the rest.
    int ampDensity = 0;
    bool formSharpen = false;
    // The tip shape's Invert for Transparency: painting the transparent colour, the tip shape's Invert is turned over, as the texture's
    // switch does; with ordinary paint it does nothing. On at the start.
    bool formEraseLow = true;  // the tip shape's Sharpen: a blot map's edge goes crisper
    QString texture = QStringLiteral("None");
    int textureStrength = 100;
    int textureScale = 100;
    bool textureInvert = false;
    // Painting the transparent colour, which the Eraser always does, turns the paper over, taking most from its low places, where
    // painting lays least: on at the start, as the reference's, on every tool. Off, it takes most from the
    // paper's tops, as painting lays paint. On other paint it does nothing.
    bool textureEraseLow = true;
    // Scratch Prs., 0 to 100 %: the paper's pits empty the more, the lighter the touch, as the reference's.
    int textureScratch = 0;
    int bristleMinimum = 0;
    // Hair, %: each hair's width, in proportion; 50 at the start, as the reference's. Its hairs are twice as wide at 100.
    int bristleSize = kHairStart;
    int bristleRandomize = 0;
    // Which way a bristle map's hairs lie: kBristleDirectionAuto, the reference's start, turns the map with the stroke;
    // kBristleDirectionNone keeps it as for a stroke going right; kBristleDirectionPen, with the pen, reads as None for now.
    int bristleDirection = kBristleDirectionAuto;
    // Detect Turn: with Direction Auto, a turn much sharper than the stroke has been taking starts the hairs afresh. On at the
    // start, as the reference's; a tool saved before it, with no key for it, starts from here, on.
    bool bristleDetectTurn = true;
    int shapeDensity = 100;
    int shapeAngleControl = 1;  // along the stroke: the reference's starting Angle Control
    int shapeAngle = 0;
    int shapeAngleJitter = 0;
    int shapeScaling = 100;
    int shapeSizeJitter = 0;
    int shapeSpacing = 25;  // % of the size: the reference's own start, which a shape's settings file sets again
    int shapeScattering = 0;
    // HQ Down Scaling: a stamp much smaller than its brush shape's picture averages a finer copy over each pixel.
    bool shapeHighQuality = false;
    int opacity = 255;
    bool cloneAligned = true;
    bool cloneAllLayers = false;
    BucketSettings bucket;

    double sizePx() const { return size * 0.1; }
    bool operator==(const BrushSettings&) const = default;

    BrushSettings clamped() const;
};

// The neutral set, with the Pencil's and Brush's own starting values on top.
BrushSettings defaultSettings(ToolType t);
// The Resets in the Tools panel's Tip shape and Texture boxes: one box's settings back to the tool's starting values, nothing
// else. The tip shape's: the choice, its Strength, Scale, Invert, Sharpen and Invert for Transparency, the bristle and brush-shape
// settings. The texture's: the choice, its Strength, Scale, Scratch, Invert and Invert for Transparency.
void resetTipShape(BrushSettings& s);
void resetTexture(BrushSettings& s);

}
