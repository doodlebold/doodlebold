#include "core/io/JpegPreview.h"

#include <gtest/gtest.h>

using namespace doodlebold;
using namespace doodlebold::io;

TEST(JpegPreview, CompressionResultFormatsForTheDialog) {
    struct Case {
        qint64 bytes;
        const char* size;
        const char* rate;
    };
    const Case cases[] = {{17291, "16.89KB", "1/11.37"}, {15571, "15.21KB", "1/12.63"}, {15314, "14.96KB", "1/12.84"},
                          {13552, "13.23KB", "1/14.51"}, {5246, "5246B", "1/37.49"},    {5061, "5061B", "1/38.86"},
                          {5020, "5020B", "1/39.18"}};
    for (const Case& c : cases) {
        CompressionResult r;
        r.pixels = QSize(256, 256);
        r.fileSize = c.bytes;
        EXPECT_EQ(r.fileSizeText(), QString::fromLatin1(c.size)) << c.bytes;
        EXPECT_EQ(r.rateText(), QString::fromLatin1(c.rate)) << c.bytes;
    }
}

TEST(JpegPreview, SmallFilesAreShownInBytesAndHugeOnesInMegabytes) {
    CompressionResult r;
    r.pixels = QSize(10, 10);
    r.fileSize = 767;
    EXPECT_EQ(r.fileSizeText(), QStringLiteral("767B"));
    r.fileSize = 0;
    EXPECT_EQ(r.fileSizeText(), QStringLiteral("0B"));
    EXPECT_EQ(r.rateText(), QStringLiteral("1/0.00"));
    r.fileSize = 10 * 1024;
    EXPECT_EQ(r.fileSizeText(), QStringLiteral("10.00KB"));
    r.fileSize = 3 * 1024 * 1024 + 512 * 1024;
    EXPECT_EQ(r.fileSizeText(), QStringLiteral("3.50MB"));
}

TEST(JpegPreview, ScaleListHasThirteenEntries) {
    ASSERT_EQ(previewScales().size(), 13u);
    const char* labels[] = {"0.78%", "1.56%", "3.12%", "6.25%", "12.5%", "25%", "50%", "100%", "200%", "400%", "800%", "1600%", "3200%"};
    for (int i = 0; i < kPreviewScaleCount; ++i)
        EXPECT_EQ(previewScaleLabel(i), QString::fromLatin1(labels[i])) << i;
    EXPECT_EQ(previewScales()[static_cast<std::size_t>(kPreviewScale100)], 100.0);
    for (int i = 1; i < kPreviewScaleCount; ++i)
        EXPECT_DOUBLE_EQ(previewScales()[static_cast<std::size_t>(i)] / previewScales()[static_cast<std::size_t>(i - 1)], 2.0);
}

TEST(JpegPreview, FitPicksTheLargestScaleThatShowsEverything) {
    EXPECT_EQ(fitScaleIndex(QSize(256, 256), QSize(400, 400)), kPreviewScale100);
    EXPECT_EQ(fitScaleIndex(QSize(256, 256), QSize(600, 400)), kPreviewScale100);
    EXPECT_EQ(fitScaleIndex(QSize(256, 256), QSize(520, 520)), kPreviewScale100 + 1);
    EXPECT_EQ(fitScaleIndex(QSize(4000, 3000), QSize(400, 400)), 3);
    EXPECT_EQ(fitScaleIndex(QSize(16000, 16000), QSize(200, 200)), 0);
    EXPECT_EQ(fitScaleIndex(QSize(16000, 16000), QSize(10, 10)), 0);
    EXPECT_EQ(fitScaleIndex(QSize(10, 10), QSize(100000, 100000)), kPreviewScaleCount - 1);
}

TEST(JpegPreview, PreviewMatchesTheEncodedFile) {
    QImage flat(64, 48, QImage::Format_RGB888);
    for (int y = 0; y < 48; ++y)
        for (int x = 0; x < 64; ++x)
            flat.setPixelColor(x, y, QColor(x * 4, y * 5, (x ^ y) * 3));
    JpegSettings s;
    s.quality = 70;
    const auto p = makeJpegPreview(flat, s);
    ASSERT_TRUE(p.ok());
    EXPECT_EQ(p.value().result.pixels, QSize(64, 48));
    EXPECT_EQ(p.value().result.fileSize, encodeJpeg(flat, s).value().size());
    EXPECT_EQ(p.value().decoded.size(), flat.size());
    EXPECT_NE(p.value().decoded, flat);
    const auto onlySize = makeJpegPreview(flat, s, false);
    ASSERT_TRUE(onlySize.ok());
    EXPECT_TRUE(onlySize.value().decoded.isNull());
    EXPECT_EQ(onlySize.value().result.fileSize, p.value().result.fileSize);
    EXPECT_FALSE(makeJpegPreview(QImage(), s).ok());
}
