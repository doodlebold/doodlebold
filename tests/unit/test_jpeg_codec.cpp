#include "core/document/Document.h"
#include "core/io/FileIo.h"
#include "core/io/FormatRegistry.h"
#include "core/io/ImageConvert.h"
#include "core/io/JpegCodec.h"

#include <QBuffer>
#include <QImage>

#include <gtest/gtest.h>

#include <cmath>
#include <cstring>
#include <optional>
#include <random>

using namespace doodlebold;

namespace {

QImage testPicture(int w = 96, int h = 80) {
    QImage img(w, h, QImage::Format_RGB888);
    std::mt19937 rng(7);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            const int noise = static_cast<int>(rng() % 24);
            img.setPixelColor(x, y, QColor((x * 255 / w + noise) % 256, (y * 255 / h + noise) % 256, ((x ^ y) * 4 + noise) % 256));
        }
    return img;
}

struct Component {
    int id = 0, h = 0, v = 0, table = 0;
};

struct Markers {
    bool soi = false, baseline = false, progressive = false;
    int jfifMajor = -1, jfifMinor = -1, units = -1, xDensity = -1, yDensity = -1;
    int width = 0, height = 0, precision = 0;
    std::vector<Component> components;
    int quantTables = 0;
    int restartInterval = 0;
};

Markers parseMarkers(const QByteArray& data) {
    Markers m;
    const auto* b = reinterpret_cast<const unsigned char*>(data.constData());
    const int n = static_cast<int>(data.size());
    m.soi = n > 2 && b[0] == 0xFF && b[1] == 0xD8;
    int i = 2;
    while (i + 4 <= n && b[i] == 0xFF) {
        const int marker = b[i + 1];
        if (marker == 0xDA)
            break;
        const int length = (b[i + 2] << 8) | b[i + 3];
        const unsigned char* p = b + i + 4;
        if (marker == 0xE0 && length >= 16 && std::memcmp(p, "JFIF\0", 5) == 0) {
            m.jfifMajor = p[5], m.jfifMinor = p[6], m.units = p[7];
            m.xDensity = (p[8] << 8) | p[9];
            m.yDensity = (p[10] << 8) | p[11];
        } else if (marker == 0xC0 || marker == 0xC2) {
            m.baseline = marker == 0xC0;
            m.progressive = marker == 0xC2;
            m.precision = p[0];
            m.height = (p[1] << 8) | p[2];
            m.width = (p[3] << 8) | p[4];
            for (int c = 0; c < p[5]; ++c)
                m.components.push_back({p[6 + 3 * c], p[7 + 3 * c] >> 4, p[7 + 3 * c] & 15, p[8 + 3 * c]});
        } else if (marker == 0xDB) {
            ++m.quantTables;
        } else if (marker == 0xDD) {
            m.restartInterval = (p[0] << 8) | p[1];
        }
        i += 2 + length;
    }
    return m;
}

QByteArray encode(const QImage& img, const io::JpegSettings& s) {
    const auto r = io::encodeJpeg(img, s);
    EXPECT_TRUE(r.ok()) << (r.ok() ? "" : qPrintable(r.error().describe()));
    return r.ok() ? r.value() : QByteArray();
}

}

TEST(JpegCodec, SamplingFactorsFollowTheChromaCheckboxes) {
    const QImage img = testPicture();
    struct Case {
        bool horizontal, vertical;
        int lumaH, lumaV;
    };
    for (const Case c : {Case{false, false, 1, 1}, Case{true, false, 2, 1}, Case{false, true, 1, 2}, Case{true, true, 2, 2}}) {
        io::JpegSettings s;
        s.chromaHorizontal = c.horizontal;
        s.chromaVertical = c.vertical;
        EXPECT_EQ(io::samplingFor(s), (io::JpegSampling{c.lumaH, c.lumaV}));
        const Markers m = parseMarkers(encode(img, s));
        ASSERT_TRUE(m.soi);
        EXPECT_TRUE(m.baseline) << "baseline (SOF0), not progressive";
        EXPECT_FALSE(m.progressive);
        EXPECT_EQ(m.precision, 8);
        EXPECT_EQ(m.width, img.width());
        EXPECT_EQ(m.height, img.height());
        ASSERT_EQ(m.components.size(), 3u);
        EXPECT_EQ(m.components[0].h, c.lumaH) << c.horizontal << c.vertical;
        EXPECT_EQ(m.components[0].v, c.lumaV) << c.horizontal << c.vertical;
        for (int i = 1; i < 3; ++i) {
            EXPECT_EQ(m.components[static_cast<std::size_t>(i)].h, 1);
            EXPECT_EQ(m.components[static_cast<std::size_t>(i)].v, 1);
            EXPECT_EQ(m.components[static_cast<std::size_t>(i)].table, 1);
        }
        EXPECT_EQ(m.components[0].table, 0);
        EXPECT_EQ(m.quantTables, 2);
    }
}

TEST(JpegCodec, WritesJfif102WithTheResolutionAsDotsPerInch) {
    const QImage img = testPicture();
    for (const double ppi : {72.0, 300.0, 349.6, 1.0}) {
        io::JpegSettings s;
        s.resolution = ppi;
        const Markers m = parseMarkers(encode(img, s));
        EXPECT_EQ(m.jfifMajor, 1);
        EXPECT_EQ(m.jfifMinor, 2);
        EXPECT_EQ(m.units, 1);
        EXPECT_EQ(m.xDensity, std::lround(ppi));
        EXPECT_EQ(m.yDensity, std::lround(ppi));
    }
}

TEST(JpegCodec, SizeGrowsWithQualityAndEveryQualityDecodes) {
    const QImage img = testPicture();
    qint64 previous = 0;
    for (const int q : {1, 5, 10, 25, 50, 75, 90, 95, 100}) {
        io::JpegSettings s;
        s.quality = q;
        const QByteArray data = encode(img, s);
        EXPECT_GT(data.size(), previous) << "quality " << q;
        previous = data.size();
        const QImage back = QImage::fromData(data, "JPEG");
        ASSERT_FALSE(back.isNull()) << q;
        EXPECT_EQ(back.size(), img.size());
    }
    io::JpegSettings low, high;
    low.quality = -5;
    high.quality = 500;
    EXPECT_EQ(encode(img, low), encode(img, io::JpegSettings{.quality = 1}));
    EXPECT_EQ(encode(img, high), encode(img, io::JpegSettings{.quality = 100}));
}

TEST(JpegCodec, ChromaDownsamplingMakesSmallerFilesAndQualityHundredIsNearLossless) {
    const QImage img = testPicture();
    io::JpegSettings none, h, v, hv;
    h.chromaHorizontal = true;
    v.chromaVertical = true;
    hv.chromaHorizontal = hv.chromaVertical = true;
    const qint64 sNone = encode(img, none).size(), sH = encode(img, h).size(), sV = encode(img, v).size(), sHV = encode(img, hv).size();
    EXPECT_LT(sH, sNone);
    EXPECT_LT(sV, sNone);
    EXPECT_LT(sHV, sH);
    EXPECT_LT(sHV, sV);

    io::JpegSettings best;
    best.quality = 100;
    const QImage back = QImage::fromData(encode(img, best), "JPEG").convertToFormat(QImage::Format_RGB888);
    long long diff = 0;
    for (int y = 0; y < img.height(); ++y)
        for (int x = 0; x < img.width() * 3; ++x)
            diff += std::abs(int(img.constScanLine(y)[x]) - int(back.constScanLine(y)[x]));
    EXPECT_LT(double(diff) / (img.width() * img.height() * 3), 2.0);
}

TEST(JpegCodec, TheJpegFormatWritesTheDocumentsResolutionAndOptions) {
    Document doc(QSize(64, 48), CanvasBackground{BackgroundMode::Color, QColor(40, 90, 200)}, 300.0);
    const DocumentFormat* jpeg = FormatRegistry::instance().byId(QStringLiteral("jpeg"));
    ASSERT_NE(jpeg, nullptr);
    ExportOptions options;
    options.quality = 60;
    options.chromaHorizontal = true;
    QBuffer buffer;
    buffer.open(QIODevice::WriteOnly);
    ASSERT_TRUE(jpeg->write(doc.content(), buffer, options).ok());
    const Markers m = parseMarkers(buffer.data());
    EXPECT_EQ(m.xDensity, 300);
    EXPECT_EQ(m.units, 1);
    ASSERT_EQ(m.components.size(), 3u);
    EXPECT_EQ(m.components[0].h, 2);
    EXPECT_EQ(m.components[0].v, 1);
    const QImage back = QImage::fromData(buffer.data(), "JPEG");
    ASSERT_FALSE(back.isNull());
    EXPECT_NEAR(back.pixelColor(10, 10).blue(), 200, 6);
}

TEST(JpegCodec, ATransparentCanvasIsFlattenedOverWhite) {
    Document doc(QSize(32, 32), CanvasBackground{BackgroundMode::TransparencyBlack}, 72.0);
    const DocumentFormat* jpeg = FormatRegistry::instance().byId(QStringLiteral("jpeg"));
    QBuffer buffer;
    buffer.open(QIODevice::WriteOnly);
    ASSERT_TRUE(jpeg->write(doc.content(), buffer, {}).ok());
    const QImage back = QImage::fromData(buffer.data(), "JPEG");
    EXPECT_GE(back.pixelColor(5, 5).red(), 250);
}

TEST(JpegCodec, EmptyPicturesAreRefusedWithATypedError) {
    const auto r = io::encodeJpeg(QImage(), {});
    ASSERT_FALSE(r.ok());
    EXPECT_EQ(r.error().kind, FileError::Kind::TooLarge);
}
