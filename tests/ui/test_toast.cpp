#include "ScreenshotSupport.h"
#include "core/document/Document.h"
#include "core/document/EditError.h"
#include "ui/DocumentTabs.h"
#include "ui/MainWindow.h"
#include "ui/Theme.h"
#include "ui/Toast.h"

#include <QAction>
#include <QElapsedTimer>
#include <QPainter>
#include <QStandardPaths>
#include <QStatusBar>

#include <gtest/gtest.h>

#include <cstdlib>
#include <memory>

using namespace doodlebold;

namespace {

void pump(int ms) {
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < ms)
        QCoreApplication::processEvents();
}

constexpr int kSettled = 350;  // past the slide and the fade

int rightInset() {
    return Theme::metric(Theme::Metric::CardGap) + Theme::metric(Theme::Metric::DialogPadding);
}

struct Host {
    QWidget window;
    QWidget* anchor = new QWidget(&window);
    ToastStack* stack = new ToastStack(&window, anchor);

    Host() {
        test::applyTheme();
        window.resize(800, 600);
        anchor->setGeometry(100, 50, 500, 400);
        window.show();
        QCoreApplication::processEvents();
    }
};

}

TEST(Toast, ComesFromTheTopRightAndRestsThereLettingThePenThrough) {
    Host h;
    EXPECT_TRUE(h.stack->toasts().isEmpty());
    h.stack->showMessage(QStringLiteral("The layer is locked."));
    ASSERT_EQ(h.stack->toasts().size(), 1);
    Toast* toast = h.stack->toasts().first();
    EXPECT_TRUE(toast->isVisible());
    const QPoint start = toast->pos();
    pump(kSettled);
    const QPoint rest = toast->pos();
    EXPECT_GT(start.x(), rest.x()) << "from the right";
    EXPECT_LT(start.y(), rest.y()) << "from above";

    EXPECT_EQ(toast->text(), QStringLiteral("The layer is locked."));
    EXPECT_TRUE(toast->testAttribute(Qt::WA_TransparentForMouseEvents));
    EXPECT_EQ(toast->focusPolicy(), Qt::NoFocus);
    EXPECT_TRUE(h.anchor->geometry().contains(toast->geometry()));
    EXPECT_EQ(rest.x() + toast->width(), h.anchor->x() + h.anchor->width() - rightInset());
    EXPECT_GT(toast->geometry().left(), h.anchor->geometry().center().x()) << "at the right, not centred";
}

TEST(Toast, TextStartsAtTheLeftInsideTheDialogPadding) {
    Host h;
    h.stack->showMessage(QStringLiteral("A rather long explanation of why this edit could not be done on this layer, in two lines."));
    pump(kSettled);
    Toast* toast = h.stack->toasts().first();
    const int padding = Theme::metric(Theme::Metric::DialogPadding);
    const QFontMetrics metrics(toast->font());
    EXPECT_GE(toast->height(), 2 * metrics.lineSpacing() + 2 * padding - 2) << "two lines and the padding";

    // Both lines start at the same place, one padding in; a centred text would start each line by its own length.
    const QImage shot = toast->grab().toImage();
    const auto firstInk = [&](int top, int bottom) {
        for (int x = 6; x < shot.width(); ++x)
            for (int y = top; y < bottom; ++y)
                if (shot.pixelColor(x, y).lightness() > 150)
                    return x;
        return -1;
    };
    EXPECT_LE(std::abs(firstInk(padding, padding + metrics.lineSpacing()) - padding), 2);
    EXPECT_LE(std::abs(firstInk(padding + metrics.lineSpacing(), padding + 2 * metrics.lineSpacing()) - padding), 2);
}

TEST(Toast, TheSameMessageShakesTheOneShowingAndStartsItsTimeAgain) {
    Host h;
    h.stack->setHoldTime(800);
    h.stack->showMessage(QStringLiteral("Locked"));
    pump(kSettled);
    Toast* toast = h.stack->toasts().first();
    const QPoint rest = toast->pos();

    h.stack->showMessage(QStringLiteral("Locked"));
    EXPECT_EQ(h.stack->toasts().size(), 1) << "no second one";
    EXPECT_TRUE(toast->isShaking());
    int furthest = 0;
    QElapsedTimer timer;
    timer.start();
    while (toast->isShaking() && timer.elapsed() < 1000) {
        QCoreApplication::processEvents();
        EXPECT_EQ(toast->y(), rest.y()) << "it shakes sideways";
        furthest = std::max(furthest, std::abs(toast->x() - rest.x()));
    }
    EXPECT_GE(furthest, 2);
    EXPECT_EQ(toast->pos(), rest) << "and comes back to its place";
    pump(200);
    EXPECT_FALSE(toast->isLeaving()) << "the shake began its time again: 350 + 360 + 200 ms is past the first 800";
}

TEST(Toast, ARepeatBringsBackAMessageThatWasFading) {
    Host h;
    h.stack->setHoldTime(10);
    h.stack->showMessage(QStringLiteral("Locked"));
    pump(60);
    ASSERT_EQ(h.stack->toasts().size(), 1);
    Toast* toast = h.stack->toasts().first();
    ASSERT_TRUE(toast->isLeaving());
    h.stack->setHoldTime(1000);
    h.stack->showMessage(QStringLiteral("Locked"));
    EXPECT_FALSE(toast->isLeaving());
    pump(kSettled);
    ASSERT_EQ(h.stack->toasts().size(), 1);
    EXPECT_EQ(h.stack->toasts().first(), toast);
}

TEST(Toast, DifferentMessagesStackAndCloseUpAsTheyLeave) {
    Host h;
    h.stack->setHoldTime(1000);
    h.stack->showMessage(QStringLiteral("First"));
    pump(300);
    h.stack->setHoldTime(5000);
    h.stack->showMessage(QStringLiteral("Second"));
    h.stack->showMessage(QStringLiteral("Third"));
    pump(kSettled - 100);
    ASSERT_EQ(h.stack->toasts().size(), 3);
    Toast* first = h.stack->toasts().at(0);
    Toast* second = h.stack->toasts().at(1);
    Toast* third = h.stack->toasts().at(2);
    EXPECT_EQ(second->text(), QStringLiteral("Second"));
    const int gap = Theme::metric(Theme::Metric::FieldSpacing);
    pump(100);
    EXPECT_EQ(first->geometry().right(), second->geometry().right()) << "right edges line up";
    EXPECT_EQ(second->y(), first->geometry().bottom() + 1 + gap);
    EXPECT_EQ(third->y(), second->geometry().bottom() + 1 + gap);
    const int top = first->y();

    pump(800);  // the first one's time is up, and it has faded; the others move up
    ASSERT_EQ(h.stack->toasts().size(), 2);
    EXPECT_EQ(second->y(), top);
    EXPECT_EQ(third->y(), second->geometry().bottom() + 1 + gap);
}

TEST(Toast, TheOldestMakesWayWhenTooManyAreShowing) {
    Host h;
    h.stack->setHoldTime(5000);
    for (int i = 1; i <= 5; ++i)
        h.stack->showMessage(QStringLiteral("Message %1").arg(i));
    pump(kSettled);
    ASSERT_EQ(h.stack->toasts().size(), 5);
    h.stack->showMessage(QStringLiteral("Message 6"));
    pump(kSettled);
    ASSERT_EQ(h.stack->toasts().size(), 5);
    EXPECT_EQ(h.stack->toasts().first()->text(), QStringLiteral("Message 2"));
    EXPECT_EQ(h.stack->toasts().last()->text(), QStringLiteral("Message 6"));
    EXPECT_TRUE(h.anchor->geometry().contains(h.stack->toasts().last()->geometry()));
}

TEST(Toast, ALongTextWrapsInsideANarrowAnchorAndFollowsItsPlace) {
    Host h;
    h.anchor->setGeometry(100, 50, 220, 400);
    h.stack->showMessage(QStringLiteral("A rather long explanation of why this edit could not be done on this layer."));
    pump(kSettled);
    Toast* toast = h.stack->toasts().first();
    EXPECT_TRUE(h.anchor->geometry().contains(toast->geometry()));
    EXPECT_GT(toast->height(), toast->fontMetrics().height() * 2);

    h.anchor->setGeometry(200, 80, 500, 400);
    QCoreApplication::processEvents();
    EXPECT_EQ(toast->x() + toast->width(), 700 - rightInset());
}

TEST(Toast, SavesAScreenshotOfAStack) {
    Host h;
    h.stack->showMessage(QStringLiteral("The layer is locked."));
    h.stack->showMessage(QStringLiteral("A rather long explanation of why this edit could not be done on this layer, in two lines."));
    h.stack->showMessage(QStringLiteral("There is nothing to paste."));
    pump(kSettled);
    EXPECT_TRUE(test::saveScreenshot(h.window.grab(), QStringLiteral("toast")));
}

TEST(Toast, ARefusedEditInTheMainWindowShowsThereNotInTheStatusBar) {
    QStandardPaths::setTestModeEnabled(true);
    test::applyTheme();
    MainWindow window;
    window.resize(1400, 900);
    window.newDocument();
    window.show();
    QCoreApplication::processEvents();
    auto* stack = window.findChild<ToastStack*>();
    ASSERT_TRUE(stack);
    EXPECT_TRUE(stack->toasts().isEmpty());

    Document* doc = window.tabs()->currentDocument();
    const LayerId layer = doc->currentLayerId();
    window.command(QStringLiteral("layer.newFolder"))->trigger();
    doc->setCurrentLayer(layer);
    window.command(QStringLiteral("layer.delete"))->trigger();
    QCoreApplication::processEvents();
    ASSERT_EQ(stack->toasts().size(), 1);
    EXPECT_EQ(stack->toasts().first()->text(), describe(EditError::LastLayer));
    EXPECT_TRUE(window.statusBar()->currentMessage().isEmpty());
    EXPECT_TRUE(doc->layer(layer));

    window.command(QStringLiteral("layer.delete"))->trigger();
    EXPECT_EQ(stack->toasts().size(), 1);
    EXPECT_TRUE(stack->toasts().first()->isShaking());
    pump(kSettled);
    EXPECT_TRUE(test::saveScreenshot(window.grab(), QStringLiteral("toast_window")));
}
