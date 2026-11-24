#include "core/raster/TiledImage.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>

using namespace doodlebold;

TEST(TiledImage, StartsTransparentAndAllocatesOnWrite) {
    TiledImage img(QSize(200, 130));
    EXPECT_EQ(img.tilesX(), 4);
    EXPECT_EQ(img.tilesY(), 3);
    EXPECT_TRUE(img.isEmpty());
    EXPECT_EQ(toUnorm8(img.pixel(10, 10))[3], 0);
    img.fill(QRect(70, 70, 4, 4), Qt::red);
    EXPECT_EQ(img.tileCount(), 1u);
    EXPECT_EQ(toUnorm8(img.pixel(71, 71))[0], 255);
    EXPECT_EQ(toUnorm8(img.pixel(71, 71))[3], 255);
    EXPECT_EQ(img.contentBounds(true), QRect(70, 70, 4, 4));
}

TEST(TiledImage, CopiesShareTilesUntilWritten) {
    TiledImage a(QSize(128, 128));
    a.fill(Qt::blue);
    TiledImage b = a;
    EXPECT_EQ(a.tileShared(0, 0), b.tileShared(0, 0));
    b.fill(QRect(0, 0, 1, 1), Qt::red);
    EXPECT_NE(a.tileShared(0, 0), b.tileShared(0, 0));
    EXPECT_EQ(a.tileShared(1, 1), b.tileShared(1, 1));
    EXPECT_EQ(toUnorm8(a.pixel(0, 0))[2], 255);
    EXPECT_EQ(toUnorm8(b.pixel(0, 0))[0], 255);
}

TEST(TiledImage, ImageRoundTripAndPartialEdges) {
    TiledImage img(QSize(100, 70));
    QImage src(30, 20, QImage::Format_RGBA8888_Premultiplied);
    src.fill(QColor(10, 20, 30));
    img.writeImage(src, QPoint(80, 60));
    const QImage out = img.toImage(QRect(80, 60, 30, 20));
    EXPECT_EQ(out.pixel(0, 0), qRgb(10, 20, 30));
    EXPECT_EQ(out.pixel(19, 9), qRgb(10, 20, 30));
    EXPECT_EQ(qAlpha(out.pixel(20, 0)), 255) << "past the canvas's right edge it stays";
    EXPECT_EQ(qAlpha(out.pixel(0, 10)), 255) << "past the bottom edge too";
    EXPECT_EQ(img.pixel(105, 60)[3], kChannelOne);
    EXPECT_EQ(img.contentBounds(true), QRect(80, 60, 30, 20));
}

TEST(TiledImage, KeepsPixelsAtNegativeCoordinatesAndStopsAtThePaintArea) {
    EXPECT_EQ(TiledImage::tileOf(0), 0);
    EXPECT_EQ(TiledImage::tileOf(Tile::kSize - 1), 0);
    EXPECT_EQ(TiledImage::tileOf(Tile::kSize), 1);
    EXPECT_EQ(TiledImage::tileOf(-1), -1);
    EXPECT_EQ(TiledImage::tileOf(-Tile::kSize), -1);
    EXPECT_EQ(TiledImage::tileOf(-Tile::kSize - 1), -2);

    TiledImage img(QSize(100, 70));
    QImage src(10, 10, QImage::Format_RGBA8888_Premultiplied);
    src.fill(QColor(10, 20, 30));
    img.writeImage(src, QPoint(-5, -70));
    EXPECT_EQ(img.pixel(-5, -70)[3], kChannelOne);
    EXPECT_EQ(img.pixel(4, -61)[3], kChannelOne);
    EXPECT_EQ(img.pixel(-6, -70)[3], 0);
    EXPECT_EQ(img.pixel(5, -70)[3], 0);
    EXPECT_EQ(img.contentBounds(true), QRect(-5, -70, 10, 10));
    const QImage out = img.toImage(QRect(-5, -70, 10, 10));
    EXPECT_EQ(out.pixel(0, 0), qRgb(10, 20, 30));
    EXPECT_EQ(img.toImage().pixelColor(0, 0).alpha(), 0) << "the canvas's picture has none of it";

    img.clear();
    img.writeImage(src, QPoint(-TiledImage::kOffCanvas - 5, 0));
    EXPECT_EQ(img.contentBounds(true), QRect(-TiledImage::kOffCanvas, 0, 5, 10));
}

TEST(TiledImage, ClearingDropsTiles) {
    TiledImage img(QSize(128, 128));
    img.fill(Qt::green);
    EXPECT_EQ(img.tileCount(), 4u);
    img.fill(QRect(0, 0, 64, 64), Qt::transparent);
    EXPECT_EQ(img.tileCount(), 3u);
    img.clear();
    EXPECT_TRUE(img.isEmpty());
}

TEST(TiledImage, ThumbnailKeepsAspectAndAveragesColour) {
    TiledImage img(QSize(400, 200));
    img.fill(QRect(0, 0, 200, 200), Qt::white);
    const QImage t = img.thumbnail(100);
    EXPECT_EQ(t.size(), QSize(100, 50));
    EXPECT_EQ(t.pixel(10, 10), 0xffffffffu);
    EXPECT_EQ(qAlpha(t.pixel(90, 10)), 0);
    EXPECT_EQ(TiledImage(QSize(30, 20)).thumbnail(100).size(), QSize(30, 20));
}

namespace {

// The thumbnail as it was first made, a tile looked up for every sample.
QImage thumbnailSampleBySample(const TiledImage& img, int maxSide) {
    const QSize size = img.size();
    const double scale = std::min(1.0, double(maxSide) / std::max(size.width(), size.height()));
    const QSize out(std::max(1, int(std::lround(size.width() * scale))), std::max(1, int(std::lround(size.height() * scale))));
    QImage thumb(out, QImage::Format_RGBA8888_Premultiplied);
    const double sx = double(size.width()) / out.width(), sy = double(size.height()) / out.height();
    for (int y = 0; y < out.height(); ++y)
        for (int x = 0; x < out.width(); ++x) {
            int sum[4] = {0, 0, 0, 0};
            for (int j = 0; j < 4; ++j)
                for (int i = 0; i < 4; ++i) {
                    const int px = std::min(size.width() - 1, int((x + (i + 0.5) / 4) * sx));
                    const int py = std::min(size.height() - 1, int((y + (j + 0.5) / 4) * sy));
                    if (const Tile* t = img.tile(px / Tile::kSize, py / Tile::kSize))
                        for (int c = 0; c < 4; ++c)
                            sum[c] += t->at(px % Tile::kSize, py % Tile::kSize)[c];
                }
            for (int c = 0; c < 4; ++c)
                thumb.scanLine(y)[x * 4 + c] = toUnorm8((sum[c] + 8) / 16);
        }
    return thumb;
}

}

TEST(TiledImage, ThumbnailIsTheAverageOfItsSamples) {
    TiledImage img(QSize(3000, 1700));
    for (int n = 0; n < 40; ++n)
        img.fill(QRect((n * 677) % 2900, (n * 389) % 1600, 30 + n * 7, 20 + n * 5),
                 QColor::fromRgb(n * 6, 255 - n * 6, (n * 37) % 256, 40 + n * 5));
    for (const int side : {116, 40, 333, 5000})
        EXPECT_EQ(img.thumbnail(side), thumbnailSampleBySample(img, side)) << side;
    TiledImage small(QSize(70, 130));
    small.fill(QRect(3, 60, 50, 40), QColor(200, 10, 90, 128));
    for (const int side : {40, 130, 7})
        EXPECT_EQ(small.thumbnail(side), thumbnailSampleBySample(small, side)) << side;
}

TEST(Channel, EightBitValuesSurviveTheRoundTrip) {
    for (int c = 0; c <= 255; ++c)
        ASSERT_EQ(toUnorm8(fromUnorm8(c)), c) << c;
    EXPECT_EQ(fromUnorm8(0), 0);
    EXPECT_EQ(fromUnorm8(255), kChannelOne);
    EXPECT_EQ(fromUnit(1.0f), kChannelOne);
    EXPECT_EQ(fromUnit(-0.5), 0);
}

TEST(Channel, EveryEightBitValueSurvivesTheRoundTrip) {
    for (int c = 0; c <= 255; ++c)
        ASSERT_EQ(toUnorm8(fromUnorm8(c)), c);
    EXPECT_EQ(fromUnorm8(150), 150 * 257);
    EXPECT_EQ(kChannelOne, 65535);
}

TEST(Channel, ProductsDoNotOverflow) {
    EXPECT_EQ(mulChannel(kChannelOne, kChannelOne), kChannelOne);
    EXPECT_EQ(mulChannel(kChannelOne, 0), 0);
    EXPECT_EQ(lerpChannel(0, kChannelOne, kChannelOne), kChannelOne);
    EXPECT_EQ(lerpChannel(kChannelOne, 0, kChannelOne), 0);
}

TEST(TiledImage, PremultipliedFillKeepsColourWithinAlpha) {
    TiledImage img(QSize(4, 4));
    img.fill(QColor(255, 128, 3, 77));
    const auto p = img.pixel(0, 0);
    EXPECT_EQ(p[3], fromUnorm8(77));
    for (int c = 0; c < 3; ++c)
        EXPECT_LE(p[c], p[3]);
    const QImage out = img.toImage();
    EXPECT_EQ(qAlpha(out.pixel(0, 0)), 77);
    EXPECT_EQ(qRed(out.pixel(0, 0)), 77);
}
