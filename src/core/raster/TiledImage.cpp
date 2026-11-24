#include "core/raster/TiledImage.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <utility>
#include <vector>

namespace doodlebold {

namespace {

bool allTransparent(const Tile& t) {
    for (int i = 3; i < Tile::kValues; i += 4)
        if (t.px[static_cast<std::size_t>(i)] != 0)
            return false;
    return true;
}

constexpr int kThumbnailGrid = 4;

// Where, along a side `from` pixels long, the kThumbnailGrid samples of each of a thumbnail's `to` pixels fall.
std::vector<int> thumbnailSamples(int to, int from) {
    std::vector<int> places;
    places.reserve(static_cast<std::size_t>(to * kThumbnailGrid));
    const double step = double(from) / to;
    for (int x = 0; x < to; ++x)
        for (int i = 0; i < kThumbnailGrid; ++i)
            places.push_back(std::min(from - 1, int((x + (i + 0.5) / kThumbnailGrid) * step)));
    return places;
}

// Adds one row of samples, `y` down in the tiles `rowTiles`, into each thumbnail pixel's sums.
void addThumbnailSamples(const std::vector<const Tile*>& rowTiles, const std::vector<int>& columns, int y,
                         std::vector<std::array<int, 4>>& sums) {
    for (std::size_t s = 0; s < columns.size(); ++s) {
        const int px = columns[s];
        const Tile* t = rowTiles[static_cast<std::size_t>(px / Tile::kSize)];
        if (!t)
            continue;
        const Channel* p = t->at(px % Tile::kSize, y);
        std::array<int, 4>& sum = sums[s / kThumbnailGrid];
        for (int c = 0; c < 4; ++c)
            sum[c] += p[c];
    }
}

}

QRect TiledImage::tileRange(const QRect& r) const {
    const QRect c = r.intersected(paintArea());
    if (c.isEmpty())
        return {};
    return QRect(QPoint(tileOf(c.left()), tileOf(c.top())), QPoint(tileOf(c.right()), tileOf(c.bottom())));
}

const Tile* TiledImage::tile(int tx, int ty) const {
    auto it = m_tiles.find(TileCoord{tx, ty});
    return it == m_tiles.end() ? nullptr : it->second.get();
}

TilePtr TiledImage::tileShared(int tx, int ty) const {
    auto it = m_tiles.find(TileCoord{tx, ty});
    return it == m_tiles.end() ? nullptr : it->second;
}

Tile* TiledImage::writableTile(int tx, int ty) {
    TilePtr& slot = m_tiles[TileCoord{tx, ty}];
    if (!slot)
        slot = std::make_shared<Tile>();
    else if (slot.use_count() > 1)
        slot = std::make_shared<Tile>(*slot);
    // The map owns the only reference now, so writing through it cannot affect a snapshot.
    return const_cast<Tile*>(slot.get());
}

Tile* TiledImage::writableExistingTile(int tx, int ty) {
    const auto found = m_tiles.find(TileCoord{tx, ty});
    if (found == m_tiles.end() || !found->second)
        return nullptr;
    TilePtr& slot = found->second;
    if (slot.use_count() > 1)
        slot = std::make_shared<Tile>(*slot);
    return const_cast<Tile*>(slot.get());
}

Tile* TiledImage::freshExistingTile(int tx, int ty, TilePtr& from) {
    const auto found = m_tiles.find(TileCoord{tx, ty});
    if (found == m_tiles.end() || !found->second)
        return nullptr;
    TilePtr& slot = found->second;
    from = slot;
    if (slot.use_count() > 2)  // `from` is one of them
        slot = std::make_shared<Tile>(Tile::Unset{});
    return const_cast<Tile*>(slot.get());
}

void TiledImage::setTile(int tx, int ty, TilePtr t) {
    if (t)
        m_tiles[TileCoord{tx, ty}] = std::move(t);
    else
        m_tiles.erase(TileCoord{tx, ty});
}

std::array<Channel, 4> TiledImage::pixel(int x, int y) const {
    if (!paintArea().contains(x, y))
        return {0, 0, 0, 0};
    const int tx = tileOf(x), ty = tileOf(y);
    const Tile* t = tile(tx, ty);
    if (!t)
        return {0, 0, 0, 0};
    const Channel* p = t->at(x - tx * Tile::kSize, y - ty * Tile::kSize);
    return {p[0], p[1], p[2], p[3]};
}

QImage TiledImage::toImage(const QRect& r) const {
    QImage img(r.size(), QImage::Format_RGBA8888_Premultiplied);
    img.fill(Qt::transparent);
    const QRect c = r.intersected(paintArea());
    if (c.isEmpty())
        return img;
    const QRect tr = tileRange(c);
    for (int ty = tr.top(); ty <= tr.bottom(); ++ty)
        for (int tx = tr.left(); tx <= tr.right(); ++tx) {
            const Tile* t = tile(tx, ty);
            if (!t)
                continue;
            const QRect part = tileRect(tx, ty).intersected(c);
            for (int y = part.top(); y <= part.bottom(); ++y) {
                std::uint8_t* dst = img.scanLine(y - r.top()) + (part.left() - r.left()) * 4;
                const Channel* src = t->at(part.left() - tx * Tile::kSize, y - ty * Tile::kSize);
                for (int i = 0; i < part.width() * 4; ++i)
                    dst[i] = toUnorm8(src[i]);
            }
        }
    return img;
}

void TiledImage::writeImage(const QImage& src, const QPoint& topLeft) {
    const QImage img =
        src.format() == QImage::Format_RGBA8888_Premultiplied ? src : src.convertToFormat(QImage::Format_RGBA8888_Premultiplied);
    const QRect c = QRect(topLeft, img.size()).intersected(paintArea());
    if (c.isEmpty())
        return;
    const QRect tr = tileRange(c);
    for (int ty = tr.top(); ty <= tr.bottom(); ++ty)
        for (int tx = tr.left(); tx <= tr.right(); ++tx) {
            const QRect part = tileRect(tx, ty).intersected(c);
            Tile* t = writableTile(tx, ty);
            for (int y = part.top(); y <= part.bottom(); ++y) {
                Channel* dst = t->at(part.left() - tx * Tile::kSize, y - ty * Tile::kSize);
                const std::uint8_t* in = img.constScanLine(y - topLeft.y()) + (part.left() - topLeft.x()) * 4;
                for (int i = 0; i < part.width() * 4; ++i)
                    dst[i] = fromUnorm8(in[i]);
            }
            if (allTransparent(*t))
                m_tiles.erase(TileCoord{tx, ty});
        }
}

std::array<Channel, 4> TiledImage::premultiplied(const QColor& c) {
    const int a = fromUnorm8(c.alpha());
    const auto premul = [a](int v) { return static_cast<Channel>(mulChannel(fromUnorm8(v), a)); };
    return {premul(c.red()), premul(c.green()), premul(c.blue()), static_cast<Channel>(a)};
}

void TiledImage::fill(const QRect& r, const QColor& c) {
    const QRect cl = r.intersected(rect());
    if (cl.isEmpty())
        return;
    const std::array<Channel, 4> v = premultiplied(c);
    const QRect tr = tileRange(cl);
    for (int ty = tr.top(); ty <= tr.bottom(); ++ty)
        for (int tx = tr.left(); tx <= tr.right(); ++tx) {
            const QRect part = tileRect(tx, ty).intersected(cl);
            if (v[3] == 0 && part == tileRect(tx, ty)) {
                m_tiles.erase(TileCoord{tx, ty});
                continue;
            }
            Tile* t = writableTile(tx, ty);
            for (int y = part.top(); y <= part.bottom(); ++y)
                for (int x = part.left(); x <= part.right(); ++x)
                    std::memcpy(t->at(x - tx * Tile::kSize, y - ty * Tile::kSize), v.data(), sizeof v);
            if (v[3] == 0 && allTransparent(*t))
                m_tiles.erase(TileCoord{tx, ty});
        }
}

QImage TiledImage::thumbnail(int maxSide) const {
    if (m_size.isEmpty() || maxSide <= 0)
        return {};
    const double scale = std::min(1.0, double(maxSide) / std::max(m_size.width(), m_size.height()));
    const QSize out(std::max(1, int(std::lround(m_size.width() * scale))), std::max(1, int(std::lround(m_size.height() * scale))));
    QImage img(out, QImage::Format_RGBA8888_Premultiplied);
    img.fill(Qt::transparent);
    if (m_tiles.empty())
        return img;
    // Each thumbnail pixel averages kGrid × kGrid samples. The samples of a row of them fall on one row of tiles, looked up
    // once for the row rather than once a sample.
    const std::vector<int> columns = thumbnailSamples(out.width(), m_size.width());
    const std::vector<int> rows = thumbnailSamples(out.height(), m_size.height());
    std::vector<const Tile*> rowTiles(static_cast<std::size_t>(tilesX()));
    int tilesRow = -1;
    std::vector<std::array<int, 4>> sums(static_cast<std::size_t>(out.width()));
    for (int y = 0; y < out.height(); ++y) {
        std::fill(sums.begin(), sums.end(), std::array<int, 4>{});
        for (int j = 0; j < kThumbnailGrid; ++j) {
            const int py = rows[static_cast<std::size_t>(y * kThumbnailGrid + j)];
            if (std::exchange(tilesRow, py / Tile::kSize) != tilesRow)
                for (int tx = 0; tx < tilesX(); ++tx)
                    rowTiles[static_cast<std::size_t>(tx)] = tile(tx, tilesRow);
            addThumbnailSamples(rowTiles, columns, py % Tile::kSize, sums);
        }
        std::uint8_t* row = img.scanLine(y);
        for (int x = 0; x < out.width(); ++x)
            for (int c = 0; c < 4; ++c)
                row[x * 4 + c] = toUnorm8((sums[static_cast<std::size_t>(x)][c] + kThumbnailGrid * kThumbnailGrid / 2) /
                                          (kThumbnailGrid * kThumbnailGrid));
    }
    return img;
}

QRect TiledImage::contentBounds(bool exact) const {
    QRect bounds;
    for (const auto& [coord, t] : m_tiles) {
        if (!t)
            continue;
        const int tx = coord.x, ty = coord.y;
        if (!exact) {
            bounds |= tileRect(tx, ty);
            continue;
        }
        int x0 = Tile::kSize, y0 = Tile::kSize, x1 = -1, y1 = -1;
        for (int y = 0; y < Tile::kSize; ++y)
            for (int x = 0; x < Tile::kSize; ++x)
                if (t->at(x, y)[3]) {
                    x0 = std::min(x0, x);
                    x1 = std::max(x1, x);
                    y0 = std::min(y0, y);
                    y1 = std::max(y1, y);
                }
        if (x1 >= 0)
            bounds |= QRect(QPoint(tx * Tile::kSize + x0, ty * Tile::kSize + y0), QPoint(tx * Tile::kSize + x1, ty * Tile::kSize + y1));
    }
    return bounds;
}

void TiledImage::squeeze() {
    for (auto it = m_tiles.begin(); it != m_tiles.end();)
        it = (!it->second || allTransparent(*it->second)) ? m_tiles.erase(it) : std::next(it);
}

}
