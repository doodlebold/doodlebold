#include "ScreenshotSupport.h"
#include "core/color/ColorAssets.h"
#include "core/color/ColorModels.h"
#include "core/color/ColorState.h"
#include "core/tools/ToolBox.h"
#include "ui/CanvasView.h"
#include "ui/DocumentTabs.h"
#include "ui/MainWindow.h"
#include "ui/PanelCard.h"
#include "ui/color/ColorSliderViews.h"
#include "ui/color/ColorWheelView.h"
#include "ui/color/SwatchesView.h"
#include "ui/panels/ColorPanel.h"

#include <QAction>
#include <QApplication>
#include <QDockWidget>
#include <QFrame>
#include <QLabel>
#include <QScrollArea>
#include <QScrollBar>
#include <QSet>
#include <QMenu>
#include <QMouseEvent>
#include <QStandardPaths>
#include <QToolButton>

#include <algorithm>
#include <cmath>

#include <gtest/gtest.h>

#include <memory>
#include <ostream>

using namespace doodlebold;

namespace {

::testing::AssertionResult near(const QColor& a, const QColor& b) {
    if (std::abs(a.red() - b.red()) <= 1 && std::abs(a.green() - b.green()) <= 1 && std::abs(a.blue() - b.blue()) <= 1)
        return ::testing::AssertionSuccess();
    return ::testing::AssertionFailure() << a.name().toStdString() << " vs " << b.name().toStdString();
}

void click(QWidget* w, QPointF pos, Qt::KeyboardModifiers mods = Qt::NoModifier, Qt::MouseButton button = Qt::LeftButton) {
    QMouseEvent press(QEvent::MouseButtonPress, pos, w->mapToGlobal(pos), button, button, mods);
    QCoreApplication::sendEvent(w, &press);
    QMouseEvent release(QEvent::MouseButtonRelease, pos, w->mapToGlobal(pos), button, Qt::NoButton, mods);
    QCoreApplication::sendEvent(w, &release);
}

void drag(QWidget* w, QPointF from, QPointF to, int steps = 10) {
    QMouseEvent press(QEvent::MouseButtonPress, from, w->mapToGlobal(from), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QCoreApplication::sendEvent(w, &press);
    for (int i = 1; i <= steps; ++i) {
        const QPointF p = from + (to - from) * (double(i) / steps);
        QMouseEvent move(QEvent::MouseMove, p, w->mapToGlobal(p), Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
        QCoreApplication::sendEvent(w, &move);
    }
    QMouseEvent release(QEvent::MouseButtonRelease, to, w->mapToGlobal(to), Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    QCoreApplication::sendEvent(w, &release);
}

class ColorPanelTest : public testing::Test {
protected:
    void SetUp() override {
        QStandardPaths::setTestModeEnabled(true);
        test::applyTheme();
        panel = std::make_unique<ColorPanel>(&tools, &assets);
        panel->resize(240, 900);
        panel->show();
        for (int i = 0; i < kColorViewCount; ++i)
            panel->toggle(ColorView(i))->setChecked(true);
        QCoreApplication::processEvents();
    }

    template <typename T> T* child(const char* name) const {
        T* w = panel->findChild<T*>(QString::fromLatin1(name));
        EXPECT_NE(w, nullptr) << name;
        return w;
    }

    ToolBox tools;
    ColorAssets assets;
    std::unique_ptr<ColorPanel> panel;
};

}

TEST_F(ColorPanelTest, ViewsStackInOrderAndTheButtonsShowAndHideThem) {
    int lastTop = -1;
    for (int i = 0; i < kColorViewCount; ++i) {
        QWidget* v = panel->view(ColorView(i));
        EXPECT_TRUE(v->isVisible()) << i;
        EXPECT_GT(v->y(), lastTop) << i;
        lastTop = v->y();
    }
    panel->toggle(ColorView::Rgb)->setChecked(false);
    EXPECT_FALSE(panel->view(ColorView::Rgb)->isVisible());
    EXPECT_TRUE(ColorPanel::shownAtStart(ColorView::Circle));
    EXPECT_FALSE(ColorPanel::shownAtStart(ColorView::Swatches));
}

TEST_F(ColorPanelTest, TheViewsFollowTheToolBoxAndBack) {
    tools.setColor(QColor(10, 200, 100));
    EXPECT_EQ(panel->state()->color(), QColor(10, 200, 100));
    auto* rgb = child<RgbSlidersView>("rgbSliders");
    EXPECT_EQ(rgb->row(0)->number(), 10);
    EXPECT_EQ(rgb->row(1)->number(), 200);
    EXPECT_EQ(rgb->row(2)->number(), 100);
    panel->state()->setColor(Qt::red);
    EXPECT_EQ(tools.color(), QColor(Qt::red));
}

TEST_F(ColorPanelTest, HsvSlidersReadTheExpectedNumbersInEachMode) {
    auto* hsv = child<HsvSlidersView>("hsvSliders");
    tools.setColor(QColor(128, 64, 32));
    EXPECT_EQ(hsv->row(0)->number(), 20);
    EXPECT_EQ(hsv->row(1)->number(), 75);
    EXPECT_EQ(hsv->row(2)->number(), 50);
    EXPECT_EQ(hsv->row(2)->letter(), "V");
    assets.setSliderMode(color::SliderMode::Hsl);
    EXPECT_EQ(hsv->row(1)->number(), 60);
    EXPECT_EQ(hsv->row(2)->number(), 31);
    EXPECT_EQ(hsv->row(2)->letter(), "L");
    tools.setColor(QColor(200, 200, 200));
    EXPECT_EQ(hsv->row(0)->number(), 20);
    EXPECT_EQ(hsv->row(1)->number(), 0);
}

TEST_F(ColorPanelTest, DraggingASliderSetsTheColour) {
    tools.setColor(Qt::black);
    auto* rgb = child<RgbSlidersView>("rgbSliders");
    ColorSliderRow* red = rgb->row(0);
    const QRect t = red->track();
    click(red, QPointF(t.right(), t.center().y()));
    EXPECT_EQ(tools.color(), QColor(255, 0, 0));
    auto* hsv = child<HsvSlidersView>("hsvSliders");
    ColorSliderRow* v = hsv->row(2);
    click(v, QPointF(v->track().left(), v->track().center().y()));
    EXPECT_EQ(tools.color(), QColor(Qt::black));
    EXPECT_EQ(hsv->row(0)->number(), 0);
    click(v, QPointF(v->track().right(), v->track().center().y()));
    EXPECT_EQ(tools.color(), QColor(255, 0, 0)) << "back to the same red";
}

TEST_F(ColorPanelTest, TheWheelsRingTurnsTheHueAndTheDiscSetsSAndValue) {
    auto* wheel = child<ColorWheelView>("colorWheel");
    wheel->resize(200, 200);
    tools.setColor(Qt::black);
    click(wheel, wheel->ringPoint(60.0));
    EXPECT_EQ(tools.color(), QColor(Qt::black));
    EXPECT_NEAR(panel->state()->hue(), 60.0, 2.0);
    click(wheel, wheel->discPoint(1.0, 1.0));
    const QColor c = tools.color();
    EXPECT_GE(c.red(), 250);
    EXPECT_GE(c.green(), 240);
    EXPECT_LE(c.blue(), 5);
    click(wheel, wheel->discPoint(0.0, 1.0));
    EXPECT_GE(tools.color().red(), 254);
    EXPECT_GE(tools.color().blue(), 254);
    click(wheel, wheel->centre());
    EXPECT_NEAR(QColor(tools.color()).valueF(), 0.5, 0.02);
    click(wheel, wheel->centre() + QPointF(0, wheel->discRadius() - 1));
    EXPECT_LE(tools.color().value(), 5);
    const QPointF red = wheel->ringPoint(0.0);
    EXPECT_LT(red.x(), wheel->centre().x());
    EXPECT_LT(red.y(), wheel->centre().y());
}

TEST_F(ColorPanelTest, TheDiscReachesEveryChromaAndValue) {
    auto* wheel = child<ColorWheelView>("colorWheel");
    wheel->resize(240, 240);
    for (double c : {0.0, 0.1, 0.5, 0.9, 1.0})
        for (double v : {0.0, 0.25, 0.5, 1.0}) {
            const QPointF at = wheel->discPoint(c, v);
            EXPECT_LE(std::hypot(at.x() - wheel->centre().x(), at.y() - wheel->centre().y()), wheel->discRadius() + 1e-9);
            const auto [c2, v2] = wheel->chromaValueAt(at);
            EXPECT_NEAR(c2, c, 1e-6) << c << " " << v;
            EXPECT_NEAR(v2, v, 1e-6) << c << " " << v;
        }
    EXPECT_LT(wheel->discRadius(), wheel->innerRadius()) << "a gap between the disc and the ring";
}

// Feedback: "The color wheel's inside circle looks strange, there is a clearly visible "X" in the middle." The disc
// shades smoothly, light at the top to black at the bottom and grey at the left to the full hue at the right.
TEST_F(ColorPanelTest, TheDiscShadesWithoutSeams) {
    for (int i = 1; i < kColorViewCount; ++i)
        panel->toggle(ColorView(i))->setChecked(false);
    QCoreApplication::processEvents();
    auto* wheel = child<ColorWheelView>("colorWheel");
    ASSERT_EQ(wheel->size(), QSize(ColorWheelView::kSize, ColorWheelView::kSize));
    const QColor red(206, 54, 37);  // the mocks' red; its knob is kept out of the measures below
    tools.setColor(red);
    const QImage shot = wheel->grab().toImage();
    EXPECT_TRUE(test::saveScreenshot(panel->grab(), QStringLiteral("color_wheel_disc")));
    const QPointF c = wheel->centre();
    const double r = wheel->discRadius();
    const QPointF knob = wheel->discPoint(red.hsvSaturationF() * red.valueF(), red.valueF());
    const auto at = [&](double u, double v) { return shot.pixelColor((c + QPointF(u, v) * r).toPoint()); };

    // Down the middle, light to black; across the middle, grey to the full hue.
    EXPECT_GT(at(0, -0.85).valueF(), 0.9);
    EXPECT_LT(at(0, 0.85).valueF(), 0.1);
    EXPECT_NEAR(at(0, 0).valueF(), 0.5, 0.04);
    EXPECT_LT(at(-0.85, 0).hsvSaturationF(), 0.2);
    EXPECT_GT(at(0.85, 0).hsvSaturationF(), 0.97);
    for (double t = -0.8; t < 0.8; t += 0.1) {
        EXPECT_GE(at(0, t).valueF(), at(0, t + 0.1).valueF()) << t;
        EXPECT_LE(at(t, 0).hsvSaturationF(), at(t + 0.1, 0).hsvSaturationF() + 0.01) << t;
    }

    // No seams: a seam is a sudden bend in the shading, a large second difference between pixels 3 apart, across the rows and
    // down the columns. The mocks' disc bends at most 6 levels so; the old disc bent 16 on its diagonals.
    constexpr int kStep = 3;
    int worst = 0;
    QPoint where;
    const double inside = 0.85 * r;
    for (int y = int(c.y() - inside); y <= int(c.y() + inside); ++y)
        for (int x = int(c.x() - inside); x <= int(c.x() + inside); ++x)
            for (const QPoint step : {QPoint(kStep, 0), QPoint(0, kStep)}) {
                const QPoint p(x, y);
                const auto in = [&](QPoint q) { return std::hypot(q.x() + 0.5 - c.x(), q.y() + 0.5 - c.y()) < inside; };
                if (!in(p - step) || !in(p + step) || std::hypot(p.x() - knob.x(), p.y() - knob.y()) < 14)
                    continue;
                const QColor a = shot.pixelColor(p - step), m = shot.pixelColor(p), b = shot.pixelColor(p + step);
                const int bend = std::max({std::abs(a.red() - 2 * m.red() + b.red()), std::abs(a.green() - 2 * m.green() + b.green()),
                                           std::abs(a.blue() - 2 * m.blue() + b.blue())});
                if (bend > worst)
                    worst = bend, where = p;
            }
    EXPECT_LE(worst, 8) << "at " << where.x() - c.x() << ", " << where.y() - c.y() << " from the centre";
}

// What the knob shows is what the disc shows under it, and picking there gives that colour.
TEST_F(ColorPanelTest, TheKnobSitsWhereTheDiscShowsItsColour) {
    auto* wheel = child<ColorWheelView>("colorWheel");
    for (const QColor colour : {QColor(206, 54, 37), QColor(40, 90, 200), QColor(250, 240, 200), QColor(60, 20, 10)}) {
        tools.setColor(colour);
        const QPointF knob = wheel->discPoint(colour.hsvSaturationF() * colour.valueF(), colour.valueF());
        const auto [chroma, value] = wheel->chromaValueAt(knob);
        EXPECT_TRUE(near(ColorWheelView::discColor(panel->state()->hue(), chroma, value), colour)) << colour.name().toStdString();
        click(wheel, knob);
        EXPECT_TRUE(near(tools.color(), colour)) << colour.name().toStdString();
    }
}

TEST_F(ColorPanelTest, TheSecondColourSitsBehindAndExchanges) {
    auto* first = panel->colorSwatch();
    auto* second = panel->secondarySwatch();
    ASSERT_NE(second, nullptr);
    tools.setColor(QColor(255, 0, 0));
    tools.setSecondaryColor(QColor(0, 0, 255));
    EXPECT_TRUE(second->styleSheet().contains(QStringLiteral("#0000ff")));
    EXPECT_GT(second->x(), first->x()) << "lower right of the brush colour";
    EXPECT_GT(second->y(), first->y());
    second->click();
    EXPECT_EQ(tools.color(), QColor(0, 0, 255));
    EXPECT_EQ(tools.secondaryColor(), QColor(255, 0, 0));
    EXPECT_TRUE(first->styleSheet().contains(QStringLiteral("#0000ff")));
}

TEST_F(ColorPanelTest, TheSmallButtonsExchangeAndSwitchToTheTransparentColour) {
    QToolButton* swap = panel->exchangeButton();
    QToolButton* clear = panel->transparentButton();
    ASSERT_NE(swap, nullptr);
    ASSERT_NE(clear, nullptr);
    tools.setColor(QColor(200, 0, 0));
    tools.setSecondaryColor(QColor(0, 200, 0));
    swap->click();
    EXPECT_EQ(tools.color(), QColor(0, 200, 0));
    EXPECT_EQ(tools.secondaryColor(), QColor(200, 0, 0));
    EXPECT_TRUE(panel->colorSwatch()->icon().isNull());

    clear->click();
    EXPECT_TRUE(tools.transparent());
    EXPECT_TRUE(clear->isChecked());
    EXPECT_FALSE(panel->colorSwatch()->icon().isNull()) << "the brush colour box shows the checkerboard";
    const QImage box = panel->colorSwatch()->grab().toImage();
    QSet<int> greys;
    for (int y = 6; y < box.height() - 6; ++y)
        for (int x = 6; x < box.width() - 6; ++x)
            greys.insert(qGray(box.pixel(x, y)) / 16);
    EXPECT_GE(greys.size(), 2) << "light and dark squares";
    EXPECT_EQ(tools.color(), QColor(0, 200, 0)) << "the colour behind it stays";
    clear->click();
    EXPECT_FALSE(tools.transparent());
    EXPECT_FALSE(clear->isChecked());
    EXPECT_TRUE(panel->colorSwatch()->icon().isNull());
    EXPECT_TRUE(panel->colorSwatch()->styleSheet().contains(QStringLiteral("#00c800")));

    clear->click();
    panel->state()->setColor(QColor(5, 6, 7));
    EXPECT_FALSE(tools.transparent());
    EXPECT_FALSE(clear->isChecked());
    clear->click();
    swap->click();
    EXPECT_FALSE(tools.transparent());
    clear->click();
    tools.setTransparent(false);
    EXPECT_FALSE(clear->isChecked());
}

TEST_F(ColorPanelTest, TheSmallButtonsSitBesideTheColourBoxes) {
    for (int i = 1; i < kColorViewCount; ++i)
        panel->toggle(ColorView(i))->setChecked(false);
    panel->resize(panel->preferredWidth(), panel->height());
    QCoreApplication::processEvents();
    const auto rect = [&](const QWidget* w) { return QRect(w->mapTo(panel.get(), QPoint(0, 0)), w->size()); };
    const QRect swap = rect(panel->exchangeButton()), clear = rect(panel->transparentButton());
    EXPECT_LT(swap.bottom(), clear.top());
    EXPECT_EQ(swap.left(), clear.left());
    EXPECT_TRUE(panel->rect().contains(swap)) << swap.x() << "," << swap.y() << " " << swap.width() << " in " << panel->width();
    EXPECT_TRUE(panel->rect().contains(clear));
    for (const QWidget* box : {static_cast<QWidget*>(panel->colorSwatch()), static_cast<QWidget*>(panel->secondarySwatch())}) {
        EXPECT_FALSE(swap.intersects(rect(box)));
        EXPECT_FALSE(clear.intersects(rect(box)));
    }
    EXPECT_GE(swap.left(), rect(child<ColorWheelView>("colorWheel")).right()) << "right of the circle";
}

TEST_F(ColorPanelTest, TheCircleAndItsColourBoxesClearTheLineUnderTheButtons) {
    const auto lineOf = [&](ColorView v) { return panel->view(v)->findChild<QFrame*>(); };
    const auto fromLine = [&](ColorView v, const QWidget* w) {
        const QWidget* line = lineOf(v);
        return w->mapTo(panel.get(), QPoint(0, 0)).y() - (line->mapTo(panel.get(), QPoint(0, 0)).y() + line->height());
    };
    auto* wheel = child<ColorWheelView>("colorWheel");
    EXPECT_GE(fromLine(ColorView::Circle, wheel), 14);
    EXPECT_GE(fromLine(ColorView::Circle, panel->colorSwatch()), 14) << "the brush colour at the corner";
    EXPECT_GE(fromLine(ColorView::Circle, panel->secondarySwatch()), 14);
    const int gap = fromLine(ColorView::Circle, wheel);
    EXPECT_LT(gap, 20) << "clear, not a wide empty band";
    const QWidget* rgb = child<QWidget>("rgbSliders");
    EXPECT_LT(fromLine(ColorView::Rgb, rgb), gap);
}

TEST_F(ColorPanelTest, TheCircleIsNotClippedAtTheSmallestHeight) {
    for (int i = 1; i < kColorViewCount; ++i)
        panel->toggle(ColorView(i))->setChecked(false);
    QCoreApplication::processEvents();
    panel->fitHeight();
    panel->resize(panel->preferredWidth(), panel->minimumHeight());
    QCoreApplication::processEvents();
    auto* wheel = child<ColorWheelView>("colorWheel");
    EXPECT_EQ(wheel->visibleRegion().boundingRect().size(), wheel->size()) << "all of the circle shows";
    EXPECT_EQ(panel->colorSwatch()->visibleRegion().boundingRect().size(), panel->colorSwatch()->size());
}

TEST_F(ColorPanelTest, TheCircleHasOneSizeAndNoGrip) {
    auto* wheel = child<ColorWheelView>("colorWheel");
    EXPECT_EQ(panel->findChild<QWidget*>(QStringLiteral("resizeGrip")), nullptr);
    EXPECT_EQ(wheel->sizeHint(), QSize(ColorWheelView::kSize, ColorWheelView::kSize));
    EXPECT_EQ(wheel->size(), wheel->sizeHint());
    EXPECT_GE(panel->preferredWidth(), ColorWheelView::kSize);
}

TEST_F(ColorPanelTest, SwatchesRegisterPickAndDelete) {
    auto* swatches = child<SwatchesView>("swatches");
    tools.setColor(QColor(0, 160, 0));
    std::unique_ptr<QMenu> menu(swatches->createCellMenu(1));
    ASSERT_EQ(menu->actions().size(), 1) << "an empty cell offers Register Color only";
    menu->actions()[0]->trigger();
    EXPECT_EQ(assets.currentSet().cells[1], QColor(0, 160, 0));
    tools.setColor(Qt::yellow);
    click(swatches->grid(), swatches->grid()->cellRect(1).center());
    EXPECT_EQ(tools.color(), QColor(0, 160, 0));
    std::unique_ptr<QMenu> filled(swatches->createCellMenu(1));
    ASSERT_EQ(filled->actions().size(), 2);
    filled->actions()[1]->trigger();
    EXPECT_FALSE(assets.currentSet().cells[1]);

    assets.addToHistory(Qt::magenta);
    tools.setColor(Qt::black);
    click(swatches->history(), swatches->history()->cellRect(0).center());
    EXPECT_EQ(tools.color(), QColor(Qt::magenta));
    assets.setSwatchSize(SwatchSize::Small);
    EXPECT_EQ(swatches->grid()->cellSize(), swatchCellPixels(SwatchSize::Small));
}

TEST(ColorPanelWindow, TogglesAreTheWindowMenuEntriesAndStrokesFillTheHistory) {
    QStandardPaths::setTestModeEnabled(true);
    test::applyTheme();
    MainWindow window;
    auto* panel = window.findChild<ColorPanel*>();
    ASSERT_TRUE(panel);
    QAction* swatches = window.findChild<QAction*>(QStringLiteral("window.show.swatches"));
    ASSERT_TRUE(swatches);
    EXPECT_EQ(panel->toggle(ColorView::Swatches)->defaultAction(), swatches);
    EXPECT_FALSE(swatches->isChecked());
    panel->toggle(ColorView::Swatches)->click();
    EXPECT_TRUE(swatches->isChecked());
    EXPECT_TRUE(panel->view(ColorView::Swatches)->isVisibleTo(&window));
    swatches->trigger();
    EXPECT_FALSE(panel->toggle(ColorView::Swatches)->isChecked());
    window.findChild<QAction*>(QStringLiteral("window.hsv.hsl"))->trigger();
    auto* hsv = panel->findChild<HsvSlidersView*>(QStringLiteral("hsvSliders"));
    ASSERT_TRUE(hsv);
    EXPECT_EQ(hsv->row(2)->letter(), "L");

    window.newDocument();
    CanvasView* canvas = window.tabs()->currentCanvas();
    ASSERT_TRUE(canvas);
    canvas->resize(300, 300);
    window.tools()->setColor(QColor(12, 34, 56));
    drag(canvas, QPointF(100, 100), QPointF(160, 120));
    auto* assets = window.findChild<ColorAssets*>();
    ASSERT_TRUE(assets);
    ASSERT_FALSE(assets->history().empty());
    EXPECT_EQ(assets->history().front(), QColor(12, 34, 56));
}

TEST(ColorPanelWindow, TheMinusKeyTogglesTheTransparentColourAndStrokesEraseWithIt) {
    QStandardPaths::setTestModeEnabled(true);
    test::applyTheme();
    MainWindow window;
    window.newDocument();
    CanvasView* canvas = window.tabs()->currentCanvas();
    ASSERT_TRUE(canvas);
    canvas->resize(300, 300);
    auto* assets = window.findChild<ColorAssets*>();
    ASSERT_TRUE(assets);
    const auto inked = [&] {
        const TiledImage& pixels = canvas->document()->currentLayer()->pixels;
        int n = 0;
        for (int y = 0; y < pixels.size().height(); ++y)
            for (int x = 0; x < pixels.size().width(); ++x)
                n += pixels.pixel(x, y)[3] > kChannelOne / 2;
        return n;
    };
    window.tools()->setColor(QColor(12, 34, 56));
    drag(canvas, QPointF(100, 100), QPointF(200, 120));
    const int painted = inked();
    ASSERT_GT(painted, 100);
    const std::size_t history = assets->history().size();

    QAction* key = window.findChild<QAction*>(QStringLiteral("color.transparent"));
    ASSERT_TRUE(key);
    EXPECT_EQ(key->shortcuts(), QList<QKeySequence>{QKeySequence(Qt::Key_Minus)});
    key->trigger();
    EXPECT_TRUE(window.tools()->transparent());
    drag(canvas, QPointF(100, 100), QPointF(200, 120));
    EXPECT_LT(inked(), painted / 4) << "the stroke took the pixels away, of " << painted;
    EXPECT_EQ(assets->history().size(), history) << "transparent is not a history colour";
    key->trigger();
    EXPECT_FALSE(window.tools()->transparent());
    EXPECT_EQ(window.tools()->paintColor(), QColor(12, 34, 56)) << "back to the previous colour";
}

TEST_F(ColorPanelTest, Screenshot) {
    tools.setColor(QColor(200, 60, 40));
    assets.addToHistory(Qt::black);
    assets.addToHistory(QColor(200, 60, 40));
    assets.setSwatch(0, Qt::white);
    QCoreApplication::processEvents();
    EXPECT_TRUE(test::saveScreenshot(panel->grab(), QStringLiteral("color_panel_all_views")));
}

TEST_F(ColorPanelTest, ScreenshotOfTheColourBox) {
    for (int i = 1; i < kColorViewCount; ++i)
        panel->toggle(ColorView(i))->setChecked(false);
    tools.setColor(QColor(60, 170, 70));
    tools.setSecondaryColor(QColor(230, 230, 235));
    QCoreApplication::processEvents();
    panel->fitHeight();
    panel->resize(panel->preferredWidth(), panel->preferredHeight());
    QCoreApplication::processEvents();
    EXPECT_TRUE(test::saveScreenshot(panel->grab(), QStringLiteral("color_box_buttons")));
    tools.setTransparent(true);
    QCoreApplication::processEvents();
    EXPECT_TRUE(test::saveScreenshot(panel->grab(), QStringLiteral("color_box_transparent")));
}

namespace {

struct CardLayout {
    int heading = 0, tabs = 0, divider = 0, wheel = 0;
    bool operator==(const CardLayout&) const = default;
};

class ColorCardTest : public testing::Test {
protected:
    void SetUp() override {
        QStandardPaths::setTestModeEnabled(true);
        test::applyTheme();
        window = std::make_unique<MainWindow>();
        window->resize(1400, 900);
        window->show();
        settle();
    }
    void settle() {
        for (int i = 0; i < 5; ++i) {
            QCoreApplication::processEvents();
            QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
        }
    }
    QDockWidget* dock() const { return window->findChild<QDockWidget*>(QStringLiteral("colorDock")); }
    ColorPanel* panel() const { return window->findChild<ColorPanel*>(); }
    CardLayout layout() const {
        const QWidget* card = dock()->widget();
        const auto y = [&](const QWidget* w) { return w->mapTo(card, QPoint(0, 0)).y(); };
        const QWidget* circle = panel()->view(ColorView::Circle);
        const QWidget* first = nullptr;
        for (int i = 0; i < kColorViewCount && !first; ++i)
            if (panel()->view(ColorView(i))->isVisibleTo(card))
                first = panel()->view(ColorView(i));
        CardLayout l;
        l.heading = y(card->findChild<QLabel*>(QStringLiteral("cardTitle")));
        l.tabs = y(panel()->toggle(ColorView::Circle));
        l.divider = first ? y(first->findChild<QFrame*>()) : -1;
        l.wheel = circle->isVisibleTo(card) ? y(circle->findChild<ColorWheelView*>()) : -1;
        return l;
    }
    std::unique_ptr<MainWindow> window;
};

}

TEST_F(ColorCardTest, TheHeaderStaysPutAcrossTogglesAndDockResizes) {
    const CardLayout start = layout();
    const auto expectSame = [&](const char* when) {
        settle();
        const CardLayout now = layout();
        EXPECT_EQ(now.heading, start.heading) << when;
        EXPECT_EQ(now.tabs, start.tabs) << when;
        if (now.divider >= 0) {
            EXPECT_EQ(now.divider, start.divider) << when;
        }
        if (now.wheel >= 0) {
            EXPECT_EQ(now.wheel, start.wheel) << when;
        }
    };
    for (int i = 0; i < kColorViewCount; ++i) {
        QToolButton* b = panel()->toggle(ColorView(i));
        b->click();
        expectSame("after one toggle");
        b->click();
        expectSame("after toggling back");
    }
    for (int i = 1; i < kColorViewCount; ++i)
        panel()->toggle(ColorView(i))->click();
    expectSame("circle alone");
    for (int i = 1; i < kColorViewCount; ++i)
        panel()->toggle(ColorView(i))->click();
    expectSame("all views");
    for (const int h : {320, 700, 450, 800}) {
        window->resizeDocks({dock()}, {h}, Qt::Vertical);
        expectSame("after a dock resize");
    }
    EXPECT_TRUE(test::saveScreenshot(dock()->widget()->grab(), QStringLiteral("color_card")));
}

namespace {

struct DockSizes {
    QSize navigator, color, tools, layers, wheel;
    bool operator==(const DockSizes&) const = default;
};

std::ostream& operator<<(std::ostream& os, const DockSizes& s) {
    const auto put = [&](const char* name, const QSize& z) { os << name << ' ' << z.width() << 'x' << z.height() << ' '; };
    put("navigator", s.navigator), put("color", s.color), put("tools", s.tools), put("layers", s.layers), put("wheel", s.wheel);
    return os;
}

}

class PanelSizesTest : public ColorCardTest {
protected:
    DockSizes sizes() const {
        const auto of = [this](const char* name) { return window->findChild<QDockWidget*>(QString::fromLatin1(name))->size(); };
        return {of("navigatorDock"), of("colorDock"), of("toolsDock"), of("layersDock"),
                panel()->view(ColorView::Circle)->findChild<ColorWheelView*>()->size()};
    }
    void expectTogglesGiveBack();
};

TEST_F(PanelSizesTest, AColorViewOnAndOffAgainLeavesThePanelsAsTheyWere) {
    // A window tall enough for every view, as the 1440 px screen gives: a shorter one grows to hold the views that are
    // shown (the card is never smaller than its views) and stays grown once they are hidden again.
    window->resize(1400, 1350);
    settle();
    for (const int dragged : {0, 420, 330}) {
        if (dragged > 0) {
            window->resizeDocks({dock()}, {dragged}, Qt::Vertical);
            settle();
            QMouseEvent release(QEvent::MouseButtonRelease, QPointF(5, 5), QPointF(5, 5), Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
            QCoreApplication::sendEvent(window.get(), &release);
        }
        settle();
        expectTogglesGiveBack();
    }
}

void PanelSizesTest::expectTogglesGiveBack() {
    const DockSizes start = sizes();
    for (int i = 0; i < kColorViewCount; ++i) {
        if (ColorView(i) == ColorView::Circle)
            continue;
        QToolButton* b = panel()->toggle(ColorView(i));
        const bool wasOn = b->isChecked();
        b->click();
        settle();
        b->click();
        settle();
        EXPECT_EQ(b->isChecked(), wasOn);
        EXPECT_EQ(sizes(), start) << "after toggling view " << i << " twice, colour dock " << start.color.height();
    }
}

TEST_F(PanelSizesTest, MovingTheColumnsToTheOtherSideKeepsTheirSizes) {
    auto* navigator = window->findChild<QDockWidget*>(QStringLiteral("navigatorDock"));
    auto* layers = window->findChild<QDockWidget*>(QStringLiteral("layersDock"));
    auto* tools = window->findChild<QDockWidget*>(QStringLiteral("toolsDock"));
    window->resizeDocks({navigator, dock()}, {300, 290}, Qt::Horizontal);
    window->resizeDocks({navigator, layers}, {260, 500}, Qt::Vertical);
    window->resizeDocks({tools}, {420}, Qt::Vertical);
    settle();
    const DockSizes start = sizes();
    for (const char* id : {"window.colorToolRight", "window.navigatorLayerRight", "window.colorToolRight", "window.navigatorLayerRight"}) {
        window->command(QString::fromLatin1(id))->trigger();
        settle();
        EXPECT_EQ(sizes(), start) << "after " << id;
    }
}

TEST_F(PanelSizesTest, TheColorDockIsAsTallAsItsViews) {
    window->resize(1400, 1300);
    settle();
    const auto content = [&] { return panel()->preferredHeight() + static_cast<PanelCard*>(dock()->widget())->chrome().height(); };
    const auto expectHugs = [&](const char* when) {
        settle();
        EXPECT_NEAR(dock()->height(), content(), 2) << when;
    };
    const auto show = [&](std::initializer_list<ColorView> views) {
        for (int i = 0; i < kColorViewCount; ++i) {
            QToolButton* b = panel()->toggle(ColorView(i));
            const bool want = std::ranges::find(views, ColorView(i)) != views.end();
            if (b->isChecked() != want)
                b->click();
        }
        settle();
    };
    expectHugs("the circle alone, as it starts");
    EXPECT_TRUE(test::saveScreenshot(window->grab(), QStringLiteral("color_dock_circle_only")));
    show({ColorView::Circle, ColorView::Rgb, ColorView::Hsv, ColorView::Swatches});
    expectHugs("circle, sliders and swatches");
    EXPECT_TRUE(test::saveScreenshot(window->grab(), QStringLiteral("color_dock_circle_sliders_swatches")));
    show({ColorView::Swatches});
    expectHugs("swatches alone");
    show({ColorView::Rgb});
    expectHugs("one slider view");
    show({});
    expectHugs("no view");
    show({ColorView::Circle});
    expectHugs("the circle again");
}

// Feedback: "When pressing the color wheel additional color modes, do not show a scrollbar, the panel should grow."
TEST_F(PanelSizesTest, MoreColorViewsGrowThePanelInsteadOfScrolling) {
    auto* views = panel()->findChild<QScrollArea*>(QStringLiteral("colorViewsScroll"));
    auto* tools = window->findChild<QDockWidget*>(QStringLiteral("toolsDock"));
    const auto content = [&] { return panel()->preferredHeight() + static_cast<PanelCard*>(dock()->widget())->chrome().height(); };
    const auto showOnly = [&](std::initializer_list<ColorView> shown) {
        for (int i = 0; i < kColorViewCount; ++i)
            if (panel()->toggle(ColorView(i))->isChecked() != (std::ranges::find(shown, ColorView(i)) != shown.end()))
                panel()->toggle(ColorView(i))->click();
        settle();
    };
    // The screen is 1440 px tall: every view at once fits under the window's title and menus.
    const std::initializer_list<ColorView> growing[] = {
        {ColorView::Circle, ColorView::Rgb},
        {ColorView::Circle, ColorView::Rgb, ColorView::Hsv},
        {ColorView::Circle, ColorView::Rgb, ColorView::Hsv, ColorView::Swatches},
    };
    for (const int height : {1350, 1000}) {
        window->resize(1400, height);
        showOnly({ColorView::Circle});
        for (const auto& shown : growing) {
            showOnly(shown);
            const int count = int(shown.size());
            const bool fits = content() + tools->minimumSizeHint().height() <= dock()->height() + tools->height();
            EXPECT_TRUE(fits || height < 1350) << count << " views";
            EXPECT_TRUE(fits || count > 3) << "two slider views fit in a window 1000 px tall";
            if (!fits)
                break;  // the column can't hold them: below
            EXPECT_FALSE(views->verticalScrollBar()->isVisible()) << "window " << height << ", " << count << " views";
            EXPECT_NEAR(dock()->height(), content(), 2) << "window " << height << ", " << count << " views";
            if (count == kColorViewCount) {
                EXPECT_TRUE(test::saveScreenshot(window->grab(), QStringLiteral("color_dock_grown")));
            }
            EXPECT_LE(views->widget()->width(), views->viewport()->width()) << "nothing cut off at the right, " << count << " views";
        }
    }

    // A column too short for the views and the tool panel: the tool panel gives up all it can, and the card still keeps its
    // views' height (no scroll bar): the window grows to hold them if it must.
    window->resize(1400, 900);
    showOnly({ColorView::Circle});
    showOnly({ColorView::Circle, ColorView::Rgb, ColorView::Hsv});
    EXPECT_NEAR(dock()->height(), content(), 2);
    EXPECT_FALSE(views->verticalScrollBar()->isVisible());
    showOnly({ColorView::Circle});
}

namespace {

// Counts the times a widget is shown.
class ShowCounter : public QObject {
public:
    int shown = 0;

protected:
    bool eventFilter(QObject* watched, QEvent* event) override {
        if (event->type() == QEvent::Show)
            ++shown;
        return QObject::eventFilter(watched, event);
    }
};

}

// Feedback: "When clicking the color wheel's other color options, you can momentarily see a scrollbar that then disappears."
// The scroll bar isn't shown even for the moment the dock takes to grow; where the column can't hold the views it still shows.
TEST_F(PanelSizesTest, NoScrollBarFlashesWhileThePanelGrows) {
    auto* views = panel()->findChild<QScrollArea*>(QStringLiteral("colorViewsScroll"));
    ShowCounter counter;
    views->verticalScrollBar()->installEventFilter(&counter);
    window->resize(1400, 1350);
    settle();
    for (const ColorView v : {ColorView::Rgb, ColorView::Hsv, ColorView::Swatches}) {
        panel()->toggle(v)->click();
        settle();
        EXPECT_EQ(counter.shown, 0) << "view " << int(v);
        EXPECT_FALSE(views->verticalScrollBar()->isVisible()) << "view " << int(v);
    }
    for (const ColorView v : {ColorView::Swatches, ColorView::Hsv, ColorView::Rgb}) {
        panel()->toggle(v)->click();
        settle();
    }
    EXPECT_EQ(counter.shown, 0) << "nor while it shrinks";

    // Never a scroll bar, even in a window too short for the views: the card stays as tall as its views.
    window->resize(1400, 700);
    settle();
    for (const ColorView v : {ColorView::Rgb, ColorView::Hsv, ColorView::Swatches})
        panel()->toggle(v)->click();
    settle();
    EXPECT_FALSE(views->verticalScrollBar()->isVisible());
    EXPECT_GE(panel()->height(), panel()->preferredHeight()) << "nothing cut off";
    for (const ColorView v : {ColorView::Rgb, ColorView::Hsv, ColorView::Swatches})
        panel()->toggle(v)->click();
    settle();
}

TEST_F(PanelSizesTest, DraggingTheColorDockTallerLeavesItAsTallAsItsViews) {
    const int hugging = dock()->height();
    for (const int h : {hugging + 40, 600, 880}) {
        window->resizeDocks({dock()}, {h}, Qt::Vertical);
        settle();
        QMouseEvent release(QEvent::MouseButtonRelease, QPointF(5, 5), QPointF(5, 5), Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
        QCoreApplication::sendEvent(window.get(), &release);
        settle();
        EXPECT_NEAR(dock()->height(), hugging, 2) << "asked for " << h;
    }
    panel()->toggle(ColorView::Rgb)->click();
    panel()->toggle(ColorView::Hsv)->click();
    panel()->toggle(ColorView::Swatches)->click();
    settle();
    const int tall = dock()->height();
    window->resizeDocks({dock()}, {tall - 60}, Qt::Vertical);
    settle();
    EXPECT_EQ(dock()->height(), tall) << "never smaller than its views, so they never scroll";
}

TEST_F(PanelSizesTest, ASeparatedColorPanelFitsItsViews) {
    dock()->setFloating(true);
    settle();
    EXPECT_NEAR(dock()->height(), dock()->sizeHint().height(), 2);
    const int circle = dock()->height();
    panel()->toggle(ColorView::Rgb)->click();
    settle();
    EXPECT_GT(dock()->height(), circle);
    EXPECT_NEAR(dock()->height(), dock()->sizeHint().height(), 2);
    panel()->toggle(ColorView::Rgb)->click();
    settle();
    EXPECT_NEAR(dock()->height(), circle, 2);
}
