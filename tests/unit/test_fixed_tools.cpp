#include "core/tools/ToolBox.h"
#include "core/tools/ToolBoxJson.h"

#include <QJsonDocument>

#include <gtest/gtest.h>

using namespace doodlebold;

TEST(FixedTools, KeysAndNamesRoundTrip) {
    for (FixedTool t : kFixedTools) {
        EXPECT_FALSE(fixedToolName(t).isEmpty());
        EXPECT_EQ(fixedToolFromKey(fixedToolKey(t)), t);
    }
    EXPECT_FALSE(fixedToolFromKey("nonsense"));
    for (TransformMode m : {TransformMode::Freeform, TransformMode::Resize, TransformMode::Deform, TransformMode::Rotate})
        EXPECT_EQ(transformModeFromKey(transformModeKey(m)), m);
    EXPECT_TRUE(isSelectionTool(FixedTool::Marquee));
    EXPECT_FALSE(isSelectionTool(FixedTool::Move));
}

TEST(FixedTools, SelectionPenAndEraserAreToolTypes) {
    EXPECT_EQ(toolTypeFromKey("select"), ToolType::SelectionPen);
    EXPECT_EQ(toolTypeFromKey("deselect"), ToolType::SelectionEraser);
    EXPECT_EQ(toolTypeName(ToolType::SelectionPen), QStringLiteral("Selection Pen"));
    EXPECT_EQ(defaultSettings(ToolType::SelectionEraser).tool, ToolType::SelectionEraser);
}

TEST(FixedTools, ActiveToolIsFixedUntilATrayToolIsPicked) {
    ToolBox tools;
    int changes = 0;
    QObject::connect(&tools, &ToolBox::activeToolChanged, [&changes] { ++changes; });
    EXPECT_FALSE(tools.fixedTool());
    tools.setFixedTool(FixedTool::Marquee);
    EXPECT_EQ(tools.fixedTool(), FixedTool::Marquee);
    EXPECT_EQ(changes, 1);
    tools.setFixedTool(FixedTool::Marquee);
    EXPECT_EQ(changes, 1);
    ASSERT_TRUE(tools.selectSlot(tools.currentSlot()));
    EXPECT_FALSE(tools.fixedTool());
    EXPECT_EQ(changes, 2);
    tools.setFixedTool(FixedTool::Lasso);
    tools.selectToolOfType(ToolType::Brush);
    EXPECT_FALSE(tools.fixedTool());
}

TEST(FixedTools, ToolKeyReturnsToTheCurrentToolBeforeCycling) {
    ToolBox tools;
    ASSERT_TRUE(tools.createSlot({1, 0}, ToolType::Pencil));
    ASSERT_TRUE(tools.selectToolOfType(ToolType::Pencil));
    const SlotRef current = tools.currentSlot();
    tools.setFixedTool(FixedTool::Hand);
    ASSERT_TRUE(tools.selectToolOfType(ToolType::Pencil));
    EXPECT_EQ(tools.currentSlot(), current);
    EXPECT_FALSE(tools.fixedTool());
    ASSERT_TRUE(tools.selectToolOfType(ToolType::Pencil));
    EXPECT_NE(tools.currentSlot(), current);
}

TEST(FixedTools, SelectOrCreateAddsTheToolWhenTheTrayHasNone) {
    ToolBox tools;
    EXPECT_FALSE(tools.selectToolOfType(ToolType::SelectionPen));
    tools.selectOrCreateToolOfType(ToolType::SelectionPen);
    EXPECT_EQ(tools.currentTool().type(), ToolType::SelectionPen);
    EXPECT_EQ(tools.toolCount(), 11);
    tools.selectToolOfType(ToolType::Pencil);
    tools.selectOrCreateToolOfType(ToolType::SelectionPen);
    EXPECT_EQ(tools.toolCount(), 11);
    EXPECT_EQ(tools.currentTool().type(), ToolType::SelectionPen);
}

TEST(FixedTools, OptionsAreClampedAndSignalled) {
    ToolBox tools;
    int changes = 0;
    QObject::connect(&tools, &ToolBox::fixedOptionsChanged, [&changes] { ++changes; });
    FixedToolOptions o = tools.fixedOptions();
    o.wand.colourThreshold = 999;
    o.wand.gapTolerance = -4;
    o.marquee.perspective = 400;
    tools.setFixedOptions(o);
    EXPECT_EQ(tools.fixedOptions().wand.colourThreshold, 255);
    EXPECT_EQ(tools.fixedOptions().wand.gapTolerance, 0);
    EXPECT_EQ(tools.fixedOptions().marquee.perspective, 100);
    EXPECT_EQ(changes, 1);
    tools.setFixedOptions(tools.fixedOptions());
    EXPECT_EQ(changes, 1);
}

TEST(FixedTools, HaveTheirDefaults) {
    const FixedToolOptions o;
    EXPECT_TRUE(o.marquee.cutAndMove);
    EXPECT_EQ(o.marquee.dragging, transform::Interpolation::Nearest);
    EXPECT_EQ(o.marquee.preview, transform::Interpolation::Nearest);
    EXPECT_EQ(o.marquee.final, transform::Interpolation::Nearest);
    EXPECT_TRUE(o.lasso.antialias);
    EXPECT_EQ(o.lasso.mode, LassoMode::Freeform);
    EXPECT_EQ(o.wand.mode, selection::WandMode::EnclosedTransparent);
    EXPECT_EQ(o.wand.source, WandSource::CurrentLayer);
    EXPECT_TRUE(o.wand.antialias);
}

TEST(FixedToolsJson, OptionsRoundTrip) {
    ToolBoxState state = defaultToolBoxState();
    state.fixed.marquee = {false,
                           TransformMode::Rotate,
                           35,
                           transform::Interpolation::Nearest,
                           transform::Interpolation::Lanczos2,
                           transform::Interpolation::Lanczos3};
    state.fixed.lasso = {LassoMode::PolygonalFreeform, false};
    state.fixed.wand = {selection::WandMode::AllSimilar, 77, 12, 9, WandSource::MergedImage, false};
    const QJsonObject json = toJson(state);
    EXPECT_EQ(json.value("version").toInt(), 10);

    const QJsonObject parsed = QJsonDocument::fromJson(QJsonDocument(json).toJson()).object();
    const auto back = toolBoxStateFromJson(parsed);
    ASSERT_TRUE(back.ok());
    EXPECT_EQ(back.value().fixed, state.fixed);

    ToolBox tools;
    tools.setState(back.value());
    EXPECT_EQ(tools.fixedOptions(), state.fixed);
}

TEST(FixedToolsJson, FilesWithoutTheOptionsTakeTheDefaults) {
    QJsonObject json = toJson(defaultToolBoxState());
    json.remove("fixedTools");
    const auto back = toolBoxStateFromJson(json);
    ASSERT_TRUE(back.ok());
    EXPECT_EQ(back.value().fixed, FixedToolOptions{});
}

TEST(FixedToolsJson, WrongTypesAndUnknownValuesKeepDefaults) {
    QJsonObject json = toJson(defaultToolBoxState());
    json["fixedTools"] =
        QJsonObject{{"marquee", QJsonObject{{"transformMode", "spin"}, {"final", "cubist"}, {"cutAndMove", "yes"}, {"perspective", 900}}},
                    {"lasso", 4},
                    {"wand", QJsonObject{{"mode", "bogus"}, {"colourThreshold", -5}, {"future", true}}}};
    const auto back = toolBoxStateFromJson(json);
    ASSERT_TRUE(back.ok());
    const FixedToolOptions& f = back.value().fixed;
    EXPECT_EQ(f.marquee.transformMode, TransformMode::Resize);
    EXPECT_EQ(f.marquee.final, transform::Interpolation::Nearest);
    EXPECT_TRUE(f.marquee.cutAndMove);
    EXPECT_EQ(f.marquee.perspective, 100);
    EXPECT_EQ(f.lasso, LassoOptions{});
    EXPECT_EQ(f.wand.mode, selection::WandMode::EnclosedTransparent);
    EXPECT_EQ(f.wand.colourThreshold, 0);
}

TEST(FixedToolsJson, SelectionToolSlotsRoundTrip) {
    ToolBoxState state = defaultToolBoxState();
    state.groups[1].cells[0] = ToolSlot{"Sel", defaultSettings(ToolType::SelectionPen), std::nullopt, {}};
    state.groups[1].cells[1] = ToolSlot{"Ers", defaultSettings(ToolType::SelectionEraser), std::nullopt, {}};
    const auto back = toolBoxStateFromJson(toJson(state));
    ASSERT_TRUE(back.ok());
    EXPECT_EQ(back.value().groups[1].cells[0]->type(), ToolType::SelectionPen);
    EXPECT_EQ(back.value().groups[1].cells[1]->type(), ToolType::SelectionEraser);
}
