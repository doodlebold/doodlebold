#include "core/document/Document.h"
#include "core/document/TransformSession.h"
#include "core/tools/ToolBox.h"
#include "core/tools/ToolBoxStore.h"
#include "ui/CanvasView.h"
#include "ui/DocumentTabs.h"
#include "ui/MainWindow.h"
#include "ui/SelectionGrowDialog.h"
#include "ui/panels/FixedToolPanels.h"
#include "ui/panels/LayerPanel.h"
#include "ui/panels/ToolPanel.h"
#include "ScreenshotSupport.h"

#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QFile>
#include <QGuiApplication>
#include <QMenuBar>
#include <QMouseEvent>
#include <QPushButton>
#include <QStandardPaths>
#include <QStatusBar>
#include <QThread>
#include <QTreeView>

#include <gtest/gtest.h>

#include <memory>

using namespace doodlebold;
using H = TransformSession::Handle;

namespace {

constexpr int kOne = kChannelOne;

class SelectionMenuTest : public testing::Test {
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

    QAction* action(const char* id) const {
        QAction* a = window->command(QString::fromLatin1(id));
        EXPECT_NE(a, nullptr) << id;
        return a;
    }
    void trigger(const char* id) const {
        if (QAction* a = action(id))
            a->trigger();
    }

    Document* doc() const { return window->tabs()->currentDocument(); }
    CanvasView* canvas() const { return window->tabs()->currentCanvas(); }
    int steps() const { return doc()->undoStack()->count(); }
    QRect content() const { return doc()->currentLayer()->pixels.contentBounds(true); }
    void paint(const QRect& r, const QColor& c) {
        doc()->paintTarget(doc()->currentLayerId()).value()->fill(r, c);
        doc()->notifyPixelsChanged(doc()->currentLayerId(), r);
    }
    void select(const QRect& r) { ASSERT_TRUE(doc()->setSelection(selection::marquee(doc()->size(), r.topLeft(), r.bottomRight()))); }
    template <typename T> T* find(const char* name) const {
        T* w = window->findChild<T*>(QString::fromLatin1(name));
        EXPECT_NE(w, nullptr) << name;
        return w;
    }

    std::unique_ptr<MainWindow> window;
};

}

TEST_F(SelectionMenuTest, ToolKeysSwitchBetweenFixedAndTrayTools) {
    ToolBox* tools = window->tools();
    trigger("tool.marquee");
    EXPECT_EQ(tools->fixedTool(), FixedTool::Marquee);
    trigger("tool.lasso");
    EXPECT_EQ(tools->fixedTool(), FixedTool::Lasso);
    trigger("tool.wand");
    EXPECT_EQ(tools->fixedTool(), FixedTool::MagicWand);
    tools->setFixedTool(FixedTool::Hand);
    trigger("tool.pencil");
    EXPECT_FALSE(tools->fixedTool());
    EXPECT_EQ(tools->settings().tool, ToolType::Pencil);

    const int count = tools->toolCount();
    trigger("tool.selectionPen");
    EXPECT_EQ(tools->settings().tool, ToolType::SelectionPen);
    EXPECT_EQ(tools->toolCount(), count + 1);
    trigger("tool.selectionEraser");
    EXPECT_EQ(tools->settings().tool, ToolType::SelectionEraser);
    trigger("tool.marquee");
    trigger("tool.selectionPen");
    EXPECT_EQ(tools->toolCount(), count + 2);
    EXPECT_FALSE(tools->fixedTool());
}

TEST_F(SelectionMenuTest, SelectAllDeselectAndInvert) {
    ASSERT_NE(doc(), nullptr);
    const int base = steps();
    trigger("selection.all");
    ASSERT_TRUE(doc()->selection());
    EXPECT_EQ(doc()->selection()->at(5, 5), kOne);
    EXPECT_EQ(steps(), base + 1);
    trigger("selection.invert");
    EXPECT_FALSE(doc()->selection());
    EXPECT_FALSE(action("selection.invert")->isEnabled());
    EXPECT_FALSE(action("selection.deselect")->isEnabled());

    select(QRect(100, 100, 50, 50));
    trigger("selection.invert");
    EXPECT_EQ(doc()->selection()->at(120, 120), 0);
    EXPECT_EQ(doc()->selection()->at(10, 10), kOne);
    trigger("selection.deselect");
    EXPECT_FALSE(doc()->selection());
    const int after = steps();
    action("selection.deselect")->trigger();
    EXPECT_EQ(steps(), after);
}

TEST_F(SelectionMenuTest, DilateAndErodeByOnePixelAndByWidth) {
    select(QRect(100, 100, 50, 50));
    trigger("selection.dilate1");
    EXPECT_EQ(doc()->selection()->bounds(true), QRect(99, 99, 52, 52));
    trigger("selection.erode1");
    EXPECT_EQ(doc()->selection()->bounds(true), QRect(100, 100, 50, 50));

    trigger("selection.dilate");
    auto* dlg = window->findChild<SelectionGrowDialog*>();
    ASSERT_NE(dlg, nullptr);
    EXPECT_TRUE(dlg->isDilate());
    EXPECT_EQ(dlg->windowTitle(), QStringLiteral("Grow Selection"));
    dlg->setGrowWidth(5);
    dlg->accept();
    QCoreApplication::processEvents();
    EXPECT_EQ(doc()->selection()->bounds(true), QRect(95, 95, 60, 60));

    trigger("selection.erode");
    dlg = window->findChild<SelectionGrowDialog*>();
    ASSERT_NE(dlg, nullptr);
    EXPECT_FALSE(dlg->isDilate());
    dlg->setGrowWidth(10);
    dlg->reject();
    QCoreApplication::processEvents();
    EXPECT_EQ(doc()->selection()->bounds(true), QRect(95, 95, 60, 60));
    EXPECT_EQ(SelectionGrowDialog::kMaxWidth, 500);
}

TEST_F(SelectionMenuTest, GrowDialogsNeedASelection) {
    EXPECT_FALSE(action("selection.dilate")->isEnabled());
    EXPECT_FALSE(action("selection.erode")->isEnabled());
    trigger("selection.dilate");
    EXPECT_EQ(window->findChild<SelectionGrowDialog*>(), nullptr);
}

TEST_F(SelectionMenuTest, ShowHideSelectionAppliesToEveryCanvas) {
    QAction* show = action("selection.show");
    ASSERT_TRUE(show->isCheckable());
    EXPECT_TRUE(show->isChecked());
    EXPECT_TRUE(canvas()->selectionVisible());
    show->trigger();
    EXPECT_FALSE(canvas()->selectionVisible());
    window->newDocument();
    EXPECT_FALSE(canvas()->selectionVisible());
    show->trigger();
    EXPECT_TRUE(canvas()->selectionVisible());
    window->tabs()->setCurrentIndex(0);
    EXPECT_TRUE(canvas()->selectionVisible());
}

TEST_F(SelectionMenuTest, CopyAndPasteMakeANewLayerAtTheSamePlace) {
    paint(QRect(100, 60, 40, 40), Qt::red);
    select(QRect(100, 60, 20, 20));
    trigger("edit.copy");
    const QImage clip = QGuiApplication::clipboard()->image();
    EXPECT_EQ(clip.size(), QSize(20, 20));
    EXPECT_EQ(content(), QRect(100, 60, 40, 40));

    const int layers = doc()->layers().layerCount();
    const int base = steps();
    trigger("edit.paste");
    EXPECT_EQ(doc()->layers().layerCount(), layers + 1);
    EXPECT_EQ(steps(), base + 1);
    EXPECT_EQ(content(), QRect(100, 60, 20, 20));
    EXPECT_FALSE(doc()->selection());
    ASSERT_TRUE(doc()->undo());
    EXPECT_EQ(doc()->layers().layerCount(), layers);
}

TEST_F(SelectionMenuTest, CopyGivesOtherProgramsThePictureOverWhite) {
    paint(QRect(100, 60, 10, 10), Qt::red);
    paint(QRect(130, 60, 10, 10), Qt::red);
    select(QRect(100, 60, 40, 10));
    trigger("edit.copy");
    const QImage clip = QGuiApplication::clipboard()->image();
    ASSERT_EQ(clip.size(), QSize(40, 10));
    EXPECT_FALSE(clip.hasAlphaChannel());
    EXPECT_EQ(clip.pixelColor(5, 5), QColor(Qt::red));
    EXPECT_EQ(clip.pixelColor(20, 5), QColor(Qt::white));

    trigger("edit.paste");
    EXPECT_EQ(content(), QRect(100, 60, 40, 10));
    EXPECT_EQ(doc()->currentLayer()->pixels.pixel(120, 65)[3], 0);
    EXPECT_EQ(doc()->currentLayer()->pixels.pixel(105, 65)[3], kChannelOne);
}

TEST_F(SelectionMenuTest, CutRemovesTheSelectedPart) {
    paint(QRect(100, 60, 40, 40), Qt::red);
    select(QRect(100, 60, 20, 40));
    const int base = steps();
    trigger("edit.cut");
    EXPECT_EQ(steps(), base + 1);
    EXPECT_EQ(content(), QRect(120, 60, 20, 40));
    EXPECT_EQ(QGuiApplication::clipboard()->image().size(), QSize(20, 40));
    trigger("edit.paste");
    EXPECT_EQ(content(), QRect(100, 60, 20, 40));
}

TEST_F(SelectionMenuTest, CutCopyAndPasteAreDisabledUntilTheyApply) {
    QGuiApplication::clipboard()->clear();
    EXPECT_FALSE(action("edit.cut")->isEnabled());
    EXPECT_FALSE(action("edit.copy")->isEnabled());
    EXPECT_FALSE(action("edit.paste")->isEnabled());
    EXPECT_FALSE(action("edit.pasteKeep")->isEnabled());
    paint(QRect(100, 60, 40, 40), Qt::red);
    select(QRect(100, 60, 20, 20));
    EXPECT_TRUE(action("edit.cut")->isEnabled());
    EXPECT_TRUE(action("edit.copy")->isEnabled());
    trigger("edit.copy");
    EXPECT_TRUE(action("edit.paste")->isEnabled());
    EXPECT_TRUE(action("edit.pasteKeep")->isEnabled());
    ASSERT_TRUE(doc()->deselect());
    EXPECT_FALSE(action("edit.copy")->isEnabled());
}

TEST_F(SelectionMenuTest, PasteWithoutDeselectingKeepsTheSelection) {
    paint(QRect(100, 60, 40, 40), Qt::red);
    select(QRect(100, 60, 20, 20));
    trigger("edit.copy");
    trigger("edit.pasteKeep");
    EXPECT_TRUE(doc()->selection());
    EXPECT_EQ(content(), QRect(100, 60, 20, 20));
    trigger("edit.paste");
    EXPECT_FALSE(doc()->selection());
}

TEST_F(SelectionMenuTest, PasteTakesAPictureFromAnotherProgramCentredOnTheCanvas) {
    QImage picture(30, 20, QImage::Format_RGBA8888);
    picture.fill(QColor(0, 0, 255));
    QGuiApplication::clipboard()->setImage(picture);
    EXPECT_TRUE(action("edit.paste")->isEnabled());
    const int layers = doc()->layers().layerCount();
    trigger("edit.paste");
    EXPECT_EQ(doc()->layers().layerCount(), layers + 1);
    EXPECT_EQ(content(), QRect((2000 - 30) / 2, (1500 - 20) / 2, 30, 20));
}

TEST_F(SelectionMenuTest, TransformLayerStartsInTheMarqueePanelAndOkAppliesIt) {
    paint(QRect(100, 60, 40, 40), Qt::red);
    auto* ok = find<QPushButton>("transformOk");
    auto* cancel = find<QPushButton>("transformCancel");
    EXPECT_FALSE(ok->isEnabled());
    trigger("layer.transform");
    EXPECT_EQ(window->tools()->fixedTool(), FixedTool::Marquee);
    ASSERT_TRUE(canvas()->hasTransform());
    EXPECT_TRUE(ok->isEnabled());
    EXPECT_TRUE(cancel->isEnabled());

    TransformSession* s = canvas()->transformSession();
    s->beginDrag(H::Corner2, {140, 100});
    s->dragTo({180, 140}, {});
    s->endDrag();
    const int base = steps();
    ok->click();
    EXPECT_FALSE(canvas()->hasTransform());
    EXPECT_FALSE(ok->isEnabled());
    EXPECT_EQ(steps(), base + 1);
    EXPECT_EQ(doc()->undoStack()->undoText(), QStringLiteral("Transform Layer"));
    EXPECT_EQ(content(), QRect(100, 60, 80, 80));
}

TEST_F(SelectionMenuTest, CancelAndModeButtons) {
    paint(QRect(100, 60, 40, 40), Qt::red);
    find<QAbstractButton>("mode_rotate")->click();
    ASSERT_TRUE(canvas()->hasTransform());
    EXPECT_EQ(canvas()->transformSession()->mode(), TransformMode::Rotate);
    find<QAbstractButton>("mode_freeform")->click();
    EXPECT_EQ(canvas()->transformSession()->mode(), TransformMode::Freeform);
    EXPECT_EQ(window->tools()->fixedOptions().marquee.transformMode, TransformMode::Freeform);

    canvas()->transformSession()->beginDrag(H::Corner1, {140, 60});
    canvas()->transformSession()->dragTo({170, 50}, {});
    canvas()->transformSession()->endDrag();
    const int base = steps();
    find<QPushButton>("transformCancel")->click();
    EXPECT_FALSE(canvas()->hasTransform());
    EXPECT_EQ(steps(), base);
    EXPECT_EQ(content(), QRect(100, 60, 40, 40));
}

TEST_F(SelectionMenuTest, ReverseAndRotateButtonsActOnTheSelectedPartExactly) {
    paint(QRect(100, 60, 20, 40), Qt::red);
    select(QRect(100, 60, 40, 40));
    window->tools()->setFixedTool(FixedTool::Marquee);
    const int base = steps();
    find<QPushButton>("reverseHorizontal")->click();
    EXPECT_EQ(steps(), base + 1);
    EXPECT_EQ(content(), QRect(120, 60, 20, 40));
    find<QPushButton>("reverseVertical")->click();
    EXPECT_EQ(content(), QRect(120, 60, 20, 40));
    find<QPushButton>("rotate90cw")->click();
    EXPECT_EQ(content().size(), QSize(40, 20));
    find<QPushButton>("rotate90ccw")->click();
    EXPECT_EQ(content().size(), QSize(20, 40));
    EXPECT_EQ(steps(), base + 4);
}

TEST_F(SelectionMenuTest, TurnButtonsApplyARunningTransformFirst) {
    paint(QRect(100, 60, 40, 40), Qt::red);
    ASSERT_TRUE(canvas()->beginTransform(TransformMode::Resize));
    canvas()->transformSession()->beginDrag(H::Corner2, {140, 100});
    canvas()->transformSession()->dragTo({180, 140}, {});
    canvas()->transformSession()->endDrag();
    const int base = steps();
    find<QPushButton>("reverseHorizontal")->click();
    EXPECT_FALSE(canvas()->hasTransform());
    EXPECT_EQ(steps(), base + 2);
    EXPECT_EQ(content(), QRect(100, 60, 80, 80));
}

TEST(LayerThumbnailSelection, ModifiersCombineTheLayerWithTheSelection) {
    Document doc(QSize(300, 200), Qt::white);
    doc.paintTarget(doc.currentLayerId()).value()->fill(QRect(100, 60, 40, 40), Qt::red);
    const LayerId a = doc.currentLayerId();
    ASSERT_TRUE(doc.addLayer());
    doc.paintTarget(doc.currentLayerId()).value()->fill(QRect(120, 80, 40, 40), Qt::blue);
    const LayerId b = doc.currentLayerId();
    LayerPanel panel;
    panel.setDocument(&doc);
    const auto count = [&doc] {
        int n = 0;
        for (int y = 0; y < 200; ++y)
            for (int x = 0; x < 300; ++x)
                n += doc.selection() && doc.selection()->at(x, y) > 0;
        return n;
    };
    panel.selectionFromLayer(a, Qt::ControlModifier);
    EXPECT_EQ(count(), 1600);
    panel.selectionFromLayer(b, Qt::ControlModifier | Qt::ShiftModifier);
    EXPECT_EQ(count(), 2800);
    panel.selectionFromLayer(b, Qt::ControlModifier | Qt::AltModifier);
    EXPECT_EQ(count(), 1200);
    panel.selectionFromLayer(a, Qt::ControlModifier | Qt::AltModifier | Qt::ShiftModifier);
    EXPECT_EQ(count(), 1200);
    panel.selectionFromLayer(b, Qt::ControlModifier);
    EXPECT_EQ(count(), 1600);
}

TEST(LayerThumbnailSelection, ACtrlClickOnTheThumbnailDoesIt) {
    test::applyTheme();
    Document doc(QSize(300, 200), Qt::white);
    doc.paintTarget(doc.currentLayerId()).value()->fill(QRect(100, 60, 40, 40), Qt::red);
    LayerPanel panel;
    panel.resize(260, 520);
    panel.setDocument(&doc);
    panel.show();
    QCoreApplication::processEvents();
    auto* view = panel.findChild<QTreeView*>("layerTree");
    ASSERT_NE(view, nullptr);
    const QRect row = view->visualRect(view->model()->index(0, 0));
    ASSERT_TRUE(row.isValid());
    bool found = false;
    int where = -1;
    for (int x = row.left(); x < row.right() && !found; x += 2) {
        QMouseEvent press(QEvent::MouseButtonPress, QPointF(x, row.center().y()), QPointF(x, row.center().y()), Qt::LeftButton,
                          Qt::LeftButton, Qt::ControlModifier);
        QCoreApplication::sendEvent(view->viewport(), &press);
        QMouseEvent release(QEvent::MouseButtonRelease, QPointF(x, row.center().y()), QPointF(x, row.center().y()), Qt::LeftButton,
                            Qt::NoButton, Qt::ControlModifier);
        QCoreApplication::sendEvent(view->viewport(), &release);
        if (doc.selection()) {
            found = true;
            where = x;
        }
    }
    ASSERT_TRUE(found);
    EXPECT_GT(where, row.left() + 12);
    EXPECT_LT(where, row.left() + 90);
    ASSERT_TRUE(doc.deselect());
    QMouseEvent plain(QEvent::MouseButtonPress, QPointF(where, row.center().y()), QPointF(where, row.center().y()), Qt::LeftButton,
                      Qt::LeftButton, Qt::NoModifier);
    QCoreApplication::sendEvent(view->viewport(), &plain);
    EXPECT_FALSE(doc.selection());
}

TEST_F(SelectionMenuTest, ToolOptionsAreSavedWithTheToolBox) {
    FixedToolOptions o = window->tools()->fixedOptions();
    o.lasso.mode = LassoMode::Polygonal;
    o.wand.gapTolerance = 12;
    o.marquee.transformMode = TransformMode::Rotate;
    o.marquee.final = transform::Interpolation::Lanczos2;
    window->tools()->setFixedOptions(o);
    window->close();
    ToolBox fresh;
    ToolBoxStore store(&fresh, ToolBoxStore::defaultPath());
    ASSERT_TRUE(store.load().ok());
    EXPECT_EQ(fresh.fixedOptions(), o);
}

namespace {

// Lets the window paint (the canvas draws on the first frames after it is shown, and the queued reset of its view runs).
void settle() {
    for (int i = 0; i < 15; ++i) {
        QCoreApplication::processEvents();
        QThread::msleep(15);
    }
}

}

TEST_F(SelectionMenuTest, ScreenshotOfTheWindowWithASelection) {
    paint(QRect(300, 300, 400, 300), QColor(200, 80, 60));
    window->resize(1400, 900);
    settle();
    canvas()->setZoom(0.5, QPointF(canvas()->width() / 2.0, canvas()->height() / 2.0));
    select(QRect(250, 250, 300, 200));
    window->tools()->setFixedTool(FixedTool::Marquee);
    settle();
    EXPECT_TRUE(test::saveScreenshot(window->grab(), QStringLiteral("main_window_marquee_selection")));
    EXPECT_TRUE(test::saveScreenshot(QPixmap::fromImage(canvas()->grabFramebuffer()), QStringLiteral("canvas_with_selection")));
    trigger("layer.transform");
    settle();
    EXPECT_TRUE(test::saveScreenshot(QPixmap::fromImage(canvas()->grabFramebuffer()), QStringLiteral("canvas_transform_handles")));
    EXPECT_TRUE(test::saveScreenshot(window->grab(), QStringLiteral("main_window_transform")));
}
