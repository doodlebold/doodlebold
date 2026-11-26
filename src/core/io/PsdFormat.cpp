#include "core/io/PsdFormat.h"

#include "core/io/ByteIO.h"
#include "core/io/ImageConvert.h"
#include "core/layers/Compositor.h"
#include "core/util/ParallelFor.h"

#include <QSet>

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstring>
#include <new>

namespace doodlebold {

namespace psd {

QByteArray packBitsRow(const std::uint8_t* row, int size) {
    QByteArray out;
    out.reserve(size + size / 128 + 2);
    int i = 0;
    while (i < size) {
        int run = 1;
        while (i + run < size && run < 128 && row[i + run] == row[i])
            ++run;
        if (run >= 3) {
            out.append(static_cast<char>(1 - run));  // header 257 - run: repeat the next byte `run` times
            out.append(static_cast<char>(row[i]));
            i += run;
            continue;
        }
        int end = i + 1;
        while (end < size && end - i < 128 && !(end + 2 < size && row[end] == row[end + 1] && row[end] == row[end + 2]))
            ++end;
        out.append(static_cast<char>(end - i - 1));
        out.append(reinterpret_cast<const char*>(row + i), end - i);
        i = end;
    }
    return out;
}

bool unpackBitsRow(const std::uint8_t* in, std::size_t inSize, std::uint8_t* out, int size) {
    std::size_t i = 0;
    int o = 0;
    while (o < size) {
        if (i >= inSize)
            return false;
        const int header = static_cast<std::int8_t>(in[i++]);  // NOLINT(bugprone-signed-char-misuse): PackBits' header byte is signed
        if (header >= 0) {
            const int n = header + 1;
            if (i + static_cast<std::size_t>(n) > inSize || o + n > size)
                return false;
            std::memcpy(out + o, in + i, static_cast<std::size_t>(n));
            i += static_cast<std::size_t>(n);
            o += n;
        } else if (header != -128) {
            const int n = 1 - header;
            if (i >= inSize || o + n > size)
                return false;
            std::memset(out + o, in[i++], static_cast<std::size_t>(n));
            o += n;
        }
    }
    return i == inSize;
}

}

namespace {

using io::ByteOrder;
using io::ByteReader;
using io::ByteWriter;

constexpr std::uint64_t kMaxFile = 1500ull << 20;
constexpr int kMaxLayers = 8189;  // also what fits comfortably in Photoshop
constexpr int kMaxDepth = LayerStack::kMaxFolderLevels;
constexpr int kMaxLayerSide = 60000;
constexpr std::uint64_t kMaxLayerArea = file_limits::kMaxLayerPixels;

// Image resource that carries the canvas background (a block in Adobe's range for plug-ins, 4000-4999): a format byte, the
// mode (the eight BackgroundMode entries) and the colour of the Color modes.
constexpr std::uint16_t kPaperResource = 0x0FA0;
constexpr std::uint16_t kResolutionResource = 0x03ED;

FileError corrupt(const QString& detail) {
    return {FileError::Kind::Corrupt, detail};
}
FileError unsupported(const QString& detail) {
    return {FileError::Kind::Unsupported, detail};
}

bool keyIs(const QByteArray& k, const char* s) {
    return k.size() == 4 && std::memcmp(k.constData(), s, 4) == 0;
}

// The mode for a PSD key; `known` is false for a key Doodlebold has no mode for (it then gives Normal). Several modes write the
// same key: without our own block (apmd) the last of them is taken, the one with the key's usual name.
BlendMode modeForKey(const QByteArray& key, bool* known) {
    *known = true;
    if (keyIs(key, "pass"))
        return BlendMode::PassThrough;
    for (int i = kBlendModeCount - 1; i >= 1; --i) {
        const BlendModeInfo& info = blendModeInfo(static_cast<BlendMode>(i));
        if (keyIs(key, info.psdKey))
            return info.mode;
    }
    *known = false;
    return BlendMode::Normal;
}

int percentFrom255(int v) {
    return (v * 100 + 127) / 255;
}
int to255(int percent) {
    return (percent * 255 + 50) / 100;
}

struct ParsedChannel {
    int id = 0;
    const std::uint8_t* data = nullptr;
    std::size_t size = 0;
};

struct ParsedLayer {
    int top = 0, left = 0, bottom = 0, right = 0;
    std::vector<ParsedChannel> channels;
    QByteArray blendKey;
    int opacity = 255;
    bool clipped = false;
    int flags = 0;
    QString name;
    std::optional<QString> unicodeName;
    int fill = 255;
    std::uint32_t lock = 0;
    int sectionType = 0;  // 0 plain, 1 open folder, 2 closed folder, 3 end marker
    QByteArray sectionKey;
    std::optional<BlendMode> doodleboldMode;
    // The layer mask: its rectangle, the value outside it and its flags (bit 1: switched off).
    bool hasMask = false;
    int maskTop = 0, maskLeft = 0, maskBottom = 0, maskRight = 0;
    int maskDefault = 255;
    int maskFlags = 0;
    bool hasVectorMask = false;  // channel -3 (a user mask from a vector mask): not read
    bool hasEffects = false;
    std::vector<std::pair<std::size_t, std::size_t>> channelRanges;

    std::int64_t width() const { return std::int64_t{right} - left; }
    std::int64_t height() const { return std::int64_t{bottom} - top; }
};

struct Header {
    bool large = false;  // PSB ("large document format", version 2): wider length fields (Adobe's specification)
    int channels = 0;
    int width = 0;
    int height = 0;
    int depth = 0;
};

QString utf16be(ByteReader& r, std::uint32_t units) {
    QString s;
    s.reserve(static_cast<qsizetype>(units));
    for (std::uint32_t i = 0; i < units; ++i)
        s.append(QChar(r.u16()));
    return s;
}

void skipToSignature(ByteReader& r) {
    // Tagged blocks are padded to 2 or 4 bytes depending on the writer: move on to the next signature.
    for (int skipped = 0; skipped < 4 && r.remaining() >= 4; ++skipped) {
        const std::uint8_t* p = r.peek(4);
        if (p && (std::memcmp(p, "8BIM", 4) == 0 || std::memcmp(p, "8B64", 4) == 0))
            return;
        r.skip(1);
    }
}

// Keys of layers that carry effects or are adjustment / fill layers: Doodlebold shows their pixels without the effect. Tagged
// blocks whose length is 8 bytes in a PSB (Adobe's specification).
bool hasLongLength(const QByteArray& k) {
    static const QSet<QByteArray> keys = {"LMsk", "Lr16", "Lr32", "Layr", "Mt16", "Mt32", "Mtrn",
                                          "Alph", "FMsk", "lnk2", "FEid", "FXid", "PxSD"};
    return keys.contains(k);
}

std::uint64_t length(ByteReader& r, bool eightBytes) {
    return eightBytes ? r.u64() : r.u32();
}

bool isEffectOrAdjustmentKey(const QByteArray& k) {
    static const QSet<QByteArray> keys = {"lfx2", "lrFX", "brit", "levl", "curv", "expA", "vibA", "hue2", "blnc", "blwh", "phfl", "mixr",
                                          "clrL", "nvrt", "post", "thrs", "grdm", "selc", "SoCo", "GdFl", "PtFl", "SoLd", "PlLd", "TySh"};
    return keys.contains(k);
}

FileResult<ParsedLayer> parseRecord(ByteReader& r, bool large) {
    using R = FileResult<ParsedLayer>;
    ParsedLayer l;
    l.top = r.i32();
    l.left = r.i32();
    l.bottom = r.i32();
    l.right = r.i32();
    const int channelCount = r.u16();
    if (!r.ok() || channelCount > 64 || l.width() < 0 || l.height() < 0)
        return R::failure(corrupt(QStringLiteral("(layer record)")));
    for (int i = 0; i < channelCount; ++i) {
        ParsedChannel c;
        c.id = r.i16();
        c.size = static_cast<std::size_t>(length(r, large));
        l.channels.push_back(c);
    }
    const QByteArray sig = r.bytes(4);
    l.blendKey = r.bytes(4);
    l.opacity = r.u8();
    l.clipped = r.u8() != 0;
    l.flags = r.u8();
    r.u8();
    const std::uint32_t extraLength = r.u32();
    ByteReader e = r.sub(extraLength);
    if (!r.ok() || sig != "8BIM")
        return R::failure(corrupt(QStringLiteral("(layer record)")));

    const std::uint32_t maskLength = e.u32();
    {
        ByteReader m = e.sub(maskLength);
        if (maskLength >= 18) {
            l.hasMask = true;
            l.maskTop = m.i32(), l.maskLeft = m.i32(), l.maskBottom = m.i32(), l.maskRight = m.i32();
            l.maskDefault = m.u8();
            l.maskFlags = m.u8();
        }
    }
    e.skip(e.u32());  // blending ranges
    const int nameLength = e.u8();
    l.name = QString::fromLatin1(e.bytes(static_cast<std::size_t>(nameLength)));
    e.skip(std::min<std::size_t>(e.remaining(), static_cast<std::size_t>((4 - (1 + nameLength) % 4) % 4)));
    if (!e.ok())
        return R::failure(corrupt(QStringLiteral("(layer name)")));

    while (e.remaining() >= 12) {
        const QByteArray bsig = e.bytes(4);
        const QByteArray key = e.bytes(4);
        ByteReader b = e.sub(static_cast<std::size_t>(length(e, large && hasLongLength(key))));
        if (!e.ok() || (bsig != "8BIM" && bsig != "8B64"))
            return R::failure(corrupt(QStringLiteral("(layer tagged block)")));
        if (keyIs(key, "luni")) {
            const std::uint32_t units = b.u32();
            if (std::uint64_t{units} * 2 <= b.remaining()) {
                QString name = utf16be(b, units);
                const qsizetype nul = name.indexOf(QChar(0));
                l.unicodeName = nul >= 0 ? name.left(nul) : name;
            }
        } else if (keyIs(key, "lsct") || keyIs(key, "lsdk")) {
            l.sectionType = static_cast<int>(b.u32());
            if (b.remaining() >= 8 && b.bytes(4) == "8BIM")
                l.sectionKey = b.bytes(4);
        } else if (keyIs(key, "lspf")) {
            l.lock = b.u32();
        } else if (keyIs(key, "iOpa")) {
            l.fill = b.u8();
        } else if (keyIs(key, "apmd")) {
            // A format byte, the length of the mode's name, then the name.
            const int version = b.u8();
            const int value = b.u8();
            if (version == 2)
                l.doodleboldMode = blendModeFromKey(QString::fromLatin1(b.bytes(static_cast<std::size_t>(value))));
        } else if (isEffectOrAdjustmentKey(key)) {
            l.hasEffects = true;
        }
        skipToSignature(e);
    }
    bool maskChannel = false;
    for (const ParsedChannel& c : l.channels) {
        maskChannel = maskChannel || c.id == -2;
        l.hasVectorMask = l.hasVectorMask || c.id == -3;
    }
    l.hasMask = l.hasMask && maskChannel;
    if (l.hasMask && (std::int64_t{l.maskRight} - l.maskLeft < 0 || std::int64_t{l.maskBottom} - l.maskTop < 0))
        return R::failure(corrupt(QStringLiteral("(layer mask rectangle)")));
    return l;
}

bool decodeChannel(const std::uint8_t* data, std::size_t size, int w, int h, int bps, bool large, QByteArray& plane) {
    const std::size_t rowBytes = static_cast<std::size_t>(w) * static_cast<std::size_t>(bps);
    const std::size_t total = rowBytes * static_cast<std::size_t>(h);
    plane.resize(static_cast<qsizetype>(total));
    auto* out = reinterpret_cast<std::uint8_t*>(plane.data());
    ByteReader r(data, size, ByteOrder::Big);
    const int compression = r.u16();
    if (!r.ok())
        return false;
    switch (compression) {
    case 0: {
        const std::uint8_t* raw = r.span(total);
        if (!raw)
            return false;
        std::memcpy(out, raw, total);
        return true;
    }
    case 1: {
        std::vector<std::uint32_t> counts(static_cast<std::size_t>(h));
        for (auto& c : counts)
            c = large ? r.u32() : r.u16();  // a PSB counts each row's bytes in 4 bytes
        if (!r.ok())
            return false;
        for (int y = 0; y < h; ++y) {
            const std::uint8_t* row = r.span(counts[static_cast<std::size_t>(y)]);
            if (!row || !psd::unpackBitsRow(row, counts[static_cast<std::size_t>(y)], out + static_cast<std::size_t>(y) * rowBytes,
                                            static_cast<int>(rowBytes)))
                return false;
        }
        return true;
    }
    case 2:
    case 3: {
        QByteArray z;
        z.reserve(static_cast<qsizetype>(r.remaining()) + 4);
        for (int shift = 24; shift >= 0; shift -= 8)
            z.append(static_cast<char>((total >> shift) & 0xFF));
        // The size is read first: r.span() uses the bytes up, and a call's arguments may be worked out in any order (Clang reads
        // the size after the span, and so appended nothing).
        const std::size_t packed = r.remaining();
        z.append(reinterpret_cast<const char*>(r.span(packed)), static_cast<qsizetype>(packed));
        const QByteArray raw = qUncompress(z);
        if (static_cast<std::size_t>(raw.size()) != total)
            return false;
        std::memcpy(out, raw.constData(), total);
        if (compression == 3) {  // prediction: each sample is stored as the difference to its left neighbour
            for (int y = 0; y < h; ++y) {
                std::uint8_t* row = out + static_cast<std::size_t>(y) * rowBytes;
                if (bps == 1) {
                    for (int x = 1; x < w; ++x)
                        row[x] = static_cast<std::uint8_t>(row[x] + row[x - 1]);
                } else {
                    for (int x = 1; x < w; ++x) {
                        const unsigned prev = (row[2 * (x - 1)] << 8) | row[2 * (x - 1) + 1];
                        const unsigned cur = ((row[2 * x] << 8) | row[2 * x + 1]) + prev;
                        row[2 * x] = static_cast<std::uint8_t>(cur >> 8);
                        row[2 * x + 1] = static_cast<std::uint8_t>(cur);
                    }
                }
            }
        }
        return true;
    }
    default: return false;
    }
}

QImage pictureFromPlanes(const QByteArray* r, const QByteArray* g, const QByteArray* b, const QByteArray* a, int w, int h, int bps) {
    QImage img(w, h, bps == 1 ? QImage::Format_RGBA8888 : QImage::Format_RGBA64);
    if (img.isNull())
        return img;
    const QByteArray* planes[4] = {r, g, b, a};
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const std::size_t i =
                (static_cast<std::size_t>(y) * static_cast<std::size_t>(w) + static_cast<std::size_t>(x)) * static_cast<std::size_t>(bps);
            for (int c = 0; c < 4; ++c) {
                const auto* p = planes[c] ? reinterpret_cast<const std::uint8_t*>(planes[c]->constData()) + i : nullptr;
                if (bps == 1)
                    img.scanLine(y)[x * 4 + c] = p ? p[0] : 255;
                else
                    reinterpret_cast<std::uint16_t*>(img.scanLine(y))[x * 4 + c] =
                        static_cast<std::uint16_t>(p ? (p[0] << 8) | p[1] : 65535);
            }
        }
    }
    return img;
}

FileResult<TiledImage> layerPixels(const ParsedLayer& l, const Header& h, const QSize& canvas, file_limits::DecodeBudget& budget) {
    using R = FileResult<TiledImage>;
    const std::int64_t w = l.width(), hgt = l.height();
    if (w == 0 || hgt == 0)
        return TiledImage(canvas);
    if (w > kMaxLayerSide || hgt > kMaxLayerSide || std::uint64_t(w) * std::uint64_t(hgt) > kMaxLayerArea)
        return R::failure({FileError::Kind::TooLarge, QStringLiteral("(layer \"%1\")").arg(l.unicodeName.value_or(l.name))});
    const QRect rect(l.left, l.top, static_cast<int>(w), static_cast<int>(hgt));
    if (!rect.intersects(QRect(QPoint(0, 0), canvas)))
        return TiledImage(canvas);
    if (!budget.take(w * hgt * file_limits::kPixelBytes))
        return R::failure({FileError::Kind::TooLarge, QStringLiteral("(layer \"%1\")").arg(l.unicodeName.value_or(l.name))});
    const int bps = h.depth / 8;
    QByteArray planes[4];
    bool have[4] = {false, false, false, false};
    for (const ParsedChannel& c : l.channels) {
        const int slot = c.id == -1 ? 3 : c.id;
        if (slot < 0 || slot > 3)
            continue;  // masks
        if (!decodeChannel(c.data, c.size, static_cast<int>(w), static_cast<int>(hgt), bps, h.large, planes[slot]))
            return R::failure(corrupt(QStringLiteral("(pixels of layer \"%1\")").arg(l.unicodeName.value_or(l.name))));
        have[slot] = true;
    }
    if (!have[0] || !have[1] || !have[2])
        return R::failure(corrupt(QStringLiteral("(layer \"%1\" lacks a colour channel)").arg(l.unicodeName.value_or(l.name))));
    const QImage picture = pictureFromPlanes(&planes[0], &planes[1], &planes[2], have[3] ? &planes[3] : nullptr, static_cast<int>(w),
                                             static_cast<int>(hgt), bps);
    if (picture.isNull())
        return R::failure({FileError::Kind::TooLarge, {}});
    return io::placeOnCanvas(io::layerFromImage(picture), canvas, rect.topLeft());
}

FileResult<SelectionMask> layerMask(const ParsedLayer& l, const Header& h, const QSize& canvas) {
    using R = FileResult<SelectionMask>;
    SelectionMask mask(canvas);
    const int outside = fromUnorm8(l.maskDefault);
    for (int ty = 0; ty < mask.tilesY(); ++ty)
        for (int tx = 0; tx < mask.tilesX(); ++tx)
            mask.setUniform(tx, ty, outside);
    const std::int64_t w = std::int64_t{l.maskRight} - l.maskLeft, hgt = std::int64_t{l.maskBottom} - l.maskTop;
    const QString name = l.unicodeName.value_or(l.name);
    if (w > 0 && hgt > 0) {
        if (w > kMaxLayerSide || hgt > kMaxLayerSide || std::uint64_t(w) * std::uint64_t(hgt) > kMaxLayerArea)
            return R::failure({FileError::Kind::TooLarge, QStringLiteral("(mask of layer \"%1\")").arg(name)});
        const ParsedChannel* channel = nullptr;
        for (const ParsedChannel& c : l.channels)
            if (c.id == -2)
                channel = &c;
        const int bps = h.depth / 8;
        QByteArray plane;
        if (!channel || !decodeChannel(channel->data, channel->size, int(w), int(hgt), bps, h.large, plane))
            return R::failure(corrupt(QStringLiteral("(mask of layer \"%1\")").arg(name)));
        const QRect rect(l.maskLeft, l.maskTop, int(w), int(hgt));
        const QRect part = rect.intersected(mask.rect());
        std::vector<std::uint16_t> row(std::size_t(std::max(0, part.width())));
        const auto* bytes = reinterpret_cast<const std::uint8_t*>(plane.constData());
        for (int y = part.top(); y <= part.bottom(); ++y) {
            const std::size_t at = std::size_t(y - rect.top()) * std::size_t(w) + std::size_t(part.left() - rect.left());
            for (int x = 0; x < part.width(); ++x) {
                const std::size_t i = (at + std::size_t(x)) * std::size_t(bps);
                row[std::size_t(x)] = std::uint16_t(bps == 1 ? fromUnorm8(bytes[i]) : (bytes[i] << 8 | bytes[i + 1]));
            }
            mask.writeRow(y, part.left(), part.width(), row.data());
        }
    }
    mask.squeeze();
    return mask;
}

void applyProps(LayerProps& p, const ParsedLayer& l, bool folder, QStringList& notes, int& unknownModes) {
    p.name = l.unicodeName.value_or(l.name);
    p.visible = (l.flags & 2) == 0;
    p.clipping = l.clipped;
    p.preserveOpacity = (l.lock & 0x1) != 0;
    p.locked = (l.lock & 0x80000000u) != 0 || (l.lock & 0x2) != 0;
    const QByteArray key = folder && l.sectionKey.size() == 4 ? l.sectionKey : l.blendKey;
    bool known = true;
    BlendMode mode = modeForKey(key, &known);
    if (!known) {
        ++unknownModes;
        if (unknownModes <= 3)
            notes << QCoreApplication::translate("PsdFormat",
                                                 R"(Layer "%1": blend mode "%2" is not available in Doodlebold; Normal is used.)")
                         .arg(p.name, QString::fromLatin1(key));
    }
    if (l.doodleboldMode && keyIs(key, blendModeInfo(*l.doodleboldMode).psdKey))
        mode = *l.doodleboldMode;
    if (folder) {
        if (mode != BlendMode::PassThrough && keyIs(key, "pass"))
            mode = BlendMode::PassThrough;
    } else if (mode == BlendMode::PassThrough) {
        mode = BlendMode::Normal;
    }
    p.mode = mode;
    // Opacity and Fill both dim the layer.
    const int opacity255 = l.fill < 255 ? (l.opacity * l.fill + 127) / 255 : l.opacity;
    p.opacity = percentFrom255(opacity255);
    p.expanded = l.sectionType != 2;
    p.maskEnabled = (l.maskFlags & 0x2) == 0;
}

FileResult<LoadedDocument> readImpl(QIODevice& device) {
    using R = FileResult<LoadedDocument>;
    if (!device.isSequential() && static_cast<std::uint64_t>(device.size()) > kMaxFile)
        return R::failure({FileError::Kind::TooLarge, QStringLiteral("(file)")});
    const QByteArray data = device.readAll();
    ByteReader r(data, ByteOrder::Big);

    if (r.bytes(4) != "8BPS")
        return R::failure({FileError::Kind::UnknownFormat, QCoreApplication::translate("PsdFormat", "(not a Photoshop file)")});
    const int version = r.u16();
    r.skip(6);
    Header h;
    h.channels = r.u16();
    h.height = static_cast<int>(r.u32());
    h.width = static_cast<int>(r.u32());
    h.depth = r.u16();
    const int mode = r.u16();
    if (!r.ok())
        return R::failure({FileError::Kind::Truncated, {}});
    h.large = version == 2;
    if (version != 1 && version != 2)
        return R::failure(corrupt(QStringLiteral("(version %1)").arg(version)));
    if (h.width <= 0 || h.height <= 0 || h.width > 300000 || h.height > 300000)
        return R::failure(corrupt(QStringLiteral("(canvas size)")));
    if (beyondCanvasLimit(QSize(h.width, h.height)))
        return R::failure(FileError::canvasTooLarge(QSize(h.width, h.height), false));
    if (h.depth != 8 && h.depth != 16)
        return R::failure(
            unsupported(QCoreApplication::translate("PsdFormat", "(%1 bits per channel; Doodlebold reads 8 and 16)").arg(h.depth)));
    if (mode != 3)
        return R::failure(
            unsupported(QCoreApplication::translate("PsdFormat", "(only RGB documents can be opened, not colour mode %1)").arg(mode)));
    if (h.channels < 3 || h.channels > 56)
        return R::failure(corrupt(QStringLiteral("(channel count)")));

    r.skip(r.u32());
    ByteReader resources = r.sub(r.u32());
    ByteReader layerSection = r.sub(static_cast<std::size_t>(length(r, h.large)));
    if (!r.ok())
        return R::failure({FileError::Kind::Truncated, {}});

    LoadedDocument out;
    while (resources.remaining() >= 12) {
        if (resources.bytes(4) != "8BIM")
            break;
        const int id = resources.u16();
        const int nameLength = resources.u8();
        resources.skip(static_cast<std::size_t>(nameLength + ((nameLength + 1) % 2)));
        const std::uint32_t length = resources.u32();
        ByteReader block = resources.sub(length);
        resources.skip(length % 2 && resources.remaining() > 0 ? 1 : 0);
        if (!resources.ok())
            break;  // a damaged resource section only costs the paper colour
        if (id == kPaperResource && length >= 5) {
            const int resourceVersion = block.u8();
            if (resourceVersion == 2) {
                const int backgroundMode = block.u8();
                const int cr = block.u8(), cg = block.u8(), cb = block.u8();
                if (backgroundMode < kBackgroundModeCount) {
                    out.content.state.background.mode = static_cast<BackgroundMode>(backgroundMode);
                    out.content.state.background.color = QColor(cr, cg, cb);
                }
            }
        } else if (id == kResolutionResource && length >= 16) {
            const double fixed = block.u32() / 65536.0;  // pixels per inch or per cm, 16.16
            const int unit = block.u16();                // 1 = per inch, 2 = per cm
            if (fixed > 0 && fixed < 100000 && (unit == 1 || unit == 2))
                out.content.resolution = unit == 1 ? fixed : fixed * 2.54;
        }
    }

    std::vector<ParsedLayer> layers;
    int layerCount = 0;
    const auto parseLayerInfo = [&](ByteReader info) -> FileResult<> {
        const int count = info.i16();
        if (!info.ok())
            return FileResult<>::failure(corrupt(QStringLiteral("(layer info)")));
        layerCount = std::abs(count);
        if (layerCount > 32767)
            return FileResult<>::failure(corrupt(QStringLiteral("(layer count)")));
        for (int i = 0; i < layerCount; ++i) {
            auto rec = parseRecord(info, h.large);
            if (!rec)
                return FileResult<>::failure(rec.error());
            layers.push_back(rec.value());
        }
        for (ParsedLayer& l : layers) {
            for (ParsedChannel& c : l.channels) {
                c.data = info.span(c.size);
                if (!c.data)
                    return FileResult<>::failure({FileError::Kind::Truncated, QStringLiteral("(layer pixels)")});
            }
        }
        return {};
    };
    if (layerSection.remaining() >= 4) {
        const std::uint64_t infoLength = length(layerSection, h.large);
        ByteReader info = layerSection.sub(static_cast<std::size_t>(infoLength));
        if (!layerSection.ok())
            return R::failure({FileError::Kind::Truncated, QStringLiteral("(layer section)")});
        if (infoLength > 0)
            if (auto done = parseLayerInfo(info); !done)
                return R::failure(done.error());
        if (layerSection.remaining() >= 4) {
            layerSection.skip(layerSection.u32());  // global layer mask info
            while (layers.empty() && layerSection.remaining() >= 12) {
                const QByteArray sig = layerSection.bytes(4);
                const QByteArray key = layerSection.bytes(4);
                ByteReader block = layerSection.sub(static_cast<std::size_t>(length(layerSection, h.large && hasLongLength(key))));
                if (!layerSection.ok() || (sig != "8BIM" && sig != "8B64"))
                    break;
                if (keyIs(key, "Lr16"))
                    if (auto done = parseLayerInfo(block); !done)
                        return R::failure(done.error());
                skipToSignature(layerSection);
            }
        }
    }

    const QSize canvas(h.width, h.height);
    LayerStack& stack = out.content.state.layers;
    stack = LayerStack(canvas);
    int unknownModes = 0, vectorMasks = 0, effects = 0;

    if (layers.empty()) {
        const int bps = h.depth / 8;
        const int compression = r.u16();
        const std::size_t planeBytes =
            static_cast<std::size_t>(h.width) * static_cast<std::size_t>(h.height) * static_cast<std::size_t>(bps);
        std::vector<QByteArray> planes(static_cast<std::size_t>(std::min(h.channels, 4)));
        if (!r.ok() || (compression != 0 && compression != 1))
            return R::failure(unsupported(QCoreApplication::translate("PsdFormat", "(merged image compression %1)").arg(compression)));
        std::vector<std::uint32_t> counts;
        if (compression == 1) {
            counts.resize(static_cast<std::size_t>(h.channels) * static_cast<std::size_t>(h.height));
            for (auto& c : counts)
                c = h.large ? r.u32() : r.u16();
        }
        for (int ch = 0; ch < h.channels; ++ch) {
            QByteArray plane;
            if (ch < 4)
                plane.resize(static_cast<qsizetype>(planeBytes));
            if (compression == 0) {
                const std::uint8_t* raw = r.span(planeBytes);
                if (!raw)
                    return R::failure({FileError::Kind::Truncated, QStringLiteral("(merged image)")});
                if (ch < 4)
                    std::memcpy(plane.data(), raw, planeBytes);
            } else {
                const std::size_t rowBytes = static_cast<std::size_t>(h.width) * static_cast<std::size_t>(bps);
                for (int y = 0; y < h.height; ++y) {
                    const std::uint32_t n =
                        counts[static_cast<std::size_t>(ch) * static_cast<std::size_t>(h.height) + static_cast<std::size_t>(y)];
                    const std::uint8_t* row = r.span(n);
                    if (!row)
                        return R::failure({FileError::Kind::Truncated, QStringLiteral("(merged image)")});
                    if (ch < 4 &&
                        !psd::unpackBitsRow(row, n, reinterpret_cast<std::uint8_t*>(plane.data()) + static_cast<std::size_t>(y) * rowBytes,
                                            static_cast<int>(rowBytes)))
                        return R::failure(corrupt(QStringLiteral("(merged image)")));
                }
            }
            if (ch < 4)
                planes[static_cast<std::size_t>(ch)] = std::move(plane);
        }
        const QImage picture =
            pictureFromPlanes(planes.data(), &planes[1], &planes[2], planes.size() > 3 ? &planes[3] : nullptr, h.width, h.height, bps);
        if (picture.isNull())
            return R::failure({FileError::Kind::TooLarge, {}});
        LayerNode layer = stack.makeRaster(stack.nextLayerName());
        layer.pixels = io::layerFromImage(picture);
        out.content.state.current = stack.insert(std::move(layer), kNoLayer, -1);
        return out;
    }

    // Layers arrive bottom → top; a section end marker opens a folder frame and the folder's own record closes it.
    std::vector<std::vector<LayerNode>> frames(1);
    file_limits::DecodeBudget budget;  // every layer's pixels together
    for (const ParsedLayer& l : layers) {
        if (l.sectionType == 3) {
            if (static_cast<int>(frames.size()) > kMaxDepth)
                return R::failure(corrupt(QStringLiteral("(folders nested deeper than %1 levels)").arg(kMaxDepth)));
            frames.emplace_back();
            continue;
        }
        if (l.hasVectorMask)
            ++vectorMasks;
        if (l.hasEffects)
            ++effects;
        std::optional<SelectionMask> mask;
        if (l.hasMask) {
            auto m = layerMask(l, h, canvas);
            if (!m)
                return R::failure(m.error());
            mask = m.value();
        }
        if (l.sectionType == 1 || l.sectionType == 2) {
            if (frames.size() < 2)
                return R::failure(corrupt(QStringLiteral("(folder structure)")));
            LayerNode folder = stack.makeFolder({});
            folder.children = std::move(frames.back());
            frames.pop_back();
            applyProps(folder.props, l, true, out.notes, unknownModes);
            folder.mask = std::move(mask);
            frames.back().push_back(std::move(folder));
            continue;
        }
        LayerNode layer = stack.makeRaster({});
        applyProps(layer.props, l, false, out.notes, unknownModes);
        auto pixels = layerPixels(l, h, canvas, budget);
        if (!pixels)
            return R::failure(pixels.error());
        layer.pixels = pixels.value();
        layer.mask = std::move(mask);
        frames.back().push_back(std::move(layer));
    }
    if (frames.size() != 1)
        return R::failure(corrupt(QStringLiteral("(folder structure)")));
    if (frames[0].empty())
        return R::failure(corrupt(QStringLiteral("(no layers)")));
    stack.root().children = std::move(frames[0]);
    const auto order = stack.displayOrder();
    out.content.state.current = order.front()->props.id;
    if (unknownModes > 3)
        out.notes << QCoreApplication::translate("PsdFormat", "%1 more layers use blend modes Doodlebold does not have; Normal is used.")
                         .arg(unknownModes - 3);
    if (vectorMasks > 0)
        out.notes << QCoreApplication::translate("PsdFormat", "Vector masks are not supported and were ignored (%n layer(s)).", nullptr,
                                                 vectorMasks);
    if (effects > 0)
        out.notes << QCoreApplication::translate(
            "PsdFormat",
            "%n layer(s) use effects, adjustments or text that Doodlebold does not have; their pixels are shown without the effect.",
            nullptr, effects);
    return out;
}

QByteArray latin1Name(const QString& name) {
    QByteArray b = name.toLatin1();  // characters outside Latin-1 become '?'; the Unicode name carries the real one
    return b.left(255);
}

void putPascalName(ByteWriter& w, const QByteArray& name) {
    w.u8(static_cast<std::uint32_t>(name.size()));
    w.bytes(name);
    w.padTo(4);
}

void putBlock(ByteWriter& w, const char* key, const QByteArray& data) {
    w.bytes("8BIM", 4);
    w.bytes(key, 4);
    w.u32(static_cast<std::uint32_t>(data.size()));
    w.bytes(data);
}

QByteArray u32Bytes(std::uint32_t v) {
    ByteWriter w(ByteOrder::Big);
    w.u32(v);
    return w.take();
}

struct Planes {
    QRect rect;
    QByteArray plane[4];
};

Planes extractPlanes(const TiledImage& img) {
    Planes p;
    p.rect = img.contentBounds(true);
    // What lies past the canvas's edge goes along while the layer stays within what the reader opens (kMaxLayerArea); past that
    // only the canvas's part is written, or the saved file wouldn't open again.
    if (static_cast<std::uint64_t>(p.rect.width()) * static_cast<std::uint64_t>(p.rect.height()) > kMaxLayerArea)
        p.rect &= img.rect();
    if (p.rect.isEmpty())
        return p;
    const std::size_t n = static_cast<std::size_t>(p.rect.width()) * static_cast<std::size_t>(p.rect.height());
    for (QByteArray& q : p.plane)
        q = QByteArray(static_cast<qsizetype>(n), '\0');
    constexpr int kT = Tile::kSize;
    for (const auto& [coord, tile] : img.tiles()) {
        if (!tile)
            continue;
        const QRect part = coord.pixelRect().intersected(p.rect);
        for (int y = part.top(); y <= part.bottom(); ++y)
            for (int x = part.left(); x <= part.right(); ++x) {
                const Channel* c = tile->at(x - coord.x * kT, y - coord.y * kT);
                const int a = c[3];
                const std::size_t i = static_cast<std::size_t>(y - p.rect.top()) * static_cast<std::size_t>(p.rect.width()) +
                                      static_cast<std::size_t>(x - p.rect.left());
                if (a == 0)
                    continue;
                for (int ch = 0; ch < 3; ++ch)
                    p.plane[ch][static_cast<qsizetype>(i)] = static_cast<char>(std::min(255, (c[ch] * 255 + a / 2) / a));
                p.plane[3][static_cast<qsizetype>(i)] = static_cast<char>(toUnorm8(a));
            }
    }
    return p;
}

QByteArray rleChannel(const QByteArray& plane, int w, int h, bool large) {
    ByteWriter counts(ByteOrder::Big);
    ByteWriter rows(ByteOrder::Big);
    counts.u16(1);
    for (int y = 0; y < h; ++y) {
        const QByteArray row = psd::packBitsRow(
            reinterpret_cast<const std::uint8_t*>(plane.constData()) + static_cast<std::size_t>(y) * static_cast<std::size_t>(w), w);
        if (large)
            counts.u32(static_cast<std::uint32_t>(row.size()));
        else
            counts.u16(static_cast<std::uint32_t>(row.size()));
        rows.bytes(row);
    }
    return counts.take() + rows.take();
}

QByteArray emptyChannel() {
    ByteWriter w(ByteOrder::Big);
    w.u16(0);
    return w.take();
}

struct OutRecord {
    QByteArray header;
    std::vector<QByteArray> channels;
};

constexpr int kChannelIds[4] = {0, 1, 2, -1};
constexpr int kMaskChannelId = -2;

// A layer mask as written into a PSD: the rectangle of everything that is not white, its values there
// at 8 bits, white (255) outside, flag bit 1 for a switched-off mask. A mask that is white everywhere has an empty rectangle.
struct MaskPlane {
    QRect rect;
    QByteArray plane;
};

MaskPlane extractMask(const SelectionMask& mask) {
    MaskPlane m;
    int x0 = INT_MAX, y0 = INT_MAX, x1 = INT_MIN, y1 = INT_MIN;
    std::array<std::uint16_t, SelectionTile::kSize> row{};
    for (int ty = 0; ty < mask.tilesY(); ++ty)
        for (int tx = 0; tx < mask.tilesX(); ++tx) {
            if (SelectionMask::isFull(mask.tile(tx, ty)))
                continue;
            const QRect r = SelectionMask::tileRect(tx, ty).intersected(mask.rect());
            for (int y = r.top(); y <= r.bottom(); ++y) {
                mask.row(y, r.left(), r.width(), row.data());
                for (int x = 0; x < r.width(); ++x)
                    if (row[std::size_t(x)] != SelectionMask::kOne) {
                        x0 = std::min(x0, r.left() + x), x1 = std::max(x1, r.left() + x);
                        y0 = std::min(y0, y), y1 = std::max(y1, y);
                    }
            }
        }
    if (x1 < x0)
        return m;
    m.rect = QRect(QPoint(x0, y0), QPoint(x1, y1));
    m.plane = QByteArray(qsizetype(m.rect.width()) * m.rect.height(), Qt::Uninitialized);
    std::vector<std::uint16_t> values(std::size_t(m.rect.width()));
    for (int y = m.rect.top(); y <= m.rect.bottom(); ++y) {
        mask.row(y, m.rect.left(), m.rect.width(), values.data());
        char* out = m.plane.data() + qsizetype(y - m.rect.top()) * m.rect.width();
        for (int x = 0; x < m.rect.width(); ++x)
            out[x] = static_cast<char>(toUnorm8(values[std::size_t(x)]));
    }
    return m;
}

OutRecord makeRecord(const LayerProps& p, const Planes* planes, bool marker, const QString& name, bool large,
                     const SelectionMask* mask = nullptr) {
    OutRecord rec;
    const bool folder = p.kind == LayerKind::Folder;
    const QRect rect = planes ? planes->rect : QRect();
    // A folder or a marker has no planes, and its rect is empty: four empty channels, without reading through the null pointer.
    if (rect.isEmpty())
        rec.channels.insert(rec.channels.end(), std::size(kChannelIds), emptyChannel());
    else
        for (const QByteArray& plane : planes->plane)
            rec.channels.push_back(rleChannel(plane, rect.width(), rect.height(), large));
    MaskPlane maskPlane;
    if (mask && !marker) {
        maskPlane = extractMask(*mask);
        rec.channels.push_back(maskPlane.rect.isEmpty()
                                   ? emptyChannel()
                                   : rleChannel(maskPlane.plane, maskPlane.rect.width(), maskPlane.rect.height(), large));
    }

    const BlendMode mode = marker ? BlendMode::Normal : p.mode;
    const int opacity255 = marker ? 255 : to255(p.opacity);
    ByteWriter w(ByteOrder::Big);
    w.i32(rect.isEmpty() ? 0 : rect.top());
    w.i32(rect.isEmpty() ? 0 : rect.left());
    w.i32(rect.isEmpty() ? 0 : rect.bottom() + 1);
    w.i32(rect.isEmpty() ? 0 : rect.right() + 1);
    w.u16(static_cast<std::uint32_t>(rec.channels.size()));
    for (std::size_t i = 0; i < rec.channels.size(); ++i) {
        w.i16(i < 4 ? kChannelIds[i] : kMaskChannelId);
        if (large)
            w.u64(static_cast<std::uint64_t>(rec.channels[i].size()));
        else
            w.u32(static_cast<std::uint32_t>(rec.channels[i].size()));
    }
    w.bytes("8BIM", 4);
    w.bytes(blendModeInfo(mode).psdKey, 4);
    w.u8(static_cast<std::uint32_t>(opacity255));
    w.u8(!marker && p.clipping ? 1 : 0);
    w.u8(8 | (!marker && !p.visible ? 2 : 0) | (!marker && p.preserveOpacity ? 1 : 0));
    w.u8(0);

    ByteWriter extra(ByteOrder::Big);
    if (rec.channels.size() > 4) {
        const QRect& m = maskPlane.rect;
        extra.u32(20);
        extra.i32(m.isEmpty() ? 0 : m.top());
        extra.i32(m.isEmpty() ? 0 : m.left());
        extra.i32(m.isEmpty() ? 0 : m.bottom() + 1);
        extra.i32(m.isEmpty() ? 0 : m.right() + 1);
        extra.u8(255);
        extra.u8(p.maskEnabled ? 0 : 2);
        extra.u16(0);
    } else {
        extra.u32(0);
    }
    extra.u32(0);
    putPascalName(extra, marker ? QByteArray("</Layer group>") : latin1Name(name));
    ByteWriter luni(ByteOrder::Big);
    const QString shown = marker ? QStringLiteral("</Layer group>") : name;
    luni.u32(static_cast<std::uint32_t>(shown.size()));
    for (const QChar c : shown)
        luni.u16(c.unicode());
    luni.padTo(4);
    putBlock(extra, "luni", luni.data());
    if (!marker)
        putBlock(extra, "lyid", u32Bytes(p.id.value()));
    if (folder || marker) {
        ByteWriter s(ByteOrder::Big);
        s.u32(marker ? 3 : (p.expanded ? 1 : 2));
        s.bytes("8BIM", 4);
        s.bytes(blendModeInfo(mode).psdKey, 4);
        putBlock(extra, "lsct", s.data());
    }
    if (!marker) {
        putBlock(extra, "lspf", u32Bytes((p.preserveOpacity ? 0x1u : 0u) | (p.locked ? 0x80000000u : 0u)));
        QByteArray ownMode = QByteArray("\x02", 1) + static_cast<char>(std::strlen(blendModeInfo(mode).key)) + blendModeInfo(mode).key;
        ownMode.append((4 - ownMode.size() % 4) % 4, '\0');
        putBlock(extra, "apmd", ownMode);
    }
    w.u32(static_cast<std::uint32_t>(extra.size()));
    w.bytes(extra.data());
    rec.header = w.take();
    return rec;
}

void collectRecords(const LayerNode& n, std::vector<OutRecord>& out, bool large, const QSize& canvas) {
    const SelectionMask* mask = n.mask ? &*n.mask : nullptr;
    if (!n.isFolder()) {
        // PSD has no layer paper: the texture is baked into the layer's pixels, so the file shows the picture the canvas shows
        const Planes planes = extractPlanes(n.pixels);
        out.push_back(makeRecord(n.props, &planes, false, n.props.name, large, mask));
        return;
    }
    out.push_back(makeRecord(n.props, nullptr, true, {}, large));
    for (const LayerNode& c : n.children)
        collectRecords(c, out, large, canvas);
    out.push_back(makeRecord(n.props, nullptr, false, n.props.name, large, mask));
}

}

bool PsdFormat::recognizes(QIODevice& device) const {
    return device.read(4) == "8BPS";
}

FileResult<LoadedDocument> PsdFormat::read(QIODevice& device) const {
    try {
        return readImpl(device);
    } catch (const std::bad_alloc&) {
        return FileResult<LoadedDocument>::failure({FileError::Kind::TooLarge, QStringLiteral("(out of memory)")});
    }
}

FileResult<> PsdFormat::write(const DocumentContent& content, QIODevice& device, const ExportOptions&) const {
    using R = FileResult<>;
    const LayerStack& stack = content.state.layers;
    if (stack.layerCount() > kMaxLayers)
        return R::failure(
            {FileError::Kind::TooLarge, QCoreApplication::translate("PsdFormat", "(a PSD holds at most %1 layers)").arg(kMaxLayers)});
    try {
        // The merged image, 8 bits: the canvas over its background colour (RGB), or with a transparent background the layers
        // over transparency with their alpha as a fourth channel (straight, not premultiplied, colour).
        const bool alpha = io::hasTransparentBackground(content);
        const int planeCount = alpha ? 4 : 3;
        const QImage picture = io::flattenedPicture(content, true);
        if (picture.isNull())
            return R::failure({FileError::Kind::TooLarge, {}});
        const int w = picture.width(), h = picture.height();

        std::vector<OutRecord> records;
        for (const LayerNode& n : stack.root().children)
            collectRecords(n, records, m_large, stack.size());

        ByteWriter out(ByteOrder::Big);
        out.bytes("8BPS", 4);
        out.u16(m_large ? 2 : 1);
        out.zeros(6);
        out.u16(static_cast<std::uint32_t>(planeCount));
        out.u32(static_cast<std::uint32_t>(h));
        out.u32(static_cast<std::uint32_t>(w));
        out.u16(8);
        out.u16(3);
        out.u32(0);

        ByteWriter paper(ByteOrder::Big);
        {
            // ResolutionInfo: horizontal and vertical resolution as 16.16 fixed point pixels per inch.
            const auto fixed = static_cast<std::uint32_t>(std::lround(std::clamp(content.resolution, 1.0, 32000.0) * 65536.0));
            paper.bytes("8BIM", 4);
            paper.u16(kResolutionResource);
            paper.u16(0);
            paper.u32(16);
            paper.u32(fixed);
            paper.u16(1);
            paper.u16(2);
            paper.u32(fixed);
            paper.u16(1);
            paper.u16(2);
            const CanvasBackground& bg = content.background();
            paper.bytes("8BIM", 4);
            paper.u16(kPaperResource);
            paper.u16(0);
            paper.u32(5);
            paper.u8(2);
            paper.u8(static_cast<std::uint32_t>(bg.mode));
            paper.u8(static_cast<std::uint32_t>(bg.color.red()));
            paper.u8(static_cast<std::uint32_t>(bg.color.green()));
            paper.u8(static_cast<std::uint32_t>(bg.color.blue()));
            paper.u8(0);
        }
        out.u32(static_cast<std::uint32_t>(paper.size()));
        out.bytes(paper.data());

        ByteWriter info(ByteOrder::Big);
        info.i16(static_cast<int>(records.size()));
        for (const OutRecord& rec : records)
            info.bytes(rec.header);
        for (const OutRecord& rec : records)
            for (const QByteArray& ch : rec.channels)
                info.bytes(ch);
        info.padTo(2);
        // Layer section: the info and its length, then the global mask length (its lengths are 8 bytes in a PSB).
        const std::uint64_t infoLength = static_cast<std::uint64_t>(info.size());
        if (m_large) {
            out.u64(infoLength + 8 + 4);
            out.u64(infoLength);
        } else {
            out.u32(static_cast<std::uint32_t>(infoLength + 4 + 4));
            out.u32(static_cast<std::uint32_t>(infoLength));
        }
        out.bytes(info.data());
        out.u32(0);

        out.u16(1);
        ByteWriter counts(ByteOrder::Big);
        ByteWriter rows(ByteOrder::Big);
        const int srcBytes = alpha ? 4 : 3;
        std::vector<QByteArray> encoded(static_cast<std::size_t>(h) * static_cast<std::size_t>(planeCount));
        parallelFor(h, [&](int y) {
            const std::uint8_t* src = picture.constScanLine(y);
            QByteArray plane(w, Qt::Uninitialized);
            for (int c = 0; c < planeCount; ++c) {
                for (int x = 0; x < w; ++x)
                    plane[x] = static_cast<char>(src[x * srcBytes + c]);
                encoded[static_cast<std::size_t>(c) * static_cast<std::size_t>(h) + static_cast<std::size_t>(y)] =
                    psd::packBitsRow(reinterpret_cast<const std::uint8_t*>(plane.constData()), w);
            }
        });
        for (const QByteArray& row : encoded) {
            if (m_large)
                counts.u32(static_cast<std::uint32_t>(row.size()));
            else
                counts.u16(static_cast<std::uint32_t>(row.size()));
        }
        for (const QByteArray& row : encoded)
            rows.bytes(row);
        out.bytes(counts.data());
        out.bytes(rows.data());

        // Not written past what the reader opens (kMaxFile): the save would succeed and the file not open again. Being below 4 GiB,
        // it also keeps a PSD's 32-bit section lengths from wrapping.
        if (static_cast<std::uint64_t>(out.data().size()) > kMaxFile)
            return R::failure({FileError::Kind::TooLarge, QStringLiteral("(the file would be %1 MB; at most %2 MB opens again)")
                                                              .arg(out.data().size() >> 20)
                                                              .arg(kMaxFile >> 20)});
        if (device.write(out.data()) != out.data().size())
            return R::failure({FileError::Kind::CannotWrite, device.errorString()});
        return {};
    } catch (const std::bad_alloc&) {
        return R::failure({FileError::Kind::TooLarge, QStringLiteral("(out of memory)")});
    }
}

}
