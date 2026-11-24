#include "core/stroke/PaperGrain.h"

#include <algorithm>
#include <atomic>
#include <cmath>

// docs/brush-engine/paper-grain.md says what the named values here are for and how they were chosen.
namespace doodlebold {

namespace {

constexpr float kGreyLevels = 255.0f;  // an 8-bit grey picture's white
// How much of a coat the paper lets through, (1 - Intensity + Intensity · the paper's value) to this power: fitted to the reference's
// textured Pencil (ramp-pressure, ramp-density40, ramp-full on Doodlebold's own plain coverage; 1.0 to 1.5 tried,
// 1.2 the least off) and its half strength (ramp-intensity50: 1.87 levels of 8 bits off, against 2.97 for 1 - Intensity (1 - value^1.2)).
constexpr float kPaperGrainPower = 1.2f;
// Where a picture pixel's centre falls: a canvas pixel's centre over Scale, less half a picture pixel, as the reference reads it
// (correlation 0.786, 0.833, 0.801 at 100, 50, 200 %, against 0.765, 0.811, 0.775 without the half).
constexpr float kPixelCentre = 0.5f;
// Turned over for erasing, a picture's top is the lowest of the lightest greys of its columns taken this many apart.
constexpr int kTurnColumnSets = 8;

// Into 0 … size, the picture repeating.
float wrapped(float value, float size) {
    const float rest = std::fmod(value, size);
    return rest < 0.0f ? rest + size : rest;
}

}

PaperGrain::PaperGrain(const QImage& picture, double scale, double intensity, bool invert, bool turnedAtLightest)
    : m_picture(picture.convertToFormat(QImage::Format_Grayscale8)),
      m_width(std::max(m_picture.width(), 1)),
      m_height(std::max(m_picture.height(), 1)),
      m_scale(static_cast<float>(scale)),
      m_intensity(static_cast<float>(intensity)),
      m_invert(invert) {
    // Turned over for erasing, as the reference's: about its top and bottom.
    m_turned = m_invert && turnedAtLightest;
    if (m_turned)
        m_range = turnedRangeOf(m_picture);
    static std::atomic<std::uint64_t> papers{0};
    m_id = ++papers;
}

// The picture's grain between its pixels' centres, which sit at whole numbers, its lightness (Invert: its darkness); past its last
// pixel it wraps to its first.
float PaperGrain::grainAt(float u, float v) const {
    const float x = wrapped(u, static_cast<float>(m_width)), y = wrapped(v, static_cast<float>(m_height));
    const int x0 = std::min(static_cast<int>(x), m_width - 1), y0 = std::min(static_cast<int>(y), m_height - 1);
    const int x1 = (x0 + 1) % m_width, y1 = (y0 + 1) % m_height;
    const float fx = x - static_cast<float>(x0), fy = y - static_cast<float>(y0);
    const uchar* top = m_picture.constScanLine(y0);
    const uchar* bottom = m_picture.constScanLine(y1);
    const float upper = static_cast<float>(top[x0]) + (static_cast<float>(top[x1]) - static_cast<float>(top[x0])) * fx;
    const float lower = static_cast<float>(bottom[x0]) + (static_cast<float>(bottom[x1]) - static_cast<float>(bottom[x0])) * fx;
    const float light = (upper + (lower - upper) * fy) / kGreyLevels;
    return m_invert ? invertedLight(light) : light;
}

float PaperGrain::invertedLight(float light) const {
    return m_turned ? m_range.turned(light) : 1.0f - light;
}

float TurnedRange::turned(float light) const {
    return std::clamp(top - light + 2.0f * bottom, 0.0f, 1.0f);
}

TurnedRange turnedRangeOf(const QImage& grey) {
    std::array<int, kTurnColumnSets> lightest{};
    int darkest = static_cast<int>(kGreyLevels);
    for (int y = 0; y < grey.height(); ++y) {
        const uchar* row = grey.constScanLine(y);
        for (int x = 0; x < grey.width(); ++x) {
            int& set = lightest[static_cast<std::size_t>(x % kTurnColumnSets)];
            set = std::max(set, static_cast<int>(row[x]));
            darkest = std::min(darkest, static_cast<int>(row[x]));
        }
    }
    // A picture narrower than the sets leaves some empty: only the sets it fills count.
    const int sets = std::min(kTurnColumnSets, grey.width());
    return {static_cast<float>(*std::min_element(lightest.begin(), lightest.begin() + sets)) / kGreyLevels,
            grey.width() % kTurnColumnSets == 0 ? static_cast<float>(darkest) / kGreyLevels : 0.0f};
}

float PaperGrain::shareOf(float grain) const {
    return std::pow(1.0f - m_intensity + m_intensity * grain, kPaperGrainPower);
}

float PaperGrain::darkAt(int x, int y) const {
    const float grain = grainAt((static_cast<float>(x) + kPixelCentre) / m_scale - kPixelCentre,
                                (static_cast<float>(y) + kPixelCentre) / m_scale - kPixelCentre);
    return 1.0f - grain;
}

// The same floats as darkAt at each pixel, as sharesOf's are shareAt's.
void PaperGrain::darksOf(TileCoord coord, float* out) const {
    const QPoint origin = coord.pixelRect().topLeft();
    std::array<Between, Tile::kSize> columns;
    for (int x = 0; x < Tile::kSize; ++x)
        columns[static_cast<std::size_t>(x)] = betweenAt(origin.x() + x, m_width);
    for (int y = 0; y < Tile::kSize; ++y) {
        const Between row = betweenAt(origin.y() + y, m_height);
        const uchar* top = m_picture.constScanLine(row.first);
        const uchar* bottom = m_picture.constScanLine(row.second);
        for (int x = 0; x < Tile::kSize; ++x) {
            const Between& column = columns[static_cast<std::size_t>(x)];
            const float upper = static_cast<float>(top[column.first]) +
                                (static_cast<float>(top[column.second]) - static_cast<float>(top[column.first])) * column.share;
            const float lower = static_cast<float>(bottom[column.first]) +
                                (static_cast<float>(bottom[column.second]) - static_cast<float>(bottom[column.first])) * column.share;
            const float light = (upper + (lower - upper) * row.share) / kGreyLevels;
            out[y * Tile::kSize + x] = 1.0f - (m_invert ? invertedLight(light) : light);
        }
    }
}

const float* PaperGrain::keptDarks(TileCoord coord) const {
    {
        const std::scoped_lock lock(m_keptLock);
        if (const auto found = m_darks.find(coord); found != m_darks.end())
            return found->second.get();
    }
    auto darks = std::make_unique<float[]>(kTilePixels);
    darksOf(coord, darks.get());
    const std::scoped_lock lock(m_keptLock);
    return m_darks.try_emplace(coord, std::move(darks)).first->second.get();
}

float PaperGrain::keptDarkAt(int x, int y) const {
    struct Last {
        std::uint64_t id = 0;
        TileCoord coord;
        const float* darks = nullptr;
    };
    thread_local Last last;
    const TileCoord coord{TiledImage::tileOf(x), TiledImage::tileOf(y)};
    if (last.id != m_id || !(last.coord == coord) || !last.darks)
        last = {m_id, coord, keptDarks(coord)};
    const QPoint origin = coord.pixelRect().topLeft();
    return last.darks[(y - origin.y()) * Tile::kSize + (x - origin.x())];
}

float PaperGrain::shareAt(int x, int y) const {
    return shareOf(grainAt((static_cast<float>(x) + kPixelCentre) / m_scale - kPixelCentre,
                           (static_cast<float>(y) + kPixelCentre) / m_scale - kPixelCentre));
}

PaperGrain::Between PaperGrain::betweenAt(int pixel, int size) const {
    const float at = wrapped((static_cast<float>(pixel) + kPixelCentre) / m_scale - kPixelCentre, static_cast<float>(size));
    const int first = std::min(static_cast<int>(at), size - 1);
    return {first, (first + 1) % size, at - static_cast<float>(first)};
}

const float* PaperGrain::kept(TileCoord coord) const {
    {
        const std::scoped_lock lock(m_keptLock);
        if (const auto found = m_kept.find(coord); found != m_kept.end())
            return found->second.get();
    }
    auto shares = std::make_unique<float[]>(m_alsoKeep ? 2 * kTilePixels : kTilePixels);
    sharesOf(coord, shares.get());
    if (m_alsoKeep)
        for (std::size_t i = 0; i < kTilePixels; ++i)
            shares[kTilePixels + i] = m_alsoKeep(shares[i]);
    const std::scoped_lock lock(m_keptLock);
    // Another thread may have kept the tile meanwhile; either one holds the same floats.
    return m_kept.try_emplace(coord, std::move(shares)).first->second.get();
}

const float* PaperGrain::keptSharesOf(TileCoord coord) const {
    return kept(coord);
}

const float* PaperGrain::keptAlsoOf(TileCoord coord) const {
    return m_alsoKeep ? kept(coord) + kTilePixels : nullptr;
}

// The same floats as shareAt at each pixel, its places along each row and column found once a tile rather than once a pixel.
void PaperGrain::sharesOf(TileCoord coord, float* out) const {
    const QPoint origin = coord.pixelRect().topLeft();
    std::array<Between, Tile::kSize> columns;
    for (int x = 0; x < Tile::kSize; ++x)
        columns[static_cast<std::size_t>(x)] = betweenAt(origin.x() + x, m_width);
    for (int y = 0; y < Tile::kSize; ++y) {
        const Between row = betweenAt(origin.y() + y, m_height);
        const uchar* top = m_picture.constScanLine(row.first);
        const uchar* bottom = m_picture.constScanLine(row.second);
        for (int x = 0; x < Tile::kSize; ++x) {
            const Between& column = columns[static_cast<std::size_t>(x)];
            const float upper = static_cast<float>(top[column.first]) +
                                (static_cast<float>(top[column.second]) - static_cast<float>(top[column.first])) * column.share;
            const float lower = static_cast<float>(bottom[column.first]) +
                                (static_cast<float>(bottom[column.second]) - static_cast<float>(bottom[column.first])) * column.share;
            const float light = (upper + (lower - upper) * row.share) / kGreyLevels;
            out[y * Tile::kSize + x] = shareOf(m_invert ? invertedLight(light) : light);
        }
    }
}

}
