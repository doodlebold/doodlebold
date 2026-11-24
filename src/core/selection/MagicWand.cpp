#include "core/selection/MagicWand.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <vector>

namespace doodlebold::selection {

namespace {

// Enclosed Transparent Area reaches this far under the soft edge of the lines around it.
constexpr int kFringeReach = 2;
// Squared distance standing for "no such pixel anywhere".
constexpr float kFar = 1e20f;

// One value per pixel of the canvas, row by row.
template <typename T> struct Grid {
    int width = 0;
    int height = 0;
    std::vector<T> v;

    Grid(int w, int h) : width(w), height(h), v(std::size_t(w) * std::size_t(h), T{}) {}
    std::size_t index(int x, int y) const { return std::size_t(y) * std::size_t(width) + std::size_t(x); }
    bool contains(int x, int y) const { return x >= 0 && y >= 0 && x < width && y < height; }
    T& at(int x, int y) { return v[index(x, y)]; }
    const T& at(int x, int y) const { return v[index(x, y)]; }
};

using Pixel = std::array<std::uint8_t, 4>;  // premultiplied RGBA, 8 bits a channel
using Mask = Grid<std::uint8_t>;            // 1 inside, 0 outside

void copyTile(const Tile& tile, int tx, int ty, Grid<Pixel>& out) {
    const QRect r = TiledImage::tileRect(tx, ty).intersected(QRect(0, 0, out.width, out.height));
    for (int y = r.top(); y <= r.bottom(); ++y)
        for (int x = r.left(); x <= r.right(); ++x) {
            const Channel* c = tile.at(x - tx * Tile::kSize, y - ty * Tile::kSize);
            out.at(x, y) = {toUnorm8(c[0]), toUnorm8(c[1]), toUnorm8(c[2]), toUnorm8(c[3])};
        }
}

Grid<Pixel> readPixels(const TiledImage& image) {
    Grid<Pixel> out(image.size().width(), image.size().height());
    for (int ty = 0; ty < image.tilesY(); ++ty)
        for (int tx = 0; tx < image.tilesX(); ++tx)
            if (const Tile* t = image.tile(tx, ty))
                copyTile(*t, tx, ty, out);
    return out;
}

// How far apart two colours are: their most different channel, alpha included.
int difference(const Pixel& a, const Pixel& b) {
    int most = 0;
    for (std::size_t c = 0; c < 4; ++c)
        most = std::max(most, std::abs(int(a[c]) - int(b[c])));
    return most;
}

// The pixels the area may take in: transparent enough, or close enough to the clicked colour.
Mask joinable(const Grid<Pixel>& px, const QPoint& seed, const WandOptions& o) {
    Mask out(px.width, px.height);
    const Pixel clicked = px.at(seed.x(), seed.y());
    for (std::size_t i = 0; i < px.v.size(); ++i)
        out.v[i] =
            o.mode == WandMode::EnclosedTransparent ? px.v[i][3] <= o.transparencyRange : difference(px.v[i], clicked) <= o.colourThreshold;
    return out;
}

// The connected part of `allowed` around the seed, a row's run at a time.
Mask floodFrom(const Mask& allowed, const QPoint& seed, bool eight) {
    Mask region(allowed.width, allowed.height);
    if (!allowed.at(seed.x(), seed.y()))
        return region;
    const int reach = eight ? 1 : 0;  // diagonal neighbours: look one pixel further along the rows above and below
    std::vector<QPoint> todo{seed};
    while (!todo.empty()) {
        const QPoint p = todo.back();
        todo.pop_back();
        if (region.at(p.x(), p.y()))
            continue;
        int left = p.x(), right = p.x();
        while (left > 0 && allowed.at(left - 1, p.y()) && !region.at(left - 1, p.y()))
            --left;
        while (right + 1 < allowed.width && allowed.at(right + 1, p.y()) && !region.at(right + 1, p.y()))
            ++right;
        for (int x = left; x <= right; ++x)
            region.at(x, p.y()) = 1;
        for (const int y : {p.y() - 1, p.y() + 1}) {
            if (y < 0 || y >= allowed.height)
                continue;
            bool inRun = false;
            for (int x = std::max(0, left - reach); x <= std::min(allowed.width - 1, right + reach); ++x) {
                const bool open = allowed.at(x, y) && !region.at(x, y);
                if (open && !inRun)
                    todo.emplace_back(x, y);
                inRun = open;
            }
        }
    }
    return region;
}

// The squared distance transform along one row or column (Felzenszwalb and Huttenlocher, "Distance Transforms of Sampled
// Functions", 2012): the lower envelope of the parabolas rooted at each sample.
void distanceAlong(std::vector<float>& f, std::vector<int>& roots, std::vector<float>& bounds) {
    const int n = int(f.size());
    const std::vector<float> in = f;
    int k = 0;
    roots[0] = 0;
    bounds[0] = -kFar;
    bounds[1] = kFar;
    for (int q = 1; q < n; ++q) {
        float s = 0;
        while (true) {
            const int r = roots[std::size_t(k)];
            const double rise = (double(in[std::size_t(q)]) + double(q) * q) - (double(in[std::size_t(r)]) + double(r) * r);
            s = float(rise / double(2 * (q - r)));
            if (s > bounds[std::size_t(k)] || k == 0)
                break;
            --k;
        }
        if (s <= bounds[std::size_t(k)])
            k = -1;  // the first parabola is hidden too: this one starts the envelope
        ++k;
        roots[std::size_t(k)] = q;
        bounds[std::size_t(k)] = k == 0 ? -kFar : s;
        bounds[std::size_t(k) + 1] = kFar;
    }
    k = 0;
    for (int q = 0; q < n; ++q) {
        while (bounds[std::size_t(k) + 1] < float(q))
            ++k;
        const int r = roots[std::size_t(k)];
        f[std::size_t(q)] = float((q - r) * (q - r)) + in[std::size_t(r)];
    }
}

// The squared distance from every pixel to the nearest pixel of `mask` that holds `value`.
Grid<float> squaredDistanceTo(const Mask& mask, std::uint8_t value) {
    Grid<float> d(mask.width, mask.height);
    for (std::size_t i = 0; i < mask.v.size(); ++i)
        d.v[i] = mask.v[i] == value ? 0.0f : kFar;
    const std::size_t longest = std::size_t(std::max(mask.width, mask.height));
    std::vector<float> line;
    std::vector<int> roots(longest);
    std::vector<float> bounds(longest + 1);
    for (int x = 0; x < d.width; ++x) {
        line.resize(std::size_t(d.height));
        for (int y = 0; y < d.height; ++y)
            line[std::size_t(y)] = d.at(x, y);
        distanceAlong(line, roots, bounds);
        for (int y = 0; y < d.height; ++y)
            d.at(x, y) = line[std::size_t(y)];
    }
    for (int y = 0; y < d.height; ++y) {
        line.assign(d.v.begin() + std::ptrdiff_t(d.index(0, y)), d.v.begin() + std::ptrdiff_t(d.index(0, y) + std::size_t(d.width)));
        distanceAlong(line, roots, bounds);
        std::ranges::copy(line, d.v.begin() + std::ptrdiff_t(d.index(0, y)));
    }
    return d;
}

// Gap Tolerance: the area is first found as if the lines around it were thicker by half the gap, so it can't pass through a gap
// that narrow, and then grows back out to the lines. A click in a place narrower than that still fills it.
Mask fillClosingGaps(const Mask& allowed, const QPoint& seed, int gap, bool eight) {
    const Grid<float> toWall = squaredDistanceTo(allowed, 0);
    const float radius = float(gap + 1) / 2.0f;
    const float limit = std::min(radius * radius, toWall.at(seed.x(), seed.y()) - 1.0f);
    Mask roomy(allowed.width, allowed.height);
    for (std::size_t i = 0; i < roomy.v.size(); ++i)
        roomy.v[i] = allowed.v[i] && toWall.v[i] > limit;
    const Mask core = floodFrom(roomy, seed, eight);
    const Grid<float> toCore = squaredDistanceTo(core, 1);
    const float back = std::sqrt(std::max(limit, 0.0f)) + 1.0f;
    Mask near(allowed.width, allowed.height);
    for (std::size_t i = 0; i < near.v.size(); ++i)
        near.v[i] = allowed.v[i] && toCore.v[i] <= back * back;
    return floodFrom(near, seed, eight);
}

// The region's pixels with a neighbour outside it.
std::vector<QPoint> edgeOf(const Mask& region) {
    std::vector<QPoint> edge;
    for (int y = 0; y < region.height; ++y)
        for (int x = 0; x < region.width; ++x) {
            if (!region.at(x, y))
                continue;
            for (const QPoint n : {QPoint(x - 1, y), QPoint(x + 1, y), QPoint(x, y - 1), QPoint(x, y + 1)})
                if (region.contains(n.x(), n.y()) && !region.at(n.x(), n.y())) {
                    edge.emplace_back(x, y);
                    break;
                }
        }
    return edge;
}

// Enclosed Transparent Area reaches under the soft edge of the lines around it, climbing each line's fringe while it gets more
// solid, so a fill leaves no pale ring inside the lines. Never into a fully solid pixel.
void reachUnderLines(Mask& region, const Grid<Pixel>& px, int transparent) {
    std::vector<QPoint> frontier = edgeOf(region);
    for (int step = 0; step < kFringeReach && !frontier.empty(); ++step) {
        std::vector<QPoint> next;
        for (const QPoint p : frontier) {
            const int from = px.at(p.x(), p.y())[3];
            for (const QPoint n :
                 {QPoint(p.x() - 1, p.y()), QPoint(p.x() + 1, p.y()), QPoint(p.x(), p.y() - 1), QPoint(p.x(), p.y() + 1)}) {
                if (!region.contains(n.x(), n.y()) || region.at(n.x(), n.y()))
                    continue;
                const int alpha = px.at(n.x(), n.y())[3];
                if (alpha > transparent && alpha < 255 && alpha >= from) {
                    region.at(n.x(), n.y()) = 1;
                    next.push_back(n);
                }
            }
        }
        frontier = std::move(next);
    }
}

// Anti-Aliasing: a pixel just outside the area is covered by the share of its 3 × 3 neighbourhood inside, weighted 1-2-1 each
// way, so the edge softens outward and the area itself stays whole.
std::uint16_t softEdge(const Mask& region, int x, int y) {
    constexpr int kWeights[3] = {1, 2, 1};
    int inside = 0;
    for (int dy = -1; dy <= 1; ++dy)
        for (int dx = -1; dx <= 1; ++dx)
            if (region.contains(x + dx, y + dy) && region.at(x + dx, y + dy))
                inside += kWeights[dx + 1] * kWeights[dy + 1];
    return std::uint16_t((inside * SelectionMask::kOne + 8) / 16);
}

SelectionMask toSelection(const Mask& region, bool antialias) {
    SelectionMask mask(QSize(region.width, region.height));
    std::vector<std::uint16_t> row(std::size_t(region.width));
    for (int y = 0; y < region.height; ++y) {
        for (int x = 0; x < region.width; ++x)
            row[std::size_t(x)] = region.at(x, y) ? std::uint16_t(SelectionMask::kOne) : antialias ? softEdge(region, x, y) : 0;
        mask.writeRow(y, 0, region.width, row.data());
    }
    mask.squeeze();
    return mask;
}

Mask findArea(const Grid<Pixel>& px, const QPoint& seed, const WandOptions& o) {
    Mask allowed = joinable(px, seed, o);
    if (o.mode == WandMode::AllSimilar)
        return allowed;
    Mask region =
        o.gapTolerance > 0 ? fillClosingGaps(allowed, seed, o.gapTolerance, o.eightConnected) : floodFrom(allowed, seed, o.eightConnected);
    if (o.mode == WandMode::EnclosedTransparent)
        reachUnderLines(region, px, o.transparencyRange);
    return region;
}

}

SelectionMask magicWand(const TiledImage& source, const QPoint& seed, const WandOptions& options) {
    if (!source.rect().contains(seed))
        return SelectionMask(source.size());
    const Grid<Pixel> px = readPixels(source);
    return toSelection(findArea(px, seed, options), options.antialias);
}

}
