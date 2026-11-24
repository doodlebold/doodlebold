#include "core/io/ImageConvert.h"

#include "core/layers/Compositor.h"
#include "core/util/ParallelFor.h"

#include <algorithm>
#include <cmath>

namespace doodlebold::io {

namespace {

constexpr int kT = Tile::kSize;

bool isDeep(const QImage& img) {
    switch (img.format()) {
    case QImage::Format_Grayscale16:
    case QImage::Format_BGR30:
    case QImage::Format_RGB30:
    case QImage::Format_A2BGR30_Premultiplied:
    case QImage::Format_A2RGB30_Premultiplied: return true;
    default: return img.depth() > 32;
    }
}

inline Channel premul(int c, int a) {
    return static_cast<Channel>(mulChannel(c, a));
}

}

TiledImage layerFromImage(const QImage& picture) {
    TiledImage out(picture.size());
    if (picture.isNull())
        return out;
    const bool deep = isDeep(picture);
    const QImage src = picture.convertToFormat(deep ? QImage::Format_RGBA64 : QImage::Format_RGBA8888);
    const int w = src.width(), h = src.height();
    const int count = out.tilesX() * out.tilesY();
    std::vector<std::shared_ptr<Tile>> tiles(static_cast<std::size_t>(count));
    parallelFor(count, [&](int idx) {
        const int tx = idx % out.tilesX(), ty = idx / out.tilesX();
        auto tile = std::make_shared<Tile>();
        bool any = false;
        for (int y = 0; y < kT && ty * kT + y < h; ++y) {
            const int sy = ty * kT + y;
            for (int x = 0; x < kT && tx * kT + x < w; ++x) {
                const int sx = tx * kT + x;
                int c[4];
                if (deep) {
                    const auto* p = reinterpret_cast<const std::uint16_t*>(src.constScanLine(sy)) + sx * 4;
                    for (int i = 0; i < 4; ++i)
                        c[i] = p[i];
                } else {
                    const std::uint8_t* p = src.constScanLine(sy) + sx * 4;
                    for (int i = 0; i < 4; ++i)
                        c[i] = fromUnorm8(p[i]);
                }
                if (c[3] == 0)
                    continue;
                Channel* d = tile->at(x, y);
                d[0] = premul(c[0], c[3]);
                d[1] = premul(c[1], c[3]);
                d[2] = premul(c[2], c[3]);
                d[3] = static_cast<Channel>(c[3]);
                any = true;
            }
        }
        if (any)
            tiles[static_cast<std::size_t>(idx)] = std::move(tile);
    });
    for (int idx = 0; idx < count; ++idx)
        if (tiles[static_cast<std::size_t>(idx)])
            out.setTile(idx % out.tilesX(), idx / out.tilesX(), std::move(tiles[static_cast<std::size_t>(idx)]));
    return out;
}

TiledImage placeOnCanvas(const TiledImage& pixels, const QSize& canvas, const QPoint& topLeft) {
    TiledImage out(canvas);
    if (topLeft.isNull() && pixels.size() == canvas)
        return pixels;
    const QRect target = QRect(topLeft, pixels.size()).intersected(out.rect());
    if (target.isEmpty())
        return out;
    for (const auto& [coord, tile] : pixels.tiles()) {
        if (!tile)
            continue;
        const QRect part = (coord.pixelRect().translated(topLeft)).intersected(target);
        for (int y = part.top(); y <= part.bottom(); ++y)
            for (int x = part.left(); x <= part.right(); ++x) {
                const int sx = x - topLeft.x(), sy = y - topLeft.y();
                const Channel* s = tile->at(sx - coord.x * kT, sy - coord.y * kT);
                if (s[3] == 0)
                    continue;
                std::copy(s, s + 4, out.writableTile(x / kT, y / kT)->at(x % kT, y % kT));
            }
    }
    return out;
}

QImage flattenedPicture(const DocumentContent& content, bool keepAlpha) {
    const LayerStack& stack = content.state.layers;
    const bool alpha = keepAlpha && hasTransparentBackground(content);
    TiledImage flat;
    if (alpha) {
        std::vector<const LayerNode*> nodes;
        for (const LayerNode& n : stack.root().children)
            nodes.push_back(&n);
        flat = Compositor::flatten(nodes, stack.size());
    } else {
        flat =
            Compositor::flattenOverPaper(stack, hasTransparentBackground(content) ? QColor(Qt::white) : content.background().paperColor());
    }
    QImage out(stack.size(), alpha ? QImage::Format_RGBA8888 : QImage::Format_RGB888);
    if (out.isNull())
        return out;
    out.setDotsPerMeterX(dotsPerMeter(content.resolution));
    out.setDotsPerMeterY(dotsPerMeter(content.resolution));
    parallelFor(flat.tilesY(), [&](int ty) {
        for (int tx = 0; tx < flat.tilesX(); ++tx) {
            const Tile* t = flat.tile(tx, ty);
            for (int y = ty * kT; y < std::min(out.height(), (ty + 1) * kT); ++y) {
                for (int x = tx * kT; x < std::min(out.width(), (tx + 1) * kT); ++x) {
                    const Channel* p = t ? t->at(x % kT, y % kT) : nullptr;
                    const int a = p ? p[3] : 0;
                    if (alpha) {
                        // Straight colour: premultiplied value / alpha, rounded to the nearest level.
                        std::uint8_t* d = out.scanLine(y) + x * 4;
                        for (int c = 0; c < 3; ++c)
                            d[c] = a == 0 ? 0 : static_cast<std::uint8_t>(std::min(255, (p[c] * 255 + a / 2) / a));
                        d[3] = toUnorm8(a);
                    } else {
                        std::uint8_t* d = out.scanLine(y) + x * 3;
                        for (int c = 0; c < 3; ++c)
                            d[c] = p ? toUnorm8(p[c]) : 0;
                    }
                }
            }
        }
    });
    return out;
}

int dotsPerMeter(double pixelsPerInch) {
    return static_cast<int>(std::lround(pixelsPerInch / 0.0254));
}

double pixelsPerInch(int dotsPerMeter) {
    return dotsPerMeter * 0.0254;
}

TiledImage mergedLayer(const DocumentContent& content) {
    const auto& kids = content.state.layers.root().children;
    if (kids.size() == 1 && !kids[0].isFolder() && kids[0].props.visible && kids[0].props.mode == BlendMode::Normal &&
        kids[0].props.opacity == 100 && !kids[0].props.paper.shows())
        return kids[0].pixels;
    std::vector<const LayerNode*> nodes;
    nodes.reserve(kids.size());
    for (const LayerNode& n : kids)
        nodes.push_back(&n);
    return Compositor::flatten(nodes, content.size());
}

DocumentContent contentFromPicture(const QImage& picture) {
    DocumentContent c;
    c.state.layers = LayerStack(picture.size());
    LayerNode layer = c.state.layers.makeRaster(c.state.layers.nextLayerName());
    layer.pixels = layerFromImage(picture);
    c.state.current = c.state.layers.insert(std::move(layer), kNoLayer, -1);
    return c;
}

}
