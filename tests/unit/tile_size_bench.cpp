// What different tile sizes would cost on real strokes: the tiles a stroke touches, and the memory they need.
//
//   doodlebold_tile_size_bench <input log>

#include "core/stroke/StrokeRecording.h"

#include <QString>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <set>
#include <utility>
#include <vector>

using namespace doodlebold;

namespace {

constexpr int kBytesPerPixel = 8 + 8;  // a 16-bit RGBA pixel, and its density and shape while the stroke runs

using TileSet = std::set<std::pair<int, int>>;

int tileOf(double pixel, int tileSize) {
    return static_cast<int>(std::floor(pixel / tileSize));
}

void markDisc(QPointF centre, double radius, int tileSize, TileSet& tiles) {
    const int left = tileOf(centre.x() - radius, tileSize);
    const int right = tileOf(centre.x() + radius, tileSize);
    const int top = tileOf(centre.y() - radius, tileSize);
    const int bottom = tileOf(centre.y() + radius, tileSize);
    for (int y = top; y <= bottom; ++y) {
        for (int x = left; x <= right; ++x)
            tiles.insert({x, y});
    }
}

void markSegment(QPointF from, QPointF to, double radius, int tileSize, TileSet& tiles) {
    const double length = std::hypot(to.x() - from.x(), to.y() - from.y());
    const int steps = std::max(1, static_cast<int>(length / std::max(1.0, radius)));
    for (int i = 0; i <= steps; ++i)
        markDisc(from + (to - from) * (static_cast<double>(i) / steps), radius, tileSize, tiles);
}

std::size_t tilesTouched(const RecordedStroke& stroke, double radius, int tileSize) {
    TileSet tiles;
    for (std::size_t i = 1; i < stroke.samples.size(); ++i)
        markSegment(stroke.samples[i - 1].pos, stroke.samples[i].pos, radius, tileSize, tiles);
    return tiles.size();
}

std::vector<std::size_t> tileCounts(const std::vector<RecordedStroke>& strokes, double radius, int tileSize) {
    std::vector<std::size_t> counts;
    counts.reserve(strokes.size());
    for (const RecordedStroke& stroke : strokes)
        counts.push_back(tilesTouched(stroke, radius, tileSize));
    std::ranges::sort(counts);
    return counts;
}

double mebibytes(std::size_t tiles, int tileSize) {
    return static_cast<double>(tiles) * tileSize * tileSize * kBytesPerPixel / (1024.0 * 1024.0);
}

void printRow(const std::vector<RecordedStroke>& strokes, double radius, int tileSize) {
    const std::vector<std::size_t> counts = tileCounts(strokes, radius, tileSize);
    const std::size_t median = counts[counts.size() / 2];
    const std::size_t largest = counts.back();
    std::printf("%6.0f %6d %8zu %8zu %10.2f %10.2f\n", radius, tileSize, median, largest, mebibytes(median, tileSize),
                mebibytes(largest, tileSize));
}

}

int main(int argc, char** argv) {
    if (argc != 2) {
        std::fprintf(stderr, "usage: %s <input log>\n", argv[0]);
        return 2;
    }
    const std::vector<RecordedStroke> strokes = readRecording(QString::fromLocal8Bit(argv[1]));
    if (strokes.empty()) {
        std::fprintf(stderr, "no pen strokes in %s\n", argv[1]);
        return 1;
    }
    std::printf("%zu strokes\nradius   tile   median  largest median MiB largest MiB\n", strokes.size());
    for (const double radius : {5.0, 25.0, 100.0, 350.0}) {
        for (const int tileSize : {32, 64, 128, 256})
            printRow(strokes, radius, tileSize);
    }
    return 0;
}
