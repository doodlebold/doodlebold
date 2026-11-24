#include "core/document/Document.h"
#include "core/input/TabletStats.h"
#include "core/paint/QuickShape.h"
#include "core/tools/ToolBox.h"
#include "ui/CanvasView.h"
#include "ui/tools/QuickShapePill.h"
#include "ui/tools/QuickShapeTool.h"
#include "../SignalCounter.h"

#include <QAbstractButton>
#include <QApplication>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPushButton>
#include <QPointingDevice>
#include <QResizeEvent>
#include <QTabletEvent>

#include <gtest/gtest.h>

#include <cmath>
#include <numbers>

using namespace doodlebold;
using Kind = QuickShapeKind;

namespace {

constexpr double kPi = std::numbers::pi;
const QSize kDoc(400, 400);
const QPointF kOffset(200, 100);  // a document at 100 % in the middle of an 800 × 600 canvas

void wait(int ms) {
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < ms)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
}

struct Rig {
    Rig() : doc(kDoc, Qt::white), canvas(&doc, &stats) {
        canvas.setToolBox(&tools);
        canvas.resize(800, 600);
        canvas.resetView();
        BrushOptions o = tools.options();
        o.stabilizer = StabilizerLevel{0};
        tools.setOptions(o);
        tools.selectToolOfType(ToolType::Pencil);
    }

    void send(QEvent::Type type, const QPointF& at, bool down = true, Qt::KeyboardModifiers mods = {}) {
        const QPointF widget = at + kOffset;
        QMouseEvent e(type, widget, widget, Qt::LeftButton, down ? Qt::LeftButton : Qt::NoButton, mods);
        QCoreApplication::sendEvent(&canvas, &e);
    }
    void press(const QPointF& at) { send(QEvent::MouseButtonPress, at); }
    void move(const QPointF& at, Qt::KeyboardModifiers mods = {}) { send(QEvent::MouseMove, at, true, mods); }
    void release(const QPointF& at) { send(QEvent::MouseButtonRelease, at, false); }
    void key(int k, Qt::KeyboardModifiers mods = {}) {
        QKeyEvent e(QEvent::KeyPress, k, mods);
        QCoreApplication::sendEvent(&canvas, &e);
    }

    // A wobbly circle, without the pen-up.
    void drawCircle(const QPointF& centre = {200, 200}, double radius = 100) {
        constexpr int kCount = 90;
        for (int i = 0; i <= kCount; ++i) {
            const double t = double(i) / kCount;
            const double a = -kPi / 2.0 + t * 2.0 * kPi;
            const double r = radius * (1.0 + 0.008 * std::sin(t * 23.0));
            const QPointF at = centre + QPointF(r * std::cos(a), r * std::sin(a));
            if (i == 0)
                press(at);
            else
                move(at);
        }
    }
    QPointF circleEnd() const { return QPointF(200, 100); }

    void snapCircle() {
        drawCircle();
        ASSERT_TRUE(canvas.snapHeldStroke());
    }
    void adjustCircle() {
        snapCircle();
        release(circleEnd());
    }

    QImage picture() const { return doc.currentLayer()->pixels.toImage(doc.rect()); }
    const QuickShape& shape() const { return canvas.shapeTool()->edit().shape(); }
    int steps() { return doc.undoStack()->count(); }
    QuickShapePill* pill() const { return canvas.shapePill(); }

    TabletStats stats;
    Document doc;
    ToolBox tools;
    CanvasView canvas;
};

QImage plainCirclePicture() {
    Rig plain;
    plain.drawCircle();
    plain.release(plain.circleEnd());
    return plain.picture();
}

}

TEST(QuickShapesHold, HoldingStillForTheWholeHoldSnapsTheStroke) {
    Rig r;
    r.drawCircle();
    const QImage drawn = r.picture();
    wait(CanvasView::kShapeHoldMs - 500);
    EXPECT_EQ(r.canvas.shapeTool(), nullptr);
    wait(700);
    ASSERT_NE(r.canvas.shapeTool(), nullptr);
    EXPECT_EQ(r.shape().kind, Kind::Circle);
    EXPECT_NE(r.picture(), drawn);
    EXPECT_TRUE(r.canvas.isPainting());
    // No handles and no pill yet, while the button is down.
    EXPECT_FALSE(r.canvas.shapeTool()->adjusting());
    EXPECT_TRUE(!r.pill() || !r.pill()->isVisibleTo(&r.canvas));
    r.release(r.circleEnd());
    EXPECT_TRUE(r.canvas.shapeTool()->adjusting());
    EXPECT_TRUE(r.canvas.isPainting());
    ASSERT_NE(r.pill(), nullptr);
    EXPECT_TRUE(r.pill()->isVisibleTo(&r.canvas));
    EXPECT_TRUE(r.pill()->shapeButton(Kind::Circle)->isChecked());
    EXPECT_EQ(r.steps(), 0);
}

TEST(QuickShapesHold, MovingAwayStartsTheHoldAgainButTremblingDoesNot) {
    Rig r;
    r.drawCircle();
    wait(CanvasView::kShapeHoldMs - 300);
    r.move(r.circleEnd() + QPointF(10, 0));
    r.move(r.circleEnd());
    wait(CanvasView::kShapeHoldMs - 400);
    EXPECT_EQ(r.canvas.shapeTool(), nullptr);  // long enough but for the move

    Rig tremble;
    tremble.drawCircle();
    wait(CanvasView::kShapeHoldMs - 300);
    tremble.move(tremble.circleEnd() + QPointF(CanvasView::kShapeStillPx - 1, 0));
    tremble.move(tremble.circleEnd() - QPointF(0, CanvasView::kShapeStillPx - 1));
    wait(500);
    EXPECT_NE(tremble.canvas.shapeTool(), nullptr);
}

TEST(QuickShapesHold, ReleasingBeforeTheHoldEndsLeavesAPlainStroke) {
    Rig r;
    r.drawCircle();
    r.release(r.circleEnd());
    EXPECT_FALSE(r.canvas.isPainting());
    wait(50);
    EXPECT_FALSE(r.canvas.snapHeldStroke());
    EXPECT_EQ(r.steps(), 1);
}

TEST(QuickShapesSnap, AStrokeThatIsNoShapeGoesOnAsDrawn) {
    Rig r;
    r.press({60, 60});
    for (int i = 1; i <= 40; ++i)
        r.move(QPointF(60 + i * 6, 60 + 40 * std::sin(i * 0.9)));
    EXPECT_FALSE(r.canvas.snapHeldStroke());
    EXPECT_EQ(r.canvas.shapeTool(), nullptr);
    EXPECT_TRUE(r.canvas.isPainting());
    const QImage before = r.picture();
    r.move({320, 300});
    EXPECT_NE(r.picture(), before);
    r.release({320, 300});
    EXPECT_FALSE(r.canvas.isPainting());
    EXPECT_EQ(r.steps(), 1);
}

TEST(QuickShapesSnap, ToolsThatDrawNoLinesDoNotSnap) {
    for (const ToolType type : {ToolType::Blur, ToolType::Smudge}) {
        Rig r;
        ASSERT_TRUE(r.tools.selectToolOfType(type));
        r.drawCircle();
        EXPECT_FALSE(r.canvas.snapHeldStroke());
        EXPECT_EQ(r.canvas.shapeTool(), nullptr);
        r.release(r.circleEnd());
    }
    Rig r;
    r.tools.selectOrCreateToolOfType(ToolType::SelectionPen);
    r.drawCircle();
    EXPECT_FALSE(r.canvas.snapHeldStroke());
    r.release(r.circleEnd());
}

TEST(QuickShapesSnap, LineToolsSnapWithTheirOwnBrush) {
    for (const ToolType type : {ToolType::Brush, ToolType::Marker, ToolType::Eraser}) {
        Rig r;
        ASSERT_TRUE(r.tools.selectToolOfType(type));
        r.doc.paintTarget(r.doc.currentLayerId()).value()->fill(r.doc.rect(), QColor(10, 10, 10));  // for the Eraser to erase
        r.drawCircle();
        EXPECT_TRUE(r.canvas.snapHeldStroke());
        r.release(r.circleEnd());
        r.canvas.acceptShape();
        EXPECT_FALSE(r.canvas.isPainting());
        EXPECT_EQ(r.steps(), 1);
    }
}

TEST(QuickShapesSnap, MovingBeforeReleaseResizesAndTurnsThePictureFollows) {
    Rig r;
    r.snapCircle();
    const double width = r.shape().width;
    const QImage small = r.picture();
    r.move(QPointF(200, 100) + QPointF(0, -1));  // trembling, within the still pixels
    EXPECT_EQ(r.shape().width, width);
    r.move({200, 40});
    EXPECT_NEAR(r.shape().width, width * 1.6, width * 0.03);
    r.release({200, 40});
    EXPECT_NE(r.picture(), small);
    EXPECT_TRUE(r.canvas.shapeTool()->adjusting());
}

TEST(QuickShapesAdjust, ThePillChangesTheShapeAndThePicture) {
    Rig r;
    r.adjustCircle();
    const QImage circle = r.picture();
    r.pill()->shapeButton(Kind::Square)->click();
    EXPECT_EQ(r.shape().kind, Kind::Square);
    EXPECT_TRUE(r.pill()->shapeButton(Kind::Square)->isChecked());
    EXPECT_FALSE(r.pill()->shapeButton(Kind::Circle)->isChecked());
    EXPECT_NE(r.picture(), circle);
    r.pill()->shapeButton(Kind::Triangle)->click();
    EXPECT_EQ(r.shape().corners.size(), 3u);

    r.pill()->shapeButton(Kind::Polygon)->click();
    EXPECT_EQ(r.canvas.shapeTool()->edit().cornerCount(), 5);
    EXPECT_FALSE(r.pill()->fewerButton()->isEnabled());
    const QImage five = r.picture();
    r.pill()->moreButton()->click();
    EXPECT_EQ(r.canvas.shapeTool()->edit().cornerCount(), 6);
    EXPECT_NE(r.picture(), five);
    EXPECT_TRUE(r.pill()->fewerButton()->isEnabled());
    EXPECT_EQ(r.steps(), 0);
    r.pill()->doneButton()->click();
    EXPECT_EQ(r.canvas.shapeTool(), nullptr);
    EXPECT_FALSE(r.pill()->isVisibleTo(&r.canvas));
    EXPECT_EQ(r.steps(), 1);
}

TEST(QuickShapesAdjust, HandlesResizeMoveAndTurn) {
    Rig r;
    r.adjustCircle();
    const QuickShape start = r.shape();
    const QPointF corner = r.canvas.shapeTool()->edit().boxHandle(QuickShapeEdit::Grip::BoxCorner, 2);
    const QImage before = r.picture();
    r.press(corner);
    r.move(corner + QPointF(40, 40));
    r.release(corner + QPointF(40, 40));
    EXPECT_NEAR(r.shape().width, start.width + 40, 1.0);
    wait(40);  // the repaint waits for the next frame
    EXPECT_NE(r.picture(), before);

    r.press(r.shape().centre);
    r.move(r.shape().centre + QPointF(-15, 5));
    const QPointF moved = start.centre + QPointF(20 - 15, 20 + 5);
    r.release(moved);
    EXPECT_NEAR(r.shape().centre.x(), moved.x(), 1.0);
    EXPECT_NEAR(r.shape().centre.y(), moved.y(), 1.0);
    EXPECT_EQ(r.steps(), 0);
    EXPECT_TRUE(r.canvas.isPainting());
    EXPECT_EQ(r.canvas.shapeTool()->adjusting(), true);
}

TEST(QuickShapesAdjust, EscapeGivesBackTheStrokeAsDrawn) {
    const QImage plain = plainCirclePicture();
    Rig r;
    r.adjustCircle();
    r.pill()->shapeButton(Kind::Square)->click();
    EXPECT_NE(r.picture(), plain);
    r.key(Qt::Key_Escape);
    EXPECT_EQ(r.canvas.shapeTool(), nullptr);
    EXPECT_FALSE(r.canvas.isPainting());
    EXPECT_EQ(r.picture(), plain);
    EXPECT_EQ(r.steps(), 1);
}

TEST(QuickShapesAdjust, TheCrossAndUndoAlsoGoBackToTheStroke) {
    const QImage plain = plainCirclePicture();
    Rig cross;
    cross.adjustCircle();
    cross.pill()->cancelButton()->click();
    EXPECT_EQ(cross.picture(), plain);

    Rig undo;
    undo.adjustCircle();
    undo.key(Qt::Key_Z, Qt::ControlModifier);
    EXPECT_EQ(undo.canvas.shapeTool(), nullptr);
    EXPECT_EQ(undo.picture(), plain);
    EXPECT_EQ(undo.steps(), 1);
}

TEST(QuickShapesAdjust, EscapeWhileStillHoldingGoesBackAndEndsTheStroke) {
    const QImage plain = plainCirclePicture();
    Rig r;
    r.snapCircle();
    r.key(Qt::Key_Escape);
    EXPECT_EQ(r.canvas.shapeTool(), nullptr);
    EXPECT_FALSE(r.canvas.isPainting());
    EXPECT_EQ(r.picture(), plain);
    r.move({50, 50});
    r.release({50, 50});
    EXPECT_EQ(r.picture(), plain);
    EXPECT_EQ(r.steps(), 1);
}

TEST(QuickShapesAdjust, EnterKeepsTheShapeAsOneUndoStep) {
    Rig r;
    const QImage empty = r.picture();
    r.adjustCircle();
    const QImage shape = r.picture();
    r.key(Qt::Key_Return);
    EXPECT_EQ(r.canvas.shapeTool(), nullptr);
    EXPECT_FALSE(r.canvas.isPainting());
    EXPECT_EQ(r.picture(), shape);
    EXPECT_EQ(r.steps(), 1);
    r.doc.undoStack()->undo();
    EXPECT_EQ(r.picture(), empty);
}

TEST(QuickShapesAdjust, APressFarFromTheShapeKeepsItAndStartsTheNextStroke) {
    Rig r;
    r.adjustCircle();
    const QImage shape = r.picture();
    r.press({5, 5});
    EXPECT_EQ(r.canvas.shapeTool(), nullptr);
    EXPECT_EQ(r.steps(), 1);
    EXPECT_TRUE(r.canvas.isPainting());
    r.move({40, 5});
    r.release({40, 5});
    EXPECT_EQ(r.steps(), 2);
    EXPECT_NE(r.picture(), shape);
    EXPECT_FALSE(r.pill()->isVisibleTo(&r.canvas));
}

TEST(QuickShapesAdjust, UsingAnythingElseKeepsTheShapeFirst) {
    Rig r;
    QPushButton elsewhere;
    r.adjustCircle();
    QMouseEvent press(QEvent::MouseButtonPress, QPointF(2, 2), QPointF(2, 2), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QCoreApplication::sendEvent(&elsewhere, &press);
    EXPECT_EQ(r.canvas.shapeTool(), nullptr);
    EXPECT_FALSE(r.doc.isPainting());
    EXPECT_EQ(r.steps(), 1);
}

TEST(QuickShapesAdjust, ClickingThePillDoesNotEndTheShape) {
    Rig r;
    r.adjustCircle();
    QMouseEvent press(QEvent::MouseButtonPress, QPointF(2, 20), QPointF(2, 20), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QCoreApplication::sendEvent(r.pill(), &press);
    EXPECT_NE(r.canvas.shapeTool(), nullptr);
}

TEST(QuickShapesAdjust, ThePillSitsCentredAtTheTopAndNarrowsOnASmallCanvas) {
    Rig r;
    r.adjustCircle();
    const QRect wide = r.pill()->geometry();
    EXPECT_EQ(wide.top(), 12);
    EXPECT_NEAR(wide.center().x(), r.canvas.width() / 2, 1);
    const QSize wideSize = r.canvas.size();
    r.canvas.resize(420, 300);
    QResizeEvent resized(QSize(420, 300), wideSize);  // a canvas never shown gets none by itself
    QCoreApplication::sendEvent(&r.canvas, &resized);
    EXPECT_LT(r.pill()->width(), wide.width());
    EXPECT_LE(r.pill()->width(), 420);
    EXPECT_NEAR(r.pill()->geometry().center().x(), 210, 1);
}

TEST(QuickShapesPen, APenSnapsWithItsPressureAndThePillIsLeftToTheMouse) {
    Rig r;
    const auto pen = [&](QEvent::Type type, const QPointF& doc, double pressure) {
        const QPointF at = doc + kOffset;
        QTabletEvent e(type, QPointingDevice::primaryPointingDevice(), at, at, pressure, 0.0, 0.0, 0.0, 0.0, 0.0, Qt::NoModifier,
                       Qt::LeftButton, type == QEvent::TabletRelease ? Qt::NoButton : Qt::LeftButton);
        QCoreApplication::sendEvent(&r.canvas, &e);
        return e.isAccepted();
    };
    constexpr int kCount = 90;
    for (int i = 0; i <= kCount; ++i) {
        const double t = double(i) / kCount, a = -kPi / 2.0 + t * 2.0 * kPi;
        pen(i == 0 ? QEvent::TabletPress : QEvent::TabletMove, QPointF(200, 200) + QPointF(100 * std::cos(a), 100 * std::sin(a)),
            0.2 + 0.8 * t);
    }
    ASSERT_TRUE(r.canvas.snapHeldStroke());
    const QImage thinToThick = r.picture();
    pen(QEvent::TabletRelease, {200, 100}, 0.0);
    ASSERT_NE(r.pill(), nullptr);
    EXPECT_TRUE(r.pill()->isVisibleTo(&r.canvas));

    // Over the pill the canvas leaves the pen alone; Qt then gives the pill a mouse press.
    const QPointF overPill = QPointF(r.pill()->geometry().center()) - kOffset;
    EXPECT_FALSE(pen(QEvent::TabletPress, overPill, 0.5));
    EXPECT_FALSE(r.canvas.shapeTool()->isActive());
    pen(QEvent::TabletRelease, overPill, 0.0);

    // A handle answers the pen.
    const QPointF corner = r.canvas.shapeTool()->edit().boxHandle(QuickShapeEdit::Grip::BoxCorner, 2);
    const double width = r.shape().width;
    EXPECT_TRUE(pen(QEvent::TabletPress, corner, 0.5));
    EXPECT_TRUE(r.canvas.shapeTool()->isActive());
    pen(QEvent::TabletMove, corner + QPointF(30, 30), 0.5);
    pen(QEvent::TabletRelease, corner + QPointF(30, 30), 0.0);
    EXPECT_NEAR(r.shape().width, width + 30, 1.0);
    r.canvas.acceptShape();
    EXPECT_EQ(r.steps(), 1);

    // The thin start and the thick end show: the line near its start is narrower than near its end.
    const auto inked = [&](int x0, int x1, int y) {
        int n = 0;
        for (int x = x0; x < x1; ++x)
            n += thinToThick.pixelColor(x, y).alpha() > 0 && thinToThick.pixelColor(x, y).lightness() < 250;
        return n;
    };
    Rig even;
    even.drawCircle();
    ASSERT_TRUE(even.canvas.snapHeldStroke());
    EXPECT_NE(thinToThick, even.picture());
    EXPECT_LE(inked(280, 320, 170), inked(70, 120, 170));
    even.release(even.circleEnd());
}

TEST(QuickShapesAdjust, ColourGoesInTheHistoryOnceTheShapeEnds) {
    Rig r;
    test::SignalCounter painted(&r.canvas, &CanvasView::strokePainted);
    r.adjustCircle();
    EXPECT_EQ(painted.count(), 0);
    r.canvas.acceptShape();
    EXPECT_EQ(painted.count(), 1);
}

TEST(QuickShapesHold, TurnedOffAStrokeHeldStillStaysAStroke) {
    Rig r;
    r.canvas.setQuickShapes(false, 1000);
    r.drawCircle();
    wait(CanvasView::kShapeHoldMs + 200);
    EXPECT_EQ(r.canvas.shapeTool(), nullptr);
    r.release(r.circleEnd());
    EXPECT_EQ(r.canvas.shapeTool(), nullptr);
}

TEST(QuickShapesHold, TheHoldTimeComesFromTheOption) {
    Rig r;
    r.canvas.setQuickShapes(true, 300);
    r.drawCircle();
    wait(500);
    ASSERT_NE(r.canvas.shapeTool(), nullptr);
}

TEST(QuickShapesPreview, ASlowShapeIsAPlainLineWhileItChangesAndPaintedWhenItRests) {
    Rig r;
    r.canvas.setShapeLiveRepaintLimit(-1);  // as with a shape too big to repaint in a frame
    r.adjustCircle();
    const QImage painted = r.picture();
    ASSERT_FALSE(painted.isNull());
    const int stepsBefore = r.steps();

    const QPointF handle = r.canvas.shapeTool()->edit().boxHandle(QuickShapeEdit::Grip::BoxCorner, 2);
    const QPointF from = handle;
    r.press(from);
    r.move(from + QPointF(40, 40));
    EXPECT_TRUE(r.canvas.isPreviewingShape());
    bool empty = true;
    const QImage during = r.picture();
    for (int y = 0; y < during.height() && empty; ++y)
        for (int x = 0; x < during.width() && empty; ++x)
            empty = qAlpha(during.pixel(x, y)) == 0;
    EXPECT_TRUE(empty) << "the brush is not run during the change";
    r.canvas.grab();

    r.move(from + QPointF(60, 60));
    wait(300);
    EXPECT_FALSE(r.canvas.isPreviewingShape()) << "it rested";
    EXPECT_NE(r.picture(), painted);
    EXPECT_NE(r.picture(), during);

    r.release(from + QPointF(60, 60));
    r.canvas.acceptShape();
    EXPECT_EQ(r.steps(), stepsBefore + 1);
    ASSERT_TRUE(r.doc.undo().ok());
    bool blank = true;
    const QImage undone = r.picture();
    for (int y = 0; y < undone.height() && blank; ++y)
        for (int x = 0; x < undone.width() && blank; ++x)
            blank = qAlpha(undone.pixel(x, y)) == 0;
    EXPECT_TRUE(blank) << "one step takes all of it";
}

TEST(QuickShapesPreview, CancellingDuringThePlainLineBringsTheStrokeBack) {
    Rig r;
    const QImage drawn = plainCirclePicture();
    r.canvas.setShapeLiveRepaintLimit(-1);
    r.adjustCircle();
    const QPointF handle = r.canvas.shapeTool()->edit().boxHandle(QuickShapeEdit::Grip::BoxCorner, 2);
    const QPointF from = handle;
    r.press(from);
    r.move(from + QPointF(40, 40));
    ASSERT_TRUE(r.canvas.isPreviewingShape());
    r.release(from + QPointF(40, 40));
    r.canvas.cancelShape();
    EXPECT_FALSE(r.canvas.isPreviewingShape());
    EXPECT_EQ(r.picture(), drawn);
}
