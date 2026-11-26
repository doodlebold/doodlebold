#include "core/document/Document.h"
#include "core/input/TabletStats.h"
#include "core/tools/ToolBox.h"
#include "core/view/ViewNavigator.h"
#include "ui/CanvasView.h"
#include "ui/MainWindow.h"

#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QElapsedTimer>
#include <QPointingDevice>
#include <QStandardPaths>
#include <QTabletEvent>

#include <gtest/gtest.h>

#include <cmath>
#include <memory>

using namespace doodlebold;

namespace {

struct Rig {
    TabletStats stats;
    Document doc{QSize(600, 300), Qt::white};
    ToolBox tools;
    CanvasView canvas{&doc, &stats};

    Rig() {
        canvas.setToolBox(&tools);
        canvas.resize(800, 500);
        canvas.show();
        QCoreApplication::processEvents();
        canvas.resetView();
        QCoreApplication::processEvents();
        BrushSettings settings = tools.settings();
        settings.size = 800;
        tools.setSettings(settings);
    }

    void pen(QEvent::Type type, const QPointF& docPos, double pressure) {
        const QPointF at = ViewNavigator(doc.view(), QSizeF(canvas.size()), QSizeF(doc.size())).docToWidget().map(docPos);
        QTabletEvent event(type, QPointingDevice::primaryPointingDevice(), at, at, pressure, 0.0, 0.0, 0.0, 0.0, 0.0, Qt::NoModifier,
                           Qt::LeftButton, type == QEvent::TabletRelease ? Qt::NoButton : Qt::LeftButton);
        QCoreApplication::sendEvent(&canvas, &event);
    }

    void line(double y, const QColor& color) {
        tools.setColor(color);
        pen(QEvent::TabletPress, QPointF(60, y), 0.5);
        for (int i = 1; i <= 40; ++i)
            pen(QEvent::TabletMove, QPointF(60 + i * 12.0, y + 8.0 * std::sin(i * 0.3)), 0.7);
        pen(QEvent::TabletRelease, QPointF(540, y), 0.0);
    }

    void waitUntilIdle() {
        QElapsedTimer clock;
        clock.start();
        while (canvas.isPainting() && clock.elapsed() < 20000)
            QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
    }
};

}

TEST(CanvasBackgroundStroke, StrokesStartedWhileOneIsPaintingComeOutTheSameAndInOrder) {
    Rig waited;
    waited.line(100, Qt::red);
    waited.waitUntilIdle();
    waited.line(140, Qt::blue);
    waited.waitUntilIdle();
    waited.line(180, Qt::green);
    waited.waitUntilIdle();

    Rig rushed;
    rushed.line(100, Qt::red);
    rushed.line(140, Qt::blue);
    rushed.line(180, Qt::green);
    rushed.waitUntilIdle();

    EXPECT_FALSE(rushed.canvas.isPainting());
    EXPECT_EQ(rushed.doc.undoStack()->count(), waited.doc.undoStack()->count());
    EXPECT_EQ(rushed.doc.projection(), waited.doc.projection());
}

TEST(CanvasBackgroundStroke, SettlingFinishesAStrokeStillPainting) {
    Rig rig;
    const int steps = rig.doc.undoStack()->count();
    const QImage before = rig.doc.projection().copy();
    rig.line(100, Qt::red);
    rig.canvas.settleStroke();
    EXPECT_FALSE(rig.canvas.isStrokeFinishing());
    EXPECT_EQ(rig.doc.undoStack()->count(), steps + 1);
    rig.doc.undoStack()->undo();
    EXPECT_EQ(rig.doc.projection(), before);
}

TEST(CanvasBackgroundStroke, SettlingAlsoFinishesHeldStrokes) {
    Rig rig;
    const int steps = rig.doc.undoStack()->count();
    rig.line(100, Qt::red);
    rig.line(140, Qt::blue);
    rig.canvas.settleStroke();
    EXPECT_FALSE(rig.canvas.isPainting());
    EXPECT_EQ(rig.doc.undoStack()->count(), steps + 2);
}

TEST(CanvasBackgroundStroke, ClosingWithAStrokeStillPaintingKeepsIt) {
    TabletStats stats;
    Document doc(QSize(600, 300), Qt::white);
    const int steps = doc.undoStack()->count();
    const QImage before = doc.projection().copy();
    ToolBox tools;
    {
        CanvasView canvas(&doc, &stats);
        canvas.setToolBox(&tools);
        canvas.resize(800, 500);
        canvas.show();
        QCoreApplication::processEvents();
        canvas.resetView();
        QCoreApplication::processEvents();
        const auto pen = [&](QEvent::Type type, const QPointF& docPos) {
            const QPointF at = ViewNavigator(doc.view(), QSizeF(canvas.size()), QSizeF(doc.size())).docToWidget().map(docPos);
            QTabletEvent event(type, QPointingDevice::primaryPointingDevice(), at, at, 0.7, 0.0, 0.0, 0.0, 0.0, 0.0, Qt::NoModifier,
                               Qt::LeftButton, type == QEvent::TabletRelease ? Qt::NoButton : Qt::LeftButton);
            QCoreApplication::sendEvent(&canvas, &event);
        };
        pen(QEvent::TabletPress, QPointF(60, 100));
        for (int i = 1; i <= 40; ++i)
            pen(QEvent::TabletMove, QPointF(60 + i * 12.0, 100));
        pen(QEvent::TabletRelease, QPointF(540, 100));
    }
    EXPECT_EQ(doc.undoStack()->count(), steps + 1);
    EXPECT_NE(doc.projection(), before);
}

// Closed with the pen still down (Ctrl+W mid-stroke, or a pen-up Windows Ink lost): the stroke is let go while the canvas is
// still whole. Let go later, by the stroke's own destructor, its pixels being put back reached a canvas half torn down.
TEST(CanvasBackgroundStroke, ClosingWithThePenStillDownLetsTheStrokeGo) {
    TabletStats stats;
    Document doc(QSize(600, 300), Qt::white);
    const int steps = doc.undoStack()->count();
    const QImage before = doc.projection().copy();
    ToolBox tools;
    {
        CanvasView canvas(&doc, &stats);
        canvas.setToolBox(&tools);
        canvas.resize(800, 500);
        canvas.show();
        QCoreApplication::processEvents();
        canvas.resetView();
        QCoreApplication::processEvents();
        const auto pen = [&](QEvent::Type type, const QPointF& docPos) {
            const QPointF at = ViewNavigator(doc.view(), QSizeF(canvas.size()), QSizeF(doc.size())).docToWidget().map(docPos);
            QTabletEvent event(type, QPointingDevice::primaryPointingDevice(), at, at, 0.7, 0.0, 0.0, 0.0, 0.0, 0.0, Qt::NoModifier,
                               Qt::LeftButton, Qt::LeftButton);
            QCoreApplication::sendEvent(&canvas, &event);
        };
        pen(QEvent::TabletPress, QPointF(60, 100));
        for (int i = 1; i <= 40; ++i)
            pen(QEvent::TabletMove, QPointF(60 + i * 12.0, 100));
        ASSERT_TRUE(canvas.isPainting());
    }
    EXPECT_FALSE(doc.isPainting());
    EXPECT_EQ(doc.undoStack()->count(), steps);
    EXPECT_EQ(doc.projection(), before);
}

TEST(CanvasBackgroundStroke, AMixingStrokeUndoesAndRedoes) {
    Rig rig;
    ASSERT_TRUE(rig.tools.selectToolOfType(ToolType::Brush));
    BrushSettings settings = rig.tools.settings();
    settings.size = 800;
    settings.blending = 40;
    settings.persistence = 80;
    rig.tools.setSettings(settings);
    rig.line(100, Qt::red);
    rig.canvas.settleStroke();
    const QImage first = rig.doc.projection().copy();
    rig.line(110, Qt::blue);
    rig.canvas.settleStroke();
    const QImage second = rig.doc.projection().copy();
    EXPECT_NE(second, first);
    rig.doc.undoStack()->undo();
    EXPECT_EQ(rig.doc.projection(), first);
    rig.doc.undoStack()->redo();
    EXPECT_EQ(rig.doc.projection(), second);
}

// Closing a dialog closes the widgets in it as they're destroyed, and the window hears about each one.
TEST(CanvasBackgroundStroke, ADialogClosingDoesntUpsetTheWindow) {
    QStandardPaths::setTestModeEnabled(true);  // never touch the real tools.json
    MainWindow window;
    window.show();
    QCoreApplication::processEvents();
    {
        QDialog dialog(&window);
        auto* combo = new QComboBox(&dialog);
        combo->addItems({QStringLiteral("a"), QStringLiteral("b")});
        dialog.show();
        QCoreApplication::processEvents();
    }
    SUCCEED();
}

TEST(CanvasBackgroundStroke, SettlingEveryCanvasFinishesTheStrokeStillPainting) {
    Rig rig;
    const int steps = rig.doc.undoStack()->count();
    rig.line(100, Qt::red);
    CanvasView::settleAllStrokes();
    EXPECT_FALSE(rig.canvas.isStrokeFinishing());
    EXPECT_EQ(rig.doc.undoStack()->count(), steps + 1);
}

// Windows Ink can keep a pen-up for its own gestures (press and hold), so the canvas next hears the pen hovering, nothing pressed.
TEST(CanvasBackgroundStroke, AMoveWithNothingPressedEndsAStrokeWhoseReleaseWasLost) {
    Rig rig;
    const int steps = rig.doc.undoStack()->count();
    const auto hover = [&](const QPointF& docPos) {
        const QPointF at = ViewNavigator(rig.doc.view(), QSizeF(rig.canvas.size()), QSizeF(rig.doc.size())).docToWidget().map(docPos);
        QTabletEvent event(QEvent::TabletMove, QPointingDevice::primaryPointingDevice(), at, at, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0,
                           Qt::NoModifier, Qt::NoButton, Qt::NoButton);
        QCoreApplication::sendEvent(&rig.canvas, &event);
    };
    rig.pen(QEvent::TabletPress, QPointF(60, 100), 0.5);
    for (int i = 1; i <= 20; ++i)
        rig.pen(QEvent::TabletMove, QPointF(60 + i * 12.0, 100), 0.7);
    hover(QPointF(400, 200));
    EXPECT_FALSE(rig.canvas.isPainting()) << "the stroke ended without its release";
    rig.canvas.settleStroke();
    EXPECT_EQ(rig.doc.undoStack()->count(), steps + 1);
    const QImage ended = rig.doc.projection().copy();
    hover(QPointF(500, 250));
    rig.canvas.settleStroke();
    EXPECT_EQ(rig.doc.projection(), ended) << "hovering after it paints nothing";
}
