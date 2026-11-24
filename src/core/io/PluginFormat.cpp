#include "core/io/PluginFormat.h"

#include "core/layers/Compositor.h"

#include <QCoreApplication>
#include <QLibrary>

#include <algorithm>
#include <deque>
#include <optional>
#include <vector>

namespace doodlebold {

namespace {

constexpr int kT = Tile::kSize;
constexpr std::int64_t kMaxPluginFile = std::int64_t{4} << 30;  // 4 GiB: the plugin is given the whole file at once
constexpr std::int64_t kMaxArea = std::int64_t{kMaxCanvasSide} * kMaxCanvasSide;
constexpr std::size_t kSniffBytes = 4096;

QString tr(const char* text) {
    return QCoreApplication::translate("PluginFormat", text);
}

FileError::Kind kindFor(int code) {
    switch (code) {
    case DOODLEBOLD_ERROR_NOT_THIS_FORMAT: return FileError::Kind::UnknownFormat;
    case DOODLEBOLD_ERROR_TRUNCATED: return FileError::Kind::Truncated;
    case DOODLEBOLD_ERROR_UNSUPPORTED: return FileError::Kind::Unsupported;
    case DOODLEBOLD_ERROR_TOO_LARGE:
    case DOODLEBOLD_ERROR_OUT_OF_MEMORY: return FileError::Kind::TooLarge;
    case DOODLEBOLD_ERROR_WRITE_FAILED: return FileError::Kind::CannotWrite;
    default: return FileError::Kind::Corrupt;
    }
}

// Pixels: the plugin's straight 16-bit RGBA and masks, and the layers' premultiplied tiles.

const std::uint16_t* rowAt(const std::uint16_t* first, std::size_t stride, int y) {
    return reinterpret_cast<const std::uint16_t*>(reinterpret_cast<const std::uint8_t*>(first) + stride * static_cast<std::size_t>(y));
}

std::uint16_t* rowAt(std::uint16_t* first, std::size_t stride, int y) {
    return reinterpret_cast<std::uint16_t*>(reinterpret_cast<std::uint8_t*>(first) + stride * static_cast<std::size_t>(y));
}

// The part of `rect`'s pixels that falls in tile (tx, ty), into that tile. False when it stays transparent.
bool fillTile(Tile& tile, int tx, int ty, const std::uint16_t* pixels, std::size_t stride, const QRect& rect, const QRect& part) {
    bool any = false;
    for (int y = part.top(); y <= part.bottom(); ++y) {
        const std::uint16_t* src = rowAt(pixels, stride, y - rect.top()) + std::size_t(part.left() - rect.left()) * 4;
        for (int x = part.left(); x <= part.right(); ++x, src += 4) {
            const int a = src[3];
            if (a == 0)
                continue;
            Channel* d = tile.at(x - tx * kT, y - ty * kT);
            for (int c = 0; c < 3; ++c)
                d[c] = static_cast<Channel>(mulChannel(src[c], a));
            d[3] = static_cast<Channel>(a);
            any = true;
        }
    }
    return any;
}

TiledImage layerFromStraight(const std::uint16_t* pixels, std::size_t stride, const QRect& rect, const QSize& canvas) {
    TiledImage out(canvas);
    const QRect area = rect.intersected(out.rect());
    if (area.isEmpty())
        return out;
    for (int ty = area.top() / kT; ty <= area.bottom() / kT; ++ty)
        for (int tx = area.left() / kT; tx <= area.right() / kT; ++tx) {
            auto tile = std::make_shared<Tile>();
            if (fillTile(*tile, tx, ty, pixels, stride, rect, TiledImage::tileRect(tx, ty).intersected(area)))
                out.setTile(tx, ty, std::move(tile));
        }
    return out;
}

void unpremultiply(const Channel* p, std::uint16_t* out) {
    const std::int64_t a = p[3];
    for (int c = 0; c < 3; ++c)
        out[c] =
            a == 0 ? 0 : static_cast<std::uint16_t>(std::min<std::int64_t>(kChannelOne, (p[c] * std::int64_t{kChannelOne} + a / 2) / a));
    out[3] = static_cast<std::uint16_t>(a);
}

// `rect` of `image` as straight 16-bit RGBA; what has no tile is transparent.
void straightFromLayer(const TiledImage& image, const QRect& rect, std::uint16_t* out, std::size_t stride) {
    for (int y = 0; y < rect.height(); ++y)
        std::fill_n(rowAt(out, stride, y), std::size_t(rect.width()) * 4, std::uint16_t{0});
    for (const auto& [coord, tile] : image.tiles()) {
        const QRect part = coord.pixelRect().intersected(rect);
        if (!tile || part.isEmpty())
            continue;
        for (int y = part.top(); y <= part.bottom(); ++y) {
            std::uint16_t* d = rowAt(out, stride, y - rect.top()) + std::size_t(part.left() - rect.left()) * 4;
            for (int x = part.left(); x <= part.right(); ++x, d += 4)
                unpremultiply(tile->at(x - coord.x * kT, y - coord.y * kT), d);
        }
    }
}

SelectionMask maskFromCoverage(const doodlebold_layer& l, const QSize& canvas) {
    SelectionMask mask(canvas);
    for (int ty = 0; ty < mask.tilesY(); ++ty)
        for (int tx = 0; tx < mask.tilesX(); ++tx)
            mask.setUniform(tx, ty, l.mask_outside);
    const QRect rect(l.mask_x, l.mask_y, l.mask_width, l.mask_height);
    const QRect part = rect.intersected(mask.rect());
    for (int y = part.top(); y <= part.bottom(); ++y)
        mask.writeRow(y, part.left(), part.width(), rowAt(l.mask, l.mask_stride, y - rect.top()) + (part.left() - rect.left()));
    mask.squeeze();
    return mask;
}

// Reading: the plugin builds the document through these callbacks.

struct ReadSession {
    LoadedDocument out;
    bool canvasSet = false;
    std::vector<LayerId> added;  // add_layer()'s numbers
    QStringList unknownModes;
    QString error;
    QSize tooLarge;  // a canvas set_canvas refused for being beyond kMaxCanvasSide
};

ReadSession& sessionOf(void* context) {
    return *static_cast<ReadSession*>(context);
}

int setCanvas(void* context, std::int32_t width, std::int32_t height, double ppi, const std::uint8_t background[4]) {
    ReadSession& s = sessionOf(context);
    if (!s.canvasSet && width >= 1 && height >= 1 && beyondCanvasLimit(QSize(width, height)))
        s.tooLarge = QSize(width, height);
    if (s.canvasSet || width < 1 || height < 1 || beyondCanvasLimit(QSize(width, height)))
        return 0;
    s.out.content.state.layers = LayerStack(QSize(width, height));
    if (ppi > 0)
        s.out.content.resolution = ppi;
    s.out.content.state.background = CanvasBackground::fromPaper(QColor(background[0], background[1], background[2], background[3]));
    s.canvasSet = true;
    return 1;
}

bool pixelsFit(std::int32_t width, std::int32_t height, const void* data, std::size_t stride, std::size_t bytesPerPixel) {
    if (width == 0 && height == 0)
        return true;
    return width > 0 && height > 0 && std::int64_t{width} * height <= kMaxArea && data && stride >= std::size_t(width) * bytesPerPixel;
}

bool layerFits(const doodlebold_layer& l) {
    if (l.struct_size < sizeof(doodlebold_layer) || (l.kind != DOODLEBOLD_LAYER_PIXELS && l.kind != DOODLEBOLD_LAYER_FOLDER))
        return false;
    if (l.kind == DOODLEBOLD_LAYER_PIXELS && !pixelsFit(l.width, l.height, l.pixels, l.stride, 8))
        return false;
    return !l.has_mask || pixelsFit(l.mask_width, l.mask_height, l.mask, l.mask_stride, 2);
}

// The folder `parent` names, kNoLayer for the top level; nullopt when it isn't a folder added before.
std::optional<LayerId> parentFolder(const ReadSession& s, std::int32_t parent) {
    if (parent == -1)
        return kNoLayer;
    if (parent < 0 || std::size_t(parent) >= s.added.size())
        return std::nullopt;
    const LayerNode* node = s.out.content.state.layers.find(s.added[std::size_t(parent)]);
    return node && node->isFolder() ? std::optional(node->props.id) : std::nullopt;
}

BlendMode modeFor(ReadSession& s, const doodlebold_layer& l) {
    const QString key = l.blend_mode ? QString::fromUtf8(l.blend_mode) : QStringLiteral("normal");
    const std::optional<BlendMode> mode = blendModeFromKey(key);
    if (!mode && !s.unknownModes.contains(key))
        s.unknownModes << key;
    if (!mode || (*mode == BlendMode::PassThrough && l.kind != DOODLEBOLD_LAYER_FOLDER))
        return BlendMode::Normal;
    return *mode;
}

void applyLayer(ReadSession& s, const doodlebold_layer& l, LayerNode& node) {
    LayerProps& p = node.props;
    if (l.name && *l.name)
        p.name = QString::fromUtf8(l.name);
    p.mode = modeFor(s, l);
    p.opacity = std::clamp(l.opacity, 0, 100);
    p.visible = l.visible != 0;
    p.clipping = l.clipping != 0;
    const QSize canvas = s.out.content.size();
    if (l.kind == DOODLEBOLD_LAYER_PIXELS && l.width > 0)
        node.pixels = layerFromStraight(l.pixels, l.stride, QRect(l.x, l.y, l.width, l.height), canvas);
    if (l.has_mask) {
        node.mask = maskFromCoverage(l, canvas);
        p.maskEnabled = l.mask_enabled != 0;
    }
}

std::int32_t addLayerUnguarded(ReadSession& s, const doodlebold_layer& l) {
    if (!s.canvasSet || !layerFits(l))
        return -1;
    const std::optional<LayerId> parent = parentFolder(s, l.parent);
    if (!parent)
        return -1;
    LayerStack& stack = s.out.content.state.layers;
    const bool folder = l.kind == DOODLEBOLD_LAYER_FOLDER;
    if (folder ? !stack.hasRoomForFolders(*parent) : stack.depthOf(*parent) + 1 > LayerStack::kMaxFolderLevels)
        return -1;
    LayerNode node = folder ? stack.makeFolder(stack.nextFolderName()) : stack.makeRaster(stack.nextLayerName());
    applyLayer(s, l, node);
    s.added.push_back(stack.insert(std::move(node), *parent, -1));
    return static_cast<std::int32_t>(s.added.size() - 1);
}

// Nothing thrown may cross into the plugin's C code.
std::int32_t addLayer(void* context, const doodlebold_layer* layer) {
    try {
        return layer ? addLayerUnguarded(sessionOf(context), *layer) : -1;
    } catch (...) {
        return -1;
    }
}

void note(void* context, const char* text) {
    try {
        if (text && *text)
            sessionOf(context).out.notes << QString::fromUtf8(text);
    } catch (...) {  // NOLINT(bugprone-empty-catch): a plugin calls this from C, which no exception may cross
    }
}

void setReadError(void* context, const char* text) {
    try {
        sessionOf(context).error = text ? QString::fromUtf8(text) : QString();
    } catch (...) {  // NOLINT(bugprone-empty-catch): a plugin calls this from C, which no exception may cross
    }
}

FileResult<LoadedDocument> finishRead(ReadSession& s) {
    using R = FileResult<LoadedDocument>;
    if (!s.canvasSet || s.out.content.state.layers.isEmpty())
        return R::failure({FileError::Kind::Corrupt, tr("(the plugin gave no layers)")});
    s.out.content.state.current = s.out.content.state.layers.displayOrder().front()->props.id;
    for (const QString& key : s.unknownModes)
        s.out.notes << tr("Blend mode \"%1\" is not available in Doodlebold; Normal is used.").arg(key);
    return std::move(s.out);
}

// Writing: the plugin reads the document through these callbacks.

struct WriteSession {
    const DocumentContent* content = nullptr;
    QIODevice* device = nullptr;
    std::vector<const LayerNode*> nodes;  // beside `layers`
    std::vector<doodlebold_layer> layers;
    std::deque<QByteArray> names;         // what the layers' names point to
    QString error;
};

WriteSession& writeSessionOf(void* context) {
    return *static_cast<WriteSession*>(context);
}

doodlebold_layer describe(WriteSession& s, const LayerNode& node, std::int32_t parent) {
    doodlebold_layer l{};
    l.struct_size = sizeof l;
    l.kind = node.isFolder() ? DOODLEBOLD_LAYER_FOLDER : DOODLEBOLD_LAYER_PIXELS;
    l.parent = parent;
    l.name = s.names.emplace_back(node.props.name.toUtf8()).constData();
    l.blend_mode = blendModeInfo(node.props.mode).key;
    l.opacity = node.props.opacity;
    l.visible = node.props.visible ? 1 : 0;
    l.clipping = node.props.clipping ? 1 : 0;
    const QRect bounds = node.isFolder() ? QRect() : node.pixels.contentBounds(true).intersected(QRect(QPoint(), s.content->size()));
    l.x = bounds.x(), l.y = bounds.y(), l.width = bounds.width(), l.height = bounds.height();
    if (node.mask) {
        l.has_mask = 1;
        l.mask_enabled = node.props.maskEnabled ? 1 : 0;
        l.mask_width = s.content->size().width(), l.mask_height = s.content->size().height();
        l.mask_outside = kChannelOne;
    }
    return l;
}

// Each folder before the layers inside it, bottom to top.
void collectLayers(WriteSession& s, const LayerNode& folder, std::int32_t parent) {
    for (const LayerNode& child : folder.children) {
        const auto index = static_cast<std::int32_t>(s.layers.size());
        s.layers.push_back(describe(s, child, parent));
        s.nodes.push_back(&child);
        if (child.isFolder())
            collectLayers(s, child, index);
    }
}

int getPixels(void* context, std::uint32_t layer, std::uint16_t* out, std::size_t stride) {
    try {
        WriteSession& s = writeSessionOf(context);
        if (layer >= s.layers.size() || !out)
            return 0;
        const doodlebold_layer& l = s.layers[layer];
        if (stride < std::size_t(l.width) * 8)
            return 0;
        straightFromLayer(s.nodes[layer]->pixels, QRect(l.x, l.y, l.width, l.height), out, stride);
        return 1;
    } catch (...) {
        return 0;
    }
}

int getMask(void* context, std::uint32_t layer, std::uint16_t* out, std::size_t stride) {
    try {
        WriteSession& s = writeSessionOf(context);
        if (layer >= s.layers.size() || !out || !s.nodes[layer]->mask)
            return 0;
        const doodlebold_layer& l = s.layers[layer];
        if (stride < std::size_t(l.mask_width) * 2)
            return 0;
        for (int y = 0; y < l.mask_height; ++y)
            s.nodes[layer]->mask->row(y, 0, l.mask_width, rowAt(out, stride, y));
        return 1;
    } catch (...) {
        return 0;
    }
}

TiledImage composite(const DocumentContent& content) {
    const LayerStack& stack = content.state.layers;
    if (!content.background().isTransparent())
        return Compositor::flattenOverPaper(stack, content.background().paperColor());
    std::vector<const LayerNode*> nodes;
    for (const LayerNode& n : stack.root().children)
        nodes.push_back(&n);
    return Compositor::flatten(nodes, stack.size());
}

int getComposite(void* context, std::uint16_t* out, std::size_t stride) {
    try {
        WriteSession& s = writeSessionOf(context);
        if (!out || stride < std::size_t(s.content->size().width()) * 8)
            return 0;
        straightFromLayer(composite(*s.content), QRect(QPoint(), s.content->size()), out, stride);
        return 1;
    } catch (...) {
        return 0;
    }
}

int writeBytes(void* context, const void* bytes, std::size_t size) {
    try {
        QIODevice& device = *writeSessionOf(context).device;
        return size == 0 || (bytes && device.write(static_cast<const char*>(bytes), qint64(size)) == qint64(size)) ? 1 : 0;
    } catch (...) {
        return 0;
    }
}

void setWriteError(void* context, const char* text) {
    try {
        writeSessionOf(context).error = text ? QString::fromUtf8(text) : QString();
    } catch (...) {  // NOLINT(bugprone-empty-catch): a plugin calls this from C, which no exception may cross
    }
}

void fillBackground(const CanvasBackground& background, std::uint8_t out[4]) {
    const QColor paper = background.paperColor();
    out[0] = std::uint8_t(paper.red()), out[1] = std::uint8_t(paper.green()), out[2] = std::uint8_t(paper.blue());
    out[3] = background.isTransparent() ? 0 : 255;
}

}

PluginFormat::PluginFormat(std::shared_ptr<QLibrary> library, const doodlebold_format& format, QStringList extensions)
    : m_library(std::move(library)),
      m_format(format),
      m_id(QString::fromUtf8(format.id)),
      m_name(QString::fromUtf8(format.name)),
      m_extensions(std::move(extensions)) {
    // The strings are the plugin's; they are copied, and the struct no longer points to them.
    m_format.id = m_format.name = m_format.extensions = nullptr;
}

DocumentFormat::Features PluginFormat::features() const {
    Features f;
    f.layers = (m_format.flags & DOODLEBOLD_FORMAT_LAYERS) != 0;
    f.alpha = (m_format.flags & DOODLEBOLD_FORMAT_ALPHA) != 0;
    return f;
}

bool PluginFormat::recognizes(QIODevice& device) const {
    if (!m_format.recognizes || !m_format.read)
        return false;
    const QByteArray head = device.read(kSniffBytes);
    return m_format.recognizes(reinterpret_cast<const std::uint8_t*>(head.constData()), std::size_t(head.size())) != 0;
}

FileResult<LoadedDocument> PluginFormat::read(QIODevice& device) const {
    using R = FileResult<LoadedDocument>;
    if (!m_format.read)
        return DocumentFormat::read(device);
    if (!device.isSequential() && device.size() > kMaxPluginFile)
        return R::failure({FileError::Kind::TooLarge, QStringLiteral("(file)")});
    const QByteArray data = device.readAll();
    ReadSession s;
    const doodlebold_read_host host{sizeof(doodlebold_read_host),
                                    &s,
                                    reinterpret_cast<const std::uint8_t*>(data.constData()),
                                    std::size_t(data.size()),
                                    setCanvas,
                                    addLayer,
                                    note,
                                    setReadError};
    const int code = m_format.read(&host);
    if (code != DOODLEBOLD_OK && s.tooLarge.isValid())
        return R::failure(FileError::canvasTooLarge(s.tooLarge, false));
    if (code != DOODLEBOLD_OK)
        return R::failure({kindFor(code), s.error});
    return finishRead(s);
}

FileResult<> PluginFormat::write(const DocumentContent& content, QIODevice& device, const ExportOptions& options) const {
    if (!m_format.write)
        return DocumentFormat::write(content, device, options);
    WriteSession s;
    s.content = &content;
    s.device = &device;
    collectLayers(s, content.state.layers.root(), -1);
    doodlebold_write_host host{};
    host.struct_size = sizeof host;
    host.context = &s;
    host.width = content.size().width();
    host.height = content.size().height();
    host.ppi = content.resolution;
    fillBackground(content.background(), host.background);
    host.layer_count = static_cast<std::uint32_t>(s.layers.size());
    host.layers = s.layers.data();
    host.get_pixels = getPixels;
    host.get_mask = getMask;
    host.get_composite = getComposite;
    host.write = writeBytes;
    host.set_error = setWriteError;
    const int code = m_format.write(&host);
    if (code != DOODLEBOLD_OK)
        return FileResult<>::failure({kindFor(code), s.error});
    return {};
}

}
