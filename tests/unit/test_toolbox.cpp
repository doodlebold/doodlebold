#include "../SignalCounter.h"
#include "core/brush/BrushResources.h"
#include "core/tools/SizeList.h"
#include "core/tools/ToolBox.h"
#include "core/tools/ToolBoxJson.h"
#include "core/tools/ToolBoxStore.h"

#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTemporaryDir>

#include <gtest/gtest.h>

#include <functional>
#include <memory>

using namespace doodlebold;
using doodlebold::test::SignalCounter;

namespace {

// The Bucket sits where the Blur was, and the Blur comes after the Clone Stamp.
constexpr SlotRef kPencil{0, 0}, kBrush{0, 1}, kWater{0, 2}, kMarker{0, 3}, kEraser{0, 4}, kBucket{0, 5}, kPixelPen{0, 7}, kClone{0, 8},
    kBlur{0, 9}, kFirstEmpty{0, 10};
constexpr int kBasicTools = 10;

class ToolBoxTest : public testing::Test {
protected:
    static void SetUpTestSuite() {
        if (!QCoreApplication::instance()) {
            static int argc = 1;
            static char name[] = "doodlebold_unit_tests";
            static char* argv[] = {name, nullptr};
            s_app = std::make_unique<QCoreApplication>(argc, argv);
        }
    }
    static void TearDownTestSuite() { s_app.reset(); }

    static std::unique_ptr<QCoreApplication> s_app;
    ToolBox tools;
};
std::unique_ptr<QCoreApplication> ToolBoxTest::s_app;

std::vector<QString> names(const ToolBox& t, int group) {
    std::vector<QString> out;
    for (const auto& cell : t.group(group).cells)
        if (cell)
            out.push_back(cell->name);
    return out;
}

}

TEST_F(ToolBoxTest, DefaultSetHasBasicAndCustomGroups) {
    ASSERT_EQ(tools.groupCount(), 2);
    EXPECT_EQ(tools.group(0).name, "Basic");
    EXPECT_EQ(tools.group(1).name, "Custom");
    EXPECT_EQ(names(tools, 0), (std::vector<QString>{"Pencil", "Brush", "Water Color", "Marker", "Eraser", "Bucket", "Smudge",
                                                     "Pixel Art Pen", "Clone Stamp", "Blur"}));
    EXPECT_EQ(tools.slot(kBucket)->type(), ToolType::Bucket);
    EXPECT_TRUE(names(tools, 1).empty());
    EXPECT_EQ(tools.currentSlot(), kPencil);
    EXPECT_EQ(tools.settings(), defaultSettings(ToolType::Pencil));
    EXPECT_EQ(tools.color(), QColor(Qt::black));
}

TEST_F(ToolBoxTest, TrayAlwaysHasAnEmptyCellAndWholeRows) {
    for (int g = 0; g < tools.groupCount(); ++g) {
        EXPECT_EQ(tools.group(g).cells.size() % ToolBox::kColumns, 0u);
        EXPECT_FALSE(tools.group(g).cells.back().has_value());
    }
    for (int i = 0; i < 8; ++i)
        ASSERT_TRUE(tools.createSlot({1, i}, ToolType::Pencil).ok());
    EXPECT_GE(tools.group(1).cells.size(), 12u);
    EXPECT_FALSE(tools.group(1).cells.back().has_value());
}

TEST_F(ToolBoxTest, SettingsEditsGoToTheCurrentSlotOnly) {
    SignalCounter changed(&tools, &ToolBox::settingsChanged);
    BrushSettings s = tools.settings();
    s.size = 777;
    tools.setSettings(s);
    EXPECT_EQ(changed.count(), 1);
    EXPECT_EQ(tools.slot(kPencil)->settings.size, 777);
    EXPECT_EQ(tools.slot(kBrush)->settings, defaultSettings(ToolType::Brush));
    tools.setSettings(s);
    EXPECT_EQ(changed.count(), 1);
}

TEST_F(ToolBoxTest, SettingsAreClampedAndKeepTheSlotsType) {
    BrushSettings s = defaultSettings(ToolType::Smudge);
    s.size = 999999;
    tools.setSettings(s);
    EXPECT_EQ(tools.settings().tool, ToolType::Pencil);
    EXPECT_EQ(tools.settings().size, BrushSettings::kMaxSize);
}

TEST_F(ToolBoxTest, SelectingASlotSwitchesTheSettings) {
    SignalCounter slotChanged(&tools, &ToolBox::currentSlotChanged);
    SignalCounter settingsChanged(&tools, &ToolBox::settingsChanged);
    ASSERT_TRUE(tools.selectSlot(kMarker).ok());
    EXPECT_EQ(tools.settings().tool, ToolType::Marker);
    EXPECT_EQ(slotChanged.count(), 1);
    EXPECT_EQ(settingsChanged.count(), 1);
    EXPECT_FALSE(tools.selectSlot({0, 20}).ok());
    EXPECT_EQ(tools.selectSlot(kFirstEmpty).error(), ToolBoxError::SlotEmpty);
    EXPECT_EQ(tools.selectSlot({5, 0}).error(), ToolBoxError::NoSuchSlot);
    EXPECT_EQ(tools.currentSlot(), kMarker);
}

TEST_F(ToolBoxTest, ToolKeyCyclesThroughToolsOfItsType) {
    ASSERT_TRUE(tools.createSlot({1, 0}, ToolType::Pencil).ok());
    EXPECT_TRUE(tools.selectToolOfType(ToolType::Eraser));
    EXPECT_EQ(tools.currentSlot(), kEraser);
    EXPECT_TRUE(tools.selectToolOfType(ToolType::Pencil));
    EXPECT_EQ(tools.currentSlot(), kPencil);
    EXPECT_TRUE(tools.selectToolOfType(ToolType::Pencil));
    EXPECT_EQ(tools.currentSlot(), (SlotRef{1, 0}));
    EXPECT_TRUE(tools.selectToolOfType(ToolType::Pencil));
    EXPECT_EQ(tools.currentSlot(), kPencil);
}

TEST_F(ToolBoxTest, SharedToolKeyCyclesThroughToolsOfEveryTypeInTrayOrder) {
    const SlotRef pixelPen = kPixelPen;
    ASSERT_EQ(tools.slot(pixelPen)->type(), ToolType::PixelArtPen);
    const std::initializer_list<ToolType> c{ToolType::WaterColor, ToolType::PixelArtPen};
    EXPECT_TRUE(tools.selectToolOfTypes(c));
    EXPECT_EQ(tools.currentSlot(), kWater);
    EXPECT_TRUE(tools.selectToolOfTypes(c));
    EXPECT_EQ(tools.currentSlot(), pixelPen);
    EXPECT_TRUE(tools.selectToolOfTypes(c));
    EXPECT_EQ(tools.currentSlot(), kWater);
}

TEST_F(ToolBoxTest, ToolKeyWithoutToolOfThatTypeChangesNothing) {
    ASSERT_TRUE(tools.deleteSlot(kEraser).ok());
    EXPECT_FALSE(tools.selectToolOfType(ToolType::Eraser));
    EXPECT_EQ(tools.currentSlot(), kPencil);
}

TEST_F(ToolBoxTest, CreateFillsOnlyEmptyCells) {
    const auto made = tools.createSlot({1, 2}, ToolType::Marker);
    ASSERT_TRUE(made.ok());
    EXPECT_EQ(made.value(), (SlotRef{1, 2}));
    EXPECT_EQ(tools.slot({1, 2})->name, "Marker");
    EXPECT_EQ(tools.slot({1, 2})->settings, defaultSettings(ToolType::Marker));
    EXPECT_EQ(tools.createSlot({1, 2}, ToolType::Pencil).error(), ToolBoxError::SlotOccupied);
    EXPECT_EQ(tools.createSlot({1, 99}, ToolType::Pencil).error(), ToolBoxError::NoSuchSlot);
    EXPECT_EQ(tools.createSlot({-1, 0}, ToolType::Pencil).error(), ToolBoxError::NoSuchSlot);
}

TEST_F(ToolBoxTest, CreateEveryM3ToolType) {
    int cell = 0;
    for (ToolType t : {ToolType::Pencil, ToolType::Eraser, ToolType::Brush, ToolType::WaterColor, ToolType::Marker, ToolType::Blur,
                       ToolType::Smudge, ToolType::PixelArtPen}) {
        const auto made = tools.createSlot({1, cell++}, t);
        ASSERT_TRUE(made.ok());
        EXPECT_EQ(tools.slot(made.value())->type(), t);
    }
}

TEST_F(ToolBoxTest, DuplicateCopiesSettingsIntoTheNextEmptyCell) {
    ASSERT_TRUE(tools.selectSlot(kBrush).ok());
    BrushSettings s = tools.settings();
    s.size = 123;
    s.stabilizer = StabilizerLevel{4};
    tools.setSettings(s);
    tools.reserveSettings();

    const auto copy = tools.duplicateSlot(kBrush);
    ASSERT_TRUE(copy.ok());
    EXPECT_EQ(copy.value(), kFirstEmpty);
    EXPECT_EQ(tools.slot(copy.value())->settings, s);
    EXPECT_TRUE(tools.slot(copy.value())->reserved.has_value());
    EXPECT_NE(tools.slot(copy.value())->name, tools.slot(kBrush)->name);
    EXPECT_EQ(tools.currentSlot(), kBrush);
    EXPECT_EQ(tools.duplicateSlot({0, 12}).error(), ToolBoxError::SlotEmpty);
}

TEST_F(ToolBoxTest, DeleteMovesTheCurrentToolToTheNeighbour) {
    ASSERT_TRUE(tools.selectSlot(kBrush).ok());
    ASSERT_TRUE(tools.deleteSlot(kBrush).ok());
    EXPECT_EQ(tools.slot(kBrush), nullptr);
    EXPECT_EQ(tools.currentSlot(), kWater);
    EXPECT_EQ(tools.settings().tool, ToolType::WaterColor);

    ASSERT_TRUE(tools.selectSlot(kBlur).ok());
    ASSERT_TRUE(tools.deleteSlot(kBlur).ok());
    EXPECT_EQ(tools.currentSlot(), kClone);
}

TEST_F(ToolBoxTest, DeletingAnotherSlotKeepsTheCurrentOne) {
    ASSERT_TRUE(tools.selectSlot(kMarker).ok());
    SignalCounter slotChanged(&tools, &ToolBox::currentSlotChanged);
    ASSERT_TRUE(tools.deleteSlot(kBlur).ok());
    EXPECT_EQ(tools.currentSlot(), kMarker);
    EXPECT_EQ(slotChanged.count(), 0);
}

TEST_F(ToolBoxTest, TheLastToolCannotBeDeleted) {
    for (int i = 1; i < kBasicTools; ++i)
        ASSERT_TRUE(tools.deleteSlot({0, i}).ok());
    EXPECT_EQ(tools.deleteSlot(kPencil).error(), ToolBoxError::LastTool);
    EXPECT_NE(tools.slot(kPencil), nullptr);
    EXPECT_EQ(tools.deleteSlot({0, 3}).error(), ToolBoxError::SlotEmpty);
}

TEST_F(ToolBoxTest, DeleteFallsBackToAnotherGroup) {
    ASSERT_TRUE(tools.createSlot({1, 0}, ToolType::Smudge).ok());
    for (int i = 0; i < kBasicTools; ++i)
        ASSERT_TRUE(tools.deleteSlot({0, i}).ok());
    EXPECT_EQ(tools.currentSlot(), (SlotRef{1, 0}));
}

TEST_F(ToolBoxTest, Rename) {
    ASSERT_TRUE(tools.renameSlot(kBrush, "  Inker ").ok());
    EXPECT_EQ(tools.slot(kBrush)->name, "Inker");
    EXPECT_EQ(tools.renameSlot(kBrush, "   ").error(), ToolBoxError::InvalidName);
    EXPECT_EQ(tools.slot(kBrush)->name, "Inker");
    EXPECT_EQ(tools.renameSlot(kFirstEmpty, "x").error(), ToolBoxError::SlotEmpty);
}

TEST_F(ToolBoxTest, InitializeSettingsRestoresDefaultsAndKeepsTheName) {
    ASSERT_TRUE(tools.selectSlot(kBrush).ok());
    BrushSettings s = tools.settings();
    s.size = 5;
    s.stabilizer = StabilizerLevel{9};
    tools.setSettings(s);
    tools.reserveSettings();
    ASSERT_TRUE(tools.renameSlot(kBrush, "Mine").ok());

    SignalCounter changed(&tools, &ToolBox::settingsChanged);
    ASSERT_TRUE(tools.initializeSlot(kBrush).ok());
    EXPECT_EQ(tools.settings(), defaultSettings(ToolType::Brush));
    EXPECT_EQ(changed.count(), 1);
    EXPECT_EQ(tools.slot(kBrush)->name, "Mine");
    EXPECT_FALSE(tools.slot(kBrush)->reserved.has_value());
}

TEST_F(ToolBoxTest, MoveToEmptyCellAndSwap) {
    ASSERT_TRUE(tools.selectSlot(kWater).ok());
    ASSERT_TRUE(tools.moveSlot(kWater, {1, 3}).ok());
    EXPECT_EQ(tools.slot(kWater), nullptr);
    EXPECT_EQ(tools.slot({1, 3})->type(), ToolType::WaterColor);
    EXPECT_EQ(tools.currentSlot(), (SlotRef{1, 3}));
    EXPECT_EQ(tools.settings().tool, ToolType::WaterColor);

    ASSERT_TRUE(tools.moveSlot(kPencil, kBrush).ok());
    EXPECT_EQ(tools.slot(kPencil)->type(), ToolType::Brush);
    EXPECT_EQ(tools.slot(kBrush)->type(), ToolType::Pencil);
    EXPECT_EQ(tools.moveSlot(kFirstEmpty, kPencil).error(), ToolBoxError::SlotEmpty);
    EXPECT_EQ(tools.moveSlot(kPencil, {0, 99}).error(), ToolBoxError::NoSuchSlot);
    EXPECT_TRUE(tools.moveSlot(kPencil, kPencil).ok());
}

TEST_F(ToolBoxTest, MoveFollowsCurrentWhenItIsSwappedAway) {
    ASSERT_TRUE(tools.selectSlot(kBrush).ok());
    ASSERT_TRUE(tools.moveSlot(kPencil, kBrush).ok());
    EXPECT_EQ(tools.currentSlot(), kPencil);
    EXPECT_EQ(tools.settings().tool, ToolType::Brush);
}

TEST_F(ToolBoxTest, ReserveAndRecallSettings) {
    EXPECT_EQ(tools.recallSettings().error(), ToolBoxError::NothingReserved);
    BrushSettings s = tools.settings();
    s.size = 321;
    tools.setSettings(s);
    tools.reserveSettings();
    s.size = 5;
    tools.setSettings(s);
    ASSERT_TRUE(tools.recallSettings().ok());
    EXPECT_EQ(tools.settings().size, 321);
}

TEST_F(ToolBoxTest, StepSizeWalksTheSizeList) {
    BrushSettings s = tools.settings();
    s.size = 80;
    tools.setSettings(s);
    tools.stepSize(+1);
    EXPECT_EQ(tools.settings().size, 160);
    tools.stepSize(-1);
    tools.stepSize(-1);
    EXPECT_EQ(tools.settings().size, 40);
    s.size = 55;
    tools.setSettings(s);
    tools.stepSize(+1);
    EXPECT_EQ(tools.settings().size, 80);
    tools.setSettings(s);
    tools.stepSize(-1);
    EXPECT_EQ(tools.settings().size, 40);
}

TEST(SizeList, StopsAtTheEnds) {
    EXPECT_EQ(steppedSize(1, -1), 1);
    EXPECT_EQ(steppedSize(BrushSettings::kMaxSize, +1), BrushSettings::kMaxSize);
    EXPECT_EQ(steppedSize(1, +1), 10);
    EXPECT_EQ(steppedSize(100, 0), 100);
}

TEST_F(ToolBoxTest, ColourAndOptions) {
    SignalCounter colour(&tools, &ToolBox::colorChanged);
    tools.setColor(QColor(10, 20, 30));
    tools.setColor(QColor());
    tools.setColor(QColor(10, 20, 30));
    EXPECT_EQ(colour.count(), 1);
    EXPECT_EQ(tools.color(), QColor(10, 20, 30));

    SignalCounter options(&tools, &ToolBox::optionsChanged);
    BrushOptions o = tools.options();
    o.stabilizer = StabilizerLevel{3};
    o.minimumPressure = 2.0;
    tools.setOptions(o);
    EXPECT_EQ(options.count(), 1);
    EXPECT_EQ(tools.options().minimumPressure, 1.0);
    tools.setOptions(tools.options());
    EXPECT_EQ(options.count(), 1);
}

TEST_F(ToolBoxTest, ModifiedFollowsEveryChange) {
    SignalCounter modified(&tools, &ToolBox::modified);
    tools.setColor(Qt::red);
    ASSERT_TRUE(tools.selectSlot(kBrush).ok());
    ASSERT_TRUE(tools.renameSlot(kBrush, "x").ok());
    tools.stepSize(+1);
    tools.reserveSettings();
    EXPECT_EQ(modified.count(), 5);
}

TEST_F(ToolBoxTest, SetStateRepairsWhatIsWrong) {
    ToolBoxState bad;
    tools.setState(bad);
    EXPECT_EQ(tools.groupCount(), 2);
    EXPECT_EQ(names(tools, 0).size(), std::size_t(kBasicTools));

    ToolBoxState s = defaultToolBoxState();
    s.current = kFirstEmpty;
    s.groups[0].cells[1]->settings.size = 999999;
    s.groups[0].cells[2]->name = "  ";
    s.groups[1].name = "";
    s.groups[1].cells.clear();
    tools.setState(s);
    EXPECT_EQ(tools.currentSlot(), kPencil);
    EXPECT_EQ(tools.slot(kBrush)->settings.size, BrushSettings::kMaxSize);
    EXPECT_EQ(tools.slot(kWater)->name, "Water Color");
    EXPECT_FALSE(tools.group(1).name.isEmpty());
    EXPECT_GE(tools.group(1).cells.size(), std::size_t(2 * ToolBox::kColumns));
}

TEST_F(ToolBoxTest, JsonRoundTripKeepsEverything) {
    ASSERT_TRUE(tools.createSlot({1, 1}, ToolType::Marker).ok());
    ASSERT_TRUE(tools.renameSlot({1, 1}, "Inks").ok());
    ASSERT_TRUE(tools.selectSlot(kBrush).ok());
    BrushSettings s = tools.settings();
    s.size = 4321, s.minSize = 12, s.pressureDensity = true;
    s.sensitivitySize = 150;
    s.stabilizer = StabilizerLevel{5};
    tools.setSettings(s);
    tools.reserveSettings();
    tools.setColor(QColor(12, 34, 56, 200));
    tools.setSecondaryColor(QColor(200, 100, 50));
    BrushSettings withResources = tools.settings();
    withResources.form = QStringLiteral("Blots One"), withResources.formStrength = 70, withResources.formScale = 150;
    withResources.formInvert = true;
    withResources.texture = QStringLiteral("Speckle"), withResources.textureStrength = 40, withResources.textureScale = 80;
    withResources.textureInvert = true;
    withResources.textureEraseLow = false;
    withResources.bristleSize = 30;
    withResources.dilution = 45;
    withResources.shapeAngle = -45, withResources.shapeSpacing = 12, withResources.shapeAngleControl = 2;
    tools.setSettings(withResources);
    s = tools.settings();
    BrushOptions o;
    o.stabilizer = StabilizerLevel{7};
    tools.setOptions(o);

    const QJsonDocument doc(toJson(tools.state()));
    const QJsonDocument reread = QJsonDocument::fromJson(doc.toJson());
    const auto state = toolBoxStateFromJson(reread.object());
    ASSERT_TRUE(state.ok());

    ToolBox other;
    other.setState(state.value());
    EXPECT_EQ(other.state().groups, tools.state().groups);
    EXPECT_EQ(other.currentSlot(), kBrush);
    EXPECT_EQ(other.settings(), s);
    EXPECT_EQ(other.color(), QColor(12, 34, 56, 200));
    EXPECT_EQ(other.secondaryColor(), QColor(200, 100, 50));
    EXPECT_EQ(other.options().minimumPressure, 0.0);
    EXPECT_EQ(other.options().stabilizer, o.stabilizer);
}

TEST_F(ToolBoxTest, TheTwoColoursExchange) {
    EXPECT_EQ(tools.secondaryColor(), QColor(Qt::white));
    tools.setColor(QColor(10, 20, 30));
    int first = 0, second = 0;
    QObject::connect(&tools, &ToolBox::colorChanged, [&] { ++first; });
    QObject::connect(&tools, &ToolBox::secondaryColorChanged, [&] { ++second; });
    tools.exchangeColors();
    EXPECT_EQ(tools.color(), QColor(Qt::white));
    EXPECT_EQ(tools.secondaryColor(), QColor(10, 20, 30));
    EXPECT_EQ(first, 1);
    EXPECT_EQ(second, 1);
    tools.exchangeColors();
    EXPECT_EQ(tools.color(), QColor(10, 20, 30));
    const auto old = toolBoxStateFromJson(QJsonDocument::fromJson(R"({"version": 10, "groups": []})").object());
    ASSERT_TRUE(old.ok());
    EXPECT_EQ(old.value().secondaryColor, QColor(Qt::white));
}

TEST_F(ToolBoxTest, TheTransparentColourPaintsTransparentAndAnyColourLeavesIt) {
    tools.setColor(QColor(10, 20, 30));
    EXPECT_FALSE(tools.transparent());
    EXPECT_EQ(tools.paintColor(), QColor(10, 20, 30));
    int changes = 0;
    bool last = false;
    QObject::connect(&tools, &ToolBox::transparentChanged, [&](bool on) { ++changes, last = on; });
    tools.setTransparent(true);
    tools.setTransparent(true);
    EXPECT_EQ(changes, 1);
    EXPECT_TRUE(last);
    EXPECT_EQ(tools.paintColor().alpha(), 0);
    EXPECT_EQ(tools.color(), QColor(10, 20, 30)) << "the colour behind it stays";
    tools.setTransparent(false);
    EXPECT_EQ(tools.paintColor(), QColor(10, 20, 30)) << "back to the previous colour";

    const auto leaves = [&](const char* how, const std::function<void()>& pick) {
        tools.setTransparent(true);
        pick();
        EXPECT_FALSE(tools.transparent()) << how;
        EXPECT_GT(tools.paintColor().alpha(), 0) << how;
    };
    leaves("another colour", [&] { tools.setColor(QColor(1, 2, 3)); });
    leaves("the same colour again", [&] { tools.setColor(QColor(1, 2, 3)); });
    leaves("exchange", [&] { tools.exchangeColors(); });
    leaves("exchange of equal colours", [&] {
        tools.setSecondaryColor(tools.color());
        tools.exchangeColors();
    });
}

// Feedback, 2026-11-06: "We should remove tip roundness from settings everywhere." A file saved before keeps loading.
TEST_F(ToolBoxTest, TipRoundnessIsNeitherSavedNorRead) {
    const QByteArray saved = QJsonDocument(toJson(tools.state())).toJson();
    EXPECT_FALSE(saved.contains("tipRoundness"));
    const QByteArray old = R"({"version": 10, "groups": [{"name": "G", "cells": [
            {"tool": "brush", "name": "B", "settings": {"size": 77, "tipRoundness": 80}}]}]})";
    const auto state = toolBoxStateFromJson(QJsonDocument::fromJson(old).object());
    ASSERT_TRUE(state.ok());
    tools.setState(state.value());
    BrushSettings expected = defaultSettings(ToolType::Brush);
    expected.size = 77;
    EXPECT_EQ(tools.slot({0, 0})->settings, expected);
}

// Min Blending is gone: a tool set saved with it still loads, and the key is ignored.
TEST_F(ToolBoxTest, MinBlendingIsNeitherSavedNorRead) {
    const QByteArray saved = QJsonDocument(toJson(tools.state())).toJson();
    EXPECT_FALSE(saved.contains("minBlending"));
    const QByteArray old = R"({"version": 10, "groups": [{"name": "G", "cells": [
            {"tool": "brush", "name": "B", "settings": {"size": 77, "minBlending": 40}}]}]})";
    const auto state = toolBoxStateFromJson(QJsonDocument::fromJson(old).object());
    ASSERT_TRUE(state.ok());
    tools.setState(state.value());
    BrushSettings expected = defaultSettings(ToolType::Brush);
    expected.size = 77;
    EXPECT_EQ(tools.slot({0, 0})->settings, expected);
}

// Taper, Taper Curve, Start Ramp, Flick and Size Curve are gone: a tool set saved with them still loads, and the
// keys are ignored.
TEST_F(ToolBoxTest, TaperStartRampFlickAndSizeCurveAreNeitherSavedNorRead) {
    const QByteArray saved = QJsonDocument(toJson(tools.state())).toJson();
    for (const char* key : {"\"taper\"", "\"taperCurve\"", "\"startRamp\"", "\"flick\"", "\"sizeCurve\""})
        EXPECT_FALSE(saved.contains(key)) << key;
    const QByteArray old = R"({"version": 10, "groups": [{"name": "G", "cells": [
            {"tool": "pencil", "name": "P", "settings": {"size": 77, "taper": 55, "taperCurve": 70, "startRamp": 15, "flick": 65,
                                                        "sizeCurve": 40}}]}]})";
    const auto state = toolBoxStateFromJson(QJsonDocument::fromJson(old).object());
    ASSERT_TRUE(state.ok());
    tools.setState(state.value());
    BrushSettings expected = defaultSettings(ToolType::Pencil);
    expected.size = 77;
    EXPECT_EQ(tools.slot({0, 0})->settings, expected);
}

// Direction starts at Auto, as the reference's, the way the engine always laid hairs. A tool saved before it holds the old key at
// 0, None, the setting never read: that key is left unread, so the tool starts at Auto, once; saved again, None stays None.
TEST_F(ToolBoxTest, ABristleDirectionSavedBeforeItWasReadStartsAtAuto) {
    EXPECT_EQ(defaultSettings(ToolType::Brush).bristleDirection, kBristleDirectionAuto);
    const QByteArray old = R"({"version": 10, "groups": [{"name": "G", "cells": [
            {"tool": "brush", "name": "B", "settings": {"size": 77, "bristleDirection": 0}}]}]})";
    const auto state = toolBoxStateFromJson(QJsonDocument::fromJson(old).object());
    ASSERT_TRUE(state.ok());
    tools.setState(state.value());
    EXPECT_EQ(tools.slot({0, 0})->settings.bristleDirection, kBristleDirectionAuto);
    EXPECT_TRUE(tools.slot({0, 0})->settings.bristleDetectTurn) << "saved before Detect Turn: on, the reference's start";
    EXPECT_EQ(tools.slot({0, 0})->settings.bristleSize, kHairStart) << "Hair's start, no key for it";
    BrushSettings none = tools.slot({0, 0})->settings;
    none.bristleDirection = kBristleDirectionNone;
    none.bristleDetectTurn = false;
    ASSERT_TRUE(tools.selectSlot({0, 0}).ok());
    tools.setSettings(none);
    const auto again = toolBoxStateFromJson(QJsonDocument::fromJson(QJsonDocument(toJson(tools.state())).toJson()).object());
    ASSERT_TRUE(again.ok());
    tools.setState(again.value());
    EXPECT_EQ(tools.slot({0, 0})->settings.bristleDirection, kBristleDirectionNone);
    EXPECT_FALSE(tools.slot({0, 0})->settings.bristleDetectTurn) << "Detect Turn kept too";
}

// Hair: a tool saved before kept it as "bristleSize", on a scale whose 100 is the new 50, so its hairs stay as wide as they were.
TEST_F(ToolBoxTest, AHairSavedBeforeItsNewScaleKeepsItsWidth) {
    const QByteArray old = R"({"version": 10, "groups": [{"name": "G", "cells": [
            {"tool": "brush", "name": "B", "settings": {"bristleSize": 100}},
            {"tool": "pencil", "name": "P", "settings": {"bristleSize": 30, "hair": 80}}]}]})";
    const auto state = toolBoxStateFromJson(QJsonDocument::fromJson(old).object());
    ASSERT_TRUE(state.ok());
    tools.setState(state.value());
    EXPECT_EQ(tools.slot({0, 0})->settings.bristleSize, 50);
    EXPECT_EQ(tools.slot({0, 1})->settings.bristleSize, 80) << "the new key, where there is one";
}

TEST_F(ToolBoxTest, JsonIgnoresUnknownKeysAndFillsMissingOnes) {
    const QByteArray text = R"({
        "version": 10, "bucketPlaced": true, "futureThing": [1, 2],
        "groups": [{"name": "G", "extra": true, "cells": [
            {"tool": "pencil", "name": "P", "settings": {"size": 77, "mystery": 5, "density": "lots"}},
            {"tool": "hologram", "name": "from the future", "settings": {}},
            {"tool": "marker", "settings": {}}
        ]}]})";
    const auto state = toolBoxStateFromJson(QJsonDocument::fromJson(text).object());
    ASSERT_TRUE(state.ok());
    tools.setState(state.value());
    ASSERT_EQ(tools.groupCount(), 1);
    EXPECT_EQ(tools.slot({0, 0})->settings.size, 77);
    EXPECT_EQ(tools.slot({0, 0})->settings.density, defaultSettings(ToolType::Pencil).density);
    EXPECT_EQ(tools.slot({0, 1}), nullptr);
    EXPECT_EQ(tools.slot({0, 2})->name, "Marker");
    EXPECT_EQ(tools.slot({0, 2})->settings, defaultSettings(ToolType::Marker));
}

// A tools.json from before the 4000 px baseline, or edited by hand, opens with every size within it.
TEST_F(ToolBoxTest, ASavedToolFileHasItsSizesClampedTo4000Px) {
    const QByteArray text = R"({
        "version": 10, "bucketPlaced": true,
        "groups": [{"name": "G", "cells": [
            {"tool": "brush", "name": "Wide", "settings": {"size": 50000}, "reserved": {"size": 45000}},
            {"tool": "marker", "name": "Huge", "settings": {"size": 1e15}},
            {"tool": "eraser", "name": "Edge", "settings": {"size": 40000}}
        ]}]})";
    const auto state = toolBoxStateFromJson(QJsonDocument::fromJson(text).object());
    ASSERT_TRUE(state.ok());
    tools.setState(state.value());
    EXPECT_EQ(tools.slot({0, 0})->settings.size, BrushSettings::kMaxSize);
    ASSERT_TRUE(tools.slot({0, 0})->reserved.has_value());
    EXPECT_EQ(tools.slot({0, 0})->reserved->size, BrushSettings::kMaxSize);
    EXPECT_EQ(tools.slot({0, 1})->settings.size, BrushSettings::kMaxSize);
    EXPECT_EQ(tools.slot({0, 2})->settings.size, 40000);
}

TEST_F(ToolBoxTest, JsonRejectsWrongLayouts) {
    EXPECT_EQ(toolBoxStateFromJson({}).error(), ToolBoxFileError::Corrupt);
    EXPECT_EQ(toolBoxStateFromJson(QJsonDocument::fromJson(R"({"version": 10, "groups": 3})").object()).error(), ToolBoxFileError::Corrupt);
    EXPECT_EQ(toolBoxStateFromJson(QJsonDocument::fromJson(R"({"version": 99, "groups": []})").object()).error(),
              ToolBoxFileError::OtherVersion);
    EXPECT_EQ(toolBoxStateFromJson(QJsonDocument::fromJson(R"({"version": 9, "groups": []})").object()).error(),
              ToolBoxFileError::OtherVersion);
}

class ToolBoxFileTest : public ToolBoxTest {
protected:
    QTemporaryDir dir;
    QString path() const { return dir.filePath("config/tools.json"); }
    static void write(const QString& file, const QByteArray& bytes) {
        QFile f(file);
        ASSERT_TRUE(f.open(QIODevice::WriteOnly));
        f.write(bytes);
    }
};

TEST_F(ToolBoxFileTest, SaveThenLoad) {
    ASSERT_TRUE(dir.isValid());
    ASSERT_TRUE(tools.selectSlot(kMarker).ok());
    tools.setColor(Qt::green);
    ToolBoxStore store(&tools, path());
    ASSERT_TRUE(store.save().ok());

    ToolBox fresh;
    ToolBoxStore loader(&fresh, path());
    ASSERT_TRUE(loader.load().ok());
    EXPECT_EQ(fresh.currentSlot(), kMarker);
    EXPECT_EQ(fresh.color(), QColor(Qt::green));
    EXPECT_EQ(fresh.state().groups, tools.state().groups);
    EXPECT_FALSE(loader.savePending());
}

TEST_F(ToolBoxFileTest, MissingFileKeepsDefaults) {
    ToolBoxStore store(&tools, path());
    EXPECT_EQ(store.load().error(), ToolBoxFileError::NoFile);
    EXPECT_EQ(tools.state().groups, defaultToolBoxState().groups);
}

// A fresh settings folder starts at the chosen global stabilizer level, 4. The reference cases assume 3, and the
// replay tool keeps that.
TEST_F(ToolBoxFileTest, AFreshStartHasStabilizerFour) {
    ToolBox fresh;
    ToolBoxStore store(&fresh, path());
    EXPECT_EQ(store.load().error(), ToolBoxFileError::NoFile);
    EXPECT_EQ(fresh.options().stabilizer, StabilizerLevel{4});
}

TEST_F(ToolBoxFileTest, CorruptFilesKeepTheCurrentToolsAndDoNotCrash) {
    ASSERT_TRUE(QDir().mkpath(dir.filePath("config")));
    ToolBoxStore store(&tools, path());
    for (const QByteArray& junk : {QByteArray("not json at all {{{"), QByteArray(""), QByteArray("[1,2,3]"), QByteArray("{}"),
                                   QByteArray("\xff\xfe\x00garbage", 10)}) {
        write(path(), junk);
        const auto loaded = store.load();
        EXPECT_FALSE(loaded.ok()) << junk.toStdString();
        EXPECT_EQ(tools.state().groups, defaultToolBoxState().groups);
    }
}

TEST_F(ToolBoxFileTest, AFileOfAnotherVersionGivesTheDefaultToolsAndIsKeptAside) {
    ASSERT_TRUE(QDir().mkpath(dir.filePath("config")));
    const QByteArray other = R"({"version":9,"groups":[{"name":"Mine","cells":[{"tool":"pencil","name":"Kept","settings":{"size":7}}]}]})";
    write(path(), other);
    ToolBoxStore store(&tools, path());
    const auto loaded = store.load();
    ASSERT_FALSE(loaded.ok());
    EXPECT_EQ(loaded.error(), ToolBoxFileError::OtherVersion);
    EXPECT_EQ(tools.state().groups, defaultToolBoxState().groups);
    EXPECT_FALSE(QFile::exists(path()));
    QFile aside(path() + QStringLiteral(".refused"));
    ASSERT_TRUE(aside.open(QIODevice::ReadOnly));
    EXPECT_EQ(aside.readAll(), other);
    aside.close();
    ASSERT_TRUE(store.save().ok()) << "the next save writes a new file beside the one kept";
    EXPECT_TRUE(QFile::exists(path()));
    EXPECT_TRUE(QFile::exists(path() + QStringLiteral(".refused")));
}

TEST_F(ToolBoxFileTest, GroupsOfGarbageGiveTheDefaultTools) {
    ASSERT_TRUE(QDir().mkpath(dir.filePath("config")));
    write(path(), R"({"version":10,"groups":[1,2,"x"]})");
    ToolBoxStore store(&tools, path());
    ASSERT_TRUE(store.load().ok());
    EXPECT_EQ(names(tools, 0).size(), std::size_t(kBasicTools));
}

TEST_F(ToolBoxFileTest, ChangesAreSavedAfterAPauseAndOnFlush) {
    ToolBoxStore store(&tools, path());
    EXPECT_FALSE(store.savePending());
    tools.setColor(Qt::blue);
    EXPECT_TRUE(store.savePending());
    store.flush();
    EXPECT_FALSE(store.savePending());

    ToolBox fresh;
    ToolBoxStore loader(&fresh, path());
    ASSERT_TRUE(loader.load().ok());
    EXPECT_EQ(fresh.color(), QColor(Qt::blue));
}

TEST_F(ToolBoxFileTest, FlushWithoutChangesWritesNothing) {
    ToolBoxStore store(&tools, path());
    store.flush();
    EXPECT_FALSE(QFile::exists(path()));
}

TEST_F(ToolBoxFileTest, UnwritablePathIsAnErrorNotACrash) {
    ASSERT_TRUE(dir.isValid());
    write(dir.filePath("blocker"), "x");
    ToolBoxStore store(&tools, dir.filePath("blocker/tools.json"));
    EXPECT_EQ(store.save().error(), ToolBoxFileError::WriteFailed);
}

TEST(ToolBoxJson, PixelArtPenSettingsRoundTrip) {
    ToolBoxState state;
    ToolGroup g;
    BrushSettings s = defaultSettings(ToolType::PixelArtPen);
    s.opacity = 77;
    g.cells.emplace_back(ToolSlot{QStringLiteral("Dots"), s, std::nullopt, {}});
    state.groups.push_back(g);
    const auto loaded = toolBoxStateFromJson(toJson(state));
    ASSERT_TRUE(loaded);
    ASSERT_EQ(loaded.value().groups.front().cells.size(), 1u);
    EXPECT_EQ(loaded.value().groups.front().cells[0]->settings, s);
}

TEST(ToolBoxJson, TheCloneStampsOptionsAreSaved) {
    ToolBoxState state;
    ToolGroup g;
    BrushSettings stamp = defaultSettings(ToolType::Clone);
    EXPECT_TRUE(stamp.cloneAligned);
    EXPECT_FALSE(stamp.cloneAllLayers);
    stamp.cloneAligned = false;
    stamp.cloneAllLayers = true;
    g.cells.emplace_back(ToolSlot{QStringLiteral("Stamp"), stamp, std::nullopt, {}});
    state.groups.push_back(g);
    const QJsonObject parsed = QJsonDocument::fromJson(QJsonDocument(toJson(state)).toJson()).object();
    const auto loaded = toolBoxStateFromJson(parsed);
    ASSERT_TRUE(loaded);
    EXPECT_EQ(loaded.value().groups.front().cells[0]->settings, stamp);
}
