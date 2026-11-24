#include "core/document/NewCanvas.h"
#include "core/io/DocumentFormat.h"

#include <gtest/gtest.h>

using namespace doodlebold;

namespace {

NewCanvasModel a4At350() {
    NewCanvasModel m;
    m.applyPreset(defaultCanvasPresets()[7], 7);
    return m;
}

}

TEST(NewCanvas, StartsFromTheGivenValues) {
    NewCanvasValues v;
    v.pixels = QSize(128, 96);
    const NewCanvasModel m(v);
    EXPECT_EQ(m.pixelsText(), QStringLiteral("128 × 96"));
    EXPECT_EQ(m.metricsText(), QStringLiteral("45.2mm × 33.9mm (72 pixels/inch)"));
    EXPECT_EQ(formatLength(m.width(), m.sizeUnit()), QStringLiteral("128"));
    EXPECT_EQ(m.activePreset(), -1);
}

TEST(NewCanvas, FollowsTheCalculationSequence) {
    NewCanvasModel m = a4At350();
    EXPECT_EQ(m.pixelsText(), QStringLiteral("2894 × 4093"));
    EXPECT_EQ(m.metricsText(), QStringLiteral("210mm × 297mm (350 pixels/inch)"));
    EXPECT_EQ(m.sizeUnit(), SizeUnit::Millimeters);

    m.setSizeUnit(SizeUnit::Pixels);
    m.setWidth(1000);
    EXPECT_EQ(m.pixelsText(), QStringLiteral("1000 × 4093"));
    EXPECT_EQ(m.metricsText(), QStringLiteral("72.6mm × 297mm (350 pixels/inch)"));
    m.setHeight(500);
    EXPECT_EQ(m.pixelsText(), QStringLiteral("1000 × 500"));
    EXPECT_EQ(m.metricsText(), QStringLiteral("72.6mm × 36.3mm (350 pixels/inch)"));

    m.setResolutionValue(72);
    EXPECT_EQ(m.pixelsText(), QStringLiteral("1000 × 500"));
    EXPECT_EQ(m.metricsText(), QStringLiteral("352.8mm × 176.4mm (72 pixels/inch)"));
    m.setResolutionValue(300);
    EXPECT_EQ(m.pixelsText(), QStringLiteral("1000 × 500"));
    EXPECT_EQ(m.metricsText(), QStringLiteral("84.7mm × 42.3mm (300 pixels/inch)"));

    m.setSizeUnit(SizeUnit::Inches);
    EXPECT_EQ(formatLength(m.width(), m.sizeUnit()), QStringLiteral("3.333"));
    EXPECT_EQ(formatLength(m.height(), m.sizeUnit()), QStringLiteral("1.667"));
    EXPECT_EQ(m.metricsText(), QStringLiteral("3.333in × 1.667in (300 pixels/inch)"));
    m.setSizeUnit(SizeUnit::Centimeters);
    EXPECT_EQ(formatLength(m.width(), m.sizeUnit()), QStringLiteral("8.47"));
    EXPECT_EQ(formatLength(m.height(), m.sizeUnit()), QStringLiteral("4.23"));
    EXPECT_EQ(m.metricsText(), QStringLiteral("8.47cm × 4.23cm (300 pixels/inch)"));
    m.setSizeUnit(SizeUnit::Millimeters);
    EXPECT_EQ(formatLength(m.width(), m.sizeUnit()), QStringLiteral("84.7"));
    EXPECT_EQ(formatLength(m.height(), m.sizeUnit()), QStringLiteral("42.3"));

    m.setWidth(100);
    EXPECT_EQ(m.pixelsText(), QStringLiteral("1181 × 500"));
    EXPECT_EQ(m.metricsText(), QStringLiteral("100mm × 42.3mm (300 pixels/inch)"));
    m.setHeight(50.5);
    EXPECT_EQ(m.pixelsText(), QStringLiteral("1181 × 596"));
    EXPECT_EQ(m.metricsText(), QStringLiteral("100mm × 50.5mm (300 pixels/inch)"));

    m.setSizeUnit(SizeUnit::Pixels);
    EXPECT_EQ(m.width(), 1181);
    EXPECT_EQ(m.height(), 596);
}

TEST(NewCanvas, PresetsGiveTheirPixelSizes) {
    const CanvasPresetList presets = defaultCanvasPresets();
    ASSERT_EQ(presets.size(), 8u);
    EXPECT_FALSE(presets[0].separator);
    EXPECT_TRUE(presets[2].separator);
    EXPECT_TRUE(presets[5].separator);
    struct Case {
        int index;
        const char* name;
        const char* pixels;
        const char* metrics;
    };
    const Case cases[] = {
        {0, "512 × 512 - 72ppi", "512 × 512", "180.6mm × 180.6mm (72 pixels/inch)"},
        {1, "1024 × 1024 - 72ppi", "1024 × 1024", "361.2mm × 361.2mm (72 pixels/inch)"},
        {3, "A5 - 300ppi", "1748 × 2480", "148mm × 210mm (300 pixels/inch)"},
        {4, "A5 - 350ppi", "2039 × 2894", "148mm × 210mm (350 pixels/inch)"},
        {6, "A4 - 300ppi", "2480 × 3508", "210mm × 297mm (300 pixels/inch)"},
        {7, "A4 - 350ppi", "2894 × 4093", "210mm × 297mm (350 pixels/inch)"},
    };
    for (const Case& c : cases) {
        EXPECT_EQ(presets[static_cast<std::size_t>(c.index)].name, QString::fromUtf8(c.name));
        NewCanvasModel m;
        m.applyPreset(presets[static_cast<std::size_t>(c.index)], c.index);
        EXPECT_EQ(m.pixelsText(), QString::fromUtf8(c.pixels)) << c.name;
        EXPECT_EQ(m.metricsText(), QString::fromUtf8(c.metrics)) << c.name;
        EXPECT_EQ(m.activePreset(), c.index);
    }
}

TEST(NewCanvas, MillimetresRoundToTheNearestPixelNotDown) {
    EXPECT_EQ(toPixels(182, SizeUnit::Millimeters, 300), 2150);
    EXPECT_EQ(toPixels(297, SizeUnit::Millimeters, 300), 3508);
    EXPECT_EQ(toPixels(182, SizeUnit::Millimeters, 350), 2508);
    EXPECT_EQ(toPixels(257, SizeUnit::Millimeters, 350), 3541);
    EXPECT_EQ(toPixels(100, SizeUnit::Millimeters, 300), 1181);
    EXPECT_EQ(toPixels(50.5, SizeUnit::Millimeters, 300), 596);
    EXPECT_EQ(toPixels(8.5, SizeUnit::Inches, 300), 2550);
    EXPECT_EQ(toPixels(21, SizeUnit::Centimeters, 300), 2480);
    EXPECT_EQ(toPixels(10.4, SizeUnit::Pixels, 300), 10);
}

TEST(NewCanvas, PixelSizeStaysWithinWhatDoodleboldOpens) {
    EXPECT_EQ(toPixels(0, SizeUnit::Pixels, 72), 1);
    EXPECT_EQ(toPixels(-5, SizeUnit::Millimeters, 72), 1);
    EXPECT_EQ(toPixels(1e9, SizeUnit::Pixels, 72), kMaxCanvasSide);
    EXPECT_EQ(toPixels(1e6, SizeUnit::Inches, 300), kMaxCanvasSide);
    NewCanvasModel m;
    EXPECT_FALSE(m.limited());
    m.setWidth(100000);
    EXPECT_EQ(m.pixels().width(), kMaxCanvasSide);
    EXPECT_TRUE(m.limited()) << "the dialog says so";
    m.setWidth(16384);
    EXPECT_FALSE(m.limited()) << "16384 is the largest side, not beyond it";
    m.setHeight(16385);
    EXPECT_EQ(m.pixels().height(), kMaxCanvasSide);
    EXPECT_TRUE(m.limited());
    m.setHeight(800);
    EXPECT_FALSE(m.limited());
}

TEST(NewCanvas, AResolutionThatTakesAPrintingSizeBeyondTheLimitSaysSo) {
    NewCanvasModel m;
    m.setSizeUnit(SizeUnit::Inches);
    m.setWidth(10);
    m.setHeight(10);
    m.setResolutionValue(1000);
    EXPECT_EQ(m.pixels(), QSize(10000, 10000));
    EXPECT_FALSE(m.limited());
    m.setResolutionValue(2000);
    EXPECT_EQ(m.pixels(), QSize(kMaxCanvasSide, kMaxCanvasSide));
    EXPECT_TRUE(m.limited());
}

TEST(NewCanvas, SwitchingTheResolutionUnitConvertsTheValueAndKeepsThePixels) {
    NewCanvasModel m = a4At350();
    const QString pixels = m.pixelsText();
    m.setResolutionUnit(ResolutionUnit::PerCentimeter);
    EXPECT_EQ(formatResolution(m.resolutionValue()), QStringLiteral("137.8"));
    EXPECT_EQ(m.pixelsText(), pixels);
    EXPECT_EQ(m.metricsText(), QStringLiteral("210mm × 297mm (137.8 pixels/cm)"));
    m.setResolutionUnit(ResolutionUnit::PerInch);
    EXPECT_EQ(formatResolution(m.resolutionValue()), QStringLiteral("350"));
    m.setResolutionUnit(ResolutionUnit::PerCentimeter);
    m.setSizeUnit(SizeUnit::Pixels);
    m.setResolutionValue(100);
    EXPECT_NEAR(m.resolutionPpi(), 254.0, 1e-9);
}

TEST(NewCanvas, ChangingTheResolutionOfAPhysicalSizeKeepsThePrintingSize) {
    NewCanvasModel m = a4At350();
    m.setResolutionValue(300);
    EXPECT_EQ(m.pixelsText(), QStringLiteral("2480 × 3508"));
    EXPECT_EQ(m.metricsText(), QStringLiteral("210mm × 297mm (300 pixels/inch)"));
}

TEST(NewCanvas, EditingAnyFieldResetsThePresetCombo) {
    const CanvasPresetList presets = defaultCanvasPresets();
    const auto fresh = [&] {
        NewCanvasModel m;
        m.applyPreset(presets[6], 6);
        EXPECT_EQ(m.activePreset(), 6);
        return m;
    };
    NewCanvasModel m = fresh();
    m.setWidth(100);
    EXPECT_EQ(m.activePreset(), -1);
    m = fresh();
    m.setHeight(100);
    EXPECT_EQ(m.activePreset(), -1);
    m = fresh();
    m.setSizeUnit(SizeUnit::Pixels);
    EXPECT_EQ(m.activePreset(), -1);
    m = fresh();
    m.setResolutionValue(200);
    EXPECT_EQ(m.activePreset(), -1);
    m = fresh();
    m.setResolutionUnit(ResolutionUnit::PerCentimeter);
    EXPECT_EQ(m.activePreset(), -1);
    m = fresh();
    m.setBackground(CanvasBackground{BackgroundMode::Black});
    EXPECT_EQ(m.activePreset(), 6);
    m.applyPreset(CanvasPreset::makeSeparator(), 2);
    EXPECT_EQ(m.activePreset(), 6);
}

TEST(NewCanvas, ASelectedPresetLeavesTheBackgroundAloneUnlessItHasOne) {
    NewCanvasModel m;
    m.setBackground(CanvasBackground{BackgroundMode::TransparencyDarkChecker});
    m.applyPreset(defaultCanvasPresets()[0], 0);
    EXPECT_EQ(m.background().mode, BackgroundMode::TransparencyDarkChecker);
    CanvasPreset withBackground = defaultCanvasPresets()[0];
    withBackground.background = CanvasBackground{BackgroundMode::Color, QColor(1, 2, 3)};
    m.applyPreset(withBackground, 0);
    EXPECT_EQ(m.background(), *withBackground.background);
}

TEST(NewCanvas, PresetListsSurviveTheirSettingsForm) {
    CanvasPresetList presets = defaultCanvasPresets();
    CanvasPreset mine;
    mine.name = QStringLiteral("Poster");
    mine.width = 11.7;
    mine.height = 16.5;
    mine.sizeUnit = SizeUnit::Inches;
    mine.resolution = 118.1;
    mine.resolutionUnit = ResolutionUnit::PerCentimeter;
    mine.background = CanvasBackground{BackgroundMode::TransparencyColor, QColor(9, 8, 7)};
    presets.insert(presets.begin() + 3, mine);
    const auto back = presetsFromJson(presetsToJson(presets));
    ASSERT_TRUE(back.has_value());
    EXPECT_EQ(*back, presets);
}

TEST(NewCanvas, DamagedPresetSettingsAreRefused) {
    EXPECT_FALSE(presetsFromJson(QJsonArray{1, 2}).has_value());
    QJsonObject o{{"name", "x"}, {"width", 0}, {"height", 5}, {"sizeUnit", "mm"}, {"resolution", 72}, {"resolutionUnit", "ppi"}};
    EXPECT_FALSE(presetsFromJson(QJsonArray{o}).has_value());
    o["width"] = 5;
    EXPECT_TRUE(presetsFromJson(QJsonArray{o}).has_value());
    o["sizeUnit"] = "furlongs";
    EXPECT_FALSE(presetsFromJson(QJsonArray{o}).has_value());
    o["sizeUnit"] = "mm";
    o["background"] = "mauve";
    EXPECT_FALSE(presetsFromJson(QJsonArray{o}).has_value());
    EXPECT_TRUE(presetsFromJson(QJsonArray{}).has_value());
}

TEST(NewCanvas, RememberedValuesRoundTripAndFallBackPerField) {
    NewCanvasValues v;
    v.pixels = QSize(2150, 3035);
    v.resolution = 300;
    v.sizeUnit = SizeUnit::Millimeters;
    v.resolutionUnit = ResolutionUnit::PerCentimeter;
    v.background = CanvasBackground{BackgroundMode::TransparencyColor, QColor(255, 128, 0)};
    EXPECT_EQ(newCanvasValuesFromJson(newCanvasValuesToJson(v)), v);
    EXPECT_EQ(NewCanvasModel(v).values(), v);
    QJsonObject broken = newCanvasValuesToJson(v);
    broken["width"] = -4;
    broken["sizeUnit"] = "?";
    broken["background"] = "?";
    const NewCanvasValues r = newCanvasValuesFromJson(broken);
    EXPECT_EQ(r.pixels, NewCanvasValues{}.pixels);
    EXPECT_EQ(r.sizeUnit, SizeUnit::Pixels);
    EXPECT_EQ(r.background.mode, BackgroundMode::White);
    EXPECT_EQ(r.resolution, 300);
    EXPECT_EQ(r.background.color, QColor(255, 128, 0));
    EXPECT_EQ(newCanvasValuesFromJson({}), NewCanvasValues{});
}

TEST(NewCanvas, UnitAndResolutionLabelsAreTheExpectedOnes) {
    EXPECT_EQ(sizeUnitLabel(SizeUnit::Pixels), QStringLiteral("pixels"));
    EXPECT_EQ(sizeUnitLabel(SizeUnit::Inches), QStringLiteral("inches"));
    EXPECT_EQ(sizeUnitLabel(SizeUnit::Centimeters), QStringLiteral("cm"));
    EXPECT_EQ(sizeUnitLabel(SizeUnit::Millimeters), QStringLiteral("mm"));
    EXPECT_EQ(resolutionUnitLabel(ResolutionUnit::PerInch), QStringLiteral("pixels/inch"));
    EXPECT_EQ(resolutionUnitLabel(ResolutionUnit::PerCentimeter), QStringLiteral("pixels/cm"));
    EXPECT_EQ(formatLength(210.0, SizeUnit::Millimeters), QStringLiteral("210"));
    EXPECT_EQ(formatLength(84.67, SizeUnit::Millimeters), QStringLiteral("84.7"));
    EXPECT_EQ(formatLength(3.3333, SizeUnit::Inches), QStringLiteral("3.333"));
    EXPECT_EQ(formatLength(1181.4, SizeUnit::Pixels), QStringLiteral("1181"));
}
