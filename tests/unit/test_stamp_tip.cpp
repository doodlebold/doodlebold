#include "core/brush/BrushSettings.h"
#include "core/document/Document.h"
#include "core/paint/StrokeController.h"
#include "core/raster/Channel.h"
#include "core/raster/TiledImage.h"
#include "core/stroke/PaperGrain.h"
#include "core/stroke/RebuiltStroke.h"
#include "core/stroke/StampForm.h"
#include "core/stroke/StampTip.h"
#include "core/stroke/StrokeMerge.h"

#include <gtest/gtest.h>

#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <limits>
#include <numbers>

using namespace doodlebold;

namespace {

InputSample pen(double x, double y, double pressure) {
    InputSample sample;
    sample.pos = QPointF(x, y);
    sample.pressure = pressure;
    sample.kind = PointerKind::Pen;
    return sample;
}

// The neutral starting values on a tool that lays colour, with the plain form: the round tip with no tool's shaping or mixing.
BrushSettings plainTip() {
    BrushSettings settings = defaultSettings(ToolType::PixelArtPen);
    settings.tool = ToolType::Smudge;
    settings.form = RebuiltStroke::kPlainForm;
    return settings;
}

// Plain stamps without mixing: the round tip with no tool's shaping, since every tool now shapes or mixes its own.
BrushSettings stamped(int size, int density = 100) {
    BrushSettings settings = plainTip();
    settings.size = size;
    settings.density = density;
    settings.pressureSize = false;
    settings.pressureDensity = false;
    return settings;
}

double alphaAt(const TiledImage& image, int x, int y) {
    return image.pixel(x, y)[3] / static_cast<double>(kChannelOne);
}

TiledImage straightLine(const BrushSettings& settings, double step = 7.0) {
    TiledImage image(QSize(600, 200));
    RebuiltStroke stroke;
    stroke.begin(&image, Qt::black, settings, pen(20.0, 100.5, 1.0));
    for (double x = 20.0 + step; x <= 580.0; x += step)
        stroke.add(pen(x, 100.5, 1.0));
    stroke.end();
    return image;
}

// A circle of radius 100 drawn through twelve samples, a 2 px line at full pressure.
TiledImage ring(ToolType tool, bool plain = false, int stabilizer = -1) {
    BrushSettings settings = plain ? plainTip() : defaultSettings(tool);
    if (stabilizer >= 0)
        settings.stabilizer = StabilizerLevel{stabilizer};
    settings.size = 20;
    settings.pressureSize = false;
    TiledImage image(QSize(300, 300));
    RebuiltStroke stroke;
    const auto onRing = [](int step) {
        const double angle = step * std::numbers::pi / 6.0;
        return pen(150.0 + 100.0 * std::cos(angle), 150.0 + 100.0 * std::sin(angle), 1.0);
    };
    stroke.begin(&image, Qt::black, settings, onRing(0));
    for (int step = 1; step <= 12; ++step)
        stroke.add(onRing(step));
    stroke.end();
    return image;
}

TiledImage click(const BrushSettings& settings) {
    TiledImage image(QSize(200, 200));
    RebuiltStroke stroke;
    stroke.begin(&image, Qt::black, settings, pen(100.5, 100.5, 1.0));
    stroke.end();
    return image;
}

}

TEST(Stamps, EveryToolButTheMixingBrushLaysThem) {
    RoundTip plain;
    EXPECT_TRUE(laysStamps(plain));
    RoundTip mixing;
    mixing.mixing.kind = TipMixing::Kind::Brush;
    EXPECT_FALSE(laysStamps(mixing));
}

// From fullCoverageFrom on, coverageOf's min(1, ...) is 1, for every density past it, so skipping expm1 there gives the same coverage:
// every float up to where expm1f is -1 is checked, for the Pencil's, the Eraser's and two soft tools' fullAt (A1, A2).
TEST(Stamps, CoverageIsFullFromWhereItSaysForEveryDensityPast) {
    const float pencil = static_cast<float>(-std::expm1(-12.0));
    const float eraser = static_cast<float>(-std::expm1(-1.5 * std::pow(8.0, 0.8)) * 0.99);
    for (const float fullAt : {pencil, eraser, static_cast<float>(-std::expm1(-1.5)), 0.9f}) {
        const auto full = [fullAt](float perShape) { return -std::expm1(-perShape) / fullAt >= 1.0f; };
        const float from = fullCoverageFrom(fullAt);
        ASSERT_TRUE(std::isfinite(from));
        EXPECT_FALSE(full(std::nextafter(from, 0.0f)));
        int notFull = 0;
        for (float x = from; std::expm1(-x) > -1.0f; x = std::nextafter(x, 100.0f))
            notFull += full(x) ? 0 : 1;
        EXPECT_EQ(notFull, 0) << fullAt;
        const double inDoubles = fullCoverageFromInDoubles(fullAt);
        const auto fullInDoubles = [fullAt](double perShape) { return -std::expm1(-perShape) / fullAt >= 1.0; };
        EXPECT_FALSE(fullInDoubles(std::nextafter(inDoubles, 0.0)));
        double x = inDoubles;
        for (int i = 0; i < 1 << 22; ++i, x = std::nextafter(x, 100.0))
            notFull += fullInDoubles(x) ? 0 : 1;
        EXPECT_EQ(notFull, 0) << fullAt;
    }
    EXPECT_EQ(fullCoverageFrom(1.5f), std::numeric_limits<float>::infinity());
}

// The merge's inline rounding gives std::lround's integer for every float the merge's channels can be, |x| up to 2^17: all of them
// are checked, each sign (A2).
TEST(Stamps, TheMergesRoundingIsLroundsForEveryFloatItCanMeet) {
    const std::uint32_t last = std::bit_cast<std::uint32_t>(131072.0f);
    std::int64_t differ = 0;
    for (std::uint32_t bits = 0; bits <= last; ++bits) {
        const float x = std::bit_cast<float>(bits);
        differ += roundToNearest(x) != std::lround(x);
        differ += roundToNearest(-x) != std::lround(-x);
    }
    EXPECT_EQ(differ, 0);
}

// The merge lays a pixel's four channels at once where it has SSE2: the same pixels as channel by channel, over and erasing, for
// coverages all through 0 to 1 over pixels from empty to solid and paint from clear to opaque.
TEST(Stamps, TheMergesFourChannelsAtOnceGiveThePixelsOfOneAtATime) {
    constexpr std::uint32_t kCoverageStride = 251;  // float bits between the coverages tried: about 4 million of them from 0 to 1
    const std::uint32_t last = std::bit_cast<std::uint32_t>(1.0f);
    const std::array<std::array<Channel, 4>, 5> belows{
        {{0, 0, 0, 0}, {65535, 65535, 65535, 65535}, {1200, 30000, 64000, 65535}, {0, 0, 9, 40000}, {32767, 32768, 32769, 32768}}};
    const std::array<std::pair<std::array<float, 3>, float>, 3> paints{
        {{{0.2f, 0.3f, 0.6f}, 1.0f}, {{0.0f, 0.1f, 0.25f}, 0.25f}, {{1.0f, 1.0f, 1.0f}, 1.0f}}};
    std::int64_t differ = 0;
    for (std::uint32_t bits = 0; bits <= last; bits = bits + kCoverageStride > last && bits < last ? last : bits + kCoverageStride) {
        const float coverage = std::bit_cast<float>(bits);
        for (const auto& below : belows) {
            for (const auto& [premultiplied, alpha] : paints) {
                std::array<Channel, 4> atOnce = below, byChannel = below;
                overPixelForTesting(atOnce.data(), premultiplied, alpha, coverage, false);
                overPixelForTesting(byChannel.data(), premultiplied, alpha, coverage, true);
                differ += atOnce != byChannel;
            }
            std::array<Channel, 4> atOnce = below, byChannel = below;
            erasePixelForTesting(atOnce.data(), coverage, false);
            erasePixelForTesting(byChannel.data(), coverage, true);
            differ += atOnce != byChannel;
        }
    }
    EXPECT_EQ(differ, 0);
}

TEST(Stamps, SpacingIsAShareOfEachStampsOwnDiameter) {
    EXPECT_DOUBLE_EQ(stampSpacing(50.0), 5.0);
    EXPECT_DOUBLE_EQ(stampSpacing(20.0), 2.0);
    EXPECT_DOUBLE_EQ(stampSpacing(1.0), 0.25) << "never closer than a quarter of a pixel";
}

TEST(Stamps, TheRimRunsFromTheCoreToNothing) {
    for (int edge = 0; edge < kEdgePresets; ++edge) {
        const Stamp stamp{QPointF(), 40.0, 30.0, 1.0};
        EXPECT_DOUBLE_EQ(stampCoverage(stamp, edge, 0.0), 1.0) << edge;
        EXPECT_DOUBLE_EQ(stampCoverage(stamp, edge, 10.0), 1.0) << "the core is solid, edge " << edge;
        EXPECT_DOUBLE_EQ(stampCoverage(stamp, edge, 40.5), 0.0) << edge;
        double last = 1.0;
        for (double d = 10.0; d <= 40.5; d += 0.5) {
            EXPECT_LE(stampCoverage(stamp, edge, static_cast<float>(d)), last + 1e-12) << edge << " at " << d;
            last = stampCoverage(stamp, edge, static_cast<float>(d));
        }
    }
}

TEST(Stamps, AFirmerEdgeHoldsOnLonger) {
    const Stamp stamp{QPointF(), 40.0, 30.0, 1.0};
    for (int edge = 1; edge < kEdgePresets; ++edge)
        EXPECT_GT(stampCoverage(stamp, edge, 30.0), stampCoverage(stamp, edge - 1, 30.0)) << edge;
}

TEST(Stamps, AHardStampsEdgeIsOneAntialiasedPixel) {
    const Stamp stamp{QPointF(), 20.0, 1.0, 1.0};
    EXPECT_DOUBLE_EQ(stampCoverage(stamp, 0, 19.5), 1.0);
    EXPECT_DOUBLE_EQ(stampCoverage(stamp, 0, 20.0), 0.5);
    EXPECT_DOUBLE_EQ(stampCoverage(stamp, 0, 20.5), 0.0);
}

// The Pencil: Curve Interpolation is on. With the Stabilizer on, halfway between two samples the arc is 3.4 px outside their
// chord; at Stabilizer 0 the stamps go straight from sample to sample, as the reference's Pencil does there. Checked between the
// fourth and fifth samples, away from the ends, where there's no sample on one side.
TEST(Stamps, ThePencilFollowsACurveThroughItsSamples) {
    const double arc = 100.0, chord = 100.0 * std::cos(std::numbers::pi / 12.0);
    const auto at = [](const TiledImage& image, double radius) {
        const double angle = 7.0 * std::numbers::pi / 12.0;
        return alphaAt(image, static_cast<int>(150.0 + radius * std::cos(angle)), static_cast<int>(150.0 + radius * std::sin(angle)));
    };
    const TiledImage pencil = ring(ToolType::Pencil, false, 3);
    EXPECT_GT(at(pencil, arc), 0.5);
    EXPECT_LT(at(pencil, chord), 0.05);
    const TiledImage straight = ring(ToolType::Pencil, false, 0);
    EXPECT_LT(at(straight, arc), 0.05) << "at Stabilizer 0 straight from sample to sample";
    EXPECT_GT(at(straight, chord), 0.5);
    const TiledImage plain = ring(ToolType::Smudge, true);
    EXPECT_LT(at(plain, arc), 0.05) << "other tools still go straight from sample to sample";
    EXPECT_GT(at(plain, chord), 0.5);
}

namespace {

// How many pixels across the top edge of a straight Pencil line fade from 10 % to 90 %: the whole side, which lies inside the image at
// this Size (on the reference's lines at 200 px: 60, 32, 18, 8 and 1 px from the 1st Shape to the 5th; trial).
int pencilEdgeFade(int edge, int sharpness = 50) {
    BrushSettings settings = defaultSettings(ToolType::Pencil);
    settings.size = 1000;
    settings.pressureSize = false;
    settings.edge = edge;
    settings.sharpness = sharpness;
    const TiledImage image = straightLine(settings);
    int fade = 0;
    for (int y = 0; y <= 100; ++y) {
        const double alpha = alphaAt(image, 300, y);
        fade += alpha > 0.1 && alpha < 0.9 ? 1 : 0;
    }
    return fade;
}

}

// The Pencil's Shape and Sharpness: the five Shape buttons go from very soft to hard, and Sharpness barely
// changes a soft one.
TEST(Stamps, ThePencilsShapeButtonsGoFromSoftToHard) {
    int softer = 1000;
    for (int edge = 0; edge < kEdgePresets; ++edge) {
        const int fade = pencilEdgeFade(edge);
        EXPECT_LT(fade, softer) << "Shape " << edge + 1;
        softer = fade;
    }
    EXPECT_GE(pencilEdgeFade(0), 10) << "about 30 % of the width";
    EXPECT_LE(pencilEdgeFade(kEdgePresets - 1), 2);
    EXPECT_NEAR(pencilEdgeFade(0, 0), pencilEdgeFade(0, 100), 1);
}

TEST(Stamps, AHardLinesEdgeStaysAPixelWide) {
    BrushSettings settings = stamped(200);
    settings.sharpness = 100;
    const TiledImage image = straightLine(settings);
    EXPECT_GT(alphaAt(image, 300, 109), 0.99);
    EXPECT_NEAR(alphaAt(image, 300, 110), 0.5, 0.1) << "overlapping stamps don't build up the outline";
    EXPECT_LT(alphaAt(image, 300, 111), 0.01);
}

TEST(Stamps, AClickIsOneStamp) {
    BrushSettings settings = stamped(200, 100);
    settings.sharpness = 100;
    const TiledImage image = click(settings);
    const double centre = alphaAt(image, 100, 100);
    EXPECT_GT(centre, 0.05);
    EXPECT_NEAR(alphaAt(image, 100, 109), centre, 0.002) << "one even disc";
    EXPECT_NEAR(alphaAt(image, 100, 110), centre / 2.0, 0.02) << "20 px across";
    EXPECT_LT(alphaAt(image, 100, 111), 0.001);
}

TEST(Stamps, HowOftenThePenReportsChangesLittle) {
    BrushSettings settings = stamped(300, 60);
    settings.sharpness = 30;
    const TiledImage often = straightLine(settings, 3.0);
    const TiledImage seldom = straightLine(settings, 17.0);
    for (const int y : {100, 105, 110, 114})
        EXPECT_NEAR(alphaAt(often, 300, y), alphaAt(seldom, 300, y), 0.01) << y;
}

TEST(Stamps, ASoftStampThatSticksOutIsLimitedByItsNeighbours) {
    BrushSettings settings = stamped(300, 100);
    settings.sharpness = 0;
    settings.pressureSize = true;
    settings.minSize = 0;
    settings.sensitivitySize = 100;
    TiledImage image(QSize(400, 200));
    RebuiltStroke stroke;
    stroke.begin(&image, Qt::black, settings, pen(20.0, 100.5, 0.3));
    for (double x = 25.0; x <= 380.0; x += 5.0)
        stroke.add(pen(x, 100.5, std::abs(x - 200.0) < 1.0 ? 1.0 : 0.3));
    stroke.end();
    EXPECT_LT(alphaAt(image, 200, 100 + 12), 0.05) << "the one wide sample doesn't show past the others";
}

TEST(Stamps, AnEvenSoftLineHasNoBands) {
    BrushSettings settings = stamped(1500, 60);
    settings.sharpness = 0;
    const TiledImage image = straightLine(settings);
    double lowest = 1.0;
    double highest = 0.0;
    for (int x = 250; x <= 350; ++x) {
        lowest = std::min(lowest, alphaAt(image, x, 100));
        highest = std::max(highest, alphaAt(image, x, 100));
    }
    EXPECT_LT(highest - lowest, 0.004);
}

// Stage 4: Density is what a whole line looks like, not a stamp's opacity.
TEST(StampDensity, ALongLineReachesDensityAlongItsCentre) {
    for (const int size : {30, 200, 1200}) {
        for (const int sharpness : {0, 50, 100}) {
            for (const int density : {20, 50, 80, 100}) {
                BrushSettings settings = stamped(size, density);
                settings.sharpness = sharpness;
                EXPECT_NEAR(alphaAt(straightLine(settings), 300, 100), density / 100.0, 0.01)
                    << size / 10 << " px, Sharpness " << sharpness << ", Density " << density;
            }
        }
    }
}

TEST(StampDensity, EveryEdgeReachesItToo) {
    for (int edge = 0; edge < kEdgePresets; ++edge) {
        BrushSettings settings = stamped(400, 60);
        settings.sharpness = 0;
        settings.edge = edge;
        EXPECT_NEAR(alphaAt(straightLine(settings), 300, 100), 0.6, 0.01) << edge;
    }
}

TEST(StampDensity, ASingleClickIsMuchLighterThanALine) {
    for (const int density : {50, 100}) {
        BrushSettings settings = stamped(200, density);
        settings.sharpness = 50;
        EXPECT_LT(alphaAt(click(settings), 100, 100), alphaAt(straightLine(settings), 300, 100) * 0.7) << density;
    }
}

TEST(StampDensity, AFullLineIsFullInSixteenBits) {
    BrushSettings settings = stamped(200, 100);
    settings.sharpness = 100;
    EXPECT_EQ(straightLine(settings).pixel(300, 100)[3], kChannelOne);
}

TEST(StampDensity, StampsOverlappingInOneStrokeBuildUp) {
    BrushSettings settings = stamped(200, 40);
    settings.sharpness = 100;
    TiledImage image(QSize(400, 200));
    RebuiltStroke stroke;
    stroke.begin(&image, Qt::black, settings, pen(20.0, 100.5, 1.0));
    for (double x = 27.0; x <= 300.0; x += 7.0)
        stroke.add(pen(x, 100.5, 1.0));
    for (double x = 293.0; x >= 100.0; x -= 7.0)
        stroke.add(pen(x, 100.5, 1.0));
    stroke.end();
    EXPECT_GT(alphaAt(image, 200, 100), 0.6) << "going back over a 40 % line builds it up";
}

// As the end closes after a lift, its stamps only narrow, so its point fades instead of piling up into a bright nub.
TEST(StampEnds, TheClosingEndNeverGetsDenserTowardItsPoint) {
    BrushSettings settings = defaultSettings(ToolType::Brush);
    settings.size = 470;
    settings.minSize = 100;
    settings.pressureDensity = true;
    Document doc(QSize(700, 200), Qt::white);
    ASSERT_TRUE(doc.addLayer());
    StrokeController stroke(&doc);
    InputSample sample = pen(50.0, 100.5, 0.3);
    ASSERT_TRUE(stroke.begin(sample, Qt::black, settings, 11));
    for (int i = 1; i <= 60; ++i) {
        sample.pos = QPointF(50.0 + i * 9.0, 100.5);
        sample.pressure = i > 56 ? 0.75 * (60 - i) / 4.0 + 0.05 : 0.75;  // a quick lift
        sample.timestampMs = static_cast<std::uint64_t>(i) * 7;
        stroke.add(sample);
    }
    stroke.finish();
    const TiledImage& layer = doc.currentLayer()->pixels;
    int point = 600;
    while (point > 0 && alphaAt(layer, point, 100) < 0.01)
        --point;
    double nearest = 0.0;
    for (int x = point - 10; x <= point; ++x)
        nearest = std::max(nearest, alphaAt(layer, x, 100));
    EXPECT_LE(nearest, alphaAt(layer, point - 25, 100) + 0.005) << "the last 10 px before the point, at " << point;
}

// Erasing, Invert for Transparency turns a blot map over about its top and bottom: a map whose rows are greys 0 and 128,
// 16 px wide, turned so is the map with its rows swapped, as it stands; turned about black every part of it would be lighter than 128.
TEST(BlotMaps, ErasingTurnsAMapOverAboutItsTopAndBottom) {
    QImage map(16, 2, QImage::Format_Grayscale8);
    QImage swapped(16, 2, QImage::Format_Grayscale8);
    for (int x = 0; x < 16; ++x) {
        map.setPixel(x, 0, qRgb(0, 0, 0));
        map.setPixel(x, 1, qRgb(128, 128, 128));
        swapped.setPixel(x, 0, qRgb(128, 128, 128));
        swapped.setPixel(x, 1, qRgb(0, 0, 0));
    }
    const StampForm turned(map, 1.0, 1.0, true, true, true, false, false, true);
    const StampForm plain(swapped, 1.0, 1.0, false, true, true);
    for (int y = 0; y < 2; ++y)
        for (const float distance : {0.0f, 5.0f, 10.0f})
            EXPECT_FLOAT_EQ(turned.blotShape(distance, 10.0f, 3, y, kEdgePresets - 1),
                            plain.blotShape(distance, 10.0f, 3, y, kEdgePresets - 1))
                << y << ", " << distance;
}

// A paper turned for erasing: its top is the lowest of the lightest greys of its columns taken 8 apart, so one such set held at
// 160 tops the paper at 160 (rows held low don't); its bottom is its darkest grey when its width is a whole number of 8, else black.
TEST(PaperGrain, ErasingTurnsAPaperAboutItsTopAndBottom) {
    QImage capped(16, 16, QImage::Format_Grayscale8);
    for (int y = 0; y < 16; ++y)
        for (int x = 0; x < 16; ++x) {
            const int grey = x % 8 == 3 ? 160 : 60 + 12 * y;  // a floor of 60, light rows, every 8th column from 3 held at 160
            capped.setPixel(x, y, qRgb(grey, grey, grey));
        }
    const TurnedRange range = turnedRangeOf(capped);
    EXPECT_FLOAT_EQ(range.top, 160.0f / 255.0f);
    EXPECT_FLOAT_EQ(range.bottom, 60.0f / 255.0f);
    EXPECT_FLOAT_EQ(turnedRangeOf(capped.copy(0, 0, 15, 16)).bottom, 0.0f) << "15 px wide: the bottom is black";
    EXPECT_FLOAT_EQ(range.turned(100.0f / 255.0f), 180.0f / 255.0f) << "160 - 100 + 2 x 60";
    EXPECT_FLOAT_EQ(turnedRangeOf(capped.copy(0, 0, 15, 16)).turned(200.0f / 255.0f), 0.0f) << "above its top with no bottom, nothing";
}
