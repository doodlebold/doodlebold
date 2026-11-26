#include "IoTestSupport.h"

#include "core/document/Document.h"
#include "core/io/ByteIO.h"
#include "core/io/FileIo.h"
#include "core/io/ImageConvert.h"
#include "core/io/PsdFormat.h"
#include "core/selection/SelectionOps.h"
#include "core/transform/Lift.h"
#include "core/transform/Resample.h"

#include <QBuffer>
#include <QFile>
#include <QDir>
#include <QImageReader>
#include <QTemporaryDir>

#include <functional>

#include <gtest/gtest.h>

using namespace doodlebold;
using namespace doodlebold::testing;

namespace {

QByteArray written(const DocumentContent& c) {
    QBuffer buf;
    buf.open(QIODevice::WriteOnly);
    const auto r = PsdFormat().write(c, buf, {});
    EXPECT_TRUE(r.ok()) << (r.ok() ? "" : qPrintable(r.error().describe()));
    if (const QByteArray dir = qgetenv("DOODLEBOLD_TEST_OUTPUT_DIR"); !dir.isEmpty()) {
        QFile f(QDir(QString::fromLocal8Bit(dir)).filePath(QStringLiteral("doodlebold-rich.psd")));
        if (f.open(QIODevice::WriteOnly))
            f.write(buf.data());
    }
    return buf.data();
}

FileResult<LoadedDocument> parse(const QByteArray& bytes) {
    QBuffer buf;
    buf.setData(bytes);
    buf.open(QIODevice::ReadOnly);
    return PsdFormat().read(buf);
}

QString fixture(const char* name) {
    return QDir(QStringLiteral(DOODLEBOLD_FIXTURE_DIR)).filePath(QString::fromLatin1(name));
}

TiledImage eightBitPixels(const QSize& size, const QRect& area, unsigned seed) {
    QImage img(size, QImage::Format_RGBA8888);
    img.fill(Qt::transparent);
    std::mt19937 rng(seed);
    for (int y = area.top(); y <= area.bottom(); ++y)
        for (int x = area.left(); x <= area.right(); ++x)
            img.setPixelColor(x, y, QColor(int(rng() % 256), int(rng() % 256), int(rng() % 256), 64 + int(rng() % 192)));
    return io::layerFromImage(img);
}

DocumentContent psdContent() {
    DocumentContent c;
    c.state.background = CanvasBackground::fromPaper(QColor(10, 20, 30));
    c.resolution = 144.0;
    LayerStack& s = c.state.layers;
    s = LayerStack(QSize(150, 100));
    unsigned seed = 1;
    const auto addRaster = [&](LayerId parent, const QString& name, BlendMode mode, int opacity, const QRect& area) {
        LayerNode n = s.makeRaster(name);
        n.props.mode = mode;
        n.props.opacity = opacity;
        n.pixels = area.isEmpty() ? TiledImage(s.size()) : eightBitPixels(s.size(), area, seed++);
        return s.insert(std::move(n), parent, -1);
    };
    addRaster(kNoLayer, QStringLiteral("Background"), BlendMode::Normal, 100, QRect(0, 0, 150, 100));
    for (int m = 1; m < kBlendModeCount; ++m)
        addRaster(kNoLayer, QStringLiteral("mode %1").arg(m), static_cast<BlendMode>(m), 100 - m, QRect(m, m, 20, 10));
    LayerNode folder = s.makeFolder(QStringLiteral("フォルダ é"));
    folder.props.mode = BlendMode::PassThrough;
    folder.props.opacity = 63;
    const LayerId outer = s.insert(std::move(folder), kNoLayer, -1);
    LayerNode inner = s.makeFolder(QStringLiteral("Inner"));
    inner.props.mode = BlendMode::Multiply;
    inner.props.expanded = false;
    inner.props.visible = false;
    const LayerId innerId = s.insert(std::move(inner), outer, -1);
    addRaster(innerId, QStringLiteral("deep"), BlendMode::Screen, 40, QRect(100, 60, 30, 30));
    const LayerId clipBase = addRaster(outer, QStringLiteral("base"), BlendMode::Normal, 90, QRect(10, 10, 50, 50));
    const LayerId clipped = addRaster(outer, QStringLiteral("clipped"), BlendMode::Multiply, 50, QRect(20, 20, 60, 60));
    s.find(clipped)->props.clipping = true;
    s.find(clipBase)->props.preserveOpacity = true;
    s.find(clipBase)->props.locked = true;
    addRaster(kNoLayer, QStringLiteral("empty"), BlendMode::Normal, 100, QRect());
    const LayerId hidden = addRaster(kNoLayer, QStringLiteral("hidden"), BlendMode::Overlay, 30, QRect(5, 5, 5, 5));
    s.find(hidden)->props.visible = false;
    c.state.current = s.root().children.back().props.id;
    return c;
}

struct HandmadeLayer {
    QString name;
    QRect rect;
    std::vector<QByteArray> rgba;  // planes R G B A, 8 or 16 bit big endian (w·h·bps bytes each)
    int compression = 0;         // 0 raw, 1 rle, 2 zip, 3 zip with prediction
    QByteArray key = "norm";
    int opacity = 255;
};

QByteArray encodePlane(const QByteArray& plane, int w, int h, int bps, int compression) {
    io::ByteWriter out(io::ByteOrder::Big);
    out.u16(static_cast<std::uint32_t>(compression));
    const int rowBytes = w * bps;
    if (compression == 0) {
        out.bytes(plane);
    } else if (compression == 1) {
        io::ByteWriter rows(io::ByteOrder::Big);
        for (int y = 0; y < h; ++y) {
            const QByteArray row = psd::packBitsRow(reinterpret_cast<const std::uint8_t*>(plane.constData()) + y * rowBytes, rowBytes);
            out.u16(static_cast<std::uint32_t>(row.size()));
            rows.bytes(row);
        }
        out.bytes(rows.data());
    } else {
        QByteArray data = plane;
        if (compression == 3) {
            auto* p = reinterpret_cast<std::uint8_t*>(data.data());
            for (int y = 0; y < h; ++y) {
                std::uint8_t* row = p + y * rowBytes;
                if (bps == 1) {
                    for (int x = w - 1; x > 0; --x)
                        row[x] = static_cast<std::uint8_t>(row[x] - row[x - 1]);
                } else {
                    for (int x = w - 1; x > 0; --x) {
                        const int cur = (row[2 * x] << 8) | row[2 * x + 1], prev = (row[2 * x - 2] << 8) | row[2 * x - 1];
                        const int d = (cur - prev) & 0xFFFF;
                        row[2 * x] = static_cast<std::uint8_t>(d >> 8);
                        row[2 * x + 1] = static_cast<std::uint8_t>(d);
                    }
                }
            }
        }
        out.bytes(qCompress(data).mid(4));  // qCompress prefixes the size; PSD does not
    }
    return out.take();
}

QByteArray handmadePsd(const QSize& canvas, int bps, const std::vector<HandmadeLayer>& layers, bool lr16 = false) {
    io::ByteWriter info(io::ByteOrder::Big);
    info.i16(static_cast<int>(layers.size()));
    std::vector<std::array<QByteArray, 4>> channels;
    for (const HandmadeLayer& l : layers) {
        std::array<QByteArray, 4> ch;
        for (int i = 0; i < 4; ++i)
            ch[static_cast<std::size_t>(i)] =
                encodePlane(l.rgba[static_cast<std::size_t>(i)], l.rect.width(), l.rect.height(), bps, l.compression);
        channels.push_back(ch);
        info.i32(l.rect.top());
        info.i32(l.rect.left());
        info.i32(l.rect.bottom() + 1);
        info.i32(l.rect.right() + 1);
        info.u16(4);
        const int ids[4] = {0, 1, 2, -1};
        for (int i = 0; i < 4; ++i) {
            info.i16(ids[i]);
            info.u32(static_cast<std::uint32_t>(ch[static_cast<std::size_t>(i)].size()));
        }
        info.bytes("8BIM", 4);
        info.bytes(l.key);
        info.u8(static_cast<std::uint32_t>(l.opacity));
        info.u8(0);
        info.u8(8);
        info.u8(0);
        io::ByteWriter extra(io::ByteOrder::Big);
        extra.u32(0);
        extra.u32(0);
        const QByteArray name = l.name.toLatin1();
        extra.u8(static_cast<std::uint32_t>(name.size()));
        extra.bytes(name);
        extra.padTo(4);
        info.u32(static_cast<std::uint32_t>(extra.size()));
        info.bytes(extra.data());
    }
    for (const auto& ch : channels)
        for (const QByteArray& c : ch)
            info.bytes(c);
    info.padTo(2);

    io::ByteWriter out(io::ByteOrder::Big);
    out.bytes("8BPS", 4);
    out.u16(1);
    out.zeros(6);
    out.u16(4);
    out.u32(static_cast<std::uint32_t>(canvas.height()));
    out.u32(static_cast<std::uint32_t>(canvas.width()));
    out.u16(static_cast<std::uint32_t>(bps * 8));
    out.u16(3);
    out.u32(0);
    out.u32(0);
    if (!lr16) {
        out.u32(static_cast<std::uint32_t>(info.size() + 8));
        out.u32(static_cast<std::uint32_t>(info.size()));
        out.bytes(info.data());
        out.u32(0);
    } else {  // 16-bit documents of Photoshop keep the layers in an 'Lr16' block after the (empty) layer info
        io::ByteWriter block(io::ByteOrder::Big);
        block.bytes("8BIM", 4);
        block.bytes("Lr16", 4);
        block.u32(static_cast<std::uint32_t>(info.size()));
        block.bytes(info.data());
        out.u32(static_cast<std::uint32_t>(4 + 4 + block.size()));
        out.u32(0);
        out.u32(0);
        out.bytes(block.data());
    }
    out.u16(0);
    return out.take();
}

QByteArray plane8(int w, int h, const std::function<int(int, int)>& f) {
    QByteArray p(w * h, '\0');
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
            p[y * w + x] = static_cast<char>(f(x, y));
    return p;
}

QByteArray plane16(int w, int h, const std::function<int(int, int)>& f) {
    QByteArray p(w * h * 2, '\0');
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            const int v = f(x, y);
            p[(y * w + x) * 2] = static_cast<char>(v >> 8);
            p[(y * w + x) * 2 + 1] = static_cast<char>(v);
        }
    return p;
}

}

TEST(PackBits, MatchesAdobesExample) {
    const std::uint8_t packed[] = {0xFE, 0xAA, 0x02, 0x80, 0x00, 0x2A, 0xFD, 0xAA, 0x03, 0x80, 0x00, 0x2A, 0x22, 0xF7, 0xAA};
    const std::uint8_t expected[] = {0xAA, 0xAA, 0xAA, 0x80, 0x00, 0x2A, 0xAA, 0xAA, 0xAA, 0xAA, 0x80, 0x00,
                                     0x2A, 0x22, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA};
    std::uint8_t out[24];
    ASSERT_TRUE(psd::unpackBitsRow(packed, sizeof packed, out, 24));
    EXPECT_EQ(std::vector<std::uint8_t>(out, out + 24), std::vector<std::uint8_t>(expected, expected + 24));
    const QByteArray ours = psd::packBitsRow(expected, 24);
    EXPECT_LE(ours.size(), 15);
}

TEST(PackBits, RoundTripsAnyRow) {
    std::mt19937 rng(7);
    for (int trial = 0; trial < 300; ++trial) {
        const int n = 1 + int(rng() % 600);
        std::vector<std::uint8_t> row(static_cast<std::size_t>(n));
        const int style = trial % 4;
        for (int i = 0; i < n; ++i)
            row[static_cast<std::size_t>(i)] = style == 0   ? std::uint8_t(rng())
                                               : style == 1 ? std::uint8_t(i / 200)
                                               : style == 2 ? std::uint8_t((rng() % 4 == 0) ? rng() : (i > 0 ? row[std::size_t(i - 1)] : 0))
                                                            : std::uint8_t(i % 2);
        const QByteArray packed = psd::packBitsRow(row.data(), n);
        EXPECT_LE(packed.size(), n + n / 128 + 1);
        std::vector<std::uint8_t> back(static_cast<std::size_t>(n));
        ASSERT_TRUE(psd::unpackBitsRow(reinterpret_cast<const std::uint8_t*>(packed.constData()), static_cast<std::size_t>(packed.size()),
                                       back.data(), n));
        ASSERT_EQ(back, row);
    }
}

TEST(PackBits, RefusesBrokenRows) {
    std::uint8_t out[10];
    const std::uint8_t tooLong[] = {0xF0, 0x11};
    EXPECT_FALSE(psd::unpackBitsRow(tooLong, 2, out, 10));
    const std::uint8_t short_[] = {0x05, 1, 2};
    EXPECT_FALSE(psd::unpackBitsRow(short_, 3, out, 6));
    const std::uint8_t tooShort[] = {0x01, 1, 2};
    EXPECT_FALSE(psd::unpackBitsRow(tooShort, 3, out, 10));
    const std::uint8_t leftover[] = {0x00, 1, 0x00, 2};
    EXPECT_FALSE(psd::unpackBitsRow(leftover, 4, out, 1));
}

TEST(PsdFormat, FileLayoutIsPhotoshops) {
    const DocumentContent c = psdContent();
    const QByteArray file = written(c);
    io::ByteReader r(file, io::ByteOrder::Big);
    EXPECT_EQ(r.bytes(4), "8BPS");
    EXPECT_EQ(r.u16(), 1);
    r.skip(6);
    EXPECT_EQ(r.u16(), 3);
    EXPECT_EQ(r.u32(), 100u);            // height
    EXPECT_EQ(r.u32(), 150u);            // width
    EXPECT_EQ(r.u16(), 8);               // depth
    EXPECT_EQ(r.u16(), 3);               // RGB
    EXPECT_EQ(r.u32(), 0u);              // no colour mode data
    r.skip(r.u32());                     // image resources
    const std::uint32_t layerSection = r.u32();
    ASSERT_TRUE(r.ok());
    const std::size_t after = r.pos() + layerSection;
    const std::uint32_t infoLength = r.u32();
    EXPECT_EQ(infoLength % 2, 0u);
    const int count = r.i16();
    int rasters = 0, folders = 0;
    c.state.layers.forEach([&](const LayerNode& n) { (n.isFolder() ? folders : rasters)++; });
    EXPECT_EQ(count, rasters + 2 * folders);
    r.seek(after);
    EXPECT_EQ(r.u16(), 1);
    const QImage shown = io::flattenedPicture(c, false);
    QImage merged(150, 100, QImage::Format_RGB888);
    std::vector<std::uint32_t> counts(3 * 100);
    for (auto& n : counts)
        n = r.u16();
    for (int ch = 0; ch < 3; ++ch)
        for (int y = 0; y < 100; ++y) {
            std::uint8_t row[150];
            const std::uint32_t n = counts[std::size_t(ch * 100 + y)];
            ASSERT_TRUE(psd::unpackBitsRow(r.span(n), n, row, 150));
            for (int x = 0; x < 150; ++x)
                merged.scanLine(y)[x * 3 + ch] = row[x];
        }
    EXPECT_TRUE(r.atEnd());
    EXPECT_TRUE(merged == shown);
}

TEST(PsdFormat, DoodleboldRoundTripKeepsEverythingPsdCanHold) {
    const DocumentContent original = psdContent();
    const auto loaded = parse(written(original));
    ASSERT_TRUE(loaded.ok()) << qPrintable(loaded.error().describe());
    EXPECT_TRUE(loaded.value().notes.isEmpty()) << qPrintable(loaded.value().notes.join(QLatin1Char('\n')));
    const DocumentContent& back = loaded.value().content;
    ASSERT_EQ(back.size(), original.size());
    EXPECT_EQ(back.state.background, original.state.background);
    EXPECT_DOUBLE_EQ(back.resolution, original.resolution);
    ASSERT_EQ(back.state.layers.root().children.size(), original.state.layers.root().children.size());
    for (std::size_t i = 0; i < original.state.layers.root().children.size(); ++i)
        expectSameNode(original.state.layers.root().children[i], back.state.layers.root().children[i], false);
    EXPECT_EQ(back.state.current, back.state.layers.displayOrder().front()->props.id);
}

TEST(PsdFormat, LayerMasksRoundTripAsPsdMasks) {
    DocumentContent c = psdContent();
    std::vector<LayerNode*> rasters, folders;
    std::function<void(LayerNode&)> walk = [&](LayerNode& n) {
        for (LayerNode& k : n.children) {
            if (k.isFolder()) {
                folders.push_back(&k);
                walk(k);
            } else if (!k.pixels.contentBounds(true).isEmpty()) {
                rasters.push_back(&k);
            }
        }
    };
    walk(c.state.layers.root());
    ASSERT_GE(rasters.size(), 2u);
    ASSERT_GE(folders.size(), 1u);
    SelectionMask soft(c.size());
    std::vector<std::uint16_t> row(std::size_t(c.size().width()));
    for (int y = 0; y < c.size().height(); ++y) {
        for (int x = 0; x < c.size().width(); ++x)
            row[std::size_t(x)] = x < 10 ? SelectionMask::kOne : std::uint16_t(fromUnorm8((x * 7 + y * 3) % 256));
        soft.writeRow(y, 0, c.size().width(), row.data());
    }
    soft.squeeze();
    rasters[0]->mask = soft;
    rasters[1]->mask = SelectionMask(c.size());
    rasters[1]->props.maskEnabled = false;
    folders[0]->mask = selection::all(c.size());
    const auto loaded = parse(written(c));
    ASSERT_TRUE(loaded.ok()) << qPrintable(loaded.error().describe());
    EXPECT_TRUE(loaded.value().notes.isEmpty()) << qPrintable(loaded.value().notes.join(QLatin1Char('\n')));
    const DocumentContent& back = loaded.value().content;
    for (std::size_t i = 0; i < c.state.layers.root().children.size(); ++i)
        expectSameNode(c.state.layers.root().children[i], back.state.layers.root().children[i], false);
}

TEST(PsdFormat, PsbRoundTrip) {
    DocumentContent c = psdContent();
    LayerNode* first = nullptr;
    c.state.layers.forEach([&](const LayerNode& n) {
        if (!first && !n.isFolder())
            first = c.state.layers.find(n.props.id);
    });
    ASSERT_TRUE(first);
    first->mask = selection::rectangle(c.size(), QRectF(2, 3, 10, 8), false);
    QByteArray bytes;
    {
        QBuffer buf(&bytes);
        buf.open(QIODevice::WriteOnly);
        ASSERT_TRUE(PsdFormat(true).write(c, buf, {}));
    }
    ASSERT_GE(bytes.size(), 6);
    EXPECT_EQ(bytes.left(4), QByteArray("8BPS"));
    EXPECT_EQ(int(bytes[4]), 0);
    EXPECT_EQ(int(bytes[5]), 2) << "version 2";
    EXPECT_NE(bytes, written(c)) << "not the same bytes as a PSD";
    for (const bool large : {false, true}) {
        QBuffer in(&bytes);
        in.open(QIODevice::ReadOnly);
        const auto loaded = PsdFormat(large).read(in);
        ASSERT_TRUE(loaded.ok()) << qPrintable(loaded.error().describe());
        const DocumentContent& back = loaded.value().content;
        ASSERT_EQ(back.size(), c.size());
        for (std::size_t i = 0; i < c.state.layers.root().children.size(); ++i)
            expectSameNode(c.state.layers.root().children[i], back.state.layers.root().children[i], false);
    }
    EXPECT_EQ(PsdFormat(true).extensions(), QStringList{QStringLiteral("psb")});
}

TEST(PsdFormat, EveryBlendModeComesBackAsItself) {
    for (int m = 0; m < kBlendModeCount; ++m) {
        DocumentContent c;
        c.state.layers = LayerStack(QSize(8, 8));
        const BlendMode mode = static_cast<BlendMode>(m);
        LayerNode n = mode == BlendMode::PassThrough ? c.state.layers.makeFolder(QStringLiteral("f"))
                                                     : c.state.layers.makeRaster(QStringLiteral("l"));
        n.props.mode = mode;
        n.props.opacity = 37;
        if (!n.isFolder())
            n.pixels = TiledImage(QSize(8, 8));
        c.state.layers.insert(std::move(n), kNoLayer, -1);
        const auto loaded = parse(written(c));
        ASSERT_TRUE(loaded.ok()) << m;
        const LayerProps& p = loaded.value().content.state.layers.root().children[0].props;
        EXPECT_EQ(p.mode, mode) << blendModeName(mode).toStdString();
        EXPECT_EQ(p.opacity, 37) << blendModeName(mode).toStdString();
    }
}

TEST(PsdFormat, WithoutOurBlockTheNearestModeIsChosen) {
    QByteArray file = written([] {
        DocumentContent c;
        c.state.layers = LayerStack(QSize(8, 8));
        LayerNode n = c.state.layers.makeRaster(QStringLiteral("l"));
        n.props.mode = BlendMode::LinearBurn;
        n.props.opacity = 60;
        n.pixels = TiledImage(QSize(8, 8));
        c.state.layers.insert(std::move(n), kNoLayer, -1);
        return c;
    }());
    const qsizetype block = file.indexOf("8BIMapmd");
    ASSERT_GE(block, 0);
    file.replace(block + 4, 4, "xxxx");
    auto loaded = parse(file);
    ASSERT_TRUE(loaded.ok());
    EXPECT_EQ(loaded.value().content.state.layers.root().children[0].props.mode, BlendMode::LinearBurn);
    EXPECT_EQ(loaded.value().content.state.layers.root().children[0].props.opacity, 60);
}

TEST(PsdFormat, PhotoshopLayerPropertiesAreRead) {
    const auto loaded = io::loadFile(fixture("imagemagick-8bit.psd"));
    ASSERT_TRUE(loaded.ok()) << qPrintable(loaded.error().describe());
    const DocumentContent& c = loaded.value().content;
    ASSERT_EQ(c.size(), QSize(40, 30));
    const auto& kids = c.state.layers.root().children;
    ASSERT_EQ(kids.size(), 2u);
    EXPECT_EQ(kids[0].props.name, QStringLiteral("Blue"));
    EXPECT_EQ(kids[1].props.name, QStringLiteral("Green"));
    EXPECT_EQ(kids[0].props.mode, BlendMode::Normal);
    EXPECT_EQ(kids[0].props.opacity, 100);
    // ImageMagick writes its blend key backwards ("mron"): an unknown key, so Normal and a note.
    ASSERT_FALSE(loaded.value().notes.isEmpty());
    EXPECT_TRUE(loaded.value().notes[0].contains(QStringLiteral("mron")));
    EXPECT_TRUE(kids[0].props.visible);
    const auto blue = kids[0].pixels.pixel(5, 7), outside = kids[0].pixels.pixel(4, 7), last = kids[0].pixels.pixel(24, 16);
    EXPECT_NEAR(toUnorm8(blue[3]), 128, 1);
    EXPECT_EQ(blue[0], 0);
    EXPECT_EQ(blue[1], 0);
    EXPECT_NEAR(blue[2], blue[3], 1);
    EXPECT_EQ(outside[3], 0);
    EXPECT_NE(last[3], 0);
    EXPECT_EQ(kids[0].pixels.pixel(25, 17)[3], 0);
    const auto green = kids[1].pixels.pixel(30, 20);
    EXPECT_EQ(green[1], kChannelOne);
    EXPECT_EQ(green[3], kChannelOne);
    EXPECT_EQ(kids[1].pixels.pixel(38, 28)[3], 0);
}

TEST(PsdFormat, SixteenBitFromAnotherProgram) {
    const auto loaded = io::loadFile(fixture("imagemagick-16bit.psd"));
    ASSERT_TRUE(loaded.ok()) << qPrintable(loaded.error().describe());
    const auto& kids = loaded.value().content.state.layers.root().children;
    ASSERT_EQ(kids.size(), 2u);
    EXPECT_EQ(kids[1].props.name, QStringLiteral("Green"));
    const auto green = kids[1].pixels.pixel(30, 20);
    EXPECT_EQ(green[1], kChannelOne);
    EXPECT_EQ(green[0], 0);
    const auto blue = kids[0].pixels.pixel(5, 7);
    EXPECT_EQ(blue[0], 0x6626);
    EXPECT_EQ(blue[1], 0xCD4C);
    EXPECT_EQ(blue[2], 0x3373);
    EXPECT_EQ(blue[3], kChannelOne);
}

TEST(PsdFormat, ReadsRawZipAndPredictedChannelsAndTheLr16Container) {
    const int w = 6, h = 3;
    for (int bps : {1, 2}) {
        for (bool lr16 : {false, true}) {
            if (lr16 && bps == 1)
                continue;
            for (int compression : {0, 1, 2, 3}) {
                const auto value = [bps](int v) { return bps == 1 ? v : v * 257; };
                HandmadeLayer l;
                l.name = QStringLiteral("L");
                l.rect = QRect(2, 1, w, h);
                l.compression = compression;
                const auto plane = [&](const std::function<int(int, int)>& f) { return bps == 1 ? plane8(w, h, f) : plane16(w, h, f); };
                l.rgba = {plane([&](int x, int y) { return value(10 * x + y); }), plane([&](int, int) { return value(200); }),
                          plane([&](int x, int) { return value(255 - 20 * x); }), plane([&](int, int) { return value(255); })};
                const auto loaded = parse(handmadePsd(QSize(10, 5), bps, {l}, lr16));
                ASSERT_TRUE(loaded.ok()) << "bps " << bps << " compression " << compression << ": "
                                         << qPrintable(loaded.error().describe());
                const auto& layer = loaded.value().content.state.layers.root().children[0];
                for (int y = 0; y < h; ++y)
                    for (int x = 0; x < w; ++x) {
                        const auto p = layer.pixels.pixel(2 + x, 1 + y);
                        ASSERT_EQ(p[0], fromUnorm8(10 * x + y)) << x << "," << y << " bps " << bps << " comp " << compression;
                        ASSERT_EQ(p[1], fromUnorm8(200));
                        ASSERT_EQ(p[2], fromUnorm8(255 - 20 * x));
                        ASSERT_EQ(p[3], kChannelOne);
                    }
            }
        }
    }
}

TEST(PsdFormat, LayerPartlyOutsideTheCanvasIsCropped) {
    HandmadeLayer l;
    l.name = QStringLiteral("L");
    l.rect = QRect(-2, -1, 6, 4);
    l.rgba = {plane8(6, 4, [](int x, int y) { return 10 * x + y; }), plane8(6, 4, [](int, int) { return 0; }),
              plane8(6, 4, [](int, int) { return 0; }), plane8(6, 4, [](int, int) { return 255; })};
    const auto loaded = parse(handmadePsd(QSize(5, 5), 1, {l}));
    ASSERT_TRUE(loaded.ok());
    const auto& px = loaded.value().content.state.layers.root().children[0].pixels;
    EXPECT_EQ(px.pixel(0, 0)[0], fromUnorm8(10 * 2 + 1));
    EXPECT_EQ(px.pixel(3, 2)[0], fromUnorm8(10 * 5 + 3));
    EXPECT_EQ(px.pixel(4, 0)[3], 0);
    EXPECT_EQ(px.pixel(0, 3)[3], 0);
}

TEST(PsdFormat, UnknownBlendModeBecomesNormalWithANote) {
    HandmadeLayer l;
    l.name = QStringLiteral("dissolved");
    l.rect = QRect(0, 0, 2, 2);
    l.key = "diss";
    l.rgba = {plane8(2, 2, [](int, int) { return 1; }), plane8(2, 2, [](int, int) { return 1; }), plane8(2, 2, [](int, int) { return 1; }),
              plane8(2, 2, [](int, int) { return 255; })};
    const auto loaded = parse(handmadePsd(QSize(4, 4), 1, {l}));
    ASSERT_TRUE(loaded.ok());
    EXPECT_EQ(loaded.value().content.state.layers.root().children[0].props.mode, BlendMode::Normal);
    ASSERT_EQ(loaded.value().notes.size(), 1);
    EXPECT_TRUE(loaded.value().notes[0].contains(QStringLiteral("diss")));
    EXPECT_TRUE(loaded.value().notes[0].contains(QStringLiteral("dissolved")));
}

TEST(PsdFormat, FlatDocumentBecomesOneLayer) {
    io::ByteWriter out(io::ByteOrder::Big);
    out.bytes("8BPS", 4);
    out.u16(1);
    out.zeros(6);
    out.u16(3);
    out.u32(2);
    out.u32(3);
    out.u16(8);
    out.u16(3);
    out.u32(0);
    out.u32(0);
    out.u32(0);
    out.u16(0);
    for (int ch = 0; ch < 3; ++ch)
        for (int i = 0; i < 6; ++i)
            out.u8(static_cast<std::uint32_t>(ch == 0 ? i * 10 : ch == 1 ? 100 : 200));
    const auto loaded = parse(out.data());
    ASSERT_TRUE(loaded.ok()) << qPrintable(loaded.error().describe());
    const DocumentContent& c = loaded.value().content;
    ASSERT_EQ(c.state.layers.layerCount(), 1);
    const auto p = c.state.layers.root().children[0].pixels.pixel(2, 1);
    EXPECT_EQ(p[0], fromUnorm8(50));
    EXPECT_EQ(p[1], fromUnorm8(100));
    EXPECT_EQ(p[3], kChannelOne);
}

TEST(PsdFormat, UnsupportedFilesAreNamed) {
    const auto header = [](int version, int channels, int depth, int mode) {
        io::ByteWriter out(io::ByteOrder::Big);
        out.bytes("8BPS", 4);
        out.u16(static_cast<std::uint32_t>(version));
        out.zeros(6);
        out.u16(static_cast<std::uint32_t>(channels));
        out.u32(4);
        out.u32(4);
        out.u16(static_cast<std::uint32_t>(depth));
        out.u16(static_cast<std::uint32_t>(mode));
        out.zeros(12);
        return out.take();
    };
    EXPECT_NE(parse(header(2, 3, 8, 3)).error().kind, FileError::Kind::Unsupported);
    EXPECT_EQ(parse(header(1, 4, 8, 4)).error().kind, FileError::Kind::Unsupported);
    EXPECT_EQ(parse(header(1, 3, 32, 3)).error().kind, FileError::Kind::Unsupported);
    EXPECT_EQ(parse(header(1, 1, 8, 1)).error().kind, FileError::Kind::Unsupported);
    EXPECT_EQ(parse(QByteArray("GIF89a not a psd")).error().kind, FileError::Kind::UnknownFormat);
    EXPECT_FALSE(parse({}).ok());
    io::ByteWriter big(io::ByteOrder::Big);
    big.bytes("8BPS", 4);
    big.u16(1);
    big.zeros(6);
    big.u16(3);
    big.u32(kMaxCanvasSide + 1);
    big.u32(kMaxCanvasSide + 1);
    big.u16(8);
    big.u16(3);
    big.zeros(12);
    const FileError tooLarge = parse(big.data()).error();
    EXPECT_EQ(tooLarge.kind, FileError::Kind::TooLarge);
    EXPECT_EQ(tooLarge.size, QSize(kMaxCanvasSide + 1, kMaxCanvasSide + 1));
    EXPECT_FALSE(tooLarge.scalable) << "a layered file isn't scaled down";
}

TEST(PsdFormat, FoldersNestedDeeperThanTenLevelsAreRefused) {
    const auto nested = [](int levels) {
        DocumentContent c;
        LayerStack& s = c.state.layers;
        s = LayerStack(QSize(8, 8));
        LayerId parent = kNoLayer;
        for (int i = 0; i < levels; ++i)
            parent = s.insert(s.makeFolder(QStringLiteral("f")), parent, -1);
        LayerNode n = s.makeRaster(QStringLiteral("inside"));
        n.pixels = eightBitPixels(s.size(), QRect(0, 0, 8, 8), 3);
        c.state.current = s.insert(std::move(n), parent, -1);
        return c;
    };
    EXPECT_TRUE(parse(written(nested(10))).ok());
    const auto deeper = parse(written(nested(11)));
    ASSERT_FALSE(deeper.ok());
    EXPECT_EQ(deeper.error().kind, FileError::Kind::Corrupt);
}

TEST(PsdFormat, DamagedFilesNeverCrash) {
    const QByteArray file = written(psdContent());
    int errors = 0;
    for (qsizetype n = 0; n < file.size(); n += (n < 600 ? 1 : 211)) {
        const auto r = parse(file.left(n));
        if (!r.ok()) {
            ++errors;
        } else {
            EXPECT_GT(r.value().content.state.layers.layerCount(), 0);
        }
    }
    EXPECT_GT(errors, 100);
    std::mt19937 rng(11);
    for (int i = 0; i < 1500; ++i) {
        QByteArray damaged = file;
        const int flips = 1 + int(rng() % 3);
        for (int f = 0; f < flips; ++f)
            damaged[qsizetype(rng() % std::uint32_t(damaged.size()))] = static_cast<char>(rng());
        const auto r = parse(damaged);
        if (r.ok()) {
            EXPECT_GT(r.value().content.state.layers.layerCount(), 0);
        }
    }
    for (int i = 0; i < 100; ++i) {
        QByteArray junk = file.left(26);
        for (int k = 0; k < 400; ++k)
            junk.append(static_cast<char>(rng()));
        EXPECT_NO_FATAL_FAILURE((void)parse(junk));
    }
}

TEST(PsdFormat, OtherProgramsReadOurFile) {
    if (!QImageReader::supportedImageFormats().contains("psd"))
        GTEST_SKIP() << "no PSD image plugin";
    const DocumentContent c = psdContent();
    const QByteArray file = written(c);
    QBuffer buf;
    buf.setData(file);
    buf.open(QIODevice::ReadOnly);
    QImageReader reader(&buf, "psd");
    const QImage theirs = reader.read().convertToFormat(QImage::Format_RGB888);
    ASSERT_FALSE(theirs.isNull()) << qPrintable(reader.errorString());
    EXPECT_TRUE(theirs == io::flattenedPicture(c, false));
}

namespace {

int channelCountOf(const QByteArray& file) {
    io::ByteReader r(file, io::ByteOrder::Big);
    r.skip(12);
    return r.u16();
}

QByteArray imageResource(const QByteArray& file, int wanted) {
    io::ByteReader r(file, io::ByteOrder::Big);
    r.skip(26);
    r.skip(r.u32());
    io::ByteReader resources = r.sub(r.u32());
    while (resources.remaining() >= 12) {
        EXPECT_EQ(resources.bytes(4), "8BIM");
        const int id = resources.u16();
        const int nameLength = resources.u8();
        resources.skip(std::size_t(nameLength + ((nameLength + 1) % 2)));
        const std::uint32_t length = resources.u32();
        const QByteArray data = resources.bytes(length);
        if (length % 2)
            resources.skip(1);
        if (id == wanted)
            return data;
    }
    return {};
}

}

TEST(PsdFormat, WritesPhotoshopsResolutionInfo) {
    DocumentContent c = psdContent();
    c.resolution = 300.0;
    const QByteArray info = imageResource(written(c), 0x03ED);
    ASSERT_EQ(info.size(), 16);
    io::ByteReader r(info, io::ByteOrder::Big);
    EXPECT_EQ(r.u32(), 300u << 16);  // horizontal resolution, 16.16 pixels per inch
    EXPECT_EQ(r.u16(), 1);
    EXPECT_EQ(r.u16(), 2);
    EXPECT_EQ(r.u32(), 300u << 16);
    EXPECT_EQ(r.u16(), 1);
    EXPECT_EQ(r.u16(), 2);
}

TEST(PsdFormat, ResolutionAndBackgroundRoundTripForEveryMode) {
    for (int i = 0; i < kBackgroundModeCount; ++i) {
        DocumentContent c = psdContent();
        c.state.background = {static_cast<BackgroundMode>(i), QColor(12, 34, 56)};
        c.resolution = 349.5;
        const auto back = parse(written(c));
        ASSERT_TRUE(back.ok()) << qPrintable(back.error().describe());
        EXPECT_EQ(back.value().content.state.background, c.state.background) << i;
        EXPECT_NEAR(back.value().content.resolution, 349.5, 1e-4) << i;
    }
}

TEST(PsdFormat, MergedImageHasThreeChannelsOrFourWhenTheBackgroundIsTransparent) {
    for (int i = 0; i < kBackgroundModeCount; ++i) {
        DocumentContent c = psdContent();
        c.state.background.mode = static_cast<BackgroundMode>(i);
        EXPECT_EQ(channelCountOf(written(c)), c.state.background.isTransparent() ? 4 : 3) << i;
    }
}

TEST(PsdFormat, ATransparentDocumentReadsBackLayeredAndTransparent) {
    DocumentContent c = psdContent();
    c.state.background.mode = BackgroundMode::TransparencyBlack;
    const QByteArray file = written(c);
    ASSERT_EQ(channelCountOf(file), 4);
    const auto back = parse(file);
    ASSERT_TRUE(back.ok());
    EXPECT_TRUE(back.value().content.background().isTransparent());
    EXPECT_EQ(back.value().content.state.layers.layerCount(), c.state.layers.layerCount());
}

TEST(PsdFormat, FilesOfOtherProgramsOpenOnAWhiteBackground) {
    const auto back = io::loadFile(fixture("imagemagick-8bit.psd"));
    ASSERT_TRUE(back.ok());
    EXPECT_EQ(back.value().content.background(), CanvasBackground{});
    EXPECT_GT(back.value().content.resolution, 0.0);
}

TEST(PsdFormat, ALayerReachingFarPastTheCanvasKeepsItsCanvasPart) {
    // px past the canvas: the layer's box is then wider than the PSD reader opens, so only the canvas's part can be written
    constexpr int kFar = 3'000'000;
    DocumentContent original = psdContent();
    LayerNode& background = original.state.layers.root().children[0];
    const TiledImage canvasPart = background.pixels;
    background.pixels = transform::composeOver(
        background.pixels, transform::moveExactly(background.pixels, QRect(0, 0, 30, 20), transform::Turn::None, QPoint(kFar, 0)));

    const auto loaded = parse(written(original));
    ASSERT_TRUE(loaded.ok()) << qPrintable(loaded.error().describe());
    expectSameImage(canvasPart, loaded.value().content.state.layers.root().children[0].pixels, QStringLiteral("background"));
}
