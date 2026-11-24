#include "core/brush/BrushSettings.h"

#include <QCoreApplication>

#include <algorithm>
#include <array>

namespace doodlebold {

namespace {

struct ToolInfo {
    ToolType type;
    const char* key;
    const char* name;
};

constexpr std::array<ToolInfo, 12> kTools = {{
    {ToolType::Pencil, "pencil", QT_TRANSLATE_NOOP("Tool", "Pencil")},
    {ToolType::Brush, "brush", QT_TRANSLATE_NOOP("Tool", "Brush")},
    {ToolType::WaterColor, "watercolor", QT_TRANSLATE_NOOP("Tool", "Water Color")},
    {ToolType::Marker, "marker", QT_TRANSLATE_NOOP("Tool", "Marker")},
    {ToolType::Eraser, "eraser", QT_TRANSLATE_NOOP("Tool", "Eraser")},
    {ToolType::Blur, "blur", QT_TRANSLATE_NOOP("Tool", "Blur")},
    {ToolType::Smudge, "smudge", QT_TRANSLATE_NOOP("Tool", "Smudge")},
    {ToolType::PixelArtPen, "pixelart", QT_TRANSLATE_NOOP("Tool", "Pixel Art Pen")},
    {ToolType::Clone, "clone", QT_TRANSLATE_NOOP("Tool", "Clone Stamp")},
    {ToolType::SelectionPen, "select", QT_TRANSLATE_NOOP("Tool", "Selection Pen")},
    {ToolType::SelectionEraser, "deselect", QT_TRANSLATE_NOOP("Tool", "Selection Eraser")},
    {ToolType::Bucket, "bucket", QT_TRANSLATE_NOOP("Tool", "Bucket")},
}};

const ToolInfo& info(ToolType t) {
    for (const ToolInfo& i : kTools)
        if (i.type == t)
            return i;
    return kTools[0];
}

}

QString toolTypeName(ToolType t) {
    return QCoreApplication::translate("Tool", info(t).name);
}

QString toolTypeKey(ToolType t) {
    return QString::fromLatin1(info(t).key);
}

std::optional<ToolType> toolTypeFromKey(const QString& key) {
    for (const ToolInfo& i : kTools)
        if (key == QLatin1String(i.key))
            return i.type;
    return std::nullopt;
}

QString StabilizerLevel::label() const {
    return QString::number(std::clamp(value, 0, kMax));
}

// A level's number, or the reference's name for a strong level, S-1 to S-7, as the reference cases give them: S-k is 15 + k, and S-6 and
// S-7, which are gone, are 20.
std::optional<StabilizerLevel> StabilizerLevel::fromLabel(const QString& label) {
    const QString text = label.trimmed().toUpper();
    const bool strong = text.startsWith(QLatin1String("S-"));
    bool ok = false;
    const int n = (strong ? text.mid(2) : text).toInt(&ok);
    const int least = strong ? 1 : 0;
    const int most = strong ? kSpecStrongLevels : kMax;
    if (!ok || n < least || n > most)
        return std::nullopt;
    return StabilizerLevel{strong ? std::min(kLastAveraged + n, kMax) : n};
}

BrushSettings BrushSettings::clamped() const {
    BrushSettings s = *this;
    s.size = std::clamp(s.size, 1, kMaxSize);
    for (int* p : {&s.minSize, &s.density, &s.minDensity, &s.sharpness, &s.dilution, &s.blurWidth, &s.blending, &s.persistence,
                   &s.blurPressure, &s.formStrength, &s.textureStrength, &s.textureScratch, &s.ampDensity, &s.bristleMinimum,
                   &s.bristleSize, &s.shapeDensity, &s.shapeAngleJitter, &s.shapeSizeJitter})
        *p = std::clamp(*p, 0, 100);
    s.edge = std::clamp(s.edge, 0, kEdgePresets - 1);
    // The ranges of the panel's controls.
    s.formScale = std::clamp(s.formScale, 10, 500);
    s.textureScale = std::clamp(s.textureScale, 10, 500);
    s.bristleRandomize = std::clamp(s.bristleRandomize, 0, kBristleRandomizeMost);
    s.bristleDirection = std::clamp(s.bristleDirection, kBristleDirectionNone, kBristleDirectionPen);
    s.shapeAngleControl = std::clamp(s.shapeAngleControl, 0, 2);
    s.shapeAngle = std::clamp(s.shapeAngle, -180, 180);
    s.shapeScaling = std::clamp(s.shapeScaling, 1, 200);
    s.shapeSpacing = std::clamp(s.shapeSpacing, 1, 50);
    s.shapeScattering = std::clamp(s.shapeScattering, 0, 50);
    s.opacity = std::clamp(s.opacity, 0, 255);
    for (int* p : {&s.sensitivitySize, &s.sensitivityDensity, &s.sensitivityBlending})
        *p = std::clamp(*p, 0, 200);
    if (s.stabilizer)
        s.stabilizer->value = std::clamp(s.stabilizer->value, 0, StabilizerLevel::kMax);
    s.bucket.colourThreshold = std::clamp(s.bucket.colourThreshold, 0, 255);
    s.bucket.gapTolerance = std::clamp(s.bucket.gapTolerance, 0, 100);
    return s;
}

namespace {

// Water Color's starting brush-shape Dens., %, the reference's: its Water Blur cases, painted at each tool's starting shape
// settings, fit 50 on Water Color (9 to 13 levels against 35 to 41 at 100) and 100 on the other tools (10 to 16 against 51 to 57 at 50).
constexpr int kWaterShapeDensity = 50;

// The painting tools' mixing starts here, from the rules. The Brush's density reaches full at about 60 % pressure.
void addMixingDefaults(BrushSettings& s) {
    switch (s.tool) {
    case ToolType::Brush:
        s.blending = 50, s.dilution = 0, s.persistence = 50, s.blurPressure = 50;
        s.pressureDensity = true, s.sensitivityDensity = 160;
        break;
    case ToolType::WaterColor:
        // Water Color's own starting values.
        s.blending = 30, s.dilution = 30, s.persistence = 50, s.blurPressure = 50;
        s.size = 500, s.pressureSize = true, s.minSize = 60;
        s.pressureDensity = true, s.pressureBlending = true;
        s.edge = kEdgePresets - 1, s.sharpness = 0;
        s.disableDensityAssist = true, s.ampDensity = 50;  // the reference's
        s.shapeDensity = kWaterShapeDensity;
        break;
    case ToolType::Marker:
        // The Marker's own starting values.
        s.blending = 50, s.dilution = 0, s.persistence = 80, s.blurPressure = 0;
        s.size = 500, s.pressureSize = true, s.minSize = 50;
        s.pressureDensity = true, s.pressureBlending = true;
        s.edge = kEdgePresets - 1, s.sharpness = 0;
        s.disableDensityAssist = true, s.ampDensity = 50;  // the reference's
        break;
    default: break;
    }
}

}

BrushSettings defaultSettings(ToolType t) {
    BrushSettings s;
    s.tool = t;
    if (t == ToolType::Pencil) {
        // The Pencil's own starting values.
        s.size = 100;
        s.pressureSize = true;
        s.edge = kEdgePresets - 1;
        s.sharpness = 50;
    }
    if (t == ToolType::Blur) {
        // Blur's own starting values: Size 30 px without pressure, Density 100 % with it, the 1st Shape at
        // Sharpness 50 %, Blur Width 50 %.
        s.size = 300;
        s.pressureDensity = true;
        s.edge = 0;
        s.sharpness = 50;
        s.blurWidth = 50;
    }
    if (t == ToolType::Smudge) {
        // Smudge's own starting values: Size 30 px and Density 90 %, both with pressure, the 1st Shape at
        // Sharpness 0 %.
        s.size = 300;
        s.pressureSize = true;
        s.density = 90;
        s.pressureDensity = true;
        s.edge = 0;
        s.sharpness = 0;
    }
    if (t == ToolType::Eraser) {
        // The Eraser's own starting values: Size 20 px and Density 100 %, both without pressure, the 5th Shape at
        // Sharpness 50 %.
        s.edge = kEdgePresets - 1;
        s.sharpness = 50;
    }
    if (t == ToolType::Clone) {
        // It starts with the Pencil's tip, the 5th Shape at Sharpness 50 %.
        s.edge = kEdgePresets - 1;
        s.sharpness = 50;
    }
    if (t == ToolType::Brush) {
        // The Brush's own starting values.
        s.size = 500;
        s.sharpness = 50;
        s.edge = kEdgePresets - 1;
        s.pressureSize = true;
        s.minSize = 50;
        s.pressureBlending = true;
        // The reference's Brush starts its tip shape at Strength 50 and its texture at 95.
        s.formStrength = 50;
        s.textureStrength = 95;
    }
    addMixingDefaults(s);
    return s;
}

void resetTipShape(BrushSettings& s) {
    const BrushSettings start = defaultSettings(s.tool);
    s.form = start.form;
    s.formStrength = start.formStrength;
    s.formScale = start.formScale;
    s.formInvert = start.formInvert;
    s.formSharpen = start.formSharpen;
    s.formEraseLow = start.formEraseLow;
    s.bristleMinimum = start.bristleMinimum;
    s.bristleSize = start.bristleSize;
    s.bristleRandomize = start.bristleRandomize;
    s.bristleDirection = start.bristleDirection;
    s.bristleDetectTurn = start.bristleDetectTurn;
    s.shapeDensity = start.shapeDensity;
    s.shapeAngleControl = start.shapeAngleControl;
    s.shapeAngle = start.shapeAngle;
    s.shapeAngleJitter = start.shapeAngleJitter;
    s.shapeScaling = start.shapeScaling;
    s.shapeSizeJitter = start.shapeSizeJitter;
    s.shapeSpacing = start.shapeSpacing;
    s.shapeScattering = start.shapeScattering;
    s.shapeHighQuality = start.shapeHighQuality;
}

void resetTexture(BrushSettings& s) {
    const BrushSettings start = defaultSettings(s.tool);
    s.texture = start.texture;
    s.textureStrength = start.textureStrength;
    s.textureScale = start.textureScale;
    s.textureScratch = start.textureScratch;
    s.textureInvert = start.textureInvert;
    s.textureEraseLow = start.textureEraseLow;
}

}
