#include "core/effects/EffectSampling.h"

#include <algorithm>
#include <cmath>

namespace doodlebold {

namespace {

constexpr int kS = Tile::kSize;
constexpr float kToUnit = 1.0f / kChannelOne;

int floorDiv(int a, int b) {
    return a >= 0 ? a / b : -((-a + b - 1) / b);
}

void addScaled(float* acc, const float* px, float w) {
    for (int c = 0; c < 4; ++c)
        acc[c] += px[c] * w;
}

}

TileGrid::TileGrid(const TiledImage& image, const QRect& edges) : m_image(&image), m_edges(edges) {
    const QRect area = image.nearArea();
    m_tx0 = TiledImage::tileOf(area.left());
    m_ty0 = TiledImage::tileOf(area.top());
    m_cols = TiledImage::tileOf(area.right()) - m_tx0 + 1;
    m_rows = TiledImage::tileOf(area.bottom()) - m_ty0 + 1;
    m_tiles.assign(static_cast<std::size_t>(m_cols) * static_cast<std::size_t>(m_rows), nullptr);
    for (const auto& [coord, tile] : image.tiles()) {
        const int col = coord.x - m_tx0, row = coord.y - m_ty0;
        if (col >= 0 && row >= 0 && col < m_cols && row < m_rows)
            m_tiles[static_cast<std::size_t>(row * m_cols + col)] = tile.get();
    }
}

const Tile* TileGrid::tile(int tx, int ty) const {
    const int col = tx - m_tx0, row = ty - m_ty0;
    if (col < 0 || row < 0 || col >= m_cols || row >= m_rows)
        return m_image->tile(tx, ty);
    return m_tiles[static_cast<std::size_t>(row * m_cols + col)];
}

Px TileGrid::pixel(int x, int y) const {
    if (!m_edges.isEmpty()) {
        x = std::clamp(x, m_edges.left(), m_edges.right());
        y = std::clamp(y, m_edges.top(), m_edges.bottom());
    }
    const Tile* t = tile(TiledImage::tileOf(x), TiledImage::tileOf(y));
    if (!t)
        return {};
    const Channel* p = t->at(x - TiledImage::tileOf(x) * kS, y - TiledImage::tileOf(y) * kS);
    return {p[0] * kToUnit, p[1] * kToUnit, p[2] * kToUnit, p[3] * kToUnit};
}

Px TileGrid::bilinear(float x, float y) const {
    const float fx = x - 0.5f, fy = y - 0.5f;
    const int x0 = static_cast<int>(std::floor(fx)), y0 = static_cast<int>(std::floor(fy));
    const float wx = fx - static_cast<float>(x0), wy = fy - static_cast<float>(y0);
    Px out = pixel(x0, y0) * ((1 - wx) * (1 - wy));
    out += pixel(x0 + 1, y0) * (wx * (1 - wy));
    out += pixel(x0, y0 + 1) * ((1 - wx) * wy);
    out += pixel(x0 + 1, y0 + 1) * (wx * wy);
    return out;
}

// Outside the edges: the run's part left of them repeats their first pixel, the part right of them their last.
void TileGrid::readRun(int y, int x0, int n, float* out) const {
    if (m_edges.isEmpty()) {
        readInside(y, x0, n, out);
        return;
    }
    y = std::clamp(y, m_edges.top(), m_edges.bottom());
    const int a = std::clamp(x0, m_edges.left(), m_edges.right() + 1), b = std::clamp(x0 + n, m_edges.left(), m_edges.right() + 1);
    readInside(y, a, b - a, out + static_cast<std::ptrdiff_t>(a - x0) * 4);
    const auto repeat = [&](int from, int to, int source) {
        const Px p = pixel(source, y);
        for (int x = from; x < to; ++x)
            std::copy(&p.r, &p.r + 4, out + static_cast<std::ptrdiff_t>(x - x0) * 4);
    };
    repeat(x0, a, m_edges.left());
    repeat(b, x0 + n, m_edges.right());
}

void TileGrid::readInside(int y, int x0, int n, float* out) const {
    const int ty = TiledImage::tileOf(y), row = y - ty * kS;
    for (int x = x0; x < x0 + n;) {
        const int tx = TiledImage::tileOf(x), col = x - tx * kS;
        const int run = std::min(kS - col, x0 + n - x);
        const Tile* t = tile(tx, ty);
        float* dst = out + static_cast<std::ptrdiff_t>(x - x0) * 4;
        if (!t)
            std::fill(dst, dst + run * 4, 0.0f);
        else
            std::transform(t->at(col, row), t->at(col, row) + run * 4, dst, [](Channel v) { return v * kToUnit; });
        x += run;
    }
}

void TileGrid::read(const QRect& r, float* out) const {
    for (int y = r.top(); y <= r.bottom(); ++y)
        readRun(y, r.left(), r.width(), out + static_cast<std::ptrdiff_t>(y - r.top()) * r.width() * 4);
}

std::vector<float> gaussianKernel(double sigma) {
    const int radius = std::max(1, static_cast<int>(std::ceil(3 * sigma)));
    std::vector<float> k(static_cast<std::size_t>(2 * radius + 1));
    double sum = 0;
    for (int i = -radius; i <= radius; ++i) {
        const double w = std::exp(-(i * i) / (2 * sigma * sigma));
        k[static_cast<std::size_t>(i + radius)] = static_cast<float>(w);
        sum += w;
    }
    for (float& w : k)
        w = static_cast<float>(w / sum);
    return k;
}

void convolve(const float* in, int inStride, float* out, int outStride, int n, const std::vector<float>& kernel) {
    const int taps = static_cast<int>(kernel.size());
    for (int i = 0; i < n; ++i) {
        float acc[4] = {0, 0, 0, 0};
        for (int j = 0; j < taps; ++j)
            addScaled(acc, in + static_cast<std::ptrdiff_t>(i + j) * inStride * 4, kernel[static_cast<std::size_t>(j)]);
        std::copy(acc, acc + 4, out + static_cast<std::ptrdiff_t>(i) * outStride * 4);
    }
}

BlurField::BlurField(const TileGrid& grid, double sigma) : m_grid(grid), m_sigma(std::max(sigma, 0.1)) {
    if (m_sigma <= kDirectSigma) {
        m_kernel = gaussianKernel(m_sigma);
        m_reach = static_cast<int>(m_kernel.size() / 2);
        return;
    }
    // The reduction (a k-wide box) and the enlargement (a 2k-wide tent) blur too, by k²/12 + k²/6 in variance; the reduced
    // picture's own blur makes up the rest.
    m_factor = static_cast<int>(m_sigma / 4);
    const double k = m_factor;
    m_kernel = gaussianKernel(std::sqrt(m_sigma * m_sigma - k * k / 4) / k);
    m_reach = static_cast<int>(m_kernel.size() / 2 + 2) * m_factor;
}

void BlurField::setArea(const QRect& area) {
    if (m_factor == 1)
        return;
    const QRect source = area.adjusted(-m_reach, -m_reach, m_reach, m_reach);
    m_origin = QPoint(floorDiv(source.left(), m_factor) * m_factor, floorDiv(source.top(), m_factor) * m_factor);
    m_width = (source.right() + 1 - m_origin.x() + m_factor - 1) / m_factor;
    m_height = (source.bottom() + 1 - m_origin.y() + m_factor - 1) / m_factor;
}

void BlurField::prepare(const ParallelRun& parallel) {
    if (m_factor == 1)
        return;
    m_reduced.assign(static_cast<std::size_t>(m_width) * static_cast<std::size_t>(m_height) * 4, 0.0f);
    const auto run = [&](int n, const std::function<void(int)>& f) {
        if (parallel)
            parallel(n, f);
        else
            for (int i = 0; i < n; ++i)
                f(i);
    };
    run(m_height, [this](int row) { reduceRow(row); });
    run(m_height, [this](int row) { blurReducedRow(row); });
    run(m_width, [this](int column) { blurReducedColumn(column); });
}

// One row of the reduced picture: each pixel the mean of the k × k layer pixels it stands for.
void BlurField::reduceRow(int row) {
    const int k = m_factor;
    std::vector<float> strip(static_cast<std::size_t>(m_width * k * k * 4));
    m_grid.read(QRect(m_origin.x(), m_origin.y() + row * k, m_width * k, k), strip.data());
    float* dst = m_reduced.data() + static_cast<std::ptrdiff_t>(row) * m_width * 4;
    const float share = 1.0f / static_cast<float>(k * k);
    for (int y = 0; y < k; ++y)
        for (int x = 0; x < m_width * k; ++x)
            addScaled(dst + (x / k) * 4, strip.data() + static_cast<std::ptrdiff_t>(y * m_width * k + x) * 4, share);
}

void BlurField::blurReducedRow(int row) {
    const int radius = static_cast<int>(m_kernel.size() / 2);
    std::vector<float> padded(static_cast<std::size_t>((m_width + 2 * radius) * 4), 0.0f);
    float* line = m_reduced.data() + static_cast<std::ptrdiff_t>(row) * m_width * 4;
    std::copy(line, line + m_width * 4, padded.begin() + radius * 4);
    convolve(padded.data(), 1, line, 1, m_width, m_kernel);
}

void BlurField::blurReducedColumn(int column) {
    const int radius = static_cast<int>(m_kernel.size() / 2);
    std::vector<float> padded(static_cast<std::size_t>((m_height + 2 * radius) * 4), 0.0f);
    float* top = m_reduced.data() + static_cast<std::ptrdiff_t>(column) * 4;
    for (int y = 0; y < m_height; ++y)
        std::copy(top + static_cast<std::ptrdiff_t>(y) * m_width * 4, top + static_cast<std::ptrdiff_t>(y) * m_width * 4 + 4,
                  padded.begin() + (y + radius) * 4);
    convolve(padded.data(), 1, top, m_width, m_height, m_kernel);
}

void BlurField::tile(TileCoord c, float* out) const {
    if (m_factor == 1)
        directTile(c, out);
    else
        reducedTile(c, out);
}

// Across the tile and its neighbourhood, then down.
void BlurField::directTile(TileCoord c, float* out) const {
    const int r = m_reach, rows = kS + 2 * r;
    const QRect tile = c.pixelRect();
    std::vector<float> patch(static_cast<std::size_t>((kS + 2 * r) * rows * 4));
    m_grid.read(tile.adjusted(-r, -r, r, r), patch.data());
    std::vector<float> across(static_cast<std::size_t>(kS * rows * 4));
    for (int y = 0; y < rows; ++y)
        convolve(patch.data() + static_cast<std::ptrdiff_t>(y) * (kS + 2 * r) * 4, 1,
                 across.data() + static_cast<std::ptrdiff_t>(y) * kS * 4, 1, kS, m_kernel);
    for (int x = 0; x < kS; ++x)
        convolve(across.data() + x * 4, kS, out + x * 4, kS, kS, m_kernel);
}

// Each layer pixel mixes the four reduced pixels around it.
void BlurField::reducedTile(TileCoord c, float* out) const {
    const QRect tile = c.pixelRect();
    const float k = static_cast<float>(m_factor);
    const auto at = [&](int x, int y) {
        x = std::clamp(x, 0, m_width - 1);
        y = std::clamp(y, 0, m_height - 1);
        return m_reduced.data() + static_cast<std::ptrdiff_t>(y * m_width + x) * 4;
    };
    for (int y = 0; y < kS; ++y) {
        const float v = (static_cast<float>(tile.top() + y - m_origin.y()) + 0.5f) / k - 0.5f;
        const int y0 = static_cast<int>(std::floor(v));
        const float wy = v - static_cast<float>(y0);
        for (int x = 0; x < kS; ++x) {
            const float u = (static_cast<float>(tile.left() + x - m_origin.x()) + 0.5f) / k - 0.5f;
            const int x0 = static_cast<int>(std::floor(u));
            const float wx = u - static_cast<float>(x0);
            float* dst = out + (y * kS + x) * 4;
            std::fill(dst, dst + 4, 0.0f);
            addScaled(dst, at(x0, y0), (1 - wx) * (1 - wy));
            addScaled(dst, at(x0 + 1, y0), wx * (1 - wy));
            addScaled(dst, at(x0, y0 + 1), (1 - wx) * wy);
            addScaled(dst, at(x0 + 1, y0 + 1), wx * wy);
        }
    }
}

}
