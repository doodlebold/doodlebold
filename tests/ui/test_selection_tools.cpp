#include "core/document/Document.h"
#include "core/document/TransformSession.h"
#include "core/input/TabletStats.h"
#include "core/tools/ToolBox.h"
#include "ui/CanvasView.h"
#include "ui/Theme.h"
#include "ui/panels/BrushPreviewStrip.h"
#include "ui/panels/FixedToolPanels.h"
#include "ui/ValueBar.h"
#include "ui/panels/ToolParameters.h"
#include "ui/panels/ToolPanel.h"
#include "ui/panels/ToolTray.h"
#include "ui/tools/SelectionOverlay.h"
#include "ui/tools/SelectionPenTool.h"
#include "ScreenshotSupport.h"
#include "../SignalCounter.h"

#include <QApplication>
#include <QComboBox>
#include <QElapsedTimer>
#include <QMenu>
#include <QKeyEvent>
#include <QLayout>
#include <QMouseEvent>
#include <QPainter>
#include <QToolButton>

#include <gtest/gtest.h>

#include <array>
#include <tuple>

using namespace doodlebold;
using H = TransformSession::Handle;
using test::SignalCounter;

namespace {

constexpr int kOne = kChannelOne;
const QSize kDoc(200, 150);

// The selection pens paint on a thread of their own and finish once the pen has lifted and it's all painted, on the event loop.
void waitUntilPainted(const Document& doc) {
    constexpr qint64 kMostWaitMs = 5000;
    QElapsedTimer waited;
    waited.start();
    while (doc.isPainting() && waited.elapsed() < kMostWaitMs)
        QCoreApplication::processEvents();
}

// A canvas of 800 × 600 on a 200 × 150 document at 100 %, centred: document point d is at widget point d + (300, 225).
struct Rig {
    Rig() : doc(kDoc, Qt::white), canvas(&doc, &stats) {
        canvas.setToolBox(&tools);
        canvas.resize(800, 600);
        canvas.resetView();
        FixedToolOptions o = tools.fixedOptions();
        o.marquee.cutAndMove = false;  // plain drags select; the tests that move turn it on
        o.wand.antialias = false;      // exact pixel counts
        o.lasso.antialias = false;
        tools.setFixedOptions(o);
        BrushOptions b = tools.options();
        b.stabilizer = StabilizerLevel{0};  // freehand lasso paths exactly as sent; the smoothing test turns it on
        tools.setOptions(b);
    }

    static QPointF w(const QPointF& d) { return d + QPointF(300, 225); }

    void send(QEvent::Type type, const QPointF& at, Qt::KeyboardModifiers mods = {}, bool down = true,
              Qt::MouseButton button = Qt::LeftButton) {
        const QPointF widget = w(at);
        QMouseEvent e(type, widget, widget, button, down ? button : Qt::NoButton, mods);
        QCoreApplication::sendEvent(&canvas, &e);
    }
    void press(const QPointF& at, Qt::KeyboardModifiers m = {}) { send(QEvent::MouseButtonPress, at, m); }
    void move(const QPointF& at, Qt::KeyboardModifiers m = {}) { send(QEvent::MouseMove, at, m); }
    void release(const QPointF& at, Qt::KeyboardModifiers m = {}) { send(QEvent::MouseButtonRelease, at, m, false); }
    void doubleClick(const QPointF& at) { send(QEvent::MouseButtonDblClick, at); }
    void click(const QPointF& at, Qt::KeyboardModifiers m = {}) {
        press(at, m);
        release(at, m);
    }
    void drag(const QPointF& from, const QPointF& to, Qt::KeyboardModifiers m = {}) {
        press(from, m);
        for (int i = 1; i <= 4; ++i)
            move(from + (to - from) * (i / 4.0), m);
        release(to, m);
    }
    void key(int k, Qt::KeyboardModifiers m = {}) {
        QKeyEvent e(QEvent::KeyPress, k, m);
        QCoreApplication::sendEvent(&canvas, &e);
    }

    void marquee(QPoint a, QPoint b, Qt::KeyboardModifiers m = {}) { drag(QPointF(a) + QPointF(.5, .5), QPointF(b) + QPointF(.5, .5), m); }

    void use(FixedTool t) { tools.setFixedTool(t); }
    void paint(const QRect& r, const QColor& c) {
        doc.paintTarget(doc.currentLayerId()).value()->fill(r, c);
        doc.notifyPixelsChanged(doc.currentLayerId(), r);
    }
    int selected() const {
        int n = 0;
        if (!doc.selection())
            return 0;
        for (int y = 0; y < kDoc.height(); ++y)
            for (int x = 0; x < kDoc.width(); ++x)
                n += doc.selection()->at(x, y) > 0;
        return n;
    }
    int steps() { return doc.undoStack()->count(); }
    QRect content() const { return doc.currentLayer()->pixels.contentBounds(true); }

    TabletStats stats;
    Document doc;
    ToolBox tools;
    CanvasView canvas;
};

}

class SelectionToolsTest : public testing::Test {
protected:
    void SetUp() override { test::applyTheme(); }
};

TEST_F(SelectionToolsTest, FixedToolsRowSwitchesBetweenFixedAndTrayTools) {
    ToolBox tools;
    ToolPanel panel(&tools);
    panel.resize(320, 760);
    panel.show();
    for (FixedTool t : kFixedTools)
        EXPECT_NE(panel.fixedToolButton(t), nullptr);
    EXPECT_TRUE(panel.fixedToolButton(FixedTool::Marquee)->toolTip().contains(QStringLiteral("Marquee")));
    for (FixedTool t : kFixedTools)
        EXPECT_FALSE(panel.fixedToolButton(t)->isChecked());
    EXPECT_TRUE(panel.preview()->isVisibleTo(&panel));

    panel.fixedToolButton(FixedTool::Lasso)->click();
    EXPECT_EQ(tools.fixedTool(), FixedTool::Lasso);
    EXPECT_TRUE(panel.fixedToolButton(FixedTool::Lasso)->isChecked());
    EXPECT_FALSE(panel.fixedToolButton(FixedTool::Marquee)->isChecked());
    EXPECT_TRUE(panel.fixedPanels()->isVisibleTo(&panel));
    EXPECT_FALSE(panel.parameters()->isVisibleTo(&panel));
    EXPECT_FALSE(panel.preview()->isVisibleTo(&panel));
    EXPECT_EQ(panel.fixedPanels()->currentWidget()->objectName(), QStringLiteral("lassoPage"));

    panel.fixedToolButton(FixedTool::MagicWand)->click();
    EXPECT_EQ(panel.fixedPanels()->currentWidget()->objectName(), QStringLiteral("wandPage"));
    panel.fixedToolButton(FixedTool::Hand)->click();
    EXPECT_EQ(panel.fixedPanels()->currentWidget()->objectName(), QStringLiteral("hintPage_hand"));

    tools.selectSlot(tools.currentSlot()).ok();
    EXPECT_FALSE(tools.fixedTool());
    EXPECT_FALSE(panel.fixedToolButton(FixedTool::Hand)->isChecked());
    EXPECT_TRUE(panel.parameters()->isVisibleTo(&panel));
    EXPECT_TRUE(panel.preview()->isVisibleTo(&panel));
}

TEST_F(SelectionToolsTest, PanelsEditTheOptionsAndFollowThem) {
    ToolBox tools;
    ToolPanel panel(&tools);
    panel.resize(320, 760);
    panel.show();
    FixedToolPanels* fp = panel.fixedPanels();
    const auto find = [fp](const char* name) { return fp->findChild<QWidget*>(QString::fromLatin1(name)); };

    tools.setFixedTool(FixedTool::Marquee);
    static_cast<QAbstractButton*>(find("cutAndMoveCheck"))->click();
    EXPECT_FALSE(tools.fixedOptions().marquee.cutAndMove);
    static_cast<QAbstractButton*>(find("mode_rotate"))->click();
    EXPECT_EQ(tools.fixedOptions().marquee.transformMode, TransformMode::Rotate);
    auto* final = static_cast<QComboBox*>(find("finalCombo"));
    EXPECT_EQ(final->currentText(), QStringLiteral("Blocky"));
    EXPECT_EQ(static_cast<QComboBox*>(find("draggingCombo"))->currentText(), QStringLiteral("Blocky"));
    EXPECT_EQ(static_cast<QComboBox*>(find("previewCombo"))->currentText(), QStringLiteral("Blocky"));
    final->setCurrentIndex(final->findText("Sharper"));
    emit final->activated(final->currentIndex());
    EXPECT_EQ(tools.fixedOptions().marquee.final, transform::Interpolation::Lanczos3);
    static_cast<ValueBar*>(find("perspectiveBar"))->setValue(40);
    EXPECT_EQ(tools.fixedOptions().marquee.perspective, 40);
    EXPECT_FALSE(find("transformOk")->isEnabled());
    EXPECT_FALSE(find("transformCancel")->isEnabled());
    fp->setTransformActive(true);
    EXPECT_TRUE(find("transformOk")->isEnabled());

    tools.setFixedTool(FixedTool::Lasso);
    static_cast<QAbstractButton*>(find("lasso_polygonal"))->click();
    EXPECT_EQ(tools.fixedOptions().lasso.mode, LassoMode::Polygonal);
    static_cast<QAbstractButton*>(find("lassoAntialiasCheck"))->click();
    EXPECT_FALSE(tools.fixedOptions().lasso.antialias);

    tools.setFixedTool(FixedTool::MagicWand);
    EXPECT_TRUE(find("transparencyBar")->isVisibleTo(fp));
    EXPECT_FALSE(find("thresholdBar")->isVisibleTo(fp));
    static_cast<QAbstractButton*>(find("wand_similar"))->click();
    EXPECT_EQ(tools.fixedOptions().wand.mode, selection::WandMode::SimilarColour);
    EXPECT_FALSE(find("transparencyBar")->isVisibleTo(fp));
    EXPECT_TRUE(find("thresholdBar")->isVisibleTo(fp));
    static_cast<ValueBar*>(find("thresholdBar"))->setValue(33);
    static_cast<ValueBar*>(find("gapBar"))->setValue(7);
    static_cast<QAbstractButton*>(find("source_merged"))->click();
    static_cast<QAbstractButton*>(find("wandAntialiasCheck"))->click();
    const WandToolOptions& w = tools.fixedOptions().wand;
    EXPECT_EQ(w.colourThreshold, 33);
    EXPECT_EQ(w.gapTolerance, 7);
    EXPECT_EQ(w.source, WandSource::MergedImage);
    EXPECT_FALSE(w.antialias);

    FixedToolOptions o = tools.fixedOptions();
    o.wand.gapTolerance = 21;
    tools.setFixedOptions(o);
    EXPECT_EQ(static_cast<ValueBar*>(find("gapBar"))->value(), 21);

    for (FixedTool t : {FixedTool::Marquee, FixedTool::Lasso, FixedTool::MagicWand}) {
        tools.setFixedTool(t);
        EXPECT_TRUE(test::saveScreenshot(panel.grab(), QStringLiteral("tool_panel_%1").arg(fixedToolKey(t))));
    }
    // The Smooth Edge choice was left out of the layout and drawn over the Select heading.
    const QWidget* smooth = find("wandAntialiasCheck");
    EXPECT_GT(smooth->y(), find("source_merged")->y()) << "under the Source choices";

    // Feedback: "The padding between these needs to match the rest of the UI paddings". The brush panel's spacing is the
    // standard for every panel: a label kPanelColumnGap from its bar, rows kPanelRowGap apart.
    tools.setFixedTool(FixedTool::MagicWand);
    panel.resize(300, 700);
    panel.show();
    QCoreApplication::processEvents();
    const QWidget* gapBar = find("gapBar");
    const QWidget* gapLabel = gapBar->parentWidget()->layout()->itemAt(0)->widget();
    EXPECT_EQ(gapBar->x() - (gapLabel->x() + gapLabel->width()), Theme::kPanelColumnGap);
    const QWidget* merged = find("source_merged");
    const QWidget* current = find("source_current");
    EXPECT_EQ(merged->y() - (current->y() + current->height()), Theme::kPanelRowGap);
}

TEST_F(SelectionToolsTest, SelectionPenAndEraserAreCreatableTrayTools) {
    ToolBox tools;
    ToolPanel panel(&tools);
    panel.show();
    std::unique_ptr<QMenu> menu(panel.tray()->createContextMenu(10));  // the first empty cell
    QStringList names;
    for (const QAction* a : menu->actions())
        names << a->text();
    EXPECT_TRUE(names.contains("Selection Pen"));
    EXPECT_TRUE(names.contains("Selection Eraser"));
    EXPECT_TRUE(names.contains("Clone Stamp"));
}

class CanvasToolsTest : public testing::Test {
protected:
    void SetUp() override { test::applyTheme(); }
    Rig r;
};

TEST_F(CanvasToolsTest, MarqueeSelectsTheInclusivePixelRectangle) {
    r.use(FixedTool::Marquee);
    const int before = r.steps();
    r.marquee({20, 20}, {79, 59});
    ASSERT_TRUE(r.doc.selection());
    EXPECT_EQ(r.selected(), 60 * 40);
    EXPECT_EQ(r.doc.selection()->at(20, 20), kOne);
    EXPECT_EQ(r.doc.selection()->at(79, 59), kOne);
    EXPECT_EQ(r.doc.selection()->at(80, 59), 0);
    EXPECT_EQ(r.steps(), before + 1);
    EXPECT_EQ(r.doc.undoStack()->undoText(), QStringLiteral("Select"));
}

TEST_F(CanvasToolsTest, MarqueeModifiersAddSubtractAndIntersect) {
    r.use(FixedTool::Marquee);
    const QPoint a0(20, 20), a1(79, 59), b0(50, 40), b1(109, 89);
    const std::array<std::tuple<Qt::KeyboardModifiers, int>, 4> cases = {
        {{Qt::NoModifier, 3000}, {Qt::ShiftModifier, 4800}, {Qt::AltModifier, 1800}, {Qt::AltModifier | Qt::ShiftModifier, 600}}};
    for (const auto& [mods, expected] : cases) {
        r.doc.deselect().ok();
        r.marquee(a0, a1);
        r.marquee(b0, b1, mods);
        EXPECT_EQ(r.selected(), expected) << int(mods);
    }
}

TEST_F(CanvasToolsTest, AltSubtractsWithASelectionToolAndPicksColourWithout) {
    r.paint(QRect(0, 0, 200, 150), QColor(10, 200, 30));
    r.tools.setColor(Qt::black);
    r.use(FixedTool::Marquee);
    r.marquee({20, 20}, {79, 59});
    r.marquee({30, 30}, {49, 49}, Qt::AltModifier);
    EXPECT_EQ(r.selected(), 2400 - 400);
    EXPECT_EQ(r.tools.color(), QColor(Qt::black));

    r.tools.setFixedTool(std::nullopt);
    r.click({100, 100}, Qt::AltModifier);
    EXPECT_EQ(r.tools.color(), QColor(10, 200, 30));
}

TEST_F(CanvasToolsTest, AltClickSetsTheCloneSourceOnlyWhileTheCloneStampIsActive) {
    r.paint(QRect(10, 10, 30, 30), QColor(10, 200, 30));
    r.tools.setColor(Qt::black);
    ASSERT_TRUE(r.tools.selectToolOfType(ToolType::Pencil));
    r.click({20.5, 20.5}, Qt::AltModifier);
    EXPECT_EQ(r.tools.color(), QColor(10, 200, 30));
    EXPECT_FALSE(r.canvas.cloneStamp().hasSource());

    r.tools.setColor(Qt::black);
    ASSERT_TRUE(r.tools.selectToolOfType(ToolType::Clone));
    r.click({20.5, 20.5}, Qt::AltModifier);
    EXPECT_EQ(r.tools.color(), QColor(Qt::black));
    EXPECT_EQ(r.canvas.cloneStamp().source(), QPoint(20, 20));
    EXPECT_FALSE(r.canvas.isPainting());

    r.click({30.5, 30.5}, Qt::AltModifier);
    EXPECT_EQ(r.canvas.cloneStamp().source(), QPoint(30, 30));
}

TEST_F(CanvasToolsTest, TheCloneStampPaintsAfterASourceIsSetAndRefusesWithoutOne) {
    SignalCounter refused(&r.canvas, &CanvasView::paintRefused);
    QString why;
    QObject::connect(&r.canvas, &CanvasView::paintRefused, [&why](const QString& text) { why = text; });
    r.paint(QRect(10, 10, 30, 30), QColor(10, 200, 30));
    ASSERT_TRUE(r.doc.addLayer());
    ASSERT_TRUE(r.tools.selectToolOfType(ToolType::Clone));
    BrushSettings s = r.tools.settings();
    s.size = 200;
    s.cloneAllLayers = true;
    r.tools.setSettings(s);

    const int steps = r.steps();
    r.drag({120.5, 80.5}, {126.5, 80.5});
    EXPECT_EQ(refused.count(), 1);
    EXPECT_EQ(why, QStringLiteral("Alt+click to set the clone source."));
    EXPECT_EQ(r.steps(), steps);
    EXPECT_TRUE(r.doc.currentLayer()->pixels.isEmpty());

    r.click({25.5, 25.5}, Qt::AltModifier);
    r.drag({120.5, 80.5}, {126.5, 80.5});
    EXPECT_EQ(refused.count(), 1);
    EXPECT_EQ(r.steps(), steps + 1);
    EXPECT_GT(toUnorm8(r.doc.currentLayer()->pixels.pixel(120, 80))[3], 200);

    EXPECT_EQ(r.canvas.cloneStamp().offset(), QPoint(95, 55));
    r.drag({60.5, 100.5}, {66.5, 100.5});
    EXPECT_EQ(r.canvas.cloneStamp().offset(), QPoint(95, 55));
    r.move({130.5, 90.5});
    EXPECT_EQ(r.canvas.cloneStamp().crosshair(QPointF(130.5, 90.5), true, false), QPointF(35.5, 35.5));
}

TEST_F(CanvasToolsTest, AClickWithoutDraggingDeselects) {
    r.use(FixedTool::Marquee);
    r.marquee({20, 20}, {79, 59});
    ASSERT_TRUE(r.doc.selection());
    r.click({150.5, 100.5});
    EXPECT_FALSE(r.doc.selection());
    r.marquee({20, 20}, {79, 59});
    r.click({150.5, 100.5}, Qt::ShiftModifier);
    EXPECT_TRUE(r.doc.selection());
}

TEST_F(CanvasToolsTest, EscapeCancelsAMarqueeDrag) {
    r.use(FixedTool::Marquee);
    r.press({20.5, 20.5});
    r.move({60.5, 60.5});
    r.key(Qt::Key_Escape);
    r.release({60.5, 60.5});
    EXPECT_FALSE(r.doc.selection());
}

TEST_F(CanvasToolsTest, FreeformLassoClosesOnRelease) {
    r.use(FixedTool::Lasso);
    r.press({20.5, 20.5});
    r.move({100.5, 20.5});
    r.move({100.5, 90.5});
    r.release({100.5, 90.5});
    ASSERT_TRUE(r.doc.selection());
    EXPECT_GT(r.doc.selection()->at(90, 40), 0);
    EXPECT_EQ(r.doc.selection()->at(30, 80), 0);
    EXPECT_EQ(r.doc.undoStack()->undoText(), QStringLiteral("Select"));
}

TEST_F(CanvasToolsTest, PolygonalLassoTakesClicksAndClosesOnDoubleClickOrEnter) {
    FixedToolOptions o = r.tools.fixedOptions();
    o.lasso.mode = LassoMode::Polygonal;
    r.tools.setFixedOptions(o);
    r.use(FixedTool::Lasso);

    r.click({20.5, 20.5});
    r.click({100.5, 20.5});
    r.click({100.5, 90.5});
    EXPECT_FALSE(r.doc.selection());
    r.doubleClick({100.5, 90.5});
    ASSERT_TRUE(r.doc.selection());
    EXPECT_GT(r.doc.selection()->at(90, 40), 0);
    r.release({100.5, 90.5});
    const int selected = r.selected();

    r.doc.deselect().ok();
    r.click({20.5, 20.5});
    r.click({100.5, 20.5});
    r.click({60.5, 120.5});
    r.key(Qt::Key_Return);
    EXPECT_GT(r.selected(), 0);
    EXPECT_NE(r.selected(), selected);

    r.doc.deselect().ok();
    r.click({20.5, 20.5});
    r.click({100.5, 20.5});
    r.click({20.5, 90.5});
    r.key(Qt::Key_Escape);
    EXPECT_FALSE(r.doc.selection());
}

TEST_F(CanvasToolsTest, PolygonalFreeformDrawsFreehandAndClicksAddVertices) {
    FixedToolOptions o = r.tools.fixedOptions();
    o.lasso.mode = LassoMode::PolygonalFreeform;
    r.tools.setFixedOptions(o);
    r.use(FixedTool::Lasso);
    r.press({20.5, 20.5});
    r.move({60.5, 22.5});
    r.move({100.5, 20.5});
    r.release({100.5, 20.5});
    EXPECT_FALSE(r.doc.selection());
    r.click({100.5, 90.5});
    r.key(Qt::Key_Enter);
    ASSERT_TRUE(r.doc.selection());
    EXPECT_GT(r.doc.selection()->at(90, 40), 0);
}

TEST_F(CanvasToolsTest, LassoModifiersCombine) {
    r.use(FixedTool::Marquee);
    r.marquee({0, 0}, {199, 149});
    r.use(FixedTool::Lasso);
    r.press({20.5, 20.5}, Qt::AltModifier);
    r.move({100.5, 20.5}, Qt::AltModifier);
    r.move({100.5, 90.5}, Qt::AltModifier);
    r.release({100.5, 90.5}, Qt::AltModifier);
    ASSERT_TRUE(r.doc.selection());
    EXPECT_EQ(r.doc.selection()->at(90, 40), 0);
    EXPECT_EQ(r.doc.selection()->at(10, 140), kOne);
}

// The wand finds the area as the Bucket does.
TEST_F(CanvasToolsTest, TheWandSelectsTheAreaAroundTheClick) {
    r.paint(QRect(60, 40, 40, 40), Qt::red);
    r.use(FixedTool::MagicWand);
    r.click({80.5, 60.5});
    EXPECT_EQ(r.selected(), 0) << "Transparent Area, clicked on paint: nothing";
    EXPECT_EQ(r.tools.color(), QColor(Qt::black));

    FixedToolOptions o = r.tools.fixedOptions();
    o.wand.mode = selection::WandMode::SimilarColour;
    r.tools.setFixedOptions(o);
    r.click({80.5, 60.5});
    EXPECT_EQ(r.selected(), 40 * 40);
    r.click({10.5, 10.5});
    EXPECT_EQ(r.selected(), kDoc.width() * kDoc.height() - 40 * 40) << "the transparent rest";
    r.click({80.5, 60.5}, Qt::ShiftModifier);
    EXPECT_EQ(r.selected(), kDoc.width() * kDoc.height()) << "Shift adds";
}

TEST_F(CanvasToolsTest, WandOnAFolderIsRefused) {
    ASSERT_TRUE(r.doc.addFolder());
    r.use(FixedTool::MagicWand);
    SignalCounter messages(&r.canvas, &CanvasView::message);
    r.click({10.5, 10.5});
    EXPECT_FALSE(r.doc.selection());
    EXPECT_EQ(messages.count(), 1);
}

TEST_F(CanvasToolsTest, SelectionPenPaintsIntoTheSelectionAndEraserRemoves) {
    r.tools.selectOrCreateToolOfType(ToolType::SelectionPen);
    BrushSettings s = r.tools.settings();
    s.size = 200;
    s.pressureSize = false;
    r.tools.setSettings(s);
    const int before = r.steps();
    r.drag({40.5, 70.5}, {140.5, 70.5});
    waitUntilPainted(r.doc);
    ASSERT_TRUE(r.doc.selection());
    EXPECT_EQ(r.steps(), before + 1);
    EXPECT_EQ(r.doc.undoStack()->undoText(), QStringLiteral("Selection Pen"));
    EXPECT_GE(r.doc.selection()->at(90, 70), kOne - 16);
    EXPECT_EQ(r.doc.selection()->at(90, 100), 0);
    const int painted = r.selected();
    EXPECT_GT(painted, 1500);

    r.tools.selectOrCreateToolOfType(ToolType::SelectionEraser);
    r.drag({90.5, 50.5}, {90.5, 90.5});
    waitUntilPainted(r.doc);
    EXPECT_EQ(r.doc.undoStack()->undoText(), QStringLiteral("Selection Eraser"));
    EXPECT_LT(r.selected(), painted);
    EXPECT_EQ(r.doc.selection()->at(90, 70), 0);
    EXPECT_GE(r.doc.selection()->at(50, 70), kOne - 16);
    EXPECT_TRUE(r.doc.currentLayer()->pixels.isEmpty());
}

void fillMask(SelectionMask& mask, const QRect& rect, int value) {
    const std::vector<std::uint16_t> row(std::size_t(rect.width()), static_cast<std::uint16_t>(value));
    for (int y = rect.top(); y <= rect.bottom(); ++y)
        mask.writeRow(y, rect.left(), rect.width(), row.data());
    mask.squeeze();
}

TEST(SelectionTintTest, UploadsOnlyWhatChanged) {
    const QRect doc(QPoint(0, 0), kDoc);
    SelectionMask all(kDoc);
    fillMask(all, doc, SelectionMask::kOne);
    SelectionTint tint;
    tint.reset(doc);
    EXPECT_EQ(tint.takeDirty(&all), QRegion(doc));
    EXPECT_TRUE(tint.takeDirty(&all).isEmpty());
    tint.invalidate(QRect(10, 10, 8, 8));
    EXPECT_EQ(tint.takeDirty(&all), QRegion(QRect(10, 10, 8, 8)));

    SelectionMask small(kDoc);
    fillMask(small, QRect(50, 50, 10, 10), SelectionMask::kOne);
    const QRect smallBounds = small.bounds();
    tint.invalidate();
    EXPECT_EQ(tint.takeDirty(&small), QRegion(doc));
    tint.invalidate();
    EXPECT_EQ(tint.takeDirty(&small), QRegion(smallBounds));
    tint.invalidate();
    EXPECT_EQ(tint.takeDirty(nullptr), QRegion(smallBounds));
}

TEST(SelectionTintTest, PixelsAreTheTintScaledByCoverage) {
    SelectionMask mask(kDoc);
    fillMask(mask, QRect(0, 0, 1, 1), SelectionMask::kOne);
    fillMask(mask, QRect(1, 0, 1, 1), SelectionMask::kOne / 2);
    const QImage px = SelectionTint::pixels(&mask, QRect(0, 0, 3, 1));
    ASSERT_EQ(px.format(), QImage::Format_RGBA8888_Premultiplied);
    const QColor tint = Theme::color(Theme::Token::SelectionTint);
    const uchar* p = px.constScanLine(0);
    EXPECT_EQ(p[3], tint.alpha());
    EXPECT_EQ(p[2], tint.blue() * tint.alpha() / 255);
    EXPECT_NEAR(p[7], tint.alpha() / 2, 1);
    EXPECT_EQ(p[11], 0);
}

class BareHost : public ToolHost {
public:
    BareHost(Document* doc, ToolBox* tools) : m_doc(doc), m_tools(tools) {}
    Document* toolDocument() override { return m_doc; }
    ToolBox* toolBox() override { return m_tools; }
    double docPerPixel() const override { return 1.0; }
    void toolChanged() override {}
    void toolMessage(const QString&) override {}
    TransformSession* session() override { return nullptr; }
    bool startMoveSession() override { return false; }
    void finishSession(bool) override {}

private:
    Document* m_doc;
    ToolBox* m_tools;
};

TEST(SelectionPenToolTest, ReportsWhereItPainted) {
    Document doc(kDoc, Qt::white);
    ToolBox tools;
    tools.selectOrCreateToolOfType(ToolType::SelectionPen);
    BrushSettings s = tools.settings();
    s.size = 100;
    s.pressureSize = false;
    s.stabilizer = StabilizerLevel{};
    tools.setSettings(s);
    BareHost host(&doc, &tools);
    SelectionPenTool tool(host, &doc, SelectionBrush::Pen);
    ToolPointer p;
    p.sample.pos = QPointF(40.5, 70.5);
    tool.press(p);
    for (double x = 44.5; x <= 80.5; x += 4.0) {
        p.sample.pos = QPointF(x, 70.5);
        tool.move(p);
    }
    tool.waitUntilCaughtUp();  // it paints on a thread of its own
    const std::optional<QRect> painted = tool.takeLiveSelectionChange();
    ASSERT_TRUE(painted);
    EXPECT_TRUE(painted->contains(QPoint(60, 70)));
    EXPECT_LE(painted->height(), Tile::kSize) << "the tiles the stroke replaced, one row of them";
    EXPECT_TRUE(tool.takeLiveSelectionChange()->isEmpty());
    tool.cancel();
    EXPECT_TRUE(tool.takeLiveSelectionChange()->contains(*painted));
    EXPECT_FALSE(doc.selection());
}

TEST_F(CanvasToolsTest, MoveToolMovesTheLayerAsOneUndoStep) {
    r.paint(QRect(60, 40, 40, 40), Qt::red);
    r.use(FixedTool::Move);
    const int before = r.steps();
    r.press({70.5, 50.5});
    r.move({80.5, 55.5});
    r.move({85.5, 60.5});
    EXPECT_EQ(r.steps(), before);
    ASSERT_NE(r.canvas.transformSession(), nullptr);
    EXPECT_EQ(r.canvas.transformSession()->kind(), TransformSession::Kind::Move);
    r.release({85.5, 60.5});
    EXPECT_EQ(r.canvas.transformSession(), nullptr);
    EXPECT_EQ(r.content(), QRect(75, 50, 40, 40));
    EXPECT_EQ(r.steps(), before + 1);
    ASSERT_TRUE(r.doc.undo());
    EXPECT_EQ(r.content(), QRect(60, 40, 40, 40));
}

TEST_F(CanvasToolsTest, MoveToolMovesOnlyTheSelectedPart) {
    r.paint(QRect(60, 40, 40, 40), Qt::red);
    r.use(FixedTool::Marquee);
    r.marquee({60, 40}, {79, 59});
    r.use(FixedTool::Move);
    r.drag({70.5, 50.5}, {120.5, 50.5});
    EXPECT_EQ(r.doc.currentLayer()->pixels.pixel(70, 50)[3], 0);
    EXPECT_EQ(r.doc.currentLayer()->pixels.pixel(120, 50)[3], kOne);
    EXPECT_EQ(r.doc.currentLayer()->pixels.pixel(90, 70)[3], kOne);
    ASSERT_TRUE(r.doc.selection());
    EXPECT_EQ(r.doc.selection()->bounds(true), QRect(110, 40, 20, 20));
}

TEST_F(CanvasToolsTest, EscapeDropsAMoveDrag) {
    r.paint(QRect(60, 40, 40, 40), Qt::red);
    r.use(FixedTool::Move);
    const int before = r.steps();
    r.press({70.5, 50.5});
    r.move({90.5, 70.5});
    r.key(Qt::Key_Escape);
    r.release({90.5, 70.5});
    EXPECT_EQ(r.steps(), before);
    EXPECT_EQ(r.content(), QRect(60, 40, 40, 40));
    EXPECT_EQ(r.canvas.transformSession(), nullptr);
}

TEST_F(CanvasToolsTest, EscapeDropsAMarqueeMoveDrag) {
    r.paint(QRect(60, 40, 40, 40), Qt::red);
    r.use(FixedTool::Marquee);
    const int before = r.steps();
    r.press({70.5, 50.5}, Qt::ControlModifier | Qt::ShiftModifier);
    r.move({90.5, 70.5}, Qt::ControlModifier | Qt::ShiftModifier);
    r.key(Qt::Key_Escape);
    r.release({90.5, 70.5});
    EXPECT_EQ(r.steps(), before);
    EXPECT_EQ(r.content(), QRect(60, 40, 40, 40));
    EXPECT_EQ(r.canvas.transformSession(), nullptr);
}

// Copy, cut, paste and the layer turns commit the open move before they run, which can be while the move is still being dragged.
TEST_F(CanvasToolsTest, AMoveDragOutlivesItsSessionBeingCommitted) {
    r.paint(QRect(60, 40, 40, 40), Qt::red);
    r.use(FixedTool::Move);
    r.press({70.5, 50.5});
    r.move({80.5, 50.5});
    r.canvas.commitTransform();
    r.move({90.5, 50.5});
    r.release({90.5, 50.5});
    EXPECT_EQ(r.content(), QRect(70, 40, 40, 40));
    EXPECT_EQ(r.canvas.transformSession(), nullptr);
}

TEST_F(CanvasToolsTest, AMarqueeMoveDragOutlivesItsSessionBeingCommitted) {
    r.paint(QRect(60, 40, 40, 40), Qt::red);
    r.use(FixedTool::Marquee);
    r.press({70.5, 50.5}, Qt::ControlModifier | Qt::ShiftModifier);
    r.move({80.5, 50.5}, Qt::ControlModifier | Qt::ShiftModifier);
    r.canvas.commitTransform();
    r.move({90.5, 50.5}, Qt::ControlModifier | Qt::ShiftModifier);
    r.release({90.5, 50.5});
    EXPECT_EQ(r.content(), QRect(70, 40, 40, 40));
    EXPECT_EQ(r.canvas.transformSession(), nullptr);
}

TEST_F(CanvasToolsTest, MarqueeCutAndMoveWithoutModifierKey) {
    r.paint(QRect(60, 40, 40, 40), Qt::red);
    r.use(FixedTool::Marquee);
    r.marquee({60, 40}, {99, 79});
    FixedToolOptions o = r.tools.fixedOptions();
    o.marquee.cutAndMove = true;
    r.tools.setFixedOptions(o);
    r.drag({70.5, 50.5}, {90.5, 50.5});
    EXPECT_EQ(r.content(), QRect(80, 40, 40, 40));
    r.marquee({0, 0}, {10, 10});
    EXPECT_EQ(r.selected(), 121);
    EXPECT_EQ(r.content(), QRect(80, 40, 40, 40));
    r.marquee({100, 100}, {110, 110});
    r.drag({105.5, 105.5}, {150.5, 105.5}, Qt::ShiftModifier);
    EXPECT_EQ(r.content(), QRect(80, 40, 40, 40));
}

TEST_F(CanvasToolsTest, CtrlShiftDragMovesWhenCutAndMoveIsOff) {
    r.paint(QRect(60, 40, 40, 40), Qt::red);
    r.use(FixedTool::Marquee);
    r.drag({70.5, 50.5}, {80.5, 60.5}, Qt::ControlModifier | Qt::ShiftModifier);
    EXPECT_EQ(r.content(), QRect(70, 50, 40, 40));
    EXPECT_FALSE(r.doc.selection());
}

TEST_F(CanvasToolsTest, ArrowKeysNudge) {
    r.paint(QRect(60, 40, 40, 40), Qt::red);
    r.use(FixedTool::Marquee);
    r.marquee({60, 40}, {79, 59});
    const int before = r.steps();
    r.key(Qt::Key_Right, Qt::ControlModifier);
    EXPECT_EQ(r.steps(), before + 1);
    EXPECT_EQ(r.doc.currentLayer()->pixels.pixel(60, 50)[3], 0);
    EXPECT_EQ(r.doc.currentLayer()->pixels.pixel(80, 50)[3], kOne);
    EXPECT_EQ(r.doc.selection()->bounds(true), QRect(61, 40, 20, 20));
    r.key(Qt::Key_Down, Qt::ControlModifier | Qt::ShiftModifier);
    EXPECT_EQ(r.doc.selection()->bounds(true), QRect(61, 50, 20, 20));

    const TiledImage layerBefore = r.doc.currentLayer()->pixels;
    r.key(Qt::Key_Left, Qt::AltModifier);
    EXPECT_EQ(r.doc.selection()->bounds(true), QRect(60, 50, 20, 20));
    EXPECT_EQ(r.doc.currentLayer()->pixels.tiles(), layerBefore.tiles());
    EXPECT_EQ(r.doc.undoStack()->undoText(), QStringLiteral("Nudge Selection"));
}

TEST_F(CanvasToolsTest, PlainArrowsNudgeWithTheMoveToolAndScrollOtherwise) {
    r.paint(QRect(60, 40, 40, 40), Qt::red);
    const QPointF pan = r.doc.view().pan;
    r.key(Qt::Key_Right);
    EXPECT_NE(r.doc.view().pan, pan);
    EXPECT_EQ(r.content(), QRect(60, 40, 40, 40));
    r.use(FixedTool::Move);
    r.key(Qt::Key_Right);
    EXPECT_EQ(r.content(), QRect(61, 40, 40, 40));
    r.key(Qt::Key_Up, Qt::ShiftModifier);
    EXPECT_EQ(r.content(), QRect(61, 30, 40, 40));
}

TEST_F(CanvasToolsTest, TransformWithHandlesAppliesAsOneUndoStepOnEnter) {
    r.paint(QRect(100, 60, 40, 40), Qt::red);
    SignalCounter changes(&r.canvas, &CanvasView::transformChanged);
    ASSERT_TRUE(r.canvas.beginTransform(TransformMode::Resize));
    EXPECT_TRUE(r.canvas.hasTransform());
    EXPECT_EQ(changes.count(), 1);
    const int before = r.steps();
    r.drag({140, 100}, {180, 140});
    EXPECT_EQ(r.steps(), before);
    EXPECT_NEAR(r.canvas.transformSession()->quad().corner[2].x(), 180, 1e-6);
    r.key(Qt::Key_Return);
    EXPECT_FALSE(r.canvas.hasTransform());
    EXPECT_EQ(changes.count(), 2);
    EXPECT_EQ(r.steps(), before + 1);
    EXPECT_EQ(r.content(), QRect(100, 60, 80, 80));
    ASSERT_TRUE(r.doc.undo());
    EXPECT_EQ(r.content(), QRect(100, 60, 40, 40));
}

TEST_F(CanvasToolsTest, EscapeAndCancelRestoreTheLayer) {
    r.paint(QRect(100, 60, 40, 40), Qt::red);
    const int before = r.steps();
    ASSERT_TRUE(r.canvas.beginTransform(TransformMode::Resize));
    r.drag({140, 100}, {180, 140});
    r.key(Qt::Key_Escape);
    EXPECT_FALSE(r.canvas.hasTransform());
    EXPECT_EQ(r.steps(), before);
    EXPECT_EQ(r.content(), QRect(100, 60, 40, 40));

    ASSERT_TRUE(r.canvas.beginTransform(TransformMode::Rotate));
    r.drag({300, 100}, {300, 200});
    r.canvas.cancelTransform();
    EXPECT_FALSE(r.canvas.hasTransform());
    EXPECT_EQ(r.steps(), before);
}

TEST_F(CanvasToolsTest, TransformModesAndPerspectiveFollowTheToolOptions) {
    r.paint(QRect(100, 60, 40, 40), Qt::red);
    ASSERT_TRUE(r.canvas.beginTransform());
    EXPECT_EQ(r.canvas.transformSession()->mode(), TransformMode::Resize);
    ASSERT_TRUE(r.canvas.beginTransform(TransformMode::Deform));
    EXPECT_EQ(r.canvas.transformSession()->mode(), TransformMode::Deform);
    FixedToolOptions o = r.tools.fixedOptions();
    o.marquee.perspective = 60;
    o.marquee.final = transform::Interpolation::Lanczos2;
    r.tools.setFixedOptions(o);
    EXPECT_NEAR(r.canvas.transformSession()->perspective(), 0.6, 1e-9);
    EXPECT_EQ(r.canvas.transformSession()->options().final, transform::Interpolation::Lanczos2);
}

TEST_F(CanvasToolsTest, TransformIsRefusedForEmptyOrLockedLayersAndFolders) {
    SignalCounter messages(&r.canvas, &CanvasView::message);
    EXPECT_FALSE(r.canvas.beginTransform());
    EXPECT_EQ(messages.count(), 1);
    r.paint(QRect(100, 60, 40, 40), Qt::red);
    LayerProps props = r.doc.currentLayer()->props;
    props.locked = true;
    ASSERT_TRUE(r.doc.setLayerProps(r.doc.currentLayerId(), props));
    EXPECT_FALSE(r.canvas.beginTransform());
    EXPECT_EQ(messages.count(), 2);
}

TEST_F(CanvasToolsTest, ADocumentChangeDropsTheFloatingTransform) {
    r.paint(QRect(100, 60, 40, 40), Qt::red);
    ASSERT_TRUE(r.canvas.beginTransform(TransformMode::Resize));
    ASSERT_TRUE(r.doc.addLayer());
    EXPECT_FALSE(r.canvas.hasTransform());
}

TEST_F(CanvasToolsTest, TransformMovesTheSelectionToo) {
    r.paint(QRect(100, 60, 40, 40), Qt::red);
    r.use(FixedTool::Marquee);
    r.marquee({100, 60}, {119, 79});
    ASSERT_TRUE(r.canvas.beginTransform(TransformMode::Resize));
    r.drag({110, 70}, {160, 70});
    r.canvas.commitTransform();
    EXPECT_EQ(r.doc.selection()->bounds(true), QRect(150, 60, 20, 20));
    EXPECT_EQ(r.doc.currentLayer()->pixels.pixel(160, 70)[3], kOne);
}

TEST_F(CanvasToolsTest, DraggingAHandleMovesTheGpuPreviewWithoutResamplingOnTheCpu) {
    r.paint(QRect(100, 60, 40, 40), Qt::red);
    ASSERT_TRUE(r.canvas.beginTransform(TransformMode::Freeform));
    TransformSession* s = r.canvas.transformSession();
    ASSERT_NE(s, nullptr);
    EXPECT_TRUE(s->gpuPreview());
    const std::uint64_t cpu = s->cpuPreviewCount();

    r.press({140, 100});
    for (int i = 1; i <= 8; ++i)
        r.move(QPointF(140, 100) + QPointF(5, 4) * i);
    EXPECT_EQ(s->cpuPreviewCount(), cpu);
    EXPECT_EQ(s->gpuPreviewSteps(), 8u);
    EXPECT_TRUE(s->gpuPreviewActive());
    EXPECT_NEAR(s->floatingQuad().corner[2].x(), 180, 1e-6);
    EXPECT_NEAR(s->floatingQuad().corner[2].y(), 132, 1e-6);

    r.release({180, 132});
    EXPECT_EQ(s->cpuPreviewCount(), cpu + 1);
    EXPECT_FALSE(s->gpuPreviewActive());
    const int before = r.steps();
    r.key(Qt::Key_Return);
    EXPECT_EQ(r.steps(), before + 1);
    EXPECT_EQ(r.content(), QRect(100, 60, 80, 72));
}

TEST_F(CanvasToolsTest, ADragThatPausesGetsItsCpuPreviewWhileTheButtonIsStillDown) {
    r.paint(QRect(100, 60, 40, 40), Qt::red);
    ASSERT_TRUE(r.canvas.beginTransform(TransformMode::Freeform));
    TransformSession* s = r.canvas.transformSession();
    const std::uint64_t cpu = s->cpuPreviewCount();
    r.press({140, 100});
    r.move({160, 120});
    ASSERT_TRUE(s->gpuPreviewActive());
    QElapsedTimer waited;
    waited.start();
    while (s->gpuPreviewActive() && waited.elapsed() < 3000)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    EXPECT_FALSE(s->gpuPreviewActive());
    EXPECT_EQ(s->cpuPreviewCount(), cpu + 1);
    EXPECT_GE(waited.elapsed(), CanvasView::kSettleMs - 20);
    r.release({160, 120});
}

TEST_F(CanvasToolsTest, NavigationToolsMakeTheLeftButtonPanZoomRotateAndPick) {
    r.paint(QRect(0, 0, 200, 150), QColor(30, 60, 90));
    r.use(FixedTool::Hand);
    const QPointF pan = r.doc.view().pan;
    r.drag({100, 75}, {130, 95});
    EXPECT_EQ(r.doc.view().pan, pan + QPointF(30, 20));

    r.use(FixedTool::Zoom);
    const double zoom = r.doc.view().zoom;
    r.drag({100, 75}, {200, 75});
    EXPECT_GT(r.doc.view().zoom, zoom);

    r.use(FixedTool::Rotate);
    r.drag({150, 75}, {100, 125});
    EXPECT_NE(r.doc.view().rotationDeg, 0.0);

    r.canvas.resetAngle();
    r.canvas.resetView();
    r.use(FixedTool::Eyedropper);
    r.click({50.5, 50.5});
    EXPECT_EQ(r.tools.color(), QColor(30, 60, 90));
}

TEST(SelectionOverlayTest, DrawsAntsOnTheOutlineWithAnEightPixelPeriod) {
    const SelectionMask rect = selection::marquee(QSize(100, 60), QPoint(10, 10), QPoint(49, 29));
    SelectionOverlay overlay;
    QImage img(100, 60, QImage::Format_ARGB32);
    img.fill(Qt::gray);
    {
        QPainter p(&img);
        overlay.paint(p, QTransform(), 0, &rect);
    }
    EXPECT_TRUE(overlay.hasOutline());
    // Along the top edge (y = 10): 4 px of black dashes, 4 px of the white line below them, repeating.
    int black = 0, white = 0;
    for (int x = 12; x < 40; ++x) {
        const QRgb c = img.pixel(x, 10) != QColor(Qt::gray).rgb() ? img.pixel(x, 10) : img.pixel(x, 9);
        black += qGray(c) < 60;
        white += qGray(c) > 200;
    }
    EXPECT_GT(black, 8);
    EXPECT_GT(white, 8);
    EXPECT_NEAR(double(black) / white, 1.0, 0.35);
    EXPECT_EQ(img.pixel(30, 20), QColor(Qt::gray).rgb());
    EXPECT_EQ(img.pixel(80, 50), QColor(Qt::gray).rgb());
}

TEST(SelectionOverlayTest, FollowsTheViewTransformAndMarches) {
    const SelectionMask rect = selection::marquee(QSize(100, 60), QPoint(10, 10), QPoint(19, 19));
    SelectionOverlay overlay;
    const auto render = [&](const QTransform& t, int phase) {
        QImage img(200, 120, QImage::Format_ARGB32);
        img.fill(Qt::gray);
        QPainter p(&img);
        overlay.paint(p, t, phase, &rect);
        return img;
    };
    const QImage plain = render(QTransform(), 0);
    const QImage zoomed = render(QTransform::fromScale(2, 2), 0);
    const QImage later = render(QTransform(), 3);
    EXPECT_NE(plain, zoomed);
    EXPECT_NE(plain, later);
    const auto columnHasInk = [](const QImage& img, int x) {
        for (int y = 0; y < img.height(); ++y)
            if (img.pixel(x, y) != QColor(Qt::gray).rgb())
                return true;
        return false;
    };
    EXPECT_TRUE(columnHasInk(plain, 10) || columnHasInk(plain, 9));
    EXPECT_FALSE(columnHasInk(plain, 30));
    EXPECT_TRUE(columnHasInk(zoomed, 30));
    EXPECT_FALSE(columnHasInk(zoomed, 15));
}

TEST(SelectionOverlayTest, RebuildsOnlyWhenInvalidatedAndHidesWithoutAMask) {
    const SelectionMask a = selection::marquee(QSize(100, 60), QPoint(10, 10), QPoint(19, 19));
    const SelectionMask b = selection::marquee(QSize(100, 60), QPoint(40, 30), QPoint(59, 49));
    SelectionOverlay overlay;
    QImage img(100, 60, QImage::Format_ARGB32);
    QPainter p(&img);
    overlay.paint(p, QTransform(), 0, &a);
    EXPECT_TRUE(overlay.hasOutline());
    overlay.paint(p, QTransform(), 0, &b);  // not invalidated: still draws the old outline (the owner must invalidate)
    overlay.invalidate();
    overlay.paint(p, QTransform(), 0, nullptr);
    EXPECT_FALSE(overlay.hasOutline());
}

TEST_F(CanvasToolsTest, SelectionVisibilityIsPerCanvas) {
    EXPECT_TRUE(r.canvas.selectionVisible());
    r.canvas.setSelectionVisible(false);
    EXPECT_FALSE(r.canvas.selectionVisible());
    r.use(FixedTool::Marquee);
    r.marquee({20, 20}, {79, 59});
    EXPECT_TRUE(r.doc.selection());
    r.canvas.setSelectionVisible(true);
    EXPECT_TRUE(r.canvas.selectionVisible());
}

TEST_F(CanvasToolsTest, SpaceShowsTheHandUnlessCtrlOrAltIsHeldPerTheKeyEvents) {
    r.use(FixedTool::Lasso);
    const auto key = [&](QEvent::Type type, int k, Qt::KeyboardModifiers mods) {
        QKeyEvent e(type, k, mods);
        QCoreApplication::sendEvent(&r.canvas, &e);
    };
    key(QEvent::KeyPress, Qt::Key_Space, Qt::NoModifier);
    EXPECT_EQ(r.canvas.cursor().shape(), Qt::OpenHandCursor);
    key(QEvent::KeyRelease, Qt::Key_Space, Qt::NoModifier);
    // Ctrl pressed and released (its own key event need not carry Ctrl): Space afterwards is the hand again.
    key(QEvent::KeyPress, Qt::Key_Control, Qt::NoModifier);
    key(QEvent::KeyPress, Qt::Key_Space, Qt::ControlModifier);
    EXPECT_EQ(r.canvas.cursor().shape(), Qt::SizeHorCursor);
    key(QEvent::KeyRelease, Qt::Key_Space, Qt::ControlModifier);
    key(QEvent::KeyRelease, Qt::Key_Control, Qt::ControlModifier);
    key(QEvent::KeyPress, Qt::Key_Space, Qt::NoModifier);
    EXPECT_EQ(r.canvas.cursor().shape(), Qt::OpenHandCursor);
    key(QEvent::KeyRelease, Qt::Key_Space, Qt::NoModifier);
}

namespace {

void penHover(CanvasView& canvas, const QPointF& at) {
    QTabletEvent pen(QEvent::TabletMove, QPointingDevice::primaryPointingDevice(), at, at, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, Qt::NoModifier,
                     Qt::NoButton, Qt::NoButton);
    QCoreApplication::sendEvent(&canvas, &pen);
}

}

// Moving the pen from a panel onto the canvas without touching the tablet hides the system cursor the moment it crosses the
// canvas's edge, and brings it back the moment it leaves; it had waited for a tap.
TEST_F(CanvasToolsTest, HoveringOntoTheCanvasHidesTheCursorWithoutATap) {
    r.stats.setProximity(true, InputDeviceInfo{});
    penHover(r.canvas, {-20.0, -20.0});
    EXPECT_EQ(QGuiApplication::overrideCursor(), nullptr);
    penHover(r.canvas, Rig::w({50.5, 50.5}));
    ASSERT_NE(QGuiApplication::overrideCursor(), nullptr);
    EXPECT_EQ(QGuiApplication::overrideCursor()->shape(), Qt::BlankCursor);
    EXPECT_EQ(r.canvas.cursor().shape(), Qt::BlankCursor);
    penHover(r.canvas, {-20.0, -20.0});
    EXPECT_EQ(QGuiApplication::overrideCursor(), nullptr);
    r.stats.setProximity(false, InputDeviceInfo{});
}

// Feedback: "Im still seeing the cursor when using the mouse". On a desktop where the pen and the mouse share one pointer, the
// pen's own cursor showed as an arrow on top of the mouse's brush circle and hand: it was set back to an arrow whenever the mouse
// moved. It now stays hidden while the pen is over the canvas, whichever was used last.
TEST_F(CanvasToolsTest, TheMouseOverTheCanvasLeavesThePensCursorHidden) {
    r.stats.setProximity(true, InputDeviceInfo{});
    const QPointF at = Rig::w({50.5, 50.5});
    penHover(r.canvas, at);
    EXPECT_TRUE(r.canvas.penCursorHidden());
    QMouseEvent mouse(QEvent::MouseMove, at, at, Qt::NoButton, Qt::NoButton, Qt::NoModifier);
    QCoreApplication::sendEvent(&r.canvas, &mouse);
    EXPECT_EQ(QGuiApplication::overrideCursor(), nullptr) << "the mouse's own cursor shows";
    EXPECT_EQ(r.canvas.drawnCursor(), CanvasGlyph::None);
    EXPECT_TRUE(r.canvas.penCursorHidden()) << "no arrow on top of it";
    penHover(r.canvas, {-20.0, -20.0});
    EXPECT_FALSE(r.canvas.penCursorHidden()) << "the pen off the canvas shows its arrow";
    r.stats.setProximity(false, InputDeviceInfo{});
}

// A pen already in range when the program started sends no proximity event: its first event says it's there.
TEST_F(CanvasToolsTest, APenAlreadyInRangeIsKnownByItsFirstEvent) {
    ASSERT_FALSE(r.stats.inProximity());
    r.use(FixedTool::Lasso);
    penHover(r.canvas, Rig::w({50.5, 50.5}));
    EXPECT_TRUE(r.stats.inProximity());
    EXPECT_EQ(r.canvas.drawnCursor(), CanvasGlyph::Cross);
    r.stats.setProximity(false, InputDeviceInfo{});
    EXPECT_EQ(QGuiApplication::overrideCursor(), nullptr);
}

// A canvas opened while the pen is hovering knows the pen is there.
TEST_F(CanvasToolsTest, ACanvasOpenedWithThePenInRangeKnowsIt) {
    r.use(FixedTool::Lasso);
    r.stats.setProximity(true, InputDeviceInfo{});
    Document doc(kDoc, Qt::white);
    {
        CanvasView canvas(&doc, &r.stats);
        canvas.setToolBox(&r.tools);
        canvas.resize(400, 300);
        penHover(canvas, QPointF(100.5, 100.5));
        EXPECT_EQ(canvas.drawnCursor(), CanvasGlyph::Cross);
    }
    r.stats.setProximity(false, InputDeviceInfo{});
    EXPECT_EQ(QGuiApplication::overrideCursor(), nullptr);
}

TEST_F(CanvasToolsTest, WithThePenTheCanvasDrawsTheCursorAndBlanksTheSystemOne) {
    // Qt on Wayland can set the pen's cursor only while the mouse pointer is over the window (CanvasCursor.h): the
    // canvas draws the cursor and a blank override hides the pen's system cursor where Qt can reach it.
    r.use(FixedTool::Lasso);
    r.stats.setProximity(true, InputDeviceInfo{});
    const QPointF at = Rig::w({50.5, 50.5});
    QTabletEvent pen(QEvent::TabletMove, QPointingDevice::primaryPointingDevice(), at, at, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, Qt::NoModifier,
                     Qt::NoButton, Qt::NoButton);
    QCoreApplication::sendEvent(&r.canvas, &pen);
    ASSERT_NE(QGuiApplication::overrideCursor(), nullptr);
    EXPECT_EQ(QGuiApplication::overrideCursor()->shape(), Qt::BlankCursor);
    EXPECT_EQ(r.canvas.drawnCursor(), CanvasGlyph::Cross);
    QKeyEvent space(QEvent::KeyPress, Qt::Key_Space, Qt::NoModifier);
    QCoreApplication::sendEvent(&r.canvas, &space);
    EXPECT_EQ(QGuiApplication::overrideCursor()->shape(), Qt::BlankCursor);
    EXPECT_EQ(r.canvas.drawnCursor(), CanvasGlyph::OpenHand);
    EXPECT_EQ(r.canvas.cursor().shape(), Qt::BlankCursor) << "one hand, the drawn one";
    QMouseEvent mouse(QEvent::MouseMove, at, at, Qt::NoButton, Qt::NoButton, Qt::NoModifier);
    QCoreApplication::sendEvent(&r.canvas, &mouse);
    EXPECT_EQ(QGuiApplication::overrideCursor(), nullptr);
    EXPECT_EQ(r.canvas.cursor().shape(), Qt::OpenHandCursor);
    EXPECT_EQ(r.canvas.drawnCursor(), CanvasGlyph::None);
    QKeyEvent release(QEvent::KeyRelease, Qt::Key_Space, Qt::NoModifier);
    QCoreApplication::sendEvent(&r.canvas, &release);
    r.stats.setProximity(false, InputDeviceInfo{});
}
