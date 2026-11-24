#pragma once

#include "core/raster/TiledImage.h"

#include <array>
#include <cstdint>
#include <memory>
#include <vector>
#include <unordered_map>

namespace doodlebold {

struct DensityTile {
    std::array<float, Tile::kSize * Tile::kSize> value{};
    std::array<float, Tile::kSize * Tile::kSize> shape{};  // how much of each pixel the stroke's outline covers, 0 to 1
    int solidPixels = 0;  // how many look the same with more paint
    // A plain stamp tool on a grid (gridCell px apart): density and shape kept at points, the tile's own and the first column
    // and row of the next tiles, (across + 1) a side; value and shape per pixel only in a cell worked out per pixel. Each cell is
    // empty, read between its points or worked out per pixel, and says whether it changed since it was last merged.
    enum CellState : std::uint8_t {
        Empty,
        Read,
        PerPixel
    };
    int gridCell = 0;
    std::vector<float> pointValue, pointShape, beforeValue, beforeShape;
    std::vector<float> middleValue, middleShape;  // each cell's middle pixel, laid as the points are, to check the reading by
    std::vector<std::uint8_t> cellState;
    mutable std::vector<char> cellChanged;
    int solidPoints = 0;
    int perPixelCells = 0;
    void makeGrid(int cell);
};

// The paint one running stroke has laid, as an unclamped density per pixel. It builds up by adding, in the order the stroke
// laid it: float sums change slightly with order, and replays have to give the same pixels. Absent tiles hold no paint.
// Shape is kept apart from density because density is averaged across a pixel, and converting that average to coverage would
// treat a dense edge as a thin wash over the whole pixel.
class StrokeBuffer {
public:
    using Tiles = std::unordered_map<TileCoord, std::unique_ptr<DensityTile>, TileCoordHash>;

    void add(int x, int y, float density, float shape = 1.0f);
    float density(int x, int y) const;
    float shape(int x, int y) const;
    DensityTile& tile(TileCoord coord);
    // Takes a tile that was filled in elsewhere. There must be none there yet.
    void adopt(TileCoord coord, std::unique_ptr<DensityTile> tile);
    const Tiles& tiles() const { return m_tiles; }
    void clear() { m_tiles.clear(); }

private:
    Tiles m_tiles;
};

}
