#include "ui/GlassPopups.h"
#include "ui/Theme.h"

#include <QApplication>
#include <QDialog>
#include <QLabel>
#include <QMenu>
#include <QPushButton>
#include <QWindow>

#include <gtest/gtest.h>

#include <vector>

using namespace doodlebold;

namespace {

struct Request {
    QWindow* window;
    QRegion region;
};

class GlassPopupsTest : public ::testing::Test {
protected:
    void SetUp() override {
        glass = new GlassPopups([this](QWindow* w, const QRegion& r) {
            requests.push_back({w, r});
            return true;
        });
        qApp->installEventFilter(glass);
    }
    void TearDown() override { delete glass; }

    GlassPopups* glass = nullptr;
    std::vector<Request> requests;
};

}

TEST(GlassPopupsKinds, MenusToolTipsAndDialogsOnly) {
    QMenu menu;
    QDialog dialog;
    QLabel tip;
    tip.setObjectName(QStringLiteral("qtooltip_label"));
    QLabel label;
    QPushButton button;
    EXPECT_TRUE(GlassPopups::isGlassKind(&menu));
    EXPECT_TRUE(GlassPopups::isGlassKind(&tip));
    EXPECT_TRUE(GlassPopups::isGlassKind(&dialog));
    EXPECT_FALSE(GlassPopups::isGlassKind(&label));
    EXPECT_FALSE(GlassPopups::isGlassKind(&button));
    EXPECT_FALSE(GlassPopups::isGlassKind(nullptr));
}

TEST(GlassPopupsShape, RoundedCornersLeaveTheCornerPixelsOut) {
    const QRegion r = GlassPopups::shape(QSize(100, 40), 8);
    EXPECT_FALSE(r.contains(QPoint(0, 0)));
    EXPECT_FALSE(r.contains(QPoint(99, 39)));
    EXPECT_TRUE(r.contains(QPoint(50, 20)));
    EXPECT_TRUE(r.contains(QPoint(50, 0))) << "the straight edges are kept";
    EXPECT_TRUE(r.contains(QPoint(0, 20)));
    EXPECT_EQ(GlassPopups::shape(QSize(100, 40), 0).boundingRect(), QRect(0, 0, 100, 40));
}

TEST_F(GlassPopupsTest, AMenuTurnsTranslucentWhenPolishedAndAsksForBlurWhenShown) {
    QMenu menu;
    menu.addAction(QStringLiteral("Open"));
    menu.addAction(QStringLiteral("Save"));
    menu.ensurePolished();
    EXPECT_TRUE(menu.testAttribute(Qt::WA_TranslucentBackground));
    EXPECT_TRUE(menu.property("glass").toBool());
    EXPECT_FALSE(menu.testAttribute(Qt::WA_WState_Created)) << "polished before its window exists: it gets alpha";

    menu.popup(QPoint(10, 10));
    QApplication::processEvents();
    ASSERT_FALSE(requests.empty());
    const Request& last = requests.back();
    EXPECT_EQ(last.window, menu.windowHandle());
    EXPECT_EQ(last.region.boundingRect(), menu.rect());
    EXPECT_FALSE(last.region.contains(QPoint(0, 0))) << "the menu's rounded corner";
    menu.hide();
}

TEST_F(GlassPopupsTest, OtherWidgetsAreLeftAlone) {
    QPushButton button(QStringLiteral("OK"));
    button.ensurePolished();
    button.show();
    QApplication::processEvents();
    EXPECT_FALSE(button.testAttribute(Qt::WA_TranslucentBackground));
    EXPECT_FALSE(button.property("glass").isValid());
    EXPECT_TRUE(requests.empty());
}

TEST_F(GlassPopupsTest, TheStyleSheetMakesGlassMenusTranslucent) {
    Theme::apply(*qobject_cast<QApplication*>(QCoreApplication::instance()), Theme::Source::BuiltIn);
    QMenu glassMenu;
    glassMenu.addAction(QStringLiteral("Open"));
    glassMenu.resize(120, 40);
    const QImage frosted = glassMenu.grab().toImage();
    const QColor centre = frosted.pixelColor(60, 2);
    EXPECT_LT(centre.alpha(), 255) << "see-through";
    EXPECT_GT(centre.alpha(), 128) << "but mostly the menu";

    qApp->removeEventFilter(glass);
    QMenu plainMenu;
    plainMenu.addAction(QStringLiteral("Open"));
    plainMenu.resize(120, 40);
    EXPECT_EQ(plainMenu.grab().toImage().pixelColor(60, 2).alpha(), 255);
}

// Options > Performance > Frosted menus: off, a menu not on screen turns plain at once, one on screen once it closes.
TEST_F(GlassPopupsTest, FrostedMenusOffMakesMenusPlainAndOnMakesThemGlassAgain) {
    QMenu shut;
    shut.addAction(QStringLiteral("Open"));
    shut.ensurePolished();
    ASSERT_TRUE(shut.property("glass").toBool());
    QMenu open;
    open.addAction(QStringLiteral("Save"));
    open.popup(QPoint(10, 10));
    QApplication::processEvents();
    ASSERT_TRUE(open.property("glass").toBool());

    GlassPopups::setFrosted(false);
    EXPECT_FALSE(GlassPopups::frosted());
    EXPECT_FALSE(shut.property("glass").toBool());
    EXPECT_FALSE(shut.testAttribute(Qt::WA_TranslucentBackground));
    EXPECT_TRUE(open.property("glass").toBool()) << "left alone while on screen";
    open.hide();
    QApplication::processEvents();
    EXPECT_FALSE(open.property("glass").toBool()) << "plain once closed";

    QMenu made;
    made.ensurePolished();
    EXPECT_FALSE(made.property("glass").toBool()) << "a new menu starts plain";
    const std::size_t asked = requests.size();
    made.addAction(QStringLiteral("Close"));
    made.popup(QPoint(10, 10));
    QApplication::processEvents();
    EXPECT_EQ(requests.size(), asked) << "no blur is asked for";
    made.hide();

    GlassPopups::setFrosted(true);
    EXPECT_TRUE(shut.property("glass").toBool());
    EXPECT_TRUE(shut.testAttribute(Qt::WA_TranslucentBackground));
    EXPECT_TRUE(made.property("glass").toBool());
}
