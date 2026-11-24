#pragma once

#include "core/raster/TiledImage.h"

#include <QImage>

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <unordered_map>

namespace doodlebold {

// A texture's grain, fixed to the canvas and repeating: at each pixel, how much of a coat the paper lets through, (1 - Intensity +
// Intensity · grain)^kPaperGrainPower, where the grain is the picture's lightness (Invert: darkness), as the reference reads a paper,
// bilinear between its pixels' centres and wrapped, a picture pixel to a canvas pixel at Scale 100 %. The merge thins a stroke's
// paint by it, beside a
// selection's share, so it changes nothing about how the stroke is laid.
// The top and bottom a picture is turned over about for erasing, and a grey turned so: top - grey + 2 bottom, within 0 to 1.
struct TurnedRange {
    float top = 1.0f, bottom = 0.0f;
    float turned(float light) const;
};
TurnedRange turnedRangeOf(const QImage& grey);

class PaperGrain {
public:
    // `scale` and `intensity` are the panel's Scale and Intensity, as shares; `invert` makes dark where paint goes.
    PaperGrain(const QImage& picture, double scale, double intensity, bool invert, bool turnedAtLightest = false);
    // A second number kept beside each pixel's share, worked out from it once a tile (keptAlsoOf): a mixing tool's whole coat.
    void keepAlso(std::function<float(float)> fromShare) { m_alsoKeep = std::move(fromShare); }

    // Each pixel of the tile at `coord`, in a tile's order.
    void sharesOf(TileCoord coord, float* out) const;
    // The same, worked out once a tile for the stroke and kept: a tile is merged again at every frame it changes. From any thread.
    const float* keptSharesOf(TileCoord coord) const;
    // keepAlso's numbers for the tile, kept with its shares; null without keepAlso.
    const float* keptAlsoOf(TileCoord coord) const;
    float shareAt(int x, int y) const;
    // How much the paper takes away at a pixel at full Intensity: its darkness, 1 - its grain.
    float darkAt(int x, int y) const;
    // darkAt's floats, kept a tile at a time and read through each thread's last tile, for the per-dab cut's every stamp and pixel.
    float keptDarkAt(int x, int y) const;
    // The tile's darkAt floats, a row after another.
    const float* keptDarksOf(TileCoord coord) const { return keptDarks(coord); }
    // The panel's Intensity, as a share: how deep the paper is.
    float intensity() const { return m_intensity; }

private:
    // Where a pixel's centre falls on the picture along one axis: the picture pixels either side, and how far from the first.
    struct Between {
        int first = 0;
        int second = 0;
        float share = 0.0f;
    };
    Between betweenAt(int pixel, int size) const;
    float grainAt(float u, float v) const;
    float shareOf(float grain) const;

    static constexpr std::size_t kTilePixels = Tile::kSize * Tile::kSize;
    // Each tile's shares, then keepAlso's numbers where there's keepAlso.
    const float* kept(TileCoord coord) const;
    mutable std::mutex m_keptLock;
    void darksOf(TileCoord coord, float* out) const;
    const float* keptDarks(TileCoord coord) const;
    mutable std::unordered_map<TileCoord, std::unique_ptr<float[]>, TileCoordHash> m_darks;
    std::uint64_t m_id = 0;  // which paper a thread's last tile belongs to; never reused, unlike an address
    mutable std::unordered_map<TileCoord, std::unique_ptr<float[]>, TileCoordHash> m_kept;
    std::function<float(float)> m_alsoKeep;
    QImage m_picture;  // 8-bit grey
    int m_width = 1, m_height = 1;
    float m_scale = 1.0f;
    float m_intensity = 1.0f;
    bool m_invert = false;
    // Turned over for erasing: a grey g reads as m_range.turned(g), in the merge's shares and the darkness a
    // stamp is cut by alike. The plain Invert switch flips it as 1 - g.
    bool m_turned = false;
    TurnedRange m_range;
    float invertedLight(float light) const;
};

}
