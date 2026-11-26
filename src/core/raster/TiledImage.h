#pragma once

#include "core/raster/Channel.h"

#include <QColor>
#include <QImage>
#include <QRect>
#include <QSize>

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <unordered_map>
#include <vector>

namespace doodlebold {

struct Tile {
    static constexpr int kSize = 64;
    static constexpr int kValues = kSize * kSize * 4;
    std::array<Channel, kValues> px;

    Tile() : px{} {}
    // Its pixels not yet set: for a tile about to be written whole.
    struct Unset {};
    explicit Tile(Unset) {}  // NOLINT(cppcoreguidelines-pro-type-member-init): left unset on purpose, see above

    Channel* at(int x, int y) { return px.data() + (y * kSize + x) * 4; }
    const Channel* at(int x, int y) const { return px.data() + (y * kSize + x) * 4; }
};

using TilePtr = std::shared_ptr<const Tile>;

struct TileCoord {
    int x = 0;
    int y = 0;
    bool operator==(const TileCoord&) const = default;
    QRect pixelRect() const { return QRect(x * Tile::kSize, y * Tile::kSize, Tile::kSize, Tile::kSize); }
};

struct TileCoordHash {
    std::size_t operator()(TileCoord c) const noexcept {
        return std::hash<std::uint64_t>{}((static_cast<std::uint64_t>(static_cast<std::uint32_t>(c.x)) << 32) |
                                          static_cast<std::uint32_t>(c.y));
    }
};

using TileMap = std::unordered_map<TileCoord, TilePtr, TileCoordHash>;

// Sparse tiled raster; tiles are copy-on-write, so copies and undo snapshots cost pointers, not pixels.
class TiledImage {
public:
    TiledImage() = default;
    explicit TiledImage(const QSize& size) : m_size(size) {}

    QSize size() const { return m_size; }
    QRect rect() const { return QRect(QPoint(0, 0), m_size); }
    // A layer keeps what is moved or transformed past the canvas's edge, this far (px, a whole number of tiles): it shows again
    // when it is moved back. 2^24 is as far as a 32-bit float, which the display draws with, holds every whole pixel; coordinates
    // are 32-bit, and sums of three of them must still fit.
    static constexpr int kOffCanvas = 1 << 24;
    QRect paintArea() const { return rect().adjusted(-kOffCanvas, -kOffCanvas, kOffCanvas, kOffCanvas); }
    // The margin round the canvas (px, a whole number of tiles) that work done over a whole area covers at once: the effects'
    // table of tiles, a move's every tile. It is how far a layer reached before moves had no limit, so such work is no slower.
    static constexpr int kNearCanvas = 5120;
    QRect nearArea() const { return rect().adjusted(-kNearCanvas, -kNearCanvas, kNearCanvas, kNearCanvas); }
    // The tile a pixel coordinate lies in; off the canvas coordinates can be negative.
    static int tileOf(int pixel) { return pixel >= 0 ? pixel / Tile::kSize : -((-pixel + Tile::kSize - 1) / Tile::kSize); }
    int tilesX() const { return (m_size.width() + Tile::kSize - 1) / Tile::kSize; }
    int tilesY() const { return (m_size.height() + Tile::kSize - 1) / Tile::kSize; }
    static QRect tileRect(int tx, int ty) { return TileCoord{tx, ty}.pixelRect(); }
    QRect tileRange(const QRect& r) const;

    const Tile* tile(int tx, int ty) const;
    TilePtr tileShared(int tx, int ty) const;
    Tile* writableTile(int tx, int ty);
    // writableTile for a tile already in the image, or nullptr: it changes only that tile's entry, so different tiles can be made
    // writable on different threads at once (the map itself isn't changed).
    Tile* writableExistingTile(int tx, int ty);
    // For a tile already in the image, about to be written whole from what it holds: if anything else shares it, a fresh tile whose
    // pixels aren't set takes its place and `from` keeps the one shared, to be read; otherwise the tile itself, and `from` the same.
    // Like writableExistingTile, it changes only that tile's entry.
    Tile* freshExistingTile(int tx, int ty, TilePtr& from);
    void setTile(int tx, int ty, TilePtr t);
    const TileMap& tiles() const { return m_tiles; }
    std::size_t tileCount() const { return m_tiles.size(); }
    bool isEmpty() const { return m_tiles.empty(); }

    std::array<Channel, 4> pixel(int x, int y) const;
    QImage toImage(const QRect& r) const;
    QImage toImage() const { return toImage(rect()); }
    void writeImage(const QImage& src, const QPoint& topLeft);
    // A straight QColor as the stored premultiplied pixel: the 8-bit colour and alpha are converted first, then multiplied.
    static std::array<Channel, 4> premultiplied(const QColor& c);
    void fill(const QRect& r, const QColor& c);
    void fill(const QColor& c) { fill(rect(), c); }
    void clear() { m_tiles.clear(); }
    // Downscaled copy (longest side at most maxSide); each pixel averages a 4×4 grid of samples, cheap enough for live
    // thumbnails.
    QImage thumbnail(int maxSide) const;
    // Bounding rectangle of non-transparent pixels, on or off the canvas (tile-granular when `exact` is false).
    QRect contentBounds(bool exact = false) const;
    void squeeze();

private:
    QSize m_size;
    TileMap m_tiles;
};

}
