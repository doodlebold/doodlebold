#include "ScreenshotSupport.h"
#include "core/document/Document.h"
#include "core/tools/ToolBox.h"
#include "ui/CanvasView.h"
#include "ui/DocumentTabs.h"
#include "ui/MainWindow.h"
#include "ui/Toast.h"
#include "ui/panels/LayerPanel.h"
#include "ui/panels/LayerTreeModel.h"

#include <QAction>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QStandardPaths>
#include <QTreeView>

#include <gtest/gtest.h>

#include <cstdlib>
#include <memory>

using namespace doodlebold;

namespace {

class HeldKeysTest : public ::testing::Test {
protected:
    void SetUp() override {
        QStandardPaths::setTestModeEnabled(true);
        test::applyTheme();
        window = std::make_unique<MainWindow>();
        window->resize(1400, 900);
        window->newDocument();
        window->show();
        settle();
        canvas = window->tabs()->currentCanvas();
        doc = window->tabs()->currentDocument();
        centre = QPointF(canvas->rect().center());
    }
    void settle() {
        for (int i = 0; i < 5; ++i)
            QCoreApplication::processEvents();
    }
    void key(QEvent::Type type, int k, Qt::KeyboardModifiers mods = {}) {
        QKeyEvent e(type, k, mods);
        QCoreApplication::sendEvent(canvas, &e);
    }
    void hold(int k, Qt::KeyboardModifiers mods = {}) { key(QEvent::KeyPress, k, mods); }
    void letGo(int k, Qt::KeyboardModifiers mods = {}) { key(QEvent::KeyRelease, k, mods); }
    void mouse(QEvent::Type type, const QPointF& at, Qt::KeyboardModifiers mods, bool down = true) {
        const QPointF global = canvas->mapToGlobal(at);
        QMouseEvent e(type, at, global, type == QEvent::MouseMove ? Qt::NoButton : Qt::LeftButton, down ? Qt::LeftButton : Qt::NoButton,
                      mods);
        QCoreApplication::sendEvent(canvas, &e);
        settle();
    }
    void press(const QPointF& at, Qt::KeyboardModifiers mods = {}) { mouse(QEvent::MouseButtonPress, at, mods); }
    void moveTo(const QPointF& at, Qt::KeyboardModifiers mods = {}) { mouse(QEvent::MouseMove, at, mods); }
    void release(const QPointF& at, Qt::KeyboardModifiers mods = {}) { mouse(QEvent::MouseButtonRelease, at, mods, false); }
    QPoint expectedShift(const QPointF& drag) const {
        return QPoint(int(std::lround(drag.x() / canvas->zoom())), int(std::lround(drag.y() / canvas->zoom())));
    }
    void selectLayers(std::initializer_list<LayerId> ids) {
        auto* panel = window->findChild<LayerPanel*>();
        auto* view = panel->findChild<QTreeView*>(QStringLiteral("layerTree"));
        auto* model = panel->findChild<LayerTreeModel*>();
        QItemSelectionModel::SelectionFlags flags = QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows;
        for (const LayerId id : ids) {
            view->selectionModel()->setCurrentIndex(model->indexOf(id), flags);
            flags = QItemSelectionModel::Select | QItemSelectionModel::Rows;
        }
    }
    LayerId addSpot(const QRect& area, const QColor& colour) {
        const LayerId id = doc->addLayer().value();
        QImage spot(area.size(), QImage::Format_RGBA8888_Premultiplied);
        spot.fill(colour);
        const auto* node = doc->layer(id);
        TiledImage pixels = node->pixels;
        pixels.writeImage(spot, area.topLeft());
        EXPECT_TRUE(doc->addImageLayer(pixels, QStringLiteral("spot")).ok());
        const LayerId made = doc->currentLayerId();
        EXPECT_TRUE(doc->deleteLayer(id).ok());
        doc->setCurrentLayer(made);
        return made;
    }

    std::unique_ptr<MainWindow> window;
    CanvasView* canvas = nullptr;
    Document* doc = nullptr;
    QPointF centre;
};

}

TEST_F(HeldKeysTest, AltIsTheEyedropperOnlyWhileItIsHeld) {
    const LayerId layer = doc->currentLayerId();
    ASSERT_TRUE(doc->fillLayer(layer, QColor(200, 30, 30)).ok());
    const int steps = doc->undoStack()->count();
    ToolBox* tools = window->tools();
    tools->setColor(QColor(1, 2, 3));
    const Qt::CursorShape normal = canvas->cursor().shape();

    hold(Qt::Key_Alt);
    EXPECT_EQ(canvas->cursor().shape(), Qt::CrossCursor);
    press(centre, Qt::AltModifier);
    EXPECT_EQ(tools->color(), QColor(200, 30, 30)) << "the press picks";
    EXPECT_EQ(doc->undoStack()->count(), steps) << "and paints nothing";
    release(centre, Qt::AltModifier);

    letGo(Qt::Key_Alt, Qt::AltModifier);
    EXPECT_EQ(canvas->cursor().shape(), normal);
    EXPECT_NE(canvas->cursor().shape(), Qt::CrossCursor);
    tools->setColor(QColor(1, 2, 3));
    press(centre);
    release(centre);
    EXPECT_EQ(tools->color(), QColor(1, 2, 3)) << "without Alt the brush paints";
    canvas->settleStroke();
    EXPECT_EQ(doc->undoStack()->count(), steps + 1);
}

TEST_F(HeldKeysTest, LettingGoOfAltEndsThePickingEvenWithTheButtonDown) {
    const LayerId layer = doc->currentLayerId();
    ASSERT_TRUE(doc->fillLayer(layer, QColor(200, 30, 30)).ok());
    ToolBox* tools = window->tools();
    tools->setColor(QColor(1, 2, 3));
    hold(Qt::Key_Alt);
    press(centre, Qt::AltModifier);
    EXPECT_EQ(tools->color(), QColor(200, 30, 30));
    letGo(Qt::Key_Alt, Qt::AltModifier);
    tools->setColor(QColor(1, 2, 3));
    const int steps = doc->undoStack()->count();
    moveTo(centre + QPointF(5, 0));
    release(centre + QPointF(5, 0));
    EXPECT_EQ(tools->color(), QColor(1, 2, 3));
    EXPECT_EQ(doc->undoStack()->count(), steps) << "and no stroke starts";
}

TEST_F(HeldKeysTest, CtrlMovesTheLayerOnlyWhileItIsHeld) {
    const LayerId layer = doc->currentLayerId();
    ASSERT_TRUE(doc->fillLayer(layer, Qt::red).ok());
    const QRect before = doc->layer(layer)->pixels.contentBounds(true);
    const int steps = doc->undoStack()->count();
    const Qt::CursorShape normal = canvas->cursor().shape();

    hold(Qt::Key_Control);
    EXPECT_EQ(canvas->cursor().shape(), Qt::SizeAllCursor);
    const QPointF drag(40, 24);
    press(centre, Qt::ControlModifier);
    moveTo(centre + drag, Qt::ControlModifier);
    EXPECT_EQ(doc->layer(layer)->pixels.contentBounds(true), before) << "nothing is applied during the drag";
    release(centre + drag, Qt::ControlModifier);
    EXPECT_EQ(doc->layer(layer)->pixels.contentBounds(true), before.translated(expectedShift(drag)));
    EXPECT_EQ(doc->undoStack()->count(), steps + 1);

    letGo(Qt::Key_Control, Qt::ControlModifier);
    EXPECT_EQ(canvas->cursor().shape(), normal);
    EXPECT_EQ(window->tools()->fixedTool(), std::nullopt) << "the tool in use has not changed";
}

TEST_F(HeldKeysTest, LettingGoOfCtrlAppliesTheMoveAndEndsIt) {
    const LayerId layer = doc->currentLayerId();
    ASSERT_TRUE(doc->fillLayer(layer, Qt::red).ok());
    const QRect before = doc->layer(layer)->pixels.contentBounds(true);
    const QPointF drag(40, 0);
    hold(Qt::Key_Control);
    press(centre, Qt::ControlModifier);
    moveTo(centre + drag, Qt::ControlModifier);
    letGo(Qt::Key_Control, Qt::ControlModifier);
    const QRect applied = before.translated(expectedShift(drag));
    EXPECT_EQ(doc->layer(layer)->pixels.contentBounds(true), applied);
    moveTo(centre + drag + QPointF(60, 0));
    release(centre + drag + QPointF(60, 0));
    EXPECT_EQ(doc->layer(layer)->pixels.contentBounds(true), applied) << "the rest of the drag does nothing";
}

TEST_F(HeldKeysTest, CtrlMovesEverySelectedLayerInOneStep) {
    const LayerId a = addSpot(QRect(10, 10, 30, 30), Qt::red);
    const LayerId b = addSpot(QRect(100, 60, 20, 20), Qt::blue);
    const LayerId other = addSpot(QRect(50, 90, 10, 10), Qt::green);
    selectLayers({a, b});
    settle();
    const QRect boxA = doc->layer(a)->pixels.contentBounds(true), boxB = doc->layer(b)->pixels.contentBounds(true),
                boxOther = doc->layer(other)->pixels.contentBounds(true);
    const int steps = doc->undoStack()->count();

    const QPointF drag(32, 16);
    hold(Qt::Key_Control);
    press(centre, Qt::ControlModifier);
    moveTo(centre + drag, Qt::ControlModifier);
    release(centre + drag, Qt::ControlModifier);
    letGo(Qt::Key_Control, Qt::ControlModifier);

    const QPoint shift = expectedShift(drag);
    EXPECT_EQ(doc->layer(a)->pixels.contentBounds(true), boxA.translated(shift));
    EXPECT_EQ(doc->layer(b)->pixels.contentBounds(true), boxB.translated(shift));
    EXPECT_EQ(doc->layer(other)->pixels.contentBounds(true), boxOther) << "a layer not selected stays";
    EXPECT_EQ(doc->undoStack()->count(), steps + 1);
    ASSERT_TRUE(doc->undo().ok());
    EXPECT_EQ(doc->layer(a)->pixels.contentBounds(true), boxA);
    EXPECT_EQ(doc->layer(b)->pixels.contentBounds(true), boxB);
}

TEST_F(HeldKeysTest, CtrlMovesWhatIsInsideASelectedFolder) {
    const LayerId inside = addSpot(QRect(10, 10, 30, 30), Qt::red);
    const LayerId folder = doc->addFolder().value();
    ASSERT_TRUE(doc->moveLayer(inside, folder, 0).ok());
    doc->setCurrentLayer(folder);
    settle();
    const QRect box = doc->layer(inside)->pixels.contentBounds(true);
    const QPointF drag(24, 0);
    hold(Qt::Key_Control);
    press(centre, Qt::ControlModifier);
    moveTo(centre + drag, Qt::ControlModifier);
    release(centre + drag, Qt::ControlModifier);
    letGo(Qt::Key_Control, Qt::ControlModifier);
    EXPECT_EQ(doc->layer(inside)->pixels.contentBounds(true), box.translated(expectedShift(drag)));
}

TEST_F(HeldKeysTest, ALockedLayerRefusesTheMoveWithAMessage) {
    const LayerId a = addSpot(QRect(10, 10, 30, 30), Qt::red);
    const LayerId b = addSpot(QRect(100, 60, 20, 20), Qt::blue);
    LayerProps props = doc->layer(b)->props;
    props.locked = true;
    ASSERT_TRUE(doc->setLayerProps(b, props).ok());
    selectLayers({a, b});
    settle();
    const QRect box = doc->layer(a)->pixels.contentBounds(true);
    hold(Qt::Key_Control);
    press(centre, Qt::ControlModifier);
    moveTo(centre + QPointF(30, 0), Qt::ControlModifier);
    release(centre + QPointF(30, 0), Qt::ControlModifier);
    letGo(Qt::Key_Control, Qt::ControlModifier);
    EXPECT_EQ(doc->layer(a)->pixels.contentBounds(true), box);
    EXPECT_FALSE(window->findChild<ToastStack*>()->toasts().isEmpty());
}

TEST_F(HeldKeysTest, CtrlDoesNotTakeOverTheTextToolOrARunningTransform) {
    const LayerId layer = doc->currentLayerId();
    ASSERT_TRUE(doc->fillLayer(layer, Qt::red).ok());
    ASSERT_TRUE(canvas->beginTransform());
    hold(Qt::Key_Control);
    EXPECT_NE(canvas->cursor().shape(), Qt::SizeAllCursor);
    letGo(Qt::Key_Control, Qt::ControlModifier);
    canvas->cancelTransform();

    window->tools()->setFixedTool(FixedTool::Text);
    settle();
    hold(Qt::Key_Control);
    EXPECT_NE(canvas->cursor().shape(), Qt::SizeAllCursor);
    letGo(Qt::Key_Control, Qt::ControlModifier);
}

TEST_F(HeldKeysTest, AltSpaceRotatesOnlyWhileBothAreHeld) {
    const double start = canvas->rotation();
    const QPointF from = centre + QPointF(150, 0);
    hold(Qt::Key_Alt);
    hold(Qt::Key_Space, Qt::AltModifier);
    press(from, Qt::AltModifier);
    moveTo(centre + QPointF(0, 150), Qt::AltModifier);
    const double turned = canvas->rotation();
    EXPECT_NE(turned, start);
    letGo(Qt::Key_Alt, Qt::AltModifier);
    moveTo(centre + QPointF(-150, 0));
    EXPECT_EQ(canvas->rotation(), turned) << "letting go of Alt ends it";
    release(centre + QPointF(-150, 0));
    letGo(Qt::Key_Space);

    hold(Qt::Key_Alt);
    hold(Qt::Key_Space, Qt::AltModifier);
    press(from, Qt::AltModifier);
    moveTo(centre + QPointF(0, -150), Qt::AltModifier);
    const double turnedAgain = canvas->rotation();
    letGo(Qt::Key_Space, Qt::AltModifier);
    moveTo(centre + QPointF(-150, 0), Qt::AltModifier);
    EXPECT_EQ(canvas->rotation(), turnedAgain) << "and so does letting go of Space";
    release(centre + QPointF(-150, 0), Qt::AltModifier);
    letGo(Qt::Key_Alt, Qt::AltModifier);
}

TEST_F(HeldKeysTest, LeavingTheCanvasWhileAKeyIsHeldEndsTheGesture) {
    const LayerId layer = doc->currentLayerId();
    ASSERT_TRUE(doc->fillLayer(layer, Qt::red).ok());
    const QRect before = doc->layer(layer)->pixels.contentBounds(true);
    hold(Qt::Key_Control);
    press(centre, Qt::ControlModifier);
    moveTo(centre + QPointF(40, 0), Qt::ControlModifier);
    QFocusEvent out(QEvent::FocusOut);
    QCoreApplication::sendEvent(canvas, &out);
    settle();
    EXPECT_NE(doc->layer(layer)->pixels.contentBounds(true), before) << "what was dragged is applied";
    EXPECT_NE(canvas->cursor().shape(), Qt::SizeAllCursor);
}
