#include "IoTestSupport.h"

#include "core/document/Document.h"
#include "core/io/ByteIO.h"
#include "core/io/FileIo.h"
#include "core/io/ImageConvert.h"
#include "core/io/ImageFormat.h"

#include <QBuffer>
#include <QImageReader>
#include <QImageWriter>
#include <QTemporaryDir>
#include <algorithm>

#include <gtest/gtest.h>

using namespace doodlebold;
using namespace doodlebold::testing;

namespace {

const DocumentFormat& format(const char* id) {
    const DocumentFormat* f = FormatRegistry::instance().byId(QString::fromLatin1(id));
    EXPECT_NE(f, nullptr) << id;
    return *f;
}

QByteArray encode(const QImage& img, const char* fmt, int quality = -1) {
    QBuffer b;
    b.open(QIODevice::WriteOnly);
    QImageWriter w(&b, fmt);
    if (quality >= 0)
        w.setQuality(quality);
    EXPECT_TRUE(w.write(img));
    return b.data();
}

FileResult<LoadedDocument> importBytes(const char* id, const QByteArray& bytes) {
    QBuffer b;
    b.setData(bytes);
    b.open(QIODevice::ReadOnly);
    return format(id).read(b);
}

DocumentContent transparentPaper(DocumentContent c) {
    c.state.background.mode = BackgroundMode::TransparencyWhite;
    return c;
}

QByteArray exportBytes(const char* id, const DocumentContent& c, const ExportOptions& o = {}) {
    QBuffer b;
    b.open(QIODevice::WriteOnly);
    const auto r = format(id).write(c, b, o);
    EXPECT_TRUE(r.ok()) << (r.ok() ? "" : qPrintable(r.error().describe()));
    return b.data();
}

QImage decode(const QByteArray& bytes, const char* hint = "") {
    QBuffer b;
    b.setData(bytes);
    b.open(QIODevice::ReadOnly);
    return QImageReader(&b, hint).read();
}

QImage levelsPicture() {
    QImage img(256, 3, QImage::Format_RGBA8888);
    for (int x = 0; x < 256; ++x) {
        img.setPixelColor(x, 0, QColor(x, 0, 0));
        img.setPixelColor(x, 1, QColor(0, x, 255 - x));
        img.setPixelColor(x, 2, QColor(x, x, x));
    }
    return img;
}

const Channel* firstLayerPixel(const DocumentContent& c, int x, int y, Channel* out) {
    const auto p = c.state.layers.root().children[0].pixels.pixel(x, y);
    std::ranges::copy(p, out);
    return out;
}

}

TEST(ImageFormats, RegistryKnowsTheBuiltIns) {
    const FormatRegistry& r = FormatRegistry::instance();
    for (const char* ext : {"x.PNG", "x.jpg", "x.JPEG", "x.bmp", "x.tga", "x.doodle"}) {
        ASSERT_NE(r.forPath(QString::fromLatin1(ext), false), nullptr) << ext;
        EXPECT_NE(r.forPath(QString::fromLatin1(ext), true), nullptr) << ext;
    }
    EXPECT_EQ(r.forPath(QStringLiteral("noextension"), false), nullptr);
    EXPECT_EQ(r.forPath(QStringLiteral("a.unknownext"), false), nullptr);
    EXPECT_TRUE(r.openFilter().startsWith(QStringLiteral("All supported files (")));
    EXPECT_TRUE(r.openFilter().contains(QStringLiteral("*.png")));
    EXPECT_TRUE(r.openFilter().contains(QStringLiteral("*.doodle")));
    if (QImageReader::supportedImageFormats().contains("tiff")) {
        EXPECT_TRUE(r.readableExtensions().contains(QStringLiteral("tiff")));
    }
    EXPECT_FALSE(format("png").extensions().isEmpty());
    EXPECT_TRUE(format("png").features().alpha);
    EXPECT_EQ(FormatRegistry::instance().forPath(QStringLiteral("a.webp"), true), nullptr);
    if (QImageReader::supportedImageFormats().contains("webp")) {
        EXPECT_NE(FormatRegistry::instance().forPath(QStringLiteral("a.webp"), false), nullptr);
    }
    EXPECT_TRUE(format("jpeg").features().quality);
    EXPECT_FALSE(format("jpeg").features().alpha);
}

TEST(ImageFormats, EightBitPngImportsAtTheStoredLevels) {
    const auto doc = importBytes("png", encode(levelsPicture(), "png"));
    ASSERT_TRUE(doc.ok()) << qPrintable(doc.error().describe());
    const DocumentContent& c = doc.value().content;
    ASSERT_EQ(c.size(), QSize(256, 3));
    ASSERT_EQ(c.state.layers.layerCount(), 1);
    for (int v : {0, 1, 100, 201, 254, 255}) {
        Channel p[4];
        firstLayerPixel(c, v, 0, p);
        EXPECT_EQ(p[0], fromUnorm8(v));
        EXPECT_EQ(p[3], kChannelOne);
    }
}

TEST(ImageFormats, OpaqueEightBitPngRoundTripsExactly) {
    const QImage original = levelsPicture();
    const auto doc = importBytes("png", encode(original, "png"));
    ASSERT_TRUE(doc.ok());
    const QImage back = decode(exportBytes("png", doc.value().content)).convertToFormat(QImage::Format_RGBA8888);
    EXPECT_EQ(back.size(), original.size());
    EXPECT_TRUE(back == original);
}

TEST(ImageFormats, TransparentEightBitPngKeepsAlphaAndColour) {
    QImage img(32, 8, QImage::Format_RGBA8888);
    for (int x = 0; x < 32; ++x)
        for (int y = 0; y < 8; ++y)
            img.setPixelColor(x, y, QColor(200, 100, 50, x * 8 + y));
    const auto doc = importBytes("png", encode(img, "png"));
    ASSERT_TRUE(doc.ok());
    const QImage back = decode(exportBytes("png", transparentPaper(doc.value().content))).convertToFormat(QImage::Format_RGBA8888);
    ASSERT_EQ(back.size(), img.size());
    for (int x = 0; x < 32; ++x)
        for (int y = 0; y < 8; ++y) {
            const QColor a = img.pixelColor(x, y), b = back.pixelColor(x, y);
            EXPECT_EQ(a.alpha(), b.alpha());
            if (a.alpha() >= 64) {
                EXPECT_NEAR(a.red(), b.red(), 1);
                EXPECT_NEAR(a.green(), b.green(), 1);
                EXPECT_NEAR(a.blue(), b.blue(), 1);
            }
        }
    const QImage flat = decode(exportBytes("png", doc.value().content));
    EXPECT_FALSE(flat.hasAlphaChannel());
    const QColor half = flat.pixelColor(16, 0);
    EXPECT_NEAR(half.red(), 255 - (255 - 200) * 128 / 255, 2);
}

TEST(ImageFormats, SixteenBitPngImportKeepsTheLayerPrecision) {
    QImage img(64, 2, QImage::Format_RGBA64);
    for (int x = 0; x < 64; ++x) {
        auto* p = reinterpret_cast<std::uint16_t*>(img.scanLine(0)) + x * 4;
        p[0] = static_cast<std::uint16_t>(x * 1000);
        p[1] = static_cast<std::uint16_t>(65535 - x * 1000);
        p[2] = static_cast<std::uint16_t>(x * 17 + 3);
        p[3] = 65535;
        auto* q = reinterpret_cast<std::uint16_t*>(img.scanLine(1)) + x * 4;
        q[0] = q[1] = q[2] = static_cast<std::uint16_t>(x * 257);
        q[3] = 65535;
    }
    const QByteArray png = encode(img, "png");
    const auto doc = importBytes("png", png);
    ASSERT_TRUE(doc.ok());
    const DocumentContent& c = doc.value().content;
    Channel p[4];
    firstLayerPixel(c, 10, 0, p);
    EXPECT_EQ(p[0], 10000);
    EXPECT_EQ(p[1], 55535);
    for (int x : {0, 1, 100 % 64, 63}) {
        firstLayerPixel(c, x, 1, p);
        EXPECT_EQ(p[0], fromUnorm8(x));
    }

    const QImage eight = decode(exportBytes("png", c)).convertToFormat(QImage::Format_RGBA8888);
    firstLayerPixel(c, 10, 0, p);
    EXPECT_EQ(qRed(eight.pixel(10, 0)), toUnorm8(p[0]));
}

TEST(ImageFormats, ExportOverPaperIsTheCanvasWithTheExportRounding) {
    Document doc(richContent());
    const QImage& shown = doc.projection();
    const QImage out = io::flattenedPicture(doc.content(), true);
    ASSERT_EQ(out.size(), shown.size());
    ASSERT_EQ(out.format(), QImage::Format_RGB888);
    for (int y = 0; y < out.height(); ++y)
        for (int x = 0; x < out.width(); ++x) {
            const std::uint8_t* a = shown.constScanLine(y) + x * 4;
            const std::uint8_t* b = out.constScanLine(y) + x * 3;
            for (int c = 0; c < 3; ++c)
                ASSERT_TRUE(b[c] == a[c] || b[c] + 1 == a[c]) << x << "," << y;
        }
}

TEST(ImageFormats, JpegWritesAViewablePictureWithAdjustableQuality) {
    Document doc(richContent());
    ExportOptions low, high;
    low.quality = 10;
    high.quality = 95;
    const QByteArray small = exportBytes("jpeg", doc.content(), low);
    const QByteArray big = exportBytes("jpeg", doc.content(), high);
    EXPECT_TRUE(small.startsWith(QByteArray("\xff\xd8", 2)));
    EXPECT_LT(small.size(), big.size());
    const QImage back = decode(big).convertToFormat(QImage::Format_RGB888);
    const QImage shown = io::flattenedPicture(doc.content(), false);
    ASSERT_EQ(back.size(), shown.size());
    long long diff = 0;
    for (int y = 0; y < back.height(); ++y)
        for (int x = 0; x < back.width() * 3; ++x)
            diff += std::abs(int(back.constScanLine(y)[x]) - int(shown.constScanLine(y)[x]));
    EXPECT_LT(double(diff) / (back.width() * back.height() * 3), 24.0);
}

TEST(ImageFormats, BmpIsExact) {
    const auto doc = importBytes("png", encode(levelsPicture(), "png"));
    for (const bool transparent : {false, true}) {
        const DocumentContent c = transparent ? transparentPaper(doc.value().content) : doc.value().content;
        const QByteArray bmp = exportBytes("bmp", c);
        ASSERT_TRUE(bmp.startsWith("BM"));
        EXPECT_TRUE(decode(bmp).convertToFormat(QImage::Format_RGBA8888) == levelsPicture()) << transparent;
    }
}

TEST(ImageFormats, TgaHasTheDocumentedLayout) {
    QImage img(5, 2, QImage::Format_RGBA8888);
    for (int x = 0; x < 5; ++x) {
        img.setPixelColor(x, 0, QColor(10 * x, 20, 30, 255));
        img.setPixelColor(x, 1, QColor(1, 2, 3 + x, 255));
    }
    const auto doc = importBytes("png", encode(img, "png"));
    const QByteArray tga24 = exportBytes("tga", doc.value().content);
    ASSERT_EQ(tga24.size(), 18 + 5 * 2 * 3);
    const auto* b = reinterpret_cast<const std::uint8_t*>(tga24.constData());
    EXPECT_EQ(b[2], 2);                 // uncompressed truecolour
    EXPECT_EQ(b[12] | (b[13] << 8), 5);  // width
    EXPECT_EQ(b[14] | (b[15] << 8), 2);  // height
    EXPECT_EQ(b[16], 24);
    EXPECT_EQ(b[17], 0);  // bottom-left origin: the rows run bottom to top
    EXPECT_EQ(b[18 + 5 * 3 + 3 * 2 + 0], 30);  // pixel (2,0) is in the last row: B, G, R
    EXPECT_EQ(b[18 + 5 * 3 + 3 * 2 + 1], 20);
    EXPECT_EQ(b[18 + 5 * 3 + 3 * 2 + 2], 20);
    EXPECT_EQ(b[18 + 0], 3);  // pixel (0,1) comes first: blue 3

    const QByteArray tga32 = exportBytes("tga", transparentPaper(doc.value().content));
    ASSERT_EQ(tga32.size(), 18 + 5 * 2 * 4);
    EXPECT_EQ(static_cast<std::uint8_t>(tga32[16]), 32);
    EXPECT_EQ(static_cast<std::uint8_t>(tga32[17]) & 0x0F, 8);  // alpha bits
    EXPECT_EQ(static_cast<std::uint8_t>(tga32[18 + 3]), 255);   // alpha of the first pixel
    EXPECT_EQ(static_cast<std::uint8_t>(tga32[17]), 8);          // bottom-left origin

    const auto reopened = importBytes("tga", tga24);
    ASSERT_TRUE(reopened.ok()) << qPrintable(reopened.error().describe());
    EXPECT_EQ(reopened.value().content.size(), QSize(5, 2));
    EXPECT_TRUE(io::flattenedPicture(reopened.value().content, false).convertToFormat(QImage::Format_RGBA8888) == img);
}

namespace {

DocumentContent halfPaintedContent(BackgroundMode mode, double ppi) {
    QImage img(16, 8, QImage::Format_RGBA8888);
    img.fill(Qt::transparent);
    for (int y = 0; y < 8; ++y)
        for (int x = 0; x < 8; ++x)
            img.setPixelColor(x, y, QColor(200, 40 + x, 20 + y));
    DocumentContent c = io::contentFromPicture(img);
    c.state.background.mode = mode;
    c.state.background.color = QColor(10, 90, 170);
    c.resolution = ppi;
    return c;
}

struct PngChunk {
    QByteArray type, data;
};

std::vector<PngChunk> pngChunks(const QByteArray& png) {
    std::vector<PngChunk> out;
    EXPECT_TRUE(png.startsWith("\x89PNG\r\n\x1a\n"));
    io::ByteReader r(png, io::ByteOrder::Big);
    r.skip(8);
    while (r.remaining() >= 12) {
        const std::uint32_t length = r.u32();
        PngChunk c;
        c.type = r.bytes(4);
        c.data = r.bytes(length);
        r.skip(4);
        out.push_back(c);
    }
    return out;
}

const PngChunk* findChunk(const std::vector<PngChunk>& chunks, const char* type) {
    for (const PngChunk& c : chunks)
        if (c.type == type)
            return &c;
    return nullptr;
}

}

TEST(ImageExports, OpaqueBackgroundsGiveEightBitRgbPngWithTheResolution) {
    for (const BackgroundMode mode : {BackgroundMode::White, BackgroundMode::Black, BackgroundMode::Color}) {
        const auto chunks = pngChunks(exportBytes("png", halfPaintedContent(mode, 300.0)));
        const PngChunk* ihdr = findChunk(chunks, "IHDR");
        ASSERT_TRUE(ihdr);
        EXPECT_EQ(std::uint8_t(ihdr->data[8]), 8);  // bit depth
        EXPECT_EQ(std::uint8_t(ihdr->data[9]), 2);  // colour type 2: RGB, no alpha
        const PngChunk* phys = findChunk(chunks, "pHYs");
        ASSERT_TRUE(phys);
        io::ByteReader r(phys->data, io::ByteOrder::Big);
        EXPECT_EQ(r.u32(), 11811u);  // 300 ppi in pixels per metre
        EXPECT_EQ(r.u32(), 11811u);
        EXPECT_EQ(r.u8(), 1);  // unit: the metre
    }
    EXPECT_EQ(decode(exportBytes("png", halfPaintedContent(BackgroundMode::Black, 72))).pixelColor(12, 3), QColor(Qt::black));
    EXPECT_EQ(decode(exportBytes("png", halfPaintedContent(BackgroundMode::Color, 72))).pixelColor(12, 3), QColor(10, 90, 170));
    const auto chunks = pngChunks(exportBytes("png", halfPaintedContent(BackgroundMode::White, 72.0)));
    io::ByteReader r(findChunk(chunks, "pHYs")->data, io::ByteOrder::Big);
    EXPECT_EQ(r.u32(), 2835u);
}

TEST(ImageExports, EveryTransparentModeGivesStraightRgbaPngWithZeroRgbWhereTransparent) {
    for (int i = int(BackgroundMode::TransparencyBrightChecker); i < kBackgroundModeCount; ++i) {
        const auto mode = static_cast<BackgroundMode>(i);
        const QByteArray png = exportBytes("png", halfPaintedContent(mode, 72.0));
        const auto chunks = pngChunks(png);
        EXPECT_EQ(std::uint8_t(findChunk(chunks, "IHDR")->data[9]), 6) << i;  // colour type 6: RGBA
        const QImage back = decode(png).convertToFormat(QImage::Format_RGBA8888);
        EXPECT_EQ(back.pixelColor(3, 2), QColor(200, 43, 22, 255));
        const QColor empty = back.pixelColor(12, 3);
        EXPECT_EQ(empty.alpha(), 0) << i;
        EXPECT_EQ(empty.red() + empty.green() + empty.blue(), 0) << i;
    }
}

TEST(ImageExports, SemiTransparentPixelsKeepStraightColour) {
    QImage img(4, 1, QImage::Format_RGBA8888);
    img.setPixelColor(0, 0, QColor(255, 0, 0, 128));
    img.setPixelColor(1, 0, QColor(0, 255, 0, 255));
    img.setPixelColor(2, 0, QColor(0, 0, 255, 200));
    img.setPixelColor(3, 0, QColor(0, 0, 0, 0));
    DocumentContent c = io::contentFromPicture(img);
    c.state.background.mode = BackgroundMode::TransparencyBrightChecker;
    const QImage back = decode(exportBytes("png", c)).convertToFormat(QImage::Format_RGBA8888);
    EXPECT_EQ(back.pixelColor(0, 0), QColor(255, 0, 0, 128));
    EXPECT_EQ(back.pixelColor(2, 0), QColor(0, 0, 255, 200));
}

TEST(ImageExports, BmpIs24BitWithTheInfoHeaderForOpaqueBackgrounds) {
    const QByteArray bmp = exportBytes("bmp", halfPaintedContent(BackgroundMode::White, 72.0));
    io::ByteReader r(bmp, io::ByteOrder::Little);
    EXPECT_EQ(r.bytes(2), "BM");
    EXPECT_EQ(r.u32(), std::uint32_t(bmp.size()));
    r.skip(4);
    EXPECT_EQ(r.u32(), 54u);   // pixel data offset: 14 + 40
    EXPECT_EQ(r.u32(), 40u);   // BITMAPINFOHEADER
    EXPECT_EQ(r.i32(), 16);    // width
    EXPECT_EQ(r.i32(), 8);     // height, positive: bottom-up rows
    EXPECT_EQ(r.u16(), 1);     // planes
    EXPECT_EQ(r.u16(), 24);    // bits per pixel
    EXPECT_EQ(r.u32(), 0u);    // BI_RGB
    EXPECT_EQ(r.u32(), 16u * 3 * 8);  // 48-byte rows (already a multiple of 4)
    EXPECT_EQ(r.u32(), 2835u);        // 72 ppi in pixels per metre
    EXPECT_EQ(r.u32(), 2835u);
    EXPECT_EQ(bmp.size(), 54 + 48 * 8);
}

TEST(ImageExports, BmpRowsArePaddedToFourBytes) {
    QImage img(5, 3, QImage::Format_RGBA8888);
    img.fill(QColor(1, 2, 3));
    const QByteArray bmp = exportBytes("bmp", io::contentFromPicture(img));
    EXPECT_EQ(bmp.size(), 54 + 16 * 3);  // 15 bytes of pixels, one of padding
    EXPECT_TRUE(decode(bmp).convertToFormat(QImage::Format_RGBA8888) == img);
}

TEST(ImageExports, BmpIs32BitV4WithMasksForTransparentBackgrounds) {
    DocumentContent c = halfPaintedContent(BackgroundMode::TransparencyDarkChecker, 300.0);
    const QByteArray bmp = exportBytes("bmp", c);
    io::ByteReader r(bmp, io::ByteOrder::Little);
    EXPECT_EQ(r.bytes(2), "BM");
    EXPECT_EQ(r.u32(), std::uint32_t(bmp.size()));
    r.skip(4);
    EXPECT_EQ(r.u32(), 14u + 108u);  // pixel data offset
    EXPECT_EQ(r.u32(), 108u);        // BITMAPV4HEADER
    EXPECT_EQ(r.i32(), 16);
    EXPECT_EQ(r.i32(), 8);
    EXPECT_EQ(r.u16(), 1);
    EXPECT_EQ(r.u16(), 32);
    EXPECT_EQ(r.u32(), 3u);          // BI_BITFIELDS
    EXPECT_EQ(r.u32(), 16u * 4 * 8);
    EXPECT_EQ(r.u32(), 11811u);      // 300 ppi
    EXPECT_EQ(r.u32(), 11811u);
    r.skip(8);                       // colours used / important
    EXPECT_EQ(r.u32(), 0x00FF0000u);  // red, green, blue, alpha masks: a BGRA pixel
    EXPECT_EQ(r.u32(), 0x0000FF00u);
    EXPECT_EQ(r.u32(), 0x000000FFu);
    EXPECT_EQ(r.u32(), 0xFF000000u);
    for (int i = 0; i < 13; ++i)      // colour space, endpoints and gamma: all zero
        EXPECT_EQ(r.u32(), 0u) << i;
    ASSERT_TRUE(r.ok());
    EXPECT_EQ(bmp.size(), 14 + 108 + 16 * 4 * 8);
    const auto* px = reinterpret_cast<const std::uint8_t*>(bmp.constData()) + 14 + 108;
    EXPECT_EQ(px[0], 27);
    EXPECT_EQ(px[1], 40);
    EXPECT_EQ(px[2], 200);
    EXPECT_EQ(px[3], 255);
    EXPECT_EQ(px[12 * 4 + 3], 0);
    EXPECT_EQ(px[12 * 4 + 0] + px[12 * 4 + 1] + px[12 * 4 + 2], 0);
    const QImage back = decode(bmp).convertToFormat(QImage::Format_RGBA8888);
    EXPECT_EQ(back.pixelColor(3, 2), QColor(200, 43, 22, 255));
    EXPECT_EQ(back.pixelColor(12, 3).alpha(), 0);
}

TEST(ImageExports, TgaIs24BitOpaqueAnd32BitWithAlpha) {
    const QByteArray opaque = exportBytes("tga", halfPaintedContent(BackgroundMode::Black, 72.0));
    EXPECT_EQ(opaque.size(), 18 + 16 * 8 * 3);
    EXPECT_EQ(std::uint8_t(opaque[2]), 2);
    EXPECT_EQ(std::uint8_t(opaque[16]), 24);
    EXPECT_EQ(std::uint8_t(opaque[17]), 0);
    const QByteArray transparent = exportBytes("tga", halfPaintedContent(BackgroundMode::TransparencyColor, 72.0));
    EXPECT_EQ(transparent.size(), 18 + 16 * 8 * 4);
    EXPECT_EQ(std::uint8_t(transparent[16]), 32);
    EXPECT_EQ(std::uint8_t(transparent[17]), 8);
    const auto reopened = importBytes("tga", transparent);
    ASSERT_TRUE(reopened.ok());
    const auto lit = reopened.value().content.state.layers.root().children[0].pixels.pixel(3, 2);
    EXPECT_EQ(toUnorm8(lit[0]), 200);
    EXPECT_EQ(lit[3], kChannelOne);
    EXPECT_EQ(reopened.value().content.state.layers.root().children[0].pixels.pixel(12, 3)[3], 0);
    const auto* px = reinterpret_cast<const std::uint8_t*>(transparent.constData()) + 18 + (7 * 16 + 12) * 4;
    EXPECT_EQ(px[0] + px[1] + px[2] + px[3], 0);
}

TEST(ImageExports, TgaReaderHandlesTheLayoutsOfTruecolourTga) {
    QByteArray tga;
    io::ByteWriter w(io::ByteOrder::Little);
    w.u8(0), w.u8(0), w.u8(10);
    w.zeros(5);
    w.u16(0), w.u16(0);
    w.u16(4), w.u16(2);
    w.u8(24), w.u8(0x20);
    w.u8(0x81), w.u8(0), w.u8(0), w.u8(255);
    w.u8(0x81), w.u8(0), w.u8(255), w.u8(0);
    w.u8(0x03);
    for (int i = 0; i < 4; ++i)
        w.u8(i * 10), w.u8(0), w.u8(0);
    const auto rle = importBytes("tga", w.data());
    ASSERT_TRUE(rle.ok()) << qPrintable(rle.error().describe());
    const QImage picture = io::flattenedPicture(rle.value().content, false);
    EXPECT_EQ(picture.pixelColor(1, 0), QColor(255, 0, 0));
    EXPECT_EQ(picture.pixelColor(2, 0), QColor(0, 255, 0));
    EXPECT_EQ(picture.pixelColor(3, 1), QColor(0, 0, 30));
    EXPECT_FALSE(importBytes("tga", w.data().left(w.data().size() - 3)).ok());
}

TEST(ImageExports, ResolutionConvertsBothWays) {
    EXPECT_EQ(io::dotsPerMeter(72.0), 2835);
    EXPECT_EQ(io::dotsPerMeter(300.0), 11811);
    EXPECT_EQ(io::dotsPerMeter(350.0), 13780);
    EXPECT_NEAR(io::pixelsPerInch(2835), 72.009, 0.001);
}

TEST(ImageFormats, BadPicturesAreErrorsNotCrashes) {
    EXPECT_FALSE(importBytes("png", QByteArray("definitely not a png")).ok());
    const QByteArray png = encode(levelsPicture(), "png");
    EXPECT_FALSE(importBytes("png", png.left(png.size() / 2)).ok());
    EXPECT_FALSE(importBytes("png", {}).ok());

    QImage wide(kMaxCanvasSide + 1, 1, QImage::Format_Grayscale8);
    wide.fill(7);
    const auto r = importBytes("png", encode(wide, "png"));
    ASSERT_FALSE(r.ok());
    EXPECT_EQ(r.error().kind, FileError::Kind::TooLarge);
    EXPECT_EQ(r.error().size, wide.size());

    QTemporaryDir dir;
    const auto missing = io::loadFile(dir.filePath(QStringLiteral("nothing.png")));
    ASSERT_FALSE(missing.ok());
    EXPECT_EQ(missing.error().kind, FileError::Kind::CannotOpen);
    QFile junk(dir.filePath(QStringLiteral("junk.dat")));
    ASSERT_TRUE(junk.open(QIODevice::WriteOnly));
    junk.write("plain text");
    junk.close();
    const auto unknown = io::loadFile(junk.fileName());
    ASSERT_FALSE(unknown.ok());
    EXPECT_EQ(unknown.error().kind, FileError::Kind::UnknownFormat);
}

// The baseline: canvases are at most 16384 × 16384. A flat picture beyond it says how large it is, and opens
// scaled down to fit when asked.
TEST(ImageFormats, APictureBeyondTheCanvasLimitSaysSoAndOpensScaledDown) {
    static_assert(kMaxCanvasSide == 16384);
    EXPECT_EQ(fittedToCanvasLimit(QSize(30000, 20000)), QSize(16384, 10923));
    EXPECT_EQ(fittedToCanvasLimit(QSize(20000, 40000)), QSize(8192, 16384));
    EXPECT_EQ(fittedToCanvasLimit(QSize(16384, 16384)), QSize(16384, 16384));
    EXPECT_EQ(fittedToCanvasLimit(QSize(100000, 1)), QSize(16384, 1));
    EXPECT_EQ(fittedToCanvasLimit(QSize(640, 480)), QSize(640, 480));

    QImage wide(2 * kMaxCanvasSide, 4, QImage::Format_RGB32);
    wide.fill(QColor(200, 40, 10));
    for (int y = 0; y < 2; ++y)
        for (int x = 0; x < wide.width(); ++x)
            wide.setPixelColor(x, y, QColor(10, 40, 200));
    QTemporaryDir dir;
    for (const char* id : {"png", "tga", "bmp", "jpeg"}) {
        SCOPED_TRACE(id);
        const QString path = dir.filePath(QStringLiteral("wide.") + QLatin1String(id));
        DocumentContent content = io::contentFromPicture(wide);
        ASSERT_TRUE(io::saveFile(content, path, format(id)).ok());
        const auto refused = io::loadFile(path);
        ASSERT_FALSE(refused.ok());
        EXPECT_EQ(refused.error().kind, FileError::Kind::TooLarge);
        EXPECT_TRUE(refused.error().scalable);
        EXPECT_EQ(refused.error().size, wide.size());
        EXPECT_TRUE(refused.error().describe().contains(QStringLiteral("32768 × 4 pixels; canvases are at most 16384 × 16384")))
            << qPrintable(refused.error().describe());

        const auto scaled = io::loadFileScaledToFit(path);
        ASSERT_TRUE(scaled.ok()) << qPrintable(scaled.error().describe());
        const DocumentContent& c = scaled.value().content;
        EXPECT_EQ(c.size(), QSize(kMaxCanvasSide, 2));
        Channel top[4], bottom[4];
        firstLayerPixel(c, 100, 0, top);
        firstLayerPixel(c, 100, 1, bottom);
        EXPECT_GT(int(top[2]), int(top[0])) << "the blue half stays on top";
        EXPECT_GT(int(bottom[0]), int(bottom[2])) << "the red half below";
    }
}

TEST(ImageFormats, AScaledReadOfAPictureWithinTheLimitKeepsItsSize) {
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("small.png"));
    ASSERT_TRUE(io::saveFile(io::contentFromPicture(levelsPicture()), path, format("png")).ok());
    const auto scaled = io::loadFileScaledToFit(path);
    ASSERT_TRUE(scaled.ok());
    EXPECT_EQ(scaled.value().content.size(), levelsPicture().size());
}

TEST(ImageFormats, ImportedPicturePlacedCentredOnAnotherCanvas) {
    const auto doc = importBytes("png", encode(levelsPicture(), "png"));
    const TiledImage layer = io::mergedLayer(doc.value().content);
    const TiledImage placed = io::placeOnCanvas(layer, QSize(300, 5), QPoint((300 - 256) / 2, 1));
    EXPECT_EQ(placed.size(), QSize(300, 5));
    EXPECT_EQ(placed.pixel(22 + 100, 1), layer.pixel(100, 0));
    EXPECT_EQ(placed.pixel(22 + 255, 3), layer.pixel(255, 2));
    EXPECT_EQ(placed.pixel(0, 0)[3], 0);
    EXPECT_EQ(placed.pixel(21, 1)[3], 0);
    const TiledImage cropped = io::placeOnCanvas(layer, QSize(100, 2), QPoint(-100, -1));
    EXPECT_EQ(cropped.pixel(0, 0), layer.pixel(100, 1));
    EXPECT_EQ(cropped.pixel(99, 1), layer.pixel(199, 2));
}

TEST(ImageFormats, MergedLayerOfALayeredDocumentIsTheTransparentComposite) {
    Document doc(richContent());
    const TiledImage merged = io::mergedLayer(doc.content());
    EXPECT_EQ(merged.size(), doc.size());
    EXPECT_FALSE(merged.isEmpty());
    DocumentContent single;
    single.state.layers = LayerStack(QSize(70, 70));
    LayerNode n = single.state.layers.makeRaster(QStringLiteral("L"));
    n.pixels = TiledImage(QSize(70, 70));
    scribble(n.pixels, QRect(0, 0, 70, 70), 9);
    single.state.layers.insert(n, kNoLayer, -1);
    expectSameImage(io::mergedLayer(single), n.pixels, QStringLiteral("lone layer"));
}
