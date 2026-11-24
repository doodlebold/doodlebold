#include "ScreenshotSupport.h"
#include "ui/ColumnDivider.h"
#include "ui/MainWindow.h"
#include "ui/Theme.h"
#include "ui/panels/ColorPanel.h"

#include <QAction>
#include <QApplication>
#include <QDockWidget>
#include <QMouseEvent>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryDir>

#include <gtest/gtest.h>

#include <memory>

using namespace doodlebold;

namespace {

void settle() {
    for (int i = 0; i < 8; ++i) {
        QCoreApplication::processEvents();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
        QCoreApplication::sendPostedEvents(nullptr, QEvent::LayoutRequest);
    }
}

class PanelDividerTest : public testing::Test {
protected:
    void SetUp() override {
        QStandardPaths::setTestModeEnabled(true);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDir.path());
        test::applyTheme();
        open();
    }
    void TearDown() override { window.reset(); }
    void open() {
        window = std::make_unique<MainWindow>();
        window->resize(1400, 900);
        window->newDocument();
        window->show();
        settle();
    }
    QDockWidget* dock(const char* name) const { return window->findChild<QDockWidget*>(QString::fromLatin1(name)); }
    int width(const char* name) const { return dock(name)->width(); }
    int canvasWidth() const { return window->centralWidget()->width(); }
    QPoint gapCentre() const {
        const QRect nav = dock("navigatorDock")->geometry();
        const QRect color = dock("colorDock")->geometry();
        return {(nav.right() + color.left()) / 2 + 1, nav.center().y()};
    }
    // Sent to what the pointer is over at the press, which then keeps the mouse, as with a real pointer.
    void drag(QPoint from, QPoint to) {
        QWidget* target = window->childAt(from);
        if (!target)
            target = window.get();
        const auto send = [&](QEvent::Type type, QPoint p, Qt::MouseButton button, Qt::MouseButtons buttons) {
            const QPoint local = target->mapFrom(window.get(), p);
            QMouseEvent e(type, QPointF(local), QPointF(target->mapToGlobal(local)), button, buttons, Qt::NoModifier);
            QApplication::sendEvent(target, &e);
            settle();
        };
        send(QEvent::MouseButtonPress, from, Qt::LeftButton, Qt::LeftButton);
        send(QEvent::MouseMove, (from + to) / 2, Qt::NoButton, Qt::LeftButton);
        send(QEvent::MouseMove, to, Qt::NoButton, Qt::LeftButton);
        send(QEvent::MouseButtonRelease, to, Qt::LeftButton, Qt::NoButton);
    }
    void dragGapBy(int dx) { drag(gapCentre(), gapCentre() + QPoint(dx, 0)); }
    void expectColumnsDoNotOverlap() {
        EXPECT_LE(dock("navigatorDock")->geometry().right(), dock("colorDock")->geometry().left());
        EXPECT_LE(dock("colorDock")->geometry().right(), window->centralWidget()->geometry().left());
        EXPECT_GE(dock("navigatorDock")->width(), dock("layersDock")->minimumSizeHint().width());
        EXPECT_EQ(dock("navigatorDock")->width(), dock("layersDock")->width());
        EXPECT_EQ(dock("colorDock")->width(), dock("toolsDock")->width());
    }
    QTemporaryDir settingsDir;
    std::unique_ptr<MainWindow> window;
};

}

TEST_F(PanelDividerTest, DraggingTheGapRightWidensTheNavigatorColumnFromTheCanvas) {
    const int navigator = width("navigatorDock"), color = width("colorDock"), canvas = canvasWidth();
    dragGapBy(80);
    EXPECT_EQ(width("navigatorDock"), navigator + 80);
    EXPECT_EQ(width("colorDock"), color);
    EXPECT_EQ(canvasWidth(), canvas - 80);
    expectColumnsDoNotOverlap();
}

TEST_F(PanelDividerTest, DraggingTheGapLeftGivesTheRoomBackAndStopsAtTheColumnsLeastWidth) {
    const int navigator = width("navigatorDock"), color = width("colorDock"), canvas = canvasWidth();
    dragGapBy(120);
    dragGapBy(-50);
    EXPECT_EQ(width("navigatorDock"), navigator + 70);
    EXPECT_EQ(canvasWidth(), canvas - 70);
    dragGapBy(-500);
    const int least = std::max(dock("navigatorDock")->minimumSizeHint().width(), dock("layersDock")->minimumSizeHint().width());
    EXPECT_EQ(width("navigatorDock"), least);
    EXPECT_EQ(width("colorDock"), color);
    expectColumnsDoNotOverlap();
}

TEST_F(PanelDividerTest, TheCanvasKeepsItsLeastWidthAndTheColourColumnItsWidth) {
    const int color = width("colorDock");
    dragGapBy(3000);
    EXPECT_EQ(width("colorDock"), color);
    EXPECT_GE(canvasWidth(), window->centralWidget()->minimumSizeHint().width());
    EXPECT_GT(width("navigatorDock"), 500);
    expectColumnsDoNotOverlap();
}

TEST_F(PanelDividerTest, AllOfTheGapTakesThePointerAndShowsTheResizeCursor) {
    const QPoint centre = gapCentre();
    const int cardGap = Theme::metric(Theme::Metric::CardGap);
    const int from = dock("navigatorDock")->geometry().right() + 1 - cardGap;
    const int to = dock("colorDock")->geometry().left() + cardGap - 1;
    for (int x = from; x <= to; ++x) {
        QWidget* under = window->childAt(x, centre.y());
        EXPECT_NE(qobject_cast<ColumnDivider*>(under), nullptr) << "x " << x;
        if (under) {
            EXPECT_EQ(under->cursor().shape(), Qt::SplitHCursor) << "x " << x;
        }
    }
    const int navigator = width("navigatorDock");
    drag({from, centre.y()}, {from + 30, centre.y()});
    EXPECT_EQ(width("navigatorDock"), navigator + 30);
    EXPECT_NE(qobject_cast<ColumnDivider*>(window->childAt(gapCentre())), nullptr) << "the divider follows the gap";
}

TEST_F(PanelDividerTest, TheDividerStaysOnTopWhenQtLaysTheWindowOutAgain) {
    window->command(QStringLiteral("window.show.layerPanel"))->setChecked(false);
    settle();
    window->command(QStringLiteral("window.show.layerPanel"))->setChecked(true);
    window->resize(1300, 800);
    settle();
    EXPECT_NE(qobject_cast<ColumnDivider*>(window->childAt(gapCentre())), nullptr);
}

TEST_F(PanelDividerTest, WithBothColumnsOnTheRightTheGapSetsTheColourColumnsWidth) {
    window->command(QStringLiteral("window.navigatorLayerRight"))->setChecked(true);
    window->command(QStringLiteral("window.colorToolRight"))->setChecked(true);
    settle();
    ASSERT_LT(dock("navigatorDock")->x(), dock("colorDock")->x());
    const int navigator = width("navigatorDock"), color = width("colorDock"), canvas = canvasWidth();
    dragGapBy(-60);
    EXPECT_EQ(width("colorDock"), color + 60);
    EXPECT_EQ(width("navigatorDock"), navigator);
    EXPECT_EQ(canvasWidth(), canvas - 60);
}

TEST_F(PanelDividerTest, ColumnsOnOppositeSidesHaveNoDivider) {
    window->command(QStringLiteral("window.colorToolRight"))->setChecked(true);
    settle();
    EXPECT_FALSE(window->findChild<ColumnDivider*>()->isVisible());
    window->command(QStringLiteral("window.colorToolRight"))->setChecked(false);
    settle();
    EXPECT_TRUE(window->findChild<ColumnDivider*>()->isVisible());
}

TEST_F(PanelDividerTest, WithOneColumnHiddenThereIsNoDivider) {
    window->command(QStringLiteral("window.show.colorPanel"))->setChecked(false);
    window->command(QStringLiteral("window.show.toolPanel"))->setChecked(false);
    settle();
    EXPECT_FALSE(window->findChild<ColumnDivider*>()->isVisible());
}

TEST_F(PanelDividerTest, TheWidthSurvivesARestart) {
    const int start = width("navigatorDock");
    dragGapBy(90);
    const int navigator = width("navigatorDock"), color = width("colorDock");
    ASSERT_EQ(navigator, start + 90);
    window->close();
    window.reset();
    open();
    EXPECT_EQ(width("navigatorDock"), navigator);
    EXPECT_EQ(width("colorDock"), color);
    expectColumnsDoNotOverlap();
}

TEST_F(PanelDividerTest, AColourViewShownAgainKeepsTheWidthDraggedSince) {
    const QString view = QStringLiteral("window.show.") + ColorPanel::viewId(ColorView(1));
    QAction* toggle = window->command(view);
    ASSERT_NE(toggle, nullptr);
    const bool shown = toggle->isChecked();
    toggle->setChecked(!shown);
    settle();
    toggle->setChecked(shown);
    settle();
    const int start = width("navigatorDock");
    dragGapBy(70);
    const int navigator = width("navigatorDock");
    ASSERT_EQ(navigator, start + 70);
    toggle->setChecked(!shown);
    settle();
    toggle->setChecked(shown);
    settle();
    EXPECT_EQ(width("navigatorDock"), navigator);
}

TEST_F(PanelDividerTest, TheLineBetweenNavigatorAndLayersTradesTheirHeightsAndLeavesTheWidth) {
    const QRect navigator = dock("navigatorDock")->geometry();
    const QRect layers = dock("layersDock")->geometry();
    const QPoint line(navigator.center().x(), (navigator.bottom() + layers.top()) / 2 + 1);
    drag(line, line + QPoint(0, 60));
    EXPECT_EQ(dock("navigatorDock")->height(), navigator.height() + 60);
    EXPECT_EQ(dock("layersDock")->height(), layers.height() - 60);
    EXPECT_EQ(width("navigatorDock"), navigator.width());
}
