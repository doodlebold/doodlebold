#include "core/layers/Compositor.h"

#include "core/layers/BlendOps.h"
#include "core/layers/PaperEffect.h"
#include "core/util/ParallelFor.h"
#include "core/util/WorkerPool.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <unordered_set>

namespace doodlebold {

namespace {

constexpr int kT = Tile::kSize;
constexpr int kFloats = kT * kT * 4;

using Buffer = std::vector<float>;

inline std::uint8_t toDisplay(float v) {
    return static_cast<std::uint8_t>(std::lround(std::clamp(v, 0.0f, 1.0f) * 255.0f));
}

bool isZero(const Buffer& b) {
    for (int i = 3; i < kFloats; i += 4)
        if (b[static_cast<std::size_t>(i)] > 0.0f)
            return false;
    return true;
}

// A layer mask multiplies the layer's opacity, pixel by pixel.
// TODO(reimplement): a mask should keep working with every blend mode once those exist.
void applyMask(const SelectionMask& mask, int tx, int ty, Buffer& content) {
    std::array<std::uint16_t, kT> row{};
    for (int y = 0; y < kT; ++y) {
        mask.row(ty * kT + y, tx * kT, kT, row.data());
        for (int x = 0; x < kT; ++x) {
            const float k = static_cast<float>(row[static_cast<std::size_t>(x)]) / SelectionMask::kOne;
            const std::size_t i = static_cast<std::size_t>((y * kT + x) * 4);
            for (std::size_t c = 0; c < 4; ++c)
                content[i + c] *= k;
        }
    }
}

void compositeChildren(const std::vector<LayerNode>& kids, int tx, int ty, Buffer& acc);

bool nodeContent(const LayerNode& n, int tx, int ty, Buffer& out) {
    if (n.isFolder()) {
        std::ranges::fill(out, 0.0f);
        compositeChildren(n.children, tx, ty, out);
        return !isZero(out);
    }
    const Tile* t = n.pixels.tile(tx, ty);
    if (!t)
        return false;
    for (int i = 0; i < kFloats; ++i)
        out[static_cast<std::size_t>(i)] = t->px[static_cast<std::size_t>(i)] * kChannelToFloat;
    paper::applyToTile(n.props.paper, tx * kT, ty * kT, kT, out.data());
    return true;
}

// A clipped layer shows only where its base is: source-atop, and the base keeps its alpha.
void clipOnto(const LayerNode& c, const Buffer& src, Buffer& base) {
    const float opacity = c.props.opacityFraction();
    for (int i = 0; i < kT * kT; ++i) {
        float* b = base.data() + i * 4;
        const float* s = src.data() + i * 4;
        const float through = 1.0f - s[3] * opacity;
        for (int ch = 0; ch < 3; ++ch)
            b[ch] = s[ch] * opacity * b[3] + b[ch] * through;
    }
}

void compositeChildren(const std::vector<LayerNode>& kids, int tx, int ty, Buffer& acc) {
    Buffer content(kFloats), clipped(kFloats);
    const std::size_t n = kids.size();
    for (std::size_t i = 0; i < n; ++i) {
        const LayerNode& node = kids[i];
        // Clipped layers belong to the nearest non-clipping layer below; a clipping layer with no base (bottom of its folder)
        // is drawn as an ordinary layer.
        if (node.props.clipping && i > 0)
            continue;
        std::size_t j = i + 1;
        while (j < n && kids[j].props.clipping)
            ++j;
        if (!node.props.visible || node.props.opacity <= 0)
            continue;
        if (!nodeContent(node, tx, ty, content))
            continue;
        for (std::size_t c = i + 1; c < j; ++c) {
            const LayerNode& cl = kids[c];
            if (!cl.props.visible || cl.props.opacity <= 0 || !nodeContent(cl, tx, ty, clipped))
                continue;
            if (const SelectionMask* clippedMask = cl.activeMask())
                applyMask(*clippedMask, tx, ty, clipped);
            clipOnto(cl, clipped, content);
        }
        if (const SelectionMask* mask = node.activeMask())
            applyMask(*mask, tx, ty, content);
        blend::blendSpan(node.props.mode, content.data(), node.props.opacityFraction(), acc.data(), kT * kT);
    }
}

}

namespace {

std::vector<QPoint> tilesOf(const QRect& r) {
    std::vector<QPoint> out;
    if (r.isEmpty())
        return out;
    for (int ty = r.top() / kT; ty <= r.bottom() / kT; ++ty)
        for (int tx = r.left() / kT; tx <= r.right() / kT; ++tx)
            out.emplace_back(tx, ty);
    return out;
}

// One job per tile on the shared pool (its threads live on, so a frame that recomposites a stroke's tiles does not start
// and join threads). The display render runs on the GUI thread only, never from inside a pool job.
template <typename F> void forEachTile(const std::vector<QPoint>& tiles, F&& f) {
    if (tiles.size() < 4) {
        for (const QPoint& t : tiles)
            f(t.x(), t.y());
        return;
    }
    WorkerPool::shared().run(int(tiles.size()), [&](int i) { f(tiles[std::size_t(i)].x(), tiles[std::size_t(i)].y()); });
}

}

void Compositor::renderOpaque(const LayerStack& stack, const QRect& rect, const QColor& paper, QImage* out) {
    const QRect r = rect.intersected(QRect(QPoint(0, 0), stack.size()));
    renderOpaqueTiles(stack, tilesOf(r), r, paper, out);
}

void Compositor::renderOpaqueTiles(const LayerStack& stack, const std::vector<QPoint>& tiles, const QRect& r, const QColor& paper,
                                   QImage* out) {
    Q_ASSERT(out && out->size() == stack.size() && out->format() == QImage::Format_RGBA8888_Premultiplied);
    const float pr = fromUnorm8(paper.red()) * kChannelToFloat, pg = fromUnorm8(paper.green()) * kChannelToFloat,
                pb = fromUnorm8(paper.blue()) * kChannelToFloat;
    // The pixels are taken here, once: QImage::scanLine() detaches a shared image, and detaching from several threads at once
    // races (each could write into a copy of its own).
    std::uint8_t* const pixels = out->bits();
    const qsizetype stride = out->bytesPerLine();
    forEachTile(tiles, [&](int tx, int ty) {
        // The layers blend against the opaque paper colour, not against transparency.
        Buffer acc(kFloats);
        for (int i = 0; i < kT * kT; ++i) {
            float* p = acc.data() + i * 4;
            p[0] = pr, p[1] = pg, p[2] = pb, p[3] = 1.0f;
        }
        compositeChildren(stack.root().children, tx, ty, acc);
        const QRect part = TiledImage::tileRect(tx, ty).intersected(r);
        for (int y = part.top(); y <= part.bottom(); ++y) {
            std::uint8_t* dst = pixels + y * stride + part.left() * 4;
            const float* s = acc.data() + ((y - ty * kT) * kT + (part.left() - tx * kT)) * 4;
            for (int x = 0; x < part.width(); ++x, s += 4, dst += 4) {
                dst[0] = toDisplay(s[0]);
                dst[1] = toDisplay(s[1]);
                dst[2] = toDisplay(s[2]);
                dst[3] = 255;
            }
        }
    });
}

void Compositor::renderOverBackdrop(const LayerStack& stack, const QRect& rect, const Backdrop& backdrop, QImage* out) {
    const QRect r = rect.intersected(QRect(QPoint(0, 0), stack.size()));
    renderOverBackdropTiles(stack, tilesOf(r), r, backdrop, out);
}

void Compositor::renderOverBackdropTiles(const LayerStack& stack, const std::vector<QPoint>& tiles, const QRect& r,
                                         const Backdrop& backdrop, QImage* out) {
    Q_ASSERT(out && out->size() == stack.size() && out->format() == QImage::Format_RGBA8888_Premultiplied);
    std::uint8_t* const pixels = out->bits();
    const qsizetype stride = out->bytesPerLine();
    forEachTile(tiles, [&](int tx, int ty) {
        Buffer acc(kFloats, 0.0f);
        compositeChildren(stack.root().children, tx, ty, acc);
        const QRect part = TiledImage::tileRect(tx, ty).intersected(r);
        for (int y = part.top(); y <= part.bottom(); ++y) {
            std::uint8_t* dst = pixels + y * stride + part.left() * 4;
            const float* s = acc.data() + ((y - ty * kT) * kT + (part.left() - tx * kT)) * 4;
            for (int x = 0; x < part.width(); ++x, s += 4, dst += 4) {
                // The composite is premultiplied, so what shows through is (1 - alpha) of the backdrop colour.
                const QRgb back = backdrop.at(part.left() + x, y);
                const float through = 1.0f - s[3];
                dst[0] = toDisplay(s[0] + through * fromUnorm8(qRed(back)) * kChannelToFloat);
                dst[1] = toDisplay(s[1] + through * fromUnorm8(qGreen(back)) * kChannelToFloat);
                dst[2] = toDisplay(s[2] + through * fromUnorm8(qBlue(back)) * kChannelToFloat);
                dst[3] = 255;
            }
        }
    });
}

void Compositor::renderDisplay(const LayerStack& stack, const QRect& rect, const CanvasBackground& background, QImage* out) {
    if (background.isTransparent())
        renderOverBackdrop(stack, rect, background.backdrop(), out);
    else
        renderOpaque(stack, rect, background.paperColor(), out);
}

namespace {

// CompositeChildren for a tile reduced by 2^level a side: `side` pixels a side, each the average of its block.
void compositeChildrenReduced(const std::vector<LayerNode>& kids, int tx, int ty, int level, Buffer& acc,
                              const Compositor::ReducedOverride* override);

bool nodeContentReduced(const LayerNode& n, int tx, int ty, int level, Buffer& out, const Compositor::ReducedOverride* override) {
    const int side = kT >> level;
    if (n.isFolder()) {
        std::ranges::fill(out, 0.0f);
        compositeChildrenReduced(n.children, tx, ty, level, out, override);
        for (int i = 3; i < side * side * 4; i += 4)
            if (out[static_cast<std::size_t>(i)] > 0.0f)
                return true;
        return false;
    }
    if (override && override->blocks && n.props.id == override->layer)
        if (const auto found = override->blocks->find(TileCoord{tx, ty}); found != override->blocks->end()) {
            std::copy(found->second.begin(), found->second.end(), out.begin());
            return true;
        }
    const Tile* t = n.pixels.tile(tx, ty);
    if (!t)
        return false;
    Compositor::reduceTile(*t, level, out.data());
    return true;
}

void applyMaskReduced(const SelectionMask& mask, int tx, int ty, int level, Buffer& content) {
    const int side = kT >> level, block = 1 << level;
    std::array<std::uint16_t, kT> row{};
    std::vector<float> k(static_cast<std::size_t>(side));
    for (int y = 0; y < side; ++y) {
        std::ranges::fill(k, 0.0f);
        for (int by = 0; by < block; ++by) {
            mask.row(ty * kT + y * block + by, tx * kT, kT, row.data());
            for (int x = 0; x < kT; ++x)
                k[static_cast<std::size_t>(x / block)] += static_cast<float>(row[static_cast<std::size_t>(x)]);
        }
        for (int x = 0; x < side; ++x) {
            const float m = k[static_cast<std::size_t>(x)] / (static_cast<float>(block * block) * SelectionMask::kOne);
            for (std::size_t c = 0; c < 4; ++c)
                content[static_cast<std::size_t>((y * side + x) * 4) + c] *= m;
        }
    }
}

void clipOntoReduced(const LayerNode& c, const Buffer& src, Buffer& base, int pixels) {
    const float opacity = c.props.opacityFraction();
    for (int i = 0; i < pixels; ++i) {
        float* b = base.data() + i * 4;
        const float* s = src.data() + i * 4;
        const float through = 1.0f - s[3] * opacity;
        for (int ch = 0; ch < 3; ++ch)
            b[ch] = s[ch] * opacity * b[3] + b[ch] * through;
    }
}

void compositeChildrenReduced(const std::vector<LayerNode>& kids, int tx, int ty, int level, Buffer& acc,
                              const Compositor::ReducedOverride* override) {
    const int side = kT >> level;
    Buffer content(static_cast<std::size_t>(side * side * 4)), clipped(static_cast<std::size_t>(side * side * 4));
    const std::size_t n = kids.size();
    for (std::size_t i = 0; i < n; ++i) {
        const LayerNode& node = kids[i];
        if (node.props.clipping && i > 0)
            continue;
        std::size_t j = i + 1;
        while (j < n && kids[j].props.clipping)
            ++j;
        if (!node.props.visible || node.props.opacity <= 0)
            continue;
        if (!nodeContentReduced(node, tx, ty, level, content, override))
            continue;
        for (std::size_t c = i + 1; c < j; ++c) {
            const LayerNode& cl = kids[c];
            if (!cl.props.visible || cl.props.opacity <= 0 || !nodeContentReduced(cl, tx, ty, level, clipped, override))
                continue;
            if (const SelectionMask* clippedMask = cl.activeMask())
                applyMaskReduced(*clippedMask, tx, ty, level, clipped);
            clipOntoReduced(cl, clipped, content, side * side);
        }
        if (const SelectionMask* mask = node.activeMask())
            applyMaskReduced(*mask, tx, ty, level, content);
        blend::blendSpan(node.props.mode, content.data(), node.props.opacityFraction(), acc.data(), side * side);
    }
}

}

void Compositor::reduceTile(const Tile& tile, int level, float* out) {
    const int side = kT >> level, block = 1 << level;
    const float share = kChannelToFloat / static_cast<float>(block * block);
    for (int y = 0; y < side; ++y)
        for (int x = 0; x < side; ++x) {
            std::array<std::uint32_t, 4> sum{};
            for (int by = 0; by < block; ++by) {
                const Channel* p = tile.px.data() + static_cast<std::size_t>(((y * block + by) * kT + x * block) * 4);
                for (int bx = 0; bx < block; ++bx, p += 4)
                    for (std::size_t c = 0; c < 4; ++c)
                        sum[c] += p[c];
            }
            for (std::size_t c = 0; c < 4; ++c)
                out[(y * side + x) * 4 + static_cast<int>(c)] = static_cast<float>(sum[c]) * share;
        }
}

void Compositor::renderDisplayTilesReduced(const LayerStack& stack, const std::vector<QPoint>& tiles, const CanvasBackground& background,
                                           int level, QImage* out, const ReducedOverride* override) {
    const int side = kT >> level, block = 1 << level;
    const QRect r = out->rect();
    std::uint8_t* const pixels = out->bits();
    const qsizetype stride = out->bytesPerLine();
    const bool transparent = background.isTransparent();
    const QColor paper = background.paperColor();
    const float pr = fromUnorm8(paper.red()) * kChannelToFloat, pg = fromUnorm8(paper.green()) * kChannelToFloat,
                pb = fromUnorm8(paper.blue()) * kChannelToFloat;
    forEachTile(tiles, [&](int tx, int ty) {
        Buffer acc(static_cast<std::size_t>(side * side * 4), 0.0f);
        if (!transparent)
            for (int i = 0; i < side * side; ++i) {
                float* p = acc.data() + i * 4;
                p[0] = pr, p[1] = pg, p[2] = pb, p[3] = 1.0f;
            }
        compositeChildrenReduced(stack.root().children, tx, ty, level, acc, override);
        const QRect part = QRect(tx * side, ty * side, side, side).intersected(r);
        for (int y = part.top(); y <= part.bottom(); ++y) {
            std::uint8_t* dst = pixels + y * stride + part.left() * 4;
            const float* s = acc.data() + ((y - ty * side) * side + (part.left() - tx * side)) * 4;
            for (int x = part.left(); x <= part.right(); ++x, s += 4, dst += 4) {
                if (transparent) {
                    // The backdrop at the block's middle shows through (1 - alpha) of it.
                    const QRgb back = background.backdrop().at(x * block + block / 2, y * block + block / 2);
                    const float through = 1.0f - s[3];
                    dst[0] = toDisplay(s[0] + through * fromUnorm8(qRed(back)) * kChannelToFloat);
                    dst[1] = toDisplay(s[1] + through * fromUnorm8(qGreen(back)) * kChannelToFloat);
                    dst[2] = toDisplay(s[2] + through * fromUnorm8(qBlue(back)) * kChannelToFloat);
                } else {
                    dst[0] = toDisplay(s[0]);
                    dst[1] = toDisplay(s[1]);
                    dst[2] = toDisplay(s[2]);
                }
                dst[3] = 255;
            }
        }
    });
}

void Compositor::renderDisplayTiles(const LayerStack& stack, const std::vector<QPoint>& tiles, const CanvasBackground& background,
                                    QImage* out) {
    const QRect doc(QPoint(0, 0), stack.size());
    if (background.isTransparent())
        renderOverBackdropTiles(stack, tiles, doc, background.backdrop(), out);
    else
        renderOpaqueTiles(stack, tiles, doc, background.paperColor(), out);
}

TiledImage Compositor::flattenOverPaper(const LayerStack& stack, const QColor& paper) {
    TiledImage result(stack.size());
    const int count = result.tilesX() * result.tilesY();
    const float pr = fromUnorm8(paper.red()) * kChannelToFloat, pg = fromUnorm8(paper.green()) * kChannelToFloat,
                pb = fromUnorm8(paper.blue()) * kChannelToFloat;
    std::vector<TilePtr> tiles(static_cast<std::size_t>(count));
    parallelFor(count, [&](int idx) {
        const int tx = idx % result.tilesX(), ty = idx / result.tilesX();
        Buffer acc(kFloats);
        for (int i = 0; i < kT * kT; ++i) {
            float* p = acc.data() + i * 4;
            p[0] = pr, p[1] = pg, p[2] = pb, p[3] = 1.0f;
        }
        compositeChildren(stack.root().children, tx, ty, acc);
        auto t = std::make_shared<Tile>();
        for (int i = 0; i < kFloats; ++i)
            t->px[static_cast<std::size_t>(i)] = fromUnit(acc[static_cast<std::size_t>(i)]);
        tiles[static_cast<std::size_t>(idx)] = std::move(t);
    });
    for (int idx = 0; idx < count; ++idx)
        result.setTile(idx % result.tilesX(), idx / result.tilesX(), std::move(tiles[static_cast<std::size_t>(idx)]));
    return result;
}

namespace {

void collectTiles(const std::vector<LayerNode>& kids, std::unordered_set<TileCoord, TileCoordHash>& out) {
    for (const LayerNode& n : kids) {
        if (n.isFolder())
            collectTiles(n.children, out);
        else
            for (const auto& [coord, tile] : n.pixels.tiles())
                if (tile)
                    out.insert(coord);
    }
}

}

TiledImage Compositor::flatten(const std::vector<const LayerNode*>& nodes, const QSize& size) {
    std::vector<LayerNode> kids;
    kids.reserve(nodes.size());
    for (const LayerNode* n : nodes)
        kids.push_back(*n);
    TiledImage result(size);
    // Every tile any layer has, on or off the canvas: what lies past the edge stays in the result.
    std::unordered_set<TileCoord, TileCoordHash> present;
    collectTiles(kids, present);
    const std::vector<TileCoord> coords(present.begin(), present.end());
    std::vector<TilePtr> tiles(coords.size());
    parallelFor(static_cast<int>(coords.size()), [&](int idx) {
        const TileCoord c = coords[static_cast<std::size_t>(idx)];
        Buffer acc(kFloats, 0.0f);
        compositeChildren(kids, c.x, c.y, acc);
        if (isZero(acc))
            return;
        auto t = std::make_shared<Tile>();
        for (int i = 0; i < kFloats; ++i)
            t->px[static_cast<std::size_t>(i)] = fromUnit(acc[static_cast<std::size_t>(i)]);
        tiles[static_cast<std::size_t>(idx)] = std::move(t);
    });
    for (std::size_t i = 0; i < coords.size(); ++i)
        if (tiles[i])
            result.setTile(coords[i].x, coords[i].y, std::move(tiles[i]));
    return result;
}

}
