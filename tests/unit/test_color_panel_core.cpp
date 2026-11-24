#include "../SignalCounter.h"
#include "core/color/ColorAssets.h"
#include "core/color/ColorAssetsStore.h"
#include "core/color/ColorModels.h"
#include "core/color/ColorState.h"

#include <QCoreApplication>
#include <QJsonArray>
#include <QTemporaryDir>

#include <gtest/gtest.h>

#include <cmath>

using namespace doodlebold;
using doodlebold::test::SignalCounter;

namespace {

int pct(double v) {
    return int(std::lround(v * 100.0));
}

struct Reading {
    QColor colour;
    int hue = 0, hsvS = 0, hsvV = 0, hslS = 0, hslL = 0;
};

const Reading kReadings[] = {
    {QColor(64, 64, 0), 60, 100, 25, 100, 13},  {QColor(255, 0, 0), 0, 100, 100, 100, 50},   {QColor(255, 128, 0), 30, 100, 100, 100, 50},
    {QColor(128, 64, 32), 20, 75, 50, 60, 31},  {QColor(10, 200, 100), 148, 95, 78, 90, 41}, {QColor(255, 255, 128), 60, 50, 100, 100, 75},
    {QColor(40, 80, 160), 220, 75, 63, 60, 39}, {QColor(250, 20, 120), 334, 92, 98, 96, 53},
};

}

TEST(ColorModels, SlidersReadTheExpectedNumbers) {
    for (const Reading& r : kReadings) {
        SCOPED_TRACE(qPrintable(r.colour.name()));
        EXPECT_EQ(int(std::lround(color::hueOf(r.colour, -1))), r.hue);
        EXPECT_EQ(pct(color::hsvSaturation(r.colour)), r.hsvS);
        EXPECT_EQ(pct(color::value(r.colour)), r.hsvV);
        EXPECT_EQ(pct(color::hslSaturation(r.colour)), r.hslS);
        EXPECT_EQ(pct(color::lightness(r.colour)), r.hslL);
    }
}

TEST(ColorModels, GreysKeepTheLastHue) {
    EXPECT_EQ(color::hueOf(QColor(200, 200, 200), 20.0), 20.0);
    EXPECT_EQ(color::hueOf(Qt::black, 334.0), 334.0);
    ColorState state;
    state.setColor(QColor(128, 64, 32));
    state.setColor(QColor(200, 200, 200));
    EXPECT_NEAR(state.hue(), 20.0, 0.01);
    state.setColor(Qt::black, 300.0);
    EXPECT_EQ(state.hue(), 300.0);
    EXPECT_EQ(state.color(), QColor(Qt::black));
}

TEST(ColorModels, SlidersRoundTripInEveryMode) {
    for (color::SliderMode mode : {color::SliderMode::Hsv, color::SliderMode::Hsl})
        for (const Reading& r : kReadings) {
            const color::SliderValues v = color::slidersFor(mode, r.colour, 0.0);
            const QColor back = color::fromSliders(mode, v);
            EXPECT_NEAR(back.red(), r.colour.red(), 1) << r.colour.name().toStdString();
            EXPECT_NEAR(back.green(), r.colour.green(), 1) << r.colour.name().toStdString();
            EXPECT_NEAR(back.blue(), r.colour.blue(), 1) << r.colour.name().toStdString();
        }
}

TEST(ColorAssets, HistoryPutsTheNewestFirstWithoutRepeats) {
    ColorAssets a;
    SignalCounter changed(&a, &ColorAssets::historyChanged);
    a.addToHistory(Qt::red);
    a.addToHistory(Qt::blue);
    a.addToHistory(Qt::black);
    a.addToHistory(Qt::red);
    EXPECT_EQ(a.history(), (std::vector<QColor>{Qt::red, Qt::black, Qt::blue}));
    a.addToHistory(Qt::red);
    EXPECT_EQ(changed.count(), 4);
    a.addToHistory(QColor(0, 0, 0, 0));
    EXPECT_EQ(a.history().size(), 3u);
    for (int i = 0; i < 100; ++i)
        a.addToHistory(QColor(i, 0, 0));
    EXPECT_EQ(int(a.history().size()), ColorAssets::kHistorySize);
}

TEST(ColorAssets, SwatchesRegisterDeleteAndSets) {
    ColorAssets a;
    a.setSwatch(3, QColor(0, 160, 0));
    EXPECT_EQ(a.currentSet().cells[3], QColor(0, 160, 0));
    a.setSwatch(3, std::nullopt);
    EXPECT_FALSE(a.currentSet().cells[3]);
    EXPECT_EQ(a.addSwatchSet(), 1);
    EXPECT_EQ(a.currentSet().name, "Swatch2");
    a.setSwatch(0, Qt::red);
    a.setCurrentSwatchSet(0);
    EXPECT_FALSE(a.currentSet().cells[0]);
    EXPECT_TRUE(a.removeSwatchSet(0));
    EXPECT_EQ(a.currentSet().cells[0], QColor(Qt::red));
    EXPECT_FALSE(a.removeSwatchSet(0)) << "the last set stays";
}

TEST(ColorAssets, JsonRoundTripKeepsEverything) {
    ColorAssets a;
    a.addToHistory(Qt::red);
    a.addToHistory(QColor(1, 2, 3));
    a.setSwatch(7, QColor(9, 8, 7));
    a.addSwatchSet();
    a.renameSwatchSet(1, "Skin");
    a.setSliderMode(color::SliderMode::Hsl);
    a.setSwatchSize(SwatchSize::Small);
    ColorAssets b;
    b.fromJson(a.toJson());
    EXPECT_EQ(b.history(), a.history());
    EXPECT_EQ(b.swatchSetCount(), 2);
    EXPECT_EQ(b.swatchSet(0), a.swatchSet(0));
    EXPECT_EQ(b.swatchSet(1).name, "Skin");
    EXPECT_EQ(b.currentSwatchSet(), 1);
    EXPECT_FALSE(a.toJson().contains(QStringLiteral("scratchPads"))) << "the scratch pad is gone";
    EXPECT_EQ(b.sliderMode(), color::SliderMode::Hsl);
    EXPECT_EQ(b.swatchSize(), SwatchSize::Small);
}

// Settings saved before the scratch pad was removed still load: its entries are passed over.
TEST(ColorAssets, OldSettingsWithScratchPadsStillLoad) {
    ColorAssets a;
    a.fromJson(QJsonObject{{"history", QJsonArray{QStringLiteral("#102030")}},
                           {"scratchPads", QJsonArray{QStringLiteral("Pad")}},
                           {"currentScratchPad", 0},
                           {"padBrushSize", 12},
                           {"padEraser", true}});
    ASSERT_EQ(a.history().size(), 1u);
    EXPECT_EQ(a.history().front(), QColor(0x10, 0x20, 0x30));
    EXPECT_FALSE(a.toJson().contains(QStringLiteral("padBrushSize")));
}

TEST(ColorAssets, BrokenJsonKeepsDefaults) {
    ColorAssets a;
    a.fromJson(QJsonObject{{"history", 5}, {"swatchSets", QJsonArray{3}}, {"scratchPads", QJsonArray{}}, {"sliderMode", "x"}});
    EXPECT_TRUE(a.history().empty());
    EXPECT_EQ(a.swatchSetCount(), 1);
    EXPECT_EQ(a.sliderMode(), color::SliderMode::Hsv);
    EXPECT_EQ(a.swatchSize(), SwatchSize::Large);
}

TEST(ColorAssets, TheSliderModeIsSavedAndRead) {
    ColorAssets a;
    a.setSliderMode(color::SliderMode::Hsv);
    a.fromJson(a.toJson());
    EXPECT_EQ(a.sliderMode(), color::SliderMode::Hsv);
    a.fromJson(QJsonObject{{"sliderMode", "hsl"}});
    EXPECT_EQ(a.sliderMode(), color::SliderMode::Hsl);
}

TEST(ColorAssetsStore, SavesAfterAChangeAndLoadsBack) {
    if (!QCoreApplication::instance()) {
        static int argc = 1;
        static char name[] = "doodlebold_unit_tests";
        static char* argv[] = {name};
        new QCoreApplication(argc, argv);
    }
    QTemporaryDir dir;
    const QString path = dir.filePath("config/colors.json");
    ColorAssets a;
    ColorAssetsStore store(&a, path);
    EXPECT_TRUE(store.load()) << "no file yet: defaults";
    a.addToHistory(Qt::green);
    EXPECT_TRUE(store.savePending());
    store.flush();
    ColorAssets b;
    ColorAssetsStore other(&b, path);
    ASSERT_TRUE(other.load());
    EXPECT_EQ(b.history(), a.history());
}
