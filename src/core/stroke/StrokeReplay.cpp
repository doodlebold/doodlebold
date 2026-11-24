#include "core/stroke/StrokeReplay.h"

#include <algorithm>

namespace doodlebold {

namespace {

// The replay's hash of pixels is 64-bit FNV-1a: these are its offset basis and prime, as the format defines them.
constexpr std::uint64_t kHashStart = 14695981039346656037ULL;
constexpr std::uint64_t kHashPrime = 1099511628211ULL;

void mix(std::uint64_t& hash, std::uint64_t value) {
    hash = (hash ^ value) * kHashPrime;
}

void paintOneStroke(const RecordedStroke& stroke, StrokeEvaluator& evaluator, StrokeBuffer& buffer) {
    buffer.clear();
    evaluator.begin(stroke.samples.front(), buffer);
    for (std::size_t i = 1; i < stroke.samples.size(); ++i)
        evaluator.add(stroke.samples[i], buffer);
    evaluator.end(buffer);
}

bool isEmptyTile(const Tile& tile) {
    return std::all_of(tile.px.begin(), tile.px.end(), [](Channel value) { return value == 0; });
}

std::vector<TileCoord> paintedTiles(const TiledImage& image) {
    std::vector<TileCoord> coords;
    for (const auto& [coord, tile] : image.tiles()) {
        if (!isEmptyTile(*tile))
            coords.push_back(coord);
    }
    std::sort(coords.begin(), coords.end(), [](TileCoord a, TileCoord b) { return a.y != b.y ? a.y < b.y : a.x < b.x; });
    return coords;
}

}

ReplayResult replayStrokes(const std::vector<RecordedStroke>& strokes, StrokeEvaluator& evaluator, const StrokePaint& paint,
                           TiledImage canvas) {
    ReplayResult result;
    result.lastLive = canvas;
    StrokeBuffer buffer;
    for (const RecordedStroke& stroke : strokes) {
        if (stroke.samples.empty())
            continue;
        paintOneStroke(stroke, evaluator, buffer);
        result.lastLive = liveFrame(canvas, buffer, paint);
        mergeStroke(buffer, paint, canvas);
    }
    result.merged = std::move(canvas);
    return result;
}

std::uint64_t pixelHash(const TiledImage& image) {
    std::uint64_t hash = kHashStart;
    for (const TileCoord coord : paintedTiles(image)) {
        mix(hash, static_cast<std::uint32_t>(coord.x));
        mix(hash, static_cast<std::uint32_t>(coord.y));
        for (const Channel value : image.tile(coord.x, coord.y)->px)
            mix(hash, value);
    }
    return hash;
}

}
