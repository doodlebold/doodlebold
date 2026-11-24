#pragma once

#include "core/selection/MagicWand.h"
#include "core/transform/Resample.h"

#include <QString>

#include <array>
#include <cstdint>
#include <optional>

namespace doodlebold {

// The fixed tools (the row above the tray): not tray slots, no brush settings.
enum class FixedTool : std::uint8_t {
    Marquee,
    Lasso,
    MagicWand,
    Move,
    Zoom,
    Rotate,
    Hand,
    Eyedropper,
    Text,
};

inline constexpr std::array<FixedTool, 9> kFixedTools = {FixedTool::Marquee, FixedTool::Lasso, FixedTool::MagicWand,
                                                         FixedTool::Text,    FixedTool::Move,  FixedTool::Zoom,
                                                         FixedTool::Rotate,  FixedTool::Hand,  FixedTool::Eyedropper};

QString fixedToolName(FixedTool t);
QString fixedToolKey(FixedTool t);
std::optional<FixedTool> fixedToolFromKey(const QString& key);
inline bool isSelectionTool(FixedTool t) {
    return t == FixedTool::Marquee || t == FixedTool::Lasso || t == FixedTool::MagicWand;
}

enum class TransformMode {
    Freeform,
    Resize,
    Deform,
    Rotate
};
QString transformModeName(TransformMode m);
QString transformModeKey(TransformMode m);
std::optional<TransformMode> transformModeFromKey(const QString& key);

struct MarqueeOptions {
    bool cutAndMove = true;  // dragging inside the selection moves the selected pixels
    TransformMode transformMode = TransformMode::Resize;
    int perspective = 0;  // 0 … 100 %
    transform::Interpolation dragging = transform::Interpolation::Nearest;
    transform::Interpolation preview = transform::Interpolation::Nearest;
    transform::Interpolation final = transform::Interpolation::Nearest;
    bool operator==(const MarqueeOptions&) const = default;
};

enum class LassoMode {
    Freeform,
    Polygonal,
    PolygonalFreeform
};
QString lassoModeKey(LassoMode m);
std::optional<LassoMode> lassoModeFromKey(const QString& key);

struct LassoOptions {
    LassoMode mode = LassoMode::Freeform;
    bool antialias = true;
    bool operator==(const LassoOptions&) const = default;
};

enum class WandSource {
    CurrentLayer,
    MergedImage
};

struct WandToolOptions {
    selection::WandMode mode = selection::WandMode::EnclosedTransparent;
    int colourThreshold = 0;
    int transparencyRange = 0;
    int gapTolerance = 0;
    WandSource source = WandSource::CurrentLayer;
    bool antialias = true;
    bool operator==(const WandToolOptions&) const = default;
};

struct TextToolOptions {
    QString font;             // family; empty = the first of the system's list
    double sizePx = 20.0;     // the default
    bool bold = false;
    bool italic = false;
    bool vertical = false;
    bool fitToPixelGrid = false;
    bool operator==(const TextToolOptions&) const = default;
};

struct FixedToolOptions {
    MarqueeOptions marquee;
    LassoOptions lasso;
    WandToolOptions wand;
    TextToolOptions text;
    bool operator==(const FixedToolOptions&) const = default;
    FixedToolOptions clamped() const;
};

}
