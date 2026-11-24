#include "core/brush/BrushSettings.h"
#include "core/document/Document.h"
#include "core/paint/StrokeController.h"
#include "core/raster/Channel.h"
#include "core/raster/TiledImage.h"
#include "core/stroke/DabTip.h"
#include "core/stroke/PressureCurve.h"
#include "core/stroke/RebuiltStroke.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <deque>
#include <iterator>
#include <map>
#include <string>
#include <utility>
#include <vector>

using namespace doodlebold;

// The rules for the Brush, Water Color and Marker, each checked over a hard red | blue field, over white, or on a
// transparent layer. A mouse is full pressure.

namespace {

constexpr QSize kArea(900, 300);
constexpr double kLineY = 150.0;
constexpr int kBoundary = 450;  // red to its left, blue to its right
const QColor kWhite(255, 255, 255);

InputSample at(double x, double y = kLineY, double pressure = 1.0, PointerKind kind = PointerKind::Pen) {
    InputSample sample;
    sample.pos = QPointF(x, y);
    sample.pressure = pressure;
    sample.kind = kind;
    return sample;
}

std::array<double, 4> pixel(const TiledImage& image, int x, int y = static_cast<int>(kLineY)) {
    const auto p = image.pixel(x, y);
    return {p[0] / double(kChannelOne), p[1] / double(kChannelOne), p[2] / double(kChannelOne), p[3] / double(kChannelOne)};
}

double alphaAt(const TiledImage& layer, int x, int y = static_cast<int>(kLineY)) {
    return pixel(layer, x, y)[3];
}

// A colour channel, straight, at a pixel.
double straight(const TiledImage& layer, int x, int channel, int y = static_cast<int>(kLineY)) {
    const auto p = pixel(layer, x, y);
    return p[3] > 0.0 ? p[static_cast<std::size_t>(channel)] / p[3] : 0.0;
}

// A tool with its rules' starting values, hard edged, 30 px, with nothing following pressure unless a check says so.
BrushSettings tool(ToolType type, int blending, int dilution, int persistence, int blurPressure = 0) {
    BrushSettings settings = defaultSettings(type);
    settings.size = 300;
    settings.pressureSize = false;
    settings.pressureDensity = false;
    settings.sharpness = 100;
    settings.density = 100;
    settings.blending = blending;
    settings.dilution = dilution;
    settings.persistence = persistence;
    settings.blurPressure = blurPressure;
    return settings;
}

// A tool that doesn't mix, hard and opaque.
BrushSettings plain(int sizePx) {
    BrushSettings settings = defaultSettings(ToolType::Pencil);
    settings.size = sizePx * 10;
    settings.pressureSize = false;
    settings.sharpness = 100;
    return settings;
}

TiledImage redBlueField() {
    TiledImage layer(kArea);
    layer.fill(QRect(0, 0, kBoundary, kArea.height()), Qt::red);
    layer.fill(QRect(kBoundary, 0, kArea.width() - kBoundary, kArea.height()), Qt::blue);
    return layer;
}

void stroke(TiledImage& layer, const BrushSettings& settings, const QColor& colour, double from, double to, double pressure = 1.0,
            PointerKind kind = PointerKind::Mouse, double y = kLineY) {
    RebuiltStroke painter;
    painter.begin(&layer, colour, settings, at(from, y, pressure, kind));
    const double step = to > from ? 6.0 : -6.0;
    for (double x = from + step; (to - x) * step >= 0.0; x += step)
        painter.add(at(x, y, pressure, kind));
    painter.end();
}

}

// Shared by all three

TEST(MixingRules, BlendingZeroOnOpaquePaintPaintsPlainBrushColour) {
    for (const ToolType type : {ToolType::Brush, ToolType::WaterColor, ToolType::Marker}) {
        TiledImage layer = redBlueField();
        stroke(layer, tool(type, 0, 0, 50), kWhite, 100.0, 800.0);
        for (const int x : {300, 600}) {
            EXPECT_NEAR(straight(layer, x, 1), 1.0, 0.01) << static_cast<int>(type) << " at " << x;
            EXPECT_NEAR(straight(layer, x, 0), 1.0, 0.01) << static_cast<int>(type) << " at " << x;
        }
    }
}

TEST(MixingRules, OnlyTheCurrentLayerCounts) {
    Document doc(QSize(600, 300), Qt::white);
    doc.paintTarget(doc.currentLayerId()).value()->fill(Qt::red);
    ASSERT_TRUE(doc.addLayer());
    StrokeController painter(&doc);
    BrushSettings settings = tool(ToolType::Brush, 100, 0, 100);
    ASSERT_TRUE(painter.begin(at(100.0, kLineY, 1.0, PointerKind::Mouse), Qt::blue, settings));
    for (double x = 106.0; x <= 500.0; x += 6.0)
        painter.add(at(x, kLineY, 1.0, PointerKind::Mouse));
    painter.finish();
    const TiledImage& top = doc.currentLayer()->pixels;
    EXPECT_LT(straight(top, 300, 0), 0.05) << "the red layer below gives nothing to pick up";
}

TEST(MixingRules, NearlyTransparentPixelsCountAsEmpty) {
    TiledImage layer(kArea);
    const std::array<Channel, 4> faint{50, 0, 0, 50};
    for (int y = 100; y < 200; ++y)
        for (int x = 0; x < 600; ++x)
            std::ranges::copy(faint, layer.writableTile(x / 64, y / 64)->at(x % 64, y % 64));
    stroke(layer, tool(ToolType::Brush, 100, 0, 100), Qt::blue, 100.0, 500.0);
    EXPECT_GT(straight(layer, 300, 2), 0.95) << "picking up a trace of red is picking up nothing, which gives the brush colour";
}

// White at Blending 50 and Persistence 100 across opaque red gives a pink a third of the way from red to white, the same on both sides.
TEST(MixingRules, BlendingFiftyGivesAThirdOfTheBrushColour) {
    TiledImage layer = redBlueField();
    stroke(layer, tool(ToolType::Brush, 50, 0, 100), kWhite, 100.0, 800.0);
    for (const int x : {300, 650}) {
        EXPECT_NEAR(straight(layer, x, 0), 1.0, 0.02) << x;
        EXPECT_NEAR(straight(layer, x, 1), 1.0 / 3.0, 0.06) << x;
        EXPECT_NEAR(straight(layer, x, 2), 1.0 / 3.0, 0.06) << x;
    }
}

TEST(MixingRules, PersistenceHundredDragsRedWellIntoTheBlue) {
    TiledImage layer = redBlueField();
    stroke(layer, tool(ToolType::Brush, 100, 0, 100), kWhite, 200.0, 850.0);
    EXPECT_GT(straight(layer, 750, 0), 0.8);
    EXPECT_LT(straight(layer, 750, 2), 0.2);
}

TEST(MixingRules, PersistenceZeroFollowsWhatsUnderAlmostAtOnce) {
    TiledImage layer = redBlueField();
    stroke(layer, tool(ToolType::Brush, 100, 0, 0), kWhite, 200.0, 850.0);
    EXPECT_GT(straight(layer, kBoundary + 60, 2), 0.8) << "60 px into the blue it's blue";
}

TEST(MixingRules, DilutionChangesNothingOverOpaquePaint) {
    for (const ToolType type : {ToolType::Brush, ToolType::WaterColor}) {
        TiledImage none = redBlueField();
        stroke(none, tool(type, 50, 0, 50), Qt::green, 100.0, 800.0);
        TiledImage half = redBlueField();
        stroke(half, tool(type, 50, 50, 50), Qt::green, 100.0, 800.0);
        for (int x = 80; x <= 820; x += 9)
            for (int y = 120; y <= 180; y += 6)
                ASSERT_EQ(none.pixel(x, y), half.pixel(x, y)) << static_cast<int>(type) << " at " << x << ", " << y;
    }
}

TEST(MixingRules, OverPartlyTransparentPaintDilutionKeepsItsTransparency) {
    const auto alphaWith = [](int dilution) {
        TiledImage layer(kArea);
        layer.fill(QColor(255, 0, 0, 128));
        stroke(layer, tool(ToolType::Brush, 100, dilution, 50), Qt::blue, 100.0, 800.0);
        return alphaAt(layer, 450);
    };
    EXPECT_GT(alphaWith(0), 0.95) << "at 0 what's picked up counts as opaque";
    EXPECT_NEAR(alphaWith(100), 0.5, 0.05) << "at 100 % it keeps the layer's transparency";
}

TEST(MixingRules, BelowBlurPressureALightTouchLeavesRedUntouched) {
    TiledImage layer = redBlueField();
    stroke(layer, tool(ToolType::Brush, 50, 0, 50, 50), kWhite, 100.0, 400.0, 0.3, PointerKind::Pen);
    EXPECT_NEAR(straight(layer, 250, 0), 1.0, 0.01);
    EXPECT_LT(straight(layer, 250, 1), 0.01);
}

TEST(MixingRules, AboveBlurPressureColourFadesInWithPressure) {
    const auto whiteAt = [](double pressure) {
        TiledImage layer = redBlueField();
        stroke(layer, tool(ToolType::Brush, 50, 0, 100, 50), kWhite, 100.0, 400.0, pressure, PointerKind::Pen);
        return straight(layer, 250, 1);
    };
    EXPECT_LT(whiteAt(0.6), whiteAt(0.8) - 0.03);
    EXPECT_LT(whiteAt(0.8), whiteAt(1.0) - 0.03);
}

TEST(MixingRules, ATransparentColourBlendsTowardTransparentByDilution) {
    const auto alphaWith = [](ToolType type, int blending, int dilution) {
        TiledImage layer = redBlueField();
        stroke(layer, tool(type, blending, dilution, 0), QColor(0, 0, 0, 0), 100.0, 400.0);
        return alphaAt(layer, 250);
    };
    EXPECT_LT(alphaWith(ToolType::Brush, 50, 0), 0.05) << "Dilution 0: it lays transparency";
    EXPECT_GT(alphaWith(ToolType::Brush, 50, 100), 0.95) << "Dilution 100 %: it only mixes";
    EXPECT_NEAR(alphaWith(ToolType::Brush, 0, 40), alphaWith(ToolType::Brush, 100, 40), 1e-9) << "Blending has no say";
    EXPECT_LT(alphaWith(ToolType::Marker, 0, 0), 0.05) << "the Marker uses Blending in its place";
    EXPECT_GT(alphaWith(ToolType::Marker, 100, 0), 0.95);
}

TEST(MixingRules, APressureButtonThatsOffLeavesItsSettingAtFullStrength) {
    const auto redWith = [](bool follows, double pressure) {
        BrushSettings settings = tool(ToolType::Brush, 100, 0, 0);
        settings.pressureBlending = follows;
        TiledImage layer = redBlueField();
        stroke(layer, settings, kWhite, 100.0, 400.0, pressure, PointerKind::Pen);
        return straight(layer, 250, 1);
    };
    EXPECT_NEAR(redWith(false, 0.4), redWith(false, 1.0), 0.01);
    EXPECT_GT(redWith(true, 0.4), redWith(false, 0.4) + 0.1) << "with it on, a light touch mixes less, so more white goes in";
}

TEST(MixingRules, SlowGradientsDontBand) {
    TiledImage layer(QSize(600, 100));
    for (int x = 0; x < 600; ++x) {
        const int red = 255 - x * 255 / 599;
        layer.fill(QRect(x, 0, 1, 100), QColor(red, 0, 255 - red));
    }
    stroke(layer, tool(ToolType::Brush, 30, 0, 0), kWhite, 40.0, 560.0, 1.0, PointerKind::Mouse, 50.0);
    double last = straight(layer, 100, 0, 50);
    for (int x = 101; x <= 500; ++x) {
        const double now = straight(layer, x, 0, 50);
        // Within rounding: the Brush's plain stamps rise by up to 1.03 levels in three places along it.
        EXPECT_LE(now, last + 1.5 / 255.0) << "red only falls along the gradient, at " << x;
        last = now;
    }
}

TEST(MixingRules, TheSameStrokeGivesTheSamePixels) {
    const auto paintOnce = [] {
        TiledImage layer = redBlueField();
        stroke(layer, tool(ToolType::Brush, 40, 30, 80), kWhite, 300.0, 700.0, 0.8, PointerKind::Pen);
        return layer;
    };
    const TiledImage a = paintOnce();
    const TiledImage b = paintOnce();
    for (int x = 280; x <= 720; x += 7)
        for (int y = 110; y <= 190; y += 5)
            ASSERT_EQ(a.pixel(x, y), b.pixel(x, y)) << x << ", " << y;
}

// The Brush

TEST(BrushRules, ItStartsWithTheRulesValuesAndFullDensityAtAboutSixtyPercentPressure) {
    const BrushSettings brush = defaultSettings(ToolType::Brush);
    EXPECT_EQ(brush.blending, 50);
    EXPECT_EQ(brush.dilution, 0);
    EXPECT_EQ(brush.persistence, 50);
    EXPECT_EQ(brush.blurPressure, 50);
    EXPECT_TRUE(brush.pressureDensity);
    EXPECT_EQ(brush.sensitivityDensity, 160);
    EXPECT_DOUBLE_EQ(pressureResponse(0.625, brush.sensitivityDensity), 1.0);
}

// The Brush: a tap on an empty layer leaves next to nothing, and light pressure lays pale paint, as dense as Density
// says. Over paint, below Blur Pressure, it only moves what's there. The tap is dots.jsonl's firm one, which wobbles a little, played as
// the app plays it, at the starting Stabilizer.
TEST(BrushSpec, ATapOnAnEmptyLayerLeavesNextToNothing) {
    struct Event {
        double dx, dy, pressure;  // px from where it landed
    };
    constexpr std::array<Event, 8> kTap{{{0.0, 0.0, 0.0},
                                         {0.0, 0.0, 0.45},
                                         {0.21, -0.226, 0.9},
                                         {0.227, -0.393, 0.9},
                                         {0.035, -0.124, 0.9},
                                         {-0.189, -0.026, 0.9},
                                         {-0.24, -0.32, 0.9},
                                         {-0.24, -0.32, 0.0}}};
    constexpr double kX = 300.0;
    Document doc(kArea, Qt::white);
    ASSERT_TRUE(doc.addLayer());
    StrokeController painter(&doc);
    ASSERT_TRUE(
        painter.begin(at(kX, kLineY, kTap.front().pressure), Qt::blue, defaultSettings(ToolType::Brush), StabilizerLevel::kGlobalStart));
    for (std::size_t i = 1; i < kTap.size(); ++i)
        painter.add(at(kX + kTap[i].dx, kLineY + kTap[i].dy, kTap[i].pressure));
    painter.finish();
    EXPECT_LT(alphaAt(doc.currentLayer()->pixels, static_cast<int>(kX)), 0.02);
}

// A click that never moves leaves one faint, flat stamp, as the reference's does.
// At 1000 px, Min Size 50 % and pressure on size, density and blending, the mouse's is 74 of 255 across the full size, and a pen's
// at 0.5 is 18 across that pressure's size.
TEST(BrushSpec, AClickThatNeverMovesLeavesOneFaintStamp) {
    constexpr double kCentre = 600.0;
    constexpr int kSide = 1200;
    BrushSettings settings = defaultSettings(ToolType::Brush);
    settings.size = 10000;  // tenths of a px
    settings.minSize = 50;
    settings.pressureSize = settings.pressureDensity = settings.pressureBlending = true;
    struct Click {
        PointerKind kind;
        double pressure;
        double alpha;  // of 255
        int inside;  // px from the centre still at that alpha
        int outside;  // and past the edge, at none
    };
    for (const Click& click : {Click{PointerKind::Mouse, 1.0, 74.0, 490, 510}, Click{PointerKind::Pen, 0.5, 18.0, 350, 410}}) {
        TiledImage layer(QSize(kSide, kSide));
        RebuiltStroke painter;
        painter.begin(&layer, QColor(32, 80, 200), settings, at(kCentre, kCentre, click.pressure, click.kind));
        painter.end();
        const int centre = static_cast<int>(kCentre);
        EXPECT_NEAR(alphaAt(layer, centre, centre) * 255.0, click.alpha, 1.0) << static_cast<int>(click.kind);
        EXPECT_NEAR(alphaAt(layer, centre + click.inside, centre) * 255.0, click.alpha, 1.0) << static_cast<int>(click.kind);
        EXPECT_EQ(alphaAt(layer, centre + click.outside, centre), 0.0) << static_cast<int>(click.kind);
    }
}

// And the Pencil's, Water Color's and the Marker's, as the reference's (section 5b): the mouse's 74, 58 and 255 across the full size,
// a pen's at 0.5 8, nothing (at Water Color's Blur Pressure) and 32 across that pressure's size.
TEST(ClickStamp, ThePencilWaterColorAndMarkerLeaveTheReferencesStamp) {
    constexpr double kCentre = 600.0;
    constexpr int kSide = 1200;
    struct Click {
        ToolType tool;
        PointerKind kind;
        double pressure;
        double alpha;  // of 255
        int inside;  // px from the centre still at that alpha
        int outside;  // and past the edge, at none
    };
    for (const Click& click :
         {Click{ToolType::Pencil, PointerKind::Mouse, 1.0, 74.0, 490, 510}, Click{ToolType::Pencil, PointerKind::Pen, 0.5, 8.0, 350, 410},
          Click{ToolType::WaterColor, PointerKind::Mouse, 1.0, 58.0, 490, 510},
          Click{ToolType::WaterColor, PointerKind::Pen, 0.5, 0.0, 0, 0}, Click{ToolType::Marker, PointerKind::Mouse, 1.0, 255.0, 490, 510},
          Click{ToolType::Marker, PointerKind::Pen, 0.5, 32.0, 350, 410}}) {
        BrushSettings settings = defaultSettings(click.tool);
        settings.size = 10000;  // tenths of a px
        settings.minSize = 50;
        settings.pressureSize = settings.pressureDensity = settings.pressureBlending = true;
        TiledImage layer(QSize(kSide, kSide));
        RebuiltStroke painter;
        painter.begin(&layer, QColor(32, 80, 200), settings, at(kCentre, kCentre, click.pressure, click.kind));
        painter.end();
        const int centre = static_cast<int>(kCentre);
        const std::string which = toolTypeName(click.tool).toStdString() + (click.kind == PointerKind::Mouse ? " mouse" : " pen");
        EXPECT_NEAR(alphaAt(layer, centre, centre) * 255.0, click.alpha, 1.0) << which;
        EXPECT_NEAR(alphaAt(layer, centre + click.inside, centre) * 255.0, click.alpha, 1.0) << which;
        EXPECT_EQ(alphaAt(layer, centre + click.outside, centre), 0.0) << which;
    }
}

// The Eraser's click on paint takes away what the Pencil's lays, as the reference's does: 74 of 255 with the mouse across the full size,
// 8 with a pen at 0.5 across that pressure's size.
TEST(ClickStamp, TheEraserTakesAwayWhatThePencilLays) {
    constexpr double kCentre = 600.0;
    constexpr int kSide = 1200;
    struct Click {
        PointerKind kind;
        double pressure;
        double takes;  // of 255
        int inside;  // px from the centre still taking that much
        int outside;  // and past the edge, taking none
    };
    // The Pencil's deposit, 73.8 of 255, over kEraserFifthFullAt: the 5th-Shape Eraser counts 0.97 of its full point as full, so a line's
    // middle erases clean.
    for (const Click& click : {Click{PointerKind::Mouse, 1.0, 76.1, 490, 510}, Click{PointerKind::Pen, 0.5, 8.0, 350, 410}}) {
        BrushSettings settings = defaultSettings(ToolType::Eraser);
        settings.size = 10000;  // tenths of a px
        settings.minSize = 50;
        settings.pressureSize = settings.pressureDensity = true;
        TiledImage layer(QSize(kSide, kSide));
        layer.fill(QColor(32, 80, 200));
        RebuiltStroke painter;
        painter.begin(&layer, QColor(32, 80, 200), settings, at(kCentre, kCentre, click.pressure, click.kind));
        painter.end();
        const int centre = static_cast<int>(kCentre);
        const char* which = click.kind == PointerKind::Mouse ? "mouse" : "pen";
        EXPECT_NEAR((1.0 - alphaAt(layer, centre, centre)) * 255.0, click.takes, 1.0) << which;
        EXPECT_NEAR((1.0 - alphaAt(layer, centre + click.inside, centre)) * 255.0, click.takes, 1.0) << which;
        EXPECT_EQ(alphaAt(layer, centre + click.outside, centre), 1.0) << which;
    }
}

// A soft Shape's outline adds up along the path, so where a stroke comes back beside itself its two soft edges add up as
// overlapping paint does, 1 - (1 - a)^2 for an edge of a alone, where keeping the most gave a and left a crease between them.
TEST(BrushSpec, ASoftStrokeComingBackBesideItselfAddsUpItsEdges) {
    constexpr double kFrom = 200.0, kTo = 1400.0, kTop = 300.0, kApart = 300.0;  // px
    constexpr double kStep = 6.0;  // px between pointer events
    constexpr int kMiddle = 800;
    BrushSettings settings = defaultSettings(ToolType::Brush);
    settings.edge = 0;
    settings.size = 3000;  // tenths of a px
    const auto paint = [&](bool comesBack) {
        TiledImage layer(QSize(1600, 800));
        RebuiltStroke painter;
        painter.begin(&layer, Qt::blue, settings, at(kFrom, kTop, 1.0, PointerKind::Mouse));
        for (double x = kFrom + kStep; x <= kTo; x += kStep)
            painter.add(at(x, kTop, 1.0, PointerKind::Mouse));
        if (comesBack) {
            for (double y = kTop + kStep; y <= kTop + kApart; y += kStep)
                painter.add(at(kTo, y, 1.0, PointerKind::Mouse));
            for (double x = kTo - kStep; x >= kFrom; x -= kStep)
                painter.add(at(x, kTop + kApart, 1.0, PointerKind::Mouse));
        }
        painter.end();
        return alphaAt(layer, kMiddle, static_cast<int>(kTop + kApart / 2.0));
    };
    const double alone = paint(false);
    ASSERT_GT(alone, 0.05);
    ASSERT_LT(alone, 0.95);
    EXPECT_GT(paint(true), alone + alone * (1.0 - alone) / 2.0) << "the two edges add up, at least halfway from the most to 1 - (1 - a)^2";
}

TEST(BrushSpec, LightPressureOnAnEmptyLayerLaysPalePaint) {
    TiledImage layer(kArea);
    stroke(layer, defaultSettings(ToolType::Brush), Qt::blue, 100.0, 800.0, 0.2, PointerKind::Pen);
    EXPECT_NEAR(alphaAt(layer, 450), 0.32, 0.05) << "Density's share at pressure 0.2 and sensitivity 160 %";
    EXPECT_NEAR(straight(layer, 450, 2), 1.0, 0.01) << "and the brush colour";
}

// Feedback: the Brush's soft Shapes "still create hard rims" where a stroke runs over itself. Their soft edges add up over the
// stroke, so a loop's middle fills, where each stamp's edge alone covers it a quarter.
TEST(BrushSpec, ALoopsSoftEdgesAddUpInItsMiddle) {
    BrushSettings settings = defaultSettings(ToolType::Brush);
    settings.size = 1981;
    settings.edge = 0;
    TiledImage layer(QSize(600, 600));
    RebuiltStroke painter;
    const auto onCircle = [](double turn) { return at(300.0 + 92.0 * std::cos(turn), 300.0 + 92.0 * std::sin(turn), 1.0); };
    painter.begin(&layer, Qt::blue, settings, onCircle(0.0));
    for (double turn = 0.03; turn <= 2.0 * 3.14159265358979 + 0.3; turn += 0.03)
        painter.add(onCircle(turn));
    painter.end();
    EXPECT_GT(alphaAt(layer, 300, 300), 0.75) << "0.28 before";
}

TEST(BrushSpec, LightPressureOverPaintOnlyMovesIt) {
    TiledImage layer = redBlueField();
    stroke(layer, defaultSettings(ToolType::Brush), Qt::yellow, 100.0, 400.0, 0.3, PointerKind::Pen);
    EXPECT_NEAR(straight(layer, 300, 1), 0.0, 0.01) << "no yellow in the red";
}

// Water Color

TEST(WaterColorRules, ItSoftensWhatsAlreadyThere) {
    TiledImage layer = redBlueField();
    stroke(layer, tool(ToolType::WaterColor, 50, 30, 50), kWhite, 400.0, 500.0, 0.4, PointerKind::Pen);
    EXPECT_GT(straight(layer, kBoundary + 3, 0), 0.05) << "red has spread across the boundary";
}

TEST(WaterColorRules, BelowBlurPressureALightTouchOnlyBlurs) {
    TiledImage layer = redBlueField();
    stroke(layer, tool(ToolType::WaterColor, 50, 30, 50, 50), kWhite, 380.0, 520.0, 0.3, PointerKind::Pen);
    EXPECT_GT(straight(layer, kBoundary + 3, 0), 0.05) << "blurred";
    EXPECT_LT(straight(layer, kBoundary + 3, 1), 0.01) << "and no white";
}

TEST(WaterColorRules, OnAnEmptyLayerALightTouchPaintsNothing) {
    TiledImage layer(kArea);
    stroke(layer, tool(ToolType::WaterColor, 30, 30, 50, 50), Qt::blue, 100.0, 600.0, 0.3, PointerKind::Pen);
    EXPECT_LT(alphaAt(layer, 350), 0.01);
}

TEST(WaterColorRules, AtDilutionZeroAStrokeStartingOnEmptyDoesntGoPale) {
    TiledImage layer(kArea);
    stroke(layer, tool(ToolType::WaterColor, 30, 0, 50), Qt::blue, 100.0, 600.0);
    EXPECT_GT(alphaAt(layer, 400), 0.95);
}

TEST(WaterColorRules, ItStartsAtTheRulesValues) {
    const BrushSettings water = defaultSettings(ToolType::WaterColor);
    EXPECT_EQ(water.blending, 30);
    EXPECT_EQ(water.dilution, 30);
    EXPECT_EQ(water.persistence, 50);
    EXPECT_EQ(water.blurPressure, 50);
    EXPECT_EQ(water.sensitivityDensity, 100);
}

// The Marker

TEST(MarkerRules, ItDoesntDarkenWhereAStrokeCrossesItself) {
    BrushSettings settings = tool(ToolType::Marker, 0, 0, 80);
    settings.density = 50;
    TiledImage layer(kArea);
    RebuiltStroke painter;
    painter.begin(&layer, Qt::blue, settings, at(100.0, kLineY, 1.0, PointerKind::Mouse));
    for (double x = 106.0; x <= 600.0; x += 6.0)
        painter.add(at(x, kLineY, 1.0, PointerKind::Mouse));
    for (double x = 594.0; x >= 100.0; x -= 6.0)
        painter.add(at(x, kLineY, 1.0, PointerKind::Mouse));
    painter.end();
    EXPECT_LE(alphaAt(layer, 350), 0.5 + 0.005);
}

// Going back over its own paint at a lighter touch, a stroke leaves the darker paint
// as it was, so it never lightens itself.
TEST(MarkerRules, ItNeverLightensItsOwnPaint) {
    const auto alphaAfter = [](bool comesBack) {
        BrushSettings settings = tool(ToolType::Marker, 0, 0, 80);
        settings.pressureDensity = true;
        TiledImage layer(kArea);
        RebuiltStroke painter;
        painter.begin(&layer, Qt::blue, settings, at(100.0, kLineY, 0.9));
        for (double x = 106.0; x <= 600.0; x += 6.0)
            painter.add(at(x, kLineY, 0.9));
        for (double x = 594.0; comesBack && x >= 100.0; x -= 6.0)
            painter.add(at(x, kLineY, 0.4));
        painter.end();
        return alphaAt(layer, 350);
    };
    EXPECT_GT(alphaAfter(false), 0.5);
    EXPECT_NEAR(alphaAfter(true), alphaAfter(false), 0.005);
}

TEST(MarkerRules, OnATransparentLayerAStrokeApproachesDensityFromBelow) {
    for (const int density : {25, 50, 75}) {
        BrushSettings settings = tool(ToolType::Marker, 0, 0, 80);
        settings.density = density;
        TiledImage layer(kArea);
        stroke(layer, settings, Qt::blue, 100.0, 700.0);
        double most = 0.0;
        for (int x = 100; x <= 700; ++x)
            for (int y = 135; y <= 165; ++y)
                most = std::max(most, alphaAt(layer, x, y));
        EXPECT_LE(most, density / 100.0 + 0.004) << density;
        EXPECT_GT(alphaAt(layer, 400), density / 100.0 * 0.6) << density;
    }
}

TEST(MarkerRules, TheCeilingIsOnOpacityNotColour) {
    BrushSettings settings = tool(ToolType::Marker, 0, 0, 80);
    settings.density = 50;
    TiledImage layer = redBlueField();
    stroke(layer, settings, Qt::green, 100.0, 400.0);
    EXPECT_GT(straight(layer, 250, 1), 0.95) << "over opaque paint its colour goes in at full strength";
    EXPECT_NEAR(alphaAt(layer, 250), 1.0, 1e-3);
}

TEST(MarkerRules, WithDensityPressureALightTouchKeepsTheWholeStrokeLight) {
    BrushSettings settings = tool(ToolType::Marker, 0, 0, 80);
    settings.pressureDensity = true;
    TiledImage layer(kArea);
    stroke(layer, settings, Qt::blue, 100.0, 700.0, 0.3, PointerKind::Pen);
    EXPECT_LT(alphaAt(layer, 400), 0.35);
}

TEST(MarkerRules, ItsStampsAreDenserThanThePencilsAndSparserWhenSoftAndLookContinuous) {
    for (int edge = 0; edge < kEdgePresets; ++edge)
        EXPECT_LT(markerSpacingShare(edge), stampSpacing(20.0) / 40.0) << "denser than the Pencil at edge " << edge;
    EXPECT_GT(markerSpacingShare(0), markerSpacingShare(4)) << "the softer, the sparser";
    BrushSettings settings = tool(ToolType::Marker, 0, 0, 80);
    settings.size = 1200;
    settings.sharpness = defaultSettings(ToolType::Marker).sharpness;
    settings.density = 60;
    TiledImage layer(kArea);
    stroke(layer, settings, Qt::blue, 100.0, 800.0, 1.0, PointerKind::Mouse);
    double lowest = 1.0;
    double highest = 0.0;
    for (int x = 300; x <= 600; ++x) {
        lowest = std::min(lowest, alphaAt(layer, x));
        highest = std::max(highest, alphaAt(layer, x));
    }
    EXPECT_LT(highest - lowest, 0.01) << "no beading";
}

// Feedback, at Stabilizer 12: "I am still seeing some very visible stamps." Where pressure falls, stamps go in between until each
// changes the strength by little, so the fade has no rings.
TEST(MarkerRules, AFadingEndChangesSmoothlyWithNoRings) {
    BrushSettings settings = defaultSettings(ToolType::Marker);
    settings.size = 1174;
    settings.minSize = 100;
    TiledImage layer(kArea);
    RebuiltStroke painter;
    painter.begin(&layer, Qt::blue, settings, at(100.0, kLineY, 1.0));
    for (double x = 106.0; x <= 700.0; x += 6.0)
        painter.add(at(x, kLineY, x < 400.0 ? 1.0 : std::max(0.05, 1.0 - (x - 400.0) / 300.0)));
    painter.end();
    double largestStep = 0.0;
    for (int x = 420; x < 760; ++x)
        largestStep = std::max(largestStep, std::abs(alphaAt(layer, x + 1) - alphaAt(layer, x)));
    EXPECT_GT(alphaAt(layer, 420), 0.3);
    EXPECT_LT(largestStep, 0.04) << "no step from one stamp's disc to the next: about 0.06 before, on a much sparser Marker";
}

// Feedback, on a 426 px Marker: "im seeing some big stamps again". The reference painted on
// this very case isn't ring-free either: faint rings about every 20 px at its last stamps'
// outlines, its largest step kReferenceLargestStep, and it holds half its alpha or more out to x kReferenceHalfTo. So the end is held to
// the reference's: a fade at least as long, and no step larger than its own while it's half or more. Laid in groups with the
// reference's stamps it holds to x 1322 with its largest step 0.054.
TEST(MarkerRules, AWideMarkersEndFadesWithNoRingsWhenThePenSlowsAsItLifts) {
    constexpr double kReferenceLargestStep = 0.056;  // of alpha, between neighbouring pixels along row 350, the reference's largest
    constexpr int kReferenceHalfTo = 1308;  // px along row 350: the last the reference's alpha is 0.5 or more
    BrushSettings settings = defaultSettings(ToolType::Marker);
    settings.size = 4259;
    settings.minSize = 100;
    TiledImage layer(QSize(1800, 700));
    RebuiltStroke painter;
    painter.begin(&layer, Qt::blue, settings, at(300.0, 350.0, 1.0));
    double x = 300.0;
    for (; x < 1000.0; x += 25.0)
        painter.add(at(x, 350.0, 1.0));
    for (int k = 1; k <= 24; ++k) {
        x += std::max(3.0, 25.0 * (1.0 - k / 12.0));
        painter.add(at(x, 350.0, std::max(0.0, 1.0 - k / 24.0)));
    }
    painter.end();
    // The rings are behind the front edge, where the last stamps' outlines all end within a few pixels of each other.
    double largestStep = 0.0;
    int px = 1000;
    for (; px < 1700 && alphaAt(layer, px + 1, 350) >= 0.5; ++px)
        largestStep = std::max(largestStep, std::abs(alphaAt(layer, px + 1, 350) - alphaAt(layer, px, 350)));
    EXPECT_GE(px, kReferenceHalfTo) << "the fade is at least as long as the reference's";
    EXPECT_LE(largestStep, kReferenceLargestStep) << "no step larger than the reference's own";
}

// Feedback: the Marker, "when drawing over the red and blue dot blurs on a dab". Its last light stamps change the colour the body
// laid only by their share of the firmest, so the end keeps the body's colour.
TEST(MarkerRules, AFadingEndOverPaintKeepsTheBodysColour) {
    BrushSettings settings = defaultSettings(ToolType::Marker);
    settings.size = 600;
    settings.minSize = 100;
    TiledImage layer = redBlueField();
    RebuiltStroke painter;
    painter.begin(&layer, Qt::green, settings, at(500.0, kLineY, 1.0));
    for (double x = 506.0; x <= 760.0; x += 6.0)
        painter.add(at(x, kLineY, x < 700.0 ? 1.0 : std::max(0.05, 1.0 - (x - 700.0) / 60.0)));
    painter.end();
    EXPECT_NEAR(straight(layer, 740, 1), straight(layer, 690, 1), 0.09) << "as green at the end as in the body: 0.13 greener before";
}

TEST(MarkerRules, ItHasNoKeepOpacity) {
    BrushSettings settings = tool(ToolType::Marker, 0, 0, 80);
    TiledImage layer(kArea);
    RebuiltStroke painter;
    painter.begin(&layer, Qt::blue, settings, at(100.0, kLineY, 1.0, PointerKind::Mouse), true);
    for (double x = 106.0; x <= 500.0; x += 6.0)
        painter.add(at(x, kLineY, 1.0, PointerKind::Mouse));
    painter.end();
    EXPECT_GT(alphaAt(layer, 300), 0.9) << "it paints on an empty layer even with the layer's opacity kept";
}

TEST(MarkerRules, ItStartsAtTheRulesValues) {
    const BrushSettings marker = defaultSettings(ToolType::Marker);
    EXPECT_EQ(marker.blending, 50);
    EXPECT_EQ(marker.dilution, 0);
    EXPECT_EQ(marker.persistence, 80);
    EXPECT_EQ(marker.blurPressure, 0);
}

TEST(PressureResponse, At100ItsAStraightLine) {
    for (const double p : {0.0, 0.25, 0.5, 1.0})
        EXPECT_DOUBLE_EQ(pressureResponse(p, 100), p);
}

TEST(PressureResponse, BelowItALightTouchBarelyRegisters) {
    EXPECT_DOUBLE_EQ(pressureResponse(0.5, 50), 0.25);
    EXPECT_DOUBLE_EQ(pressureResponse(0.5, 0), 0.0625);
    EXPECT_DOUBLE_EQ(pressureResponse(1.0, 0), 1.0);
}

TEST(PressureResponse, AboveItReachesFullEarlyAndStaysThere) {
    EXPECT_DOUBLE_EQ(pressureResponse(0.25, 200), 0.5);
    EXPECT_DOUBLE_EQ(pressureResponse(0.5, 200), 1.0);
    EXPECT_DOUBLE_EQ(pressureResponse(0.8, 200), 1.0);
    EXPECT_DOUBLE_EQ(pressureResponse(0.5, 150), 0.75);
}

TEST(PressureResponse, ItNeverFalls) {
    for (const int sensitivity : {0, 30, 70, 100, 130, 200}) {
        double last = 0.0;
        for (double p = 0.0; p <= 1.0; p += 0.01) {
            EXPECT_GE(pressureResponse(p, sensitivity), last - 1e-12) << sensitivity << " at " << p;
            last = pressureResponse(p, sensitivity);
        }
    }
}

TEST(PressureResponse, AMinimumIsWhatsLeftAtNoPressure) {
    EXPECT_DOUBLE_EQ(pressureShare(0.0, true, 0.4, 100), 0.4);
    EXPECT_DOUBLE_EQ(pressureShare(0.5, true, 0.4, 100), 0.7);
    EXPECT_DOUBLE_EQ(pressureShare(0.0, false, 0.4, 100), 1.0) << "with the switch off it's always full";
}

// How wide a hard stroke comes out, with a pen held at this pressure.
int widthAtPressure(const BrushSettings& settings, double pressure) {
    TiledImage layer(kArea);
    RebuiltStroke stroke;
    InputSample sample = at(200.0);
    sample.kind = PointerKind::Pen;
    sample.pressure = pressure;
    stroke.begin(&layer, Qt::blue, settings, sample);
    for (double x = 206.0; x <= 700.0; x += 6.0) {
        sample.pos.setX(x);
        stroke.add(sample);
    }
    stroke.end();
    int width = 0;
    for (int y = 0; y < kArea.height(); ++y) {
        if (pixel(layer, 450, y)[3] > 0.5)
            ++width;
    }
    return width;
}

TEST(SizeSensitivity, ItSetsTheWidthFromPressure) {
    BrushSettings settings = plain(200);
    settings.pressureSize = true;
    settings.minSize = 0;
    settings.sensitivitySize = 50;
    EXPECT_NEAR(widthAtPressure(settings, 0.5), 50, 3) << "a quarter of 200 px";
    EXPECT_NEAR(widthAtPressure(settings, 1.0), 200, 3);
    settings.sensitivitySize = 200;
    EXPECT_NEAR(widthAtPressure(settings, 0.5), 200, 3) << "full from half pressure";
}

TEST(SizeSensitivity, NoToolStartsCurved) {
    EXPECT_EQ(defaultSettings(ToolType::Brush).sensitivitySize, 100);
    EXPECT_EQ(defaultSettings(ToolType::Marker).sensitivitySize, 100);
}

// A pixel the stroke already covers fully still takes on new paint, and one nearly covered isn't swapped for it whole, so going back
// over a stroke leaves no marks where the two would meet.
TEST(Dabs, GoingBackOverItsOwnStrokeLeavesNoMarksDownTheMiddle) {
    BrushSettings settings = defaultSettings(ToolType::Brush);
    settings.size = 1482;
    TiledImage layer(kArea);
    RebuiltStroke stroke;
    stroke.begin(&layer, QColor(255, 16, 51), settings, at(100.0, kLineY, 0.7));
    for (double x = 105.5; x <= 760.0; x += 5.5)
        stroke.add(at(x, kLineY, 0.7));
    for (double x = 754.5; x >= 100.0; x -= 5.5)
        stroke.add(at(x, kLineY, 0.7));
    stroke.end();
    for (const int y : {static_cast<int>(kLineY) - 1, static_cast<int>(kLineY), static_cast<int>(kLineY) + 1}) {
        double steepest = 0.0;
        for (int x = 300; x < 560; ++x)
            steepest = std::max(steepest, std::abs(alphaAt(layer, x + 1, y) - alphaAt(layer, x, y)));
        EXPECT_LT(steepest, 0.002) << "row " << y << ": from one pixel to the next";
    }
}

// The numbered test cases for the Brush, Water Color and Marker. A 768 × 512 field, red on the left and blue on the
// right, white ink, size 30, density 100, a mouse, and a stroke along the middle from well inside the red into the blue.

namespace {}

// The cases B1 and B2 for the Brush, and W2 and W3 for Water Color, were from before the Brush and Water Color were redesigned,
// which win.
// The case B3 had white start to show at about two-thirds pressure. The Brush now has colour
// come in from about half pressure and more and more above it.
TEST(RecordedCases, B3TheBrushLaysNoColourUnderHalfPressureAndMoreAboveIt) {
    BrushSettings settings = defaultSettings(ToolType::Brush);
    settings.size = 300;
    settings.blending = 50;
    settings.pressureBlending = true;
    settings.blurPressure = 50;
    settings.pressureDensity = false;
    settings.pressureSize = false;
    TiledImage layer(QSize(1200, 300));
    layer.fill(Qt::red);
    RebuiltStroke painter;
    painter.begin(&layer, kWhite, settings, at(50.5, 150.5, 0.0));
    for (double x = 52.5; x <= 1150.5; x += 2.0)
        painter.add(at(x, 150.5, (x - 50.5) / 1100.0));
    painter.end();
    const auto greenAt = [&](double pressure) { return toUnorm8(layer.pixel(static_cast<int>(50.5 + pressure * 1100.0), 150))[1]; };
    EXPECT_EQ(greenAt(0.47), 0);
    EXPECT_LT(greenAt(0.55), greenAt(0.7));
    EXPECT_LT(greenAt(0.7), greenAt(0.85));
    EXPECT_LT(greenAt(0.85), greenAt(0.99));
}

// The M2: a stroke approaches Density from below. Its alphas are the reference's, this very stroke painted on it,
// 0.715, 0.409 and 0.132: chosen
// on 2026-11-17 in place of the early M2 note's 0.744, 0.459 and 0.167. M1, a click being
// about Density squared, is gone: a Marker tap leaves nothing.
TEST(RecordedCases, M2AStrokeApproachesDensityFromBelow) {
    const std::array<std::pair<int, double>, 3> expected{{{75, 0.715}, {50, 0.409}, {25, 0.132}}};
    for (const auto& [density, alpha] : expected) {
        BrushSettings settings = defaultSettings(ToolType::Marker);
        settings.size = 400;
        settings.edge = 0;
        settings.density = density;
        settings.blending = settings.dilution = settings.persistence = 0;
        TiledImage layer(QSize(768, 512));
        RebuiltStroke painter;
        painter.begin(&layer, Qt::black, settings, at(200.5, 256.5, 1.0, PointerKind::Mouse));
        for (double x = 202.5; x <= 600.5; x += 2.0)
            painter.add(at(x, 256.5, 1.0, PointerKind::Mouse));
        painter.end();
        EXPECT_NEAR(alphaAt(layer, 400, 256), alpha, 0.03) << density;
        EXPECT_LT(alphaAt(layer, 400, 256), density / 100.0);
    }
}

// The M4 had a second stroke crossing the first darken it. The Marker now has strokes of one colour at
// one pressure never darken each other.
TEST(RecordedCases, M4TwoSeparateStrokesOfOneColourCrossingAreNoDarker) {
    BrushSettings settings = tool(ToolType::Marker, 0, 0, 0);
    settings.density = 50;
    TiledImage layer(kArea);
    stroke(layer, settings, Qt::black, 100.0, 700.0);
    const double one = alphaAt(layer, 400, 150);
    RebuiltStroke across;
    across.begin(&layer, Qt::black, settings, at(400.0, 40.0, 1.0, PointerKind::Mouse));
    for (double y = 46.0; y <= 260.0; y += 6.0)
        across.add(at(400.0, y, 1.0, PointerKind::Mouse));
    across.end();
    EXPECT_NEAR(alphaAt(layer, 400, 150), one, 0.01);
}

TEST(RecordedCases, M6TheMarkersSpacingBySoftness) {
    const std::array<double, 5> expected{1.696, 1.392, 1.244, 1.092, 0.94};
    for (int edge = 0; edge < 5; ++edge)
        EXPECT_NEAR(markerSpacingShare(edge) * 40.0, expected[static_cast<std::size_t>(edge)], 0.01) << edge;
}

TEST(RecordedCases, M8WithDensityPressureALightTouchKeepsTheWholeStrokeUnderItsSquare) {
    BrushSettings settings = tool(ToolType::Marker, 0, 0, 0);
    settings.pressureDensity = true;
    TiledImage layer(kArea);
    stroke(layer, settings, Qt::black, 100.0, 700.0, 0.3, PointerKind::Pen);
    double most = 0.0;
    for (int x = 100; x <= 700; x += 2)
        most = std::max(most, alphaAt(layer, x, 150));
    EXPECT_LE(most, 0.09 + 0.005);
}

// Water Color melts what's under it into one even colour, where it used to let it show through.
// Even under half pressure, where it lays no colour of its own.
TEST(WaterColorRules, ItMeltsWhatsUnderneathIntoOneColour) {
    TiledImage layer(QSize(600, 200));
    for (int x = 0; x < 600; x += 24) {
        layer.fill(QRect(x, 0, 12, 200), Qt::red);
        layer.fill(QRect(x + 12, 0, 12, 200), Qt::blue);
    }
    BrushSettings settings = defaultSettings(ToolType::WaterColor);
    settings.size = 500;
    settings.pressureDensity = false;
    settings.pressureSize = false;
    RebuiltStroke painter;
    painter.begin(&layer, Qt::white, settings, at(60.5, 100.5, 0.35));
    for (double x = 62.5; x <= 540.5; x += 2.0)
        painter.add(at(x, 100.5, 0.35));
    painter.end();
    double lowest = 1.0;
    double highest = 0.0;
    for (int x = 276; x < 324; ++x) {
        lowest = std::min(lowest, straight(layer, x, 0, 100));
        highest = std::max(highest, straight(layer, x, 0, 100));
    }
    EXPECT_LT(highest - lowest, 0.03) << "the stripes are gone";
    EXPECT_NEAR(straight(layer, 300, 0, 100), 0.5, 0.05) << "into their average";
    EXPECT_NEAR(straight(layer, 300, 1, 100), 0.0, 0.01) << "with no white in it";
}

// Feedback: "Blur is barely noticeable. We should have a strength modifier for it." Blur Width 0 doesn't blur, as the mocks'
// program's doesn't, and the wider it is the flatter stripes go, more than 50 % flattening further where the box no longer can.
TEST(BlurTool, BlurWidthSetsHowFarItBlurs) {
    const auto contrastAt = [](int width) {
        BrushSettings settings = defaultSettings(ToolType::Blur);
        settings.blurWidth = width;
        TiledImage stripes(QSize(200, 100));
        stripes.fill(Qt::white);
        for (int x = 0; x < 200; x += 12)
            stripes.fill(QRect(x, 0, 6, 100), Qt::black);
        RebuiltStroke painter;
        painter.begin(&stripes, Qt::blue, settings, at(20.5, 50.5, 1.0));
        for (double x = 22.5; x <= 180.5; x += 2.0)
            painter.add(at(x, 50.5, 1.0));
        painter.end();
        int least = 255, most = 0;
        for (int x = 94; x < 106; ++x) {
            least = std::min(least, int(toUnorm8(stripes.pixel(x, 50))[1]));
            most = std::max(most, int(toUnorm8(stripes.pixel(x, 50))[1]));
        }
        return most - least;
    };
    EXPECT_EQ(contrastAt(0), 255) << "no blur at all";
    EXPECT_LT(contrastAt(25), 255);
    EXPECT_LT(contrastAt(50), contrastAt(25));
    EXPECT_LT(contrastAt(100), contrastAt(50));
}

// A light Brush landing half on a colour and half on an empty part lays that colour thinned, not its own: the empty part counts as
// the colour it carries, as in the reference's preview. On an empty layer that's its
// own colour, so a light stroke there still lays pale paint.
TEST(MixingRules, ALightStartOverAColourAndEmptyLaysThatColourThinned) {
    TiledImage layer(QSize(200, 100));
    layer.fill(QRect(0, 50, 200, 50), Qt::green);
    BrushSettings settings = defaultSettings(ToolType::Brush);
    settings.size = 300;
    RebuiltStroke painter;
    painter.begin(&layer, Qt::blue, settings, at(40.5, 50.5, 0.3));
    for (double x = 42.5; x <= 80.5; x += 2.0)
        painter.add(at(x, 50.5, 0.3));
    painter.end();
    const auto above = toUnorm8(layer.pixel(60, 44));
    EXPECT_GT(above[3], 20) << "it lays something over the empty part";
    EXPECT_GT(above[1], 200 * above[3] / 255) << "green";
    EXPECT_LT(above[2], 10) << "with none of its own blue";

    TiledImage empty(QSize(200, 100));
    RebuiltStroke onEmpty;
    onEmpty.begin(&empty, Qt::blue, settings, at(40.5, 50.5, 0.3));
    for (double x = 42.5; x <= 80.5; x += 2.0)
        onEmpty.add(at(x, 50.5, 0.3));
    onEmpty.end();
    const auto pale = toUnorm8(empty.pixel(60, 50));
    EXPECT_GT(pale[3], 20) << "pale paint on an empty layer";
    EXPECT_GT(pale[2], pale[1]) << "of its own colour";
}

// Feedback: "Blur width and density seem to do the same thing." In the reference Blur Width sets how soft the stripes go, and
// Density how much of the blur is laid over them, so they keep their edges and lose contrast. A box
// blur as wide as the stripes' repeat had flattened them whole at any width.
TEST(BlurTool, WidthSoftensTheEdgesAndDensityKeepsThem) {
    struct Stripes {
        int contrast;
        int steepest;  // the largest step from one pixel to the next
    };
    const auto blurredAt = [](int width, int density) {
        BrushSettings settings = defaultSettings(ToolType::Blur);
        settings.blurWidth = width;
        settings.density = density;
        TiledImage stripes(QSize(200, 100));
        stripes.fill(Qt::white);
        for (int x = 0; x < 200; x += 12)
            stripes.fill(QRect(x, 0, 6, 100), Qt::black);
        RebuiltStroke painter;
        painter.begin(&stripes, Qt::blue, settings, at(20.5, 50.5, 1.0));
        for (double x = 22.5; x <= 180.5; x += 2.0)
            painter.add(at(x, 50.5, 1.0));
        painter.end();
        Stripes s{0, 0};
        int least = 255, most = 0;
        for (int x = 94; x < 118; ++x) {
            const int v = toUnorm8(stripes.pixel(x, 50))[1];
            least = std::min(least, v);
            most = std::max(most, v);
            s.steepest = std::max(s.steepest, std::abs(v - int(toUnorm8(stripes.pixel(x + 1, 50))[1])));
        }
        s.contrast = most - least;
        return s;
    };
    const Stripes narrow = blurredAt(25, 100), wide = blurredAt(50, 100);
    EXPECT_GT(narrow.contrast, 60) << "a narrow blur leaves the stripes";
    EXPECT_LT(narrow.steepest, narrow.contrast * 2 / 3) << "but soft";
    EXPECT_LT(wide.contrast, narrow.contrast / 2) << "a wider one takes more away";
    EXPECT_GT(wide.contrast, 0);
    EXPECT_LT(wide.steepest, wide.contrast * 2 / 3) << "and is soft too, where a box flattened them into steps";
    for (const int density : {25, 50}) {
        const Stripes thin = blurredAt(100, density);
        EXPECT_GT(thin.contrast, 80) << density;
        EXPECT_GE(thin.steepest, thin.contrast * 9 / 10) << "the edges stay sharp at Density " << density;
    }
    EXPECT_GT(blurredAt(100, 25).contrast, blurredAt(100, 50).contrast) << "less Density, less blur laid";
}

// Blur lays no colour. An even colour blurs to itself, and over stripes it softens them, more at full
// pressure, its Gaussian never quite flattening them at the starting Blur Width.
TEST(BlurTool, ItSoftensWhatsUnderAndLaysNoColour) {
    const BrushSettings settings = defaultSettings(ToolType::Blur);
    const auto blurred = [&settings](TiledImage& layer, double pressure) {
        RebuiltStroke painter;
        painter.begin(&layer, Qt::blue, settings, at(20.5, 50.5, pressure));
        for (double x = 22.5; x <= 180.5; x += 2.0)
            painter.add(at(x, 50.5, pressure));
        painter.end();
    };
    TiledImage even(QSize(200, 100));
    even.fill(Qt::red);
    blurred(even, 1.0);
    EXPECT_EQ(toUnorm8(even.pixel(100, 50)), (std::array<std::uint8_t, 4>{255, 0, 0, 255})) << "an even colour stays";
    const auto contrastAfter = [&blurred](double pressure) {
        TiledImage stripes(QSize(200, 100));
        stripes.fill(Qt::white);
        for (int x = 0; x < 200; x += 12)
            stripes.fill(QRect(x, 0, 6, 100), Qt::black);
        blurred(stripes, pressure);
        int least = 255, most = 0;
        for (int x = 94; x < 106; ++x) {
            const int v = toUnorm8(stripes.pixel(x, 50))[1];
            least = std::min(least, v);
            most = std::max(most, v);
        }
        return most - least;
    };
    const int light = contrastAfter(0.5);
    const int full = contrastAfter(1.0);
    EXPECT_LT(light, 200);
    EXPECT_LT(full, light);
    EXPECT_GT(full, 3) << "one pass never quite flattens them";
}

namespace {

// A wide mixing tip, the size of the recorded slow strokes on a 3000 px canvas.
StampTip wideTip(double radius) {
    StampTip tip;
    tip.radius = radius;
    tip.edge = 0;  // the softest: the most stamps in a dab
    tip.rimShare = 0.5;
    tip.spacingShare = 0.05;
    return tip;
}

// What a mixing tip leaves on a painted field, or an empty layer, laid on the stroke's workers or on one thread, per pixel or on the grid.
std::map<std::pair<int, int>, std::vector<Channel>> paintedWide(TipMixing::Kind kind, double radius, bool onWorkers,
                                                                bool memoryPressed = false, int samples = 40, bool onGrid = false,
                                                                bool overField = true, bool wetDetail = true,
                                                                std::map<std::pair<int, int>, std::vector<float>>* outlines = nullptr) {
    TiledImage field(QSize(1800, 1100));
    if (overField) {
        field.fill(QRect(0, 0, 900, 1100), Qt::red);
        field.fill(QRect(900, 0, 900, 1100), Qt::blue);
        field.fill(QRect(300, 200, 1200, 120), QColor(255, 255, 255, 90));
    }
    TipMixing mixing;
    mixing.kind = kind;
    mixing.onGrid = onGrid;
    mixing.wetDetail = wetDetail;
    mixing.blending = 0.5;
    mixing.dilution = 0.3;
    mixing.persistence = 0.5;
    if (kind == TipMixing::Kind::WaterColor)
        mixing.wetHold = 0.6265;  // the old hold, TipMixing's default before: these tests still lay Water Color's held share
    if (kind == TipMixing::Kind::WaterColor || kind == TipMixing::Kind::Marker)
        mixing.wetBlurRadii = 0.25;  // TipMixing's old default: these tests blur and read around as they did
    if (kind == TipMixing::Kind::Blur) {
        mixing.blurPasses = 3;
        mixing.blurStrength = 1.0;
        mixing.wetBlurRadii = 0.4;
    }
    if (kind == TipMixing::Kind::Smudge) {
        mixing.smudgeShare = 0.75;
        mixing.smudgeReach = 0.75;
        mixing.smudgeFalloff = 0.7;
    }
    StrokePaint paint;
    paint.color = {0.2f, 0.3f, 0.6f};
    DabPainter painter(wideTip(radius), mixing, paint, field, onWorkers);
    if (memoryPressed)
        painter.setMemoryPressed([] { return true; });
    InputSample s = at(500.0, 500.0, 0.6);
    painter.begin(s, {1.0, 0.6});
    for (int i = 1; i <= samples; ++i) {
        // Forward along the field and, past 40 samples, back over where it's been.
        const int along = i <= 40 ? i : 80 - i;
        s.pos = QPointF(500.0 + along * 22.0, 500.0 + 120.0 * std::sin(along * 0.25));
        s.pressure = 0.5 + 0.4 * std::sin(i * 0.3);
        painter.add(s, {1.0, s.pressure});
    }
    painter.end();
    std::map<std::pair<int, int>, std::vector<Channel>> tiles;
    for (const TileCoord coord : painter.takeChangedTiles()) {
        if (const TilePtr tile = painter.tile(coord))
            tiles[{coord.x, coord.y}] = std::vector<Channel>(tile->px.begin(), tile->px.end());
        std::vector<float> outline(static_cast<std::size_t>(Tile::kSize * Tile::kSize));
        if (outlines && painter.outline(coord, outline.data()))
            (*outlines)[{coord.x, coord.y}] = std::move(outline);
    }
    return tiles;
}

}

// Laying a wide dab in bands of rows on the workers, reading the blur's rows there, keeping the blur's blocks from one dab to the
// next, and smudging a wide stamp's rows on the workers change no pixel: each is as one thread lays it.
TEST(DabPainterThreads, WideDabsOnTheWorkersGiveExactlyThePixelsOfOneThread) {
    for (const auto kind :
         {TipMixing::Kind::Brush, TipMixing::Kind::WaterColor, TipMixing::Kind::Marker, TipMixing::Kind::Blur, TipMixing::Kind::Smudge}) {
        const auto workers = paintedWide(kind, 280.0, true);
        const auto alone = paintedWide(kind, 280.0, false);
        ASSERT_GT(alone.size(), 40u) << int(kind);
        EXPECT_TRUE(workers == alone) << "kind " << int(kind);
    }
}

// Below kGridTwoFrom the Brush's and Water Color's grid is a pixel apart, and paints exactly what the per-pixel model does. Water
// Color's paint carries no detail of what's under, as the tool has it (shapeWater): the grid lays none.
TEST(DabPainterGrid, APixelApartItGivesThePerPixelModelsPixels) {
    constexpr double kBelowTwoApart = 230.0;  // px radius: a 460 px tip, under kGridTwoFrom
    constexpr int kSamples = 20;
    for (const auto kind : {TipMixing::Kind::Brush, TipMixing::Kind::WaterColor})
        for (const bool overField : {true, false}) {
            const auto perPixel = paintedWide(kind, kBelowTwoApart, true, false, kSamples, false, overField, false);
            const auto onGrid = paintedWide(kind, kBelowTwoApart, true, false, kSamples, true, overField, false);
            if (overField) {
                ASSERT_GT(perPixel.size(), 20u) << int(kind);
            }
            EXPECT_TRUE(onGrid == perPixel) << "kind " << int(kind) << (overField ? " over the field" : " on an empty layer");
        }
}

// The Brush's 5th Shape: stamps whole to their outline, a skirt past it shown inside the outlines of the three stamps either side,
// along paintedWide's curve, over paint or on an empty layer, per pixel or on the grid.
std::map<std::pair<int, int>, std::vector<Channel>> paintedSkirted(double radius, bool overField, bool onGrid) {
    constexpr double kFullStamp = 18964.0 / 65535.0;  // RebuiltStroke.cpp's kBrushFullStamp
    constexpr int kNeighbours = 3;
    constexpr float kRampWidth = 1.6f;  // the Brush's outline ramp, px
    StampTip tip = wideTip(radius);
    tip.edge = 4;
    tip.rimShare = 0.0;
    tip.rampWidth = kRampWidth;
    tip.skirtNeighbours = kNeighbours;
    tip.mostOpacity = kFullStamp;
    TiledImage field(QSize(1800, 1100));
    if (overField) {
        field.fill(QRect(0, 0, 900, 1100), Qt::red);
        field.fill(QRect(900, 0, 900, 1100), Qt::blue);
    }
    TipMixing mixing;
    mixing.kind = TipMixing::Kind::Brush;
    mixing.onGrid = onGrid;
    mixing.blending = 0.5;
    mixing.persistence = 0.5;
    mixing.emptyIsOwnColour = true;
    StrokePaint paint;
    paint.color = {0.2f, 0.3f, 0.6f};
    DabPainter painter(tip, mixing, paint, field, false);
    InputSample s = at(500.0, 500.0, 1.0);
    painter.begin(s, {1.0, 1.0});
    for (int i = 1; i <= 40; ++i) {
        s.pos = QPointF(500.0 + i * 22.0, 500.0 + 120.0 * std::sin(i * 0.25));
        painter.add(s, {1.0, 1.0});
    }
    painter.end();
    std::map<std::pair<int, int>, std::vector<Channel>> tiles;
    for (const TileCoord coord : painter.takeChangedTiles())
        if (const TilePtr tile = painter.tile(coord))
            tiles[{coord.x, coord.y}] = std::vector<Channel>(tile->px.begin(), tile->px.end());
    return tiles;
}

// The most any channel of the two differs, in 8-bit levels.
double skirtedGridAgainstPerPixel(double radius, bool overField) {
    const auto perPixel = paintedSkirted(radius, overField, false), onGrid = paintedSkirted(radius, overField, true);
    double most = 0.0;
    for (const auto& [coord, px] : perPixel) {
        const auto found = onGrid.find(coord);
        for (std::size_t c = 0; c < px.size(); ++c)
            most = std::max(most, std::abs(double(px[c]) - double(found == onGrid.end() ? 0 : found->second[c])) * 255.0 / kChannelOne);
    }
    return most;
}

// From kGridTwoFrom a skirted stamp's grid reads its skirts between the points, and works them out per pixel where an outline steps
// them at the stroke's side: on an empty layer within a level and a half of the per-pixel model, two, four and eight px apart, as inside
// the stroke the grid's cut is a ramp where the per-pixel model's is a step; over paint the grid's mixing keeps to the margin
// the Brush is held to, its faint-edge flecks (about 10 levels here, 7.5 for the same tip without the skirt).
TEST(DabPainterGrid, ASkirtedStampOnTheGridKeepsCloseToThePerPixelModel) {
    constexpr double kTwoApart = 300.0, kFourApart = 600.0,
                     kEightApart = 1600.0;  // px radii, past kGridTwoFrom, kGridFourFrom and kGridEightFrom
    // 8-bit levels: the margin over paint; on an empty layer one level, and half a level more for the skirt's ramp, 1.41 here
    constexpr double kEmptyLevels = 1.5, kOverPaintLevels = 14.0;
    for (const double radius : {kTwoApart, kFourApart, kEightApart}) {
        EXPECT_LE(skirtedGridAgainstPerPixel(radius, false), kEmptyLevels) << "radius " << radius << " on an empty layer";
        EXPECT_LE(skirtedGridAgainstPerPixel(radius, true), kOverPaintLevels) << "radius " << radius << " over the field";
    }
}

// The grid's read-back four and eight pixels at once, with SSE2 and AVX2 where the processor has them, gives exactly the pixels of
// one at a time: four apart from kGridFourFrom and eight apart from kGridEightFrom, over paint and on an empty layer.
TEST(DabPainterGrid, ItsReadBackSeveralPixelsAtOnceGivesThePixelsOfOneAtATime) {
    constexpr double kFourApart = 520.0, kEightApart = 1520.0;  // px radii: tips past kGridFourFrom and kGridEightFrom
    constexpr int kSamples = 6;
    for (const auto kind : {TipMixing::Kind::Brush, TipMixing::Kind::WaterColor, TipMixing::Kind::Marker})
        for (const double radius : {kFourApart, kEightApart})
            for (const bool overField : {true, false}) {
                setGridOnePixelAtATime(true);
                const auto oneAtATime = paintedWide(kind, radius, true, false, kSamples, true, overField);
                setGridOnePixelAtATime(false);
                const auto severalAtOnce = paintedWide(kind, radius, true, false, kSamples, true, overField);
                if (overField) {
                    ASSERT_GT(oneAtATime.size(), 20u) << int(kind);
                }
                EXPECT_TRUE(severalAtOnce == oneAtATime)
                    << "kind " << int(kind) << ", radius " << radius << (overField ? " over the field" : " on an empty layer");
            }
}

// A skirted stamp's marked pixels, which take their exact marks four at once where all four are reached, give exactly the pixels of one
// at a time, four and eight apart, over paint and on an empty layer.
TEST(DabPainterGrid, ASkirtedStampsReadBackSeveralPixelsAtOnceGivesThePixelsOfOneAtATime) {
    constexpr double kFourApart = 600.0, kEightApart = 1600.0;  // px radii, past kGridFourFrom and kGridEightFrom
    for (const double radius : {kFourApart, kEightApart})
        for (const bool overField : {true, false}) {
            setGridOnePixelAtATime(true);
            const auto oneAtATime = paintedSkirted(radius, overField, true);
            setGridOnePixelAtATime(false);
            const auto severalAtOnce = paintedSkirted(radius, overField, true);
            ASSERT_GT(oneAtATime.size(), 20u);
            EXPECT_TRUE(severalAtOnce == oneAtATime) << "radius " << radius << (overField ? " over the field" : " on an empty layer");
        }
}

// Under memory pressure the record of tiles a stroke has left is compressed and unpacked when it comes back over them: no pixel moves.
TEST(DabPainterThreads, PackingWhatAStrokeHasLeftMovesNoPixel) {
    for (const auto kind : {TipMixing::Kind::Brush, TipMixing::Kind::WaterColor, TipMixing::Kind::Marker, TipMixing::Kind::Blur}) {
        const auto packed = paintedWide(kind, 60.0, true, true, 76);
        const auto kept = paintedWide(kind, 60.0, true, false, 76);
        ASSERT_GT(kept.size(), 20u) << int(kind);
        EXPECT_TRUE(packed == kept) << "kind " << int(kind);
    }
}

// The Brush laid in groups along a straight line on an empty layer, at a Shape (0 to 4) and Sharpness (rimShare = 1 - Sharpness): how many
// pixels it covers at least half, and how many it leaves faint (alpha 0.01 to 0.25).
std::pair<int, int> brushLineCover(double radius, int edge, double rimShare) {
    constexpr double kFullStamp = 18964.0 / 65535.0;  // RebuiltStroke.cpp's kBrushFullStamp
    StampTip tip = wideTip(radius);
    tip.edge = edge;
    tip.rimShare = rimShare;
    tip.rampWidth = 1.6f;  // the Brush's outline ramp, px
    tip.skirtNeighbours = edge == 4 ? 3 : 0;
    tip.mostOpacity = kFullStamp;
    TiledImage field(QSize(600, 300));
    TipMixing mixing;
    mixing.kind = TipMixing::Kind::Brush;
    mixing.onGrid = true;
    mixing.emptyIsOwnColour = true;
    StrokePaint paint;
    paint.color = {0.2f, 0.3f, 0.6f};
    DabPainter painter(tip, mixing, paint, field, false);
    InputSample s = at(100.0, 150.3, 1.0);
    painter.begin(s, {1.0, 1.0});
    for (int i = 1; i <= 40; ++i) {
        s.pos = QPointF(100.0 + i * 10.0, 150.3);
        painter.add(s, {1.0, 1.0});
    }
    painter.end();
    int half = 0, faint = 0;
    for (const TileCoord coord : painter.takeChangedTiles())
        if (const TilePtr tile = painter.tile(coord))
            for (std::size_t i = 3; i < tile->px.size(); i += 4) {
                const double alpha = tile->px[i] / 65535.0;
                half += alpha >= 0.5;
                faint += alpha >= 0.01 && alpha < 0.25;
            }
    return {half, faint};
}

// On the 5th Shape the Brush's edge fades over a faint tail, as the reference's (on the C at 35 px and Sharpness 50 its faint
// fringe is 0.55 px wide along its outline; the earlier edge left 0.07 here), longer toward Sharpness 0; on the 4th the dab's edge blur
// widens the fringe toward 0 too (the earlier edge left it the same at any Sharpness). At a small size, where the reference's
// edges differ most.
TEST(DabPainterGroups, TheBrushsEdgeMovesWithSharpnessAsTheReferences) {
    constexpr double kRadius = 17.5;  // a 35 px Brush
    constexpr double kSides = 2.0 * 400.0;  // px of the line's two sides
    constexpr double kLeastFringe = 0.3;  // px wide
    const auto fifth0 = brushLineCover(kRadius, 4, 1.0), fifth50 = brushLineCover(kRadius, 4, 0.5),
               fifth100 = brushLineCover(kRadius, 4, 0.0);
    EXPECT_GT(fifth50.second, kLeastFringe * kSides);
    EXPECT_GT(fifth0.second, fifth50.second);
    EXPECT_GT(fifth50.second, fifth100.second);
    const auto fourth0 = brushLineCover(kRadius, 3, 1.0), fourth100 = brushLineCover(kRadius, 3, 0.0);
    EXPECT_GT(fourth0.second, fourth100.second);
}

// A tile whose record is packed away still gives its outline, for the paper to break the stroke's edge: the same numbers. On
// Smudge, which keeps its record a dab at a time; the Brush and Water Color laid in groups keep none.
TEST(DabPainterThreads, APackedTileStillGivesItsOutline) {
    std::map<std::pair<int, int>, std::vector<float>> packed, kept;
    paintedWide(TipMixing::Kind::Smudge, 60.0, true, true, 76, false, true, true, &packed);
    paintedWide(TipMixing::Kind::Smudge, 60.0, true, false, 76, false, true, true, &kept);
    ASSERT_GT(kept.size(), 20u);
    EXPECT_TRUE(packed == kept);
}

// The Marker lays a whole mark without working it out inside solidReach, so there limitedMark must give exactly paint 1 and
// shape 1, for every edge, with and without a rim, and with neighbours of other sizes.
TEST(DabPainterThreads, InsideItsSolidReachAStampsMarkIsWhole) {
    std::deque<Stamp> stamps;
    const double radii[] = {500.0, 480.0, 510.0, 40.0, 3.0, 500.0, 200.0};
    const double bands[] = {12.0, 1.0, 40.0, 6.0, 1.0, 300.0, 2.5};
    const double ramps[] = {1.0, 3.0, 0.5, 2.0, 1.0, 1.0, 8.0};
    for (std::size_t k = 0; k < std::size(radii); ++k) {
        Stamp s;
        s.centre = QPointF(1000.0 + 6.3 * double(k), 900.0 + 1.7 * double(k));
        s.radius = static_cast<float>(radii[k]);
        s.band = static_cast<float>(bands[k]);
        s.rampWidth = static_cast<float>(ramps[k]);
        s.easedRamp = k % 2 == 1;
        s.opacity = static_cast<float>(0.4 + 0.08 * double(k));
        stamps.push_back(s);
    }
    int checked = 0;
    for (std::size_t i = 0; i < stamps.size(); ++i) {
        const double reach = solidReach(stamps, i);
        if (reach <= 0.0)
            continue;
        for (int edge = 0; edge <= 4; ++edge)
            for (int a = 0; a < 64; ++a)
                for (const double share : {0.0, 0.3, 0.7, 0.99, 0.999999}) {
                    const double angle = a * 0.0981747704;
                    const QPointF at = stamps[i].centre + QPointF(std::cos(angle), std::sin(angle)) * (reach * share);
                    const StampMark mark = limitedMark(
                        stamps, i, edge, static_cast<float>(std::hypot(at.x() - stamps[i].centre.x(), at.y() - stamps[i].centre.y())));
                    ASSERT_EQ(mark.paint, 1.0) << "stamp " << i << " edge " << edge << " at " << share;
                    ASSERT_EQ(mark.shape, 1.0) << "stamp " << i << " edge " << edge << " at " << share;
                    ++checked;
                }
    }
    EXPECT_GT(checked, 5000);
    Stamp blurred = stamps.front();
    blurred.edgeBlur = 2.0;
    EXPECT_GT(solidReach(std::deque<Stamp>{blurred}, 0), 0.0) << "at floats a blurred outline's most rounds to whole";
}

// The Pencil and the Eraser lay a stamp's mark without working it out inside its deepReach: there limitedMark gives exactly
// the mark deepReach says, for every edge, with blurred outlines and ramps, rims and neighbours of other sizes.
TEST(DabPainterThreads, InsideItsDeepReachAStampsMarkIsTheSame) {
    std::deque<Stamp> stamps;
    const double radii[] = {500.0, 480.0, 40.0, 3.0, 500.0, 200.0, 120.0};
    const double bands[] = {12.0, 1.0, 6.0, 1.0, 300.0, 2.5, 30.0};
    const double blurs[] = {2.0, 0.0, 1.5, 0.0, 9.0, 0.0, 0.7};
    for (std::size_t k = 0; k < std::size(radii); ++k) {
        Stamp s;
        s.centre = QPointF(1000.0 + 6.3 * double(k), 900.0 + 1.7 * double(k));
        s.radius = static_cast<float>(radii[k]);
        s.band = static_cast<float>(bands[k]);
        s.edgeBlur = static_cast<float>(blurs[k]);
        s.rampWidth = static_cast<float>(1.0 + double(k % 3));
        s.easedRamp = k % 2 == 1;
        stamps.push_back(s);
    }
    int checked = 0;
    for (std::size_t i = 0; i < stamps.size(); ++i) {
        StampMark whole;
        const double reach = deepReach(stamps, i, whole) - kDeepMargin;
        if (reach <= 0.0)
            continue;
        for (int edge = 0; edge <= 4; ++edge)
            for (int a = 0; a < 64; ++a)
                for (const double share : {0.0, 0.3, 0.7, 0.99, 0.999999}) {
                    const double angle = a * 0.0981747704;
                    const QPointF at = stamps[i].centre + QPointF(std::cos(angle), std::sin(angle)) * (reach * share);
                    const StampMark mark = limitedMark(
                        stamps, i, edge, static_cast<float>(std::hypot(at.x() - stamps[i].centre.x(), at.y() - stamps[i].centre.y())));
                    ASSERT_EQ(mark.paint, whole.paint) << "stamp " << i << " edge " << edge << " at " << share;
                    ASSERT_EQ(mark.shape, whole.shape) << "stamp " << i << " edge " << edge << " at " << share;
                    ++checked;
                }
    }
    EXPECT_GT(checked, 5000);
}
