#include "core/stroke/StrokeBuffer.h"

#include <utility>

#include <algorithm>

namespace doodlebold {

namespace {

TileCoord tileAt(int x, int y) {
    return {TiledImage::tileOf(x), TiledImage::tileOf(y)};
}

int indexIn(TileCoord coord, int x, int y) {
    return (y - coord.y * Tile::kSize) * Tile::kSize + (x - coord.x * Tile::kSize);
}

}

void StrokeBuffer::add(int x, int y, float density, float shape) {
    if (density <= 0.0f)
        return;
    const TileCoord coord = tileAt(x, y);
    DensityTile& target = tile(coord);
    const int index = indexIn(coord, x, y);
    target.value[index] += density;
    target.shape[index] = std::max(target.shape[index], shape);
}

void DensityTile::makeGrid(int cell) {
    gridCell = cell;
    const std::size_t across = static_cast<std::size_t>(Tile::kSize / cell);
    const std::size_t points = (across + 1) * (across + 1);
    pointValue.assign(points, 0.0f);
    pointShape.assign(points, 0.0f);
    beforeValue.assign(points, 0.0f);
    beforeShape.assign(points, 0.0f);
    middleValue.assign(across * across, 0.0f);
    middleShape.assign(across * across, 0.0f);
    cellState.assign(across * across, Empty);
    cellChanged.assign(across * across, 0);
}

void StrokeBuffer::adopt(TileCoord coord, std::unique_ptr<DensityTile> tile) {
    m_tiles[coord] = std::move(tile);
}

float StrokeBuffer::density(int x, int y) const {
    const TileCoord coord = tileAt(x, y);
    const auto found = m_tiles.find(coord);
    return found == m_tiles.end() ? 0.0f : found->second->value[indexIn(coord, x, y)];
}

float StrokeBuffer::shape(int x, int y) const {
    const TileCoord coord = tileAt(x, y);
    const auto found = m_tiles.find(coord);
    return found == m_tiles.end() ? 0.0f : found->second->shape[indexIn(coord, x, y)];
}

DensityTile& StrokeBuffer::tile(TileCoord coord) {
    std::unique_ptr<DensityTile>& slot = m_tiles[coord];
    if (!slot)
        slot = std::make_unique<DensityTile>();
    return *slot;
}

}
