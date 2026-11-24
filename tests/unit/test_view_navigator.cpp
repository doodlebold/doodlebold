#include "core/view/ViewNavigator.h"

#include <gtest/gtest.h>

#include <cmath>
#include <vector>

using namespace doodlebold;

namespace {

constexpr double kEps = 1e-9;

void expectNear(const QPointF& a, const QPointF& b) {
    EXPECT_NEAR(a.x(), b.x(), 1e-6);
    EXPECT_NEAR(a.y(), b.y(), 1e-6);
}

}

TEST(ViewNavigator, ZoomKeepsAnchorAndClamps) {
    ViewState v;
    ViewNavigator nav(v, QSizeF(800, 600), QSizeF(2000, 1500));
    const QPointF anchor(123, 456);
    const QPointF docUnder = nav.widgetToDoc().map(anchor);
    nav.zoomBy(3.0, anchor);
    EXPECT_NEAR(v.zoom, 3.0, kEps);
    expectNear(nav.docToWidget().map(docUnder), anchor);
    nav.zoomTo(1000.0, anchor);
    EXPECT_DOUBLE_EQ(v.zoom, ViewNavigator::kMaxZoom);
    nav.zoomTo(0.0, anchor);
    EXPECT_DOUBLE_EQ(v.zoom, ViewNavigator::kMinZoom);
}

TEST(ViewNavigator, RotationKeepsCentreAndNormalises) {
    ViewState v;
    v.pan = QPointF(40, -25);
    ViewNavigator nav(v, QSizeF(800, 600), QSizeF(1000, 1000));
    const QPointF docAtCentre = nav.widgetToDoc().map(nav.centre());
    nav.rotateTo(200.0, nav.centre());
    EXPECT_NEAR(v.rotationDeg, -160.0, kEps);
    expectNear(nav.docToWidget().map(docAtCentre), nav.centre());
}

TEST(ViewNavigator, FlipMirrorsOnScreenAndTwiceIsIdentity) {
    ViewState v;
    v.rotationDeg = 30.0;
    v.zoom = 0.5;
    ViewNavigator nav(v, QSizeF(800, 600), QSizeF(1000, 800));
    const QPointF c = nav.centre();
    const QPointF docPoint(700, 300);
    const QPointF before = nav.docToWidget().map(docPoint);
    nav.flip(c);
    const QPointF after = nav.docToWidget().map(docPoint);
    EXPECT_NEAR(after.x(), 2 * c.x() - before.x(), 1e-6);
    EXPECT_NEAR(after.y(), before.y(), 1e-6);
    nav.flip(c);
    expectNear(nav.docToWidget().map(docPoint), before);
    EXPECT_FALSE(v.flipH);
}

TEST(ViewNavigator, FitShowsWholeRotatedCanvas) {
    ViewState v;
    v.rotationDeg = 45.0;
    v.pan = QPointF(300, 300);
    ViewNavigator nav(v, QSizeF(800, 600), QSizeF(2000, 1000));
    nav.fit();
    const QRectF shown = nav.docToWidget().mapRect(QRectF(0, 0, 2000, 1000));
    EXPECT_TRUE(QRectF(0, 0, 800, 600).contains(shown));
    EXPECT_TRUE(v.pan.isNull());
}

TEST(ViewNavigator, FitLeavesAMargin) {
    ViewState v;
    ViewNavigator nav(v, QSizeF(1160, 850), QSizeF(400, 300));
    nav.fit();
    EXPECT_DOUBLE_EQ(v.zoom, 2.8);
}

TEST(ViewNavigator, CentreOnAndVisibleArea) {
    ViewState v;
    ViewNavigator nav(v, QSizeF(400, 300), QSizeF(1000, 1000));
    nav.centreOn(QPointF(100, 900));
    expectNear(nav.widgetToDoc().map(nav.centre()), QPointF(100, 900));
    const QRectF area = nav.visibleDocumentArea().boundingRect();
    EXPECT_NEAR(area.width(), 400.0, 1e-6);
    EXPECT_NEAR(area.center().x(), 100.0, 1e-6);
}

// The reference's zoom-button levels: the buttons visit every one, up from 1/128 and down from 3200 %, and snap from between.
TEST(ViewNavigator, ZoomButtonsVisitEveryLevelOfTheReferencesLadderAndSnapOntoIt) {
    const std::vector<double> levels = {1.0 / 128, 1.0 / 96, 1.0 / 64, 1.0 / 48, 1.0 / 32, 1.0 / 24, 1.0 / 16, 1.0 / 12, 1.0 / 8, 1.0 / 6,
                                        1.0 / 4,   1.0 / 3,  1.0 / 2,  0.75,     1.0,      1.5,      2.0,      3.0,      4.0,     5.0,
                                        6.0,       8.0,      10.0,     12.0,     16.0,     20.0,     26.0,     32.0};
    double z = ViewNavigator::kMinZoom;
    EXPECT_DOUBLE_EQ(z, levels.front());
    for (std::size_t i = 1; i < levels.size(); ++i) {
        z = ViewNavigator::nextLadderZoom(z, 1);
        EXPECT_DOUBLE_EQ(z, levels[i]) << i;
    }
    EXPECT_DOUBLE_EQ(ViewNavigator::nextLadderZoom(z, 1), ViewNavigator::kMaxZoom);
    for (std::size_t i = levels.size() - 1; i-- > 0;) {
        z = ViewNavigator::nextLadderZoom(z, -1);
        EXPECT_DOUBLE_EQ(z, levels[i]) << i;
    }
    EXPECT_DOUBLE_EQ(ViewNavigator::nextLadderZoom(z, -1), ViewNavigator::kMinZoom);
    EXPECT_DOUBLE_EQ(ViewNavigator::nextLadderZoom(0.594, 1), 0.75);
    EXPECT_DOUBLE_EQ(ViewNavigator::nextLadderZoom(0.594, -1), 0.5);
    EXPECT_DOUBLE_EQ(ViewNavigator::nextLadderZoom(2.2, 1), 3.0);
    EXPECT_DOUBLE_EQ(ViewNavigator::nextLadderZoom(2.2, -1), 2.0);
}

// A wheel notch goes to the next level of 2^(k/4): four from 100 % land on exactly 200 %, the levels between are 2^(1/4),
// 2^(2/4) and 2^(3/4), a zoom between levels snaps onto the next, and the limits hold.
TEST(ViewNavigator, FourWheelNotchesFromOneHundredPercentLandOnExactlyTwoHundred) {
    double z = 1.0;
    const double between[] = {std::exp2(0.25), std::exp2(0.5), std::exp2(0.75)};
    for (int i = 0; i < 3; ++i) {
        z = ViewNavigator::nextWheelZoom(z, 1);
        EXPECT_DOUBLE_EQ(z, between[i]) << i;
    }
    z = ViewNavigator::nextWheelZoom(z, 1);
    EXPECT_EQ(z, 2.0);
    for (int i = 0; i < 4; ++i)
        z = ViewNavigator::nextWheelZoom(z, -1);
    EXPECT_EQ(z, 1.0);
    EXPECT_DOUBLE_EQ(ViewNavigator::nextWheelZoom(1.3, 1), std::exp2(0.5));
    EXPECT_DOUBLE_EQ(ViewNavigator::nextWheelZoom(1.3, -1), std::exp2(0.25));
    EXPECT_DOUBLE_EQ(ViewNavigator::nextWheelZoom(0.75, 1), std::exp2(-0.25));
    EXPECT_DOUBLE_EQ(ViewNavigator::nextWheelZoom(32.0, 1), ViewNavigator::kMaxZoom);
    EXPECT_DOUBLE_EQ(ViewNavigator::nextWheelZoom(1.0 / 128, -1), ViewNavigator::kMinZoom);
}
