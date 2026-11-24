#pragma once

#include "core/raster/Channel.h"
#include "core/raster/TiledImage.h"

#include <QImage>
#include <QRect>
#include <QSize>

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <unordered_map>

namespace doodlebold {

struct SelectionTile {
    static constexpr int kSize = Tile::kSize;
    static constexpr int kValues = kSize * kSize;
    std::array<std::uint16_t, kValues> v{};

    std::uint16_t* row(int y) { return v.data() + y * kSize; }
    const std::uint16_t* row(int y) const { return v.data() + y * kSize; }
};

using SelectionTilePtr = std::shared_ptr<const SelectionTile>;

// Per-pixel selection coverage, 0 … kOne (the layers' own scale), so it weights a pixel with the same integer
// arithmetic as a brush mask. Sparse 64×64 copy-on-write tiles: a missing tile is 0, a fully selected tile is one shared "full"
// tile. Edits go through writableTile()/writeRow() and then squeeze(), which restores the invariant: no stored tile is
// all-zero, every all-full tile inside the canvas is the shared one. Pixels outside the canvas are always 0. "No selection" is
// not an empty mask: Document keeps a std::optional<SelectionMask> (missing = the whole canvas is editable) and never stores an
// empty mask.
class SelectionMask {
public:
    static constexpr int kOne = kChannelOne;

    SelectionMask() = default;
    explicit SelectionMask(const QSize& size) : m_size(size) {}

    QSize size() const { return m_size; }
    QRect rect() const { return QRect(QPoint(0, 0), m_size); }
    int tilesX() const { return (m_size.width() + SelectionTile::kSize - 1) / SelectionTile::kSize; }
    int tilesY() const { return (m_size.height() + SelectionTile::kSize - 1) / SelectionTile::kSize; }
    static QRect tileRect(int tx, int ty) { return TileCoord{tx, ty}.pixelRect(); }
    QRect tileRange(const QRect& r) const;

    bool isEmpty() const { return m_tiles.empty(); }
    int at(int x, int y) const;
    void row(int y, int x0, int n, std::uint16_t* out) const;
    QRect bounds(bool exact = false) const;

    const SelectionTile* tile(int tx, int ty) const;
    SelectionTilePtr tileShared(int tx, int ty) const;
    static bool isFull(const SelectionTile* t);
    // Write access: allocates a zero tile, or copies a shared one (the full tile included). Call squeeze() when done.
    SelectionTile* writableTile(int tx, int ty);
    void setTile(int tx, int ty, SelectionTilePtr t);
    void setUniform(int tx, int ty, int value);
    // True for the tiles on the right and bottom edge when the canvas size is not a multiple of 64: they hold real data
    bool isEdgeTile(int tx, int ty) const;
    // Tiles that would stay zero are not allocated; call squeeze() when done.
    void writeRow(int y, int x0, int n, const std::uint16_t* values);
    void squeeze();
    const std::unordered_map<TileCoord, SelectionTilePtr, TileCoordHash>& tiles() const { return m_tiles; }

    QImage toImage(const QRect& r) const;
    QImage toImage() const { return toImage(rect()); }

    bool operator==(const SelectionMask& o) const { return m_size == o.m_size && differingArea(&o).isEmpty(); }
    // Pixels of the tiles that differ between this mask and `o` (null = no selection, compared as an empty mask): the
    QRect differingArea(const SelectionMask* o) const;

private:
    QSize m_size;
    std::unordered_map<TileCoord, SelectionTilePtr, TileCoordHash> m_tiles;
};

QRect selectionChangedArea(const std::optional<SelectionMask>& a, const std::optional<SelectionMask>& b);

}
