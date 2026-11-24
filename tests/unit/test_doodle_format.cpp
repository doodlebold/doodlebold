#include "IoTestSupport.h"

#include "core/document/Document.h"
#include "core/io/DoodleFormat.h"
#include "core/io/FileIo.h"
#include "core/io/ByteIO.h"

#include <QBuffer>
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

#include <functional>

#include <gtest/gtest.h>

using namespace doodlebold;
using namespace doodlebold::testing;

namespace {

QByteArray saved(const DocumentContent& c) {
    QBuffer buf;
    buf.open(QIODevice::WriteOnly);
    const auto r = DoodleFormat().write(c, buf, {});
    EXPECT_TRUE(r.ok());
    return buf.data();
}

FileResult<LoadedDocument> parse(const QByteArray& bytes) {
    QBuffer buf;
    buf.setData(bytes);
    buf.open(QIODevice::ReadOnly);
    return DoodleFormat().read(buf);
}

struct Chunk {
    QByteArray type;
    QByteArray payload;
};

std::vector<Chunk> chunksOf(const QByteArray& file) {
    io::ByteReader r(file, io::ByteOrder::Little);
    r.skip(12);
    std::vector<Chunk> out;
    while (!r.atEnd()) {
        Chunk c;
        c.type = r.bytes(4);
        c.payload = r.bytes(r.u64());
        r.u32();
        EXPECT_TRUE(r.ok());
        out.push_back(c);
    }
    return out;
}

QByteArray build(const QByteArray& header, const std::vector<Chunk>& chunks) {
    QByteArray out = header.left(12);
    for (const Chunk& c : chunks) {
        io::ByteWriter w(io::ByteOrder::Little);
        w.bytes(c.type);
        w.u64(static_cast<std::uint64_t>(c.payload.size()));
        w.bytes(c.payload);
        std::uint32_t crc = io::crc32(0, reinterpret_cast<const std::uint8_t*>(c.type.constData()), 4);
        crc = io::crc32(crc, reinterpret_cast<const std::uint8_t*>(c.payload.constData()), static_cast<std::size_t>(c.payload.size()));
        w.u32(crc);
        out += w.data();
    }
    return out;
}

QJsonObject manifestOf(const Chunk& c) {
    return QJsonDocument::fromJson(qUncompress(c.payload)).object();
}

Chunk manifestChunk(const QJsonObject& o) {
    return {QByteArrayLiteral("MNFT"), qCompress(QJsonDocument(o).toJson(QJsonDocument::Compact))};
}

}

TEST(DoodleFormat, RoundTripIsBitExact) {
    const DocumentContent original = richContent();
    const auto loaded = parse(saved(original));
    ASSERT_TRUE(loaded.ok()) << qPrintable(loaded.error().describe());
    expectSameContent(original, loaded.value().content);
    EXPECT_TRUE(loaded.value().notes.isEmpty());
}

TEST(DoodleFormat, WritesTheDoodleMagic) {
    const DocumentContent c = richContent();
    QByteArray file = saved(c);
    EXPECT_EQ(file.left(8), QByteArray("\x89"
                                       "DOODLE"
                                       "\x1A",
                                       8));
    QBuffer buffer(&file);
    buffer.open(QIODevice::ReadOnly);
    EXPECT_TRUE(DoodleFormat().recognizes(buffer));
    EXPECT_EQ(DoodleFormat().extensions(), (QStringList{QStringLiteral("doodle")}));
}

TEST(DoodleFormat, LayerMasksRoundTrip) {
    DocumentContent c = richContent();
    std::vector<LayerNode*> rasters;
    std::function<void(LayerNode&)> walk = [&](LayerNode& n) {
        for (LayerNode& k : n.children)
            k.isFolder() ? walk(k) : rasters.push_back(&k);
    };
    walk(c.state.layers.root());
    ASSERT_GE(rasters.size(), 2u);
    SelectionMask soft(c.size());
    std::vector<std::uint16_t> row(std::size_t(c.size().width()));
    for (int y = 0; y < c.size().height(); ++y) {
        for (int x = 0; x < c.size().width(); ++x)
            row[std::size_t(x)] = static_cast<std::uint16_t>((x * 97 + y * 31) % (SelectionMask::kOne + 1));
        soft.writeRow(y, 0, c.size().width(), row.data());
    }
    soft.squeeze();
    rasters[0]->mask = soft;
    rasters[1]->mask = SelectionMask(c.size());
    rasters[1]->props.maskEnabled = false;
    QByteArray file = saved(c);
    const auto loaded = parse(file);
    ASSERT_TRUE(loaded.ok()) << qPrintable(loaded.error().describe());
    expectSameContent(c, loaded.value().content);
}

TEST(DoodleFormat, TextLayersRoundTrip) {
    DocumentContent c = richContent();
    LayerStack& s = c.state.layers;
    TextContent t;
    t.origin = QPointF(12.5, 30.25);
    t.vertical = true;
    t.fitToPixelGrid = true;
    TextStyle a{QStringLiteral("DejaVu Sans"), 20.0, false, false, Qt::black};
    TextStyle b{QStringLiteral("Adwaita Mono"), 33.5, true, true, QColor(200, 10, 30)};
    t.insert(0, QStringLiteral("Hello é中\n"), a);
    t.insert(t.length(), QStringLiteral("Ink"), b);
    t.emptyStyle = b;
    LayerNode node = s.makeText(QStringLiteral("Text1"), t);
    scribble(node.pixels, QRect(10, 10, 40, 20), 9);
    s.insert(std::move(node), kNoLayer, -1);
    QByteArray file = saved(c);
    const auto loaded = parse(file);
    ASSERT_TRUE(loaded.ok()) << qPrintable(loaded.error().describe());
    expectSameContent(c, loaded.value().content);
    EXPECT_TRUE(loaded.value().content.state.layers.root().children.back().isText());
}

TEST(DoodleFormat, SavingTwiceGivesTheSameBytes) {
    const DocumentContent c = richContent();
    EXPECT_EQ(saved(c), saved(c));
}

TEST(DoodleFormat, AnEmptyDocumentRoundTrips) {
    Document doc(QSize(70, 33), QColor(1, 2, 3));
    const DocumentContent original = doc.content();
    EXPECT_FALSE(original.view.value_or(ViewState{}).flipH);
    const auto loaded = parse(saved(original));
    ASSERT_TRUE(loaded.ok());
    expectSameContent(original, loaded.value().content);
    EXPECT_FALSE(loaded.value().content.state.selection.has_value());
}

TEST(DoodleFormat, FoldersNestedDeeperThanTenLevelsAreRefused) {
    const auto nested = [](int levels) {
        DocumentContent c;
        LayerStack& s = c.state.layers;
        s = LayerStack(QSize(8, 8));
        LayerId parent = kNoLayer;
        for (int i = 0; i < levels; ++i)
            parent = s.insert(s.makeFolder(QStringLiteral("f")), parent, -1);
        c.state.current = s.insert(s.makeRaster(QStringLiteral("inside")), parent, -1);
        return c;
    };
    EXPECT_TRUE(parse(saved(nested(10))).ok());
    const auto deeper = parse(saved(nested(11)));
    ASSERT_FALSE(deeper.ok());
    EXPECT_EQ(deeper.error().kind, FileError::Kind::Corrupt);
}

TEST(DoodleFormat, DocumentSurvivesSaveAndOpen) {
    Document doc(richContent());
    Document again(parse(saved(doc.content())).value().content);
    expectSameContent(doc.content(), again.content());
    EXPECT_EQ(again.currentLayerId(), doc.currentLayerId());
    const auto added = again.addLayer();
    ASSERT_TRUE(added.ok());
    EXPECT_GE(added.value().value(), doc.content().state.layers.nextIdValue());
}

TEST(DoodleFormat, FileRoundTripAndSniffing) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const DocumentContent original = richContent();
    const QString path = dir.filePath(QStringLiteral("picture.doodle"));
    ASSERT_TRUE(io::saveFile(original, path, DoodleFormat()).ok());
    auto loaded = io::loadFile(path);
    ASSERT_TRUE(loaded.ok()) << qPrintable(loaded.error().describe());
    expectSameContent(original, loaded.value().content);

    const QString odd = dir.filePath(QStringLiteral("picture.bin"));
    ASSERT_TRUE(QFile::copy(path, odd));
    loaded = io::loadFile(odd);
    ASSERT_TRUE(loaded.ok());
    expectSameContent(original, loaded.value().content);
}

TEST(DoodleFormat, FailedSaveLeavesTheOldFileAlone) {
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("keep.doodle"));
    ASSERT_TRUE(io::saveFile(richContent(), path, DoodleFormat()).ok());
    QFile f(path);
    ASSERT_TRUE(f.open(QIODevice::ReadOnly));
    const QByteArray before = f.readAll();
    f.close();

    class Failing : public DoodleFormat {
    public:
        FileResult<> write(const DocumentContent&, QIODevice& d, const ExportOptions&) const override {
            d.write("partial", 7);
            return FileResult<>::failure({FileError::Kind::CannotWrite, QStringLiteral("disk full")});
        }
    };
    const auto r = io::saveFile(richContent(), path, Failing());
    ASSERT_FALSE(r.ok());
    EXPECT_EQ(r.error().kind, FileError::Kind::CannotWrite);
    ASSERT_TRUE(f.open(QIODevice::ReadOnly));
    EXPECT_EQ(f.readAll(), before);
    EXPECT_EQ(QDir(dir.path()).entryList(QDir::Files).size(), 1);

    const auto missing = io::saveFile(richContent(), dir.filePath(QStringLiteral("no/such/dir/x.doodle")), DoodleFormat());
    ASSERT_FALSE(missing.ok());
    EXPECT_EQ(missing.error().kind, FileError::Kind::CannotWrite);
}

TEST(DoodleFormat, NotOurFiles) {
    EXPECT_EQ(parse({}).error().kind, FileError::Kind::NotADoodleFile);
    EXPECT_EQ(parse(QByteArray("this is certainly not a picture, but long enough")).error().kind, FileError::Kind::NotADoodleFile);
    EXPECT_EQ(parse(QByteArray("\x89PNG\r\n\x1a\n", 8)).error().kind, FileError::Kind::NotADoodleFile);
}

TEST(DoodleFormat, EveryOtherVersionIsRefusedWithThePlainError) {
    const QByteArray good = saved(richContent());
    EXPECT_EQ(quint8(good[8]), DoodleFormat::kVersion);
    EXPECT_TRUE(parse(good).ok());
    QTemporaryDir dir;
    for (const int version : {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 11, 12, 19, 23}) {  // earlier builds' 1 … 9, and 11, a canvas with frames
        QByteArray file = good;
        file[8] = static_cast<char>(version);
        const auto r = parse(file);
        ASSERT_FALSE(r.ok()) << version;
        EXPECT_EQ(r.error().kind, FileError::Kind::UnsupportedVersion) << version;
        EXPECT_TRUE(r.error().describe().startsWith(QStringLiteral("The file was saved by a different version of Doodlebold")));
        const QString path = dir.filePath(QStringLiteral("v%1.doodle").arg(version));
        QFile out(path);
        ASSERT_TRUE(out.open(QIODevice::WriteOnly));
        out.write(file);
        out.close();
        const auto opened = io::loadFile(path);
        ASSERT_FALSE(opened.ok()) << "opening the file itself: " << version;
        EXPECT_EQ(opened.error().kind, FileError::Kind::UnsupportedVersion) << version;
    }
}

TEST(DoodleFormat, EveryTruncationIsAnError) {
    const QByteArray file = saved(richContent());
    ASSERT_GT(file.size(), 200);
    for (qsizetype n = 0; n < file.size(); n += (n < 3000 ? 1 : 97)) {
        const auto r = parse(file.left(n));
        ASSERT_FALSE(r.ok()) << "cut at " << n;
    }
    const auto r = parse(file.left(file.size() - 1));
    ASSERT_FALSE(r.ok());
    EXPECT_EQ(r.error().kind, FileError::Kind::Truncated);
}

TEST(DoodleFormat, EverySingleByteChangeIsDetected) {
    const QByteArray file = saved(richContent());
    const qsizetype step = std::max<qsizetype>(1, file.size() / 2500);
    for (qsizetype i = 0; i < file.size(); i += step) {
        QByteArray damaged = file;
        damaged[i] = static_cast<char>(damaged[i] ^ 0x5A);
        const auto r = parse(damaged);
        ASSERT_FALSE(r.ok()) << "flipped byte " << i;
    }
}

TEST(DoodleFormat, DataAfterTheEndIsAnError) {
    const auto r = parse(saved(richContent()) + QByteArray("x"));
    ASSERT_FALSE(r.ok());
    EXPECT_EQ(r.error().kind, FileError::Kind::Corrupt);
}

TEST(DoodleFormat, WellFormedButImpossibleContentIsRefused) {
    const QByteArray file = saved(richContent());
    const std::vector<Chunk> good = chunksOf(file);
    ASSERT_GE(good.size(), 5u);
    ASSERT_TRUE(parse(build(file, good)).ok());

    const auto withManifest = [&](const std::function<void(QJsonObject&)>& edit) {
        std::vector<Chunk> chunks = good;
        QJsonObject m = manifestOf(chunks[0]);
        edit(m);
        chunks[0] = manifestChunk(m);
        return parse(build(file, chunks));
    };
    const auto expectCorrupt = [](const FileResult<LoadedDocument>& r, const char* what) {
        ASSERT_FALSE(r.ok()) << what;
        EXPECT_EQ(r.error().kind, FileError::Kind::Corrupt) << what;
    };

    expectCorrupt(withManifest([](QJsonObject& m) { m["width"] = 0; }), "zero width");
    expectCorrupt(withManifest([](QJsonObject& m) { m["height"] = 1 << 30; }), "huge height");
    expectCorrupt(withManifest([](QJsonObject& m) { m["width"] = 1.5; }), "fractional width");
    expectCorrupt(withManifest([](QJsonObject& m) { m["background"] = QJsonObject{{"mode", "mauve"}, {"color", "#808080"}}; }),
                  "unknown background mode");
    expectCorrupt(withManifest([](QJsonObject& m) { m["background"] = QJsonObject{{"mode", "color"}, {"color", "no"}}; }),
                  "background colour");
    expectCorrupt(withManifest([](QJsonObject& m) { m["resolution"] = 0; }), "zero resolution");
    expectCorrupt(withManifest([](QJsonObject& m) { m["resolution"] = "high"; }), "resolution not a number");
    expectCorrupt(withManifest([](QJsonObject& m) { m.remove("background"); }), "no background");
    expectCorrupt(withManifest([](QJsonObject& m) { m.remove("resolution"); }), "no resolution");
    expectCorrupt(withManifest([](QJsonObject& m) { m["currentLayer"] = 999; }), "unknown current layer");
    expectCorrupt(withManifest([](QJsonObject& m) { m["nextLayerId"] = 1; }), "id counter below used ids");
    expectCorrupt(withManifest([](QJsonObject& m) { m["layers"] = QJsonArray(); }), "no layers");
    expectCorrupt(withManifest([](QJsonObject& m) { m["layers"] = 5; }), "layers not a list");
    expectCorrupt(withManifest([](QJsonObject& m) {
                      QJsonArray l = m["layers"].toArray();
                      QJsonObject first = l[0].toObject();
                      first["opacity"] = 101;
                      l[0] = first;
                      m["layers"] = l;
                  }),
                  "opacity above 100");
    expectCorrupt(withManifest([](QJsonObject& m) {
                      QJsonArray l = m["layers"].toArray();
                      QJsonObject first = l[0].toObject();
                      first["mode"] = 99;
                      l[0] = first;
                      m["layers"] = l;
                  }),
                  "unknown old blend mode number");
    expectCorrupt(withManifest([](QJsonObject& m) {
                      QJsonArray l = m["layers"].toArray();
                      QJsonObject first = l[0].toObject();
                      first["mode"] = "mauve";
                      l[0] = first;
                      m["layers"] = l;
                  }),
                  "unknown blend mode name");
    expectCorrupt(withManifest([](QJsonObject& m) {
                      QJsonArray l = m["layers"].toArray();
                      QJsonObject first = l[0].toObject();
                      first["mode"] = 0;
                      l[0] = first;
                      m["layers"] = l;
                  }),
                  "raster layer in Pass Through");
    expectCorrupt(withManifest([](QJsonObject& m) {
                      QJsonArray l = m["layers"].toArray();
                      QJsonObject second = l[1].toObject();
                      second["id"] = l[0].toObject()["id"];
                      l[1] = second;
                      m["layers"] = l;
                  }),
                  "duplicate layer id");
    expectCorrupt(withManifest([](QJsonObject& m) {
                      QJsonObject inner = m["layers"].toArray()[0].toObject();
                      for (int i = 0; i < 400; ++i) {
                          QJsonObject f;
                          f["id"] = 5000 + i;
                          f["kind"] = "folder";
                          f["name"] = "f";
                          f["mode"] = 0;
                          f["opacity"] = 100;
                          f["visible"] = f["clipping"] = f["preserveOpacity"] = f["locked"] = f["expanded"] = false;
                          f["children"] = QJsonArray{inner};
                          inner = f;
                      }
                      m["layers"] = QJsonArray{inner};
                      m["nextLayerId"] = 9000;
                  }),
                  "folders nested without limit");
    {
        std::vector<Chunk> chunks = good;
        chunks[0].payload = QByteArray("\x7f\xff\xff\xff\x78\x9c", 6);
        expectCorrupt(parse(build(file, chunks)), "manifest claiming 2 GiB");
    }
    {
        std::vector<Chunk> chunks = good;
        std::swap(chunks[1], chunks[2]);
        expectCorrupt(parse(build(file, chunks)), "layer chunks out of order");
    }
    {
        std::vector<Chunk> chunks = good;
        chunks.erase(chunks.begin() + 1);
        expectCorrupt(parse(build(file, chunks)), "layer chunk missing");
    }
    {
        std::vector<Chunk> chunks = good;
        QByteArray& p = chunks[1].payload;
        ASSERT_GT(p.size(), 20);
        p[8] = 100;
        expectCorrupt(parse(build(file, chunks)), "tile beyond the canvas");
    }
    {
        std::vector<Chunk> chunks = good;
        chunks[1].payload[4] = 0x7f;
        expectCorrupt(parse(build(file, chunks)), "tile count");
    }
    {
        std::vector<Chunk> chunks = good;
        QByteArray& p = chunks[1].payload;
        for (int i = 24; i < std::min<int>(static_cast<int>(p.size()), 60); ++i)
            p[i] = static_cast<char>(0xEE);
        expectCorrupt(parse(build(file, chunks)), "garbage tile");
    }
    {
        std::vector<Chunk> chunks = good;
        chunks.erase(chunks.end() - 2);
        expectCorrupt(parse(build(file, chunks)), "selection missing");
    }
    {
        std::vector<Chunk> chunks = good;
        chunks.pop_back();
        EXPECT_FALSE(parse(build(file, chunks)).ok());
    }
}

TEST(DoodleFormat, ChunkLengthBeyondTheFileIsTruncation) {
    QByteArray file = saved(richContent());
    // First chunk's length field (offset 12 + 4) set to nearly 3 GiB: must stop at the end of the file, not allocate.
    file[12 + 4 + 3] = 0x7f;
    const auto r = parse(file);
    ASSERT_FALSE(r.ok());
    EXPECT_EQ(r.error().kind, FileError::Kind::Truncated);
}

TEST(DoodleFormat, TileCodecsBothOccur) {
    DocumentContent c;
    c.state.layers = LayerStack(QSize(128, 64));
    LayerNode n = c.state.layers.makeRaster(QStringLiteral("L"));
    n.pixels = TiledImage(c.state.layers.size());
    for (int y = 0; y < 64; ++y)
        for (int x = 0; x < 64; ++x) {
            Channel* a = n.pixels.writableTile(0, 0)->at(x, y);
            a[0] = a[1] = a[2] = fromUnorm8(x * 4);
            a[3] = kChannelOne;
            Channel* b = n.pixels.writableTile(1, 0)->at(x, y);
            b[0] = b[1] = b[2] = static_cast<Channel>(x * 251 + y * 3);
            b[3] = kChannelOne;
        }
    c.state.current = c.state.layers.insert(std::move(n), kNoLayer, -1);
    const auto loaded = parse(saved(c));
    ASSERT_TRUE(loaded.ok());
    expectSameContent(c, loaded.value().content);
}

namespace {

const LayerNode* firstRasterOf(const DocumentContent& c) {
    const LayerNode* found = nullptr;
    c.state.layers.forEach([&](const LayerNode& n) {
        if (!found && !n.isFolder())
            found = &n;
    });
    return found;
}

LayerNode* firstRaster(DocumentContent& c) {
    return const_cast<LayerNode*>(firstRasterOf(c));
}

}

TEST(DoodleFormat, LayerPaperRoundTrips) {
    DocumentContent c = richContent();
    LayerNode* a = firstRaster(c);
    ASSERT_NE(a, nullptr);
    a->props.paper = {QStringLiteral("Wasser Farbe é"), 80, 300};
    QByteArray file = saved(c);
    const auto loaded = parse(file);
    ASSERT_TRUE(loaded.ok()) << qPrintable(loaded.error().describe());
    expectSameContent(c, loaded.value().content);
    EXPECT_EQ(firstRasterOf(loaded.value().content)->props.paper, a->props.paper);

    a->props.paper = {QString(), 80, 300};
    const auto kept = parse(saved(c));
    ASSERT_TRUE(kept.ok());
    EXPECT_EQ(firstRasterOf(kept.value().content)->props.paper, a->props.paper);
}

TEST(DoodleFormat, LayerPaperInTheManifestIsChecked) {
    DocumentContent c = richContent();
    firstRaster(c)->props.paper = {QStringLiteral("Weave"), 15, 100};
    QByteArray file = saved(c);
    const std::vector<Chunk> good = chunksOf(file);
    const auto withPaper = [&](const std::function<void(QJsonObject&)>& edit) {
        std::vector<Chunk> chunks = good;
        QJsonObject m = manifestOf(chunks[0]);
        QJsonArray layers = m["layers"].toArray();
        const std::function<bool(QJsonArray&)> editFirstRaster = [&](QJsonArray& list) {
            for (auto&& i : list) {
                QJsonObject o = i.toObject();
                if (o.contains("paper")) {
                    QJsonObject paper = o["paper"].toObject();
                    edit(paper);
                    o["paper"] = paper;
                    i = o;
                    return true;
                }
                if (o["kind"] == "folder") {
                    QJsonArray kids = o["children"].toArray();
                    if (editFirstRaster(kids)) {
                        o["children"] = kids;
                        i = o;
                        return true;
                    }
                }
            }
            return false;
        };
        EXPECT_TRUE(editFirstRaster(layers));
        m["layers"] = layers;
        chunks[0] = manifestChunk(m);
        QByteArray bytes = build(file, chunks);
        return parse(bytes);
    };
    EXPECT_TRUE(withPaper([](QJsonObject&) {}).ok());
    EXPECT_FALSE(withPaper([](QJsonObject& p) { p["scale"] = 9; }).ok()) << "scale below 10";
    EXPECT_FALSE(withPaper([](QJsonObject& p) { p["scale"] = 501; }).ok());
    EXPECT_FALSE(withPaper([](QJsonObject& p) { p["intensity"] = 101; }).ok());
    EXPECT_FALSE(withPaper([](QJsonObject& p) { p["texture"] = 3; }).ok());
}

TEST(DoodleFormat, EveryBlendModeHasItsOwnStoredName) {
    QSet<QString> keys;
    for (int m = 0; m < kBlendModeCount; ++m) {
        const auto mode = static_cast<BlendMode>(m);
        EXPECT_EQ(blendModeInfo(mode).mode, mode);
        const QString key = QString::fromLatin1(blendModeInfo(mode).key);
        keys.insert(key);
        EXPECT_EQ(blendModeFromKey(key), mode);
    }
    EXPECT_EQ(keys.size(), kBlendModeCount);
    EXPECT_FALSE(blendModeFromKey(QStringLiteral("mauve")));
}
