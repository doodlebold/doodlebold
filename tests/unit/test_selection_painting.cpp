#include "core/document/Document.h"
#include "core/paint/StrokeController.h"

#include <gtest/gtest.h>

using namespace doodlebold;

namespace {

constexpr int kOne = SelectionMask::kOne;
const QSize kCanvas(160, 120);

// A mouse, so these strokes are at full pressure: the Brush starts with Blur Pressure, below which it only moves paint.
InputSample at(double x, double y) {
    InputSample s;
    s.pos = QPointF(x, y);
    s.kind = PointerKind::Mouse;
    return s;
}

SelectionMask rect(const QRect& r) {
    return selection::rectangle(kCanvas, QRectF(r), false);
}

SelectionMask soft(const QRect& r, int value) {
    SelectionMask m(kCanvas);
    std::vector<std::uint16_t> row(static_cast<std::size_t>(r.width()), static_cast<std::uint16_t>(value));
    for (int y = r.top(); y <= r.bottom(); ++y)
        m.writeRow(y, r.left(), r.width(), row.data());
    m.squeeze();
    return m;
}

void stroke(Document& doc, ToolType tool, double x0, double x1, double y = 60, int size = 24) {
    StrokeController sc(&doc);
    BrushSettings st = defaultSettings(tool);
    st.size = size;
    ASSERT_TRUE(sc.begin(at(x0, y), QColor(200, 30, 30), st));
    sc.add(at((x0 + x1) / 2, y));
    sc.add(at(x1, y));
    sc.finish();
}

int alphaAt(const Document& doc, int x, int y) {
    return doc.currentLayer()->pixels.pixel(x, y)[3];
}

class SelectionPainting : public ::testing::TestWithParam<ToolType> {};

}

TEST_P(SelectionPainting, ChangesOnlySelectedPixels) {
    Document doc(kCanvas, Qt::white);
    ASSERT_TRUE(doc.setSelection(rect(QRect(50, 0, 60, 120))));
    stroke(doc, GetParam(), 10, 150);
    int inside = 0;
    for (int y = 0; y < kCanvas.height(); ++y)
        for (int x = 0; x < kCanvas.width(); ++x) {
            const int a = alphaAt(doc, x, y);
            if (x >= 50 && x < 110) {
                inside += a > 0;
            } else {
                ASSERT_EQ(a, 0) << x << "," << y;
            }
        }
    EXPECT_GT(inside, 100);
}

TEST_P(SelectionPainting, ASelectionAroundTheWholeStrokeChangesNothing) {
    Document reference(kCanvas, Qt::white);
    stroke(reference, GetParam(), 70, 90);
    Document doc(kCanvas, Qt::white);
    ASSERT_TRUE(doc.setSelection(rect(QRect(40, 0, 80, 120))));
    stroke(doc, GetParam(), 70, 90);
    ASSERT_FALSE(doc.currentLayer()->pixels.isEmpty());
    for (int y = 0; y < kCanvas.height(); ++y)
        for (int x = 0; x < kCanvas.width(); ++x)
            ASSERT_EQ(doc.currentLayer()->pixels.pixel(x, y), reference.currentLayer()->pixels.pixel(x, y)) << x << "," << y;
}

TEST_P(SelectionPainting, UndoRemovesTheClippedStroke) {
    Document doc(kCanvas, Qt::white);
    ASSERT_TRUE(doc.setSelection(rect(QRect(50, 0, 60, 120))));
    stroke(doc, GetParam(), 10, 150);
    ASSERT_FALSE(doc.currentLayer()->pixels.isEmpty());
    ASSERT_TRUE(doc.undo());
    EXPECT_TRUE(doc.currentLayer()->pixels.isEmpty());
    ASSERT_TRUE(doc.redo());
    EXPECT_GT(alphaAt(doc, 80, 60), 0);
    EXPECT_EQ(alphaAt(doc, 20, 60), 0);
}

TEST_P(SelectionPainting, AStrokeFullyOutsideTheSelectionDoesNothing) {
    Document doc(kCanvas, Qt::white);
    ASSERT_TRUE(doc.setSelection(rect(QRect(120, 0, 30, 30))));
    const int steps = doc.undoStack()->count();
    stroke(doc, GetParam(), 10, 100);
    EXPECT_TRUE(doc.currentLayer()->pixels.isEmpty());
    EXPECT_EQ(doc.undoStack()->count(), steps);
}

INSTANTIATE_TEST_SUITE_P(PaintingTools, SelectionPainting,
                         ::testing::Values(ToolType::Pencil, ToolType::Brush, ToolType::WaterColor, ToolType::Marker),
                         [](const ::testing::TestParamInfo<ToolType>& i) { return toolTypeKey(i.param).toStdString(); });

TEST(SelectionPaintingSoft, PartialCoveragePaintsALighterStroke) {
    Document reference(kCanvas, Qt::white);
    stroke(reference, ToolType::Pencil, 10, 150);
    Document doc(kCanvas, Qt::white);
    ASSERT_TRUE(doc.setSelection(soft(QRect(0, 0, 160, 120), kOne / 2)));
    stroke(doc, ToolType::Pencil, 10, 150);
    const int full = alphaAt(reference, 80, 60);
    ASSERT_GT(full, 0);
    EXPECT_GT(alphaAt(doc, 80, 60), full / 4);
    EXPECT_LT(alphaAt(doc, 80, 60), full);
    const auto p = doc.currentLayer()->pixels.pixel(80, 60);
    const auto q = reference.currentLayer()->pixels.pixel(80, 60);
    EXPECT_NEAR(double(p[0]) / p[3], double(q[0]) / q[3], 0.01);
}

TEST(SelectionPaintingSoft, StrokeOnSoftEdgeFadesAcrossIt) {
    Document reference(kCanvas, Qt::white);
    stroke(reference, ToolType::Pencil, 0, 159, 60, 12);
    Document doc(kCanvas, Qt::white);
    SelectionMask m(kCanvas);
    for (int y = 0; y < kCanvas.height(); ++y) {
        std::vector<std::uint16_t> row(static_cast<std::size_t>(kCanvas.width()));
        for (int x = 0; x < kCanvas.width(); ++x)
            row[static_cast<std::size_t>(x)] = static_cast<std::uint16_t>(std::clamp((x - 40) * kOne / 80, 0, kOne));
        m.writeRow(y, 0, kCanvas.width(), row.data());
    }
    m.squeeze();
    ASSERT_TRUE(doc.setSelection(std::move(m)));
    stroke(doc, ToolType::Pencil, 0, 159, 60, 12);
    EXPECT_EQ(alphaAt(doc, 20, 60), 0);
    EXPECT_LT(alphaAt(doc, 60, 60), alphaAt(doc, 90, 60));
    EXPECT_LT(alphaAt(doc, 90, 60), alphaAt(doc, 130, 60));
    EXPECT_EQ(alphaAt(doc, 130, 60), alphaAt(reference, 130, 60));
}

TEST(SelectionPaintingSoft, EraserErasesOnlyInsideTheSelection) {
    Document doc(kCanvas, Qt::white);
    ASSERT_TRUE(doc.fillLayer(doc.currentLayerId(), Qt::blue));
    ASSERT_TRUE(doc.setSelection(rect(QRect(50, 0, 60, 120))));
    stroke(doc, ToolType::Eraser, 10, 150);
    EXPECT_LT(alphaAt(doc, 80, 60), kOne);
    EXPECT_EQ(alphaAt(doc, 20, 60), kOne);
    EXPECT_EQ(alphaAt(doc, 130, 60), kOne);
    EXPECT_EQ(alphaAt(doc, 80, 10), kOne);
}

TEST(SelectionPaintingSoft, PreserveOpacityStillKeepsAlphaInsideTheSelection) {
    Document doc(kCanvas, Qt::white);
    ASSERT_TRUE(doc.fillLayer(doc.currentLayerId(), QColor(0, 0, 255, 128)));
    LayerProps p = doc.currentLayer()->props;
    p.preserveOpacity = true;
    ASSERT_TRUE(doc.setLayerProps(doc.currentLayerId(), p));
    ASSERT_TRUE(doc.setSelection(rect(QRect(50, 0, 60, 120))));
    const int alpha = alphaAt(doc, 80, 60);
    stroke(doc, ToolType::Pencil, 10, 150);
    EXPECT_EQ(alphaAt(doc, 80, 60), alpha);
    EXPECT_GT(doc.currentLayer()->pixels.pixel(80, 60)[0], doc.currentLayer()->pixels.pixel(20, 60)[0]);
}

namespace {
void paintEdge(Document& doc) {
    TiledImage* px = doc.paintTarget(doc.currentLayerId()).value();
    px->fill(QRect(0, 0, 80, 120), Qt::black);
    px->fill(QRect(80, 0, 80, 120), Qt::white);
}
}

namespace {

void expectToolStaysInBand(ToolType tool, double x0, double x1, int size) {
    Document doc(kCanvas, Qt::white);
    paintEdge(doc);
    const TiledImage before = doc.currentLayer()->pixels;
    ASSERT_TRUE(doc.setSelection(rect(QRect(78, 0, 4, 120))));
    stroke(doc, tool, x0, x1, 60, size);
    bool changedInside = false;
    for (int y = 0; y < 120; ++y)
        for (int x = 0; x < 160; ++x) {
            const bool differs = doc.currentLayer()->pixels.pixel(x, y) != before.pixel(x, y);
            if (x < 78 || x >= 82) {
                ASSERT_FALSE(differs) << x << "," << y;
            }
            changedInside = changedInside || differs;
        }
    EXPECT_TRUE(changedInside);

    Document free(kCanvas, Qt::white);
    paintEdge(free);
    stroke(free, tool, x0, x1, 60, size);
    bool changedOutside = false;
    for (int y = 0; y < 120; ++y)
        for (int x = 0; x < 160; ++x)
            if (x < 78 || x >= 82)
                changedOutside = changedOutside || free.currentLayer()->pixels.pixel(x, y) != before.pixel(x, y);
    EXPECT_TRUE(changedOutside);
}

}

TEST(SelectionPaintingBlurSmudge, BlurOnlyChangesSelectedPixels) {
    expectToolStaysInBand(ToolType::Blur, 40, 120, 40);
}

TEST(SelectionPaintingBlurSmudge, SmudgeOnlyChangesSelectedPixels) {
    expectToolStaysInBand(ToolType::Smudge, 40, 120, 30);
}

TEST(SelectionPaintingBlurSmudge, BlurStillSeesPixelsOutsideTheSelection) {
    Document doc(kCanvas, Qt::white);
    paintEdge(doc);
    ASSERT_TRUE(doc.setSelection(rect(QRect(0, 0, 80, 120))));
    stroke(doc, ToolType::Blur, 40, 120, 60, 40);
    EXPECT_GT(doc.currentLayer()->pixels.pixel(78, 60)[0], 0);
}

TEST(SelectionFillClear, FillColoursOnlyTheSelectedPart) {
    Document doc(kCanvas, Qt::white);
    ASSERT_TRUE(doc.setSelection(rect(QRect(10, 20, 50, 40))));
    ASSERT_TRUE(doc.fillLayer(doc.currentLayerId(), QColor(0, 255, 0)));
    const TiledImage& px = doc.currentLayer()->pixels;
    EXPECT_EQ(px.pixel(10, 20), TiledImage::premultiplied(QColor(0, 255, 0)));
    EXPECT_EQ(px.pixel(59, 59), TiledImage::premultiplied(QColor(0, 255, 0)));
    EXPECT_EQ(px.pixel(9, 20)[3], 0);
    EXPECT_EQ(px.pixel(60, 30)[3], 0);
    EXPECT_EQ(px.pixel(30, 60)[3], 0);
    ASSERT_TRUE(doc.undo());
    EXPECT_TRUE(doc.currentLayer()->pixels.isEmpty());
}

TEST(SelectionFillClear, FillByCoverageBlendsTowardsTheColour) {
    Document doc(kCanvas, Qt::white);
    ASSERT_TRUE(doc.fillLayer(doc.currentLayerId(), QColor(255, 0, 0)));
    ASSERT_TRUE(doc.setSelection(soft(QRect(0, 0, 80, 120), kOne / 4)));
    ASSERT_TRUE(doc.fillLayer(doc.currentLayerId(), QColor(0, 0, 255)));
    const auto p = doc.currentLayer()->pixels.pixel(10, 10);
    EXPECT_EQ(p[3], kOne);
    EXPECT_NEAR(p[0], kOne * 3 / 4, 1);
    EXPECT_NEAR(p[2], kOne / 4, 1);
    const auto q = doc.currentLayer()->pixels.pixel(120, 10);
    EXPECT_EQ(q[0], kOne);
    EXPECT_EQ(q[2], 0);
}

TEST(SelectionFillClear, FillRespectsPreserveOpacity) {
    Document doc(kCanvas, Qt::white);
    ASSERT_TRUE(doc.setSelection(rect(QRect(0, 0, 50, 50))));
    ASSERT_TRUE(doc.fillLayer(doc.currentLayerId(), QColor(255, 0, 0)));
    LayerProps p = doc.currentLayer()->props;
    p.preserveOpacity = true;
    ASSERT_TRUE(doc.setLayerProps(doc.currentLayerId(), p));
    ASSERT_TRUE(doc.setSelection(rect(QRect(25, 0, 50, 50))));
    ASSERT_TRUE(doc.fillLayer(doc.currentLayerId(), QColor(0, 0, 255)));
    const TiledImage& px = doc.currentLayer()->pixels;
    EXPECT_EQ(px.pixel(10, 10), TiledImage::premultiplied(QColor(255, 0, 0)));
    EXPECT_EQ(px.pixel(30, 10), TiledImage::premultiplied(QColor(0, 0, 255)));
    EXPECT_EQ(px.pixel(60, 10)[3], 0);
}

TEST(SelectionFillClear, ClearErasesOnlyTheSelectedPart) {
    Document doc(kCanvas, Qt::white);
    ASSERT_TRUE(doc.fillLayer(doc.currentLayerId(), QColor(0, 255, 0)));
    ASSERT_TRUE(doc.setSelection(rect(QRect(70, 30, 50, 40))));
    ASSERT_TRUE(doc.clearLayer(doc.currentLayerId()));
    const TiledImage& px = doc.currentLayer()->pixels;
    EXPECT_EQ(px.pixel(70, 30)[3], 0);
    EXPECT_EQ(px.pixel(119, 69)[3], 0);
    EXPECT_EQ(px.pixel(69, 30)[3], kOne);
    EXPECT_EQ(px.pixel(120, 30)[3], kOne);
    EXPECT_EQ(px.pixel(80, 70)[3], kOne);
    ASSERT_TRUE(doc.undo());
    EXPECT_EQ(doc.currentLayer()->pixels.pixel(80, 40)[3], kOne);
}

TEST(SelectionFillClear, ClearByCoverageFades) {
    Document doc(kCanvas, Qt::white);
    ASSERT_TRUE(doc.fillLayer(doc.currentLayerId(), QColor(0, 255, 0)));
    ASSERT_TRUE(doc.setSelection(soft(QRect(0, 0, 160, 120), kOne / 2)));
    ASSERT_TRUE(doc.clearLayer(doc.currentLayerId()));
    const auto p = doc.currentLayer()->pixels.pixel(10, 10);
    EXPECT_NEAR(p[3], kOne / 2, 1);
    EXPECT_NEAR(p[1], kOne / 2, 1);
}

TEST(SelectionFillClear, ClearOutsideLayerContentChangesNothing) {
    Document doc(kCanvas, Qt::white);
    ASSERT_TRUE(doc.setSelection(rect(QRect(0, 0, 50, 50))));
    ASSERT_TRUE(doc.fillLayer(doc.currentLayerId(), QColor(0, 255, 0)));
    ASSERT_TRUE(doc.setSelection(rect(QRect(100, 80, 20, 20))));
    EXPECT_EQ(doc.clearLayer(doc.currentLayerId()).error(), EditError::NoChange);
}

TEST(SelectionFillClear, WholeCanvasSelectionMatchesNoSelection) {
    Document a(kCanvas, Qt::white), b(kCanvas, Qt::white);
    ASSERT_TRUE(a.fillLayer(a.currentLayerId(), QColor(10, 200, 90)));
    ASSERT_TRUE(b.selectAll());
    ASSERT_TRUE(b.fillLayer(b.currentLayerId(), QColor(10, 200, 90)));
    EXPECT_EQ(a.projection(), b.projection());
    ASSERT_TRUE(b.clearLayer(b.currentLayerId()));
    EXPECT_TRUE(b.currentLayer()->pixels.isEmpty());
}

#include "core/paint/SelectionStrokeController.h"

namespace {

void selectionStroke(Document& doc, SelectionBrush brush, double x0, double x1, double y = 60, int size = 200) {
    SelectionStrokeController sc(&doc);
    BrushSettings st = defaultSettings(ToolType::Pencil);
    st.size = size;
    ASSERT_TRUE(sc.begin(at(x0, y), brush, st));
    for (double x = x0 + 4; x < x1; x += 4)
        sc.add(at(x, y));
    sc.add(at(x1, y));
    sc.finish();
}

}

TEST(SelectionPen, PaintsASelectionWithFullDensity) {
    Document doc(kCanvas, Qt::white);
    selectionStroke(doc, SelectionBrush::Pen, 30, 120);
    ASSERT_TRUE(doc.selection().has_value());
    EXPECT_GT(doc.selection()->at(75, 60), kOne * 99 / 100);
    EXPECT_EQ(doc.selection()->at(75, 20), 0);
    EXPECT_TRUE(doc.currentLayer()->pixels.isEmpty());
    EXPECT_EQ(doc.undoStack()->undoText(), QStringLiteral("Selection Pen"));
    ASSERT_TRUE(doc.undo());
    EXPECT_FALSE(doc.selection().has_value());
}

TEST(SelectionPen, StrokeHasTheCalibratedWidth) {
    Document doc(QSize(160, 120), Qt::white);
    SelectionStrokeController sc(&doc);
    BrushSettings st = defaultSettings(ToolType::Pencil);
    st.size = 300;
    st.edge = 2;
    st.sharpness = 100;  // the Pencil's old setting, which the width was calibrated at
    ASSERT_TRUE(sc.begin(at(30, 60), SelectionBrush::Pen, st));
    for (double x = 34; x <= 130; x += 2)
        sc.add(at(x, 60));
    sc.finish();
    int wide = 0, soft = 0;
    for (int y = 0; y < 120; ++y) {
        const int v = doc.selection()->at(80, y);
        wide += v >= kOne / 2;
        soft += v > kOne / 100 && v < kOne * 99 / 100;
    }
    EXPECT_NEAR(wide, 29, 1);
    EXPECT_LE(soft, 10);
}

// Options' pressure range reaches the selection pens as it does painting strokes: with "Full pressure at" 50 %, a pen at half
// pressure draws the full size.
TEST(SelectionPen, FollowsThePressureRange) {
    const auto widthAt = [](PressureRange range) {
        Document doc(QSize(160, 120), Qt::white);
        SelectionStrokeController sc(&doc);
        sc.setPressureRange(range);
        BrushSettings st = defaultSettings(ToolType::SelectionPen);
        st.size = 300;
        st.pressureSize = true;
        InputSample pen = at(30, 60);
        pen.kind = PointerKind::Pen;
        pen.pressure = 0.5;
        EXPECT_TRUE(sc.begin(pen, SelectionBrush::Pen, st));
        for (double x = 34; x <= 130; x += 2) {
            pen.pos.setX(x);
            sc.add(pen);
        }
        sc.finish();
        int wide = 0;
        for (int y = 0; y < 120; ++y)
            wide += doc.selection()->at(80, y) >= kOne / 2;
        return wide;
    };
    EXPECT_NEAR(widthAt({0.0, 0.5}), 30, 1) << "half pressure is full";
    EXPECT_NEAR(widthAt({0.0, 1.0}), 15, 1) << "half pressure is half";
}

TEST(SelectionPen, ExtendsAnExistingSelection) {
    Document doc(kCanvas, Qt::white);
    ASSERT_TRUE(doc.setSelection(rect(QRect(0, 0, 40, 40))));
    selectionStroke(doc, SelectionBrush::Pen, 30, 120);
    EXPECT_EQ(doc.selection()->at(10, 10), kOne);
    EXPECT_GT(doc.selection()->at(75, 60), kOne * 99 / 100);
}

TEST(SelectionEraser, RemovesSelectionAndDeselectsWhenNothingIsLeft) {
    Document doc(kCanvas, Qt::white);
    ASSERT_TRUE(doc.setSelection(rect(QRect(0, 40, 160, 40))));
    selectionStroke(doc, SelectionBrush::Eraser, 30, 120, 60, 200);
    ASSERT_TRUE(doc.selection().has_value());
    EXPECT_LT(doc.selection()->at(75, 60), kOne / 100);
    EXPECT_EQ(doc.selection()->at(75, 45), kOne);
    EXPECT_EQ(doc.selection()->at(10, 60), kOne);
    EXPECT_EQ(doc.undoStack()->undoText(), QStringLiteral("Selection Eraser"));

    Document small(kCanvas, Qt::white);
    ASSERT_TRUE(small.setSelection(rect(QRect(50, 55, 30, 10))));
    selectionStroke(small, SelectionBrush::Eraser, 20, 120, 60, 400);
    EXPECT_FALSE(small.selection().has_value());
}

TEST(SelectionPen, CancelLeavesTheSelectionAndBlocksEditsWhileRunning) {
    Document doc(kCanvas, Qt::white);
    SelectionStrokeController sc(&doc);
    BrushSettings big = defaultSettings(ToolType::Pencil);
    big.size = 200;
    ASSERT_TRUE(sc.begin(at(30, 60), SelectionBrush::Pen, big));
    for (double x = 34; x <= 100; x += 4)
        sc.add(at(x, 60));
    EXPECT_GT(sc.mask().at(60, 60), kOne * 99 / 100);
    EXPECT_FALSE(doc.selection().has_value());
    EXPECT_EQ(doc.selectAll().error(), EditError::StrokeInProgress);
    sc.cancel();
    EXPECT_FALSE(doc.isPainting());
    EXPECT_FALSE(doc.selection().has_value());
    EXPECT_TRUE(doc.selectAll());
}

namespace {

class CanvasEdgePainting : public ::testing::TestWithParam<ToolType> {};

void strokeAcross(Document& doc, ToolType tool, bool inBackground) {
    StrokeController sc(&doc);
    if (inBackground)
        sc.paintInBackground({}, {});
    BrushSettings st = defaultSettings(tool);
    st.size = 24;
    ASSERT_TRUE(sc.begin(at(20, 60), QColor(200, 30, 30), st));
    for (double x = 40; x <= 300; x += 20)
        sc.add(at(x, 60));
    sc.finish();
}

}

// Painting stops at the canvas's edge, for every tool and on either thread; what was moved past the edge stays as it was.
TEST_P(CanvasEdgePainting, AStrokeOverTheEdgeLeavesWhatLiesPastItAlone) {
    for (const bool inBackground : {false, true}) {
        Document doc(kCanvas, Qt::white);
        const LayerId id = doc.currentLayerId();
        doc.paintTarget(id).value()->fill(QRect(100, 40, 60, 40), Qt::blue);
        ASSERT_TRUE(doc.moveLayerExactly(id, transform::Turn::None, QPoint(80, 0)));  // to x 180 to 240, past the edge
        const auto blue = doc.currentLayer()->pixels.pixel(200, 60);
        ASSERT_GT(blue[3], 0);
        strokeAcross(doc, GetParam(), inBackground);
        const TiledImage& pixels = doc.currentLayer()->pixels;
        for (int y = 40; y < 80; ++y)
            for (int x = 180; x < 240; ++x)
                ASSERT_EQ(pixels.pixel(x, y), blue) << x << "," << y << (inBackground ? " in the background" : "");
        for (int x = kCanvas.width(); x < 180; ++x)
            ASSERT_EQ(pixels.pixel(x, 60)[3], 0) << x << (inBackground ? " in the background" : "");
        for (int x = 240; x < 320; ++x)
            ASSERT_EQ(pixels.pixel(x, 60)[3], 0) << x << (inBackground ? " in the background" : "");
        if (GetParam() != ToolType::Eraser && GetParam() != ToolType::Blur && GetParam() != ToolType::Smudge)
            EXPECT_GT(pixels.pixel(kCanvas.width() - 1, 60)[3], 0) << "it paints up to the edge";
    }
}

INSTANTIATE_TEST_SUITE_P(PaintingTools, CanvasEdgePainting,
                         ::testing::Values(ToolType::Pencil, ToolType::Brush, ToolType::WaterColor, ToolType::Marker, ToolType::Eraser,
                                           ToolType::Blur, ToolType::Smudge, ToolType::PixelArtPen),
                         [](const ::testing::TestParamInfo<ToolType>& i) { return toolTypeKey(i.param).toStdString(); });
