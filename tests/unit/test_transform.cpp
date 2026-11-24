#include "core/document/Document.h"
#include "core/transform/Lift.h"

#include <gtest/gtest.h>

using namespace doodlebold;
using namespace doodlebold::transform;

namespace {

constexpr int kOne = kChannelOne;
const QSize kCanvas(200, 150);

const Interpolation kAllModes[] = {Interpolation::Nearest, Interpolation::Bilinear, Interpolation::Bicubic, Interpolation::Lanczos2,
                                   Interpolation::Lanczos3};

TiledImage pattern() {
    TiledImage img(kCanvas);
    for (int y = 20; y < 70; ++y)
        for (int x = 30; x < 110; ++x) {
            Tile* t = img.writableTile(x / Tile::kSize, y / Tile::kSize);
            Channel* p = t->at(x % Tile::kSize, y % Tile::kSize);
            const int a = 6000 + ((x * 37 + y * 91) % 10000);
            p[3] = static_cast<Channel>(a);
            p[0] = static_cast<Channel>(a * ((x * 5) % 100) / 100);
            p[1] = static_cast<Channel>(a * ((y * 7) % 100) / 100);
            p[2] = static_cast<Channel>(a * ((x + y) % 100) / 100);
        }
    return img;
}

const QRect kPatternBox(30, 20, 80, 50);

long long alphaSum(const TiledImage& img) {
    long long s = 0;
    for (int y = 0; y < img.size().height(); ++y)
        for (int x = 0; x < img.size().width(); ++x)
            s += img.pixel(x, y)[3];
    return s;
}

bool same(const TiledImage& a, const TiledImage& b) {
    for (int y = 0; y < a.size().height(); ++y)
        for (int x = 0; x < a.size().width(); ++x)
            if (a.pixel(x, y) != b.pixel(x, y))
                return false;
    return true;
}

TiledImage solid(const QRect& r, const QColor& c) {
    TiledImage img(kCanvas);
    img.fill(r, c);
    return img;
}

}

TEST(Transform, InteractiveQualityNeverExceedsFinal) {
    using I = Interpolation;
    EXPECT_EQ(interactiveQuality(I::Bicubic, I::Bilinear, I::Nearest, false), I::Bilinear);
    EXPECT_EQ(interactiveQuality(I::Bicubic, I::Bilinear, I::Nearest, true), I::Nearest);
    EXPECT_EQ(interactiveQuality(I::Nearest, I::Lanczos3, I::Lanczos3, true), I::Nearest);
    EXPECT_EQ(interactiveQuality(I::Lanczos3, I::Lanczos3, I::Bicubic, false), I::Lanczos3);
}

TEST(Transform, IdentityQuadReproducesTheSourceExactlyInEveryMode) {
    const TiledImage src = pattern();
    for (const Interpolation m : kAllModes)
        EXPECT_TRUE(same(resample(src, kPatternBox, Quad::of(kPatternBox), m), src)) << int(m);
}

TEST(Transform, WholePixelMoveWithoutScalingIsExactInEveryMode) {
    const TiledImage src = pattern();
    const QPoint by(37, 41);
    for (const Interpolation m : kAllModes) {
        const TiledImage got = resample(src, kPatternBox, Quad::of(QRectF(kPatternBox.translated(by))), m);
        for (int y = 0; y < kCanvas.height(); ++y)
            for (int x = 0; x < kCanvas.width(); ++x)
                ASSERT_EQ(got.pixel(x, y), src.pixel(x - by.x(), y - by.y())) << int(m) << " at " << x << "," << y;
    }
}

TEST(Transform, NearestEnlargementRepeatsEachPixel) {
    const TiledImage src = pattern();
    const TiledImage big = resample(src, kPatternBox, Quad::of(QRectF(30, 20, 160, 100)), Interpolation::Nearest);
    for (int y = 0; y < 100; ++y)
        for (int x = 0; x < 160; ++x)
            ASSERT_EQ(big.pixel(30 + x, 20 + y), src.pixel(30 + x / 2, 20 + y / 2)) << x << "," << y;
    EXPECT_EQ(big.pixel(190, 50)[3], 0);
}

TEST(Transform, UniformColourStaysUniformUnderEveryFilterAndScale) {
    const TiledImage src = solid(QRect(20, 20, 60, 40), QColor(200, 100, 50));
    const auto want = src.pixel(30, 30);
    const QRectF targets[] = {QRectF(20, 20, 107, 71), QRectF(30, 30, 20, 13), QRectF(20, 20, 60, 41)};
    for (const Interpolation m : kAllModes)
        for (const QRectF& t : targets) {
            const TiledImage out = resample(src, QRect(20, 20, 60, 40), Quad::of(t), m);
            const int cx = int(t.center().x()), cy = int(t.center().y());
            for (int dy = -3; dy <= 3; ++dy)
                for (int dx = -3; dx <= 3; ++dx) {
                    const auto p = out.pixel(cx + dx, cy + dy);
                    for (int c = 0; c < 4; ++c)
                        ASSERT_NEAR(p[std::size_t(c)], want[std::size_t(c)], 2) << int(m);
                }
        }
}

TEST(Transform, ADeformedQuadFollowsTheBilinearMap) {
    const TiledImage src = solid(QRect(20, 20, 40, 40), Qt::white);
    Quad q;
    q.corner = {QPointF(20, 20), QPointF(100, 20), QPointF(100, 100), QPointF(20, 60)};
    const TiledImage out = resample(src, QRect(20, 20, 40, 40), q, Interpolation::Bilinear);
    EXPECT_EQ(out.pixel(60, 40)[3], kOne);
    EXPECT_EQ(out.pixel(90, 90)[3], kOne);
    EXPECT_EQ(out.pixel(30, 90)[3], 0);
    EXPECT_EQ(out.pixel(105, 50)[3], 0);
    EXPECT_NEAR(double(alphaSum(out)) / kOne, 4800.0, 40.0);
}

TEST(Transform, PerspectiveAtZeroIsTheBilinearMapAndDiffersOtherwise) {
    TiledImage src(kCanvas);
    for (int y = 20; y < 60; ++y)
        src.fill(QRect(20, y, 40, 1), QColor(y * 4 % 256, 80, 200));
    Quad q;
    q.corner = {QPointF(30, 20), QPointF(110, 30), QPointF(90, 100), QPointF(20, 90)};
    const TiledImage flat = resample(src, QRect(20, 20, 40, 40), q, Interpolation::Bilinear, 0.0);
    const TiledImage same0 = resample(src, QRect(20, 20, 40, 40), q, Interpolation::Bilinear, 0.0);
    const TiledImage deep = resample(src, QRect(20, 20, 40, 40), q, Interpolation::Bilinear, 1.0);
    EXPECT_TRUE(same(flat, same0));
    EXPECT_FALSE(same(flat, deep));
    EXPECT_NEAR(double(alphaSum(flat)), double(alphaSum(deep)), double(alphaSum(flat)) * 0.01);
}

TEST(Transform, DegenerateQuadGivesAnEmptyResult) {
    const TiledImage src = pattern();
    Quad q;
    q.corner = {QPointF(50, 50), QPointF(90, 50), QPointF(130, 50), QPointF(10, 50)};
    EXPECT_TRUE(resample(src, kPatternBox, q, Interpolation::Bilinear).isEmpty());
}

TEST(Transform, SelectionMasksGoThroughTheSameResampler) {
    SelectionMask m = selection::marquee(kCanvas, QPoint(30, 20), QPoint(69, 59));
    const SelectionMask big = resample(m, QRect(30, 20, 40, 40), Quad::of(QRectF(10, 10, 80, 80)), Interpolation::Bicubic);
    EXPECT_EQ(big.bounds(true), QRect(10, 10, 80, 80));
    EXPECT_EQ(big.at(50, 50), SelectionMask::kOne);
    const TiledImage layer = solid(QRect(30, 20, 40, 40), Qt::black);
    const TiledImage bigLayer = resample(layer, QRect(30, 20, 40, 40), Quad::of(QRectF(10, 10, 80, 80)), Interpolation::Bicubic);
    EXPECT_TRUE(selection::fromLayerAlpha(bigLayer) == big);
}

TEST(Transform, MovedAreaTurnsAboutTheCentre) {
    const QRect a(10, 20, 50, 30);
    EXPECT_EQ(movedArea(a, Turn::FlipHorizontal), a);
    EXPECT_EQ(movedArea(a, Turn::Rotate180), a);
    EXPECT_EQ(movedArea(a, Turn::Clockwise), QRect(20, 10, 30, 50));
    EXPECT_EQ(movedArea(a, Turn::CounterClockwise), QRect(20, 10, 30, 50));
    EXPECT_EQ(movedArea(a, Turn::None, QPoint(5, -3)), QRect(15, 17, 50, 30));
    EXPECT_EQ(movedArea(QRect(0, 0, 5, 2), Turn::Clockwise), QRect(1, -2, 2, 5));
}

TEST(Transform, FlipsAndQuarterTurnsMovePixelsExactly) {
    const TiledImage src = pattern();
    const QRect a = kPatternBox;
    const TiledImage h = moveExactly(src, a, Turn::FlipHorizontal);
    const TiledImage v = moveExactly(src, a, Turn::FlipVertical);
    const TiledImage cw = moveExactly(src, a, Turn::Clockwise);
    const TiledImage ccw = moveExactly(src, a, Turn::CounterClockwise);
    const QRect t = movedArea(a, Turn::Clockwise);
    for (int j = 0; j < a.height(); ++j)
        for (int i = 0; i < a.width(); ++i) {
            const auto p = src.pixel(a.left() + i, a.top() + j);
            ASSERT_EQ(h.pixel(a.left() + a.width() - 1 - i, a.top() + j), p);
            ASSERT_EQ(v.pixel(a.left() + i, a.top() + a.height() - 1 - j), p);
            ASSERT_EQ(cw.pixel(t.left() + a.height() - 1 - j, t.top() + i), p);
            ASSERT_EQ(ccw.pixel(t.left() + j, t.top() + a.width() - 1 - i), p);
        }
    EXPECT_EQ(alphaSum(cw), alphaSum(src));
    EXPECT_EQ(cw.contentBounds(true), t);
}

TEST(Transform, FlipsAndTurnsUndoEachOtherExactly) {
    const TiledImage src = pattern();
    const QRect a = kPatternBox;
    EXPECT_TRUE(same(moveExactly(moveExactly(src, a, Turn::FlipHorizontal), a, Turn::FlipHorizontal), src));
    EXPECT_TRUE(same(moveExactly(moveExactly(src, a, Turn::FlipVertical), a, Turn::FlipVertical), src));
    EXPECT_TRUE(same(moveExactly(moveExactly(src, a, Turn::Rotate180), a, Turn::Rotate180), src));
    TiledImage r = src;
    QRect area = a;
    for (int i = 0; i < 4; ++i) {
        r = moveExactly(r, area, Turn::Clockwise);
        area = movedArea(area, Turn::Clockwise);
    }
    EXPECT_TRUE(same(r, src));
    const TiledImage there = moveExactly(src, a, Turn::Clockwise);
    EXPECT_TRUE(same(moveExactly(there, movedArea(a, Turn::Clockwise), Turn::CounterClockwise), src));
    EXPECT_TRUE(same(moveExactly(src, a, Turn::Rotate180), moveExactly(moveExactly(src, a, Turn::FlipHorizontal), a, Turn::FlipVertical)));
}

TEST(Transform, OffsetMovesAreExactAndKeepWhatLandsPastTheCanvas) {
    const TiledImage src = pattern();
    const TiledImage moved = moveExactly(src, kPatternBox, Turn::None, QPoint(100, 60));
    EXPECT_EQ(moved.pixel(130, 80), src.pixel(30, 20));
    EXPECT_EQ(moved.contentBounds(true), kPatternBox.translated(100, 60));

    const TiledImage away = moveExactly(src, kPatternBox, Turn::None, QPoint(-500, -300));
    EXPECT_EQ(away.contentBounds(true), kPatternBox.translated(-500, -300));
    EXPECT_EQ(away.pixel(30 - 500, 20 - 300), src.pixel(30, 20));
    const TiledImage back = moveExactly(away, away.contentBounds(true), Turn::None, QPoint(500, 300));
    EXPECT_EQ(back.pixel(30, 20), src.pixel(30, 20));
    EXPECT_EQ(back.contentBounds(true), kPatternBox) << "and it comes back whole";

    EXPECT_TRUE(moveExactly(src, kPatternBox, Turn::None, QPoint(TiledImage::kOffCanvas * 3, 0)).isEmpty());
}

TEST(Transform, SelectionsStillEndAtTheCanvas) {
    const SelectionMask m = selection::marquee(kCanvas, QPoint(30, 20), QPoint(79, 49));
    EXPECT_TRUE(moveExactly(m, QRect(30, 20, 50, 30), Turn::None, QPoint(-500, 0)).isEmpty());
}

TEST(Transform, AResampleKeepsWhatLandsPastTheCanvas) {
    const TiledImage src = pattern();
    const QRect box = kPatternBox;
    const TiledImage out = resample(src, box, Quad::of(QRectF(box).translated(-200, -100)), Interpolation::Nearest, 0);
    EXPECT_FALSE(out.isEmpty());
    EXPECT_EQ(out.contentBounds(true).size(), box.size());
    EXPECT_EQ(out.contentBounds(true).topLeft(), box.topLeft() + QPoint(-200, -100));
}

TEST(Transform, ExactMovesIgnorePixelsOutsideTheArea) {
    TiledImage src = pattern();
    src.fill(QRect(150, 100, 10, 10), Qt::red);
    const TiledImage out = moveExactly(src, kPatternBox, Turn::FlipHorizontal);
    EXPECT_EQ(out.pixel(155, 105)[3], 0);
}

TEST(Transform, SelectionMasksMoveExactlyToo) {
    const SelectionMask m = selection::marquee(kCanvas, QPoint(30, 20), QPoint(79, 49));
    const SelectionMask turned = moveExactly(m, QRect(30, 20, 50, 30), Turn::Clockwise);
    EXPECT_EQ(turned.bounds(true), QRect(40, 10, 30, 50));
    EXPECT_EQ(turned.at(45, 12), SelectionMask::kOne);
}

TEST(Lift, SplitsALayerBySelectionCoverageAndRecombinesExactly) {
    const TiledImage layer = pattern();
    SelectionMask sel(kCanvas);
    std::vector<std::uint16_t> row(50);
    for (int i = 0; i < 50; ++i)
        row[std::size_t(i)] = static_cast<std::uint16_t>(i * SelectionMask::kOne / 49);
    for (int y = 25; y < 45; ++y)
        sel.writeRow(y, 40, 50, row.data());
    sel.squeeze();
    const Lifted l = liftSelection(layer, sel);
    for (int y = 0; y < 150; ++y)
        for (int x = 0; x < 200; ++x) {
            const auto p = layer.pixel(x, y), a = l.part.pixel(x, y), b = l.rest.pixel(x, y);
            for (std::size_t c = 0; c < 4; ++c)
                ASSERT_EQ(a[c] + b[c], p[c]);
        }
    EXPECT_EQ(l.rest.tileShared(1, 1), layer.tileShared(1, 1));
    SelectionMask all = selection::all(kCanvas);
    const Lifted whole = liftSelection(layer, all);
    EXPECT_TRUE(same(whole.part, layer));
    EXPECT_TRUE(whole.rest.isEmpty());
    EXPECT_EQ(whole.part.tileShared(1, 0), layer.tileShared(1, 0));
}

TEST(Lift, ComposeOverIsSourceOverAndLeavesOpaqueTilesAlone) {
    TiledImage base = solid(QRect(0, 0, 64, 64), Qt::blue);
    TiledImage top = solid(QRect(10, 10, 20, 20), Qt::red);
    top.fill(QRect(40, 10, 10, 10), QColor(255, 0, 0, 128));
    const TiledImage out = composeOver(base, top);
    EXPECT_EQ(out.pixel(15, 15), top.pixel(15, 15));
    EXPECT_EQ(out.pixel(5, 5), base.pixel(5, 5));
    const auto half = out.pixel(45, 15);
    EXPECT_EQ(half[3], kOne);
    const int through = kOne - top.pixel(45, 15)[3];
    EXPECT_NEAR(half[0], top.pixel(45, 15)[0] + base.pixel(0, 0)[0] * through / kOne, 1);
    EXPECT_NEAR(half[2], base.pixel(0, 0)[2] * through / kOne, 1);
    EXPECT_EQ(composeOver(base, TiledImage(kCanvas)).tileShared(0, 0), base.tileShared(0, 0));
}
