#include "core/brush/BrushResources.h"
#include "core/document/Document.h"
#include "core/paint/StrokeController.h"
#include "core/paint/StrokeMemory.h"
#include "core/paint/StrokeSmoother.h"
#include "core/selection/SelectionOps.h"
#include "core/stroke/RebuiltStroke.h"
#include "core/stroke/StampTip.h"
#include "core/stroke/StrokeMerge.h"
#include "core/stroke/StrokePainter.h"
#include "core/stroke/StrokeReplay.h"
#include "core/stroke/StrokeWorker.h"

#include "core/util/ThreadPriority.h"

#include <gtest/gtest.h>

#include <QDir>
#include <QTemporaryDir>

#include <thread>

#ifdef __linux__
#include <sys/resource.h>
#include <sys/syscall.h>
#include <unistd.h>
#endif

#include <atomic>
#include <cmath>
#include <functional>
#include <memory>
#include <numbers>
#include <tuple>
#include <unordered_set>
#include <utility>

using namespace doodlebold;

namespace {

InputSample pen(double x, double y, double pressure) {
    InputSample sample;
    sample.pos = QPointF(x, y);
    sample.pressure = pressure;
    sample.kind = PointerKind::Pen;
    return sample;
}

// A line's middle with the Pencil's 5th Shape, its starting Shape: as many whole stamps as the line holds there, short of full by what
// a stamp leaves uncovered, as the reference's line's middle is 65487. What's left of what was under is the rest.
constexpr int kPencilLineMiddle = 65492;
constexpr int kPencilLineLeaves = kChannelOne - kPencilLineMiddle;

BrushSettings pencil(int size = 200) {
    BrushSettings settings = defaultSettings(ToolType::Pencil);
    settings.size = size;
    settings.sensitivitySize = 100;
    return settings;
}

// The neutral starting values on a tool that lays colour, with the plain form: the round tip with no tool's shaping or mixing.
BrushSettings plainTip() {
    BrushSettings settings = defaultSettings(ToolType::PixelArtPen);
    settings.tool = ToolType::Smudge;
    settings.form = RebuiltStroke::kPlainForm;
    return settings;
}

// Plain stamps without mixing: the round tip with no tool's shaping, since every tool now shapes or mixes its own.
BrushSettings plainStamps(int size) {
    BrushSettings settings = plainTip();
    settings.size = size;
    settings.pressureSize = true;
    return settings;
}

void drawLine(StrokeController& stroke, const BrushSettings& settings, double pressure = 1.0) {
    ASSERT_TRUE(stroke.begin(pen(20, 40.5, pressure), Qt::black, settings));
    for (int x = 30; x <= 180; x += 10)
        stroke.add(pen(x, 40.5, pressure));
}

int alphaAt(const Document& doc, int x, int y) {
    return doc.currentLayer()->pixels.pixel(x, y)[3];
}

int widthAt(const Document& doc, int x) {
    int width = 0;
    for (int y = 0; y < 80; ++y)
        width += alphaAt(doc, x, y) > kChannelOne / 2 ? 1 : 0;
    return width;
}

InputSample timed(double x, double pressure, std::uint64_t timeMs) {
    InputSample sample = pen(x, 40.5, pressure);
    sample.timestampMs = timeMs;
    return sample;
}

// A wide straight stroke of even width, at the given sharpness and density.
Document* paintedBand(Document& doc, int sharpness, int density) {
    BrushSettings settings = plainStamps(800);
    settings.pressureSize = false;
    settings.sharpness = sharpness;
    settings.density = density;
    StrokeController stroke(&doc);
    drawLine(stroke, settings);
    stroke.finish();
    return &doc;
}

double centreCoverage(int sharpness, int density) {
    Document doc(QSize(200, 80), Qt::white);
    paintedBand(doc, sharpness, density);
    return alphaAt(doc, 100, 40) / static_cast<double>(kChannelOne);
}

// The coverage along a stroke's centre at an even pressure.
double pressedCoverage(double pressure, int density = 100, int sensitivity = 100, bool follows = true) {
    BrushSettings settings = pencil(800);
    settings.pressureSize = false;
    settings.pressureDensity = follows;
    settings.density = density;
    settings.sensitivityDensity = sensitivity;
    Document doc(QSize(200, 80), Qt::white);
    StrokeController stroke(&doc);
    drawLine(stroke, settings, pressure);
    stroke.finish();
    return alphaAt(doc, 100, 40) / static_cast<double>(kChannelOne);
}

// How many pixels the edge takes to fall from 90 % to 10 % of the centre.
int edgeRamp(int sharpness) {
    Document doc(QSize(200, 80), Qt::white);
    paintedBand(doc, sharpness, 100);
    int ramp = 0;
    for (int y = 40; y < 80; ++y) {
        const double coverage = alphaAt(doc, 100, y) / static_cast<double>(kChannelOne);
        ramp += coverage < 0.9 && coverage > 0.1 ? 1 : 0;
    }
    return ramp;
}

// The stroke's width at x, painted on a fresh layer and lifted.
int liftedWidth(const BrushSettings& settings, int x, double pressure = 0.8) {
    Document doc(QSize(200, 80), Qt::white);
    StrokeController stroke(&doc);
    drawLine(stroke, settings, pressure);
    stroke.finish();
    return widthAt(doc, x);
}

}

TEST(RebuiltStroke, CanPaintPlainRoundToolsThatLayColourOrErase) {
    EXPECT_TRUE(RebuiltStroke::canPaint(defaultSettings(ToolType::Pencil)));
    EXPECT_TRUE(RebuiltStroke::canPaint(defaultSettings(ToolType::Brush)));
    EXPECT_TRUE(RebuiltStroke::canPaint(defaultSettings(ToolType::Eraser)));
    EXPECT_TRUE(RebuiltStroke::canPaint(defaultSettings(ToolType::Clone))) << "with the Pencil's tip";
    EXPECT_TRUE(RebuiltStroke::canPaint(defaultSettings(ToolType::PixelArtPen))) << "a hard disc";
    EXPECT_TRUE(RebuiltStroke::canPaint(defaultSettings(ToolType::SelectionPen))) << "with the plain round tip";
    EXPECT_TRUE(RebuiltStroke::canPaint(defaultSettings(ToolType::SelectionEraser)));
    BrushSettings textured = defaultSettings(ToolType::Pencil);
    textured.texture = QStringLiteral("Paper");
    EXPECT_TRUE(RebuiltStroke::canPaint(textured)) << "the grain at the merge";
    textured.tool = ToolType::Brush;
    EXPECT_TRUE(RebuiltStroke::canPaint(textured)) << "the mixing tools' grain at the merge too";
    BrushSettings shaped = defaultSettings(ToolType::Pencil);
    shaped.form = QStringLiteral("Star");
    EXPECT_TRUE(RebuiltStroke::canPaint(shaped)) << "the stamp tools' outline";
    shaped.tool = ToolType::Brush;
    EXPECT_TRUE(RebuiltStroke::canPaint(shaped)) << "the mixing tools' dabs' outline";
}

TEST(RebuiltStroke, StaysInsideASelection) {
    Document doc(QSize(200, 80), Qt::white);
    ASSERT_TRUE(doc.setSelection(selection::rectangle(doc.size(), QRectF(50, 0, 60, 80), false)));
    StrokeController stroke(&doc);
    drawLine(stroke, pencil());
    stroke.finish();
    EXPECT_EQ(alphaAt(doc, 80, 40), kPencilLineMiddle);
    EXPECT_EQ(alphaAt(doc, 40, 40), 0);
    EXPECT_EQ(alphaAt(doc, 130, 40), 0);
}

TEST(RebuiltStroke, ErasingRemovesPaintAndLeavesNoEmptyTiles) {
    Document doc(QSize(200, 80), Qt::white);
    StrokeController stroke(&doc);
    drawLine(stroke, pencil(400));
    stroke.finish();
    BrushSettings eraser = defaultSettings(ToolType::Eraser);
    eraser.size = 800;
    eraser.pressureSize = false;
    // The 4th Shape, whose coverage clears to transparent; the 5th takes away the Pencil's deposits, as the reference's Eraser
    // does, and leaves a trace of a line's middle.
    constexpr int kClearingShape = kEdgePresets - 2;
    eraser.edge = kClearingShape;
    ASSERT_TRUE(stroke.begin(pen(-120, 40.5, 1.0), Qt::black, eraser));  // well past the paint: a stroke's ends are half as dense
    for (int x = -100; x <= 320; x += 20)
        stroke.add(pen(x, 40.5, 1.0));
    stroke.finish();
    EXPECT_EQ(alphaAt(doc, 100, 40), 0);
    EXPECT_EQ(doc.currentLayer()->pixels.pixel(100, 40)[0], 0);
    EXPECT_TRUE(doc.currentLayer()->pixels.contentBounds(true).isEmpty());
    Document clean(QSize(200, 80), Qt::white);
    StrokeController onEmpty(&clean);
    drawLine(onEmpty, eraser);
    onEmpty.finish();
    EXPECT_TRUE(clean.currentLayer()->pixels.isEmpty());
}

TEST(RebuiltStroke, PreservingOpacityOnlyRecoloursWhatsThere) {
    TiledImage image(QSize(200, 80));
    image.fill(QRect(0, 0, 200, 40), QColor(255, 0, 0, 128));
    const int alphaBefore = image.pixel(100, 30)[3];
    BrushSettings settings = pencil(800);
    settings.pressureSize = false;
    RebuiltStroke stroke;
    stroke.begin(&image, Qt::blue, settings, pen(20, 40, 1.0), true);
    for (int x = 30; x <= 180; x += 10)
        stroke.add(pen(x, 40, 1.0));
    stroke.end();
    const auto pixel = image.pixel(100, 39);
    EXPECT_EQ(pixel[3], alphaBefore);
    EXPECT_LE(pixel[0], kPencilLineLeaves);
    EXPECT_NEAR(pixel[2], alphaBefore * static_cast<double>(kPencilLineMiddle) / kChannelOne, 2);  // recoloured as far as the line covers
    EXPECT_EQ(image.pixel(100, 50)[3], 0);
}

TEST(RebuiltStroke, PaintsOnALayerMask) {
    TiledImage mask(QSize(200, 80));
    mask.fill(Qt::white);
    RebuiltStroke stroke;
    stroke.begin(&mask, Qt::black, pencil(), pen(20, 40, 1.0));
    for (int x = 30; x <= 180; x += 10)
        stroke.add(pen(x, 40, 1.0));
    stroke.end();
    // Laid sample by sample in whole numbers, the mask can keep a level more of white than the line leaves.
    EXPECT_LE(mask.pixel(100, 39)[0], kPencilLineLeaves + 1);
    EXPECT_EQ(mask.pixel(100, 39)[3], kChannelOne);
}

TEST(StrokePainter, PaintsTheSameAsTheRebuiltEngineWhereItCan) {
    TiledImage a(QSize(200, 80));
    TiledImage b(QSize(200, 80));
    StrokePainter painter;
    RebuiltStroke rebuilt;
    painter.begin(&a, Qt::black, pencil(), pen(20, 40, 1.0));
    rebuilt.begin(&b, Qt::black, pencil(), pen(20, 40, 1.0));
    for (int x = 30; x <= 180; x += 10) {
        painter.add(pen(x, 40, 1.0));
        rebuilt.add(pen(x, 40, 1.0));
    }
    painter.end();
    rebuilt.end();
    EXPECT_EQ(pixelHash(a), pixelHash(b));
}

// Every tool that paints strokes paints through RebuiltStroke; the Bucket, which paints none, lays nothing.
TEST(StrokePainter, EveryStrokeToolPaintsOnTheRebuiltEngine) {
    for (int i = 0; i <= static_cast<int>(ToolType::Bucket); ++i) {
        const auto tool = static_cast<ToolType>(i);
        EXPECT_EQ(RebuiltStroke::canPaint(defaultSettings(tool)), tool != ToolType::Bucket) << toolTypeKey(tool).toStdString();
    }
    TiledImage a(QSize(200, 80));
    StrokePainter painter;
    painter.begin(&a, Qt::black, defaultSettings(ToolType::Bucket), pen(20, 40, 1.0));
    painter.add(pen(100, 40, 1.0));
    EXPECT_TRUE(painter.end().empty());
    EXPECT_TRUE(a.isEmpty());
}

TEST(RebuiltStroke, PaintsWhileThePenMoves) {
    Document doc(QSize(200, 80), Qt::white);
    StrokeController stroke(&doc);
    drawLine(stroke, pencil());
    // Laid sample by sample in whole numbers, a level from the finished line's.
    EXPECT_NEAR(alphaAt(doc, 100, 40), kPencilLineMiddle, 1);
    stroke.finish();
}

TEST(RebuiltStroke, LiftingOnlyAddsTheLastStretch) {
    Document doc(QSize(200, 80), Qt::white);
    StrokeController stroke(&doc);
    drawLine(stroke, pencil());
    const auto painted = doc.currentLayer()->pixels;
    stroke.finish();
    for (int x = 20; x < 150; x += 3) {
        for (int y = 25; y < 56; y += 3)
            EXPECT_EQ(doc.currentLayer()->pixels.pixel(x, y), painted.pixel(x, y)) << x << ", " << y;
    }
}

TEST(RebuiltStroke, UndoAndRedoWork) {
    Document doc(QSize(200, 80), Qt::white);
    StrokeController stroke(&doc);
    drawLine(stroke, pencil());
    stroke.finish();
    const std::uint64_t painted = pixelHash(doc.currentLayer()->pixels);
    ASSERT_TRUE(doc.undo());
    EXPECT_EQ(pixelHash(doc.currentLayer()->pixels), pixelHash(TiledImage(QSize(200, 80))));
    ASSERT_TRUE(doc.redo());
    EXPECT_EQ(pixelHash(doc.currentLayer()->pixels), painted);
}

TEST(RebuiltStroke, CancelPutsThePixelsBack) {
    Document doc(QSize(200, 80), Qt::white);
    const int steps = doc.undoStack()->count();
    StrokeController stroke(&doc);
    drawLine(stroke, pencil());
    stroke.cancel();
    EXPECT_FALSE(doc.isPainting());
    EXPECT_TRUE(doc.currentLayer()->pixels.isEmpty());
    EXPECT_EQ(doc.undoStack()->count(), steps);
}

TEST(RebuiltStroke, PaintsOverWhatsThereWithoutBuildingUpBetweenEvents) {
    Document doc(QSize(200, 80), Qt::white);
    BrushSettings half = pencil(400);
    half.density = 50;
    half.pressureSize = false;
    StrokeController stroke(&doc);
    drawLine(stroke, half);
    stroke.finish();
    EXPECT_NEAR(alphaAt(doc, 100, 40) / static_cast<double>(kChannelOne), 0.5, 0.02);
}

TEST(RebuiltStroke, ASecondStrokeLaysOverTheFirst) {
    Document doc(QSize(200, 80), Qt::white);
    BrushSettings half = pencil(400);
    half.density = 50;
    half.pressureSize = false;
    StrokeController stroke(&doc);
    drawLine(stroke, half);
    stroke.finish();
    drawLine(stroke, half);
    stroke.finish();
    EXPECT_NEAR(alphaAt(doc, 100, 40) / static_cast<double>(kChannelOne), 0.75, 0.02);
}

TEST(RebuiltStroke, OpacityCapsTheStroke) {
    Document doc(QSize(200, 80), Qt::white);
    BrushSettings faint = pencil(400);
    faint.opacity = 128;
    StrokeController stroke(&doc);
    drawLine(stroke, faint);
    stroke.finish();
    EXPECT_NEAR(alphaAt(doc, 100, 40) / static_cast<double>(kChannelOne), 128.0 / 255.0, 0.01);
}

TEST(RebuiltStroke, WidthFollowsPressureWhenAskedTo) {
    BrushSettings settings = pencil(400);
    settings.pressureSize = true;
    EXPECT_GT(liftedWidth(settings, 100, 1.0), liftedWidth(settings, 100, 0.5));
    settings.pressureSize = false;
    EXPECT_EQ(liftedWidth(settings, 100, 1.0), liftedWidth(settings, 100, 0.5));
}

// The Pencil: width follows pressure in a nearly straight line, and full pressure is a touch under the Size.
TEST(RebuiltStroke, TheDefaultPencilsWidthFollowsPressureInAStraightLine) {
    BrushSettings settings = defaultSettings(ToolType::Pencil);
    settings.size = 400;
    EXPECT_NEAR(liftedWidth(settings, 100, 0.25), 9, 1);
    EXPECT_NEAR(liftedWidth(settings, 100, 0.5), 19, 1);
    EXPECT_NEAR(liftedWidth(settings, 100, 1.0), 39, 1);
}

TEST(RebuiltStroke, LowerSensitivityKeepsALightTouchThinner) {
    BrushSettings settings = pencil(400);
    const int straight = liftedWidth(settings, 100, 0.5);
    settings.sensitivitySize = 50;
    const int curved = liftedWidth(settings, 100, 0.5);
    settings.sensitivitySize = 200;
    const int eager = liftedWidth(settings, 100, 0.5);
    EXPECT_LT(curved, straight);
    EXPECT_GT(eager, straight);
    settings.sensitivitySize = 0;
    EXPECT_GE(liftedWidth(settings, 100, 1.0), 38) << "full pressure still reaches full width";
}

TEST(RebuiltStroke, ZeroSharpnessIsSoftAcrossHalfTheTip) {
    EXPECT_GE(edgeRamp(0), 18);  // of a radius of 40
    EXPECT_LE(edgeRamp(100), 2);
}

TEST(RebuiltStroke, SharpnessMovesTheEdgeSteadily) {
    int previous = edgeRamp(0);
    for (const int sharpness : {25, 50, 75, 100}) {
        const int ramp = edgeRamp(sharpness);
        EXPECT_LT(ramp, previous) << "at sharpness " << sharpness;
        previous = ramp;
    }
}

TEST(RebuiltStroke, ACentreStaysSolidAtAnySharpness) {
    for (const int sharpness : {0, 25, 50, 75, 100})
        EXPECT_NEAR(centreCoverage(sharpness, 100), 1.0, 0.01) << "at sharpness " << sharpness;
}

TEST(RebuiltStroke, DensityMeansTheSameAtAnySharpness) {
    for (const int sharpness : {0, 50, 100})
        EXPECT_NEAR(centreCoverage(sharpness, 50), 0.5, 0.02) << "at sharpness " << sharpness;
}

// Both start at Sharpness 50 %.
TEST(RebuiltStroke, ThePencilAndTheBrushStartAtHalfSharpness) {
    EXPECT_EQ(defaultSettings(ToolType::Pencil).sharpness, 50);
    EXPECT_EQ(defaultSettings(ToolType::Brush).sharpness, 50);
}

TEST(RebuiltStroke, PressureSetsHowMuchPaintTheCentreGets) {
    EXPECT_NEAR(pressedCoverage(1.0), 1.0, 0.01);
    EXPECT_NEAR(pressedCoverage(0.5), 0.5, 0.02);
    EXPECT_NEAR(pressedCoverage(0.25), 0.25, 0.02);
    EXPECT_NEAR(pressedCoverage(0.0), 0.0, 1e-6);
}

TEST(RebuiltStroke, DensityPressureFollowsTheDensitySetting) {
    EXPECT_NEAR(pressedCoverage(1.0, 60), 0.6, 0.02);
    EXPECT_NEAR(pressedCoverage(0.5, 60), 0.3, 0.02);
}

TEST(RebuiltStroke, PressureLeavesDensityAloneUnlessAsked) {
    EXPECT_NEAR(pressedCoverage(0.5, 100, 100, false), 1.0, 0.01);
}

TEST(RebuiltStroke, DensitySensitivityMovesTheCurve) {
    EXPECT_LT(pressedCoverage(0.5, 100, 50), pressedCoverage(0.5, 100, 100));
    EXPECT_GT(pressedCoverage(0.5, 100, 150), pressedCoverage(0.5, 100, 100));
    EXPECT_NEAR(pressedCoverage(1.0, 100, 50), 1.0, 0.01);
}

TEST(RebuiltStroke, DensityPressureDoesNotChangeWidth) {
    BrushSettings settings = pencil(400);
    settings.pressureSize = false;
    settings.pressureDensity = true;
    const auto paintedWidth = [&](double pressure) {
        Document doc(QSize(200, 80), Qt::white);
        StrokeController stroke(&doc);
        drawLine(stroke, settings, pressure);
        stroke.finish();
        int width = 0;
        for (int y = 0; y < 80; ++y)
            width += alphaAt(doc, 100, y) > 0 ? 1 : 0;
        return width;
    };
    EXPECT_NEAR(paintedWidth(0.3), paintedWidth(1.0), 1);
}

TEST(RebuiltStroke, PaintBuildsWithPressureAlongAStroke) {
    Document doc(QSize(220, 80), Qt::white);
    BrushSettings settings = pencil(400);
    settings.pressureSize = false;
    settings.pressureDensity = true;
    StrokeController stroke(&doc);
    ASSERT_TRUE(stroke.begin(pen(10, 40.5, 0.1), Qt::black, settings));
    for (int x = 20; x <= 210; x += 10)
        stroke.add(pen(x, 40.5, 0.1 + 0.9 * (x - 10) / 200.0));
    stroke.finish();
    int previous = 0;
    for (int x = 40; x <= 180; x += 20) {
        EXPECT_GT(alphaAt(doc, x, 40), previous) << "at x " << x;
        previous = alphaAt(doc, x, 40);
    }
}

TEST(RebuiltStroke, LightPassesBuildUp) {
    Document doc(QSize(200, 80), Qt::white);
    BrushSettings settings = pencil(400);
    settings.pressureSize = false;
    settings.pressureDensity = true;
    StrokeController stroke(&doc);
    drawLine(stroke, settings, 0.3);
    stroke.finish();
    const int once = alphaAt(doc, 100, 40);
    drawLine(stroke, settings, 0.3);
    stroke.finish();
    EXPECT_GT(alphaAt(doc, 100, 40), once);
}

TEST(RebuiltStroke, EraserPressureErasesLess) {
    Document doc(QSize(200, 80), Qt::white);
    StrokeController stroke(&doc);
    drawLine(stroke, pencil(800));
    stroke.finish();
    BrushSettings eraser = defaultSettings(ToolType::Eraser);
    eraser.size = 400;
    eraser.pressureSize = false;
    eraser.pressureDensity = true;
    drawLine(stroke, eraser, 0.5);
    stroke.finish();
    EXPECT_NEAR(alphaAt(doc, 100, 40) / static_cast<double>(kChannelOne), 0.5, 0.03);
}

// Density is what a line reaches, so a click, which is one stamp, is much lighter than a line at the same pressure.
TEST(RebuiltStroke, ADotUsesTheMostPressureAndIsLighterThanALine) {
    const auto dotAt = [](double first, double then) {
        TiledImage image(QSize(100, 100));
        BrushSettings settings = pencil(400);
        settings.pressureSize = false;
        settings.pressureDensity = true;
        RebuiltStroke stroke;
        stroke.begin(&image, Qt::black, settings, pen(50.5, 50.5, first));
        stroke.add(pen(50.5, 50.5, then));
        stroke.end();
        return image.pixel(50, 50)[3] / static_cast<double>(kChannelOne);
    };
    EXPECT_GT(dotAt(0.2, 0.5), dotAt(0.2, 0.2) * 1.5) << "the most pressure it reached";
    EXPECT_GT(dotAt(0.5, 0.2), dotAt(0.2, 0.2) * 1.5);
    EXPECT_LT(dotAt(0.5, 0.5), 0.2) << "a line at this pressure is 0.5";
}

// The rules give the Brush density following pressure, reaching full at about 60 %.
TEST(RebuiltStroke, TheBrushStartsWithDensityPressureAndThePencilWithout) {
    EXPECT_TRUE(defaultSettings(ToolType::Brush).pressureDensity);
    EXPECT_FALSE(defaultSettings(ToolType::Pencil).pressureDensity);
}

TEST(RebuiltStroke, ATaperToNothingLeavesNoStrayPixels) {
    BrushSettings settings = pencil(400);
    TiledImage image(QSize(220, 80));
    image.fill(Qt::white);
    RebuiltStroke stroke;
    stroke.begin(&image, Qt::black, settings, pen(10, 40.3, 1.0));
    for (int x = 20; x <= 200; x += 10)
        stroke.add(pen(x, 40.3, (200 - x) / 190.0));
    stroke.end();
    // Black over white can only darken, and any reading past where the line ends has to stay white.
    for (int y = 30; y < 50; ++y) {
        int previous = 0;
        for (int x = 150; x < 215; ++x) {
            const int value = image.pixel(x, y)[0];
            if (x > 203) {
                EXPECT_EQ(value, kChannelOne) << x << ", " << y;
            }
            if (y == 40 && x > 150) {
                EXPECT_GE(value + 2000, previous) << "lightens towards the tip at " << x;
            }
            if (y == 40)
                previous = value;
        }
    }
}

TEST(PencilDefaults, TheyAreTheSpecsStartingValues) {
    const BrushSettings pencil = defaultSettings(ToolType::Pencil);
    EXPECT_EQ(pencil.size, 100);
    EXPECT_EQ(pencil.sharpness, 50);
    EXPECT_EQ(pencil.edge, kEdgePresets - 1);
    EXPECT_EQ(pencil.density, 100);
    EXPECT_EQ(pencil.minSize, 0);
    EXPECT_EQ(pencil.minDensity, 0);
    EXPECT_TRUE(pencil.pressureSize);
    EXPECT_FALSE(pencil.pressureDensity);
    EXPECT_EQ(pencil.sensitivitySize, 100);
    EXPECT_EQ(pencil.sensitivityDensity, 100);
    EXPECT_FALSE(pencil.stabilizer.has_value()) << "Global";
}

// A tap is one stamp, at the most pressure it reached: full size, even, and lighter than a line.
TEST(StrokeStart, AHardTapIsOneFullSizeStamp) {
    BrushSettings settings = pencil(400);
    TiledImage image(QSize(100, 100));
    RebuiltStroke stroke;
    stroke.begin(&image, Qt::black, settings, pen(50.5, 50.5, 0.0));
    stroke.add(pen(50.5, 50.5, 1.0));
    stroke.end();
    EXPECT_GT(image.pixel(50, 50)[3], kChannelOne / 5);
    EXPECT_LT(image.pixel(50, 50)[3], kChannelOne);
    EXPECT_EQ(image.pixel(50, 35)[3], image.pixel(50, 50)[3]) << "18 px from the centre of a 20 px radius";
}

TEST(StrokeStart, ATapThatWobblesCountsAsATap) {
    BrushSettings settings = pencil(400);
    TiledImage image(QSize(100, 100));
    RebuiltStroke stroke;
    stroke.begin(&image, Qt::black, settings, pen(50.5, 50.5, 0.0));
    stroke.add(pen(51.0, 50.5, 1.0));
    stroke.add(pen(51.5, 50.8, 1.0));
    stroke.end();
    EXPECT_GT(image.pixel(50, 35)[3], kChannelOne / 5);
    EXPECT_EQ(image.pixel(50, 35)[3], image.pixel(50, 50)[3]) << "one stamp, not a short line";
}

TEST(PressureRange, AFirmPressCountsAsFull) {
    BrushSettings settings = pencil(400);
    TiledImage image(QSize(200, 80));
    RebuiltStroke stroke;
    stroke.setPressureRange({0.0, 0.8});
    stroke.begin(&image, Qt::black, settings, pen(20, 40.5, 0.8));
    for (int x = 30; x <= 180; x += 10)
        stroke.add(pen(x, 40.5, 0.8));
    stroke.end();
    EXPECT_GT(image.pixel(100, 23)[3], kChannelOne / 2) << "full width at a reading of 0.8";
}

TEST(PressureRange, BelowTheLowEndCountsAsNone) {
    BrushSettings settings = pencil(400);
    TiledImage image(QSize(200, 80));
    RebuiltStroke stroke;
    stroke.setPressureRange({0.3, 0.8});
    stroke.begin(&image, Qt::black, settings, pen(20, 40.5, 0.2));
    for (int x = 30; x <= 180; x += 10)
        stroke.add(pen(x, 40.5, 0.2));
    stroke.end();
    EXPECT_TRUE(image.isEmpty());
}

TEST(PressureRange, AMouseIgnoresIt) {
    BrushSettings settings = pencil(400);
    TiledImage image(QSize(200, 80));
    RebuiltStroke stroke;
    stroke.setPressureRange({0.5, 0.8});
    InputSample mouse = pen(20, 40.5, 0.3);
    mouse.kind = PointerKind::Mouse;
    stroke.begin(&image, Qt::black, settings, mouse);
    for (int x = 30; x <= 180; x += 10) {
        mouse.pos = QPointF(x, 40.5);
        stroke.add(mouse);
    }
    stroke.end();
    EXPECT_FALSE(image.isEmpty());
}

TEST(ShapedTips, WithNoStabilizerNothingIsLeftAtTheLift) {
    StrokeSmoother plain(0);
    EXPECT_EQ(plain.feed(timed(10, 0.5, 0)).size(), 1u);
    EXPECT_EQ(plain.feed(timed(20, 0.5, 8)).size(), 1u);
    EXPECT_TRUE(plain.finish().empty());
}

TEST(ShapedTips, ASuddenDropAtTheEndDoesntNarrowTheBody) {
    BrushSettings settings = pencil(400);
    Document doc(QSize(460, 80), Qt::white);
    StrokeController stroke(&doc);
    ASSERT_TRUE(stroke.begin(timed(10, 1.0, 0), Qt::black, settings, 15));
    for (int i = 1; i <= 40; ++i)
        stroke.add(timed(10 + i * 10, i < 38 ? 1.0 : 0.1, i * 8u));
    stroke.finish();
    EXPECT_GE(widthAt(doc, 330), 30) << "most of the body holds until the end starts to close";
}

TEST(ShapedTips, PressureDipsMidStrokeShowWhereTheyHappened) {
    BrushSettings settings = pencil(400);
    Document doc(QSize(420, 80), Qt::white);
    StrokeController stroke(&doc);
    ASSERT_TRUE(stroke.begin(timed(10, 0.2, 0), Qt::black, settings, 5));
    for (int i = 1; i <= 40; ++i)
        stroke.add(timed(10 + i * 8, i < 15 ? 0.2 : 1.0, i * 8u));
    stroke.finish();
    EXPECT_LT(widthAt(doc, 100), 14);
    EXPECT_GE(widthAt(doc, 200), 36);
}

TEST(ShapedTips, AStabilizedStrokeHasALongTipWithoutPassingThePen) {
    BrushSettings settings = pencil(400);
    Document doc(QSize(320, 80), Qt::white);
    StrokeController stroke(&doc);
    ASSERT_TRUE(stroke.begin(timed(10, 1.0, 0), Qt::black, settings, 15));
    for (int i = 1; i <= 30; ++i)
        stroke.add(timed(10 + i * 8, 1.0, i * 8u));
    stroke.finish();
    EXPECT_GE(widthAt(doc, 120), 36);
    EXPECT_LT(widthAt(doc, 240), widthAt(doc, 200));
    EXPECT_LT(widthAt(doc, 240), 22);
    EXPECT_EQ(alphaAt(doc, 275, 40), 0);
}

TEST(Settings, AQuickShapeIgnoresStrokeEnds) {
    BrushSettings settings = pencil(200);
    Document doc(QSize(200, 80), Qt::white);
    StrokeController stroke(&doc);
    drawLine(stroke, settings);
    std::vector<InputSample> shape;
    for (int x = 20; x <= 180; x += 20)
        shape.push_back(pen(x, 60.5, 1.0));
    stroke.repaintAlong(shape);
    const std::uint64_t shown = pixelHash(doc.currentLayer()->pixels);
    EXPECT_GT(alphaAt(doc, 22, 60), 0) << "no fine start";
    EXPECT_GT(alphaAt(doc, 178, 60), 0) << "and no narrow end";
    stroke.finish();
    EXPECT_EQ(pixelHash(doc.currentLayer()->pixels), shown);
}

TEST(RebuiltStroke, MinimumSizeKeepsSomeWidth) {
    BrushSettings settings = pencil(400);
    settings.pressureSize = true;
    settings.minSize = 50;
    EXPECT_NEAR(liftedWidth(settings, 100, 0.0), 20, 2);
}

TEST(RebuiltStroke, ALiftNarrowsThePenToNothingAtItsEnd) {
    BrushSettings settings = pencil(400);
    settings.pressureSize = true;
    Document doc(QSize(200, 80), Qt::white);
    StrokeController stroke(&doc);
    ASSERT_TRUE(stroke.begin(pen(20, 40.5, 0.8), Qt::black, settings));
    for (int x = 30; x <= 100; x += 10)
        stroke.add(pen(x, 40.5, 0.8));
    for (int x = 110; x <= 180; x += 10)
        stroke.add(pen(x, 40.5, 0.8 * (180 - x) / 80.0 + 0.02));
    stroke.finish();
    EXPECT_GT(widthAt(doc, 90), 25);
    EXPECT_LT(widthAt(doc, 175), 6);
    EXPECT_EQ(alphaAt(doc, 183, 40), 0);
}

// A pen's lift is pressure 0 where it last was, but a place keeps the firmer of its two samples: a sudden lift reaches the last sample
// with its own width and ends round there, as the reference's does.
TEST(RebuiltStroke, ASuddenLiftReachesTheLastSampleWithItsWidth) {
    BrushSettings settings = pencil(400);
    settings.pressureSize = true;
    Document lifted(QSize(220, 80), Qt::white);
    StrokeController stroke(&lifted);
    drawLine(stroke, settings, 0.8);
    stroke.finish();
    EXPECT_EQ(widthAt(lifted, 180), widthAt(lifted, 100));
    EXPECT_GT(alphaAt(lifted, 190, 40), 0) << "its round end, past the last sample";
}

TEST(RebuiltStroke, AMouseIsntNarrowedAtTheLift) {
    BrushSettings settings = pencil(400);
    settings.pressureSize = true;
    Document doc(QSize(200, 80), Qt::white);
    StrokeController stroke(&doc);
    InputSample mouse = pen(20, 40.5, 1.0);
    mouse.kind = PointerKind::Mouse;
    ASSERT_TRUE(stroke.begin(mouse, Qt::black, settings));
    for (int x = 30; x <= 180; x += 10) {
        mouse.pos = QPointF(x, 40.5);
        stroke.add(mouse);
    }
    stroke.finish();
    EXPECT_NEAR(widthAt(doc, 178), widthAt(doc, 100), 2);
}

TEST(RebuiltStroke, ReportsWhereItPainted) {
    TiledImage image(QSize(200, 80));
    RebuiltStroke stroke;
    QRect dirty = stroke.begin(&image, Qt::black, pencil(), pen(20, 40, 1.0));
    for (int x = 30; x <= 180; x += 10)
        dirty |= stroke.add(pen(x, 40, 1.0));
    dirty |= stroke.lift();
    const QRect painted = image.contentBounds(true);
    EXPECT_FALSE(painted.isEmpty());
    EXPECT_TRUE(dirty.contains(painted)) << "painted outside the reported area";
}

TEST(RebuiltStroke, GivesBackTheTilesAsTheyWere) {
    TiledImage image(QSize(200, 80));
    image.fill(QRect(0, 0, 64, 64), Qt::white);
    const TilePtr untouched = image.tileShared(0, 0);
    RebuiltStroke stroke;
    stroke.begin(&image, Qt::black, pencil(), pen(20, 40, 1.0));
    stroke.add(pen(30, 40, 1.0));
    stroke.add(pen(40, 40, 1.0));
    const TileMap before = stroke.end();
    ASSERT_TRUE(before.contains(TileCoord{0, 0}));
    EXPECT_EQ(before.at(TileCoord{0, 0}), untouched);
    EXPECT_NE(image.tileShared(0, 0), untouched);
}

TEST(RebuiltStroke, AQuickShapeStaysAsItsShown) {
    Document doc(QSize(200, 80), Qt::white);
    StrokeController stroke(&doc);
    drawLine(stroke, pencil());
    std::vector<InputSample> shape;
    for (int x = 20; x <= 180; x += 20)
        shape.push_back(pen(x, 60.5, 1.0));
    stroke.repaintAlong(shape);
    const std::uint64_t shown = pixelHash(doc.currentLayer()->pixels);
    EXPECT_EQ(alphaAt(doc, 100, 40), 0);
    EXPECT_GT(alphaAt(doc, 178, 60), 0);
    stroke.finish();
    EXPECT_EQ(pixelHash(doc.currentLayer()->pixels), shown);
}

TEST(RebuiltStroke, ClearingTheStrokeLeavesTheLayerAsItWas) {
    Document doc(QSize(200, 80), Qt::white);
    StrokeController stroke(&doc);
    drawLine(stroke, pencil());
    stroke.clearPainted();
    EXPECT_EQ(pixelHash(doc.currentLayer()->pixels), pixelHash(TiledImage(QSize(200, 80))));
    stroke.cancel();
}

namespace {

// `showEvery` is how many events go by between showing what's been laid: 0 paints without a thread of its own. `viewLevel` is how far
// out the view the frames are for is, 2^viewLevel; at 0 they're whole tiles.
QImage strokeShownEvery(int showEvery, const BrushSettings& settings = pencil(400), const QColor& color = Qt::black,
                        const SelectionMask* selection = nullptr, int viewLevel = 0) {
    Document doc(QSize(460, 120), Qt::white);
    doc.paintTarget(doc.currentLayerId()).value()->fill(QRect(100, 0, 60, 120), QColor(200, 40, 40));
    if (selection)
        (void)doc.setSelection(*selection);
    StrokeController stroke(&doc);
    if (showEvery > 0)
        stroke.paintInBackground({}, {});
    EXPECT_TRUE(stroke.begin(timed(30, 0.4, 0), color, settings, 12));
    stroke.setViewLevel(viewLevel);
    for (int i = 1; i <= 50; ++i) {
        stroke.add(timed(30 + i * 8.0, 0.5 + 0.4 * std::sin(i * 0.2), i * 8u));
        if (showEvery > 0 && i % showEvery == 0)
            stroke.showPaint();
    }
    stroke.finish();
    return doc.projection().copy();
}

void expectTheSameInBackground(const BrushSettings& settings, const QColor& color = Qt::black, const SelectionMask* selection = nullptr) {
    const QImage alone = strokeShownEvery(0, settings, color, selection);
    EXPECT_EQ(strokeShownEvery(1, settings, color, selection), alone);
    EXPECT_EQ(strokeShownEvery(7, settings, color, selection), alone);
    EXPECT_EQ(strokeShownEvery(1000, settings, color, selection), alone) << "shown only at the lift";
}

}

TEST(Background, TheFinalPixelsAreTheSameAsOnOneThread) {
    BrushSettings settings = pencil(400);
    settings.sharpness = 40;
    expectTheSameInBackground(settings);
    settings.sharpness = 100;
    expectTheSameInBackground(settings);
}

TEST(Background, AHugeTipGivesTheSamePixels) {
    BrushSettings settings = pencil(8000);
    settings.sharpness = 0;
    expectTheSameInBackground(settings);
}

// A mixing stroke paints dab by dab onto its own copy of the layer, on the worker's thread or the caller's.
TEST(Background, TheMixingToolsGiveTheSamePixels) {
    for (const ToolType tool : {ToolType::Brush, ToolType::WaterColor, ToolType::Marker}) {
        BrushSettings settings = defaultSettings(tool);
        settings.size = 300;
        expectTheSameInBackground(settings, QColor(30, 90, 220));
    }
}

TEST(Background, AMixingStrokeInASelectionGivesTheSamePixels) {
    const SelectionMask selection = selection::rectangle(QSize(460, 120), QRectF(60, 0, 200, 120), false);
    BrushSettings settings = defaultSettings(ToolType::Brush);
    settings.size = 300;
    expectTheSameInBackground(settings, QColor(30, 90, 220), &selection);
}

TEST(Background, AnEraserGivesTheSamePixels) {
    BrushSettings settings = pencil(300);
    settings.tool = ToolType::Eraser;
    expectTheSameInBackground(settings);
}

// The blocks a zoomed-out view shows come with the frames, and the lift's and end's frames are shown without taking them. Kept,
// they came out with the next stroke's first frame as that frame's level: a level-1 block (32² pixels) put where a level-2 view
// has room for 16², past the end of the compositor's buffer.
TEST(Background, ANewStrokeShowsNoBlocksOfTheLastOne) {
    TiledImage target(QSize(460, 120));
    RebuiltStroke stroke;
    stroke.paintInBackground({}, {});
    for (const int level : {1, 2}) {
        stroke.begin(&target, Qt::black, pencil(400), timed(30, 0.5, 0));
        stroke.setViewLevel(level);
        int shownLevel = -1;
        EXPECT_TRUE(stroke.takeShownBlocks(shownLevel).empty()) << "at level " << level;
        for (int i = 1; i <= 30; ++i) {
            stroke.add(timed(30 + i * 12.0, 0.5, i * 8u));
            if (i % 5 == 0) {  // shown as a canvas does (StrokeController::reportShown)
                stroke.waitUntilCaughtUp();
                (void)stroke.showNow();
                (void)stroke.takeShownBlocks(shownLevel);
            }
        }
        stroke.lift();
        (void)stroke.end();
    }
}

namespace {

// A stroke shown after every pen event once its thread has caught up, so each event's stamps are merged in a frame of their own.
QImage strokeShownCaughtUp(const BrushSettings& settings) {
    Document doc(QSize(460, 120), Qt::white);
    doc.paintTarget(doc.currentLayerId()).value()->fill(QRect(100, 0, 60, 120), QColor(200, 40, 40));
    StrokeController stroke(&doc);
    stroke.paintInBackground({}, {});
    EXPECT_TRUE(stroke.begin(timed(30, 0.4, 0), Qt::black, settings, 12));
    for (int i = 1; i <= 50; ++i) {
        stroke.add(timed(30 + i * 8.0, 0.5 + 0.4 * std::sin(i * 0.2), i * 8u));
        stroke.waitUntilCaughtUp();
        stroke.showPaint();
    }
    stroke.finish();
    return doc.projection().copy();
}

}

// From kStampGridTwoFrom the Pencil and Eraser lay their stamps on a grid. A frame merges only the cells changed since the last one,
// over the tile as last merged: the same pixels as working each tile out again whole, and as one thread.
TEST(Background, APlainGridMergingOnlyChangedCellsGivesTheSamePixels) {
    constexpr int kOnGrid = 10000;  // tenths of a px: a 1000 px tip, past kStampGridTwoFrom
    for (const ToolType tool : {ToolType::Pencil, ToolType::Eraser}) {
        BrushSettings settings = pencil(kOnGrid);
        settings.tool = tool;
        setGridMergeRebuildsWhole(true);
        const QImage whole = strokeShownCaughtUp(settings);
        setGridMergeRebuildsWhole(false);
        EXPECT_EQ(strokeShownCaughtUp(settings), whole) << int(tool);
        EXPECT_EQ(strokeShownEvery(0, settings), whole) << int(tool);
    }
}

// Zoomed out, the frames are blocks at the view's scale and the whole tiles wait for the stroke's end: the finished pixels are the same.
// A Brush this wide that falls behind would lay what's waiting in fewer dabs, by the clock, so that's decided here: never.
TEST(Background, FramesForAZoomedOutViewLeaveTheSamePixels) {
    constexpr int kZoomedOut = 4;  // the view is 2^this out, as the whole of a 16000 px canvas in a window
    constexpr int kOnGrid = 10000;  // tenths of a px: a 1000 px tip, past kStampGridTwoFrom
    StrokeWorker::setBacklogForTests(StrokeWorker::Backlog::Never);
    for (const ToolType tool : {ToolType::Pencil, ToolType::Brush, ToolType::Marker}) {
        BrushSettings settings = tool == ToolType::Pencil ? pencil(kOnGrid) : defaultSettings(tool);
        settings.size = kOnGrid;
        const QImage alone = strokeShownEvery(0, settings, QColor(30, 90, 220));
        EXPECT_EQ(strokeShownEvery(1, settings, QColor(30, 90, 220), nullptr, kZoomedOut), alone) << int(tool);
        EXPECT_EQ(strokeShownEvery(7, settings, QColor(30, 90, 220), nullptr, kZoomedOut), alone) << int(tool);
    }
    StrokeWorker::setBacklogForTests(StrokeWorker::Backlog::ByTheClock);
}

TEST(Background, ASelectionLimitsItTheSameWay) {
    const SelectionMask selection = selection::rectangle(QSize(460, 120), QRectF(60, 0, 200, 120), false);
    expectTheSameInBackground(pencil(400), Qt::black, &selection);
}

TEST(Background, NothingReachesTheCanvasUntilItsAskedFor) {
    Document doc(QSize(460, 120), Qt::white);
    StrokeController stroke(&doc);
    stroke.paintInBackground({}, {});
    int reports = 0;
    QObject guard;
    QObject::connect(&doc, &Document::imageChanged, &guard, [&reports] { ++reports; });
    ASSERT_TRUE(stroke.begin(timed(30, 1.0, 0), Qt::black, pencil(200)));
    for (int i = 1; i <= 10; ++i)
        stroke.add(timed(30 + i * 8.0, 1.0, i * 8u));
    stroke.waitUntilCaughtUp();
    EXPECT_TRUE(stroke.hasUnshownPaint());
    EXPECT_EQ(reports, 0);
    EXPECT_TRUE(doc.currentLayer()->pixels.isEmpty());
    stroke.showPaint();
    EXPECT_FALSE(stroke.hasUnshownPaint());
    EXPECT_EQ(reports, 1);
    EXPECT_FALSE(doc.currentLayer()->pixels.isEmpty());
    stroke.finish();
}

TEST(Background, ItSaysWhenThereIsAFrameToShow) {
    Document doc(QSize(460, 120), Qt::white);
    StrokeController stroke(&doc);
    std::atomic<int> frames{0};
    stroke.paintInBackground([&frames] { ++frames; }, {});
    ASSERT_TRUE(stroke.begin(timed(30, 1.0, 0), Qt::black, pencil(200)));
    for (int i = 1; i <= 10; ++i)
        stroke.add(timed(30 + i * 8.0, 1.0, i * 8u));
    stroke.waitUntilCaughtUp();
    EXPECT_GT(frames.load(), 0);
    stroke.finish();
}

TEST(Background, ACancelledStrokeLeavesNothingBehind) {
    Document doc(QSize(460, 120), Qt::white);
    const QImage before = doc.projection().copy();
    StrokeController stroke(&doc);
    stroke.paintInBackground({}, {});
    ASSERT_TRUE(stroke.begin(timed(30, 1.0, 0), Qt::black, pencil(200)));
    for (int i = 1; i <= 10; ++i)
        stroke.add(timed(30 + i * 8.0, 1.0, i * 8u));
    stroke.waitUntilCaughtUp();
    stroke.showPaint();
    stroke.cancel();
    EXPECT_FALSE(stroke.hasUnshownPaint());
    EXPECT_EQ(doc.projection(), before);
}

TEST(Background, UndoPutsThePictureBack) {
    Document doc(QSize(460, 120), Qt::white);
    const QImage before = doc.projection().copy();
    StrokeController stroke(&doc);
    stroke.paintInBackground({}, {});
    ASSERT_TRUE(stroke.begin(timed(30, 1.0, 0), Qt::black, pencil(200)));
    for (int i = 1; i <= 10; ++i)
        stroke.add(timed(30 + i * 8.0, 1.0, i * 8u));
    stroke.finish();
    const QImage painted = doc.projection().copy();
    EXPECT_NE(painted, before);
    doc.undoStack()->undo();
    EXPECT_EQ(doc.projection(), before);
    doc.undoStack()->redo();
    EXPECT_EQ(doc.projection(), painted);
}

TEST(Background, StrokesOneAfterAnotherEachStartFromTheLast) {
    const auto paintTwo = [](bool background) {
        Document doc(QSize(460, 120), Qt::white);
        StrokeController stroke(&doc);
        if (background)
            stroke.paintInBackground({}, {});
        for (const double y : {40.0, 60.0}) {
            InputSample first = timed(30, 1.0, 0);
            first.pos.setY(y);
            EXPECT_TRUE(stroke.begin(first, Qt::black, pencil(300)));
            for (int i = 1; i <= 20; ++i) {
                InputSample sample = timed(30 + i * 8.0, 1.0, i * 8u);
                sample.pos.setY(y);
                stroke.add(sample);
            }
            stroke.finish();
        }
        return doc.projection().copy();
    };
    EXPECT_EQ(paintTwo(true), paintTwo(false));
}

TEST(Background, AQuickShapeRepaintGivesTheSamePixels) {
    const auto repainted = [](bool background) {
        Document doc(QSize(460, 120), Qt::white);
        StrokeController stroke(&doc);
        if (background)
            stroke.paintInBackground({}, {});
        EXPECT_TRUE(stroke.begin(timed(30, 1.0, 0), Qt::black, pencil(300)));
        for (int i = 1; i <= 20; ++i)
            stroke.add(timed(30 + i * 8.0, 1.0, i * 8u));
        std::vector<InputSample> path;
        for (int i = 0; i <= 30; ++i)
            path.push_back(timed(50 + i * 10.0, 1.0, i * 8u));
        stroke.repaintAlong(path);
        stroke.finish();
        return doc.projection().copy();
    };
    EXPECT_EQ(repainted(true), repainted(false));
}

// The Pixel Art Pen paints on a thread of its own too: nothing reaches the canvas until a frame is shown,
// and the finished pixels are those of one thread.
TEST(Background, ThePixelArtPenPaintsOnAThreadOfItsOwn) {
    Document doc(QSize(460, 120), Qt::white);
    StrokeController stroke(&doc);
    stroke.paintInBackground({}, {});
    BrushSettings settings = pencil(200);
    settings.tool = ToolType::PixelArtPen;
    ASSERT_TRUE(stroke.begin(timed(30, 1.0, 0), Qt::black, settings));
    for (int i = 1; i <= 10; ++i)
        stroke.add(timed(30 + i * 8.0, 1.0, i * 8u));
    stroke.waitUntilCaughtUp();
    EXPECT_TRUE(stroke.hasUnshownPaint());
    EXPECT_TRUE(doc.currentLayer()->pixels.isEmpty());
    stroke.showPaint();
    EXPECT_FALSE(doc.currentLayer()->pixels.isEmpty());
    stroke.finish();
}

TEST(Background, TheToolsOnceOnTheFirstEngineGiveTheSamePixels) {
    constexpr int kWide = 3000;  // tenths of a px: a 300 px tip
    BrushSettings pixelArt = pencil(kWide);
    pixelArt.tool = ToolType::PixelArtPen;
    expectTheSameInBackground(pixelArt);
    BrushSettings textured = pencil(kWide);
    textured.texture = QStringLiteral("Canvas");  // without a library the tip stays round
    expectTheSameInBackground(textured);
    BrushSettings brush = defaultSettings(ToolType::Brush);
    brush.size = kWide;
    brush.texture = QStringLiteral("Canvas");
    expectTheSameInBackground(brush, QColor(30, 90, 220));
}

namespace {

void addLine(StrokeController& stroke, int events = 30) {
    for (int i = 1; i <= events; ++i)
        stroke.add(timed(30 + i * 8.0, 0.6 + 0.3 * std::sin(i * 0.3), i * 8u));
}

}

TEST(BackgroundLift, ItLeavesTheStrokeRunningUntilItsFinished) {
    Document doc(QSize(460, 120), Qt::white);
    StrokeController stroke(&doc);
    stroke.paintInBackground({}, {});
    const int steps = doc.undoStack()->count();
    ASSERT_TRUE(stroke.begin(timed(30, 0.5, 0), Qt::black, pencil(600)));
    addLine(stroke);
    stroke.finishInBackground();
    EXPECT_TRUE(stroke.isFinishing());
    EXPECT_TRUE(stroke.isActive());
    EXPECT_TRUE(doc.isPainting());
    EXPECT_EQ(doc.undoStack()->count(), steps);
    stroke.waitUntilCaughtUp();
    EXPECT_TRUE(stroke.isPainted());
    stroke.finish();
    EXPECT_FALSE(stroke.isFinishing());
    EXPECT_FALSE(stroke.isActive());
    EXPECT_FALSE(doc.isPainting());
    EXPECT_EQ(doc.undoStack()->count(), steps + 1);
}

TEST(BackgroundLift, TheFinalPixelsAreTheSame) {
    const auto painted = [](bool background) {
        Document doc(QSize(460, 120), Qt::white);
        StrokeController stroke(&doc);
        if (background)
            stroke.paintInBackground({}, {});
        EXPECT_TRUE(stroke.begin(timed(30, 0.5, 0), Qt::black, pencil(600), 12));
        addLine(stroke);
        if (background)
            stroke.finishInBackground();
        stroke.finish();
        return doc.projection().copy();
    };
    EXPECT_EQ(painted(true), painted(false));
}

TEST(BackgroundLift, ItSaysOnceMoreWhenPaintingIsDone) {
    Document doc(QSize(460, 120), Qt::white);
    StrokeController stroke(&doc);
    std::atomic<bool> doneWhenTold{false};
    stroke.paintInBackground([&] { doneWhenTold = doneWhenTold || stroke.isFinishing(); }, {});
    ASSERT_TRUE(stroke.begin(timed(30, 0.5, 0), Qt::black, pencil(200)));
    addLine(stroke, 3);
    stroke.finishInBackground();
    stroke.waitUntilCaughtUp();
    stroke.finish();
    EXPECT_TRUE(doneWhenTold.load());
}

TEST(BackgroundLift, CancellingAStrokeStillPaintingDoesntWaitForItAndLeavesNothing) {
    Document doc(QSize(2000, 1500), Qt::white);
    const QImage before = doc.projection().copy();
    StrokeController stroke(&doc);
    stroke.paintInBackground({}, {});
    ASSERT_TRUE(stroke.begin(timed(30, 0.5, 0), Qt::black, pencil(20000)));
    addLine(stroke, 200);
    stroke.finishInBackground();
    stroke.cancel();
    EXPECT_FALSE(stroke.isActive());
    EXPECT_FALSE(doc.isPainting());
    EXPECT_EQ(doc.projection(), before);
}

#ifdef __linux__
TEST(ThreadPriority, AWorkerGivesWayToTheInterface) {
    int niceness = 0;
    std::thread worker([&niceness] {
        lowerThisThreadsPriority();
        niceness = getpriority(PRIO_PROCESS, static_cast<id_t>(syscall(SYS_gettid)));
    });
    worker.join();
    EXPECT_GT(niceness, getpriority(PRIO_PROCESS, static_cast<id_t>(syscall(SYS_gettid)))) << "and only the worker";
}
#endif

namespace {

struct UsableGuard {
    explicit UsableGuard(std::int64_t bytes) { stroke_memory::usableOverride() = bytes; }
    ~UsableGuard() {
        stroke_memory::usableOverride() = 0;
        stroke_memory::setPolicy({});
    }
};

BrushSettings marker(int size) {
    BrushSettings settings = defaultSettings(ToolType::Marker);
    settings.size = size;
    return settings;
}

QImage paintedLine(const BrushSettings& settings, QStringList* messages = nullptr) {
    Document doc(QSize(200, 80), Qt::white);
    QObject guard;
    if (messages) {
        QObject::connect(&doc, &Document::strokeLimited, &guard,
                         [messages](int chosen, int used) { messages->append(QStringLiteral("limited %1 to %2").arg(chosen).arg(used)); });
        QObject::connect(&doc, &Document::strokeStopped, &guard, [messages] { messages->append(QStringLiteral("stopped")); });
    }
    StrokeController stroke(&doc);
    drawLine(stroke, settings);
    stroke.finish();
    return doc.currentLayer()->pixels.toImage();
}

}

TEST(StrokeMemory, TheEstimateGrowsWithTheSizeAndTheCanvas) {
    const std::int64_t small = stroke_memory::estimateBytes(marker(6000), QSize(3000, 3000));
    const std::int64_t wide = stroke_memory::estimateBytes(marker(40000), QSize(3000, 3000));
    const std::int64_t huge = stroke_memory::estimateBytes(marker(40000), QSize(16384, 16384));
    EXPECT_LT(small, wide);
    EXPECT_LT(wide, huge);
    // A 4000 px Marker across a 16000 px canvas held 6.2 GB more at its peak over paint: the estimate mustn't fall short.
    EXPECT_GT(huge, std::int64_t(62) * (std::int64_t(1) << 30) / 10);
    EXPECT_LT(stroke_memory::estimateBytes(pencil(40000), QSize(16384, 16384)), huge) << "stamps hold less than mixing";
}

TEST(StrokeMemory, TheLargestSizeThatFitsIsFound) {
    const QSize canvas(16384, 16384);
    const std::int64_t budget = std::int64_t(4) << 30;
    const int size = stroke_memory::largestSizeWithin(marker(40000), canvas, budget);
    EXPECT_LT(size, 40000);
    EXPECT_LE(stroke_memory::estimateBytes(marker(size), canvas), budget);
    EXPECT_GT(stroke_memory::estimateBytes(marker(size + 1), canvas), budget);
    EXPECT_EQ(stroke_memory::largestSizeWithin(marker(2000), canvas, budget), 2000) << "a stroke that fits keeps its size";
}

TEST(StrokeMemory, AStrokeThatFitsPaintsExactlyAsWithNoLimit) {
    QImage unlimited;
    {
        UsableGuard none(-1);
        unlimited = paintedLine(marker(400));
    }
    QStringList messages;
    UsableGuard plenty(std::int64_t(64) << 30);
    EXPECT_EQ(paintedLine(marker(400), &messages), unlimited);
    EXPECT_TRUE(messages.isEmpty());
}

TEST(StrokeMemory, AStrokeTooLargeIsLimitedAndSaysSo) {
    // Room for what the process holds now and a little more: the 3000 px brush is cut down, and the stroke stops once the
    // process holds more than that.
    QStringList messages;
    QImage limited;
    {
        stroke_memory::giveBackFreed();  // as the stroke does before it's limited, so it finds the process holding what this did
        UsableGuard tight(stroke_memory::heldBytes() + stroke_memory::estimateBytes(marker(200), QSize(200, 80)));
        limited = paintedLine(marker(30000), &messages);
    }
    ASSERT_FALSE(messages.isEmpty());
    EXPECT_TRUE(messages.front().startsWith(QStringLiteral("limited 30000 to "))) << messages.front().toStdString();
    EXPECT_NE(limited, paintedLine(marker(30000)));
}

// Options › Performance › Memory: a custom amount is used in place of the machine's, and a lower cgroup limit still wins.
TEST(StrokeMemory, ACustomAmountIsUsedInPlaceOfAutomatic) {
    UsableGuard reset(0);
    const std::int64_t automatic = stroke_memory::allowedBytes();
    constexpr std::int64_t kGiB = std::int64_t(1) << 30;
    stroke_memory::setPolicy({kGiB, true});
    EXPECT_EQ(stroke_memory::allowedBytes(), kGiB);
    EXPECT_EQ(stroke_memory::usableBytes(), kGiB - (std::int64_t(256) << 20)) << "less the reserve";
    stroke_memory::setPolicy({3 * kGiB, true});
    EXPECT_LE(stroke_memory::allowedBytes(), 3 * kGiB);
    EXPECT_GE(stroke_memory::allowedBytes(), std::min(3 * kGiB, automatic)) << "only a cgroup's limit is lower";
    stroke_memory::setPolicy({});
    EXPECT_EQ(stroke_memory::allowedBytes(), automatic);
}

// With "Keep brushes within this memory" off, a brush is never limited as it starts, but a stroke that runs out still stops.
TEST(StrokeMemory, WithBrushesNotKeptWithinAStrokeIsntLimitedButStillStops) {
    QStringList messages;
    {
        stroke_memory::giveBackFreed();
        UsableGuard tight(stroke_memory::heldBytes() + stroke_memory::estimateBytes(marker(200), QSize(200, 80)));
        stroke_memory::setPolicy({0, false});
        EXPECT_FALSE(stroke_memory::sizeLimit(marker(30000), QSize(200, 80)));
        paintedLine(marker(30000), &messages);
    }
    EXPECT_FALSE(messages.isEmpty() || messages.front().startsWith(QStringLiteral("limited"))) << messages.join(' ').toStdString();
    EXPECT_TRUE(messages.contains(QStringLiteral("stopped")));
}

TEST(StrokeMemory, InTheBackgroundNoSampleIsLaidOnceItDoesntFit) {
    const auto paint = [](bool fits) {
        TiledImage image(QSize(300, 80));
        RebuiltStroke stroke;
        stroke.paintInBackground({}, {});
        stroke.setMemoryGuard([fits] { return fits; });
        stroke.begin(&image, Qt::black, pencil(200), pen(20, 40.5, 1.0));
        for (int x = 30; x <= 280; x += 10)
            stroke.add(pen(x, 40.5, 1.0));
        stroke.liftInBackground();
        stroke.catchUp();
        const bool ranOut = stroke.ranOutOfMemory();
        stroke.end();
        return std::pair(image.pixel(250, 40)[3], ranOut);
    };
    EXPECT_GT(paint(true).first, 0);
    EXPECT_FALSE(paint(true).second);
    EXPECT_EQ(paint(false).first, 0) << "only the stroke's start is laid";
    EXPECT_TRUE(paint(false).second);
}

// The Brush's soft Shapes add their outline up along the path, and a tile whose shape is all 1 shares one read-only array of
// ones, which no dab may write again. A dab coming back over such a tile wrote its 1 again, and the stroke crashed where a tile
// fills inside the stroke while the grid's points are a pixel apart: a straight line on the 1st Shape from 300 px, the 2nd from 150 px
// and the 3rd from 100 px, each up to 500 px, and a loop back over itself on the 3rd from 110 px.
TEST(RebuiltStroke, TheBrushsSoftShapesPaintOnOverTilesTheyHaveFilled) {
    const auto alphaAfter = [](int edge, double sizePx, const std::vector<QPointF>& path, QPoint at) {
        TiledImage image(QSize(900, 500));
        BrushSettings brush = defaultSettings(ToolType::Brush);
        brush.edge = edge;
        brush.size = static_cast<int>(sizePx * 10.0);
        RebuiltStroke stroke;
        stroke.begin(&image, Qt::black, brush, pen(path.front().x(), path.front().y(), 1.0));
        for (std::size_t i = 1; i < path.size(); ++i)
            stroke.add(pen(path[i].x(), path[i].y(), 1.0));
        stroke.end();
        return image.pixel(at.x(), at.y())[3];
    };
    std::vector<QPointF> line;
    for (int x = 150; x <= 750; x += 10)
        line.emplace_back(x, 250.0);
    std::vector<QPointF> loop;  // a turn and a quarter round (450, 250), back over where it began
    const double turn = 2.0 * std::acos(-1.0);
    for (int k = 0; k <= 100; ++k)
        loop.emplace_back(450.0 + 120.0 * std::cos(1.25 * turn * k / 100.0), 250.0 + 120.0 * std::sin(1.25 * turn * k / 100.0));
    EXPECT_GT(alphaAfter(0, 400.0, line, {450, 250}), kChannelOne / 2) << "the 1st Shape, a line at 400 px";
    EXPECT_GT(alphaAfter(1, 250.0, line, {450, 250}), kChannelOne / 2) << "the 2nd Shape, a line at 250 px";
    EXPECT_GT(alphaAfter(2, 200.0, line, {450, 250}), kChannelOne / 2) << "the 3rd Shape, a line at 200 px";
    EXPECT_GT(alphaAfter(2, 120.0, loop, {570, 250}), kChannelOne / 2) << "the 3rd Shape, a loop at 120 px";
}

// The Pixel Art Pen: a hard disc.
namespace {

BrushSettings pixelArt(int size) {
    BrushSettings settings = defaultSettings(ToolType::PixelArtPen);
    settings.size = size;
    return settings;
}

// Along y = 40.5 from x = 20 to 180, with a light touch, which the pen doesn't read.
void drawPixelArt(Document& doc, const BrushSettings& settings, const QColor& colour) {
    StrokeController stroke(&doc);
    ASSERT_TRUE(stroke.begin(pen(20, 40.5, 0.3), colour, settings));
    for (int x = 30; x <= 180; x += 10)
        stroke.add(pen(x, 40.5, 0.3));
    stroke.finish();
}

}

TEST(PixelArtPen, IsAHardDiscWithNothingInBetween) {
    Document doc(QSize(200, 80), Qt::white);
    drawPixelArt(doc, pixelArt(50), Qt::black);
    for (int y = 0; y < 80; ++y)
        for (int x = 0; x < 200; ++x) {
            const int alpha = alphaAt(doc, x, y);
            ASSERT_TRUE(alpha == 0 || alpha == kChannelOne) << x << ", " << y << ": " << alpha;
        }
    EXPECT_EQ(widthAt(doc, 100), 5) << "the pixels whose centres are within 2.5 px of the line";
}

TEST(PixelArtPen, TheFinestPenMarksThePixelUnderIt) {
    Document doc(QSize(200, 80), Qt::white);
    drawPixelArt(doc, pixelArt(1), Qt::black);  // 0.1 px
    for (int x = 20; x <= 180; ++x) {
        EXPECT_EQ(alphaAt(doc, x, 40), kChannelOne) << x;
        EXPECT_EQ(alphaAt(doc, x, 39), 0) << x;
        EXPECT_EQ(alphaAt(doc, x, 41), 0) << x;
    }
}

TEST(PixelArtPen, OpacityAndTheColoursAlphaBothCount) {
    const auto alphaWith = [](int opacity, int colourAlpha) {
        Document doc(QSize(200, 80), Qt::white);
        BrushSettings settings = pixelArt(50);
        settings.opacity = opacity;
        QColor colour(Qt::black);
        colour.setAlpha(colourAlpha);
        drawPixelArt(doc, settings, colour);
        return alphaAt(doc, 100, 40);
    };
    EXPECT_EQ(alphaWith(255, 255), kChannelOne);
    EXPECT_NEAR(alphaWith(128, 255), 128 * 257, 1) << "Opacity";
    EXPECT_NEAR(alphaWith(255, 128), 128 * 257, 1) << "the colour's alpha";
    EXPECT_NEAR(alphaWith(128, 128), kChannelOne * (128.0 / 255.0) * (128.0 / 255.0), 1) << "both";
}

TEST(PixelArtPen, ATransparentColourErases) {
    Document doc(QSize(200, 80), Qt::white);
    doc.paintTarget(doc.currentLayerId()).value()->fill(Qt::black);
    drawPixelArt(doc, pixelArt(50), Qt::transparent);
    EXPECT_EQ(alphaAt(doc, 100, 40), 0);
    EXPECT_EQ(alphaAt(doc, 100, 10), kChannelOne);
}

// A texture's grain.
namespace {

QImage greyPicture(int width, int height, int level) {
    QImage picture(width, height, QImage::Format_Grayscale8);
    picture.fill(QColor(level, level, level));
    return picture;
}

}

// Light is where paint goes, as the reference reads a paper; inverted, dark is. The tests below build their papers inverted,
// dark where paint goes.
TEST(PaperGrain, LightIsWherePaintGoesAndIntensityScalesIt) {
    EXPECT_FLOAT_EQ(PaperGrain(greyPicture(4, 4, 255), 1.0, 1.0, false).shareAt(10, 10), 1.0f) << "white: all of the stroke";
    EXPECT_FLOAT_EQ(PaperGrain(greyPicture(4, 4, 0), 1.0, 1.0, false).shareAt(10, 10), 0.0f) << "black: none";
    EXPECT_FLOAT_EQ(PaperGrain(greyPicture(4, 4, 0), 1.0, 0.5, false).shareAt(10, 10), std::pow(0.5f, 1.2f))
        << "half the Intensity, to the fit's power";
    EXPECT_FLOAT_EQ(PaperGrain(greyPicture(4, 4, 0), 1.0, 1.0, true).shareAt(10, 10), 1.0f) << "inverted";
}

TEST(PaperGrain, AtScale100APicturePixelIsACanvasPixel) {
    QImage picture = greyPicture(8, 8, 255);
    picture.setPixel(3, 5, qRgb(0, 0, 0));
    const PaperGrain grain(picture, 1.0, 1.0, true);
    EXPECT_FLOAT_EQ(grain.shareAt(3, 5), 1.0f) << "read whole, not blended with its neighbours";
    EXPECT_FLOAT_EQ(grain.shareAt(4, 5), 0.0f);
    EXPECT_FLOAT_EQ(grain.shareAt(11, 13), 1.0f) << "the paper repeats from the canvas's top left";
}

TEST(PaperGrain, RepeatsAcrossTheCanvasAtItsScale) {
    QImage picture = greyPicture(8, 8, 255);
    picture.setPixel(3, 5, qRgb(0, 0, 0));
    const PaperGrain grain(picture, 2.0, 1.0, true);  // each picture pixel two canvas pixels across
    for (const auto& [x, y] : {std::pair{6, 10}, std::pair{20, 30}, std::pair{-9, -7}})
        EXPECT_FLOAT_EQ(grain.shareAt(x, y), grain.shareAt(x + 16, y + 32)) << x << ", " << y;
    EXPECT_GT(grain.shareAt(6, 10), 0.5f) << "the dark pixel, scaled";
    EXPECT_EQ(grain.shareAt(0, 0), 0.0f);
}

TEST(StrokeMerge, AGrainThinsAFullCoatAndASelectionScalesIt) {
    DensityTile density;
    density.value[0] = 20.0f;  // past full
    density.shape[0] = 1.0f;
    StrokePaint paint;
    paint.grain = std::make_shared<const PaperGrain>(greyPicture(4, 4, 255), 1.0, 0.25, true);
    const double share = std::pow(0.75, 1.2);
    const double full = 1.0 - std::exp(-8.0 * share);  // a full coat holds a density of 8 against the paper
    Tile tile;
    mergeDensityTile(density, paint, tile, nullptr, TileCoord{0, 0});
    EXPECT_NEAR(tile.px[3], full * kChannelOne, 2);
    SelectionTile half;
    half.v.fill(static_cast<std::uint16_t>(kChannelOne / 2));
    Tile selected;
    mergeDensityTile(density, paint, selected, &half, TileCoord{0, 0});
    EXPECT_NEAR(selected.px[3], full * 0.5 * kChannelOne, 2) << "both";
}

TEST(PaperGrain, ATilesSharesAreEachPixelsShare) {
    QImage picture(37, 23, QImage::Format_Grayscale8);  // sizes that don't divide a tile
    for (int y = 0; y < picture.height(); ++y)
        for (int x = 0; x < picture.width(); ++x)
            picture.setPixel(x, y, qRgb((x * 37 + y * 91) % 256, 0, 0));
    for (const double scale : {0.1, 0.73, 1.0, 2.0, 5.0})
        for (const bool invert : {false, true}) {
            const PaperGrain grain(picture, scale, 0.6, invert);
            for (const TileCoord coord : {TileCoord{0, 0}, TileCoord{-3, 7}, TileCoord{12, -1}}) {
                std::array<float, Tile::kSize * Tile::kSize> shares;
                grain.sharesOf(coord, shares.data());
                const QPoint origin = coord.pixelRect().topLeft();
                for (int i = 0; i < Tile::kSize * Tile::kSize; ++i)
                    ASSERT_EQ(shares[static_cast<std::size_t>(i)],
                              grain.shareAt(origin.x() + i % Tile::kSize, origin.y() + i / Tile::kSize))
                        << scale << " " << invert << " " << i;
            }
        }
}

// The paper thins a stamp tool's paint: a light coat shows the grain as a share of itself, a full one keeps the darkest pits.
TEST(StrokeMerge, AStampToolsPaintIsThinnedByThePaper) {
    DensityTile density;
    density.shape[0] = 1.0f;
    const auto alphaWith = [&](float value, int grey) {
        density.value[0] = value;
        StrokePaint paint;
        paint.grain = std::make_shared<const PaperGrain>(greyPicture(4, 4, grey), 1.0, 1.0, true);
        Tile tile;
        mergeDensityTile(density, paint, tile, nullptr, TileCoord{0, 0});
        return tile.px[3] / double(kChannelOne);
    };
    const double light = 1.0 - std::exp(-0.1);  // a coat of density 0.1, as the merge reads it at full Sharpness
    EXPECT_NEAR(alphaWith(0.1f, 0), light, 2e-4) << "black: all of it";
    EXPECT_NEAR(alphaWith(0.1f, 128), 1.0 - std::exp(-0.1 * std::pow(127.0 / 255.0, 1.2)), 2e-4) << "half grey: thinned";
    EXPECT_NEAR(alphaWith(20.0f, 230), 1.0 - std::exp(-8.0 * std::pow(25.0 / 255.0, 1.2)), 2e-4) << "a full coat keeps a light pit open";
    EXPECT_EQ(alphaWith(20.0f, 255), 0.0) << "white: none";
}

// A soft Shape's coat is laid through the paper's tops: where its outline is whole the paper shows as a share of the paint,
// and as the outline fades only the paper's higher tops take any.
TEST(StrokeMerge, ASoftEdgeBreaksIntoThePapersTops) {
    DensityTile density;
    density.value[0] = 20.0f;  // past full
    const auto alphaWith = [&](float outline, int grey) {
        density.shape[0] = outline;
        StrokePaint paint;
        paint.softEdge = 1.0f;
        paint.grain = std::make_shared<const PaperGrain>(greyPicture(4, 4, grey), 1.0, 1.0, true);
        Tile tile;
        mergeDensityTile(density, paint, tile, nullptr, TileCoord{0, 0});
        return tile.px[3] / double(kChannelOne);
    };
    EXPECT_NEAR(alphaWith(1.0f, 0), 1.0, 2e-4) << "black, a whole outline: all of it";
    const double share = std::pow(127.0 / 255.0, 1.2);
    EXPECT_NEAR(alphaWith(1.0f, 128), std::pow(share, 0.6), 2e-4) << "half grey, a whole outline: the paper's share of the coat";
    EXPECT_LT(alphaWith(0.9f, 128), alphaWith(1.0f, 128) * 0.9) << "a fading outline takes less than its own share";
    EXPECT_EQ(alphaWith(0.5f, 128), 0.0) << "half grey isn't high enough for a faint rim";
    EXPECT_GT(alphaWith(0.5f, 0), 0.0) << "the paper's highest tops still take it";
    EXPECT_EQ(alphaWith(1.0f, 255), 0.0) << "white: none";
}

// A hard Shape's edge breaks into the paper's tops too: inside the outline the coat is thinned as ever; on its edge, a pixel
// low on the paper loses it, one on its tops keeps it.
TEST(StrokeMerge, AHardEdgeBreaksIntoThePapersTopsToo) {
    DensityTile density;
    const auto alphaWith = [&](float outline, int grey, bool hard) {
        density.value[0] = 20.0f * outline;
        density.shape[0] = outline;
        StrokePaint paint;
        paint.hardEdge = hard;
        paint.grain = std::make_shared<const PaperGrain>(greyPicture(4, 4, grey), 1.0, 1.0, true);
        Tile tile;
        mergeDensityTile(density, paint, tile, nullptr, TileCoord{0, 0});
        return tile.px[3] / double(kChannelOne);
    };
    EXPECT_EQ(alphaWith(1.0f, 128, true), alphaWith(1.0f, 128, false)) << "inside the outline, the thinning alone";
    EXPECT_EQ(alphaWith(0.5f, 200, true), 0.0) << "a light grey, low on the paper, on the edge: none";
    EXPECT_GT(alphaWith(0.5f, 200, false), 0.0) << "as the thinning alone would leave some";
    EXPECT_NEAR(alphaWith(0.5f, 0, true), alphaWith(0.5f, 0, false), 2e-4) << "the paper's top keeps all of it";
}

// A firmer soft Shape goes its share of the way from a hard edge's thinning to the softest's tops.
TEST(StrokeMerge, AFirmerSoftShapeIsBetweenTheHardAndTheSoftest) {
    DensityTile density;
    density.value[0] = 20.0f * 0.8f;
    density.shape[0] = 0.8f;
    const auto alphaWith = [&](float softness) {
        StrokePaint paint;
        paint.softEdge = softness;
        paint.grain = std::make_shared<const PaperGrain>(greyPicture(4, 4, 60), 1.0, 1.0, true);
        Tile tile;
        mergeDensityTile(density, paint, tile, nullptr, TileCoord{0, 0});
        return double(tile.px[3]);
    };
    const double hard = alphaWith(0.0f), softest = alphaWith(1.0f);
    EXPECT_GT(std::abs(hard - softest), 1000.0) << "the two rules differ here";
    EXPECT_NEAR(alphaWith(0.25f), hard + (softest - hard) * 0.25, 2.0);
}

// Below the soft bar a firmer soft Shape's hard part fades out too, so the paper's low places on a faint rim get nothing: no haze is
// left past its clumps.
TEST(StrokeMerge, AFirmerSoftShapeLeavesNoHazeBelowItsBar) {
    DensityTile density;
    density.value[0] = 20.0f * 0.2f;
    density.shape[0] = 0.2f;  // a faint rim: its bar is whole
    StrokePaint paint;
    paint.softEdge = 0.5f;
    paint.grain = std::make_shared<const PaperGrain>(greyPicture(4, 4, 220), 1.0, 1.0, true);  // a low place: well under the bar
    Tile tile;
    mergeDensityTile(density, paint, tile, nullptr, TileCoord{0, 0});
    EXPECT_EQ(tile.px[3], 0);
}

// At Intensity 0 the paper has no depth: a soft edge is laid as without a texture, its faintest rim and all.
TEST(StrokeMerge, ASoftEdgeOnAPaperOfNoDepthIsItsPlainSelf) {
    for (const float outline : {1.0f, 0.6f, 0.2f, 0.02f}) {
        DensityTile density;
        density.value[0] = 20.0f * outline;
        density.shape[0] = outline;
        StrokePaint plain;
        Tile without;
        mergeDensityTile(density, plain, without, nullptr, TileCoord{0, 0});
        StrokePaint paint;
        paint.softEdge = 1.0f;
        paint.grain = std::make_shared<const PaperGrain>(greyPicture(4, 4, 200), 1.0, 0.0, true);
        Tile with;
        mergeDensityTile(density, paint, with, nullptr, TileCoord{0, 0});
        EXPECT_GT(without.px[3], 0) << outline;
        EXPECT_EQ(with.px[3], without.px[3]) << outline;
    }
}

// A mixing tool's painted tile is thinned by the paper as a stamp tool's coat is, its coverage read from how far its alpha went toward
// whole; where the alpha can't say, over paint already whole, it goes the paper's share of its way.
TEST(StrokeMerge, AMixingToolsPaintedTileTakesTheGrain) {
    const double share = std::pow(0.75, 1.2);  // a paper that lets 0.75^1.2 of a coat through
    const auto laid = [](Channel beforeAlpha, Channel paintedAlpha, bool thinsCoat = true, bool beforeTile = true) {
        auto painted = std::make_shared<Tile>();
        auto before = beforeTile ? std::make_shared<Tile>() : nullptr;
        for (std::size_t i = 0; i < Tile::kValues; i += 4) {
            painted->px[i + 3] = paintedAlpha;
            if (before)
                before->px[i + 3] = beforeAlpha;
        }
        StrokePaint paint;
        paint.grain = std::make_shared<const PaperGrain>(greyPicture(4, 4, 255), 1.0, 0.25, true);
        paint.thinsCoat = thinsCoat;
        std::vector<TileMergeJob> jobs{{TileCoord{0, 0}, nullptr, nullptr, before, nullptr, painted, nullptr}};
        mergeTiles(jobs, paint);
        EXPECT_NE(jobs.front().fresh, painted) << "a copy, the painted tile kept whole for the stroke to pick up from";
        EXPECT_EQ(painted->px[3], paintedAlpha);
        return jobs.front().fresh->px[3] / double(kChannelOne);
    };
    EXPECT_NEAR(laid(0, kChannelOne), 1.0 - std::exp(-8.0 * share), 2e-4) << "a full coat keeps only the darkest pits";
    EXPECT_NEAR(laid(0, kChannelOne / 10), 1.0 - std::pow(0.9, share), 2e-4) << "a light coat shows the grain as a share of itself";
    EXPECT_NEAR(laid(kChannelOne / 2, kChannelOne * 3 / 4), 0.5 + 0.5 * (1.0 - std::pow(0.5, share)), 2e-4) << "over half";
    EXPECT_NEAR(laid(kChannelOne, kChannelOne), 1.0, 2e-4) << "over paint already whole, the alpha stays";
    EXPECT_NEAR(laid(0, kChannelOne, true, false), 1.0 - std::exp(-8.0 * share), 2e-4) << "with no tile before it";
    EXPECT_NEAR(laid(0, kChannelOne, false), share, 2e-4) << "Blur's goes the paper's share of the way";
}

// The Marker's and Water Color's strokes are whole coats through the paper: each pixel's share is a whole coat's, and the painted tile
// goes that times its top, from the one over clear paper to the one over a whole layer by the alpha before.
TEST(StrokeMerge, AWholeCoatGoesTheGrainsWholeShareTimesItsTop) {
    const double share = std::pow(64.0 / 255.0, 1.2);  // grey 191, inverted: a paper that lets (64/255)^1.2 of a coat through
    const double whole = 1.0 - std::exp(-16.0 * share);
    EXPECT_NEAR(wholeCoatThroughPaper(PaperGrain(greyPicture(4, 4, 191), 1.0, 1.0, true).shareAt(3, 3), 16.0f), whole, 1e-5);
    const auto laid = [](Channel beforeAlpha) {
        auto painted = std::make_shared<Tile>();
        auto before = std::make_shared<Tile>();
        for (std::size_t i = 0; i < Tile::kValues; i += 4) {
            painted->px[i + 3] = static_cast<Channel>(kChannelOne);
            before->px[i + 3] = beforeAlpha;
        }
        StrokePaint paint;
        auto grain = std::make_shared<PaperGrain>(greyPicture(4, 4, 191), 1.0, 1.0, true);
        grain->keepAlso([](float s) { return wholeCoatThroughPaper(s, 16.0f); });
        paint.grain = grain;
        paint.wholeCoatDensity = 16.0f;
        paint.wholeCoatClear = 1.0f;
        paint.wholeCoatOver = 0.8f;
        std::vector<TileMergeJob> jobs{{TileCoord{0, 0}, nullptr, nullptr, before, nullptr, painted, nullptr}};
        mergeTiles(jobs, paint);
        return jobs.front().fresh->px[3] / double(kChannelOne);
    };
    EXPECT_NEAR(laid(0), whole, 2e-4) << "over clear paper";
    const double half = 0.5 + 0.5 * whole * 0.9;  // halfway from clear to whole, the top halfway from 1 to 0.8
    EXPECT_NEAR(laid(kChannelOne / 2), half, 2e-4) << "over half";
}

// The Brush's way through the paper: where its alpha says how much it coated, that coat thinned; over paint where it
// can't, a full coat thinned as a stamp tool's; and on its edge the paper's tops alone take it.
TEST(StrokeMerge, TheBrushsWayThroughThePaper) {
    StrokePaint paint;
    paint.thinsCoat = true;
    paint.uncoatedDensity = kPaperFullDensity;
    paint.uncoatedTop = 1.0f;
    paint.edgeBarDepth = 1.5f;
    paint.edgeBarWidth = 0.4f;
    const float share = 0.3f;
    EXPECT_NEAR(mixingWayThroughPaper(paint, share, 1.0f, 1.0f, 1.0f), 1.0 - std::exp(-8.0 * share), 1e-5) << "over paint already whole";
    EXPECT_NEAR(mixingWayThroughPaper(paint, share, 0.0f, 0.5f, 1.0f), mixingCoatShareForTesting(0.0f, 0.5f, share), 1e-6)
        << "a coat it can read";
    EXPECT_LT(mixingWayThroughPaper(paint, share, 0.0f, 0.5f, 0.5f), mixingWayThroughPaper(paint, share, 0.0f, 0.5f, 1.0f)) << "its edge";
    EXPECT_EQ(mixingWayThroughPaper(paint, 1.0f, 0.0f, 0.5f, 0.5f), mixingWayThroughPaper(paint, 1.0f, 0.0f, 0.5f, 1.0f))
        << "the tops keep it";
    paint.edgeBarDepth = 0.0f;
    EXPECT_EQ(mixingWayThroughPaper(paint, share, 0.0f, 0.5f, 0.5f), mixingWayThroughPaper(paint, share, 0.0f, 0.5f, 1.0f))
        << "no bar, no edge";
}

// A mixing tool's coat through the paper is worked out by a near log and exponential, within 1e-5 of the formula.
TEST(StrokeMerge, AMixingToolsCoatShareIsNearTheFormula) {
    double worst = 0.0;
    for (const int before : {0, 1, 300, 20000, 50000, 65400})
        for (int after = before + 7; after <= kChannelOne; after += 37)
            for (int si = 0; si <= 100; ++si) {
                const float a0 = static_cast<float>(before) * kChannelToFloat, a1 = static_cast<float>(after) * kChannelToFloat,
                            share = static_cast<float>(si) / 100.0f;
                const double c = (double(a1) - a0) / (1.0 - a0);
                const double exact = (1.0 - std::exp(-std::min(-std::log1p(-std::min(c, 1.0 - 1e-300)), 8.0) * share)) / c;
                worst = std::max(worst, std::abs((mixingCoatShareForTesting(a0, a1, share) - exact) * c));
            }
    EXPECT_LT(worst, 1e-5) << "as a share of a pixel";
}

// A mixing tool's painted tile laid back toward the tile before by a selection's share and the grain's gives each channel
// floor(before + (painted - before) · share + 1/2), the four channels at once or one at a time.
TEST(StrokeMerge, APaintedTileGoesItsShareOfTheWayChannelByChannel) {
    auto painted = std::make_shared<Tile>();
    auto before = std::make_shared<Tile>();
    SelectionTile selection;
    for (std::size_t i = 0; i < Tile::kValues; ++i) {
        painted->px[i] = static_cast<Channel>((i * 7919u + 13u) % 65536u);
        before->px[i] = static_cast<Channel>((i * 104729u + 7u) % 65536u);
    }
    for (std::size_t i = 0; i < selection.v.size(); ++i)
        selection.v[i] = static_cast<std::uint16_t>((i * 2654435761u) % 65536u);
    QImage picture(37, 23, QImage::Format_Grayscale8);
    for (int y = 0; y < picture.height(); ++y)
        for (int x = 0; x < picture.width(); ++x)
            picture.setPixel(x, y, qRgb((x * 37 + y * 91) % 256, 0, 0));
    StrokePaint paint;
    paint.grain = std::make_shared<const PaperGrain>(picture, 0.73, 0.8, true);
    paint.thinsCoat = true;
    std::vector<TileMergeJob> jobs{{TileCoord{2, -1}, nullptr, &selection, before, nullptr, painted, nullptr}};
    mergeTiles(jobs, paint);
    std::array<float, Tile::kSize * Tile::kSize> grain;
    paint.grain->sharesOf(TileCoord{2, -1}, grain.data());
    for (int i = 0; i < Tile::kSize * Tile::kSize; ++i) {
        // How far the tile goes, as the merge works it out.
        const float a0 = before->px[static_cast<std::size_t>(i * 4 + 3)] * kChannelToFloat;
        const float a1 = painted->px[static_cast<std::size_t>(i * 4 + 3)] * kChannelToFloat;
        float paper = grain[static_cast<std::size_t>(i)];
        if (a1 > a0 + 1e-4f && a0 < 0.999f)  // kCoatLeastRise and kCoatWholeAlpha
            paper = mixingCoatShareForTesting(a0, a1, paper);
        const float share = paper * (selection.v[static_cast<std::size_t>(i)] * kChannelToFloat);
        for (int c = 0; c < 4; ++c) {
            const std::size_t at = static_cast<std::size_t>(i * 4 + c);
            const float old = before->px[at];
            ASSERT_EQ(jobs.front().fresh->px[at], static_cast<Channel>(std::floor(old + (painted->px[at] - old) * share + 0.5f)))
                << i << ", " << c;
        }
    }
}

// A form's picture as a stamp's outline.
TEST(StampForm, IsThePicturesDarknessCentredOnTheStamp) {
    QImage picture = greyPicture(10, 10, 255);
    for (int y = 0; y < 5; ++y)
        for (int x = 0; x < 5; ++x)
            picture.setPixel(x, y, qRgb(0, 0, 0));  // its top left quarter dark
    const StampForm form(picture, 1.0, 1.0, false);
    EXPECT_FLOAT_EQ(form.at(-5.0f, -5.0f, 10.0f), 1.0f) << "the top left, the picture 20 px across";
    EXPECT_FLOAT_EQ(form.at(5.0f, 5.0f, 10.0f), 0.0f) << "the bottom right";
    EXPECT_FLOAT_EQ(form.at(-11.0f, 0.0f, 10.0f), 0.0f) << "past its edge";
    EXPECT_FLOAT_EQ(StampForm(picture, 1.0, 1.0, true).at(5.0f, 5.0f, 10.0f), 1.0f) << "inverted";
    EXPECT_FLOAT_EQ(StampForm(picture, 2.0, 1.0, false).at(-15.0f, -15.0f, 10.0f), 1.0f) << "at Scale 200 % it's 40 px across";
    EXPECT_GE(form.reachFor(10.0f), std::hypot(10.0f, 10.0f)) << "its corners";
}

namespace {

// The Pencil's 1st Shape with a form, at 30 px, along y = 40.5, from the built-in pictures.
TiledImage formedPencil(const QString& form, int strength) {
    static QTemporaryDir folder;
    static const BrushResourceLibrary library(folder.path());
    RebuiltStroke::setResources(&library);
    Document doc(QSize(200, 80), Qt::white);
    BrushSettings settings = defaultSettings(ToolType::Pencil);
    settings.size = 300;
    settings.edge = 0;
    settings.form = form;
    settings.formStrength = strength;
    StrokeController stroke(&doc);
    drawLine(stroke, settings);
    stroke.finish();
    RebuiltStroke::setResources(nullptr);
    return doc.currentLayer()->pixels;
}

}

// A blot map at Mess. 0 plays no part: whichever picture it is, the stroke is the bare cone through its Shape's window, as the
// reference paints it.
TEST(FormedStamps, AtStrengthZeroABlotMapsPicturePlaysNoPart) {
    const TiledImage blots = formedPencil(QStringLiteral("Blots"), 0);
    const TiledImage fine = formedPencil(QStringLiteral("Fine Blots"), 0);
    for (int y = 0; y < 80; ++y)
        for (int x = 0; x < 200; ++x)
            ASSERT_EQ(blots.pixel(x, y), fine.pixel(x, y)) << x << ", " << y;
}

// A blot map lies on the canvas as a second paper, repeating from its top left at its own size times Scale; it doesn't travel with
// the stamps (the reference's, painted: a stroke moved by the picture's width is the same, moved, and moved by less it isn't).
TEST(StampForm, ABlotMapIsFixedToTheCanvas) {
    QImage picture = greyPicture(8, 8, 255);
    for (int x = 0; x < 4; ++x)
        for (int y = 0; y < 8; ++y)
            picture.setPixel(x, y, qRgb(0, 0, 0));  // its left half dark, its right half light
    const StampForm blot(picture, 1.0, 1.0, false, true);
    EXPECT_FLOAT_EQ(blot.blotShape(0.0f, 10.0f, 0, 0, 4), blot.blotShape(0.0f, 10.0f, 8, 16, 4)) << "it repeats every 8 px";
    EXPECT_GT(blot.blotShape(0.0f, 10.0f, 1, 0, 4), blot.blotShape(0.0f, 10.0f, 5, 0, 4)) << "its light parts take away";
    EXPECT_FLOAT_EQ(blot.blotShape(0.0f, 10.0f, 1, 0, 4), blot.blotShape(0.0f, 300.0f, 1, 0, 4)) << "whatever the tip's size";
    EXPECT_FLOAT_EQ(blot.blotShape(20.0f, 10.0f, 1, 0, 4), 0.0f) << "past the cone";
    const StampForm doubled(picture, 2.0, 1.0, false, true);
    EXPECT_FLOAT_EQ(doubled.blotShape(0.0f, 10.0f, 7, 0, 4), blot.blotShape(0.0f, 10.0f, 3, 0, 4)) << "at Scale 200 % it's 16 px across";
}

// Doodlebold's own blot maps leave their white ground empty, so no band runs along a stroke's middle (feedback: "I dont like the
// line"): from Mess. 25 up, nothing at the cone's middle where the picture is white, on every Shape and window; at Mess. 0 the
// plain round stamp still; where the picture is dark, the rule as it was.
TEST(StampForm, DoodleboldsOwnBlotMapsLeaveTheirWhiteGroundEmpty) {
    const QImage white = greyPicture(8, 8, 255), black = greyPicture(8, 8, 0);
    using Window = StampForm::BlotWindow;
    for (const Window window : {Window::Stamp, Window::Mixing, Window::BrushFifth, Window::MarkerSoft})
        for (int edge = 0; edge < kEdgePresets; ++edge) {
            const auto shape = [&](const QImage& picture, double mess, bool groundEmpty) {
                const StampForm form(picture, 1.0, mess, false, true, false, false, false, false, false, groundEmpty);
                return form.blotShape(0.5f, 10.0f, 3, 3, edge, window);
            };
            EXPECT_GT(shape(white, 0.5, false), 0.0f) << "the band, edge " << edge;
            for (const double mess : {0.25, 0.5, 0.75, 1.0})
                EXPECT_FLOAT_EQ(shape(white, mess, true), 0.0f) << "edge " << edge << ", Mess. " << mess;
            EXPECT_FLOAT_EQ(shape(white, 0.0, true), shape(white, 0.0, false)) << "Mess. 0, edge " << edge;
            EXPECT_LE(shape(white, 0.1, true), shape(white, 0.1, false)) << "a narrower band at a low Mess., edge " << edge;
            if (window == Window::Stamp && edge == 0) {  // the narrow windows are still whole there, only further out less
                EXPECT_LT(shape(white, 0.1, true), shape(white, 0.1, false));
            }
            for (const double mess : {0.0, 0.25, 0.5, 1.0})
                EXPECT_FLOAT_EQ(shape(black, mess, true), shape(black, mess, false)) << "the blots, edge " << edge;
        }
}

// Only the pictures that come with Doodlebold: a user's own blot map, even one that takes a built-in's name, paints by the rule alone.
TEST(StampForm, OnlyTheBuiltInBlotMapsLeaveTheirGroundEmpty) {
    QTemporaryDir folder;
    BrushResourceLibrary library(folder.path());
    for (const char* name : {"Blots", "Fine Blots", "Mottled Blots"})
        EXPECT_TRUE(groundLaysNothing(*library.form(QString::fromLatin1(name)))) << name;
    for (const char* name : {"Chalk", "Flat Nib", "Round Bristle"})
        EXPECT_FALSE(groundLaysNothing(*library.form(QString::fromLatin1(name)))) << name;
    ASSERT_TRUE(library
                    .add({{ResourceKind::BlotMap, QStringLiteral("Blots"), greyPicture(8, 8, 255)},
                          {ResourceKind::BlotMap, QStringLiteral("My Blots"), greyPicture(8, 8, 255)}})
                    .ok());
    EXPECT_FALSE(library.form(QStringLiteral("Blots"))->builtIn) << "the user's takes its place";
    EXPECT_FALSE(groundLaysNothing(*library.form(QStringLiteral("Blots"))));
    EXPECT_FALSE(groundLaysNothing(*library.form(QStringLiteral("My Blots"))));
}

TEST(FormedStamps, AFormBreaksTheLineIntoItsPicture) {
    const TiledImage round = formedPencil(BrushResourceLibrary::kSimpleCircle, 100);
    const TiledImage formed = formedPencil(QStringLiteral("Blots"), 100);
    int differ = 0;
    for (int x = 40; x < 160; ++x)
        differ += formed.pixel(x, 40) != round.pixel(x, 40);
    EXPECT_GT(differ, 10) << "the middle of the line isn't the round Pencil's";
    EXPECT_EQ(formed.pixel(100, 5)[3], 0) << "nothing far outside it";
}

namespace {

// A mixing tool at 30 px along y = 40.5 with a form from the built-in pictures, over a Pencil line across its path for Blur and Smudge
// to move.
TiledImage formedMixing(ToolType tool, const QString& form) {
    static QTemporaryDir folder;
    static const BrushResourceLibrary library(folder.path());
    RebuiltStroke::setResources(&library);
    Document doc(QSize(200, 80), Qt::white);
    if (tool == ToolType::Blur || tool == ToolType::Smudge) {
        BrushSettings line = defaultSettings(ToolType::Pencil);
        line.size = 100;
        StrokeController across(&doc);
        EXPECT_TRUE(across.begin(pen(100, 5, 1.0), Qt::black, line));
        for (int y = 10; y <= 75; y += 5)
            across.add(pen(100, y, 1.0));
        across.finish();
    }
    BrushSettings settings = defaultSettings(tool);
    settings.size = 300;
    settings.form = form;
    settings.formStrength = 100;
    StrokeController stroke(&doc);
    drawLine(stroke, settings);
    stroke.finish();
    RebuiltStroke::setResources(nullptr);
    return doc.currentLayer()->pixels;
}

}

// A form is the mixing tools' outline too: their dabs take the picture, and Blur and Smudge move what's under only where it is.
TEST(FormedDabs, AFormShapesEachMixingTool) {
    for (const ToolType tool : {ToolType::Brush, ToolType::WaterColor, ToolType::Marker, ToolType::Blur, ToolType::Smudge}) {
        const TiledImage round = formedMixing(tool, BrushResourceLibrary::kSimpleCircle);
        const TiledImage formed = formedMixing(tool, QStringLiteral("Blots"));
        int differ = 0;
        for (int y = 20; y <= 60; y += 2)  // across the whole line: a blot map's outline is its own
            for (int x = 40; x < 160; ++x)
                differ += formed.pixel(x, y) != round.pixel(x, y);
        // A blot map's Smudge is still open: it reads the picture only inside its own round reach, where at 30 px the
        // mixing tools' narrow window cuts nothing of this picture.
        if (tool != ToolType::Smudge) {
            EXPECT_GT(differ, 20) << "tool " << int(tool) << ": the line isn't its round self";
        }
        EXPECT_EQ(formed.pixel(30, 5), round.pixel(30, 5)) << "tool " << int(tool) << ": nothing far outside it";
    }
}

// Erasing turns the paper over with the switch on, as the reference's does, the Eraser and any tool painting the transparent colour
// alike: with the picture read dark as paint (Invert), they take most where it's light. Off, they take most where it's dark, as
// painting lays paint.
TEST(PaperGrain, ErasingTurnsThePaperOverWithItsSwitch) {
    static QTemporaryDir folder;
    static const BrushResourceLibrary library(folder.path());
    RebuiltStroke::setResources(&library);
    const QImage picture = library.texture(QStringLiteral("Paper Grain"))->image.convertToFormat(QImage::Format_Grayscale8);
    const auto takenBy = [](ToolType tool, const QColor& colour, bool switchOn) {
        BrushSettings settings = defaultSettings(tool);
        settings.size = 300;
        settings.texture = QStringLiteral("Paper Grain");
        settings.textureInvert = true;  // dark where painting goes, as Doodlebold draws its pictures
        settings.textureEraseLow = switchOn;
        Document doc(QSize(200, 80), Qt::white);
        doc.paintTarget(doc.currentLayerId()).value()->fill(Qt::black);
        drawPixelArt(doc, settings, colour);
        return doc.currentLayer()->pixels;
    };
    // How the share taken follows the picture's lightness, over the stroke's middle: Pearson's correlation.
    const auto withLightness = [&picture](const TiledImage& layer) {
        double n = 0, sx = 0, sy = 0, sxx = 0, syy = 0, sxy = 0;
        for (int y = 32; y <= 48; ++y)
            for (int x = 40; x < 160; ++x) {
                const double light = qGray(picture.pixel(x % picture.width(), y % picture.height())) / 255.0;
                const double taken = 1.0 - layer.pixel(x, y)[3] / double(kChannelOne);
                n += 1, sx += light, sy += taken, sxx += light * light, syy += taken * taken, sxy += light * taken;
            }
        return (n * sxy - sx * sy) / std::sqrt((n * sxx - sx * sx) * (n * syy - sy * sy));
    };
    // A transparent colour's near-full coat clears most pixels outright, so its correlations are weaker than the Eraser's.
    EXPECT_GT(withLightness(takenBy(ToolType::Eraser, Qt::black, true)), 0.5) << "the Eraser, on: most where the picture is light";
    EXPECT_GT(withLightness(takenBy(ToolType::Pencil, Qt::transparent, true)), 0.1) << "a transparent colour, on: the same";
    EXPECT_LT(withLightness(takenBy(ToolType::Eraser, Qt::black, false)), -0.5) << "the Eraser, off: most where it's dark";
    EXPECT_LT(withLightness(takenBy(ToolType::Pencil, Qt::transparent, false)), -0.1) << "a transparent colour, off: the same";
    RebuiltStroke::setResources(nullptr);
}

// The Smudge leaves the paper as it is: with a texture it smudges as without, to the bit.
TEST(PaperGrain, TheSmudgeIgnoresIt) {
    static QTemporaryDir folder;
    static const BrushResourceLibrary library(folder.path());
    RebuiltStroke::setResources(&library);
    const auto smudged = [](const QString& texture) {
        Document doc(QSize(200, 80), Qt::white);
        drawPixelArt(doc, defaultSettings(ToolType::Pencil), Qt::black);  // a line along y = 40.5 to smudge
        BrushSettings smudge = defaultSettings(ToolType::Smudge);
        smudge.size = 300;
        smudge.texture = texture;
        StrokeController stroke(&doc);
        EXPECT_TRUE(stroke.begin(pen(100, 5, 1.0), Qt::black, smudge));
        for (int y = 10; y <= 75; y += 5)
            stroke.add(pen(100, y, 1.0));
        stroke.finish();
        return doc.currentLayer()->pixels;
    };
    const TiledImage textured = smudged(QStringLiteral("Paper Grain")), plain = smudged(BrushResourceLibrary::kNoTexture);
    RebuiltStroke::setResources(nullptr);
    int moved = 0;
    for (int y = 0; y < 80; ++y)
        for (int x = 0; x < 200; ++x) {
            for (int c = 0; c < 4; ++c)
                ASSERT_EQ(textured.pixel(x, y)[c], plain.pixel(x, y)[c]) << x << ", " << y;
            moved += plain.pixel(x, 60)[3] > 0 ? 1 : 0;
        }
    EXPECT_GT(moved, 0) << "the line was smudged";
}

// The Marker's soft Shapes lay their edge where the reference's does. On its C at 35 px, ours laid the 2nd Shape's edge 0.85 px out
// at half the line's top on either side; read further in (kMarkerEdgeShiftSmall), with its blur, this line is 1.3 px narrower there.
TEST(MarkerEdges, TheSecondShapesEdgeSitsWhereTheReferencesDoes) {
    Document doc(QSize(200, 80), Qt::white);
    BrushSettings settings = defaultSettings(ToolType::Marker);
    settings.size = 350;
    settings.edge = 1;
    settings.sharpness = 50;
    StrokeController stroke(&doc);
    drawLine(stroke, settings);
    stroke.finish();
    // its width at half its top, each side's crossing read between the two pixels either side of it, over the line's middle
    double across = 0.0;
    for (int x = 80; x < 120; ++x) {
        int top = 0;
        for (int y = 0; y < 80; ++y)
            top = std::max(top, alphaAt(doc, x, y));
        const double half = top / 2.0;
        double from = 0.0, to = 0.0;
        for (int y = 1; y < 80; ++y) {
            const double above = alphaAt(doc, x, y - 1), here = alphaAt(doc, x, y);
            if (above < half && here >= half)
                from = y - 1 + (half - above) / (here - above);
            if (above >= half && here < half)
                to = y - 1 + (above - half) / (above - here);
        }
        across += (to - from) / 40.0;
    }
    EXPECT_LT(across, 31.5);  // 32.18 before, 30.84 now
    EXPECT_GT(across, 30.2);
}

// The Brush's 5th Shape with a blot map lays nothing between the blots, as the reference's does at Strength 100. Its skirt, put back
// on by the Brush's own shaping after the form had taken it off, laid the round stamp's paint there, a wash of about 0.4.
TEST(FormedDabs, TheBrushLaysNothingBetweenItsBlots) {
    QTemporaryDir folder;
    const BrushResourceLibrary library(folder.path());
    RebuiltStroke::setResources(&library);
    const BrushResource* blots = library.form(QStringLiteral("Blots"));
    ASSERT_TRUE(blots && !blots->image.isNull());
    const QImage picture = blots->image.convertToFormat(QImage::Format_Grayscale8);
    Document doc(QSize(400, 120), Qt::white);
    BrushSettings settings = defaultSettings(ToolType::Brush);
    settings.size = 350;
    settings.form = QStringLiteral("Blots");
    settings.formStrength = 100;
    StrokeController stroke(&doc);
    ASSERT_TRUE(stroke.begin(pen(20, 60.5, 1.0), Qt::black, settings));
    for (int x = 30; x <= 380; x += 10)
        stroke.add(pen(x, 60.5, 1.0));
    stroke.finish();
    RebuiltStroke::setResources(nullptr);
    // Where the picture is white two pixels all round, the reference lays none; inside its darkest parts, it lays the stroke.
    const auto lightAround = [&](int x, int y, bool white) {
        for (int dy = -2; dy <= 2; ++dy)
            for (int dx = -2; dx <= 2; ++dx) {
                const int px = ((x + dx) % picture.width() + picture.width()) % picture.width();
                const int py = ((y + dy) % picture.height() + picture.height()) % picture.height();
                const int grey = picture.constScanLine(py)[px];
                if (white ? grey < 255 : grey > 0)
                    return false;
            }
        return true;
    };
    int between = 0, laidBetween = 0, inBlots = 0, laidInBlots = 0;
    for (int y = 52; y < 69; ++y)  // the line's middle
        for (int x = 60; x < 340; ++x) {
            const int alpha = alphaAt(doc, x, y);
            if (lightAround(x, y, true)) {
                ++between;
                laidBetween += alpha > kChannelOne / 100 ? 1 : 0;
            } else if (lightAround(x, y, false)) {
                ++inBlots;
                laidInBlots += alpha > kChannelOne / 2 ? 1 : 0;
            }
        }
    ASSERT_GT(between, 200);
    ASSERT_GT(inBlots, 200);
    EXPECT_EQ(laidBetween, 0);
    EXPECT_GT(laidInBlots, inBlots * 9 / 10);
}

namespace {

// A brush shape's stroke on an empty layer: a mouse click at `at`, or a pen line through `points` at pressure 0.8.
TiledImage shapeStroke(ToolType tool, const QString& form, int size, const std::vector<QPointF>& points, int angleControl = 0,
                       const std::function<void(BrushSettings&)>& adjust = {}) {
    Document doc(QSize(320, 240), Qt::white);
    BrushSettings settings = defaultSettings(tool);
    settings.size = size * 10;  // tenths of a pixel
    settings.form = form;
    settings.shapeAngleControl = angleControl;
    if (adjust)
        adjust(settings);
    StrokeController stroke(&doc);
    InputSample first = pen(points.front().x(), points.front().y(), 0.8);
    if (points.size() == 1)
        first.kind = PointerKind::Mouse;
    EXPECT_TRUE(stroke.begin(first, Qt::black, settings));
    for (std::size_t i = 1; i < points.size(); ++i)
        stroke.add(pen(points[i].x(), points[i].y(), 0.8));
    stroke.finish();
    return doc.currentLayer()->pixels;
}

int alphaOf(const TiledImage& image, int x, int y) {
    return image.pixel(x, y)[3];
}

}

// The reference stamps a brush shape's picture whole: a click lays the picture itself, its lightness the alpha, bilinear between its
// pixels, centred and as wide as the size, on the Pencil and the Brush alike (the two tools' clicks the same
// to the pixel). Any picture: Soft Edge and Chalk, each at its own size, where no halved copy is read.
TEST(BrushShapes, AClickLaysItsPictureWholeOnEveryTool) {
    QTemporaryDir folder;
    const BrushResourceLibrary library(folder.path());
    RebuiltStroke::setResources(&library);
    for (const QString& form : {QStringLiteral("Soft Edge"), QStringLiteral("Chalk")}) {
        const QImage picture = library.form(form)->image.convertToFormat(QImage::Format_Grayscale8);
        const int side = std::max(picture.width(), picture.height());
        // A brush shape paints where it's light.
        const auto dark = [&](int x, int y) {
            return picture.constScanLine(std::clamp(y, 0, picture.height() - 1))[std::clamp(x, 0, picture.width() - 1)] / 255.0;
        };
        TiledImage first;
        for (const ToolType tool : {ToolType::Pencil, ToolType::Brush}) {
            const TiledImage laid = shapeStroke(tool, form, side, {QPointF(160.5, 120.5)});
            int off = 0, painted = 0;
            for (int y = 0; y < 240; ++y)
                for (int x = 0; x < 320; ++x) {
                    const double px = (x + 0.5 - 160.5) + picture.width() / 2.0 - 0.5;
                    const double py = (y + 0.5 - 120.5) + picture.height() / 2.0 - 0.5;
                    double expected = 0.0;
                    if (px >= -0.5 && py >= -0.5 && px <= picture.width() - 0.5 && py <= picture.height() - 0.5) {
                        const int x0 = static_cast<int>(std::floor(px)), y0 = static_cast<int>(std::floor(py));
                        const double fx = std::clamp(px - x0, 0.0, 1.0), fy = std::clamp(py - y0, 0.0, 1.0);
                        expected = (dark(x0, y0) * (1 - fx) + dark(x0 + 1, y0) * fx) * (1 - fy) +
                                   (dark(x0, y0 + 1) * (1 - fx) + dark(x0 + 1, y0 + 1) * fx) * fy;
                    }
                    const double alpha = alphaOf(laid, x, y) / double(kChannelOne);
                    off += std::abs(alpha - expected) > 3.0 / 255.0 ? 1 : 0;
                    painted += alpha > 0.0 ? 1 : 0;
                }
            EXPECT_GT(painted, 5000) << form.toStdString() << " tool " << int(tool);
            EXPECT_LT(off, painted / 200) << form.toStdString() << " tool " << int(tool);
            if (tool == ToolType::Pencil)
                first = laid;
            else
                for (int y = 0; y < 240; y += 3)
                    for (int x = 0; x < 320; x += 3)
                        ASSERT_EQ(alphaOf(laid, x, y), alphaOf(first, x, y)) << form.toStdString() << " at " << x << ", " << y;
        }
    }
    RebuiltStroke::setResources(nullptr);
}

// A brush shape's stamps lay over each other, as the reference's do: one Soft Edge stamp lays 254/255 at its middle, and a line's
// middle, where stamps overlap, builds up past it (the reference's C 0.9999); keeping the most of them would hold it at 254/255.
TEST(BrushShapes, ItsStampsLayOverEachOther) {
    QTemporaryDir folder;
    const BrushResourceLibrary library(folder.path());
    RebuiltStroke::setResources(&library);
    const TiledImage click = shapeStroke(ToolType::Pencil, QStringLiteral("Soft Edge"), 120, {QPointF(160.5, 120.5)});
    std::vector<QPointF> points;
    for (int x = 20; x <= 300; x += 10)
        points.emplace_back(x, 120.5);
    const TiledImage line = shapeStroke(ToolType::Pencil, QStringLiteral("Soft Edge"), 120, points);
    RebuiltStroke::setResources(nullptr);
    const double one = alphaOf(click, 160, 120) / double(kChannelOne);
    EXPECT_NEAR(one, 254.0 / 255.0, 2.0 / 255.0);
    EXPECT_GT(alphaOf(line, 160, 120) / double(kChannelOne), 1.0 - (1.0 - one) / 2.0);
}

// With Angle Control "along the stroke" a brush shape turns with the way the stroke goes: Flat Nib's line is as wide across on the
// diagonal as across the level; fixed, the diagonal crosses the nib.
TEST(BrushShapes, AlongTheStrokeTheShapeTurnsWithIt) {
    QTemporaryDir folder;
    const BrushResourceLibrary library(folder.path());
    RebuiltStroke::setResources(&library);
    const auto widthAcross = [](ToolType tool, bool diagonal, int control) {
        std::vector<QPointF> points;
        const double dx = diagonal ? std::sqrt(0.5) : 1.0, dy = diagonal ? std::sqrt(0.5) : 0.0;
        for (int i = 0; i <= 20; ++i)
            points.emplace_back(60.0 + dx * i * 8.0, (diagonal ? 40.0 : 120.0) + dy * i * 8.0);
        const TiledImage doc = shapeStroke(tool, QStringLiteral("Flat Nib"), 50, points, control);
        double lo = 1e9, hi = -1e9;
        for (int y = 0; y < 240; ++y)
            for (int x = 0; x < 320; ++x) {
                const double ox = x + 0.5 - 60.0, oy = y + 0.5 - (diagonal ? 40.0 : 120.0);
                const double along = ox * dx + oy * dy, across = -ox * dy + oy * dx;
                if (along > 50.0 && along < 110.0 && alphaOf(doc, x, y) > kChannelOne / 2) {
                    lo = std::min(lo, across);
                    hi = std::max(hi, across);
                }
            }
        return hi - lo;
    };
    for (const ToolType tool : {ToolType::Pencil, ToolType::Brush}) {
        EXPECT_NEAR(widthAcross(tool, true, 1), widthAcross(tool, false, 1), 2.0) << "tool " << int(tool);
        EXPECT_GT(widthAcross(tool, true, 0), widthAcross(tool, false, 0) + 10.0) << "tool " << int(tool);
    }
    RebuiltStroke::setResources(nullptr);
}

// Flat Nib with its own settings file, held still and its stamps 1 % of the size apart, lays a level line whose edge, traced
// by the nib's corners, runs straight: no stamp shows. At the shapes' usual Spacing, 25, each stamp's corner stands out as a step.
TEST(BrushShapes, FlatNibsOwnSettingsLayNoSteps) {
    QTemporaryDir folder;
    const BrushResourceLibrary library(folder.path());
    RebuiltStroke::setResources(&library);
    const ShapeSettings nib = *library.form(QStringLiteral("Flat Nib"))->shapeSettings;
    const auto edgeWobble = [&](int spacing) {
        std::vector<QPointF> points;
        for (int x = 40; x <= 280; x += 8)
            points.emplace_back(x, 120.5);
        const TiledImage doc =
            shapeStroke(ToolType::Brush, QStringLiteral("Flat Nib"), 60, points, nib.angleControl, [&](BrushSettings& s) {
                applyShapeSettings(s, nib);
                s.shapeSpacing = spacing;
            });
        double lo = 1e9, hi = -1e9;
        for (int x = 100; x < 220; ++x)  // the line's middle: where its top edge crosses half cover, between whole pixels
            for (int y = 1; y < 120; ++y)
                if (alphaOf(doc, x, y) >= kChannelOne / 2) {
                    const double above = alphaOf(doc, x, y - 1), here = alphaOf(doc, x, y), half = kChannelOne / 2.0;
                    const double edge = y - (here - half) / std::max(here - above, 1.0);
                    lo = std::min(lo, edge);
                    hi = std::max(hi, edge);
                    break;
                }
        return hi - lo;
    };
    EXPECT_LT(edgeWobble(nib.spacing), 0.25);
    EXPECT_GT(edgeWobble(25), 2.0);
    RebuiltStroke::setResources(nullptr);
}

// Flat Nib's stamps, 1 % apart, lay as one stroke: its line's tone is Dens. times the tool's Density, the same all along it, not
// built up to solid by the close stamps as another shape's would be, a click's the same; and its footprint is Dens. 100's at any Dens.
TEST(BrushShapes, FlatNibsDensitySetsItsTone) {
    QTemporaryDir folder;
    const BrushResourceLibrary library(folder.path());
    RebuiltStroke::setResources(&library);
    const ShapeSettings nib = *library.form(QStringLiteral("Flat Nib"))->shapeSettings;
    std::vector<QPointF> points;
    for (int x = 40; x <= 280; x += 8)
        points.emplace_back(x, 120.5);
    const auto line = [&](ToolType tool, int dens, int density) {
        return shapeStroke(tool, QStringLiteral("Flat Nib"), 60, points, nib.angleControl, [&](BrushSettings& s) {
            applyShapeSettings(s, nib);
            s.shapeDensity = dens;
            s.density = density;
            s.pressureDensity = false;  // the pen's 0.8 would lighten it too
        });
    };
    const auto painted = [](const TiledImage& doc) {
        int count = 0;
        for (int y = 60; y < 180; ++y)
            count += alphaOf(doc, 160, y) > 0 ? 1 : 0;
        return count;
    };
    constexpr double kOneLevel = 1.0 / 255.0;
    for (const ToolType tool : {ToolType::Pencil, ToolType::Brush}) {
        const TiledImage full = line(tool, 100, 100);
        for (const auto& [dens, density, tone] :
             {std::tuple{50, 100, 0.5}, std::tuple{25, 100, 0.25}, std::tuple{100, 50, 0.5}, std::tuple{50, 50, 0.25}}) {
            const TiledImage doc = line(tool, dens, density);
            for (const int x : {80, 160, 240})
                EXPECT_NEAR(alphaOf(doc, x, 120) / double(kChannelOne), tone, kOneLevel)
                    << "tool " << int(tool) << " Dens. " << dens << " Density " << density << " at " << x;
            EXPECT_EQ(painted(doc), painted(full)) << "tool " << int(tool) << " Dens. " << dens;
        }
        // One stamp alone, a click, fills its outline as one at Dens. 100 fills the whole.
        const TiledImage click =
            shapeStroke(tool, QStringLiteral("Flat Nib"), 60, {QPointF(160.5, 120.5)}, nib.angleControl, [&](BrushSettings& s) {
                applyShapeSettings(s, nib);
                s.shapeDensity = 50;
            });
        EXPECT_NEAR(alphaOf(click, 160, 120) / double(kChannelOne), 0.5, kOneLevel) << "tool " << int(tool);
    }
    // Water Color's own wash, about 0.7 at Dens. 100, falls with Dens. too, though not in proportion: at full pressure its pick-up,
    // lighter over a lighter wash, takes Dens. 50 a little under half (0.31 of 0.71 on 100 px lines); below it, as at this pen's 0.8,
    // its melt moves what's under toward the blurred wash, which the outline doesn't hold, and Dens. 50 keeps about three quarters (0.54
    // of 0.70; open).
    const double washFull = alphaOf(line(ToolType::WaterColor, 100, 100), 160, 120) / double(kChannelOne);
    const double washHalf = alphaOf(line(ToolType::WaterColor, 50, 100), 160, 120) / double(kChannelOne);
    const double washQuarter = alphaOf(line(ToolType::WaterColor, 25, 100), 160, 120) / double(kChannelOne);
    EXPECT_GT(washFull, 0.6);
    EXPECT_LT(washHalf, 0.85 * washFull);
    EXPECT_LT(washQuarter, 0.85 * washHalf);
    RebuiltStroke::setResources(nullptr);
}

// The Eraser takes a brush shape away as the shape itself, as it paints, though Invert for Transparency is on from the start: turned
// over, the stamp, whole, erased the square around the picture and left the picture.
TEST(BrushShapes, TheEraserTakesTheShapeAway) {
    QTemporaryDir folder;
    const BrushResourceLibrary library(folder.path());
    RebuiltStroke::setResources(&library);
    TiledImage image(QSize(320, 240));
    image.fill(QColor(200, 60, 40));
    BrushSettings settings = defaultSettings(ToolType::Eraser);
    ASSERT_TRUE(settings.formEraseLow);
    settings.size = 2000;
    settings.form = QStringLiteral("Soft Edge");
    RebuiltStroke stroke;
    InputSample click = pen(160.5, 120.5, 1.0);
    click.kind = PointerKind::Mouse;
    stroke.begin(&image, Qt::black, settings, click);
    stroke.end();
    RebuiltStroke::setResources(nullptr);
    EXPECT_LT(image.pixel(160, 120)[3], kChannelOne / 10) << "the core is taken away";
    EXPECT_GT(image.pixel(160 + 95, 120 + 95)[3], kChannelOne * 9 / 10) << "the square's corner is left";
}

// On a soft Shape a mixing tool takes the paper dab by dab, as the Pencil does: each stamp's dab less the paper's darkness times
// Intensity, down to nothing. So with a texture its faint fringe keeps only the paper's tops, as the reference's: at Shape 1 its
// textured Brush, Marker and Water Color lay about 0.4 of their plain lines' pixels, where taking the paper afterwards kept all of them.
TEST(PaperGrain, AMixingToolsSoftShapeTakesThePaperDabByDab) {
    static QTemporaryDir folder;
    static const BrushResourceLibrary library(folder.path());
    RebuiltStroke::setResources(&library);
    const auto painted = [](ToolType tool, bool textured) {
        BrushSettings settings = defaultSettings(tool);
        settings.size = 600;
        settings.edge = 0;
        if (textured) {
            settings.texture = QStringLiteral("Paper Grain");
            settings.textureStrength = 95;
        }
        Document doc(QSize(240, 80), Qt::white);
        StrokeController stroke(&doc);
        EXPECT_TRUE(stroke.begin(pen(20, 40.5, 0.7), Qt::black, settings));
        for (int x = 30; x <= 220; x += 6)
            stroke.add(pen(x, 40.5, 0.7));
        stroke.finish();
        int count = 0;
        for (int y = 0; y < 80; ++y)
            for (int x = 40; x < 200; ++x)
                count += alphaAt(doc, x, y) > kChannelOne / 500 ? 1 : 0;
        return count;
    };
    for (const ToolType tool : {ToolType::Brush, ToolType::Marker, ToolType::WaterColor}) {
        const int plain = painted(tool, false), textured = painted(tool, true);
        EXPECT_GT(plain, 3000) << "tool " << int(tool);
        EXPECT_LT(textured, plain * 3 / 4) << "tool " << int(tool);
    }
    RebuiltStroke::setResources(nullptr);
}

// On the softer Shapes a blot map eats into the stamp tools' stamps less deep, through a wider window, as the reference's: the
// Pencil with Blots at Strength 50 lays nearly all of its plain line's pixels at Shape 1, where the 5th Shape's depth left 12 % fewer
// than the reference's.
TEST(FormedDabs, AtTheSoftestShapeABlotMapEatsLessDeep) {
    QTemporaryDir folder;
    const BrushResourceLibrary library(folder.path());
    RebuiltStroke::setResources(&library);
    const auto painted = [](const QString& form) {
        BrushSettings settings = defaultSettings(ToolType::Pencil);
        settings.size = 600;
        settings.edge = 0;
        settings.form = form;
        settings.formStrength = 50;
        Document doc(QSize(240, 80), Qt::white);
        StrokeController stroke(&doc);
        EXPECT_TRUE(stroke.begin(pen(20, 40.5, 0.7), Qt::black, settings));
        for (int x = 30; x <= 220; x += 6)
            stroke.add(pen(x, 40.5, 0.7));
        stroke.finish();
        double sum = 0.0;
        for (int y = 0; y < 80; ++y)
            for (int x = 40; x < 200; ++x)
                sum += alphaAt(doc, x, y) / double(kChannelOne);
        return sum;
    };
    const double plain = painted(BrushResourceLibrary::kSimpleCircle), blots = painted(QStringLiteral("Blots"));
    RebuiltStroke::setResources(nullptr);
    EXPECT_GT(blots, plain * 0.8);
}

namespace {

// A bristle map of 63 x 63, its hairs at these pixels, in a library folder of its own.
void writeBristleMap(const QString& folder, const QString& name, const std::vector<QPoint>& hairs) {
    QImage map(63, 63, QImage::Format_Grayscale8);
    map.fill(255);
    for (const QPoint& hair : hairs)
        map.setPixel(hair, qRgb(0, 0, 0));
    ASSERT_TRUE(QDir().mkpath(folder + QStringLiteral("/forms/bristles")));
    ASSERT_TRUE(map.save(folder + QStringLiteral("/forms/bristles/") + name + QStringLiteral(".png")));
}

// A Pencil stroke with a bristle map, straight along y = 120 from x = 40 to 280, Size 100 px.
TiledImage bristleLine(const QString& form, int bristleSize) {
    Document doc(QSize(320, 240), Qt::white);
    BrushSettings settings = defaultSettings(ToolType::Pencil);
    settings.size = 1000;  // tenths of a pixel
    settings.pressureSize = false;
    settings.form = form;
    settings.bristleSize = bristleSize;
    StrokeController stroke(&doc);
    EXPECT_TRUE(stroke.begin(pen(40.0, 120.0, 1.0), Qt::black, settings));
    for (double x = 50.0; x <= 280.0; x += 10.0)
        stroke.add(pen(x, 120.0, 1.0));
    stroke.finish();
    return doc.currentLayer()->pixels;
}

}

// Each dark pixel of a bristle map is a hair of its own, a line along the stroke at its place across the map, the map's right to the
// stroke's right, the map's longest side kHairSpreadShare of the size: two hairs at either side of the map leave two lines,
// nothing between them. Each is kHairWidthShare of the size wide, times Bristle Size.
TEST(Bristles, EachHairIsALineOfItsOwnAtItsPlaceAcross) {
    QTemporaryDir folder;
    writeBristleMap(folder.path(), QStringLiteral("Two Hairs"), {QPoint(10, 31), QPoint(52, 31)});
    const BrushResourceLibrary library(folder.path());
    RebuiltStroke::setResources(&library);
    const TiledImage whole = bristleLine(QStringLiteral("Two Hairs"), 100);
    const TiledImage half = bristleLine(QStringLiteral("Two Hairs"), 50);
    RebuiltStroke::setResources(nullptr);
    // Going right, the stroke's right is down the canvas: map column 52 lands (52.5 - 31.5) / 31.5 of the map's half span below.
    const double halfSpan = kHairSpreadShare * 50.0;
    const int below = 120 + static_cast<int>(std::lround((52.5 - 31.5) / 31.5 * halfSpan));
    const int above = 120 + static_cast<int>(std::lround((10.5 - 31.5) / 31.5 * halfSpan));
    EXPECT_GT(alphaOf(whole, 160, below), kChannelOne / 2);
    EXPECT_GT(alphaOf(whole, 160, above), kChannelOne / 2);
    EXPECT_EQ(alphaOf(whole, 160, 120), 0) << "nothing between the hairs";
    const auto widthAt = [](const TiledImage& image, int y0) {
        int painted = 0;
        for (int y = y0 - 15; y <= y0 + 15; ++y)
            painted += alphaOf(image, 160, y) > kChannelOne / 2 ? 1 : 0;
        return painted;
    };
    EXPECT_NEAR(widthAt(whole, below), kHairWidthShare * 100.0, 1.5) << "a hair kHairWidthShare of the size wide";
    EXPECT_NEAR(widthAt(half, below), kHairWidthShare * 50.0, 1.5);
}

// The Marker lays a bristle map's hairs too: two hairs at the map's sides leave two lines and nothing between them, where laid as
// round stamps its events filled the band between.
TEST(Bristles, TheMarkerLaysItsHairs) {
    QTemporaryDir folder;
    writeBristleMap(folder.path(), QStringLiteral("Two Hairs"), {QPoint(10, 31), QPoint(52, 31)});
    const BrushResourceLibrary library(folder.path());
    RebuiltStroke::setResources(&library);
    Document doc(QSize(320, 240), Qt::white);
    BrushSettings settings = defaultSettings(ToolType::Marker);
    settings.size = 1000;  // tenths of a pixel
    settings.pressureSize = false;
    settings.form = QStringLiteral("Two Hairs");
    StrokeController stroke(&doc);
    EXPECT_TRUE(stroke.begin(pen(40.0, 120.0, 1.0), Qt::black, settings));
    for (double x = 50.0; x <= 280.0; x += 10.0)
        stroke.add(pen(x, 120.0, 1.0));
    stroke.finish();
    RebuiltStroke::setResources(nullptr);
    const TiledImage& line = doc.currentLayer()->pixels;
    const double halfSpan = kHairSpreadShare * 50.0;
    const int below = 120 + static_cast<int>(std::lround((52.5 - 31.5) / 31.5 * halfSpan));
    EXPECT_GT(alphaOf(line, 160, below), 0);
    EXPECT_EQ(alphaOf(line, 160, 120), 0) << "nothing between the hairs";
}

// A hair whose path swings sideways or back as the stroke turns about it, the inner hair of a tight turn, is laid whole all the same, as
// the reference's is (it used to fade).
TEST(Bristles, AHairThatSwingsBackIsLaidWhole) {
    StampTip tip{20.0, 1.0, kEdgePresets - 1, 0.0};
    auto bristles = std::make_shared<Bristles>();
    bristles->hairs.push_back({1.0, 0.0});  // at the stroke's right
    tip.bristles = bristles;
    tip.bristleSpread = 20.0;
    tip.hairRadius = 2.0;
    StampWalk walk(tip);
    walk.begin(pen(0.0, 0.0, 1.0), {});
    std::vector<Stamp> stamps;
    const auto add = [&](double x, double y) {
        for (const Stamp& stamp : walk.add(pen(x, y, 1.0), {}))
            stamps.push_back(stamp);
    };
    for (double x = 10.0; x <= 40.0; x += 10.0)
        add(x, 0.0);
    add(42.0, 10.0);  // a tight turn to the right, toward the hair: it swings back
    for (double y = 20.0; y <= 120.0; y += 10.0)
        add(42.0, y);
    for (const Stamp& stamp : walk.end())
        stamps.push_back(stamp);
    int backwards = 0;
    for (std::size_t i = 1; i < stamps.size(); ++i) {
        const QPointF step = stamps[i].centre - stamps[i - 1].centre;
        for (const Stamp::HairDot& dot : *stamps[i].hairs) {
            const QPointF run(dot.dx - dot.fromX, dot.dy - dot.fromY);
            backwards += run.x() * step.x() + run.y() * step.y() < 0.0 ? 1 : 0;
            EXPECT_FLOAT_EQ(hairCoverage(stamps[i], dot.dx, dot.dy), 1.0f) << "stamp " << i;
        }
    }
    EXPECT_GT(backwards, 0) << "the turn swings the hair back";
}

// Hair Min: with Size on pressure a hair's width follows the size's pressure response, from Hair Min's share of its full width at
// no pressure to all of it at full; at Hair Min 100 %, or with Size off pressure, it keeps its full width.
TEST(Bristles, HairMinIsTheThinnestAHairGetsUnderALightTouch) {
    const auto hairRadiusAt = [](double pressure, double least, bool follows) {
        StampTip tip{20.0, 1.0, kEdgePresets - 1, 0.0};
        auto bristles = std::make_shared<Bristles>();
        bristles->hairs.push_back({1.0, 0.0});
        tip.bristles = bristles;
        tip.bristleSpread = 20.0;
        tip.hairRadius = 2.0;
        tip.hairLeast = least;
        tip.hairFollowsPressure = follows;
        tip.hairSensitivity = 100;  // a straight response
        StampWalk walk(tip);
        walk.begin(pen(0.0, 0.0, pressure), {});
        std::vector<Stamp> stamps;
        for (double x = 10.0; x <= 60.0; x += 10.0)
            for (const Stamp& stamp : walk.add(pen(x, 0.0, pressure), {}))
                stamps.push_back(stamp);
        EXPECT_FALSE(stamps.empty());
        return stamps.empty() ? 0.0f : stamps[stamps.size() / 2].hairRadius;
    };
    EXPECT_FLOAT_EQ(hairRadiusAt(1.0, 0.0, true), 2.0f) << "full width at full pressure";
    EXPECT_FLOAT_EQ(hairRadiusAt(0.5, 0.0, true), 1.0f);
    EXPECT_FLOAT_EQ(hairRadiusAt(0.5, 0.5, true), 1.5f) << "half way from Hair Min's share to the whole";
    EXPECT_FLOAT_EQ(hairRadiusAt(0.5, 1.0, true), 2.0f) << "at 100 % the hairs keep their width";
    EXPECT_FLOAT_EQ(hairRadiusAt(0.5, 0.0, false), 2.0f) << "Size off pressure: Hair Min does nothing";
}

// Direction: Auto turns the bristle map with the stroke, its right to the stroke's right; None keeps it as for a stroke going
// right, whichever way the stroke goes. A hair at the map's right, on a stroke going down the canvas.
TEST(Bristles, DirectionNoneKeepsTheMapAsForAStrokeGoingRight) {
    const auto hairAt = [](bool turns) {
        StampTip tip{20.0, 1.0, kEdgePresets - 1, 0.0};
        auto bristles = std::make_shared<Bristles>();
        bristles->hairs.push_back({1.0, 0.0});
        tip.bristles = bristles;
        tip.bristleSpread = 20.0;
        tip.hairRadius = 2.0;
        tip.hairWay = turns ? StampTip::HairWay::AlongStroke : StampTip::HairWay::Fixed;
        StampWalk walk(tip);
        walk.begin(pen(0.0, 0.0, 1.0), {});
        std::vector<Stamp> stamps;
        for (double y = 10.0; y <= 60.0; y += 10.0)
            for (const Stamp& stamp : walk.add(pen(0.0, y, 1.0), {}))
                stamps.push_back(stamp);
        EXPECT_FALSE(stamps.empty());
        const Stamp::HairDot dot = stamps.empty() ? Stamp::HairDot{} : stamps[stamps.size() / 2].hairs->front();
        return QPointF(dot.dx, dot.dy);
    };
    const QPointF autoWay = hairAt(true), none = hairAt(false);
    EXPECT_NEAR(autoWay.x(), -20.0, 1e-3) << "going down, the stroke's right is the canvas's left";
    EXPECT_NEAR(autoWay.y(), 0.0, 1e-3);
    EXPECT_NEAR(none.x(), 0.0, 1e-3);
    EXPECT_NEAR(none.y(), 20.0, 1e-3) << "as for a stroke going right: the map's right down the canvas";
}

namespace {

// A walk's stamps along `points`, its hairs at `hairs` in the map (halves of its span), spread 20 px and 2 px across, set by `adjust`;
// each sample made by `sample`.
std::vector<Stamp> hairWalk(const std::vector<Hair>& hairs, const std::vector<QPointF>& points,
                            const std::function<void(StampTip&)>& adjust, const std::function<InputSample(QPointF, std::size_t)>& sample) {
    StampTip tip{20.0, 1.0, kEdgePresets - 1, 0.0};
    auto bristles = std::make_shared<Bristles>();
    bristles->hairs = hairs;
    tip.bristles = bristles;
    tip.bristleSpread = 20.0;
    tip.hairRadius = 2.0;
    adjust(tip);
    StampWalk walk(tip);
    walk.begin(sample(points.front(), 0), {});
    std::vector<Stamp> stamps;
    for (std::size_t i = 1; i < points.size(); ++i)
        for (const Stamp& stamp : walk.add(sample(points[i], i), {}))
            stamps.push_back(stamp);
    for (const Stamp& stamp : walk.end())
        stamps.push_back(stamp);
    return stamps;
}

InputSample plainSample(QPointF at, std::size_t) {
    return pen(at.x(), at.y(), 1.0);
}

}

// Pen Direction: the map's top points the way the pen leans, whichever way the stroke goes; a pen with no lean, or a mouse, lays
// it as None. A hair at the map's right, on a stroke going right, the pen leaning down the canvas.
TEST(Bristles, PenDirectionPointsTheMapTheWayThePenLeans) {
    std::vector<QPointF> points;
    for (double x = 0.0; x <= 60.0; x += 10.0)
        points.emplace_back(x, 0.0);
    const auto hairOf = [&](double tiltY) {
        const std::vector<Stamp> stamps = hairWalk(
            {{1.0, 0.0}}, points, [](StampTip& t) { t.hairWay = StampTip::HairWay::WithPen; },
            [&](QPointF at, std::size_t) {
                InputSample s = pen(at.x(), at.y(), 1.0);
                s.tiltY = tiltY;
                return s;
            });
        EXPECT_FALSE(stamps.empty());
        const Stamp::HairDot dot = stamps[stamps.size() / 2].hairs->back();
        return QPointF(dot.dx, dot.dy);
    };
    const QPointF leaning = hairOf(30.0), upright = hairOf(0.0);
    EXPECT_NEAR(leaning.x(), -20.0, 1e-3) << "the map's top down the canvas: its right is the canvas's left";
    EXPECT_NEAR(leaning.y(), 0.0, 1e-3);
    EXPECT_NEAR(upright.x(), 0.0, 1e-3) << "no lean: as None";
    EXPECT_NEAR(upright.y(), 20.0, 1e-3);
}

// Auto on a tight turn: a hair's run from its last place is split into pieces that turn the map kHairMostTurnStep at the most,
// so the turn's hairs stay smooth; with Detect Turn a corner far sharper than the stroke has been turning starts them afresh instead,
// no run from before.
TEST(Bristles, AtACornerTheHairsSwingRoundOrWithDetectTurnStartAfresh) {
    std::vector<QPointF> points;
    for (double x = 0.0; x <= 100.0; x += 10.0)
        points.emplace_back(x, 0.0);
    for (double y = 10.0; y <= 100.0; y += 10.0)
        points.emplace_back(100.0, y);
    const auto corner = [&](bool detect) {
        const std::vector<Stamp> stamps = hairWalk({{1.0, 0.0}}, points, [&](StampTip& t) { t.hairDetectTurn = detect; }, plainSample);
        std::size_t most = 0, fresh = 0;
        for (std::size_t i = 1; i < stamps.size(); ++i) {
            most = std::max(most, stamps[i].hairs->size());
            for (const Stamp::HairDot& dot : *stamps[i].hairs)
                fresh += dot.dx == dot.fromX && dot.dy == dot.fromY ? 1 : 0;
        }
        return std::pair{most, fresh};
    };
    const auto [swingPieces, swingFresh] = corner(false);
    const auto [turnPieces, turnFresh] = corner(true);
    EXPECT_GT(swingPieces, 10u) << "a quarter turn in pieces of 5 degrees at the most";
    EXPECT_EQ(swingFresh, 0u);
    EXPECT_EQ(turnFresh, 1u) << "afresh once, at the corner";
    EXPECT_EQ(turnPieces, 1u);
}

// Detect Turn on a hairpin: the hairs end where the bend is caught, cut off, and lay nothing round it, the map not sweeping round;
// past it they start afresh. Off, they swing round it.
TEST(Bristles, OnAHairpinDetectTurnCutsTheHairsOff) {
    std::vector<QPointF> points;
    for (double x = 0.0; x <= 100.0; x += 5.0)
        points.emplace_back(x, 0.0);
    for (int a = 1; a < 12; ++a) {  // a half turn of radius 7 px, down and back
        const double angle = std::numbers::pi / 2.0 - std::numbers::pi * a / 12.0;
        points.emplace_back(100.0 + 7.0 * std::cos(angle), 7.0 - 7.0 * std::sin(angle));
    }
    for (double x = 100.0; x >= 0.0; x -= 5.0)
        points.emplace_back(x, 14.0);
    const auto bare = [&](bool detect) {
        const std::vector<Stamp> stamps = hairWalk({{1.0, 0.0}}, points, [&](StampTip& t) { t.hairDetectTurn = detect; }, plainSample);
        return std::count_if(stamps.begin(), stamps.end(), [](const Stamp& s) { return s.hairs->empty(); });
    };
    EXPECT_GT(bare(true), 0) << "nothing laid round the bend";
    EXPECT_EQ(bare(false), 0);
}

// Randomize: each hair wanders about its place, the map's middle most and its edge not at all; the wander is new each stroke,
// set by where and when it lands, so a stroke replays the same.
TEST(Bristles, RandomizeWandersTheMiddleHairsAndIsNewEachStroke) {
    std::vector<QPointF> points;
    for (double x = 0.0; x <= 400.0; x += 10.0)
        points.emplace_back(x, 0.0);
    const auto acrossOf = [&](std::uint64_t landedAt) {
        const std::vector<Stamp> stamps = hairWalk(
            {{0.0, 0.0}, {1.0, 0.0}}, points, [](StampTip& t) { t.hairWander = 1.0; },
            [&](QPointF at, std::size_t i) {
                InputSample s = pen(at.x(), at.y(), 1.0);
                s.timestampMs = landedAt + 8 * i;
                return s;
            });
        std::vector<std::pair<double, double>> across;  // the middle hair's and the edge hair's, down the canvas from the stamp
        across.reserve(stamps.size());
        for (const Stamp& stamp : stamps)
            across.emplace_back(stamp.hairs->front().dy, stamp.hairs->back().dy);
        return across;
    };
    const auto first = acrossOf(1000), again = acrossOf(1000), other = acrossOf(5000);
    double most = 0.0;
    for (const auto& [middle, edge] : first) {
        most = std::max(most, std::abs(middle));
        EXPECT_NEAR(edge, 20.0, 1e-3) << "the edge hair stays in its place";
    }
    EXPECT_GT(most, 0.1) << "the middle hair wanders";
    EXPECT_LE(most, 20.0 * 0.025 + 1e-6) << "by kHairWanderMost of half the span at the most";
    EXPECT_EQ(first, again) << "the same stroke, landed at the same time, replays the same";
    EXPECT_NE(first, other) << "painted again, it wanders anew";
}

namespace {

// A brush shape of the user's own, light where it paints, `light(x, y)` 0 to 255 over a picture `side` px a side.
void writeShape(const QString& folder, const QString& name, int side, const std::function<int(int, int)>& light) {
    QImage picture(side, side, QImage::Format_Grayscale8);
    for (int y = 0; y < side; ++y)
        for (int x = 0; x < side; ++x)
            picture.setPixel(x, y, qRgb(light(x, y), light(x, y), light(x, y)));
    ASSERT_TRUE(QDir().mkpath(folder + QStringLiteral("/forms/shapes")));
    ASSERT_TRUE(picture.save(folder + QStringLiteral("/forms/shapes/") + name + QStringLiteral(".png")));
}

// A Pencil stroke on a 320 x 240 canvas with a brush shape, its settings changed by `change`.
TiledImage shapedPencil(const QString& form, const std::function<void(BrushSettings&)>& change, const std::vector<QPointF>& points) {
    Document doc(QSize(320, 240), Qt::white);
    BrushSettings settings = defaultSettings(ToolType::Pencil);
    settings.size = 400;  // tenths of a pixel
    settings.pressureSize = false;
    settings.form = form;
    settings.shapeAngleControl = 0;
    change(settings);
    StrokeController stroke(&doc);
    InputSample first = pen(points.front().x(), points.front().y(), 1.0);
    if (points.size() == 1)
        first.kind = PointerKind::Mouse;
    EXPECT_TRUE(stroke.begin(first, Qt::black, settings));
    for (std::size_t i = 1; i < points.size(); ++i)
        stroke.add(pen(points[i].x(), points[i].y(), 1.0));
    stroke.finish();
    return doc.currentLayer()->pixels;
}

double alphaShare(const TiledImage& image, int x, int y) {
    return image.pixel(x, y)[3] / double(kChannelOne);
}

std::vector<QPointF> levelLine() {
    std::vector<QPointF> points;
    for (int x = 40; x <= 280; x += 8)
        points.emplace_back(x + 0.5, 120.5);
    return points;
}

}

// A brush shape's own settings paint: its opacity is Dens. times the tool's Density, Scaling sizes it against the tip, and its
// stamps go its Spacing of the size apart.
TEST(BrushShapes, DensityScalingAndSpacingAreItsOwn) {
    QTemporaryDir folder;
    writeShape(folder.path(), QStringLiteral("Square"), 64, [](int, int) { return 255; });
    writeShape(folder.path(), QStringLiteral("Bar"), 64, [](int x, int) { return x >= 30 && x < 34 ? 255 : 0; });
    const BrushResourceLibrary library(folder.path());
    RebuiltStroke::setResources(&library);
    const QPointF middle(160.5, 120.5);
    const TiledImage half = shapedPencil(QStringLiteral("Square"), [](BrushSettings& s) { s.shapeDensity = 50; }, {middle});
    const TiledImage quarter = shapedPencil(QStringLiteral("Square"),
                                            [](BrushSettings& s) {
                                                s.shapeDensity = 50;
                                                s.density = 50;
                                            },
                                            {middle});
    const TiledImage scaled = shapedPencil(QStringLiteral("Square"), [](BrushSettings& s) { s.shapeScaling = 150; }, {middle});
    const TiledImage bars = shapedPencil(QStringLiteral("Bar"), [](BrushSettings& s) { s.shapeSpacing = 50; }, levelLine());
    RebuiltStroke::setResources(nullptr);
    EXPECT_NEAR(alphaShare(half, 160, 120), 0.5, 2.0 / 255.0);
    EXPECT_NEAR(alphaShare(quarter, 160, 120), 0.25, 2.0 / 255.0);
    const auto across = [](const TiledImage& image) {
        int painted = 0;
        for (int x = 0; x < 320; ++x)
            painted += alphaShare(image, x, 120) > 0.5 ? 1 : 0;
        return painted;
    };
    EXPECT_NEAR(across(half), 40, 1);
    EXPECT_NEAR(across(scaled), 60, 1);
    // A bar every 20 px, half the 40 px size: the row between them holds runs of paint as many as the stamps.
    int runs = 0;
    for (int x = 80; x < 240; ++x)
        runs += alphaShare(bars, x, 120) > 0.5 && alphaShare(bars, x - 1, 120) <= 0.5 ? 1 : 0;
    EXPECT_EQ(runs, 8);
}

// A stamp much smaller than its picture reads the halved copies: a picture of single-pixel checks, 256 px, on a 40 px click is
// an even grey, where reading the picture itself picks checks and leaves a pattern. HQ Down Scaling averages it too.
TEST(BrushShapes, ASmallStampReadsTheHalvedCopies) {
    QTemporaryDir folder;
    writeShape(folder.path(), QStringLiteral("Checks"), 256, [](int x, int y) { return (x + y) % 2 == 0 ? 255 : 0; });
    const BrushResourceLibrary library(folder.path());
    RebuiltStroke::setResources(&library);
    const QPointF middle(160.5, 120.5);
    const TiledImage blended = shapedPencil(QStringLiteral("Checks"), [](BrushSettings&) {}, {middle});
    const TiledImage fine = shapedPencil(QStringLiteral("Checks"), [](BrushSettings& s) { s.shapeHighQuality = true; }, {middle});
    RebuiltStroke::setResources(nullptr);
    for (const TiledImage* image : {&blended, &fine})
        for (int y = 110; y <= 130; ++y)
            for (int x = 150; x <= 170; ++x)
                EXPECT_NEAR(alphaShare(*image, x, y), 0.5, 3.0 / 255.0) << x << ", " << y;
}

// The jitters are random but the same on every replay of a stroke: each stamp turned, sized and moved at random.
TEST(BrushShapes, ItsJittersReplayTheSame) {
    QTemporaryDir folder;
    writeShape(folder.path(), QStringLiteral("Bar"), 64, [](int x, int) { return x >= 30 && x < 34 ? 255 : 0; });
    const BrushResourceLibrary library(folder.path());
    RebuiltStroke::setResources(&library);
    const auto jittered = [](BrushSettings& s) {
        s.shapeAngleJitter = 100;
        s.shapeSizeJitter = 25;
        s.shapeScattering = 15;
    };
    const TiledImage first = shapedPencil(QStringLiteral("Bar"), jittered, levelLine());
    const TiledImage again = shapedPencil(QStringLiteral("Bar"), jittered, levelLine());
    const TiledImage plain = shapedPencil(QStringLiteral("Bar"), [](BrushSettings&) {}, levelLine());
    RebuiltStroke::setResources(nullptr);
    int same = 0, moved = 0;
    for (int y = 60; y < 180; ++y)
        for (int x = 0; x < 320; ++x) {
            same += first.pixel(x, y) == again.pixel(x, y) ? 1 : 0;
            moved += first.pixel(x, y) != plain.pixel(x, y) ? 1 : 0;
        }
    EXPECT_EQ(same, 120 * 320);
    EXPECT_GT(moved, 1000);
}

// A hair is a run from where it was at the stamp before to where it is, so it stays a line however far apart the stamps go.
TEST(Bristles, AHairRunsOnFromItsLastPlace) {
    Stamp stamp;
    stamp.hairRadius = 2.0f;
    auto dots = std::make_shared<std::vector<Stamp::HairDot>>();
    dots->push_back({0.0f, 0.0f, -10.0f, 0.0f});
    stamp.hairs = dots;
    EXPECT_FLOAT_EQ(hairCoverage(stamp, -5.0f, 0.0f), 1.0f) << "half way along its run";
    EXPECT_FLOAT_EQ(hairCoverage(stamp, -5.0f, 3.0f), 0.0f) << "past its edge";
    EXPECT_FLOAT_EQ(hairCoverage(stamp, -5.0f, 2.0f), 0.5f) << "on its edge, half";
}
