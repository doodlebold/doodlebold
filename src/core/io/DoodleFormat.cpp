#include "core/io/DoodleFormat.h"

#include "core/animation/AnimationOps.h"
#include "core/io/ByteIO.h"
#include "core/timelapse/TimelapseFile.h"
#include "core/util/BuildInfo.h"
#include "core/util/ParallelFor.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <optional>
#include <unordered_map>
#include <unordered_set>

namespace doodlebold {

namespace {

using io::ByteOrder;
using io::ByteReader;
using io::ByteWriter;

constexpr std::array<std::uint8_t, 8> kMagic = {0x89, 'D', 'O', 'O', 'D', 'L', 'E', 0x1A};

bool isOurMagic(const char* p) {
    return std::memcmp(p, kMagic.data(), kMagic.size()) == 0;
}
constexpr std::size_t kHeaderSize = 12;  // magic + version
constexpr std::size_t kMaxManifest = 64u << 20;
constexpr std::uint64_t kMaxChunk = 3u << 30;  // a layer of the largest canvas, uncompressed, is 2 GiB
constexpr int kMaxDepth = LayerStack::kMaxFolderLevels;
constexpr int kMaxLayers = 20000;
constexpr int kLargestEverCanvasSide = 20000;

constexpr char kManifestChunk[] = "MNFT";
constexpr char kLayerChunk[] = "LAYR";
constexpr char kSelectionChunk[] = "SELM";
constexpr char kMaskChunk[] = "LMSK";  // a layer mask, right after its layer's LAYR chunk; folders' masks follow all LAYR
                                       // chunks, in list order
constexpr char kSoundChunk[] = "SOND";  // a sound clip's file: its id, then the bytes
constexpr char kEndChunk[] = "END ";

// A tile blob is one codec byte and a qCompress stream of the values.
enum class Codec : std::uint8_t {
    Raw = 0,          // the values as little-endian 16-bit words
    PlanesDelta = 1,  // per channel: horizontal difference within the row, then low bytes, then high bytes
};

FileError corrupt(const QString& detail) {
    return {FileError::Kind::Corrupt, detail};
}
FileError truncated(const QString& detail = {}) {
    return {FileError::Kind::Truncated, detail};
}

std::uint32_t chunkCrc(const char type[4], const QByteArray& payload) {
    const std::uint32_t c = io::crc32(0, reinterpret_cast<const std::uint8_t*>(type), 4);
    return io::crc32(c, reinterpret_cast<const std::uint8_t*>(payload.constData()), static_cast<std::size_t>(payload.size()));
}

constexpr int kTileValues = Tile::kSize * Tile::kSize;

QByteArray encodeRaw(const std::uint16_t* v, int channels) {
    QByteArray out(kTileValues * channels * 2, Qt::Uninitialized);
    for (int i = 0; i < kTileValues * channels; ++i) {
        out[2 * i] = static_cast<char>(v[i] & 0xFF);
        out[2 * i + 1] = static_cast<char>(v[i] >> 8);
    }
    return out;
}

QByteArray encodePlanesDelta(const std::uint16_t* v, int channels) {
    QByteArray out(kTileValues * channels * 2, Qt::Uninitialized);
    char* lo = out.data();
    for (int c = 0; c < channels; ++c) {
        char* hi = lo + kTileValues;
        for (int i = 0; i < kTileValues; ++i) {
            const std::uint16_t left = (i % Tile::kSize) ? v[(i - 1) * channels + c] : 0;
            const std::uint16_t d = static_cast<std::uint16_t>(v[i * channels + c] - left);
            lo[i] = static_cast<char>(d & 0xFF);
            hi[i] = static_cast<char>(d >> 8);
        }
        lo += 2 * kTileValues;
    }
    return out;
}

QByteArray encodeValues(const std::uint16_t* v, int channels) {
    // Pictures drawn with the brushes have noisy low bits and favour the planes; pictures imported from 8-bit files have
    // regular words that zlib finds as they are. Both are tried, the smaller one is kept.
    QByteArray best;
    char codec = static_cast<char>(Codec::Raw);
    best = qCompress(encodeRaw(v, channels));
    const QByteArray planes = qCompress(encodePlanesDelta(v, channels));
    if (planes.size() < best.size()) {
        best = planes;
        codec = static_cast<char>(Codec::PlanesDelta);
    }
    best.prepend(codec);
    return best;
}

bool decodeValues(const std::uint8_t* blob, std::size_t size, int channels, std::uint16_t* v) {
    const std::size_t bytes = static_cast<std::size_t>(kTileValues) * static_cast<std::size_t>(channels) * 2;
    if (size < 1 + 4)
        return false;
    // qCompress prefixes the unpacked size (big endian); check it before qUncompress allocates anything.
    const std::uint8_t* z = blob + 1;
    const std::uint32_t claimed = (std::uint32_t(z[0]) << 24) | (std::uint32_t(z[1]) << 16) | (std::uint32_t(z[2]) << 8) | z[3];
    if (claimed != bytes)
        return false;
    const QByteArray data = qUncompress(z, static_cast<qsizetype>(size - 1));
    if (static_cast<std::size_t>(data.size()) != bytes)
        return false;
    const auto* d = reinterpret_cast<const std::uint8_t*>(data.constData());
    switch (static_cast<Codec>(blob[0])) {
    case Codec::Raw:
        for (int i = 0; i < kTileValues * channels; ++i)
            v[i] = static_cast<std::uint16_t>(d[2 * i] | (d[2 * i + 1] << 8));
        return true;
    case Codec::PlanesDelta:
        for (int c = 0; c < channels; ++c) {
            const std::uint8_t* lo = d + static_cast<std::size_t>(c) * 2 * kTileValues;
            const std::uint8_t* hi = lo + kTileValues;
            std::uint16_t left = 0;
            for (int i = 0; i < kTileValues; ++i) {
                if (i % Tile::kSize == 0)
                    left = 0;
                left = static_cast<std::uint16_t>(left + (lo[i] | (hi[i] << 8)));
                v[i * channels + c] = left;
            }
        }
        return true;
    }
    return false;
}

bool isAllZero(const Tile& t) {
    return std::ranges::all_of(t.px, [](Channel c) { return c == 0; });
}

bool writeAll(QIODevice& device, const char* data, qint64 size) {
    return device.write(data, size) == size;
}

bool writeChunk(QIODevice& device, const char type[4], const QByteArray& payload) {
    ByteWriter head(ByteOrder::Little);
    head.bytes(type, 4);
    head.u64(static_cast<std::uint64_t>(payload.size()));
    ByteWriter tail(ByteOrder::Little);
    tail.u32(chunkCrc(type, payload));
    return writeAll(device, head.data().constData(), head.data().size()) && writeAll(device, payload.constData(), payload.size()) &&
           writeAll(device, tail.data().constData(), tail.data().size());
}

struct TileEntry {
    int tx, ty;
    const Tile* tile;
};

QJsonObject styleToJson(const TextStyle& s) {
    return {{"font", s.font}, {"size", s.sizePx}, {"bold", s.bold}, {"italic", s.italic}, {"colour", s.colour.name()}};
}

QJsonObject textToJson(const TextContent& t) {
    QJsonArray runs;
    for (const TextRun& r : t.runs) {
        QJsonObject o = styleToJson(r.style);
        o[QLatin1String("text")] = r.text;
        runs.append(o);
    }
    return {{"x", t.origin.x()},
            {"y", t.origin.y()},
            {"vertical", t.vertical},
            {"fitToPixelGrid", t.fitToPixelGrid},
            {"empty", styleToJson(t.emptyStyle)},
            {"runs", runs}};
}

std::optional<TextStyle> styleFromJson(const QJsonObject& o) {
    TextStyle s;
    const QColor colour(o.value(QLatin1String("colour")).toString());
    if (!o.value(QLatin1String("font")).isString() || !o.value(QLatin1String("size")).isDouble() || !colour.isValid())
        return std::nullopt;
    s.font = o.value(QLatin1String("font")).toString();
    s.sizePx = o.value(QLatin1String("size")).toDouble();
    s.bold = o.value(QLatin1String("bold")).toBool();
    s.italic = o.value(QLatin1String("italic")).toBool();
    s.colour = colour;
    if (!(s.sizePx > 0.0 && s.sizePx <= 10000.0))
        return std::nullopt;
    return s;
}

std::optional<TextContent> textFromJson(const QJsonValue& v) {
    if (!v.isObject())
        return std::nullopt;
    const QJsonObject o = v.toObject();
    TextContent t;
    if (!o.value(QLatin1String("x")).isDouble() || !o.value(QLatin1String("y")).isDouble() || !o.value(QLatin1String("runs")).isArray())
        return std::nullopt;
    t.origin = QPointF(o.value(QLatin1String("x")).toDouble(), o.value(QLatin1String("y")).toDouble());
    t.vertical = o.value(QLatin1String("vertical")).toBool();
    t.fitToPixelGrid = o.value(QLatin1String("fitToPixelGrid")).toBool();
    const auto empty = styleFromJson(o.value(QLatin1String("empty")).toObject());
    if (!empty)
        return std::nullopt;
    t.emptyStyle = *empty;
    int at = 0;
    for (const auto& rv : o.value(QLatin1String("runs")).toArray()) {
        const auto style = styleFromJson(rv.toObject());
        if (!style || !rv.toObject().value(QLatin1String("text")).isString())
            return std::nullopt;
        const QString text = rv.toObject().value(QLatin1String("text")).toString();
        t.insert(at, text, *style);
        at += int(text.size());
    }
    t.emptyStyle = *empty;
    return t;
}

QJsonObject nodeToJson(const LayerNode& n) {
    const LayerProps& p = n.props;
    QJsonObject o;
    o[QLatin1String("id")] = static_cast<qint64>(p.id.value());
    o[QLatin1String("kind")] = p.kind == LayerKind::Folder ? QLatin1String("folder")
                               : p.kind == LayerKind::Text ? QLatin1String("text")
                                                           : QLatin1String("raster");
    if (n.text)
        o[QLatin1String("text")] = textToJson(*n.text);
    o[QLatin1String("name")] = p.name;
    o[QLatin1String("mode")] = QLatin1String(blendModeInfo(p.mode).key);
    o[QLatin1String("opacity")] = p.opacity;
    o[QLatin1String("visible")] = p.visible;
    o[QLatin1String("clipping")] = p.clipping;
    o[QLatin1String("preserveOpacity")] = p.preserveOpacity;
    o[QLatin1String("locked")] = p.locked;
    o[QLatin1String("expanded")] = p.expanded;
    if (n.mask) {
        QJsonObject mask;
        mask[QLatin1String("enabled")] = p.maskEnabled;
        mask[QLatin1String("linked")] = p.maskLinked;
        o[QLatin1String("mask")] = mask;
    }
    if (p.paper != LayerPaper{}) {  // the numbers stay with the layer even with no texture chosen
        QJsonObject paper;
        paper[QLatin1String("texture")] = p.paper.texture;
        paper[QLatin1String("intensity")] = p.paper.intensity;
        paper[QLatin1String("scale")] = p.paper.scale;
        o[QLatin1String("paper")] = paper;
    }
    if (n.isFolder()) {
        QJsonArray kids;
        for (const LayerNode& c : n.children)
            kids.append(nodeToJson(c));
        o[QLatin1String("children")] = kids;
    }
    return o;
}

void collectRastersPreorder(const LayerNode& n, std::vector<const LayerNode*>& out) {
    for (const LayerNode& c : n.children) {
        if (c.isFolder())
            collectRastersPreorder(c, out);
        else
            out.push_back(&c);
    }
}

std::vector<const LayerNode*> maskedFolders(const LayerNode& n) {
    std::vector<const LayerNode*> out;
    const auto walk = [&](auto&& self, const LayerNode& folder) -> void {
        for (const LayerNode& c : folder.children)
            if (c.isFolder()) {
                if (c.mask)
                    out.push_back(&c);
                self(self, c);
            }
    };
    walk(walk, n);
    return out;
}

QByteArray layerPayload(const LayerNode& layer) {
    std::vector<TileEntry> tiles;
    for (const auto& [coord, ptr] : layer.pixels.tiles())
        if (ptr && !isAllZero(*ptr))
            tiles.push_back({coord.x, coord.y, ptr.get()});
    std::ranges::sort(tiles, [](const TileEntry& a, const TileEntry& b) { return a.ty != b.ty ? a.ty < b.ty : a.tx < b.tx; });
    std::vector<QByteArray> blobs(tiles.size());
    parallelFor(static_cast<int>(tiles.size()),
                [&](int i) { blobs[static_cast<std::size_t>(i)] = encodeValues(tiles[static_cast<std::size_t>(i)].tile->px.data(), 4); });
    ByteWriter w(ByteOrder::Little);
    w.u32(layer.props.id.value());
    w.u32(static_cast<std::uint32_t>(tiles.size()));
    for (std::size_t i = 0; i < tiles.size(); ++i) {
        w.i32(tiles[i].tx);
        w.i32(tiles[i].ty);
        w.u32(static_cast<std::uint32_t>(blobs[i].size()));
        w.bytes(blobs[i]);
    }
    return w.take();
}

QByteArray coveragePayload(const SelectionMask& mask, std::optional<LayerId> layer = std::nullopt) {
    std::vector<std::pair<TileCoord, const SelectionTile*>> tiles;
    for (const auto& [coord, ptr] : mask.tiles())
        tiles.emplace_back(coord, ptr.get());
    std::ranges::sort(tiles,
                      [](const auto& a, const auto& b) { return a.first.y != b.first.y ? a.first.y < b.first.y : a.first.x < b.first.x; });
    std::vector<QByteArray> blobs(tiles.size());
    parallelFor(static_cast<int>(tiles.size()), [&](int i) {
        const SelectionTile* t = tiles[static_cast<std::size_t>(i)].second;
        if (!SelectionMask::isFull(t))
            blobs[static_cast<std::size_t>(i)] = encodeValues(t->v.data(), 1);
    });
    ByteWriter w(ByteOrder::Little);
    if (layer)
        w.u32(layer->value());
    w.u32(static_cast<std::uint32_t>(tiles.size()));
    for (std::size_t i = 0; i < tiles.size(); ++i) {
        w.i32(tiles[i].first.x);
        w.i32(tiles[i].first.y);
        if (blobs[i].isEmpty()) {
            w.u8(1);
        } else {
            w.u8(0);
            w.u32(static_cast<std::uint32_t>(blobs[i].size()));
            w.bytes(blobs[i]);
        }
    }
    return w.take();
}

bool hasAnimation(const Animation& a) {
    return !a.tracks.empty() || !a.sounds.empty() || a.video.has_value();
}

QJsonObject frameToJson(const AnimationFrame& f) {
    QJsonObject fo;
    fo[QLatin1String("id")] = f.id;
    fo[QLatin1String("hold")] = f.hold;
    fo[QLatin1String("currentLayer")] = static_cast<qint64>(f.current.value());
    if (f.picture >= 0)
        fo[QLatin1String("picture")] = f.picture;
    QJsonArray own;
    for (const LayerNode& n : f.layers)
        own.append(nodeToJson(n));
    fo[QLatin1String("layers")] = own;
    return fo;
}

QString trackKindKey(TrackKind kind) {
    switch (kind) {
    case TrackKind::Sound: return QStringLiteral("sound");
    case TrackKind::Video: return QStringLiteral("video");
    case TrackKind::Frames: break;
    }
    return QStringLiteral("frames");
}

QJsonObject trackToJson(const AnimationTrack& t) {
    QJsonObject to;
    to[QLatin1String("id")] = t.id;
    to[QLatin1String("name")] = t.name;
    to[QLatin1String("kind")] = trackKindKey(t.kind);
    to[QLatin1String("hidden")] = t.hidden;
    to[QLatin1String("locked")] = t.locked;
    if (t.kind == TrackKind::Sound)
        to[QLatin1String("volume")] = t.volume;
    if (t.label != 0)
        to[QLatin1String("label")] = t.label;
    to[QLatin1String("current")] = t.current;
    QJsonArray layers;
    for (LayerId id : t.layers)
        layers.append(static_cast<qint64>(id.value()));
    to[QLatin1String("layers")] = layers;
    QJsonArray frames;
    for (const AnimationFrame& f : t.frames)
        frames.append(frameToJson(f));
    to[QLatin1String("frames")] = frames;
    return to;
}

QJsonObject animationToJson(const Animation& a) {
    QJsonObject o;
    QJsonArray tracks;
    for (const AnimationTrack& t : a.tracks)
        tracks.append(trackToJson(t));
    o[QLatin1String("tracks")] = tracks;
    o[QLatin1String("currentTrack")] = a.currentTrack;
    o[QLatin1String("beat")] = a.beat;
    o[QLatin1String("nextTrackId")] = a.nextTrackId;
    QJsonArray shared;
    for (LayerId id : a.shared)
        shared.append(static_cast<qint64>(id.value()));
    o[QLatin1String("shared")] = shared;
    o[QLatin1String("fps")] = a.fps;
    o[QLatin1String("loop")] = a.loop;
    o[QLatin1String("playStart")] = a.playStart;
    o[QLatin1String("playEnd")] = a.playEnd;
    o[QLatin1String("playbackSpeed")] = a.view.speed;
    o[QLatin1String("nextFrameId")] = a.nextFrameId;
    o[QLatin1String("nextSoundId")] = a.nextSoundId;
    QJsonObject onion;
    onion[QLatin1String("on")] = a.view.onionSkin;
    onion[QLatin1String("before")] = a.view.onionBefore;
    onion[QLatin1String("after")] = a.view.onionAfter;
    onion[QLatin1String("opacity")] = a.view.onionOpacity;
    o[QLatin1String("onionSkin")] = onion;
    o[QLatin1String("timelineShown")] = a.view.timelineShown;
    QJsonArray collapsed;
    for (int id : a.view.collapsed)
        collapsed.append(id);
    o[QLatin1String("collapsedTracks")] = collapsed;
    QJsonArray sounds;
    for (const SoundClip& c : a.sounds) {
        QJsonObject so;
        so[QLatin1String("id")] = c.id;
        so[QLatin1String("name")] = c.name;
        so[QLatin1String("startBeat")] = c.startBeat;
        so[QLatin1String("seconds")] = c.seconds;
        so[QLatin1String("trimStart")] = c.trimStart;
        so[QLatin1String("trimEnd")] = c.trimEnd;
        so[QLatin1String("track")] = c.track;
        so[QLatin1String("muted")] = c.muted;
        sounds.append(so);
    }
    o[QLatin1String("sounds")] = sounds;
    if (a.video) {
        const VideoReference& v = *a.video;
        QJsonObject vo;
        vo[QLatin1String("path")] = v.path;
        vo[QLatin1String("width")] = v.sourceSize.width();
        vo[QLatin1String("height")] = v.sourceSize.height();
        vo[QLatin1String("sourceFps")] = v.sourceFps;
        vo[QLatin1String("seconds")] = v.seconds;
        vo[QLatin1String("hasSound")] = v.hasSound;
        vo[QLatin1String("fps")] = v.fps;
        vo[QLatin1String("startBeat")] = v.startBeat;
        vo[QLatin1String("opacity")] = v.opacity;
        vo[QLatin1String("lossless")] = v.lossless;
        o[QLatin1String("video")] = vo;
    }
    return o;
}

QByteArray manifest(const DocumentContent& c) {
    const DocumentState& s = c.state;
    QJsonObject root;
    root[QLatin1String("application")] = QStringLiteral("Doodlebold %1").arg(build_info::version());
    root[QLatin1String("width")] = s.layers.size().width();
    root[QLatin1String("height")] = s.layers.size().height();
    const CanvasBackground& bg = s.background;
    QJsonObject background;
    background[QLatin1String("mode")] = backgroundModeKey(bg.mode);
    background[QLatin1String("color")] = bg.color.name(QColor::HexRgb);
    root[QLatin1String("background")] = background;
    root[QLatin1String("resolution")] = c.resolution;
    root[QLatin1String("nextLayerId")] = static_cast<qint64>(s.layers.nextIdValue());
    root[QLatin1String("currentLayer")] = static_cast<qint64>(s.current.value());
    root[QLatin1String("hasSelection")] = s.selection.has_value();
    if (c.view) {
        QJsonObject v;
        v[QLatin1String("zoom")] = c.view->zoom;
        v[QLatin1String("rotation")] = c.view->rotationDeg;
        v[QLatin1String("flipH")] = c.view->flipH;
        v[QLatin1String("panX")] = c.view->pan.x();
        v[QLatin1String("panY")] = c.view->pan.y();
        root[QLatin1String("view")] = v;
    }
    QJsonArray layers;
    for (const LayerNode& n : s.layers.root().children)
        layers.append(nodeToJson(n));
    root[QLatin1String("layers")] = layers;
    if (hasAnimation(s.animation))
        root[QLatin1String("animation")] = animationToJson(s.animation);
    return qCompress(QJsonDocument(root).toJson(QJsonDocument::Compact));
}

// `n` bytes from the device. The buffer grows as data arrives, so a damaged length field cannot make us allocate more than the
// file really holds.
FileResult<QByteArray> readExact(QIODevice& device, std::uint64_t n) {
    QByteArray out;
    constexpr qint64 kStep = 8 << 20;
    while (static_cast<std::uint64_t>(out.size()) < n) {
        const qint64 want = static_cast<qint64>(std::min<std::uint64_t>(kStep, n - static_cast<std::uint64_t>(out.size())));
        const qint64 old = out.size();
        out.resize(old + want);
        const qint64 got = device.read(out.data() + old, want);
        if (got <= 0) {
            out.resize(old);
            return FileResult<QByteArray>::failure(truncated());
        }
        out.resize(old + got);
    }
    return out;
}

using Chunk = doodle_file::Chunk;

FileResult<Chunk> readChunk(QIODevice& device) {
    auto head = readExact(device, 12);
    if (!head)
        return FileResult<Chunk>::failure(head.error());
    ByteReader h(head.value(), ByteOrder::Little);
    Chunk chunk;
    std::memcpy(chunk.type.data(), h.span(4), 4);
    const std::uint64_t length = h.u64();
    if (length > kMaxChunk)
        return FileResult<Chunk>::failure(corrupt(QStringLiteral("(chunk length)")));
    auto payload = readExact(device, length);
    if (!payload)
        return FileResult<Chunk>::failure(payload.error());
    auto crc = readExact(device, 4);
    if (!crc)
        return FileResult<Chunk>::failure(crc.error());
    ByteReader c(crc.value(), ByteOrder::Little);
    if (c.u32() != chunkCrc(chunk.type.data(), payload.value()))
        return FileResult<Chunk>::failure(corrupt(QStringLiteral("(checksum of chunk %1)").arg(QString::fromLatin1(chunk.type.data(), 4))));
    chunk.payload = payload.value();
    return chunk;
}

// Manifest values with their checks. A bad value makes `ok` false and the first complaint is kept.
struct ManifestReader {
    bool ok = true;
    QString problem;

    void fail(const QString& what) {
        if (ok)
            problem = what;
        ok = false;
    }
    std::int64_t integer(const QJsonObject& o, const char* key, std::int64_t lo, std::int64_t hi) {
        const QJsonValue v = o.value(QLatin1String(key));
        if (!v.isDouble() || v.toDouble() != std::floor(v.toDouble()) || v.toDouble() < double(lo) || v.toDouble() > double(hi)) {
            fail(QStringLiteral("(manifest: %1)").arg(QLatin1String(key)));
            return lo;
        }
        return static_cast<std::int64_t>(v.toDouble());
    }
    bool boolean(const QJsonObject& o, const char* key) {
        const QJsonValue v = o.value(QLatin1String(key));
        if (!v.isBool())
            fail(QStringLiteral("(manifest: %1)").arg(QLatin1String(key)));
        return v.toBool();
    }
    double real(const QJsonObject& o, const char* key) {
        const QJsonValue v = o.value(QLatin1String(key));
        if (!v.isDouble() || !std::isfinite(v.toDouble()))
            fail(QStringLiteral("(manifest: %1)").arg(QLatin1String(key)));
        return v.toDouble();
    }
};

struct TreeBuilder {
    ManifestReader& m;
    QSet<std::uint32_t> ids;
    std::uint32_t maxId = 0;
    int count = 0;

    bool buildNodes(const QJsonArray& in, int depth, std::vector<LayerNode>& out) {
        if (depth > kMaxDepth) {
            m.fail(QStringLiteral("(folders nested deeper than %1 levels)").arg(kMaxDepth));
            return false;
        }
        for (const auto& value : in) {
            if (!value.isObject() || ++count > kMaxLayers) {
                m.fail(QStringLiteral("(layer list)"));
                return false;
            }
            const QJsonObject o = value.toObject();
            LayerNode n;
            LayerProps& p = n.props;
            const auto id = static_cast<std::uint32_t>(m.integer(o, "id", 1, 0xFFFFFFFFLL));
            if (ids.contains(id))
                m.fail(QStringLiteral("(duplicate layer id)"));
            ids.insert(id);
            maxId = std::max(maxId, id);
            p.id = LayerId(id);
            const QString kind = o.value(QLatin1String("kind")).toString();
            if (kind == QLatin1String("folder")) {
                p.kind = LayerKind::Folder;
            } else if (kind == QLatin1String("raster")) {
                p.kind = LayerKind::Raster;
            } else if (kind == QLatin1String("text")) {
                p.kind = LayerKind::Text;
                n.text = textFromJson(o.value(QLatin1String("text")));
                if (!n.text)
                    m.fail(QStringLiteral("(text layer)"));
            } else {
                m.fail(QStringLiteral("(layer kind)"));
            }
            if (!o.value(QLatin1String("name")).isString())
                m.fail(QStringLiteral("(layer name)"));
            p.name = o.value(QLatin1String("name")).toString();
            const std::optional<BlendMode> mode = blendModeFromKey(o.value(QLatin1String("mode")).toString());
            if (!mode)
                m.fail(QStringLiteral("(blend mode)"));
            p.mode = mode.value_or(BlendMode::Normal);
            if (p.kind != LayerKind::Folder && p.mode == BlendMode::PassThrough)
                m.fail(QStringLiteral("(Pass Through on a raster layer)"));
            p.opacity = static_cast<int>(m.integer(o, "opacity", 0, 100));
            p.visible = m.boolean(o, "visible");
            p.clipping = m.boolean(o, "clipping");
            p.preserveOpacity = m.boolean(o, "preserveOpacity");
            p.locked = m.boolean(o, "locked");
            p.expanded = m.boolean(o, "expanded");
            if (o.contains(QLatin1String("mask"))) {
                const QJsonValue mask = o.value(QLatin1String("mask"));
                if (!mask.isObject()) {
                    m.fail(QStringLiteral("(layer mask)"));
                } else {
                    p.maskEnabled = m.boolean(mask.toObject(), "enabled");
                    p.maskLinked = m.boolean(mask.toObject(), "linked");
                }
                n.mask = SelectionMask();  // its pixels follow the layer's (LMSK chunk)
            }
            if (o.contains(QLatin1String("paper"))) {
                const QJsonValue paper = o.value(QLatin1String("paper"));
                if (!paper.isObject() || p.kind == LayerKind::Folder || !paper.toObject().value(QLatin1String("texture")).isString()) {
                    m.fail(QStringLiteral("(layer paper)"));
                } else {
                    const QJsonObject po = paper.toObject();
                    p.paper.texture = po.value(QLatin1String("texture")).toString();
                    p.paper.intensity = static_cast<int>(m.integer(po, "intensity", LayerPaper::kMinIntensity, LayerPaper::kMaxIntensity));
                    p.paper.scale = static_cast<int>(m.integer(po, "scale", LayerPaper::kMinScale, LayerPaper::kMaxScale));
                }
            }
            if (p.kind == LayerKind::Folder) {
                if (!o.value(QLatin1String("children")).isArray())
                    m.fail(QStringLiteral("(folder children)"));
                else
                    buildNodes(o.value(QLatin1String("children")).toArray(), depth + 1, n.children);
            } else if (o.contains(QLatin1String("children"))) {
                m.fail(QStringLiteral("(children of a raster layer)"));
            }
            if (!m.ok)
                return false;
            out.push_back(std::move(n));
        }
        return true;
    }
};

constexpr int kMaxFrames = 100000;

bool parseFrame(const QJsonObject& fo, TreeBuilder& builder, ManifestReader& m, QSet<int>& frameIds, AnimationFrame& f) {
    f.id = static_cast<int>(m.integer(fo, "id", 1, kMaxFrames * 100));
    f.hold = static_cast<int>(m.integer(fo, "hold", 1, AnimationFrame::kMaxHold));
    f.current = LayerId(static_cast<std::uint32_t>(m.integer(fo, "currentLayer", 0, 0xFFFFFFFFLL)));
    if (fo.contains(QLatin1String("picture")))
        f.picture = static_cast<int>(m.integer(fo, "picture", 0, 0x7FFFFFFF));
    if (frameIds.contains(f.id) || !fo.value(QLatin1String("layers")).isArray() || frameIds.size() >= kMaxFrames)
        m.fail(QStringLiteral("(animation frames)"));
    frameIds.insert(f.id);
    return m.ok && builder.buildNodes(fo.value(QLatin1String("layers")).toArray(), 0, f.layers);
}

bool parseFrames(const QJsonValue& value, TreeBuilder& builder, ManifestReader& m, QSet<int>& frameIds, AnimationTrack& t) {
    if (!value.isArray() || value.toArray().isEmpty()) {
        m.fail(QStringLiteral("(animation frames)"));
        return false;
    }
    for (const auto& fv : value.toArray()) {
        AnimationFrame f;
        if (!parseFrame(fv.toObject(), builder, m, frameIds, f))
            return false;
        t.frames.push_back(std::move(f));
    }
    return true;
}

// A track's frame whose layers are in the stack holds none of its own; a hidden track's are all held. A video track from
// before its pictures were frames has none (they are made when the file is read).
bool trackIsWhole(const AnimationTrack& t) {
    if (!t.hasFrames() || (t.kind == TrackKind::Video && t.frames.empty()))
        return t.layers.empty() && t.frames.empty();
    if (t.hidden)
        return t.current == -1 && t.layers.empty();
    return t.current >= 0 && t.current < t.frameCount() && t.frames[static_cast<std::size_t>(t.current)].layers.empty();
}

void parseTracks(const QJsonObject& o, Animation& a, TreeBuilder& builder, ManifestReader& m, QSet<int>& frameIds) {
    a.nextTrackId = static_cast<int>(m.integer(o, "nextTrackId", 1, 0x7FFFFFFF));
    QSet<int> trackIds;
    for (const auto& tv : o.value(QLatin1String("tracks")).toArray()) {
        const QJsonObject to = tv.toObject();
        AnimationTrack t;
        t.id = static_cast<int>(m.integer(to, "id", 1, 0x7FFFFFFF));
        t.name = to.value(QLatin1String("name")).toString();
        const QString kind = to.value(QLatin1String("kind")).toString(QStringLiteral("frames"));  // version 21: frames only
        if (kind != trackKindKey(TrackKind::Frames))
            t.kind = kind == trackKindKey(TrackKind::Sound) ? TrackKind::Sound : TrackKind::Video;
        if (kind != trackKindKey(t.kind))
            m.fail(QStringLiteral("(track kind)"));
        t.hidden = m.boolean(to, "hidden");
        if (to.contains(QLatin1String("locked")))
            t.locked = m.boolean(to, "locked");
        if (to.contains(QLatin1String("volume")))  // files from before the tracks' volume: as loud as their files
            t.volume = static_cast<int>(m.integer(to, "volume", 0, AnimationTrack::kFullVolume));
        if (to.contains(QLatin1String("label")))  // files from before label colours: none
            t.label = static_cast<int>(m.integer(to, "label", 0, AnimationTrack::kLabels));
        // "keys" and "pivot", from the timeline build that had tweens, aren't read: those files open as still tracks.
        t.current = static_cast<int>(m.integer(to, "current", -1, kMaxFrames));
        for (const auto& id : to.value(QLatin1String("layers")).toArray()) {
            if (!id.isDouble() || !builder.ids.contains(static_cast<std::uint32_t>(id.toDouble())))
                m.fail(QStringLiteral("(track layers)"));
            t.layers.emplace_back(static_cast<std::uint32_t>(id.toDouble()));
        }
        if (trackIds.contains(t.id) || t.id >= a.nextTrackId || !to.value(QLatin1String("layers")).isArray())
            m.fail(QStringLiteral("(animation tracks)"));
        trackIds.insert(t.id);
        const bool oldVideo = t.kind == TrackKind::Video && to.value(QLatin1String("frames")).toArray().isEmpty();
        if (t.hasFrames() && !oldVideo && !parseFrames(to.value(QLatin1String("frames")), builder, m, frameIds, t))
            return;
        if (!t.hasFrames() || oldVideo)
            t.current = -1;
        if (!trackIsWhole(t))
            m.fail(QStringLiteral("(animation track %1)").arg(t.name));
        a.tracks.push_back(std::move(t));
    }
    if (a.tracks.empty())
        m.fail(QStringLiteral("(animation tracks)"));
    a.currentTrack = static_cast<int>(m.integer(o, "currentTrack", 0, std::max<std::int64_t>(0, std::int64_t(a.tracks.size()) - 1)));
    a.beat = static_cast<int>(m.integer(o, "beat", 0, 0x7FFFFFFF));
}

// Version 20: one row of frames, which becomes the only track; its layers are found when the file is read (normalize).
void parseOneTrack(const QJsonObject& o, Animation& a, TreeBuilder& builder, ManifestReader& m, QSet<int>& frameIds) {
    AnimationTrack t;
    t.id = 1;
    t.name = QStringLiteral("Track 1");
    if (!parseFrames(o.value(QLatin1String("frames")), builder, m, frameIds, t))
        return;
    t.current = static_cast<int>(m.integer(o, "current", 0, std::max<std::int64_t>(0, std::int64_t(t.frames.size()) - 1)));
    if (!trackIsWhole(t))
        m.fail(QStringLiteral("(animation current frame)"));
    a.beat = t.startBeat(t.current);
    a.tracks.push_back(std::move(t));
    a.nextTrackId = 2;
}

// Version 21's sound lane and video row become tracks: one sound track per row of clips, over the frames tracks, and the
// video's track at the bottom, hidden as the lane and row were.
void moveToSoundTracks(Animation& a, bool soundsHidden, bool videoHidden) {
    std::map<int, int> trackOfRow;
    for (SoundClip& c : a.sounds) {
        if (c.track > 0)
            continue;
        auto [it, added] = trackOfRow.emplace(c.track, 0);
        if (added) {
            AnimationTrack t;
            t.id = a.nextTrackId++;
            t.kind = TrackKind::Sound;
            t.name = c.name;
            t.hidden = soundsHidden;
            t.current = -1;
            it->second = t.id;
            a.tracks.push_back(std::move(t));
        }
        c.track = it->second;
    }
    if (a.video && a.videoTrack() < 0 && !a.tracks.empty()) {
        AnimationTrack t;
        t.id = a.nextTrackId++;
        t.kind = TrackKind::Video;
        t.name = QStringLiteral("Video");
        t.hidden = videoHidden;
        t.current = -1;
        a.tracks.insert(a.tracks.begin(), std::move(t));
        ++a.currentTrack;
    }
}

Animation parseAnimation(const QJsonValue& value, TreeBuilder& builder, ManifestReader& m) {
    Animation a;
    if (!value.isObject()) {
        m.fail(QStringLiteral("(animation)"));
        return a;
    }
    const QJsonObject o = value.toObject();
    const bool tracks = o.value(QLatin1String("tracks")).isArray();
    if ((!tracks && !o.value(QLatin1String("frames")).isArray()) || !o.value(QLatin1String("shared")).isArray() ||
        !o.value(QLatin1String("sounds")).isArray()) {
        m.fail(QStringLiteral("(animation)"));
        return a;
    }
    QSet<int> frameIds;
    if (tracks)
        parseTracks(o, a, builder, m, frameIds);
    else
        parseOneTrack(o, a, builder, m, frameIds);
    if (!m.ok)
        return a;
    for (const auto& id : o.value(QLatin1String("shared")).toArray()) {
        if (!id.isDouble() || !builder.ids.contains(static_cast<std::uint32_t>(id.toDouble())))
            m.fail(QStringLiteral("(shared layers)"));
        a.shared.emplace_back(static_cast<std::uint32_t>(id.toDouble()));
    }
    a.fps = m.real(o, "fps");
    if (!(a.fps >= Animation::kMinFps && a.fps <= Animation::kMaxFps))
        m.fail(QStringLiteral("(animation fps)"));
    a.loop = m.boolean(o, "loop");
    // Files from before the play range and speed: all of the animation, at its rate.
    if (o.contains(QLatin1String("playStart")))
        a.playStart = static_cast<int>(m.integer(o, "playStart", 0, 0x7FFFFFFF));
    if (o.contains(QLatin1String("playEnd")))
        a.playEnd = static_cast<int>(m.integer(o, "playEnd", -1, 0x7FFFFFFF));
    if (o.contains(QLatin1String("playbackSpeed"))) {
        const double speed = m.real(o, "playbackSpeed");
        a.view.speed = std::clamp(speed, TimelineView::kMinSpeed, TimelineView::kMaxSpeed);
    }
    a.nextFrameId = static_cast<int>(m.integer(o, "nextFrameId", 1, kMaxFrames * 100 + 1));
    a.nextSoundId = static_cast<int>(m.integer(o, "nextSoundId", 1, 0x7FFFFFFF));
    for (int id : frameIds)
        if (id >= a.nextFrameId)
            m.fail(QStringLiteral("(animation frame ids)"));
    if (o.contains(QLatin1String("onionSkin"))) {
        const QJsonObject onion = o.value(QLatin1String("onionSkin")).toObject();
        a.view.onionSkin = m.boolean(onion, "on");
        a.view.onionBefore = static_cast<int>(m.integer(onion, "before", 0, 10));
        a.view.onionAfter = static_cast<int>(m.integer(onion, "after", 0, 10));
        a.view.onionOpacity = static_cast<int>(m.integer(onion, "opacity", 0, 100));
    }
    // Files from before each canvas had its own timeline open it for an animation.
    a.view.timelineShown = o.contains(QLatin1String("timelineShown")) ? m.boolean(o, "timelineShown") : a.isAnimation();
    for (const auto& id : o.value(QLatin1String("collapsedTracks")).toArray())  // none in files from before folding
        if (id.isDouble())
            a.view.collapsed.insert(id.toInt());
    QSet<int> soundIds;
    for (const auto& sv : o.value(QLatin1String("sounds")).toArray()) {
        const QJsonObject so = sv.toObject();
        SoundClip c;
        c.id = static_cast<int>(m.integer(so, "id", 1, 0x7FFFFFFF));
        c.name = so.value(QLatin1String("name")).toString();
        c.startBeat = static_cast<int>(m.integer(so, "startBeat", 0, 0x7FFFFFFF));
        c.seconds = m.real(so, "seconds");
        if (so.contains(QLatin1String("trimStart")))  // files from before clips could be trimmed have none
            c.trimStart = m.real(so, "trimStart");
        if (so.contains(QLatin1String("trimEnd")))
            c.trimEnd = m.real(so, "trimEnd");
        // Version 21 kept clips in rows of one sound lane: each row becomes a sound track (moveToSoundTracks).
        c.track = so.contains(QLatin1String("track")) ? static_cast<int>(m.integer(so, "track", 1, 0x7FFFFFFF))
                                                      : -1 - static_cast<int>(m.integer(so, "row", 0, 1000));
        if (so.contains(QLatin1String("muted")))
            c.muted = m.boolean(so, "muted");
        if (soundIds.contains(c.id) || c.id >= a.nextSoundId || c.seconds < 0.0 || !(c.trimStart >= 0.0) || !(c.trimEnd >= 0.0))
            m.fail(QStringLiteral("(sound clips)"));
        soundIds.insert(c.id);
        a.sounds.push_back(std::move(c));
    }
    const bool soundsHidden = o.contains(QLatin1String("soundsHidden")) && m.boolean(o, "soundsHidden");
    const bool videoHidden = o.contains(QLatin1String("videoHidden")) && m.boolean(o, "videoHidden");
    if (o.contains(QLatin1String("video"))) {
        const QJsonObject vo = o.value(QLatin1String("video")).toObject();
        VideoReference v;
        v.path = vo.value(QLatin1String("path")).toString();
        v.sourceSize = QSize(static_cast<int>(m.integer(vo, "width", 1, 100000)), static_cast<int>(m.integer(vo, "height", 1, 100000)));
        v.sourceFps = m.real(vo, "sourceFps");
        v.seconds = m.real(vo, "seconds");
        v.hasSound = m.boolean(vo, "hasSound");
        v.fps = m.real(vo, "fps");
        v.startBeat = static_cast<int>(m.integer(vo, "startBeat", 0, 0x7FFFFFFF));
        v.opacity = static_cast<int>(m.integer(vo, "opacity", 0, 100));
        v.lossless = m.boolean(vo, "lossless");
        if (v.path.isEmpty() || !(v.fps > 0.0) || v.seconds < 0.0)
            m.fail(QStringLiteral("(video)"));
        a.video = v;
    }
    moveToSoundTracks(a, soundsHidden, videoHidden);
    return a;
}

struct Manifest {
    DocumentContent content;
    bool hasSelection = false;
};

FileResult<Manifest> parseManifest(const QByteArray& compressed) {
    using R = FileResult<Manifest>;
    if (compressed.size() < 4)
        return R::failure(corrupt(QStringLiteral("(manifest)")));
    const auto* z = reinterpret_cast<const std::uint8_t*>(compressed.constData());
    const std::uint32_t claimed = (std::uint32_t(z[0]) << 24) | (std::uint32_t(z[1]) << 16) | (std::uint32_t(z[2]) << 8) | z[3];
    if (claimed > kMaxManifest)
        return R::failure(corrupt(QStringLiteral("(manifest size)")));
    const QByteArray json = qUncompress(compressed);
    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(json, &err);
    if (json.isEmpty() || err.error != QJsonParseError::NoError || !doc.isObject())
        return R::failure(corrupt(QStringLiteral("(manifest)")));
    const QJsonObject root = doc.object();

    ManifestReader m;
    // Up to the largest side a canvas ever had (20000, in early versions), so that a canvas beyond today's limit says so.
    const int width = static_cast<int>(m.integer(root, "width", 1, kLargestEverCanvasSide));
    const int height = static_cast<int>(m.integer(root, "height", 1, kLargestEverCanvasSide));
    if (!m.ok)
        return R::failure(m.problem.isEmpty() ? corrupt({}) : FileError{FileError::Kind::Corrupt, m.problem});
    if (beyondCanvasLimit(QSize(width, height)))
        return R::failure(FileError::canvasTooLarge(QSize(width, height), false));

    Manifest out;
    out.content.state.layers = LayerStack(QSize(width, height));
    const QJsonObject b = root.value(QLatin1String("background")).toObject();
    const auto mode = backgroundModeFromKey(b.value(QLatin1String("mode")).toString());
    const QColor color(b.value(QLatin1String("color")).toString());
    if (!mode || !color.isValid())
        m.fail(QStringLiteral("(canvas background)"));
    else
        out.content.state.background = {*mode, QColor(color.red(), color.green(), color.blue())};
    const QJsonValue r = root.value(QLatin1String("resolution"));
    if (!r.isDouble() || !(r.toDouble() >= 1.0 && r.toDouble() <= 100000.0))
        m.fail(QStringLiteral("(resolution)"));
    else
        out.content.resolution = r.toDouble();
    const auto nextId = static_cast<std::uint32_t>(m.integer(root, "nextLayerId", 1, 0xFFFFFFFFLL));
    const auto current = static_cast<std::uint32_t>(m.integer(root, "currentLayer", 0, 0xFFFFFFFFLL));
    out.hasSelection = m.boolean(root, "hasSelection");
    if (root.contains(QLatin1String("view"))) {
        const QJsonObject v = root.value(QLatin1String("view")).toObject();
        ViewState view;
        view.zoom = m.real(v, "zoom");
        view.rotationDeg = m.real(v, "rotation");
        view.flipH = m.boolean(v, "flipH");
        view.pan = QPointF(m.real(v, "panX"), m.real(v, "panY"));
        if (view.zoom <= 0.0)
            m.fail(QStringLiteral("(view zoom)"));
        out.content.view = view;
    }
    if (!root.value(QLatin1String("layers")).isArray())
        m.fail(QStringLiteral("(layers)"));
    TreeBuilder builder{m, {}, 0, 0};
    if (m.ok)
        builder.buildNodes(root.value(QLatin1String("layers")).toArray(), 0, out.content.state.layers.root().children);
    if (m.ok && out.content.state.layers.root().children.empty())
        m.fail(QStringLiteral("(no layers)"));
    if (m.ok && root.contains(QLatin1String("animation")))
        out.content.state.animation = parseAnimation(root.value(QLatin1String("animation")), builder, m);
    if (m.ok && (nextId <= builder.maxId || !builder.ids.contains(current)))
        m.fail(QStringLiteral("(layer ids)"));
    if (!m.ok)
        return R::failure(FileError{FileError::Kind::Corrupt, m.problem});
    out.content.state.layers.setNextIdValue(nextId);
    out.content.state.current = LayerId(current);
    return out;
}

FileResult<> readLayerChunk(const Chunk& chunk, LayerNode& layer) {
    ByteReader r(chunk.payload, ByteOrder::Little);
    const std::uint32_t id = r.u32();
    const std::uint32_t count = r.u32();
    if (!r.ok() || id != layer.props.id.value())
        return FileResult<>::failure(corrupt(QStringLiteral("(layer chunk does not match the layer list)")));
    // A layer can hold pixels past the canvas's edge. Each tile is listed with its column, row and length (4 bytes each), so the
    // count can't be more than the bytes left hold: what is set aside for the list is then no more than the file's size.
    constexpr std::size_t kTileEntryBytes = 12;
    const QRect tileArea = layer.pixels.tileRange(layer.pixels.paintArea());
    if (count > std::size_t(tileArea.width()) * std::size_t(tileArea.height()) || count > r.remaining() / kTileEntryBytes)
        return FileResult<>::failure(corrupt(QStringLiteral("(too many tiles in layer \"%1\")").arg(layer.props.name)));

    struct Pending {
        int tx, ty;
        const std::uint8_t* blob;
        std::size_t size;
    };
    std::vector<Pending> pending;
    pending.reserve(count);
    std::unordered_set<TileCoord, TileCoordHash> seen;
    for (std::uint32_t i = 0; i < count; ++i) {
        const int tx = r.i32(), ty = r.i32();
        const std::uint32_t length = r.u32();
        const std::uint8_t* blob = r.span(length);
        if (!r.ok())
            return FileResult<>::failure(corrupt(QStringLiteral("(tile list of layer \"%1\")").arg(layer.props.name)));
        if (!tileArea.contains(tx, ty) || !seen.insert({tx, ty}).second)
            return FileResult<>::failure(corrupt(QStringLiteral("(tile position in layer \"%1\")").arg(layer.props.name)));
        pending.push_back({tx, ty, blob, length});
    }
    if (!r.atEnd())
        return FileResult<>::failure(corrupt(QStringLiteral("(extra data in layer \"%1\")").arg(layer.props.name)));

    std::vector<std::shared_ptr<Tile>> tiles(pending.size());
    std::atomic<bool> bad{false};
    parallelFor(static_cast<int>(pending.size()), [&](int i) {
        if (bad.load(std::memory_order_relaxed))
            return;
        const Pending& p = pending[static_cast<std::size_t>(i)];
        auto tile = std::make_shared<Tile>();
        if (!decodeValues(p.blob, p.size, 4, tile->px.data()))
            bad = true;
        else
            tiles[static_cast<std::size_t>(i)] = std::move(tile);
    });
    if (bad)
        return FileResult<>::failure(corrupt(QStringLiteral("(tile data of layer \"%1\")").arg(layer.props.name)));
    for (std::size_t i = 0; i < pending.size(); ++i)
        if (!isAllZero(*tiles[i]))
            layer.pixels.setTile(pending[i].tx, pending[i].ty, std::move(tiles[i]));
    return {};
}

FileResult<SelectionMask> readCoverage(ByteReader& r, const QSize& size, const QString& what) {
    using R = FileResult<SelectionMask>;
    SelectionMask mask(size);
    const std::uint32_t count = r.u32();
    if (!r.ok() || count > std::size_t(mask.tilesX()) * std::size_t(mask.tilesY()))
        return R::failure(corrupt(QStringLiteral("(%1 tile count)").arg(what)));
    struct Pending {
        int tx, ty;
        const std::uint8_t* blob;  // null: fully selected
        std::size_t size;
    };
    std::vector<Pending> pending;
    std::unordered_set<TileCoord, TileCoordHash> seen;
    for (std::uint32_t i = 0; i < count; ++i) {
        const int tx = r.i32(), ty = r.i32();
        const std::uint8_t kind = r.u8();
        const std::uint8_t* blob = nullptr;
        std::size_t length = 0;
        if (kind == 0) {
            length = r.u32();
            blob = r.span(length);
        } else if (kind != 1) {
            return R::failure(corrupt(QStringLiteral("(%1 tile kind)").arg(what)));
        }
        if (!r.ok())
            return R::failure(corrupt(QStringLiteral("(%1 tile list)").arg(what)));
        if (tx < 0 || ty < 0 || tx >= mask.tilesX() || ty >= mask.tilesY() || !seen.insert({tx, ty}).second)
            return R::failure(corrupt(QStringLiteral("(%1 tile position)").arg(what)));
        pending.push_back({tx, ty, blob, length});
    }
    if (!r.atEnd())
        return R::failure(corrupt(QStringLiteral("(extra data in %1)").arg(what)));

    std::vector<std::shared_ptr<SelectionTile>> tiles(pending.size());
    std::atomic<bool> bad{false};
    parallelFor(static_cast<int>(pending.size()), [&](int i) {
        const Pending& p = pending[static_cast<std::size_t>(i)];
        if (!p.blob)
            return;
        auto tile = std::make_shared<SelectionTile>();
        if (!decodeValues(p.blob, p.size, 1, tile->v.data())) {
            bad = true;
            return;
        }
        // Coverage outside the canvas is always 0.
        const QRect inside = TileCoord{p.tx, p.ty}.pixelRect().intersected(QRect(QPoint(0, 0), size));
        for (int y = 0; y < SelectionTile::kSize; ++y)
            for (int x = 0; x < SelectionTile::kSize; ++x) {
                const bool in = inside.contains(p.tx * SelectionTile::kSize + x, p.ty * SelectionTile::kSize + y);
                std::uint16_t& v = tile->v[static_cast<std::size_t>(y * SelectionTile::kSize + x)];
                v = in ? std::min<std::uint16_t>(v, SelectionMask::kOne) : 0;
            }
        tiles[static_cast<std::size_t>(i)] = std::move(tile);
    });
    if (bad)
        return R::failure(corrupt(QStringLiteral("(%1 tile data)").arg(what)));
    for (std::size_t i = 0; i < pending.size(); ++i) {
        if (pending[i].blob)
            mask.setTile(pending[i].tx, pending[i].ty, std::move(tiles[i]));
        else
            mask.setUniform(pending[i].tx, pending[i].ty, SelectionMask::kOne);
    }
    mask.squeeze();
    return mask;
}

FileResult<SelectionMask> readSelectionChunk(const Chunk& chunk, const QSize& size) {
    ByteReader r(chunk.payload, ByteOrder::Little);
    auto mask = readCoverage(r, size, QStringLiteral("the selection"));
    if (mask && mask.value().isEmpty())
        return FileResult<SelectionMask>::failure(corrupt(QStringLiteral("(the selection is empty)")));
    return mask;
}

// A layer mask: the layer's id, then its tiles. Unlike a selection it may be empty (it hides the whole layer).
FileResult<> readMaskChunk(const Chunk& chunk, LayerNode& layer, const QSize& size) {
    ByteReader r(chunk.payload, ByteOrder::Little);
    if (r.u32() != layer.props.id.value() || !r.ok())
        return FileResult<>::failure(corrupt(QStringLiteral("(mask chunk does not match the layer list)")));
    auto mask = readCoverage(r, size, QStringLiteral("the mask of layer \"%1\"").arg(layer.props.name));
    if (!mask)
        return FileResult<>::failure(mask.error());
    layer.mask = mask.value();
    return {};
}

}

namespace doodle_file {

bool writeChunk(QIODevice& device, const char type[4], const QByteArray& payload) {
    return doodlebold::writeChunk(device, type, payload);
}

FileResult<Chunk> readChunk(QIODevice& device) {
    return doodlebold::readChunk(device);
}

}

bool DoodleFormat::recognizes(QIODevice& device) const {
    const QByteArray head = device.read(static_cast<qint64>(kMagic.size()));
    return head.size() == static_cast<qint64>(kMagic.size()) && isOurMagic(head.constData());
}

FileResult<> DoodleFormat::write(const DocumentContent& content, QIODevice& device, const ExportOptions&) const {
    const FileError failed{FileError::Kind::CannotWrite, device.errorString()};
    ByteWriter head(ByteOrder::Little);
    head.bytes(reinterpret_cast<const char*>(kMagic.data()), kMagic.size());
    const Animation& animation = content.state.animation;
    head.u32(hasAnimation(animation) ? kAnimationVersion : kVersion);
    const bool recording = content.timelapse && !content.timelapse->isEmpty();
    if (!writeAll(device, head.data().constData(), head.data().size()) || !writeChunk(device, kManifestChunk, manifest(content)))
        return FileResult<>::failure(failed);
    const auto writePixels = [&](const LayerNode& root) {
        std::vector<const LayerNode*> rasters;
        collectRastersPreorder(root, rasters);
        for (const LayerNode* layer : rasters) {
            if (!writeChunk(device, kLayerChunk, layerPayload(*layer)))
                return false;
            if (layer->mask && !writeChunk(device, kMaskChunk, coveragePayload(*layer->mask, layer->props.id)))
                return false;
        }
        for (const LayerNode* folder : maskedFolders(root))
            if (!writeChunk(device, kMaskChunk, coveragePayload(*folder->mask, folder->props.id)))
                return false;
        return true;
    };
    if (!writePixels(content.state.layers.root()))
        return FileResult<>::failure(failed);
    for (const AnimationTrack& track : animation.tracks) {
        for (const AnimationFrame& frame : track.frames) {
            LayerNode holder;
            holder.children = frame.layers;
            if (!writePixels(holder))
                return FileResult<>::failure(failed);
        }
    }
    if (content.state.selection && !writeChunk(device, kSelectionChunk, coveragePayload(*content.state.selection)))
        return FileResult<>::failure(failed);
    for (const SoundClip& clip : animation.sounds) {
        ByteWriter w(ByteOrder::Little);
        w.u32(static_cast<std::uint32_t>(clip.id));
        if (clip.data)
            w.bytes(*clip.data);
        if (!writeChunk(device, kSoundChunk, w.take()))
            return FileResult<>::failure(failed);
    }
    if (recording && !timelapse_file::write(device, *content.timelapse))
        return FileResult<>::failure(failed);
    if (!writeChunk(device, kEndChunk, {}))
        return FileResult<>::failure(failed);
    return {};
}

FileResult<LoadedDocument> DoodleFormat::read(QIODevice& device) const {
    using R = FileResult<LoadedDocument>;
    auto head = readExact(device, kHeaderSize);
    if (!head) {
        // A file shorter than the magic cannot be one of ours; one that starts with the magic and stops is damaged.
        return R::failure(FileError{FileError::Kind::NotADoodleFile, {}});
    }
    ByteReader h(head.value(), ByteOrder::Little);
    if (!isOurMagic(reinterpret_cast<const char*>(h.span(kMagic.size()))))
        return R::failure(FileError{FileError::Kind::NotADoodleFile, {}});
    const std::uint32_t version = h.u32();
    if (version != kVersion && version != kAnimationVersion && version != kOneTrackVersion && version != kFramesTracksVersion)
        return R::failure(FileError{FileError::Kind::UnsupportedVersion, QStringLiteral("(file version %1)").arg(version)});

    auto first = readChunk(device);
    if (!first)
        return R::failure(first.error());
    if (!first.value().is(kManifestChunk))
        return R::failure(corrupt(QStringLiteral("(the canvas description is missing)")));
    auto parsed = parseManifest(first.value().payload);
    if (!parsed)
        return R::failure(parsed.error());
    Manifest description = parsed.value();
    DocumentContent& content = description.content;

    // The pixels of a list of layers: LAYR (and LMSK) per raster layer in list order, then the folders' masks.
    const auto readPixels = [&](std::vector<LayerNode>& nodes) -> std::optional<FileError> {
        std::unordered_map<LayerId, LayerNode*> byId;
        std::vector<LayerNode*> rasters;
        const auto walk = [&](auto&& self, std::vector<LayerNode>& list) -> void {
            for (LayerNode& n : list) {
                byId[n.props.id] = &n;
                if (n.isFolder()) {
                    self(self, n.children);
                } else {
                    n.pixels = TiledImage(content.size());
                    rasters.push_back(&n);
                }
            }
        };
        walk(walk, nodes);
        for (LayerNode* layer : rasters) {
            auto chunk = readChunk(device);
            if (!chunk)
                return chunk.error();
            if (!chunk.value().is(kLayerChunk))
                return corrupt(QStringLiteral("(layer \"%1\" has no pixel data)").arg(layer->props.name));
            if (auto done = readLayerChunk(chunk.value(), *layer); !done)
                return done.error();
            if (layer->mask) {
                auto maskChunk = readChunk(device);
                if (!maskChunk)
                    return maskChunk.error();
                if (!maskChunk.value().is(kMaskChunk))
                    return corrupt(QStringLiteral("(the mask of layer \"%1\" is missing)").arg(layer->props.name));
                if (auto done = readMaskChunk(maskChunk.value(), *layer, content.size()); !done)
                    return done.error();
            }
        }
        LayerNode holder;
        holder.children = nodes;  // only to list the masked folders, in order
        for (const LayerNode* f : maskedFolders(holder)) {
            LayerNode& folder = *byId.at(f->props.id);
            auto maskChunk = readChunk(device);
            if (!maskChunk)
                return maskChunk.error();
            if (!maskChunk.value().is(kMaskChunk))
                return corrupt(QStringLiteral("(the mask of folder \"%1\" is missing)").arg(folder.props.name));
            if (auto done = readMaskChunk(maskChunk.value(), folder, content.size()); !done)
                return done.error();
        }
        return std::nullopt;
    };
    if (auto failed = readPixels(content.state.layers.root().children))
        return R::failure(*failed);
    for (AnimationTrack& track : content.state.animation.tracks)
        for (AnimationFrame& frame : track.frames)
            if (auto failed = readPixels(frame.layers))
                return R::failure(*failed);
    if (description.hasSelection) {
        auto chunk = readChunk(device);
        if (!chunk)
            return R::failure(chunk.error());
        if (!chunk.value().is(kSelectionChunk))
            return R::failure(corrupt(QStringLiteral("(the selection is missing)")));
        auto mask = readSelectionChunk(chunk.value(), content.size());
        if (!mask)
            return R::failure(mask.error());
        content.state.selection = mask.value();
    }
    QStringList notes;
    auto end = readChunk(device);
    for (SoundClip& clip : content.state.animation.sounds) {
        if (!end)
            return R::failure(end.error());
        if (!end.value().is(kSoundChunk))
            return R::failure(corrupt(QStringLiteral("(the sound \"%1\" is missing)").arg(clip.name)));
        ByteReader r(end.value().payload, ByteOrder::Little);
        if (end.value().payload.size() < 4 || r.u32() != static_cast<std::uint32_t>(clip.id))
            return R::failure(corrupt(QStringLiteral("(sound chunk does not match the sound list)")));
        clip.data = std::make_shared<const QByteArray>(end.value().payload.mid(4));
        end = readChunk(device);
    }
    if (end && end.value().is(timelapse_file::kHeaderChunk)) {
        auto recording = timelapse_file::read(device, end.value(), content.size());
        if (!recording)
            return R::failure(recording.error());
        if (recording.value().isEmpty())
            notes << QCoreApplication::translate("DoodleFormat",
                                                 "The timelapse recording does not belong to this picture and was left out.");
        else
            content.timelapse = std::make_shared<const Timelapse>(recording.value());
        end = readChunk(device);
    }
    if (!end)
        return R::failure(end.error());
    if (!end.value().is(kEndChunk) || !end.value().payload.isEmpty())
        return R::failure(corrupt(QStringLiteral("(unexpected data after the last layer)")));
    char extra;
    if (device.read(&extra, 1) > 0)
        return R::failure(corrupt(QStringLiteral("(data after the end of the file)")));
    animation_ops::normalize(content.state);  // a version 20 file's layers join its only track
    return LoadedDocument{std::move(content), notes};
}

}
