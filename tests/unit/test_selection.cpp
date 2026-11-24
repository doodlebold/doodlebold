#include "core/selection/SelectionOps.h"

#include <numbers>

#include <gtest/gtest.h>

using namespace doodlebold;
using namespace doodlebold::selection;

namespace {

constexpr int kOne = SelectionMask::kOne;
const QSize kCanvas(150, 100);

long long sum(const SelectionMask& m) {
    long long s = 0;
    for (int y = 0; y < m.size().height(); ++y)
        for (int x = 0; x < m.size().width(); ++x)
            s += m.at(x, y);
    return s;
}

SelectionMask block(const QRect& r, int value = kOne) {
    SelectionMask m(kCanvas);
    for (int y = r.top(); y <= r.bottom(); ++y) {
        std::vector<std::uint16_t> row(static_cast<std::size_t>(r.width()), static_cast<std::uint16_t>(value));
        m.writeRow(y, r.left(), r.width(), row.data());
    }
    m.squeeze();
    return m;
}

}

TEST(SelectionMask, EmptyMaskStoresNothingAndReadsZero) {
    const SelectionMask m(kCanvas);
    EXPECT_TRUE(m.isEmpty());
    EXPECT_EQ(m.at(10, 10), 0);
    EXPECT_EQ(m.at(-1, 0), 0);
    EXPECT_EQ(m.at(150, 0), 0);
    EXPECT_TRUE(m.bounds().isNull());
}

TEST(SelectionMask, SelectAllSharesOneFullTileAndKeepsEdgeTilesInsideTheCanvas) {
    const SelectionMask m = all(kCanvas);
    EXPECT_EQ(m.at(0, 0), kOne);
    EXPECT_EQ(m.at(149, 99), kOne);
    EXPECT_EQ(m.at(150, 99), 0);
    EXPECT_EQ(m.tilesX() * m.tilesY(), 6);
    EXPECT_EQ(m.tile(0, 0), m.tile(1, 0));
    EXPECT_TRUE(SelectionMask::isFull(m.tile(0, 0)));
    EXPECT_FALSE(SelectionMask::isFull(m.tile(2, 1)));
    EXPECT_EQ(m.bounds(true), QRect(QPoint(0, 0), kCanvas));
    EXPECT_EQ(sum(m), 150ll * 100 * kOne);
}

TEST(SelectionMask, WritingToASharedTileLeavesTheCopyAlone) {
    const SelectionMask a = all(kCanvas);
    SelectionMask b = a;
    b.writableTile(0, 0)->row(3)[4] = 100;
    EXPECT_EQ(b.at(4, 3), 100);
    EXPECT_EQ(a.at(4, 3), kOne);
    EXPECT_EQ(a.at(5, 3), kOne);
    EXPECT_FALSE(a == b);
    EXPECT_EQ(a.differingArea(&b), QRect(0, 0, 64, 64));
}

TEST(SelectionMask, SqueezeDropsZeroTilesAndSharesFullOnes) {
    SelectionMask m(kCanvas);
    m.writableTile(0, 0);
    SelectionTile* t = m.writableTile(1, 0);
    t->v.fill(kOne);
    m.squeeze();
    EXPECT_EQ(m.tile(0, 0), nullptr);
    EXPECT_TRUE(SelectionMask::isFull(m.tile(1, 0)));
}

TEST(SelectionMask, RowReadsAndBoundsAreExact) {
    const SelectionMask m = block(QRect(70, 20, 30, 10), 8000);
    std::vector<std::uint16_t> row(41);
    m.row(25, 60, 41, row.data());
    EXPECT_EQ(row[9], 0);
    EXPECT_EQ(row[10], 8000);
    EXPECT_EQ(row[39], 8000);
    EXPECT_EQ(row[40], 0);
    EXPECT_EQ(m.bounds(true), QRect(70, 20, 30, 10));
    EXPECT_EQ(m.bounds(), QRect(64, 0, 64, 64));
}

TEST(SelectionMask, ToImageIsAnAlpha8Rendering) {
    const SelectionMask m = block(QRect(2, 2, 4, 4), kOne / 2);
    const QImage img = m.toImage(QRect(0, 0, 10, 10));
    ASSERT_EQ(img.format(), QImage::Format_Alpha8);
    EXPECT_EQ(img.size(), QSize(10, 10));
    EXPECT_EQ(img.constScanLine(3)[3], toUnorm8(kOne / 2));
    EXPECT_EQ(img.constScanLine(0)[0], 0);
}

TEST(SelectionOps, InvertIsTheComplementAndItsOwnInverse) {
    const SelectionMask m = block(QRect(10, 10, 100, 50), 12000);
    const SelectionMask inv = invert(m);
    EXPECT_EQ(inv.at(0, 0), kOne);
    EXPECT_EQ(inv.at(20, 20), kOne - 12000);
    EXPECT_EQ(inv.at(149, 99), kOne);
    EXPECT_EQ(inv.at(150, 99), 0);
    EXPECT_TRUE(invert(inv) == m);
    EXPECT_TRUE(invert(all(kCanvas)).isEmpty());
    EXPECT_TRUE(invert(SelectionMask(kCanvas)) == all(kCanvas));
}

TEST(SelectionOps, RectangleWithoutAntialiasSelectsPixelsWhoseCentresAreInside) {
    const SelectionMask m = rectangle(kCanvas, QRectF(10.4, 20.6, 30, 10), false);
    EXPECT_EQ(m.bounds(true), QRect(10, 21, 30, 10));
    EXPECT_EQ(sum(m), 30ll * 10 * kOne);
    EXPECT_EQ(m.at(10, 21), kOne);
    EXPECT_EQ(m.at(9, 21), 0);
}

TEST(SelectionOps, RectangleAntialiasCoversExactAreaWithPartialEdges) {
    const SelectionMask m = rectangle(kCanvas, QRectF(10.25, 20.5, 30.5, 10.0), true);
    EXPECT_EQ(m.at(10, 25), kOne * 3 / 4);
    EXPECT_EQ(m.at(40, 25), kOne * 3 / 4);
    EXPECT_NEAR(m.at(20, 20), kOne / 2, 1);
    EXPECT_NEAR(m.at(20, 30), kOne / 2, 1);
    EXPECT_EQ(m.at(20, 25), kOne);
    EXPECT_NEAR(m.at(10, 20), kOne * 3 / 8, 1);
    EXPECT_NEAR(double(sum(m)) / kOne, 30.5 * 10.0, 0.05);
}

TEST(SelectionOps, RectangleIsClippedToTheCanvasAndCanCoverIt) {
    const SelectionMask m = rectangle(kCanvas, QRectF(-50, -50, 1000, 1000), true);
    EXPECT_TRUE(m == all(kCanvas));
    EXPECT_TRUE(rectangle(kCanvas, QRectF(200, 10, 20, 20), true).isEmpty());
    EXPECT_TRUE(rectangle(kCanvas, QRectF(), true).isEmpty());
    EXPECT_EQ(rectangle(kCanvas, QRectF(40, 30, -20, -10), false).bounds(true), QRect(20, 20, 20, 10));
}

TEST(SelectionOps, LassoIsHardEdgedAndSelectsWhatItMostlyCovers) {
    QPolygonF tri;
    tri << QPointF(10, 10) << QPointF(90, 10) << QPointF(10, 70);
    const SelectionMask hard = polygon(kCanvas, tri);
    for (int y = 0; y < 100; ++y)
        for (int x = 0; x < 150; ++x)
            ASSERT_TRUE(hard.at(x, y) == 0 || hard.at(x, y) == kOne);
    EXPECT_NEAR(double(sum(hard)) / kOne, 0.5 * 80 * 60, 0.5 * 80 * 60 * 0.02);
    EXPECT_EQ(hard.at(12, 12), kOne);
    EXPECT_EQ(hard.at(80, 60), 0);
    QPolygonF p;
    p << QPointF(10.0, 10.75) << QPointF(30.0, 10.75) << QPointF(30.0, 11.75) << QPointF(10.0, 11.75);
    const SelectionMask band = polygon(kCanvas, p);
    EXPECT_EQ(band.at(15, 10), 0) << "a quarter of the pixel";
    EXPECT_EQ(band.at(15, 11), kOne) << "three quarters of the pixel";
}

TEST(SelectionOps, MarqueeSelectsTheInclusivePixelRectangle) {
    const SelectionMask m = marquee(kCanvas, QPoint(10, 20), QPoint(40, 30));
    EXPECT_EQ(m.bounds(true), QRect(10, 20, 31, 11));
    EXPECT_EQ(sum(m), 31ll * 11 * kOne);
    EXPECT_TRUE(marquee(kCanvas, QPoint(40, 30), QPoint(10, 20)) == m);
    EXPECT_EQ(sum(marquee(kCanvas, QPoint(5, 5), QPoint(5, 5))), kOne);
}

TEST(SelectionOps, PolygonAlignedSquareIsExactAndMatchesRectangle) {
    QPolygonF sq;
    sq << QPointF(20, 30) << QPointF(80, 30) << QPointF(80, 70) << QPointF(20, 70);
    const SelectionMask p = polygon(kCanvas, sq);
    EXPECT_TRUE(p == rectangle(kCanvas, QRectF(20, 30, 60, 40), false));
    EXPECT_EQ(p.bounds(true), QRect(20, 30, 60, 40));
}

TEST(SelectionOps, PolygonFillRulesDifferOnASelfOverlap) {
    QPolygonF star;
    for (int i = 0; i < 5; ++i) {
        const double a = -std::numbers::pi / 2 + i * 4 * std::numbers::pi / 5;
        star << QPointF(75 + 40 * std::cos(a), 50 + 40 * std::sin(a));
    }
    const SelectionMask evenOdd = polygon(kCanvas, star, Qt::OddEvenFill);
    const SelectionMask winding = polygon(kCanvas, star, Qt::WindingFill);
    EXPECT_EQ(evenOdd.at(75, 50), 0);
    EXPECT_EQ(winding.at(75, 50), kOne);
    EXPECT_LT(sum(evenOdd), sum(winding));
}

TEST(SelectionOps, PolygonOutsideTheCanvasIsClippedAndDegenerateIsEmpty) {
    QPolygonF big;
    big << QPointF(-100, -100) << QPointF(500, -100) << QPointF(500, 500) << QPointF(-100, 500);
    EXPECT_TRUE(polygon(kCanvas, big) == all(kCanvas));
    QPolygonF line;
    line << QPointF(10, 10) << QPointF(50, 10) << QPointF(90, 10);
    EXPECT_TRUE(polygon(kCanvas, line).isEmpty());
    EXPECT_TRUE(polygon(kCanvas, QPolygonF()).isEmpty());
}

TEST(SelectionOps, FromLayerAlphaUsesTheAlphaChannelAsCoverage) {
    TiledImage layer(kCanvas);
    layer.fill(QRect(10, 10, 40, 40), QColor(255, 0, 0, 255));
    layer.fill(QRect(60, 10, 10, 10), QColor(0, 0, 255, 128));
    const SelectionMask m = fromLayerAlpha(layer);
    EXPECT_EQ(m.at(20, 20), kOne);
    EXPECT_EQ(m.at(65, 15), layer.pixel(65, 15)[3]);
    EXPECT_GT(m.at(65, 15), 0);
    EXPECT_LT(m.at(65, 15), kOne);
    EXPECT_EQ(m.at(0, 0), 0);
    EXPECT_TRUE(fromLayerAlpha(TiledImage(kCanvas)).isEmpty());
}

TEST(SelectionOps, CombineReplaceTakesTheShape) {
    const SelectionMask a = block(QRect(0, 0, 50, 50)), b = block(QRect(25, 25, 50, 50));
    EXPECT_TRUE(combine(a, b, CombineMode::Replace) == b);
    EXPECT_TRUE(combine(std::nullopt, b, CombineMode::Replace) == b);
}

TEST(SelectionOps, CombineModesOnHardMasksAreSetOperations) {
    const SelectionMask a = block(QRect(0, 0, 50, 50)), b = block(QRect(25, 25, 50, 50));
    const SelectionMask u = combine(a, b, CombineMode::Add);
    const SelectionMask d = combine(a, b, CombineMode::Subtract);
    const SelectionMask i = combine(a, b, CombineMode::Intersect);
    EXPECT_EQ(sum(u), (2500ll + 2500 - 625) * kOne);
    EXPECT_EQ(sum(d), (2500ll - 625) * kOne);
    EXPECT_EQ(sum(i), 625ll * kOne);
    EXPECT_EQ(u.at(60, 60), kOne);
    EXPECT_EQ(d.at(30, 30), 0);
    EXPECT_EQ(d.at(10, 10), kOne);
    EXPECT_EQ(i.at(30, 30), kOne);
    EXPECT_EQ(i.at(10, 10), 0);
}

TEST(SelectionOps, CombineModesOnSoftMasksAddSubtractAndTakeTheSmaller) {
    const SelectionMask a = block(QRect(0, 0, 10, 10), 12000), b = block(QRect(0, 0, 10, 10), 4000);
    EXPECT_EQ(combine(a, b, CombineMode::Add).at(5, 5), 16000);
    EXPECT_EQ(combine(b, a, CombineMode::Add).at(5, 5), 16000);
    EXPECT_EQ(combine(a, b, CombineMode::Subtract).at(5, 5), 8000);
    EXPECT_FALSE(combine(b, a, CombineMode::Subtract).at(5, 5));
    EXPECT_EQ(combine(a, b, CombineMode::Intersect).at(5, 5), 4000);
    const SelectionMask h = block(QRect(0, 0, 10, 10)), g = block(QRect(5, 5, 10, 10));
    EXPECT_EQ(combine(h, g, CombineMode::Add).at(7, 7), kOne);
    EXPECT_EQ(combine(h, g, CombineMode::Subtract).at(7, 7), 0);
    EXPECT_EQ(combine(h, g, CombineMode::Subtract).at(2, 2), kOne);
    EXPECT_EQ(combine(h, g, CombineMode::Intersect).at(2, 2), 0);
}

TEST(SelectionOps, CombiningWithNoSelectionTreatsItAsNothingSelected) {
    const SelectionMask b = block(QRect(25, 25, 50, 50));
    EXPECT_TRUE(combine(std::nullopt, b, CombineMode::Add) == b);
    EXPECT_TRUE(combine(std::nullopt, b, CombineMode::Subtract).isEmpty());
    EXPECT_TRUE(combine(std::nullopt, b, CombineMode::Intersect).isEmpty());
}

TEST(SelectionOps, SubtractingEverythingLeavesAnEmptyMaskEvenInEdgeTiles) {
    EXPECT_TRUE(combine(all(kCanvas), all(kCanvas), CombineMode::Subtract).isEmpty());
    EXPECT_TRUE(combine(all(kCanvas), invert(SelectionMask(kCanvas)), CombineMode::Subtract).isEmpty());
}

TEST(SelectionOps, DilateByOnePixelUsesThePlusShape) {
    const SelectionMask m = block(QRect(50, 50, 1, 1));
    const SelectionMask d = dilate(m, 1);
    EXPECT_EQ(sum(d), 5ll * kOne);
    EXPECT_EQ(d.at(51, 50), kOne);
    EXPECT_EQ(d.at(50, 49), kOne);
    EXPECT_EQ(d.at(51, 51), 0);
}

TEST(SelectionOps, GrowAndShrinkUseACircle) {
    const SelectionMask d = dilate(block(QRect(50, 50, 1, 1)), 5);
    EXPECT_EQ(d.at(55, 50), kOne);
    EXPECT_EQ(d.at(53, 54), kOne);
    EXPECT_EQ(d.at(54, 54), 0);
    EXPECT_TRUE(erode(d, 5) == block(QRect(50, 50, 1, 1)));
}

TEST(SelectionOps, ErodeShrinksAndRemovesThinShapes) {
    const SelectionMask m = block(QRect(40, 40, 20, 20));
    EXPECT_EQ(erode(m, 3).bounds(true), QRect(43, 43, 14, 14));
    EXPECT_TRUE(erode(m, 10).isEmpty());
    EXPECT_TRUE(erode(block(QRect(10, 10, 30, 2)), 1).isEmpty());
    EXPECT_TRUE(erode(m, 0) == m);
    EXPECT_TRUE(dilate(m, 0) == m);
}

TEST(SelectionOps, MorphologyOnSoftMasksTakesMaxAndMin) {
    SelectionMask m(kCanvas);
    const std::uint16_t row[3] = {1000, 9000, 4000};
    m.writeRow(50, 50, 3, row);
    m.squeeze();
    const SelectionMask d = dilate(m, 1);
    EXPECT_EQ(d.at(49, 50), 1000);
    EXPECT_EQ(d.at(50, 50), 9000);
    EXPECT_EQ(d.at(51, 49), 9000);
    EXPECT_EQ(d.at(52, 49), 4000);
    EXPECT_EQ(d.at(49, 49), 0);
    EXPECT_EQ(d.at(53, 50), 4000);
}

TEST(SelectionOps, ErosionDoesNotEatIntoTheCanvasBorder) {
    const SelectionMask e = erode(all(kCanvas), 4);
    EXPECT_TRUE(e == all(kCanvas));
    const SelectionMask half = erode(rectangle(kCanvas, QRectF(0, 0, 80, 100), false), 4);
    EXPECT_EQ(half.bounds(true), QRect(0, 0, 76, 100));
}
