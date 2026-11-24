#include "ScreenshotSupport.h"
#include "ui/CanvasView.h"
#include "ui/DocumentTabs.h"
#include "ui/DoodleboldStyle.h"
#include "ui/MainWindow.h"
#include "ui/PanelCard.h"

#include <QAction>
#include <QDockWidget>
#include <QHoverEvent>
#include <QMdiSubWindow>
#include <QMenuBar>
#include <QComboBox>
#include <QAbstractButton>
#include <QMouseEvent>
#include <QStandardPaths>
#include <QStatusBar>
#include <QToolBar>
#include <QWindow>

#include <gtest/gtest.h>

#include <cstdlib>
#include <memory>

using namespace doodlebold;

namespace {

class WindowMarginsTest : public testing::Test {
protected:
    void SetUp() override {
        QStandardPaths::setTestModeEnabled(true);
        test::applyTheme();
        window = std::make_unique<MainWindow>();
        window->resize(1400, 900);
        window->newDocument();
        window->show();
        for (int i = 0; i < 5; ++i) {
            QCoreApplication::processEvents();
            QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
        }
    }
    void settle() {
        for (int i = 0; i < 5; ++i) {
            QCoreApplication::processEvents();
            QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
        }
    }
    QRect inWindow(const QWidget* w) const { return QRect(w->mapTo(window.get(), QPoint(0, 0)), w->size()); }
    QRect cardOf(const char* dock) const {
        auto* card = static_cast<PanelCard*>(window->findChild<QDockWidget*>(QString::fromLatin1(dock))->widget());
        return QRect(card->mapTo(window.get(), card->cardRect().topLeft()), card->cardRect().size());
    }
    QRect canvasCard() const {
        const QMdiSubWindow* sub = window->tabs()->findChild<QMdiSubWindow*>();
        return inWindow(sub).adjusted(DoodleboldStyle::kFrameMargin, DoodleboldStyle::kFrameMargin, -DoodleboldStyle::kFrameMargin,
                                      -DoodleboldStyle::kFrameMargin);
    }
    void expectOuterGapsEqual() {
        const int top = window->menuBar()->geometry().bottom() + 1;
        const int bottom = window->statusBar()->geometry().top();
        const int gap = window->outerGap();
        for (const char* d : {"navigatorDock", "colorDock"})
            EXPECT_EQ(cardOf(d).top() - top, gap) << d;
        for (const char* d : {"layersDock", "toolsDock"})
            EXPECT_EQ(bottom - (cardOf(d).bottom() + 1), gap) << d;
    }
    QDockWidget* dock(const char* name) const { return window->findChild<QDockWidget*>(QString::fromLatin1(name)); }
    QPoint gapBetween(const char* top, const char* bottom) const {
        const QRect a = dock(top)->geometry();
        return {a.center().x(), (a.bottom() + dock(bottom)->geometry().top()) / 2};
    }
    Qt::CursorShape cursorHovering(QPoint at) {
        QHoverEvent e(QEvent::HoverMove, QPointF(at), QPointF(window->mapToGlobal(at)), QPointF());
        QApplication::sendEvent(window.get(), &e);
        settle();
        return window->cursor().shape();
    }
    void dragBy(QPoint at, int dy) {
        const auto mouse = [this](QEvent::Type type, QPoint p, Qt::MouseButton button, Qt::MouseButtons buttons) {
            QMouseEvent e(type, QPointF(p), QPointF(window->mapToGlobal(p)), button, buttons, Qt::NoModifier);
            QApplication::sendEvent(window.get(), &e);
            settle();
        };
        mouse(QEvent::MouseButtonPress, at, Qt::LeftButton, Qt::LeftButton);
        mouse(QEvent::MouseMove, at + QPoint(0, dy), Qt::NoButton, Qt::LeftButton);
        mouse(QEvent::MouseButtonRelease, at + QPoint(0, dy), Qt::LeftButton, Qt::NoButton);
    }
    std::unique_ptr<MainWindow> window;
};

}

TEST_F(WindowMarginsTest, TheFirstCardsTopGapEqualsTheLastCardsBottomGapAndTheCardGap) {
    expectOuterGapsEqual();
    const int gap = window->outerGap();
    EXPECT_EQ(cardOf("layersDock").top() - (cardOf("navigatorDock").bottom() + 1), gap) << "two cards in a column";
    EXPECT_EQ(cardOf("colorDock").left() - (cardOf("navigatorDock").right() + 1), gap) << "two columns";
    EXPECT_TRUE(test::saveScreenshot(window->grab(), QStringLiteral("main_window")));
}

TEST_F(WindowMarginsTest, TheCanvasCardLinesUpWithThePanelCards) {
    window->command(QStringLiteral("window.show.viewSelector"))->setChecked(false);
    settle();
    EXPECT_EQ(canvasCard().top(), cardOf("navigatorDock").top());
    EXPECT_EQ(canvasCard().bottom(), cardOf("layersDock").bottom());
}

TEST_F(WindowMarginsTest, TheGapsHoldAfterTheColumnsMoveAndAPanelIsHidden) {
    window->command(QStringLiteral("window.colorToolRight"))->setChecked(true);
    settle();
    expectOuterGapsEqual();
    window->command(QStringLiteral("window.show.layerPanel"))->setChecked(false);
    settle();
    const int bottom = window->statusBar()->geometry().top();
    EXPECT_EQ(bottom - (cardOf("navigatorDock").bottom() + 1), window->outerGap());
    EXPECT_EQ(cardOf("navigatorDock").top() - (window->menuBar()->geometry().bottom() + 1), window->outerGap());
    window->command(QStringLiteral("window.show.layerPanel"))->setChecked(true);
    settle();
    expectOuterGapsEqual();
}

TEST_F(WindowMarginsTest, TheGapUnderTheColorCirclesFixedHeightGivesNoCueAndNoDrag) {
    settle();
    const QPoint gap = gapBetween("colorDock", "toolsDock");
    const int colorHeight = dock("colorDock")->height();
    const QRect tools = dock("toolsDock")->geometry();

    EXPECT_EQ(cursorHovering(gap), Qt::ArrowCursor);
    EXPECT_TRUE(test::saveScreenshot(window->grab(), QStringLiteral("panel_gap_hovered")));
    EXPECT_EQ(cursorHovering(gapBetween("navigatorDock", "layersDock")), Qt::SplitVCursor);
    EXPECT_EQ(cursorHovering(gap), Qt::ArrowCursor) << "the cursor of the gap left behind";

    dragBy(gap, -60);
    dragBy(gap, 60);
    EXPECT_EQ(dock("colorDock")->height(), colorHeight);
    EXPECT_EQ(dock("toolsDock")->geometry(), tools);
}

TEST_F(WindowMarginsTest, AGapThatCanMoveKeepsItsCueAndDrag) {
    settle();
    const QPoint layers = gapBetween("navigatorDock", "layersDock");
    const int navigatorHeight = dock("navigatorDock")->height();
    EXPECT_EQ(cursorHovering(layers), Qt::SplitVCursor);
    dragBy(layers, -30);
    EXPECT_EQ(dock("navigatorDock")->height(), navigatorHeight - 30);
    cursorHovering(QPoint(5, 5));

    // The colour card is as tall as its views and never smaller (no scroll bar): dragging its lower gap up leaves it.
    window->command(QStringLiteral("window.show.rgbSlider"))->setChecked(true);
    window->command(QStringLiteral("window.show.swatches"))->setChecked(true);
    settle();
    const QPoint color = gapBetween("colorDock", "toolsDock");
    const int colorHeight = dock("colorDock")->height();
    dragBy(color, -10);
    EXPECT_EQ(dock("colorDock")->height(), colorHeight);
}

TEST(MainWindowSurface, TheFirstCanvasDoesNotMakeTheWindowAgain) {
    QStandardPaths::setTestModeEnabled(true);
    test::applyTheme();
    MainWindow window;
    window.show();
    QCoreApplication::processEvents();
    ASSERT_TRUE(window.windowHandle());
    EXPECT_EQ(window.windowHandle()->surfaceType(), QSurface::OpenGLSurface);
    const QWindow* const handle = window.windowHandle();
    const WId id = window.winId();

    window.newDocument();
    QCoreApplication::processEvents();

    EXPECT_EQ(window.windowHandle(), handle);
    EXPECT_EQ(window.winId(), id);
}

TEST(MenuBarItems, SitInTheMiddleOfABarOfOneHeightWhereverTheQuickBarIs) {
    QStandardPaths::setTestModeEnabled(true);
    test::applyTheme();
    int height = 0;
    for (const int width : {1500, 900}) {
        MainWindow window;
        window.resize(width, 700);
        window.show();
        for (int i = 0; i < 8; ++i)
            QCoreApplication::processEvents();
        QMenuBar* bar = window.menuBar();
        const QRect file = bar->actionGeometry(bar->actions().first());
        EXPECT_LE(std::abs(file.center().y() - bar->rect().center().y()), 1) << "at width " << width;
        // The quick bar's controls, when it sits in the bar's corner, are in the bar's middle too (feedback: "The settings bar
        // for zoom, stabilizer and other tools is not vertically centered").
        if (QWidget* quick = bar->cornerWidget(Qt::TopRightCorner)) {
            int controls = 0;
            for (QWidget* control : quick->findChildren<QWidget*>()) {
                if (!control->isVisible() || !(qobject_cast<QAbstractButton*>(control) || qobject_cast<QComboBox*>(control)))
                    continue;
                const int centre = control->mapTo(bar, control->rect().center()).y();
                EXPECT_LE(std::abs(centre - bar->rect().center().y()), 1) << control->objectName().toStdString();
                ++controls;
            }
            EXPECT_GT(controls, 3);
        }
        if (height) {
            EXPECT_EQ(bar->height(), height) << "the bar keeps its height when the quick bar leaves it";
        }
        height = bar->height();
    }
}
