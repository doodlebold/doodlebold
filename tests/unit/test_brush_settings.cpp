#include "core/brush/BrushSettings.h"

#include <QSet>

#include <gtest/gtest.h>

using namespace doodlebold;

// The baseline: no tool's size goes beyond 4000 px.
TEST(BrushSettingsClamped, SizeIsClampedToOneTenthTo4000PxForEveryTool) {
    static_assert(BrushSettings::kMaxSize == 40000);
    for (int i = 0; i <= int(ToolType::Bucket); ++i) {
        BrushSettings s = defaultSettings(ToolType(i));
        EXPECT_LE(s.size, BrushSettings::kMaxSize) << "the starting size of tool " << i;
        for (int tooSmall : {0, -5, -100000}) {
            s.size = tooSmall;
            EXPECT_EQ(s.clamped().size, 1) << tooSmall;
        }
        s.size = 40000;
        EXPECT_EQ(s.clamped().size, 40000);
        for (int tooLarge : {40001, 50000, 1000000}) {
            s.size = tooLarge;
            EXPECT_EQ(s.clamped().size, 40000) << "tool " << i << ", " << tooLarge;
        }
        EXPECT_DOUBLE_EQ(s.clamped().sizePx(), 4000.0);
    }
}

TEST(BrushSettingsClamped, PercentagesAreClampedToZeroToHundred) {
    BrushSettings s = defaultSettings(ToolType::Brush);
    s.minSize = 150, s.density = 150, s.minDensity = 150, s.sharpness = 150;
    BrushSettings c = s.clamped();
    for (int v : {c.minSize, c.density, c.minDensity, c.sharpness})
        EXPECT_EQ(v, 100);
    s.minSize = -7, s.density = -7, s.minDensity = -7, s.sharpness = -7;
    c = s.clamped();
    for (int v : {c.minSize, c.density, c.minDensity, c.sharpness})
        EXPECT_EQ(v, 0);
}

TEST(BrushSettingsClamped, EdgeAndSensitivityAreClamped) {
    BrushSettings s = defaultSettings(ToolType::Pencil);
    s.edge = 9;
    s.sensitivitySize = 500, s.sensitivityDensity = -3;
    const BrushSettings c = s.clamped();
    EXPECT_EQ(c.edge, kEdgePresets - 1);
    EXPECT_EQ(c.sensitivitySize, 200);
    EXPECT_EQ(c.sensitivityDensity, 0);
    s.edge = -2;
    EXPECT_EQ(s.clamped().edge, 0);
}

TEST(BrushSettingsClamped, StabilizerOverrideIsClamped) {
    BrushSettings s = defaultSettings(ToolType::Pencil);
    s.stabilizer = StabilizerLevel{40};
    EXPECT_EQ(s.clamped().stabilizer->value, StabilizerLevel::kMax);
    s.stabilizer = StabilizerLevel{-1};
    EXPECT_EQ(s.clamped().stabilizer->value, 0);
    s.stabilizer = StabilizerLevel{4};
    EXPECT_EQ(s.clamped().stabilizer->value, 4);
    s.stabilizer.reset();
    EXPECT_FALSE(s.clamped().stabilizer.has_value());
}

TEST(BrushSettingsClamped, InRangeSettingsAreUnchanged) {
    for (ToolType t :
         {ToolType::Pencil, ToolType::Eraser, ToolType::Brush, ToolType::WaterColor, ToolType::Marker, ToolType::Blur, ToolType::Smudge}) {
        const BrushSettings s = defaultSettings(t);
        EXPECT_EQ(s.clamped(), s) << int(t);
    }
}

TEST(ToolType, KeysRoundTrip) {
    QSet<QString> keys;
    for (ToolType t : {ToolType::Pencil, ToolType::Brush, ToolType::WaterColor, ToolType::Marker, ToolType::Eraser, ToolType::Blur,
                       ToolType::Smudge, ToolType::PixelArtPen, ToolType::Clone, ToolType::SelectionPen, ToolType::SelectionEraser}) {
        EXPECT_EQ(toolTypeFromKey(toolTypeKey(t)), t);
        keys.insert(toolTypeKey(t));
    }
    EXPECT_EQ(keys.size(), 11);
    EXPECT_EQ(toolTypeKey(ToolType::WaterColor), "watercolor");
    EXPECT_EQ(toolTypeKey(ToolType::PixelArtPen), "pixelart");
    EXPECT_EQ(toolTypeKey(ToolType::SelectionPen), "select");
    EXPECT_EQ(toolTypeKey(ToolType::SelectionEraser), "deselect");
    EXPECT_FALSE(toolTypeFromKey("airbrush").has_value());
    EXPECT_FALSE(toolTypeFromKey("").has_value());
}

TEST(DefaultSettings, EveryToolPassesItsOwnLimits) {
    for (ToolType t : {ToolType::Pencil, ToolType::Brush, ToolType::WaterColor, ToolType::Marker, ToolType::Eraser, ToolType::Blur,
                       ToolType::Smudge, ToolType::PixelArtPen, ToolType::Clone, ToolType::SelectionPen, ToolType::SelectionEraser}) {
        const BrushSettings s = defaultSettings(t);
        EXPECT_EQ(s.tool, t);
        EXPECT_EQ(s.clamped(), s) << toolTypeKey(t).toStdString();
    }
}

// Feedback: "Change S1-S7 to 16-20, remove the last 2 stabilizers".
TEST(StabilizerLevelLabel, IsTheNumberFrom0To20) {
    EXPECT_EQ(StabilizerLevel::kMax, 20);
    EXPECT_EQ((StabilizerLevel{0}).label(), "0");
    EXPECT_EQ((StabilizerLevel{16}).label(), "16");
    EXPECT_EQ((StabilizerLevel{20}).label(), "20");
    for (int value = 0; value <= StabilizerLevel::kMax; ++value)
        EXPECT_EQ(StabilizerLevel::fromLabel(StabilizerLevel{value}.label()), StabilizerLevel{value}) << value;
    EXPECT_FALSE(StabilizerLevel::fromLabel("21").has_value());
    EXPECT_FALSE(StabilizerLevel::fromLabel("S-8").has_value());
    EXPECT_FALSE(StabilizerLevel::fromLabel("S-0").has_value());
}

// The reference cases' names: S-1 to S-5 are 16 to 20, and S-6 and S-7, which are gone, are 20.
TEST(StabilizerLevelLabel, TheSpecsStrongNamesAre16To20) {
    EXPECT_EQ(StabilizerLevel::fromLabel("S-1"), StabilizerLevel{16});
    EXPECT_EQ(StabilizerLevel::fromLabel("S-5"), StabilizerLevel{20});
    EXPECT_EQ(StabilizerLevel::fromLabel("S-6"), StabilizerLevel{20});
    EXPECT_EQ(StabilizerLevel::fromLabel("S-7"), StabilizerLevel{20});
}

// A saved S-6 or S-7, stored as 21 or 22, becomes 20.
TEST(StabilizerLevel, ASavedLevelPast20Becomes20) {
    BrushSettings s;
    s.stabilizer = StabilizerLevel{22};
    EXPECT_EQ(s.clamped().stabilizer, StabilizerLevel{20});
    s.stabilizer = StabilizerLevel{21};
    EXPECT_EQ(s.clamped().stabilizer, StabilizerLevel{20});
}

// The choice; the reference cases still render at 3.
TEST(StabilizerLevel, TheGlobalLevelStartsAt4) {
    EXPECT_EQ(BrushOptions{}.stabilizer, StabilizerLevel{4});
}

// Water Color and the Marker differ from it only in how they mix and their tips.
TEST(DefaultSettings, EveryToolHasTheSameNeutralSet) {
    BrushSettings neutral = defaultSettings(ToolType::Eraser);
    EXPECT_EQ(neutral.size, 200) << "20 px";
    EXPECT_EQ(neutral.density, 100);
    EXPECT_EQ(neutral.blurWidth, 0);
    EXPECT_EQ(neutral.blending, 0);
    EXPECT_FALSE(neutral.pressureSize || neutral.pressureDensity);
    // Water Color, the Marker, Blur and Smudge have their own starting values.
    for (ToolType t : {ToolType::PixelArtPen, ToolType::Clone, ToolType::SelectionPen, ToolType::SelectionEraser}) {
        BrushSettings unmixed = defaultSettings(t);
        unmixed.blending = unmixed.dilution = unmixed.persistence = unmixed.blurPressure = 0;
        unmixed.pressureBlending = false;
        unmixed.edge = neutral.edge;
        unmixed.sharpness = neutral.sharpness;
        neutral.tool = t;
        EXPECT_EQ(unmixed, neutral) << toolTypeKey(t).toStdString();
    }
}

TEST(DefaultSettings, ThePencilChangesWidthWithPressureOnly) {
    const BrushSettings pencil = defaultSettings(ToolType::Pencil);
    EXPECT_TRUE(pencil.pressureSize);
    EXPECT_FALSE(pencil.pressureDensity);
    EXPECT_EQ(pencil.minSize, 0);
    EXPECT_EQ(pencil.sensitivitySize, 100);
    EXPECT_EQ(pencil.sharpness, 50);
}

// The Brush's own starting values.
TEST(DefaultSettings, TheBrushHasItsOwnStartingValues) {
    const BrushSettings brush = defaultSettings(ToolType::Brush);
    EXPECT_EQ(brush.size, 500);
    EXPECT_EQ(brush.dilution, 0);
    EXPECT_EQ(brush.sharpness, 50);
    EXPECT_EQ(brush.edge, kEdgePresets - 1);
    EXPECT_TRUE(brush.pressureSize);
    EXPECT_TRUE(brush.pressureDensity);
    EXPECT_EQ(brush.sensitivityDensity, 160);
    EXPECT_EQ(brush.minSize, 50);
    EXPECT_EQ(brush.sensitivitySize, 100);
    EXPECT_EQ(brush.density, 100);
    EXPECT_EQ(brush.blending, 50);
    EXPECT_TRUE(brush.pressureBlending);
    EXPECT_FALSE(brush.pressureDilution);
    EXPECT_EQ(brush.persistence, 50);
    EXPECT_EQ(brush.blurPressure, 50);
}

TEST(BrushSettingsClamped, TheMixingSettingsAreClampedToZeroToHundred) {
    BrushSettings s = defaultSettings(ToolType::Brush);
    s.dilution = 101, s.blending = -3, s.persistence = 300;
    EXPECT_EQ(s.clamped().dilution, 100);
    EXPECT_EQ(s.clamped().blending, 0);
    EXPECT_EQ(s.clamped().persistence, 100);
}

// The Resets in the Tools panel's Tip shape and Texture boxes: each puts back the tool's starting values of its own box's
// settings, every one of them, Water Color's own Dens. 50 for a brush shape among them, and leaves every other setting as it was, the
// other box's too.
namespace {

BrushSettings everythingOffItsStart(ToolType tool) {
    BrushSettings s = defaultSettings(tool);
    s.form = QStringLiteral("Flat Nib");
    s.formStrength = 7, s.formScale = 300, s.formInvert = true, s.formSharpen = true, s.formEraseLow = false;
    s.bristleMinimum = 40, s.bristleSize = 30, s.bristleRandomize = 9, s.bristleDirection = 2;
    s.shapeDensity = 12, s.shapeAngleControl = 0, s.shapeAngle = -45, s.shapeAngleJitter = 60, s.shapeScaling = 150;
    s.shapeSizeJitter = 20, s.shapeSpacing = 1, s.shapeScattering = 30, s.shapeHighQuality = true;
    s.texture = QStringLiteral("Canvas");
    s.textureStrength = 3, s.textureScale = 250, s.textureScratch = 70, s.textureInvert = true, s.textureEraseLow = false;
    s.size = 1234, s.minSize = 33, s.density = 44, s.edge = 1, s.sharpness = 77, s.opacity = 99, s.blending = 11;
    s.pressureSize = !s.pressureSize, s.antiRipple = false, s.stabilizer = StabilizerLevel{5};
    return s;
}

}

TEST(BrushSettingsReset, TheTipShapesResetPutsBackItsOwnSettingsAndNothingElse) {
    for (const ToolType tool : {ToolType::Brush, ToolType::WaterColor, ToolType::Pencil}) {
        BrushSettings s = everythingOffItsStart(tool);
        const BrushSettings before = s;
        resetTipShape(s);
        const BrushSettings start = defaultSettings(tool);
        BrushSettings expected = before;
        expected.form = start.form;
        expected.formStrength = start.formStrength, expected.formScale = start.formScale, expected.formInvert = start.formInvert;
        expected.formSharpen = start.formSharpen, expected.formEraseLow = start.formEraseLow;
        expected.bristleMinimum = start.bristleMinimum, expected.bristleSize = start.bristleSize;
        expected.bristleRandomize = start.bristleRandomize, expected.bristleDirection = start.bristleDirection;
        expected.shapeDensity = start.shapeDensity, expected.shapeAngleControl = start.shapeAngleControl,
        expected.shapeAngle = start.shapeAngle;
        expected.shapeAngleJitter = start.shapeAngleJitter, expected.shapeScaling = start.shapeScaling;
        expected.shapeSizeJitter = start.shapeSizeJitter, expected.shapeSpacing = start.shapeSpacing;
        expected.shapeScattering = start.shapeScattering, expected.shapeHighQuality = start.shapeHighQuality;
        EXPECT_EQ(s, expected) << "tool " << int(tool);
        EXPECT_EQ(s.texture, QStringLiteral("Canvas")) << "the texture's stay";
        EXPECT_EQ(s.textureStrength, 3);
    }
    BrushSettings water = defaultSettings(ToolType::WaterColor);
    water.shapeDensity = 100;
    resetTipShape(water);
    EXPECT_EQ(water.shapeDensity, 50) << "Water Color's own start";
    BrushSettings brush = defaultSettings(ToolType::Brush);
    brush.formStrength = 100;
    resetTipShape(brush);
    EXPECT_EQ(brush.formStrength, 50) << "the Brush's own start";
}

TEST(BrushSettingsReset, TheTexturesResetPutsBackItsOwnSettingsAndNothingElse) {
    for (const ToolType tool : {ToolType::Brush, ToolType::WaterColor, ToolType::Pencil}) {
        BrushSettings s = everythingOffItsStart(tool);
        const BrushSettings before = s;
        resetTexture(s);
        const BrushSettings start = defaultSettings(tool);
        BrushSettings expected = before;
        expected.texture = start.texture;
        expected.textureStrength = start.textureStrength, expected.textureScale = start.textureScale;
        expected.textureScratch = start.textureScratch, expected.textureInvert = start.textureInvert;
        expected.textureEraseLow = start.textureEraseLow;
        EXPECT_EQ(s, expected) << "tool " << int(tool);
        EXPECT_EQ(s.form, QStringLiteral("Flat Nib")) << "the tip shape's stay";
        EXPECT_EQ(s.shapeSpacing, 1);
    }
    BrushSettings brush = defaultSettings(ToolType::Brush);
    brush.textureStrength = 10;
    resetTexture(brush);
    EXPECT_EQ(brush.textureStrength, 95) << "the Brush's own start";
}
