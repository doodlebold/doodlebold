#include "../SignalCounter.h"
#include "core/brush/BrushResources.h"
#include "core/brush/BrushSettings.h"
#include "core/color/ColorState.h"
#include "core/tools/ToolBox.h"
#include "core/tools/ToolBoxStore.h"
#include "ui/MainWindow.h"
#include "ui/Theme.h"
#include "ui/panels/BrushPreviewStrip.h"
#include "ui/panels/StabilizerCombo.h"
#include "ui/panels/ToolIcons.h"
#include "ui/panels/ToolParameters.h"
#include "ui/panels/ToolPanel.h"
#include "ui/ValueBar.h"
#include "ui/panels/ToolTray.h"
#include "ScreenshotSupport.h"

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDockWidget>
#include <QDoubleSpinBox>
#include <QFile>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QTimer>
#include <QMap>
#include <QMenuBar>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QRadioButton>
#include <QSettings>
#include <QStandardPaths>
#include <QTabBar>
#include <QTemporaryDir>
#include <QThread>
#include <QScrollArea>
#include <QToolBar>
#include <QToolButton>
#include <QWheelEvent>

#include <gtest/gtest.h>

#include <algorithm>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

using namespace doodlebold;
using doodlebold::test::SignalCounter;

namespace {

// The Bucket sits where the Blur was, and the Blur comes after the Clone Stamp; the first empty cell is 10.
constexpr SlotRef kPencil{0, 0}, kBrush{0, 1}, kWater{0, 2}, kMarker{0, 3}, kEraser{0, 4}, kBucket{0, 5}, kSmudge{0, 6}, kClone{0, 8},
    kBlur{0, 9};

class ToolPanelTest : public testing::Test {
protected:
    void SetUp() override {
        QStandardPaths::setTestModeEnabled(true);  // MainWindow tests must not touch the real tools.json
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDir.path());  // nor the tray's rows
        test::applyTheme();
        panel = std::make_unique<ToolPanel>(&tools);
        panel->resize(320, 760);
        panel->show();
        QCoreApplication::processEvents();
    }

    template <typename T> T* child(const char* name) const {
        T* w = panel->findChild<T*>(QString::fromLatin1(name));
        EXPECT_NE(w, nullptr) << name;
        return w;
    }
    bool shown(const char* name) const { return child<QWidget>(name)->isVisibleTo(panel.get()); }
    int top(const char* name) const {
        const QWidget* w = child<QWidget>(name);
        return w->mapTo(panel.get(), QPoint(0, 0)).y();
    }
    int left(const char* name) const {
        const QWidget* w = child<QWidget>(name);
        return w->mapTo(panel.get(), QPoint(0, 0)).x();
    }
    void type(const char* name, const QString& text) const {
        auto* bar = child<ValueBar>(name);
        bar->editValue();
        auto* editor = bar->findChild<QLineEdit*>(QStringLiteral("valueEditor"));
        ASSERT_NE(editor, nullptr);
        editor->setText(text);
        QKeyEvent enter(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
        QCoreApplication::sendEvent(editor, &enter);
    }

    QWidget* grid() const { return child<QWidget>("toolGrid"); }
    void mouse(QEvent::Type type, int cell, Qt::MouseButton button = Qt::LeftButton) const {
        const QPoint pos = grid()->mapFrom(panel->tray(), panel->tray()->cellRect(cell).center());
        QMouseEvent e(type, QPointF(pos), QPointF(grid()->mapToGlobal(pos)), button,
                      type == QEvent::MouseButtonRelease ? Qt::NoButton : button, Qt::NoModifier);
        QApplication::sendEvent(grid(), &e);
    }
    void click(int cell) const {
        mouse(QEvent::MouseButtonPress, cell);
        mouse(QEvent::MouseButtonRelease, cell);
    }
    void drop(SlotRef from, int cell) const {
        panel->tray()->moveSlotTo(from, cell);  // what the grid's dropEvent does (a real drag needs a window system)
    }
    static QStringList texts(const QMenu& menu) {
        QStringList out;
        for (const QAction* a : menu.actions())
            if (!a->isSeparator())
                out << a->text();
        return out;
    }
    static QAction* action(const QMenu& menu, const QString& text) {
        for (QAction* a : menu.actions())
            if (a->text() == text)
                return a;
        return nullptr;
    }

    QTemporaryDir settingsDir;
    ToolBox tools;
    std::unique_ptr<ToolPanel> panel;
};

}

// Feedback: "The pen shapes 1-5 should be circles ranging from soft to hard." Each Shape button is a disc, solid in its middle, its rim
// softer the lower the Shape: the 1st's fades over the most pixels, the 5th's over the fewest.
TEST_F(ToolPanelTest, TheShapeButtonsAreDiscsFromSoftToHard) {
    ASSERT_TRUE(tools.selectSlot(kBrush).ok());
    QCoreApplication::processEvents();
    std::vector<int> fading;
    QWidget* row = nullptr;
    for (int i = 0; i < kEdgePresets; ++i) {
        auto* b = child<QToolButton>(QStringLiteral("edgeButton%1").arg(i).toLatin1().constData());
        ASSERT_NE(b, nullptr);
        row = b->parentWidget();
        const QImage disc = b->icon().pixmap(b->iconSize(), 2.0).toImage().convertToFormat(QImage::Format_ARGB32);
        ASSERT_EQ(disc.width(), b->iconSize().width() * 2);
        const QPoint middle(disc.width() / 2, disc.height() / 2);
        EXPECT_GE(qAlpha(disc.pixel(middle)), 250) << "Shape " << i + 1 << ": solid in the middle";
        EXPECT_LE(qAlpha(disc.pixel(0, 0)), 5) << "Shape " << i + 1 << ": round, its corners empty";
        int partly = 0;
        for (int y = 0; y < disc.height(); ++y)
            for (int x = 0; x < disc.width(); ++x)
                partly += qAlpha(disc.pixel(x, y)) > 8 && qAlpha(disc.pixel(x, y)) < 247 ? 1 : 0;
        fading.push_back(partly);
    }
    for (int i = 1; i < kEdgePresets; ++i)
        EXPECT_LT(fading[std::size_t(i)], fading[std::size_t(i) - 1]) << "Shape " << i + 1 << " is harder than Shape " << i;
    const QPixmap shot = row->grab();
    EXPECT_TRUE(test::saveScreenshot(shot.scaled(shot.size() * 4, Qt::KeepAspectRatio, Qt::FastTransformation),
                                     QStringLiteral("shape_buttons_soft_to_hard")));
}

TEST_F(ToolPanelTest, TheWheelNeverChangesABrushSetting) {
    const auto bars = child<QWidget>("toolParametersScroll")->findChildren<ValueBar*>();
    ASSERT_GT(bars.size(), 5);
    for (ValueBar* bar : bars)
        EXPECT_FALSE(bar->wheelSteps()) << bar->objectName().toStdString();
    auto* density = child<ValueBar>("densityBar");
    const double before = density->value();
    QWheelEvent down(QPointF(5, 5), QPointF(5, 5), QPoint(), QPoint(0, -240), Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
    QCoreApplication::sendEvent(density, &down);
    EXPECT_EQ(density->value(), before);
}

TEST_F(ToolPanelTest, TrayShowsGroupsAndTools) {
    QTabBar* tabs = panel->tray()->tabBar();
    ASSERT_EQ(tabs->count(), 2);
    EXPECT_EQ(tabs->tabText(0), "Basic");
    EXPECT_EQ(tabs->tabText(1), "Custom");
    EXPECT_EQ(tabs->currentIndex(), 0);
    static_assert(ToolBox::kColumns == 3);
    const ToolTray* tray = panel->tray();
    EXPECT_EQ(tray->cellRect(0).top(), tray->cellRect(1).top());
    EXPECT_LT(tray->cellRect(0).right(), tray->cellRect(1).left());
    EXPECT_EQ(tray->cellRect(0).left(), tray->cellRect(3).left());
    EXPECT_LT(tray->cellRect(0).bottom(), tray->cellRect(3).top());
    EXPECT_EQ(tray->cellAt(tray->cellRect(5).center()), 5);
    EXPECT_EQ(tray->cellAt(QPoint(-5, -5)), -1);
}

TEST_F(ToolPanelTest, TheTrayShowsThreeRowsAndScrolls) {
    auto* scroll = child<QScrollArea>("toolGridScroll");
    const int rows = (int(tools.group(0).cells.size()) + ToolBox::kColumns - 1) / ToolBox::kColumns;
    ASSERT_GT(rows, ToolTray::kVisibleRows) << "the default Basic group has more rows than show";
    const int cell = panel->tray()->cellRect(0).height();
    EXPECT_LT(scroll->height(), ToolTray::kVisibleRows * cell + ToolTray::kVisibleRows * 8);
    EXPECT_GE(scroll->height(), ToolTray::kVisibleRows * cell);
    EXPECT_GT(grid()->height(), scroll->height()) << "the rest scroll";
}

// Feedback: "Add a small, thick separator between the brush preview and the selectable tools, so i can adjust the size of it
// vertically to show more / less of the tools."
TEST_F(ToolPanelTest, TheHandleUnderTheTrayShowsMoreOrFewerRowsAndIsRemembered) {
    panel->resize(320, 1100);
    QCoreApplication::processEvents();
    auto* scroll = child<QScrollArea>("toolGridScroll");
    auto* handle = child<QWidget>("toolTrayHandle");
    const QRect gridArea(scroll->mapTo(panel.get(), QPoint(0, 0)), scroll->size());
    EXPECT_EQ(top("toolTrayHandle"), gridArea.bottom() + 1) << "right under the tools";
    EXPECT_EQ(top("brushPreview"), top("toolTrayHandle") + handle->height()) << "and right over the preview";
    EXPECT_EQ(handle->height(), Theme::kPanelSectionGap) << "it is the gap that was there";
    EXPECT_EQ(handle->cursor().shape(), Qt::SplitVCursor);

    const int pitch = panel->tray()->rowPitch();
    const int groupRows = panel->tray()->groupRows();
    ASSERT_GE(groupRows, 4);
    const auto drag = [&](int dy) {
        const QPoint from(handle->width() / 2, handle->height() / 2);
        const auto send = [&](QEvent::Type type, QPoint at, Qt::MouseButton button, Qt::MouseButtons buttons) {
            QMouseEvent e(type, QPointF(at), QPointF(handle->mapToGlobal(at)), button, buttons, Qt::NoModifier);
            QApplication::sendEvent(handle, &e);
            QCoreApplication::processEvents();
        };
        send(QEvent::MouseButtonPress, from, Qt::LeftButton, Qt::LeftButton);
        send(QEvent::MouseMove, from + QPoint(0, dy / 2), Qt::NoButton, Qt::LeftButton);
        send(QEvent::MouseMove, from + QPoint(0, dy), Qt::NoButton, Qt::LeftButton);
        send(QEvent::MouseButtonRelease, from + QPoint(0, dy), Qt::LeftButton, Qt::NoButton);
    };
    const int threeRows = scroll->height();
    EXPECT_EQ(panel->tray()->shownRows(), 3);

    drag(pitch);
    EXPECT_EQ(panel->tray()->shownRows(), 4);
    EXPECT_EQ(scroll->height(), threeRows + pitch);
    EXPECT_EQ(top("toolTrayHandle"), scroll->mapTo(panel.get(), QPoint(0, scroll->height())).y()) << "the handle follows";

    drag(-pitch / 3);
    EXPECT_EQ(panel->tray()->shownRows(), 4) << "a whole row at a time, to the nearest";
    drag(20 * pitch);
    EXPECT_EQ(panel->tray()->shownRows(), groupRows) << "no further than the group's rows";
    EXPECT_EQ(scroll->height(), grid()->height()) << "every tool shows, with nothing to scroll";
    drag(-20 * pitch);
    EXPECT_EQ(panel->tray()->shownRows(), 1) << "at least one row";

    drag(pitch);
    EXPECT_EQ(panel->tray()->shownRows(), 2);
    ToolPanel again(&tools);
    EXPECT_EQ(again.tray()->shownRows(), 2) << "remembered";
}

TEST_F(ToolPanelTest, TheTrayTakesNoMoreThanTheParametersCanGiveUp) {
    panel->resize(320, 520);
    QCoreApplication::processEvents();
    auto* handle = child<QWidget>("toolTrayHandle");
    auto* parameters = child<QScrollArea>("toolParametersScroll");
    const int height = panel->height();
    const QPoint from(handle->width() / 2, handle->height() / 2);
    QMouseEvent press(QEvent::MouseButtonPress, QPointF(from), QPointF(handle->mapToGlobal(from)), Qt::LeftButton, Qt::LeftButton,
                      Qt::NoModifier);
    QApplication::sendEvent(handle, &press);
    const QPoint to = from + QPoint(0, 30 * panel->tray()->rowPitch());
    QMouseEvent move(QEvent::MouseMove, QPointF(to), QPointF(handle->mapToGlobal(to)), Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(handle, &move);
    QMouseEvent release(QEvent::MouseButtonRelease, QPointF(to), QPointF(handle->mapToGlobal(to)), Qt::LeftButton, Qt::NoButton,
                        Qt::NoModifier);
    QApplication::sendEvent(handle, &release);
    QCoreApplication::processEvents();
    EXPECT_LE(panel->minimumSizeHint().height(), height) << "the panel asks for no more height than it had";
    EXPECT_GE(parameters->height(), parameters->minimumSizeHint().height());
}

TEST_F(ToolPanelTest, ClickingASlotSelectsIt) {
    click(2);
    EXPECT_EQ(tools.currentSlot(), kWater);
    EXPECT_EQ(tools.settings().tool, ToolType::WaterColor);
    click(10);
    EXPECT_EQ(tools.currentSlot(), kWater);
}

TEST_F(ToolPanelTest, TabsSwitchGroupsAndFollowTheCurrentTool) {
    QTabBar* tabs = panel->tray()->tabBar();
    tabs->setCurrentIndex(1);
    EXPECT_EQ(panel->tray()->group(), 1);
    EXPECT_EQ(tools.currentSlot(), kPencil);
    ASSERT_TRUE(tools.selectToolOfType(ToolType::Eraser));
    EXPECT_EQ(tabs->currentIndex(), 0);
}

TEST_F(ToolPanelTest, TrayFollowsRenameAndDelete) {
    ASSERT_TRUE(tools.renameSlot(kBrush, "Inker").ok());
    EXPECT_EQ(tools.slot(kBrush)->name, "Inker");
    ASSERT_TRUE(tools.createSlot({1, 0}, ToolType::Marker).ok());
    panel->tray()->tabBar()->setCurrentIndex(1);
    EXPECT_EQ(panel->tray()->group(), 1);
    ASSERT_TRUE(tools.deleteSlot({1, 0}).ok());
    EXPECT_EQ(tools.slot({1, 0}), nullptr);
}

TEST_F(ToolPanelTest, DraggingReordersSlots) {
    drop(kPencil, 10);
    EXPECT_EQ(tools.slot(kPencil), nullptr);
    EXPECT_EQ(tools.slot({0, 10})->type(), ToolType::Pencil);
    EXPECT_EQ(tools.currentSlot(), (SlotRef{0, 10}));
    drop({0, 10}, 1);
    EXPECT_EQ(tools.slot({0, 1})->type(), ToolType::Pencil);
    EXPECT_EQ(tools.slot({0, 10})->type(), ToolType::Brush);
}

TEST_F(ToolPanelTest, SlotContextMenuOffersTheSlotOperations) {
    std::unique_ptr<QMenu> menu(panel->tray()->createContextMenu(1));
    EXPECT_EQ(texts(*menu), (QStringList{"Duplicate", "Delete", "Initialize Settings", "Rename…"}));

    action(*menu, "Duplicate")->trigger();
    EXPECT_EQ(tools.toolCount(), 11);
    EXPECT_EQ(tools.slot({0, 10})->type(), ToolType::Brush);
    EXPECT_EQ(tools.currentSlot(), (SlotRef{0, 10}));

    BrushSettings s = tools.settings();
    s.size = 4321;
    tools.setSettings(s);
    std::unique_ptr<QMenu> again(panel->tray()->createContextMenu(10));
    action(*again, "Initialize Settings")->trigger();
    EXPECT_EQ(tools.settings(), defaultSettings(ToolType::Brush));

    action(*again, "Delete")->trigger();
    EXPECT_EQ(tools.toolCount(), 10);
}

TEST_F(ToolPanelTest, DeleteIsDisabledOnTheLastTool) {
    for (int i = 1; i < 10; ++i)
        ASSERT_TRUE(tools.deleteSlot({0, i}).ok());
    std::unique_ptr<QMenu> menu(panel->tray()->createContextMenu(0));
    EXPECT_FALSE(action(*menu, "Delete")->isEnabled());
}

TEST_F(ToolPanelTest, EmptyCellMenuCreatesEachToolType) {
    std::unique_ptr<QMenu> menu(panel->tray()->createContextMenu(10));
    EXPECT_EQ(texts(*menu), (QStringList{"Create Tool", "Pencil", "Brush", "Water Color", "Marker", "Eraser", "Bucket", "Blur", "Smudge",
                                         "Selection Pen", "Selection Eraser", "Pixel Art Pen", "Clone Stamp"}));
    EXPECT_FALSE(action(*menu, "Create Tool")->isEnabled());

    action(*menu, "Smudge")->trigger();
    ASSERT_NE(tools.slot({0, 10}), nullptr);
    EXPECT_EQ(tools.slot({0, 10})->type(), ToolType::Smudge);
    EXPECT_EQ(tools.currentSlot(), (SlotRef{0, 10}));

    action(*menu, "Selection Pen")->trigger();
    EXPECT_EQ(tools.slot({0, 10})->type(), ToolType::Smudge);
    std::unique_ptr<QMenu> next(panel->tray()->createContextMenu(11));
    action(*next, "Selection Pen")->trigger();
    EXPECT_EQ(tools.slot({0, 11})->type(), ToolType::SelectionPen);
    std::unique_ptr<QMenu> after(panel->tray()->createContextMenu(12));
    action(*after, "Selection Eraser")->trigger();
    EXPECT_EQ(tools.slot({0, 12})->type(), ToolType::SelectionEraser);
}

TEST_F(ToolPanelTest, RefusalsAreReported) {
    SignalCounter refused(panel.get(), &ToolPanel::refused);
    ASSERT_TRUE(tools.createSlot({0, 10}, ToolType::Pencil).ok());
    std::unique_ptr<QMenu> stale(panel->tray()->createContextMenu(11));
    ASSERT_TRUE(tools.createSlot({0, 11}, ToolType::Pencil).ok());
    action(*stale, "Brush")->trigger();
    EXPECT_EQ(refused.count(), 1);
}

TEST_F(ToolPanelTest, ControlsEditTheCurrentToolsSettings) {
    ASSERT_TRUE(tools.selectSlot(kBrush).ok());
    child<ValueBar>("sizeBar")->setValue(25.5);
    EXPECT_EQ(tools.settings().size, 255);
    child<ValueBar>("minSizeBar")->setValue(35);
    EXPECT_EQ(tools.settings().minSize, 35);
    const bool densityPressure = tools.settings().pressureDensity;
    child<QToolButton>("densityPressure")->click();
    EXPECT_EQ(tools.settings().pressureDensity, !densityPressure);
    child<ValueBar>("sensitivityDensityBar")->setValue(60);
    EXPECT_EQ(tools.settings().sensitivityDensity, 60);
    const bool sizePressure = tools.settings().pressureSize;
    child<QToolButton>("sizePressure")->click();
    EXPECT_EQ(tools.settings().pressureSize, !sizePressure);
    EXPECT_EQ(tools.slot(kPencil)->settings, defaultSettings(ToolType::Pencil));
    EXPECT_EQ(tools.slot(kWater)->settings, defaultSettings(ToolType::WaterColor));
}

TEST_F(ToolPanelTest, SizeBarIsLogarithmicOverTheWholeRange) {
    ValueBar* bar = child<ValueBar>("sizeBar");
    bar->setValue(bar->minimum());
    EXPECT_EQ(tools.settings().size, 1);
    bar->setValue(bar->maximum());
    EXPECT_EQ(bar->maximum(), 4000.0) << "the baseline";
    EXPECT_EQ(tools.settings().size, BrushSettings::kMaxSize);
    const QPointF middle(bar->width() / 2.0, bar->height() / 2.0);
    QMouseEvent press(QEvent::MouseButtonPress, middle, bar->mapToGlobal(middle), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QCoreApplication::sendEvent(bar, &press);
    QMouseEvent release(QEvent::MouseButtonRelease, middle, bar->mapToGlobal(middle), Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    QCoreApplication::sendEvent(bar, &release);
    EXPECT_GT(tools.settings().size, 100);
    EXPECT_LT(tools.settings().size, 400);
}

TEST_F(ToolPanelTest, ControlsFollowSettingsChangedElsewhere) {
    BrushSettings s = tools.settings();
    s.size = 333;
    s.density = 40;
    s.pressureSize = false;
    s.stabilizer = StabilizerLevel{3};
    tools.setSettings(s);
    EXPECT_DOUBLE_EQ(child<ValueBar>("sizeBar")->value(), 33.3);
    EXPECT_EQ(child<ValueBar>("densityBar")->value(), 40);
    EXPECT_EQ(child<ValueBar>("densityBar")->text(), QStringLiteral("40%"));
    EXPECT_FALSE(child<QToolButton>("sizePressure")->isChecked());
    EXPECT_EQ(child<StabilizerCombo>("stabilizerCombo")->level(), s.stabilizer);

    tools.stepSize(+1);
    EXPECT_DOUBLE_EQ(child<ValueBar>("sizeBar")->value(), 64.0);
}

TEST_F(ToolPanelTest, PixelArtPenHasItsOwnControls) {
    EXPECT_FALSE(shown("opacityBar"));
    ASSERT_TRUE(tools.selectToolOfType(ToolType::PixelArtPen));
    for (const char* name : {"sizeBar", "minSizeBar", "opacityBar"})
        EXPECT_TRUE(shown(name)) << name;
    for (const char* name : {"densityBar", "sharpnessBar"})
        EXPECT_FALSE(shown(name)) << name;
    EXPECT_EQ(child<ValueBar>("opacityBar")->maximum(), 255);

    child<ValueBar>("opacityBar")->setValue(128);
    EXPECT_EQ(tools.settings().opacity, 128);
}

TEST_F(ToolPanelTest, OnlyTheParametersOfTheToolsTypeAreShown) {
    EXPECT_TRUE(shown("sizeBar"));
    EXPECT_TRUE(shown("densityBar"));
    EXPECT_TRUE(shown("sharpnessBar"));
    EXPECT_FALSE(shown("opacityBar"));
}

// Feedback, 2026-11-06: "We should remove tip roundness from settings everywhere."
TEST_F(ToolPanelTest, NoToolHasTipRoundness) {
    for (const ToolType type : {ToolType::Pencil, ToolType::Brush, ToolType::Eraser, ToolType::Blur, ToolType::Smudge}) {
        ASSERT_TRUE(tools.selectToolOfType(type));
        EXPECT_EQ(panel->findChild<QWidget*>(QStringLiteral("tipRoundnessBar")), nullptr);
        EXPECT_EQ(panel->findChild<QWidget*>(QStringLiteral("endsGroup")), nullptr);
    }
}

// Feedback: "remove min blending from the mixing options".
TEST_F(ToolPanelTest, NoToolHasMinBlending) {
    for (const ToolType type : {ToolType::Brush, ToolType::WaterColor, ToolType::Marker}) {
        ASSERT_TRUE(tools.selectToolOfType(type));
        EXPECT_EQ(panel->findChild<QWidget*>(QStringLiteral("minBlendingBar")), nullptr);
    }
}

TEST_F(ToolPanelTest, MinDensityEditsTheCurrentTool) {
    child<ValueBar>("minDensityBar")->setValue(35);
    EXPECT_EQ(tools.settings().minDensity, 35);
}

TEST_F(ToolPanelTest, StartAndTrailAreGone) {
    EXPECT_EQ(panel->findChild<ValueBar*>("startLengthBar"), nullptr);
    EXPECT_EQ(panel->findChild<ValueBar*>("trailLengthBar"), nullptr);
}

TEST_F(ToolPanelTest, MixingShowsOnThePaintingToolsAndEditsThem) {
    ASSERT_TRUE(tools.selectToolOfType(ToolType::Brush));
    QCoreApplication::processEvents();
    EXPECT_EQ(panel->findChild<QWidget*>("widthCurve"), nullptr) << "there's no curve to drag";
    EXPECT_FALSE(child<QWidget>("sensitivitySizeBar")->isHidden());
    EXPECT_TRUE(shown("blendingBar"));
    child<ValueBar>("blendingBar")->setValue(35);
    EXPECT_EQ(tools.settings().blending, 35);
    const bool blendingPressure = tools.settings().pressureBlending;
    child<QToolButton>("blendingPressure")->click();
    EXPECT_EQ(tools.settings().pressureBlending, !blendingPressure);
    child<ValueBar>("dilutionBar")->setValue(60);
    EXPECT_EQ(tools.settings().dilution, 60);
    const bool dilutionPressure = tools.settings().pressureDilution;
    child<QToolButton>("dilutionPressure")->click();
    EXPECT_EQ(tools.settings().pressureDilution, !dilutionPressure);
    child<ValueBar>("persistenceBar")->setValue(75);
    EXPECT_EQ(tools.settings().persistence, 75);
    child<ValueBar>("blurPressureBar")->setValue(25);
    EXPECT_EQ(tools.settings().blurPressure, 25);
    child<ValueBar>("sensitivityBlendingBar")->setValue(130);
    EXPECT_EQ(tools.settings().sensitivityBlending, 130);
    for (const ToolType tool : {ToolType::WaterColor, ToolType::Marker}) {
        ASSERT_TRUE(tools.selectToolOfType(tool));
        QCoreApplication::processEvents();
        EXPECT_TRUE(shown("mixingGroup"));
        EXPECT_TRUE(shown("persistenceBar"));
        const bool marker = tool == ToolType::Marker;
        EXPECT_EQ(shown("dilutionBar"), !marker) << "the Marker has no Dilution";
        EXPECT_EQ(shown("blurPressureBar"), !marker) << "or Blur Pressure";
    }
    ASSERT_TRUE(tools.selectToolOfType(ToolType::Pencil));
    QCoreApplication::processEvents();
    EXPECT_FALSE(shown("blendingBar"));
    EXPECT_FALSE(shown("mixingGroup"));
}

TEST_F(ToolPanelTest, TheOldMixingControlsAreGone) {
    for (const char* name : {"reservoirPickUpBar", "reservoirRefillBar", "dragBar", "unevenBar", "coreBar", "mixBlurBar", "pigmentMixCheck",
                             "sensitivityRefillBar", "sensitivityMixBlurBar"})
        EXPECT_EQ(panel->findChild<QWidget*>(QString::fromLatin1(name)), nullptr) << name;
}

TEST_F(ToolPanelTest, ControlsThatPaintNothingAreGone) {
    for (const char* name :
         {"mixBar", "mixOpacityCheck", "softenBar", "paintLoadBar", "pressCoverBar", "keepColourBar", "sizeCurveBar", "startRampBar",
          "taperBar", "taperCurveBar", "flickBar", "pickUpBar", "layDownBar", "refillBar", "liftDelayBar", "liftReshapeBar"})
        EXPECT_EQ(panel->findChild<QWidget*>(QString::fromLatin1(name)), nullptr) << name;
}

TEST_F(ToolPanelTest, TheParametersAreGroupedAndAGroupShowsOnlyWhenTheToolHasARow) {
    const std::vector<const char*> order = {"sizeGroup", "densityGroup", "blurGroup", "edgeGroup", "cloneGroup"};
    struct Case {
        SlotRef slot;
        ToolType type;
        std::vector<const char*> groups;
    };
    const std::vector<Case> cases = {
        {kPencil, ToolType::Pencil, {"sizeGroup", "densityGroup", "edgeGroup"}},
        {kBrush, ToolType::Brush, {"sizeGroup", "densityGroup", "edgeGroup"}},
        {kWater, ToolType::WaterColor, {"sizeGroup", "densityGroup", "edgeGroup"}},
        {kMarker, ToolType::Marker, {"sizeGroup", "densityGroup", "edgeGroup"}},
        {kEraser, ToolType::Eraser, {"sizeGroup", "densityGroup", "edgeGroup"}},
        {kBlur, ToolType::Blur, {"sizeGroup", "densityGroup", "blurGroup", "edgeGroup"}},
        {kSmudge, ToolType::Smudge, {"sizeGroup", "densityGroup", "edgeGroup"}},
        {kClone, ToolType::Clone, {"sizeGroup", "densityGroup", "edgeGroup", "cloneGroup"}},
    };
    for (const Case& c : cases) {
        ASSERT_TRUE(tools.selectSlot(c.slot).ok());
        int lastTop = -1;
        for (const char* group : order) {
            const bool expected = std::ranges::find(c.groups, std::string_view(group)) != c.groups.end();
            EXPECT_EQ(shown(group), expected) << group << " for " << int(c.type);
            if (expected) {
                EXPECT_GT(top(group), lastTop) << group << " comes after the one before it";
                lastTop = top(group);
            }
        }
    }
    ASSERT_TRUE(tools.selectToolOfType(ToolType::PixelArtPen));
    for (const char* group : order)
        EXPECT_EQ(shown(group), std::string_view(group) == "sizeGroup" || std::string_view(group) == "densityGroup") << group;
    EXPECT_TRUE(child<QWidget>("densityGroup")->isAncestorOf(child<QWidget>("opacityBar"))) << "Opacity sits under DENSITY";
    EXPECT_FALSE(shown("densityBar")) << "its rows are the tool's own";
}

// Feedback: "Blur is barely noticeable. We should have a strength modifier for it." It's Blur Width, in a BLUR group, 0 to 100 %,
// starting at 50 %; a saved 0 shows as 0, so it can be raised.
TEST_F(ToolPanelTest, TheBlurHasABlurWidthSlider) {
    ASSERT_TRUE(tools.selectSlot(kBlur).ok());
    auto* width = child<ValueBar>("blurWidthBar");
    EXPECT_TRUE(child<QWidget>("blurGroup")->isAncestorOf(width));
    EXPECT_EQ(width->value(), 50);
    EXPECT_EQ(width->maximum(), 100);
    BrushSettings saved = tools.settings();
    saved.blurWidth = 0;
    tools.setSettings(saved);
    EXPECT_EQ(width->value(), 0) << "a saved width shows as saved";
    width->setValue(80);
    EXPECT_EQ(tools.settings().blurWidth, 80);
    ASSERT_TRUE(tools.selectSlot(kPencil).ok());
    EXPECT_FALSE(shown("blurGroup"));
}

TEST_F(ToolPanelTest, TheCloneStampHasACloneGroupWithAlignedAndSample) {
    ASSERT_TRUE(tools.selectSlot(kClone).ok());
    ASSERT_EQ(tools.settings().tool, ToolType::Clone);
    for (const char* row : {"cloneAlignedCheck", "cloneSampleRadio0", "cloneSampleRadio1"}) {
        EXPECT_TRUE(shown(row)) << row;
        EXPECT_TRUE(child<QWidget>("cloneGroup")->isAncestorOf(child<QWidget>(row))) << row;
    }
    EXPECT_TRUE(shown("sizeBar"));
    EXPECT_TRUE(shown("minSizeBar"));
    EXPECT_TRUE(shown("densityBar"));
    EXPECT_TRUE(shown("sharpnessBar"));
    EXPECT_FALSE(shown("opacityBar"));
    EXPECT_LT(top("sharpnessBar"), top("cloneAlignedCheck"));
    EXPECT_LT(top("cloneAlignedCheck"), top("cloneSampleRadio0"));

    EXPECT_TRUE(child<QCheckBox>("cloneAlignedCheck")->isChecked());
    EXPECT_TRUE(child<QRadioButton>("cloneSampleRadio0")->isChecked());
    EXPECT_FALSE(tools.settings().cloneAllLayers);

    child<QCheckBox>("cloneAlignedCheck")->click();
    EXPECT_FALSE(tools.settings().cloneAligned);
    child<QRadioButton>("cloneSampleRadio1")->click();
    EXPECT_TRUE(tools.settings().cloneAllLayers);

    ASSERT_TRUE(tools.selectSlot(kPencil).ok());
    EXPECT_FALSE(shown("cloneAlignedCheck"));
    ASSERT_TRUE(tools.selectSlot(kClone).ok());
    EXPECT_FALSE(child<QCheckBox>("cloneAlignedCheck")->isChecked());
    EXPECT_TRUE(child<QRadioButton>("cloneSampleRadio1")->isChecked());
    BrushSettings s = tools.settings();
    s.cloneAligned = true;
    s.cloneAllLayers = false;
    tools.setSettings(s);
    EXPECT_TRUE(child<QCheckBox>("cloneAlignedCheck")->isChecked());
    EXPECT_TRUE(child<QRadioButton>("cloneSampleRadio0")->isChecked());

    EXPECT_EQ(tools.slot(kClone)->name, "Clone Stamp");
    EXPECT_FALSE(toolTypeIcon(ToolType::Clone).isNull());
    EXPECT_FALSE(toolTypeIcon(ToolType::Clone).pixmap(16, 16).isNull());
    EXPECT_TRUE(test::saveScreenshot(panel->grab(), QStringLiteral("clone_stamp_panel")));
}

TEST_F(ToolPanelTest, RowsSitInTheirGroupsInOrder) {
    ASSERT_TRUE(tools.selectSlot(kBlur).ok());
    const auto under = [&](const char* group, const char* row) { return child<QWidget>(group)->isAncestorOf(child<QWidget>(row)); };
    for (const char* row : {"sizeBar", "minSizeBar"})
        EXPECT_TRUE(under("sizeGroup", row)) << row;
    EXPECT_TRUE(under("densityGroup", "densityBar"));
    EXPECT_TRUE(under("edgeGroup", "sharpnessBar"));
    EXPECT_LT(top("sizeBar"), top("minSizeBar"));
    EXPECT_LT(top("densityBar"), top("sharpnessBar"));

    child<QWidget>("pressureSection")->findChild<QToolButton*>("sectionHeader")->click();
    for (const char* bar : {"minSizeBar", "densityBar", "sharpnessBar", "sensitivitySizeBar"})
        EXPECT_EQ(left(bar), left("sizeBar")) << bar;
}

TEST_F(ToolPanelTest, HeadingsAreSmallCapitalsAndTheSectionsAreNamed) {
    for (const char* group : {"sizeGroup", "edgeGroup"}) {
        const auto labels = child<QWidget>(group)->findChildren<QLabel*>();
        ASSERT_FALSE(labels.isEmpty());
        EXPECT_EQ(labels.first()->property("role").toString(), QStringLiteral("sectionTitle")) << group;
        EXPECT_EQ(labels.first()->font().capitalization(), QFont::AllUppercase) << group;
    }
    EXPECT_EQ(child<QWidget>("edgeGroup")->findChildren<QLabel*>().first()->text(), QStringLiteral("Edge"));
    for (auto [section, title] : {std::pair{"pressureSection", "Pressure sensitivity"}, std::pair{"strokeSection", "Stroke"}}) {
        auto* header = child<QWidget>(section)->findChild<QToolButton*>("sectionHeader");
        ASSERT_NE(header, nullptr) << section;
        EXPECT_EQ(header->text(), QString::fromLatin1(title));
        EXPECT_EQ(header->font().capitalization(), QFont::AllUppercase);
        EXPECT_FALSE(header->isChecked()) << "folded at first";
    }
    EXPECT_EQ(panel->findChild<QWidget*>(QStringLiteral("miscSection")), nullptr) << "renamed Stroke";
}

TEST_F(ToolPanelTest, SlidersOfSharesShowAPercent) {
    ASSERT_TRUE(tools.selectSlot(kBrush).ok());
    BrushSettings s = tools.settings();
    s.minSize = 6;
    s.density = 100;
    s.sharpness = 0;
    s.sensitivitySize = 150;
    tools.setSettings(s);
    EXPECT_EQ(child<ValueBar>("minSizeBar")->text(), QStringLiteral("6%"));
    EXPECT_EQ(child<ValueBar>("densityBar")->text(), QStringLiteral("100%"));
    EXPECT_EQ(child<ValueBar>("sharpnessBar")->text(), QStringLiteral("0%"));
    EXPECT_EQ(child<ValueBar>("sensitivitySizeBar")->text(), QStringLiteral("150%")) << "the sensitivities run to 200 %";
    for (const char* name : {"minSizeBar", "densityBar", "sharpnessBar", "sensitivitySizeBar"})
        EXPECT_TRUE(child<ValueBar>(name)->text().endsWith(QLatin1Char('%'))) << name;
    EXPECT_EQ(child<ValueBar>("sensitivitySizeBar")->maximum(), 200);
    EXPECT_TRUE(child<ValueBar>("sizeBar")->text().endsWith(QStringLiteral(" px")));
    EXPECT_FALSE(child<ValueBar>("opacityBar")->text().contains(QLatin1Char('%'))) << "0 … 255 is not a percentage";
    EXPECT_EQ(child<ValueBar>("opacityBar")->maximum(), 255);
}

TEST_F(ToolPanelTest, TypingAValueTakesItWithOrWithoutThePercentSign) {
    ASSERT_TRUE(tools.selectSlot(kBrush).ok());
    type("sharpnessBar", QStringLiteral("43"));
    EXPECT_EQ(tools.settings().sharpness, 43);
    type("sharpnessBar", QStringLiteral("17%"));
    EXPECT_EQ(tools.settings().sharpness, 17);
    EXPECT_EQ(child<ValueBar>("sharpnessBar")->text(), QStringLiteral("17%"));
    child<QWidget>("pressureSection")->findChild<QToolButton*>("sectionHeader")->click();  // the editor needs the bar shown
    type("sensitivityDensityBar", QStringLiteral("43%"));
    EXPECT_EQ(tools.settings().sensitivityDensity, 43);
    type("sizeBar", QStringLiteral("12.5 px"));
    EXPECT_EQ(tools.settings().size, 125);
}

TEST_F(ToolPanelTest, PressureSensitivitiesAreFoldedAwayUntilOpened) {
    QWidget* section = child<QWidget>("pressureSection");
    EXPECT_FALSE(shown("sensitivitySizeBar"));
    section->findChild<QToolButton*>("sectionHeader")->click();
    EXPECT_TRUE(shown("sensitivitySizeBar"));
    child<ValueBar>("sensitivitySizeBar")->setValue(150);
    EXPECT_EQ(tools.settings().sensitivitySize, 150);
}

TEST_F(ToolPanelTest, PerToolStabilizerOverride) {
    child<QWidget>("strokeSection")->findChild<QToolButton*>("sectionHeader")->click();
    auto* combo = child<StabilizerCombo>("stabilizerCombo");
    EXPECT_FALSE(combo->level().has_value()) << "the Pencil starts at Global";
    EXPECT_EQ(combo->itemText(0), "Global");
    EXPECT_EQ(combo->itemText(1), "0");
    EXPECT_EQ(combo->itemText(combo->count() - 1), "20") << "the levels, 0 to 20";
    EXPECT_EQ(combo->findText("S-1"), -1);

    combo->setCurrentIndex(combo->findText("5"));
    ASSERT_TRUE(tools.settings().stabilizer.has_value());
    EXPECT_EQ(*tools.settings().stabilizer, (StabilizerLevel{5}));
    combo->setCurrentIndex(combo->findText("8"));
    EXPECT_EQ(*tools.settings().stabilizer, (StabilizerLevel{8}));
    combo->setCurrentIndex(0);
    EXPECT_FALSE(tools.settings().stabilizer.has_value());

    ASSERT_TRUE(tools.selectSlot(kBrush).ok());
    EXPECT_EQ(combo->currentIndex(), 0);
}

TEST_F(ToolPanelTest, ReserveAndRecallButtons) {
    auto* recall = child<QPushButton>("recallButton");
    EXPECT_FALSE(recall->isEnabled());
    child<ValueBar>("minSizeBar")->setValue(20);
    child<QPushButton>("reserveButton")->click();
    EXPECT_TRUE(recall->isEnabled());
    child<ValueBar>("minSizeBar")->setValue(70);
    recall->click();
    EXPECT_EQ(tools.settings().minSize, 20);
    EXPECT_EQ(child<ValueBar>("minSizeBar")->value(), 20);
}

TEST_F(ToolPanelTest, PanelIsNeverNarrowerThanItsParameters) {
    const int needed = panel->parameters()->minimumSizeHint().width();
    EXPECT_GE(panel->minimumSizeHint().width(), needed);
    panel->resize(panel->minimumSizeHint().width(), 700);
    panel->show();
    QApplication::processEvents();
    for (const char* name : {"sizeBar", "densityBar", "recallButton"}) {
        const QWidget* w = child<QWidget>(name);
        const int right = w->mapTo(panel.get(), QPoint(w->width(), 0)).x();
        EXPECT_LE(right, panel->width()) << name;
        EXPECT_GE(w->width(), w->minimumSizeHint().width()) << name;
    }
}

TEST_F(ToolPanelTest, PreviewSitsBetweenTrayAndParameters) {
    const BrushPreviewStrip* preview = panel->preview();
    const int previewTop = preview->mapTo(panel.get(), QPoint(0, 0)).y();
    const int trayBottom = panel->tray()->mapTo(panel.get(), QPoint(0, 0)).y() + panel->tray()->height();
    const int paramsTop = panel->parameters()->mapTo(panel.get(), QPoint(0, 0)).y();
    EXPECT_GE(previewTop, trayBottom);
    EXPECT_GE(paramsTop, previewTop + preview->height());
    EXPECT_GE(preview->height(), 50);
    EXPECT_LE(preview->height(), 70);
}

TEST_F(ToolPanelTest, PreviewRedrawsWhenSettingsOrColourChange) {
    BrushPreviewStrip* preview = panel->preview();
    const QImage before = preview->image();
    ASSERT_FALSE(before.isNull());
    const int renders = preview->renderCount();

    child<ValueBar>("sizeBar")->setValue(4.0);
    EXPECT_TRUE(preview->redrawPending());
    const QImage smaller = preview->image();
    EXPECT_FALSE(preview->redrawPending());
    EXPECT_EQ(preview->renderCount(), renders + 1);
    EXPECT_NE(smaller, before);

    tools.setColor(Qt::red);
    EXPECT_TRUE(preview->redrawPending());
    EXPECT_NE(preview->image(), smaller);

    ASSERT_TRUE(tools.selectSlot(kBrush).ok());
    const QImage brush = preview->image();
    EXPECT_NE(brush, smaller);
}

namespace {

// The Pencil's preview stroke, by how many pixels down a column differ from the ground at its top.
int strokePixelsInColumn(const QImage& image, int x) {
    const QColor ground = image.pixelColor(x, 0);
    int count = 0;
    for (int y = 0; y < image.height(); ++y) {
        const QColor c = image.pixelColor(x, y);
        count += std::max({std::abs(c.red() - ground.red()), std::abs(c.green() - ground.green()), std::abs(c.blue() - ground.blue())}) > 60
                     ? 1
                     : 0;
    }
    return count;
}

const QColor kStrokeRed(200, 30, 30);

constexpr int kPreviewStrokeStart = 39;  // kStrokeMargin of 240 px
constexpr int kPreviewStrokeEnd = 200;  // the start's mirror image

QImage previewOf(const BrushSettings& settings, const QColor& color) {
    BrushPreviewStrip strip;
    strip.resize(240, BrushPreviewStrip::kHeight);
    strip.setBrush(settings, color);
    return strip.image();
}

QImage previewOf(ToolType tool, const QColor& color) {
    return previewOf(defaultSettings(tool), color);
}

}

// The preview of the Pencil after Initialize Settings started as a round, blunt blob. Its pressure now rises from 0 and falls
// back to it, so it tapers in as it tapers out.
TEST(BrushPreview, ThePencilTapersInAndOut) {
    const QImage image = previewOf(ToolType::Pencil, kStrokeRed);
    const int middle = strokePixelsInColumn(image, 120);
    ASSERT_GT(middle, 4);
    EXPECT_LT(strokePixelsInColumn(image, kPreviewStrokeStart + 2), middle) << "no blunt blob where it starts";
    EXPECT_LT(strokePixelsInColumn(image, kPreviewStrokeEnd - 2), middle);
}

// At Size 69.5 the preview still started blunt and rounded and ended in a point. In the reference both ends come to a
// point, mirror images of each other.
TEST(BrushPreview, ThePencilsEndsAreMirrorImages) {
    BrushSettings settings = defaultSettings(ToolType::Pencil);
    settings.size = 695;
    const QImage image = previewOf(settings, kStrokeRed);
    ASSERT_GT(strokePixelsInColumn(image, 120), 30);
    EXPECT_LT(strokePixelsInColumn(image, kPreviewStrokeStart + 2), 6) << "no blunt start";
    for (int in = 2; in <= 56; in += 6)
        EXPECT_NEAR(strokePixelsInColumn(image, kPreviewStrokeStart + in), strokePixelsInColumn(image, kPreviewStrokeEnd - in), 1) << in;
}

// Feedback: "center the stroke preview", and "take a picture of how it looks like in doodlebold".
// Both of the stroke's tips are on the middle and the sides meet them there, as in the mocks'
// program's; the Brush's faint start picks up the left side's colour and lays it thinned, not its own.
TEST(BrushPreview, TheStrokeIsCentredAndStartsOnTheFirstColour) {
    const QImage image = previewOf(ToolType::Pencil, kStrokeRed);
    const auto tipRows = [&](int x0, int x1) {
        int top = image.height(), bottom = -1;
        for (int x = x0; x <= x1; ++x)
            for (int y = 0; y < image.height(); ++y)
                if (std::abs(image.pixelColor(x, y).red() - image.pixelColor(x, 0).red()) > 10)
                    top = std::min(top, y), bottom = std::max(bottom, y);
        return std::pair{top, bottom};
    };
    const auto [startTop, startBottom] = tipRows(kPreviewStrokeStart - 4, kPreviewStrokeStart + 2);
    const auto [endTop, endBottom] = tipRows(kPreviewStrokeEnd - 2, kPreviewStrokeEnd + 4);
    ASSERT_LE(startTop, startBottom) << "the tip is there";
    ASSERT_LE(endTop, endBottom);
    const double middle = (image.height() - 1) / 2.0;
    EXPECT_NEAR((startTop + startBottom) / 2.0, middle, 1.0) << "the first tip is on the middle";
    EXPECT_NEAR((endTop + endBottom) / 2.0, middle, 1.0) << "and the last";

    const QImage brush = previewOf(ToolType::Brush, QColor(0, 0, 255));
    EXPECT_EQ(brush.pixelColor(2, brush.height() / 2), QColor(0, 255, 0)) << "the left side, green for blue, starts on the middle";
    EXPECT_NE(brush.pixelColor(2, brush.height() / 2 - 1), QColor(0, 255, 0));
    // Its faint start, over the checker just above the side: green thinned, with no blue in it.
    for (const int x : {kPreviewStrokeStart + 6, kPreviewStrokeStart + 10}) {
        const QColor tip = brush.pixelColor(x, brush.height() / 2 - 3);
        EXPECT_GT(tip.green(), tip.red() + 40) << x;
        EXPECT_LE(tip.blue(), tip.red() + 12) << "no more blue than the checker's grey, at " << x;
    }
    EXPECT_EQ(previewOf(defaultSettings(ToolType::Brush), QColor(0, 0, 255)), brush);
}

// The reference shows the same preview at every stabilizer level.
TEST(BrushPreview, TheStabilizerLevelDoesNotChangeIt) {
    BrushSettings settings = defaultSettings(ToolType::Pencil);
    const QImage plain = previewOf(settings, Qt::black);
    for (const QString& label : {QStringLiteral("0"), QStringLiteral("15"), QStringLiteral("20")}) {
        settings.stabilizer = StabilizerLevel::fromLabel(label);
        EXPECT_EQ(previewOf(settings, Qt::black), plain) << label.toStdString();
    }
}

TEST(BrushPreview, ThePencilShowsOnlyItsStrokeInTheChosenColour) {
    const QImage image = previewOf(ToolType::Pencil, kStrokeRed);
    EXPECT_EQ(image.pixelColor(2, 2), QColor(Qt::black));
    EXPECT_EQ(image.pixelColor(237, 61), QColor(64, 64, 64));
    const QColor middle = image.pixelColor(120, 32);
    EXPECT_NEAR(middle.red(), 200, 2);
    EXPECT_NEAR(middle.green(), 30, 2);
}

// The Pencil's ground is black on the left and grey on the right whatever the colour, as the reference's is, so a black stroke
// shows on the grey half and a white one on both.
TEST(BrushPreview, ThePencilsGroundIsTheSameWhateverTheColour) {
    for (const QColor& c : {QColor(Qt::black), QColor(Qt::white), kStrokeRed, QColor(250, 235, 150)}) {
        const QImage image = previewOf(ToolType::Pencil, c);
        EXPECT_EQ(image.pixelColor(2, 2), QColor(Qt::black)) << c.name().toStdString();
        EXPECT_EQ(image.pixelColor(237, 2), QColor(64, 64, 64)) << c.name().toStdString();
    }
    EXPECT_GT(strokePixelsInColumn(previewOf(ToolType::Pencil, Qt::black), 150), 4) << "black shows on the grey half";
}

TEST(BrushPreview, AWhitePencilStillShows) {
    const QImage image = previewOf(ToolType::Pencil, Qt::white);
    EXPECT_NE(image.pixelColor(120, 32), image.pixelColor(2, 2));
}

TEST(BrushPreview, TheSidesMeetInTheMiddle) {
    const QImage image = previewOf(ToolType::Marker, QColor(200, 30, 30));
    const int low = image.height() - 2;
    EXPECT_EQ(image.pixelColor(2, low).hsvSaturation(), 255);
    EXPECT_NE(image.pixelColor(237, low), image.pixelColor(2, low));
    EXPECT_EQ(image.pixelColor(119, low), image.pixelColor(2, low));
    EXPECT_EQ(image.pixelColor(120, low), image.pixelColor(237, low));
}

namespace {

const QColor kEraserGrey(138, 143, 152), kLightStripe(216, 221, 230), kDarkStripe(108, 122, 150);

}

// Feedback: "The eraser stroke itself should be a transparent stroke, where you can see the tiled square background that other
// transparent previews see." One plain grey layer, and the transparency checker where it erases, whatever the colour.
TEST(BrushPreview, TheEraserShowsTheCheckerWhereItErases) {
    const QImage image = previewOf(ToolType::Eraser, QColor(200, 30, 30));
    EXPECT_TRUE(test::saveScreenshot(QPixmap::fromImage(image), QStringLiteral("preview_eraser")));
    EXPECT_EQ(image.pixelColor(2, 2), kEraserGrey) << "no coloured halves";
    EXPECT_EQ(image.pixelColor(237, image.height() - 2), kEraserGrey);
    // The reference's preview checker: 17 px squares, white and (243, 243, 243), white at the top left.
    const auto checkerAt = [](int x, int y) { return ((x / 17 + y / 17) & 1) == 0 ? QColor(255, 255, 255) : QColor(243, 243, 243); };
    const int y = image.height() / 2;
    for (const int x : {112, 118, 120, 126})
        EXPECT_EQ(image.pixelColor(x, y), checkerAt(x, y)) << "fully erased shows the checker, at " << x;
    EXPECT_NE(checkerAt(118, y), checkerAt(120, y)) << "both of its squares";
    EXPECT_EQ(previewOf(ToolType::Eraser, Qt::blue), image) << "the colour doesn't change it";
    EXPECT_LT(strokePixelsInColumn(image, kPreviewStrokeStart + 2), strokePixelsInColumn(image, 120)) << "it comes to a point";
}

// Feedback: "Since blur does not depend on color, we can just show a striped background to show how it is blurred." The mocks'
// program's stripes, 10 px wide, light from the left edge, the same as the Smudge's and the Clone Stamp's.
TEST(BrushPreview, TheBlurShowsHowItBlursStripes) {
    // The stripes' contrast along a row from x to x + 20, on the row near the middle where the stroke flattens them most.
    const auto contrastAcross = [](const QImage& image, int x0, int y0) {
        int flattest = 255;
        for (int y = y0 - 6; y <= y0 + 6; ++y) {
            int least = 255, most = 0;
            for (int x = x0; x < x0 + 20; ++x) {
                least = std::min(least, image.pixelColor(x, y).red());
                most = std::max(most, image.pixelColor(x, y).red());
            }
            flattest = std::min(flattest, most - least);
        }
        return flattest;
    };
    BrushSettings settings = defaultSettings(ToolType::Blur);
    const QImage image = previewOf(settings, QColor(200, 30, 30));
    EXPECT_TRUE(test::saveScreenshot(QPixmap::fromImage(image), QStringLiteral("preview_blur")));
    EXPECT_EQ(image.pixelColor(1, 2), kLightStripe) << "stripes 10 px wide, light from the left edge";
    EXPECT_EQ(image.pixelColor(11, 2), kDarkStripe);
    const QImage smudge = previewOf(ToolType::Smudge, QColor(200, 30, 30)), clone = previewOf(ToolType::Clone, QColor(200, 30, 30));
    for (const int x : {1, 9, 11, 19, 21, 237})
        EXPECT_TRUE(image.pixelColor(x, 2) == smudge.pixelColor(x, 2) && image.pixelColor(x, 2) == clone.pixelColor(x, 2)) << x;
    EXPECT_EQ(contrastAcross(image, 0, image.height() / 2), 108) << "untouched before the stroke";
    // Feedback: "Blur still looks weak in the preview." At the starting Blur Width the stripes are mostly gone along the stroke,
    // its pressure full over its middle.
    const int y = image.height() / 2;
    for (const int x0 : {60, 80, 100, 120, 140})
        EXPECT_LT(contrastAcross(image, x0, y), 40) << x0;
    EXPECT_EQ(previewOf(settings, Qt::blue), image) << "the colour doesn't change it";
    // It follows the tool's own Blur Width: at 0, as the saved Blur has it, it doesn't blur, and the preview says so.
    for (const int width : {0, 25, 100}) {
        settings.blurWidth = width;
        const QImage atWidth = previewOf(settings, QColor(200, 30, 30));
        EXPECT_TRUE(test::saveScreenshot(QPixmap::fromImage(atWidth), QStringLiteral("preview_blur_width%1").arg(width)));
        if (width == 0)
            EXPECT_EQ(contrastAcross(atWidth, 110, y), 108);
        else
            EXPECT_LT(contrastAcross(atWidth, 110, y), width == 25 ? 90 : 20) << width;
    }
}

// Feedback: "Smudge should also have the same background as clone or blur." and "Pixel art pen should have the same background
// as Pencil."
TEST(BrushPreview, TheSmudgeHasStripesAndThePixelArtPenTheHalves) {
    const QImage smudge = previewOf(ToolType::Smudge, QColor(200, 30, 30));
    EXPECT_TRUE(test::saveScreenshot(QPixmap::fromImage(smudge), QStringLiteral("preview_smudge")));
    EXPECT_EQ(smudge.pixelColor(5, 2), kLightStripe);
    EXPECT_EQ(smudge.pixelColor(15, 2), kDarkStripe);
    EXPECT_EQ(previewOf(ToolType::Smudge, Qt::blue), smudge);
    const QImage pixelArt = previewOf(ToolType::PixelArtPen, kStrokeRed);
    EXPECT_TRUE(test::saveScreenshot(QPixmap::fromImage(pixelArt), QStringLiteral("preview_pixel_art_pen")));
    const QImage pencil = previewOf(ToolType::Pencil, kStrokeRed);
    EXPECT_EQ(pixelArt.pixelColor(2, 2), pencil.pixelColor(2, 2)) << "black on the left";
    EXPECT_EQ(pixelArt.pixelColor(237, 2), pencil.pixelColor(237, 2)) << "grey on the right";
    EXPECT_GT(strokePixelsInColumn(pixelArt, 120), 4) << "with its stroke on them";
}

// The Clone Stamp's preview copies from half a stripe to the right (5 px), so inside the stroke every stripe moves half its width.
TEST(BrushPreview, TheCloneStampSwapsTheStripes) {
    const QImage image = previewOf(ToolType::Clone, QColor(200, 30, 30));
    EXPECT_TRUE(test::saveScreenshot(QPixmap::fromImage(image), QStringLiteral("preview_clone_stamp")));
    const int y = image.height() / 2 + 1;  // the stroke's middle there
    EXPECT_EQ(image.pixelColor(127, 2), kLightStripe) << "x 120 to 129 is a light stripe";
    EXPECT_EQ(image.pixelColor(127, y), kDarkStripe) << "inside the stroke it copies x 132, in the dark stripe, 5 px to the right";
    EXPECT_EQ(image.pixelColor(137, y), kLightStripe) << "and x 142, in the light one after it";
    EXPECT_EQ(image.pixelColor(122, y), kLightStripe) << "x 127 is still light";
    EXPECT_EQ(previewOf(ToolType::Clone, Qt::blue), image);
}

// Feedback: "The brush preview looks strange for a few frames when first selecting the bucket tool, then water color, or any other
// one." Every frame shown after a switch is already the new tool's finished preview.
TEST_F(ToolPanelTest, PreviewShowsTheNewToolsPictureFromTheFirstFrame) {
    for (const SlotRef next : {kWater, kPencil, kBrush, kMarker, kEraser, kSmudge, kClone, kBlur}) {
        ASSERT_TRUE(tools.selectSlot(kBucket).ok());
        QCoreApplication::processEvents();
        ASSERT_TRUE(tools.selectSlot(next).ok());
        std::vector<QImage> frames;
        for (int i = 0; i < 8; ++i) {
            frames.push_back(panel->preview()->grab().toImage());
            QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
        }
        for (int i = 0; i < 100 && panel->preview()->redrawPending(); ++i) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
            QThread::msleep(5);
        }
        const QImage settled = panel->preview()->grab().toImage();
        for (std::size_t i = 0; i < frames.size(); ++i)
            EXPECT_EQ(frames[i], settled) << "frame " << i << " after choosing " << toolTypeName(tools.settings().tool).toStdString();
    }
}

TEST_F(ToolPanelTest, PreviewRedrawsOnTheTimerAndMergesBursts) {
    BrushPreviewStrip* preview = panel->preview();
    preview->image();
    const int renders = preview->renderCount();
    for (int v = 20; v <= 60; v += 10)
        child<ValueBar>("densityBar")->setValue(v);
    EXPECT_EQ(preview->renderCount(), renders);
    for (int i = 0; i < 100 && preview->redrawPending(); ++i) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        QThread::msleep(5);
    }
    EXPECT_FALSE(preview->redrawPending());
    EXPECT_EQ(preview->renderCount(), renders + 1);
}

::testing::AssertionResult sameColour(const QColor& a, const QColor& b) {
    if (std::abs(a.red() - b.red()) <= 1 && std::abs(a.green() - b.green()) <= 1 && std::abs(a.blue() - b.blue()) <= 1)
        return ::testing::AssertionSuccess();
    return ::testing::AssertionFailure() << a.name().toStdString() << " vs " << b.name().toStdString();
}

TEST_F(ToolPanelTest, PreviewStrokeIsTheSelectedColour) {
    ASSERT_TRUE(tools.selectSlot(kPencil).ok());
    for (const QColor c :
         {QColor(0x1f, 0x3a, 0x8a), QColor(0xe0, 0x20, 0x20), QColor(0x20, 0xa0, 0x40), QColor(Qt::black), QColor(Qt::white)}) {
        tools.setColor(c);
        const QImage img = panel->preview()->image();
        const QColor mid = img.pixelColor(img.width() / 2, img.height() / 2);
        EXPECT_LE(std::abs(mid.red() - c.red()) + std::abs(mid.green() - c.green()) + std::abs(mid.blue() - c.blue()), 6)
            << c.name().toStdString() << " vs " << mid.name().toStdString();
    }
}

TEST_F(ToolPanelTest, PreviewShowsThePressureTaper) {
    ASSERT_TRUE(tools.selectSlot(kPencil).ok());
    BrushSettings s = tools.settings();
    s.size = 300;
    s.pressureSize = true;
    tools.setSettings(s);
    tools.setColor(kStrokeRed);
    const QImage img = panel->preview()->image();
    EXPECT_GT(strokePixelsInColumn(img, img.width() / 2), strokePixelsInColumn(img, img.width() / 4) + 5)
        << "wider at full pressure than at the light end";
}

TEST_F(ToolPanelTest, PreviewShowsAWhiteBrush) {
    ASSERT_TRUE(tools.selectSlot(kPencil).ok());
    tools.setColor(Qt::white);
    const QImage img = panel->preview()->image();
    EXPECT_NE(img.pixelColor(img.width() / 2, img.height() / 2), img.pixelColor(1, 1)) << "the stroke stands out from the ground";
}

// The lower half's two sides are the pure colours that complete a triad with the hue, 120° back on the left and on on the right;
// the upper half is empty, over the transparency checker.
TEST_F(ToolPanelTest, PreviewHasTwoColouredSidesFromTheChosenColour) {
    ASSERT_TRUE(tools.selectSlot(kBrush).ok());
    tools.setColor(QColor::fromHsv(0, 220, 220));
    const QImage img = panel->preview()->image();
    const int low = img.height() - 3;
    const QColor left = img.pixelColor(2, low), right = img.pixelColor(img.width() - 3, low);
    EXPECT_NEAR(left.hsvHue(), 240, 2);
    EXPECT_NEAR(right.hsvHue(), 120, 2);
    for (const QColor& side : {left, right}) {
        EXPECT_EQ(side.hsvSaturation(), 255);
        EXPECT_EQ(side.value(), 255);
    }
    EXPECT_EQ(img.pixelColor(2, 2), QColor(255, 255, 255)) << "the upper half is empty, over the reference's checker";
    EXPECT_EQ(img.pixelColor(20, 2), QColor(243, 243, 243));
}

// Where the Eraser takes the grey away, the transparency checker shows, as asked (it
// had shown black).
TEST_F(ToolPanelTest, PreviewEraserShowsTheCheckerWhereItErases) {
    ASSERT_TRUE(tools.selectSlot(kEraser).ok());
    const QImage img = panel->preview()->image();
    const QColor erased = img.pixelColor(img.width() / 2, img.height() / 2);
    EXPECT_EQ(erased.hsvSaturation(), 0);
    EXPECT_GE(erased.value(), 190) << "a square of the light checker";
}

TEST_F(ToolPanelTest, PreviewShowsTheStrokeAtItsTrueWidth) {
    ASSERT_TRUE(tools.selectSlot(kPencil).ok());
    tools.setColor(kStrokeRed);
    const auto widestWith = [this](int sizeTenths) {
        BrushSettings s = tools.settings();
        s.size = sizeTenths;
        tools.setSettings(s);
        const QImage img = panel->preview()->image();
        return strokePixelsInColumn(img, img.width() / 2);
    };
    EXPECT_NEAR(widestWith(100), 10, 2);
    EXPECT_NEAR(widestWith(500), 50, 3);
}

// Black, white and grey keep the hue the wheel is on, so moving into the wheel's dark or pale corner never changes the sides; only
// turning the hue does.
TEST_F(ToolPanelTest, PreviewSidesFollowOnlyTheWheelsHue) {
    ASSERT_TRUE(tools.selectSlot(kBrush).ok());
    ColorState wheel;
    panel->setColorState(&wheel);
    const auto sides = [this] {
        const QImage img = panel->preview()->image();
        return std::pair{img.pixelColor(2, img.height() - 3), img.pixelColor(img.width() - 3, img.height() - 3)};
    };
    wheel.setColor(QColor::fromHsv(200, 255, 255), 200.0);
    tools.setColor(wheel.color());
    const auto vivid = sides();
    EXPECT_NEAR(vivid.first.hsvHue(), 80, 2);
    EXPECT_NEAR(vivid.second.hsvHue(), 320, 2);
    for (const QColor& c : {QColor(Qt::black), QColor(Qt::white), QColor(128, 128, 128), QColor::fromHsv(200, 60, 90)}) {
        wheel.setColor(c, 200.0);
        tools.setColor(c);
        EXPECT_EQ(sides(), vivid) << c.name().toStdString();
    }
    wheel.setColor(Qt::black, 0.0);
    EXPECT_NE(sides(), vivid) << "turning the hue does change them";
}

TEST_F(ToolPanelTest, ARunOfColourChangesIsRenderedAtMostOncePerDelay) {
    BrushPreviewStrip* preview = panel->preview();
    preview->image();
    const int renders = preview->renderCount();
    for (int i = 0; i < 60; ++i)
        tools.setColor(QColor::fromHsv(i * 5, 200, 200));
    EXPECT_EQ(preview->renderCount(), renders);
    for (int i = 0; i < 100 && preview->redrawPending(); ++i) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        QThread::msleep(5);
    }
    EXPECT_EQ(preview->renderCount(), renders + 1);
}

TEST_F(ToolPanelTest, ScreenshotsOfThePanel) {
    tools.setColor(QColor(0x1f, 0x3a, 0x8a));
    ASSERT_TRUE(tools.selectSlot(kWater).ok());
    child<QWidget>("pressureSection")->findChild<QToolButton*>("sectionHeader")->click();
    QCoreApplication::processEvents();
    panel->preview()->image();
    EXPECT_TRUE(test::saveScreenshot(panel->grab(), QStringLiteral("tool_panel_water_color")));

    ASSERT_TRUE(tools.selectSlot(kPencil).ok());
    tools.setColor(Qt::black);
    QCoreApplication::processEvents();
    panel->preview()->image();
    EXPECT_TRUE(test::saveScreenshot(panel->grab(), QStringLiteral("tool_panel_pencil")));

    ASSERT_TRUE(tools.selectSlot(kMarker).ok());
    tools.setColor(QColor(0xc0, 0x30, 0x40));
    QCoreApplication::processEvents();
    panel->preview()->image();
    EXPECT_TRUE(test::saveScreenshot(panel->grab(), QStringLiteral("tool_panel_marker")));

    ASSERT_TRUE(tools.selectSlot(kBrush).ok());
    tools.setColor(QColor(0x1f, 0x3a, 0x8a));
    QCoreApplication::processEvents();
    panel->preview()->image();
    EXPECT_TRUE(test::saveScreenshot(panel->grab(), QStringLiteral("tool_panel_brush")));
}

namespace {

class ToolWindowTest : public testing::Test {
protected:
    void SetUp() override {
        QStandardPaths::setTestModeEnabled(true);  // never touch the real tools.json
        test::applyTheme();
        QFile::remove(ToolBoxStore::defaultPath());
        window = std::make_unique<MainWindow>();
        window->newDocument();  // before show(): the offscreen platform cannot paint a canvas added to a visible window
        window->show();
        QCoreApplication::processEvents();
    }
    void TearDown() override { QFile::remove(ToolBoxStore::defaultPath()); }

    QAction* command(const char* id) const {
        QAction* a = window->command(QString::fromLatin1(id));
        EXPECT_NE(a, nullptr) << id;
        return a;
    }

    std::unique_ptr<MainWindow> window;
};

}

TEST_F(ToolWindowTest, ToolKeysSelectTools) {
    ToolBox* t = window->tools();
    EXPECT_EQ(command("tool.eraser")->shortcut(), QKeySequence(Qt::Key_E));
    command("tool.eraser")->trigger();
    EXPECT_EQ(t->settings().tool, ToolType::Eraser);
    command("tool.brush")->trigger();
    EXPECT_EQ(t->settings().tool, ToolType::Brush);
    EXPECT_EQ(command("tool.brush")->shortcut(), QKeySequence(Qt::Key_V));
    command("tool.waterColor")->trigger();
    EXPECT_EQ(t->settings().tool, ToolType::WaterColor);
    EXPECT_EQ(command("tool.waterColor")->shortcut(), QKeySequence(Qt::Key_C));
    command("tool.pencil")->trigger();
    EXPECT_EQ(t->settings().tool, ToolType::Pencil);
    EXPECT_EQ(command("tool.pencil")->shortcut(), QKeySequence(Qt::Key_N));
}

TEST_F(ToolWindowTest, SizeAndDensityKeys) {
    ToolBox* t = window->tools();
    BrushSettings start = t->settings();
    start.size = 320;
    t->setSettings(start);
    EXPECT_EQ(command("brush.sizePrevious")->shortcut(), QKeySequence(Qt::Key_BracketLeft));
    EXPECT_EQ(command("brush.sizeNext")->shortcut(), QKeySequence(Qt::Key_BracketRight));
    const int before = t->settings().size;
    command("brush.sizeNext")->trigger();
    EXPECT_GT(t->settings().size, before);
    command("brush.sizePrevious")->trigger();
    EXPECT_EQ(t->settings().size, before);

    command("brush.density30")->trigger();
    EXPECT_EQ(t->settings().density, 30);
    EXPECT_EQ(command("brush.density30")->shortcut(), QKeySequence(Qt::Key_3));
    command("brush.density100")->trigger();
    EXPECT_EQ(t->settings().density, 100);
    EXPECT_EQ(command("brush.density100")->shortcut(), QKeySequence(Qt::Key_0));
}

TEST_F(ToolWindowTest, NoTwoActionsShareAShortcut) {
    QMap<QKeySequence, QString> seen;
    for (const QAction* a : window->findChildren<QAction*>())
        for (const QKeySequence& k : a->shortcuts()) {
            EXPECT_FALSE(seen.contains(k)) << k.toString().toStdString() << ": " << a->text().toStdString() << " and "
                                           << seen.value(k).toStdString();
            seen.insert(k, a->text());
        }
}

// Every tool has a command, even those with no key at the start, so a key can reach it once bound.
TEST_F(ToolWindowTest, EveryToolHasACommand) {
    const struct {
        const char* id;
        ToolType type;
    } tray[] = {{"tool.marker", ToolType::Marker},
                {"tool.pixelArtPen", ToolType::PixelArtPen},
                {"tool.blur", ToolType::Blur},
                {"tool.smudge", ToolType::Smudge},
                {"tool.cloneStamp", ToolType::Clone}};
    for (const auto& [id, type] : tray) {
        QAction* a = command(id);
        ASSERT_NE(a, nullptr);
        EXPECT_TRUE(a->shortcut().isEmpty()) << id << ": no key at the start";
        a->trigger();
        EXPECT_FALSE(window->tools()->fixedTool().has_value()) << id;
        EXPECT_EQ(window->tools()->settings().tool, type) << id;
    }
    const struct {
        const char* id;
        FixedTool tool;
    } fixed[] = {{"tool.text", FixedTool::Text},     {"tool.move", FixedTool::Move}, {"tool.zoom", FixedTool::Zoom},
                 {"tool.rotate", FixedTool::Rotate}, {"tool.hand", FixedTool::Hand}, {"tool.eyedropper", FixedTool::Eyedropper}};
    for (const auto& [id, tool] : fixed) {
        QAction* a = command(id);
        ASSERT_NE(a, nullptr);
        a->trigger();
        EXPECT_EQ(window->tools()->fixedTool(), tool) << id;
    }
}

// A fresh settings folder shows the chosen global level, 4.
TEST_F(ToolWindowTest, AFreshStartShowsStabilizerFour) {
    auto* combo = window->findChild<QToolBar*>("quickBar")->findChild<StabilizerCombo*>("globalStabilizer");
    ASSERT_NE(combo, nullptr);
    EXPECT_EQ(combo->level(), StabilizerLevel{4});
    EXPECT_EQ(combo->currentText(), "4");
}

TEST_F(ToolWindowTest, GlobalStabilizerLivesInTheQuickBar) {
    auto* combo = window->findChild<QToolBar*>("quickBar")->findChild<StabilizerCombo*>("globalStabilizer");
    ASSERT_NE(combo, nullptr);
    EXPECT_EQ(combo->itemText(0), "0");
    combo->setCurrentIndex(combo->findText("2"));
    EXPECT_EQ(window->tools()->options().stabilizer, (StabilizerLevel{2}));
    BrushOptions o = window->tools()->options();
    o.stabilizer = StabilizerLevel{9};
    window->tools()->setOptions(o);
    EXPECT_EQ(combo->level(), o.stabilizer);
}

TEST_F(ToolWindowTest, TheInterimSizeBoxesAreGone) {
    auto* bar = window->findChild<QToolBar*>("quickBar");
    ASSERT_NE(bar, nullptr);
    EXPECT_TRUE(bar->findChildren<QDoubleSpinBox*>().isEmpty());
}

TEST_F(ToolWindowTest, ToolPanelIsDocked) {
    EXPECT_NE(window->findChild<QDockWidget*>("toolsDock"), nullptr);
    EXPECT_NE(window->findChild<ToolPanel*>(), nullptr);
    EXPECT_TRUE(test::saveScreenshot(window->grab(), QStringLiteral("main_window_with_tool_panel")));
}

TEST_F(ToolWindowTest, ToolsAreSavedOnClose) {
    window->tools()->setColor(QColor(1, 2, 3));
    ASSERT_TRUE(window->tools()->selectSlot(kMarker).ok());
    window->close();
    ASSERT_TRUE(QFile::exists(ToolBoxStore::defaultPath()));

    ToolBox fresh;
    ToolBoxStore store(&fresh, ToolBoxStore::defaultPath());
    ASSERT_TRUE(store.load().ok());
    EXPECT_EQ(fresh.color(), QColor(1, 2, 3));
    EXPECT_EQ(fresh.currentSlot(), kMarker);
}

TEST_F(ToolWindowTest, SavedToolsAreLoadedAtStartUp) {
    window->tools()->setColor(QColor(9, 8, 7));
    BrushSettings s = window->tools()->settings();
    s.size = 4242;
    window->tools()->setSettings(s);
    window->close();
    window = std::make_unique<MainWindow>();
    EXPECT_EQ(window->tools()->color(), QColor(9, 8, 7));
    EXPECT_EQ(window->tools()->settings().size, 4242);
}

TEST_F(ToolWindowTest, DockedPanelsHaveNoTitleBars) {
    const auto docks = window->findChildren<QDockWidget*>();
    ASSERT_GE(docks.size(), 4);
    for (QDockWidget* d : docks) {
        if (!d->isFloating()) {
            EXPECT_NE(d->titleBarWidget(), nullptr) << qPrintable(d->objectName());
        }
    }
    QDockWidget* tools = window->findChild<QDockWidget*>("toolsDock");
    tools->setFloating(true);
    EXPECT_EQ(tools->titleBarWidget(), nullptr);
    tools->setFloating(false);
    EXPECT_NE(tools->titleBarWidget(), nullptr);
}

// Feedback: "The tip options for tools shows off view. Verify that this is also fixed for texture." Every row of the tip shape's and the
// texture's options lies inside the panel's view, at the width the Tools panel usually has beside the Colour panel and at its narrowest,
// for each kind of picture (a shape, a bristle, a blot map) and a texture, on the Pencil and the Brush.
TEST(ToolPanelFit, TheTipShapeAndTextureOptionsFitThePanel) {
    QStandardPaths::setTestModeEnabled(true);
    QTemporaryDir settingsDir;
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDir.path());
    test::applyTheme();
    QTemporaryDir pictures;
    BrushResourceLibrary library(pictures.path());
    ToolBox tools;
    ToolPanel panel(&tools, &library);
    panel.resize(260, 900);
    panel.show();
    QCoreApplication::processEvents();
    const int narrowest = panel.minimumSizeHint().width();
    struct Case {
        ToolType tool;
        QString form, texture, name;
    };
    const std::vector<Case> cases{
        {ToolType::Brush, QStringLiteral("Soft Edge"), QStringLiteral("None"), QStringLiteral("brush-softedge")},
        {ToolType::Pencil, QStringLiteral("Soft Edge"), QStringLiteral("None"), QStringLiteral("pencil-softedge")},
        {ToolType::Brush, QStringLiteral("Flat Bristle"), QStringLiteral("None"), QStringLiteral("brush-bristle")},
        {ToolType::Brush, QStringLiteral("Blots"), QStringLiteral("None"), QStringLiteral("brush-blots")},
        {ToolType::Brush, QStringLiteral("Round"), QStringLiteral("Paper Grain"), QStringLiteral("brush-papergrain")},
        {ToolType::Pencil, QStringLiteral("Soft Edge"), QStringLiteral("Canvas"), QStringLiteral("pencil-softedge-canvas")}};
    auto* scroll = panel.findChild<QScrollArea*>();
    ASSERT_NE(scroll, nullptr);
    auto* tipSection = panel.findChild<QWidget*>(QStringLiteral("tipShapeSection"));
    ASSERT_NE(tipSection, nullptr);
    auto* header = tipSection->findChild<QToolButton*>();
    ASSERT_NE(header, nullptr);
    header->setChecked(true);  // open, as it was left
    for (const int width : {260, narrowest}) {
        for (const Case& c : cases) {
            tools.selectOrCreateToolOfType(c.tool);
            BrushSettings s = tools.settings();
            s.form = c.form;
            s.texture = c.texture;
            tools.setSettings(s);
            panel.resize(std::max(width, panel.minimumSizeHint().width()), 900);
            for (int i = 0; i < 3; ++i)
                QCoreApplication::processEvents();
            const QWidget* view = scroll->viewport();
            const int right = view->width();
            for (const char* section : {"formSection", "textureSection"}) {
                const QWidget* box = panel.findChild<QWidget*>(QString::fromLatin1(section));
                ASSERT_NE(box, nullptr) << section;
                if (!box->isVisibleTo(&panel))
                    continue;
                for (const QWidget* w : box->findChildren<QWidget*>()) {
                    if (!w->isVisibleTo(&panel) || w->width() <= 0)
                        continue;
                    const int edge = w->mapTo(view, QPoint(w->width(), 0)).x();
                    EXPECT_LE(edge, right) << c.name.toStdString() << " at " << width << " px: " << w->objectName().toStdString() << " ("
                                           << w->metaObject()->className() << ") ends at " << edge << " of " << right;
                }
                EXPECT_TRUE(
                    test::saveScreenshot(const_cast<QWidget*>(box)->grab(),
                                         QStringLiteral("tool-panel-%1-%2-%3").arg(c.name).arg(width).arg(QString::fromLatin1(section))));
            }
            // Each box's Reset, at its foot, fits too, whole, inside its box.
            for (const char* name : {"formResetButton", "textureResetButton"}) {
                const QWidget* reset = panel.findChild<QWidget*>(QString::fromLatin1(name));
                ASSERT_NE(reset, nullptr) << name;
                EXPECT_TRUE(reset->isVisibleTo(&panel)) << name;
                EXPECT_LE(reset->mapTo(view, QPoint(reset->width(), 0)).x(), right)
                    << c.name.toStdString() << " at " << width << " px: " << name;
                EXPECT_GE(reset->width(), reset->minimumSizeHint().width()) << c.name.toStdString() << " at " << width << " px: " << name;
                const QWidget* box = reset->parentWidget();
                EXPECT_TRUE(box->objectName() == QStringLiteral("formSection") || box->objectName() == QStringLiteral("textureSection"))
                    << name;
                EXPECT_GE(box->height(), reset->geometry().bottom()) << name << ": inside its box";
            }
            EXPECT_TRUE(test::saveScreenshot(panel.grab(), QStringLiteral("tool-panel-%1-%2").arg(c.name).arg(width)));
            scroll->ensureWidgetVisible(panel.findChild<QWidget*>(QStringLiteral("textureSection")));
            QCoreApplication::processEvents();
            EXPECT_TRUE(test::saveScreenshot(panel.grab(), QStringLiteral("tool-panel-%1-%2-texture").arg(c.name).arg(width)));
            scroll->ensureVisible(0, 0);
        }
    }
}

namespace {

// The Tools panel with its own library, the Tip Shape & Texture section open, for the Resets' tests.
struct TipResetPanel {
    TipResetPanel() : library(pictures.path()), panel(&tools, &library) {
        panel.resize(320, 900);
        panel.show();
        panel.findChild<QWidget*>(QStringLiteral("tipShapeSection"))->findChild<QToolButton*>()->setChecked(true);
        QCoreApplication::processEvents();
    }
    // Clicks box `prefix`'s Reset ("form" or "texture") and answers its question with Reset (`confirm`) or Cancel once it shows;
    // returns its text.
    QString resetAnswering(const QString& prefix, bool confirm) {
        QString asked;
        QTimer::singleShot(0, [&asked, prefix, confirm] {
            auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
            if (!box || box->objectName() != prefix + QStringLiteral("ResetDialog"))
                return;
            asked = box->text();
            EXPECT_EQ(box->defaultButton(), box->findChild<QAbstractButton*>(prefix + QStringLiteral("ResetCancel")))
                << "Cancel, the default";
            box->findChild<QAbstractButton*>(prefix + (confirm ? QStringLiteral("ResetConfirm") : QStringLiteral("ResetCancel")))->click();
        });
        panel.findChild<QPushButton*>(prefix + QStringLiteral("ResetButton"))->click();
        QCoreApplication::processEvents();
        return asked;
    }
    BrushSettings changed() const {
        BrushSettings s = tools.settings();
        s.form = QStringLiteral("Flat Nib");
        s.shapeAngleControl = 0, s.shapeAngle = -45, s.shapeSpacing = 1, s.shapeDensity = 30;
        s.texture = QStringLiteral("Canvas");
        s.textureStrength = 40, s.textureScale = 200;
        s.size = 777, s.sharpness = 10, s.edge = 2;
        return s;
    }

    // Before the panel, which reads the tray's rows from the settings: the test's own, never the real ones.
    static bool isolated(const QTemporaryDir& dir) {
        QStandardPaths::setTestModeEnabled(true);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, dir.path());
        test::applyTheme();
        return true;
    }
    QTemporaryDir settingsDir, pictures;
    bool ready = isolated(settingsDir);
    BrushResourceLibrary library;
    ToolBox tools;
    ToolPanel panel;
};

}

// Each box's Reset asks first, saying which settings; Cancel leaves the tool's settings as they were, without a change.
TEST(TipReset, CancelChangesNothing) {
    TipResetPanel p;
    p.tools.selectOrCreateToolOfType(ToolType::Brush);
    p.tools.setSettings(p.changed());
    const BrushSettings before = p.tools.settings();
    int changes = 0;
    QObject::connect(&p.tools, &ToolBox::settingsChanged, [&changes] { ++changes; });
    EXPECT_EQ(p.resetAnswering(QStringLiteral("form"), false),
              QStringLiteral("Reset this tool's tip shape settings to their starting values?"));
    EXPECT_EQ(p.resetAnswering(QStringLiteral("texture"), false),
              QStringLiteral("Reset this tool's texture settings to their starting values?"));
    EXPECT_EQ(p.tools.settings(), before);
    EXPECT_EQ(changes, 0);
}

// Confirmed, the Tip shape box's Reset puts back the tool's starting tip shape settings in one change, and the box shows them; the
// texture, size, Shape and Sharpness stay.
TEST(TipReset, TheTipShapesResetPutsBackOnlyTheTipShape) {
    TipResetPanel p;
    p.tools.selectOrCreateToolOfType(ToolType::Brush);
    p.tools.setSettings(p.changed());
    int changes = 0;
    QObject::connect(&p.tools, &ToolBox::settingsChanged, [&changes] { ++changes; });
    EXPECT_FALSE(p.resetAnswering(QStringLiteral("form"), true).isEmpty());
    BrushSettings expected = p.changed();
    resetTipShape(expected);
    EXPECT_EQ(p.tools.settings(), expected);
    EXPECT_EQ(p.tools.settings().form, defaultSettings(ToolType::Brush).form);
    EXPECT_EQ(p.tools.settings().texture, QStringLiteral("Canvas")) << "the texture stays";
    EXPECT_EQ(p.tools.settings().size, 777);
    EXPECT_EQ(changes, 1);
    auto* list = p.panel.findChild<QComboBox*>(QStringLiteral("brushFormList"));
    ASSERT_NE(list, nullptr);
    EXPECT_EQ(list->currentText(), defaultSettings(ToolType::Brush).form);
}

// Confirmed, the Texture box's Reset puts back the tool's starting texture settings in one change; the tip shape stays.
TEST(TipReset, TheTexturesResetPutsBackOnlyTheTexture) {
    TipResetPanel p;
    p.tools.selectOrCreateToolOfType(ToolType::Brush);
    p.tools.setSettings(p.changed());
    int changes = 0;
    QObject::connect(&p.tools, &ToolBox::settingsChanged, [&changes] { ++changes; });
    EXPECT_FALSE(p.resetAnswering(QStringLiteral("texture"), true).isEmpty());
    BrushSettings expected = p.changed();
    resetTexture(expected);
    EXPECT_EQ(p.tools.settings(), expected);
    EXPECT_EQ(p.tools.settings().form, QStringLiteral("Flat Nib")) << "the tip shape stays";
    EXPECT_EQ(p.tools.settings().shapeSpacing, 1);
    EXPECT_EQ(changes, 1);
    auto* strength = p.panel.findChild<ValueBar*>(QStringLiteral("textureStrengthBar"));
    ASSERT_NE(strength, nullptr);
    EXPECT_EQ(strength->value(), defaultSettings(ToolType::Brush).textureStrength);
}

// Choosing Flat Nib in the panel brings its settings file: a tool that already holds Flat Nib with the old starting settings
// (Auto, Spacing 25) gets them by choosing another tip first, since choosing the tip it has changes nothing; and after the Tip shape
// box's Reset, choosing Flat Nib gives None, -45 and 1 again.
TEST(TipReset, ChoosingFlatNibBringsItsCalligraphicSettings) {
    TipResetPanel p;
    p.tools.selectOrCreateToolOfType(ToolType::Brush);
    BrushSettings old = p.tools.settings();
    old.form = QStringLiteral("Flat Nib");
    old.shapeAngleControl = 1, old.shapeAngle = 0, old.shapeSpacing = 25;
    p.tools.setSettings(old);
    QCoreApplication::processEvents();
    auto* list = p.panel.findChild<QComboBox*>(QStringLiteral("brushFormList"));
    ASSERT_NE(list, nullptr);
    const auto choose = [list](const QString& name) {
        const int index = list->findText(name);
        ASSERT_GE(index, 0) << name.toStdString();
        list->setCurrentIndex(index);
        emit list->activated(index);
        QCoreApplication::processEvents();
    };
    choose(QStringLiteral("Flat Nib"));
    EXPECT_EQ(p.tools.settings().shapeSpacing, 25) << "the tip it has, chosen again: nothing changes";
    choose(QStringLiteral("Round"));
    choose(QStringLiteral("Flat Nib"));
    EXPECT_EQ(p.tools.settings().shapeAngleControl, 0);
    EXPECT_EQ(p.tools.settings().shapeAngle, -45);
    EXPECT_EQ(p.tools.settings().shapeSpacing, 1);
    p.resetAnswering(QStringLiteral("form"), true);
    EXPECT_EQ(p.tools.settings().form, defaultSettings(ToolType::Brush).form);
    choose(QStringLiteral("Flat Nib"));
    EXPECT_EQ(p.tools.settings().shapeAngleControl, 0);
    EXPECT_EQ(p.tools.settings().shapeAngle, -45);
    EXPECT_EQ(p.tools.settings().shapeSpacing, 1);
}
