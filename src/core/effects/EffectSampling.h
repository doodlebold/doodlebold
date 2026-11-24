#pragma once

#include "core/effects/EffectRender.h"
#include "core/raster/TiledImage.h"

#include <QPoint>
#include <QRect>

#include <vector>

namespace doodlebold {

// A premultiplied pixel, each channel 0 … 1.
struct Px {
    float r = 0, g = 0, b = 0, a = 0;

    Px& operator+=(const Px& o) {
        r += o.r;
        g += o.g;
        b += o.b;
        a += o.a;
        return *this;
    }
    Px operator*(float w) const { return {r * w, g * w, b * w, a * w}; }
};

inline constexpr int kTilePixels = Tile::kSize * Tile::kSize;

// The layer's tiles in a flat table over its whole paint area, so reading a pixel costs no hash lookup. With `edges`, a pixel
// outside it reads as the nearest one inside (the canvas's border carries on outwards).
class TileGrid {
public:
    explicit TileGrid(const TiledImage& image, const QRect& edges = {});

    const Tile* tile(int tx, int ty) const;
    Px pixel(int x, int y) const;
    // At a point where pixel (i, j) has its centre at (i + 0.5, j + 0.5); between centres it mixes the four around.
    Px bilinear(float x, float y) const;
    // The pixels of r, row by row, 4 floats each.
    void read(const QRect& r, float* out) const;

private:
    void readRun(int y, int x0, int n, float* out) const;
    void readInside(int y, int x0, int n, float* out) const;

    QRect m_edges;
    int m_tx0 = 0, m_ty0 = 0, m_cols = 0, m_rows = 0;
    std::vector<const Tile*> m_tiles;
};

// A Gaussian blur of the layer, read a tile at a time. Up to kDirectSigma it convolves each tile's own neighbourhood; above,
// it blurs a picture reduced k times once and enlarges it again for each tile, so a wide blur costs about as much as a
// narrow one.
class BlurField {
public:
    static constexpr double kDirectSigma = 12;

    // sigma in pixels.
    BlurField(const TileGrid& grid, double sigma);

    // How far from a pixel the blur reads.
    int reach() const { return m_reach; }
    // Where tile() will be asked for; then prepare() once.
    void setArea(const QRect& area);
    void prepare(const ParallelRun& parallel);
    // The blurred tile, kTilePixels pixels of 4 floats.
    void tile(TileCoord c, float* out) const;

private:
    void directTile(TileCoord c, float* out) const;
    void reducedTile(TileCoord c, float* out) const;
    void reduceRow(int row);
    void blurReducedRow(int row);
    void blurReducedColumn(int column);

    const TileGrid& m_grid;
    double m_sigma;
    int m_reach = 0;
    std::vector<float> m_kernel;  // 2·radius + 1 weights, for the direct blur or the reduced picture's
    int m_factor = 1;             // k: one reduced pixel is k × k of the layer's
    QPoint m_origin;              // the layer pixel where the reduced picture starts
    int m_width = 0, m_height = 0;
    std::vector<float> m_reduced;
};

std::vector<float> gaussianKernel(double sigma);

// out[i] = Σ kernel[j] · in[i + j] for n pixels of 4 floats; `in` holds n + kernel.size() - 1 pixels. Strides in pixels.
void convolve(const float* in, int inStride, float* out, int outStride, int n, const std::vector<float>& kernel);

}
