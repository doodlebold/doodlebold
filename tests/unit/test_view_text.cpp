#include "core/view/ViewNavigator.h"
#include "core/view/ViewText.h"

#include <gtest/gtest.h>

using namespace doodlebold;
using namespace doodlebold::view_text;

TEST(ViewText, ZoomIsRounded) {
    EXPECT_EQ(zoomText(1.1892), QStringLiteral("119%"));
    EXPECT_EQ(zoomText(1.0 / 6.0), QStringLiteral("16.7%"));
    EXPECT_EQ(zoomText(1.0 / 24.0), QStringLiteral("4.17%"));
    EXPECT_EQ(zoomText(1.0), QStringLiteral("100%"));
    EXPECT_EQ(zoomText(0.5), QStringLiteral("50%"));
    EXPECT_EQ(zoomText(32.0), QStringLiteral("3200%"));
}

TEST(ViewText, AngleHasOneDecimal) {
    EXPECT_EQ(angleText(0.0), QStringLiteral("0.0°"));
    EXPECT_EQ(angleText(11.25), QStringLiteral("11.3°"));
    EXPECT_EQ(angleText(33.75), QStringLiteral("33.8°"));
    EXPECT_EQ(angleText(-11.25), QStringLiteral("-11.3°"));
    EXPECT_EQ(angleText(-168.75), QStringLiteral("-168.8°"));
    EXPECT_EQ(angleText(45.0), QStringLiteral("45.0°"));
    EXPECT_EQ(angleText(-180.0), QStringLiteral("180.0°"));
    EXPECT_EQ(angleText(180.0), QStringLiteral("180.0°"));
    EXPECT_EQ(angleText(-0.0), QStringLiteral("0.0°"));
}

TEST(ViewText, AngleListEntriesArePaddedToThreeDigits) {
    EXPECT_EQ(angleListText(-180), QStringLiteral("-180°"));
    EXPECT_EQ(angleListText(-90), QStringLiteral("-90°"));
    EXPECT_EQ(angleListText(-45), QStringLiteral("-45°"));
    EXPECT_EQ(angleListText(0), QStringLiteral("0°"));
    EXPECT_EQ(angleListText(45), QStringLiteral("45°"));
    EXPECT_EQ(angleListText(135), QStringLiteral("135°"));
    EXPECT_EQ(angleListText(180), QStringLiteral("180°"));
}

TEST(ViewNavigatorHome, ResetsPositionZoomAngleAndFlip) {
    ViewState v;
    v.zoom = 3.0;
    v.rotationDeg = 45.0;
    v.flipH = true;
    v.pan = QPointF(120, -40);
    ViewNavigator nav(v, QSizeF(800, 600), QSizeF(2000, 1000));
    nav.home(false);
    EXPECT_DOUBLE_EQ(v.zoom, 1.0);
    EXPECT_DOUBLE_EQ(v.rotationDeg, 0.0);
    EXPECT_FALSE(v.flipH);
    EXPECT_TRUE(v.pan.isNull());
}

TEST(ViewNavigatorHome, FitsToTheWindowWhenThePreferenceIsOn) {
    ViewState v;
    v.zoom = 3.0;
    v.rotationDeg = 90.0;
    v.flipH = true;
    ViewNavigator nav(v, QSizeF(800, 600), QSizeF(2000, 1000));
    nav.home(true);
    EXPECT_DOUBLE_EQ(v.rotationDeg, 0.0);
    EXPECT_FALSE(v.flipH);
    const QRectF shown = nav.docToWidget().mapRect(QRectF(0, 0, 2000, 1000));
    EXPECT_TRUE(QRectF(0, 0, 800, 600).contains(shown));
    EXPECT_LT(v.zoom, 0.5);
}
