#include "core/document/Document.h"
#include "core/paint/StrokeController.h"
#include "core/selection/SelectionOps.h"

#include <QDir>
#include <QPainter>

#include <gtest/gtest.h>

#include <array>
#include <cstdlib>
#include <memory>

using namespace doodlebold;

namespace {

const QSize kCanvas(160, 120);
const QColor kRed(255, 0, 0), kGreen(0, 255, 0), kBlue(0, 0, 255);

constexpr QPoint kRedSpot(20, 20);

TiledImage twoBlocks() {
    TiledImage img(kCanvas);
    img.fill(QRect(10, 10, 26, 26), kRed);
    img.fill(QRect(10, 40, 26, 26), kGreen);
    return img;
}

InputSample at(double x, double y) {
    InputSample s;
    s.pos = QPointF(x, y);
    return s;
}

BrushSettings stamp(bool aligned = true, bool allLayers = false) {
    BrushSettings s = defaultSettings(ToolType::Clone);
    s.size = 300;
    s.cloneAligned = aligned;
    s.cloneAllLayers = allLayers;
    return s;
}

EditResult<> clone(StrokeController& sc, const BrushSettings& settings, double x, double y) {
    const auto started = sc.begin(at(x, y), QColor(10, 20, 30), settings);
    if (!started)
        return started;
    for (int i = 1; i <= 6; ++i)
        sc.add(at(x + i, y));
    for (int i = 5; i >= 0; --i)
        sc.add(at(x + i, y));
    sc.finish();
    return {};
}

std::array<int, 4> px(const Document& doc, int x, int y) {
    const auto p = doc.currentLayer()->pixels.pixel(x, y);
    return {p[0], p[1], p[2], p[3]};
}

std::array<int, 4> opaque(const QColor& c) {
    return {fromUnorm8(c.red()), fromUnorm8(c.green()), fromUnorm8(c.blue()), kChannelOne};
}

constexpr int kTolerance = kChannelOne / 16;
::testing::AssertionResult isColour(const std::array<int, 4>& p, const std::array<int, 4>& expected) {
    for (std::size_t i = 0; i < 4; ++i)
        if (std::abs(p[i] - expected[i]) > kTolerance)
            return ::testing::AssertionFailure() << "channel " << i << ": " << p[i] << " vs " << expected[i];
    return ::testing::AssertionSuccess();
}

std::unique_ptr<Document> blocksDocument() {
    auto doc = std::make_unique<Document>(kCanvas, Qt::white);
    EXPECT_TRUE(doc->addImageLayer(twoBlocks(), QStringLiteral("blocks")));
    return doc;
}

}

TEST(CloneStampState, NoSourceNoOffset) {
    CloneStamp state;
    EXPECT_FALSE(state.hasSource());
    EXPECT_FALSE(state.beginStroke(QPointF(50, 50), true));
    EXPECT_FALSE(state.crosshair(QPointF(50, 50), true, false));
}

TEST(CloneStampState, TheFirstStrokeFixesTheOffsetAndAlignedKeepsIt) {
    CloneStamp state;
    state.setSource(QPointF(20.7, 20.2));
    EXPECT_EQ(state.source(), QPoint(20, 20));
    EXPECT_FALSE(state.offset());
    EXPECT_EQ(state.beginStroke(QPointF(100.4, 60.9), true), QPoint(80, 40));
    EXPECT_EQ(state.beginStroke(QPointF(130, 10), true), QPoint(80, 40));
    EXPECT_EQ(state.beginStroke(QPointF(130, 10), false), QPoint(110, -10));
    EXPECT_EQ(state.beginStroke(QPointF(100, 60), false), QPoint(80, 40));
}

TEST(CloneStampState, ANewSourceForgetsTheOffset) {
    CloneStamp state;
    state.setSource(QPointF(20, 20));
    ASSERT_TRUE(state.beginStroke(QPointF(100, 60), true));
    state.setSource(QPointF(30, 30));
    EXPECT_FALSE(state.offset());
    EXPECT_EQ(state.beginStroke(QPointF(100, 60), true), QPoint(70, 30));
    state.reset();
    EXPECT_FALSE(state.hasSource());
}

TEST(CloneStampState, TheCrosshairRestsAtTheSourceAndFollowsTheOffsetWhenAlignedOrPainting) {
    CloneStamp state;
    state.setSource(QPointF(20.7, 20.2));
    const QPointF pointer(90.5, 70.25);
    EXPECT_EQ(state.crosshair(pointer, true, false), QPointF(20.5, 20.5));
    ASSERT_TRUE(state.beginStroke(QPointF(100, 60), true));
    EXPECT_EQ(state.crosshair(pointer, true, false), QPointF(10.5, 30.25));
    EXPECT_EQ(state.crosshair(pointer, false, false), QPointF(20.5, 20.5));
    EXPECT_EQ(state.crosshair(pointer, false, true), QPointF(10.5, 30.25));
}

TEST(CloneStamp, OneStrokeIsOneUndoStep) {
    auto doc = blocksDocument();
    ASSERT_TRUE(doc->addLayer());
    const int steps = doc->undoStack()->count();
    StrokeController sc(doc.get());
    sc.cloneStamp().setSource(QPointF(kRedSpot));
    ASSERT_TRUE(clone(sc, stamp(true, true), 100, 60));
    EXPECT_EQ(doc->undoStack()->count(), steps + 1);
    ASSERT_NE(px(*doc, 100, 60)[3], 0);
    ASSERT_TRUE(doc->undo());
    EXPECT_EQ(px(*doc, 100, 60)[3], 0);
    ASSERT_TRUE(doc->redo());
    EXPECT_NE(px(*doc, 100, 60)[3], 0);
}

TEST(CloneStamp, WithoutASourceTheStrokeIsRefused) {
    auto doc = blocksDocument();
    StrokeController sc(doc.get());
    const auto started = sc.begin(at(100, 60), kRed, stamp());
    ASSERT_FALSE(started);
    EXPECT_EQ(started.error(), EditError::NoCloneSource);
    EXPECT_FALSE(sc.isActive());
    EXPECT_EQ(describe(EditError::NoCloneSource), QStringLiteral("Alt+click to set the clone source."));
}

TEST(CloneStamp, ItDoesNotPaintLayerMasks) {
    auto doc = blocksDocument();
    ASSERT_TRUE(doc->addLayerMask(doc->currentLayerId()));
    doc->setMaskTarget(true);
    StrokeController sc(doc.get());
    sc.cloneStamp().setSource(QPointF(kRedSpot));
    const auto started = sc.begin(at(100, 60), kRed, stamp());
    ASSERT_FALSE(started);
    EXPECT_EQ(started.error(), EditError::CloneOnMask);
}

TEST(CloneStamp, LockedAndHiddenLayersAreRefusedLikeForEveryTool) {
    auto doc = blocksDocument();
    LayerProps props = doc->currentLayer()->props;
    props.locked = true;
    ASSERT_TRUE(doc->setLayerProps(doc->currentLayerId(), props));
    StrokeController sc(doc.get());
    sc.cloneStamp().setSource(QPointF(kRedSpot));
    EXPECT_EQ(sc.begin(at(100, 60), kRed, stamp()).error(), EditError::LayerLocked);
}

// Feedback: "Each pixel painted copies the pixel at that offset".
TEST(CloneStamp, EachPixelCopiesThePixelAtTheOffset) {
    auto doc = blocksDocument();
    StrokeController sc(doc.get());
    sc.cloneStamp().setSource(QPointF(kRedSpot));
    ASSERT_TRUE(clone(sc, stamp(), 100, 60));
    EXPECT_TRUE(isColour(px(*doc, 100, 60), opaque(kRed)));
    EXPECT_TRUE(isColour(px(*doc, 104, 62), opaque(kRed))) << "copies (24, 22), in the red block";
    EXPECT_EQ(px(*doc, 118, 60)[3], 0) << "copies (38, 20), just past the red block's right edge";
    EXPECT_EQ(px(*doc, 140, 60)[3], 0) << "outside the tip nothing changes";
}

// Aligned keeps the offset the first stroke fixed; off, each stroke copies from the source point again.
TEST(CloneStamp, AlignedKeepsTheOffsetAndOffStartsFromTheSourceAgain) {
    for (const bool aligned : {true, false}) {
        auto doc = blocksDocument();
        StrokeController sc(doc.get());
        sc.cloneStamp().setSource(QPointF(kRedSpot));
        ASSERT_TRUE(clone(sc, stamp(aligned), 100, 30));
        ASSERT_TRUE(clone(sc, stamp(aligned), 100, 60));
        EXPECT_TRUE(isColour(px(*doc, 100, 60), opaque(aligned ? kGreen : kRed))) << aligned;
    }
}

// Sample: the current layer, or every visible layer flattened over transparency.
TEST(CloneStamp, AllLayersCopiesTheVisiblePicture) {
    for (const bool allLayers : {false, true}) {
        auto doc = blocksDocument();
        ASSERT_TRUE(doc->addLayer());
        StrokeController sc(doc.get());
        sc.cloneStamp().setSource(QPointF(kRedSpot));
        ASSERT_TRUE(clone(sc, stamp(true, allLayers), 100, 60));
        if (allLayers)
            EXPECT_TRUE(isColour(px(*doc, 100, 60), opaque(kRed)));
        else
            EXPECT_EQ(px(*doc, 100, 60)[3], 0) << "the new layer is empty where the source is";
    }
}

// The source is the picture as the stroke started: what the stroke has just laid isn't copied again.
TEST(CloneStamp, TheSourceIsThePictureAsTheStrokeStarted) {
    auto doc = blocksDocument();
    StrokeController sc(doc.get());
    sc.cloneStamp().setSource(QPointF(12, 22));
    ASSERT_TRUE(sc.begin(at(20, 22), kBlue, stamp()));
    for (double x = 21; x <= 70; x += 1)
        sc.add(at(x, 22));
    sc.finish();
    EXPECT_TRUE(isColour(px(*doc, 40, 22), opaque(kRed))) << "copied from the red block, 8 px back";
    EXPECT_EQ(px(*doc, 60, 22)[3], 0) << "8 px back was empty when the stroke began, though the stroke has painted it since";
}

// Preserve Opacity: it only recolours what's there, as the Pencil does.
TEST(CloneStamp, PreserveOpacityKeepsWhatIsThere) {
    auto doc = blocksDocument();
    LayerProps props = doc->currentLayer()->props;
    props.preserveOpacity = true;
    ASSERT_TRUE(doc->setLayerProps(doc->currentLayerId(), props));
    StrokeController sc(doc.get());
    sc.cloneStamp().setSource(QPointF(kRedSpot));
    ASSERT_TRUE(clone(sc, stamp(), 20, 50));
    EXPECT_TRUE(isColour(px(*doc, 20, 50), opaque(kRed))) << "the green block takes the red";
    ASSERT_TRUE(clone(sc, stamp(), 100, 60));
    EXPECT_EQ(px(*doc, 100, 60)[3], 0) << "an empty pixel stays empty";
}

// The tip is the Pencil's: copying from solid red lays exactly what the Pencil lays in red, size and density pressure, Shape and
// Sharpness included, and a selection limits both alike.
TEST(CloneStamp, ItsTipIsThePencils) {
    const auto paintWith = [](ToolType tool) {
        auto doc = std::make_unique<Document>(kCanvas, Qt::white);
        TiledImage half(kCanvas);
        half.fill(QRect(0, 0, 70, 120), kRed);
        EXPECT_TRUE(doc->addImageLayer(half, QStringLiteral("half")));
        EXPECT_TRUE(doc->setSelection(selection::marquee(kCanvas, QPoint(0, 0), QPoint(112, 120))));
        BrushSettings settings = defaultSettings(ToolType::Pencil);
        settings.tool = tool;
        settings.size = 200;
        settings.edge = 1;
        settings.sharpness = 30;
        settings.pressureDensity = true;
        StrokeController sc(doc.get());
        sc.cloneStamp().setSource(QPointF(20, 60));
        InputSample first = at(100, 60);
        first.pressure = 0.3;
        first.kind = PointerKind::Pen;
        EXPECT_TRUE(sc.begin(first, kRed, settings));
        for (int i = 1; i <= 20; ++i) {
            InputSample s = at(100 + i * 0.75, 60 + i * 0.5);
            s.pressure = 0.3 + i * 0.03;
            s.kind = PointerKind::Pen;
            sc.add(s);
        }
        sc.finish();
        return doc->currentLayer()->pixels;
    };
    const TiledImage cloned = paintWith(ToolType::Clone);
    const TiledImage pencil = paintWith(ToolType::Pencil);
    // The same tip; the Pencil lays each pen sample straight onto the layer in whole numbers and the Clone Stamp, laying another
    // picture, the stroke at once, so they meet within 4 of 65535, under a fiftieth of an 8-bit level.
    for (int y = 30; y < 100; ++y)
        for (int x = 80; x < 140; ++x)
            for (int c = 0; c < 4; ++c)
                ASSERT_NEAR(cloned.pixel(x, y)[static_cast<std::size_t>(c)], pencil.pixel(x, y)[static_cast<std::size_t>(c)], 4)
                    << x << ", " << y;
    EXPECT_EQ(cloned.pixel(118, 66)[3], 0) << "outside the selection";
}
