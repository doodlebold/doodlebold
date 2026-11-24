#include "core/document/Projection.h"

#include "core/layers/Compositor.h"

#include <algorithm>
#include <map>
#include <vector>

namespace doodlebold {

Projection::Projection(const QSize& size, const CanvasBackground& background) : m_size(size), m_background(background) {
    invalidate(rect());
}

void Projection::setBackground(const CanvasBackground& background) {
    if (background == m_background)
        return;
    m_background = background;
    invalidate(rect());
}

void Projection::invalidate(const QRect& rect) {
    const QRect c = rect.intersected(this->rect());
    if (c.isEmpty())
        return;
    for (int ty = c.top() / Tile::kSize; ty <= c.bottom() / Tile::kSize; ++ty)
        for (int tx = c.left() / Tile::kSize; tx <= c.right() / Tile::kSize; ++tx) {
            m_dirty.insert(TileCoord{tx, ty});
            for (auto& [level, reduced] : m_reduced)
                if (!reduced.allDirty)
                    reduced.dirty.insert(TileCoord{tx, ty});
        }
}

const QImage& Projection::reducedImage(const LayerStack& layers, int level, const QRect& area) {
    Reduced& reduced = m_reduced[level];
    if (reduced.image.isNull()) {
        const int block = 1 << level;
        reduced.image =
            QImage((m_size.width() + block - 1) / block, (m_size.height() + block - 1) / block, QImage::Format_RGBA8888_Premultiplied);
        reduced.allDirty = true;
    }
    if (reduced.allDirty) {
        for (int ty = 0; ty * Tile::kSize < m_size.height(); ++ty)
            for (int tx = 0; tx * Tile::kSize < m_size.width(); ++tx)
                reduced.dirty.insert(TileCoord{tx, ty});
        reduced.allDirty = false;
    }
    std::vector<QPoint> tiles;
    for (auto it = reduced.dirty.begin(); it != reduced.dirty.end();) {
        if (area.isValid() && !area.intersects(it->pixelRect())) {
            ++it;
            continue;
        }
        tiles.emplace_back(it->x, it->y);
        it = reduced.dirty.erase(it);
    }
    const Compositor::ReducedOverride override{m_blocksLayer, &m_blocks};
    if (!tiles.empty())
        Compositor::renderDisplayTilesReduced(layers, tiles, m_background, level, &reduced.image,
                                              !m_blocks.empty() && m_blocksLevel == level ? &override : nullptr);
    return reduced.image;
}

void Projection::setStrokeBlocks(LayerId layer, int level, std::unordered_map<TileCoord, std::vector<float>, TileCoordHash> blocks) {
    if (layer != m_blocksLayer || level != m_blocksLevel) {
        // The level that showed the old blocks shows its layer's tiles there again.
        if (const auto shown = m_reduced.find(m_blocksLevel); shown != m_reduced.end() && !shown->second.allDirty)
            for (const auto& [coord, block] : m_blocks)
                shown->second.dirty.insert(coord);
        m_blocks.clear();
        m_blocksSinceFrame.clear();
    }
    m_blocksLayer = layer;
    m_blocksLevel = level;
    const auto shown = m_reduced.find(level);
    for (auto& [coord, block] : blocks) {
        if (shown != m_reduced.end() && !shown->second.allDirty)
            shown->second.dirty.insert(coord);
        m_blocks[coord] = std::move(block);
        m_blocksSinceFrame.insert(coord);
    }
}

void Projection::clearStrokeBlocks() {
    m_blocks.clear();
    m_blocksSinceFrame.clear();
}

namespace {

// Tile c of the canvas from the image reduced by 2^level, enlarged smoothly: the reduced pixels round it, one more all round
// so its edges mix with the right neighbours, scaled up and cut to the tile.
QImage enlargedTile(const QImage& reduced, int level, TileCoord c, const QRect& canvas) {
    const int f = 1 << level;
    const QRect r = c.pixelRect() & canvas;
    const QRect source = QRect(QPoint(r.left() / f - 1, r.top() / f - 1), QPoint(r.right() / f + 1, r.bottom() / f + 1)) & reduced.rect();
    const QImage big = reduced.copy(source).scaled(source.size() * f, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    return big.copy(r.translated(-source.topLeft() * f)).convertToFormat(QImage::Format_RGBA8888_Premultiplied);
}

}

std::unordered_map<TileCoord, QImage, TileCoordHash> Projection::takeStrokeFrameTiles(const LayerStack& layers) {
    std::unordered_map<TileCoord, QImage, TileCoordHash> out;
    if (m_blocks.empty() || m_blocksLevel <= 0 || m_blocksSinceFrame.empty())
        return out;
    // An enlarged tile's edge reads its neighbours' reduced pixels, so a stroke tile next to a changed one changes too.
    std::unordered_set<TileCoord, TileCoordHash> wanted;
    QRect area;
    for (const TileCoord c : m_blocksSinceFrame)
        for (int dy = -1; dy <= 1; ++dy)
            for (int dx = -1; dx <= 1; ++dx)
                if (const TileCoord n{c.x + dx, c.y + dy};
                    m_blocks.contains(n) && n.pixelRect().intersects(rect()) && wanted.insert(n).second)
                    area |= n.pixelRect();
    m_blocksSinceFrame.clear();
    if (wanted.empty())
        return out;
    const QImage& reduced = reducedImage(layers, m_blocksLevel, area.adjusted(-Tile::kSize, -Tile::kSize, Tile::kSize, Tile::kSize));
    for (const TileCoord c : wanted)
        out.emplace(c, enlargedTile(reduced, m_blocksLevel, c, rect()));
    return out;
}

void Projection::assign(const QImage& image) {
    if (image.size() != m_size || image.format() != QImage::Format_RGBA8888_Premultiplied)
        return;
    m_image = image;
    m_image.detach();  // recompositing writes from several threads: the pixels must not be shared with `image`
    m_dirty.clear();
}

void Projection::release() {
    m_image = QImage();
    m_reduced.clear();
    invalidate(rect());
}

const QImage& Projection::image(const LayerStack& layers) {
    if (m_image.isNull() && !m_size.isEmpty())
        m_image = QImage(m_size, QImage::Format_RGBA8888_Premultiplied);  // every tile is dirty since construction or release()
    if (m_dirty.empty())
        return m_image;
    std::vector<QPoint> tiles;
    tiles.reserve(m_dirty.size());
    for (const TileCoord& c : m_dirty)
        tiles.emplace_back(c.x, c.y);
    m_dirty.clear();
    Compositor::renderDisplayTiles(layers, tiles, m_background, &m_image);
    return m_image;
}

bool Projection::catchUpSome(const LayerStack& layers, std::size_t most) {
    if (m_image.isNull() && !m_size.isEmpty())
        m_image = QImage(m_size, QImage::Format_RGBA8888_Premultiplied);
    std::vector<QPoint> tiles;
    tiles.reserve(std::min(most, m_dirty.size()));
    for (auto it = m_dirty.begin(); it != m_dirty.end() && tiles.size() < most;) {
        tiles.emplace_back(it->x, it->y);
        it = m_dirty.erase(it);
    }
    if (!tiles.empty())
        Compositor::renderDisplayTiles(layers, tiles, m_background, &m_image);
    return !m_dirty.empty();
}

namespace {

QRect nodeBounds(const LayerNode& n) {
    if (!n.isFolder())
        return n.pixels.contentBounds();
    QRect r;
    for (const LayerNode& c : n.children)
        r |= nodeBounds(c);
    return r;
}

struct Placement {
    const LayerNode* node = nullptr;
    LayerId parent;
    int index = 0;
};

void collectPlacements(const LayerNode& folder, std::map<LayerId, Placement>& out) {
    for (std::size_t i = 0; i < folder.children.size(); ++i) {
        const LayerNode& c = folder.children[i];
        out[c.props.id] = {&c, folder.props.id, static_cast<int>(i)};
        if (c.isFolder())
            collectPlacements(c, out);
    }
}

// Properties that change the composite (renaming, locking or folding a folder does not; the paper does).
bool sameLook(const LayerProps& a, const LayerProps& b) {
    return a.mode == b.mode && a.opacity == b.opacity && a.visible == b.visible && a.clipping == b.clipping &&
           a.maskEnabled == b.maskEnabled && a.paper == b.paper;
}

QRect changedMask(const LayerNode& a, const LayerNode& b) {
    if (!a.mask && !b.mask)
        return {};
    if (a.mask && b.mask)
        return a.mask->differingArea(&*b.mask);
    const SelectionMask& only = a.mask ? *a.mask : *b.mask;
    return only.rect();
}

QRect changedTiles(const TileMap& a, const TileMap& b) {
    QRect r;
    for (const auto& [coord, tile] : a) {
        const auto it = b.find(coord);
        if (it == b.end() || it->second != tile)
            r |= coord.pixelRect();
    }
    for (const auto& [coord, tile] : b)
        if (!a.contains(coord))
            r |= coord.pixelRect();
    return r;
}

}

QRect changedArea(const LayerStack& before, const LayerStack& after) {
    std::map<LayerId, Placement> pa, pb;
    collectPlacements(before.root(), pa);
    collectPlacements(after.root(), pb);
    QRect r;
    for (const auto& [id, p] : pa) {
        const auto it = pb.find(id);
        if (it == pb.end()) {
            r |= nodeBounds(*p.node);
            continue;
        }
        const Placement& q = it->second;
        if (!sameLook(p.node->props, q.node->props) || p.parent != q.parent || p.index != q.index)
            r |= nodeBounds(*p.node) | nodeBounds(*q.node);
        else if (!p.node->isFolder())
            r |= changedTiles(p.node->pixels.tiles(), q.node->pixels.tiles());
        if (const QRect m = changedMask(*p.node, *q.node); !m.isEmpty())
            r |= m.intersected(nodeBounds(*p.node) | nodeBounds(*q.node));
    }
    for (const auto& [id, q] : pb)
        if (!pa.contains(id))
            r |= nodeBounds(*q.node);
    return r.intersected(QRect(QPoint(0, 0), before.size()));
}

}
