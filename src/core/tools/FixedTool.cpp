#include "core/tools/FixedTool.h"

#include <QCoreApplication>

#include <algorithm>

namespace doodlebold {

namespace {

struct FixedInfo {
    FixedTool tool;
    const char* key;
    const char* name;
};

constexpr std::array<FixedInfo, 9> kInfo = {{
    {FixedTool::Marquee, "marquee", QT_TRANSLATE_NOOP("Tool", "Marquee")},
    {FixedTool::Lasso, "lasso", QT_TRANSLATE_NOOP("Tool", "Lasso")},
    {FixedTool::MagicWand, "wand", QT_TRANSLATE_NOOP("Tool", "Magic Wand")},
    {FixedTool::Move, "move", QT_TRANSLATE_NOOP("Tool", "Move")},
    {FixedTool::Zoom, "zoom", QT_TRANSLATE_NOOP("Tool", "Zoom")},
    {FixedTool::Rotate, "rotate", QT_TRANSLATE_NOOP("Tool", "Rotate")},
    {FixedTool::Hand, "hand", QT_TRANSLATE_NOOP("Tool", "Hand")},
    {FixedTool::Eyedropper, "syringe", QT_TRANSLATE_NOOP("Tool", "Eyedropper")},
    {FixedTool::Text, "newtext", QT_TRANSLATE_NOOP("Tool", "Text")},
}};

const FixedInfo& info(FixedTool t) {
    for (const FixedInfo& i : kInfo)
        if (i.tool == t)
            return i;
    return kInfo[0];
}

struct ModeInfo {
    TransformMode mode;
    const char* key;
    const char* name;
};
constexpr std::array<ModeInfo, 4> kModes = {{
    {TransformMode::Freeform, "freeform", QT_TRANSLATE_NOOP("Transform", "Freeform")},
    {TransformMode::Resize, "resize", QT_TRANSLATE_NOOP("Transform", "Resize")},
    {TransformMode::Deform, "deform", QT_TRANSLATE_NOOP("Transform", "Deform")},
    {TransformMode::Rotate, "rotate", QT_TRANSLATE_NOOP("Transform", "Rotate")},
}};

struct LassoInfo {
    LassoMode mode;
    const char* key;
};
constexpr std::array<LassoInfo, 3> kLassoModes = {{
    {LassoMode::Freeform, "freeform"},
    {LassoMode::Polygonal, "polygonal"},
    {LassoMode::PolygonalFreeform, "polygonalFreeform"},
}};

}

QString fixedToolName(FixedTool t) {
    return QCoreApplication::translate("Tool", info(t).name);
}

QString fixedToolKey(FixedTool t) {
    return QString::fromLatin1(info(t).key);
}

std::optional<FixedTool> fixedToolFromKey(const QString& key) {
    for (const FixedInfo& i : kInfo)
        if (key == QLatin1String(i.key))
            return i.tool;
    return std::nullopt;
}

QString transformModeName(TransformMode m) {
    for (const ModeInfo& i : kModes)
        if (i.mode == m)
            return QCoreApplication::translate("Transform", i.name);
    return {};
}

QString transformModeKey(TransformMode m) {
    for (const ModeInfo& i : kModes)
        if (i.mode == m)
            return QString::fromLatin1(i.key);
    return {};
}

std::optional<TransformMode> transformModeFromKey(const QString& key) {
    for (const ModeInfo& i : kModes)
        if (key == QLatin1String(i.key))
            return i.mode;
    return std::nullopt;
}

QString lassoModeKey(LassoMode m) {
    for (const LassoInfo& i : kLassoModes)
        if (i.mode == m)
            return QString::fromLatin1(i.key);
    return {};
}

std::optional<LassoMode> lassoModeFromKey(const QString& key) {
    for (const LassoInfo& i : kLassoModes)
        if (key == QLatin1String(i.key))
            return i.mode;
    return std::nullopt;
}

FixedToolOptions FixedToolOptions::clamped() const {
    FixedToolOptions o = *this;
    o.marquee.perspective = std::clamp(o.marquee.perspective, 0, 100);
    o.wand.colourThreshold = std::clamp(o.wand.colourThreshold, 0, 255);
    o.wand.transparencyRange = std::clamp(o.wand.transparencyRange, 0, 255);
    o.wand.gapTolerance = std::clamp(o.wand.gapTolerance, 0, 100);
    o.text.sizePx = std::clamp(o.text.sizePx, 1.0, 1000.0);
    return o;
}

}
