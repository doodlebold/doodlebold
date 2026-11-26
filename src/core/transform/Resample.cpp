#include "core/transform/Resample.h"

#include "core/util/ParallelFor.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <mutex>
#include <vector>

namespace doodlebold::transform {

namespace {

constexpr int kTile = Tile::kSize;
int floorDiv(int a, int b) {
    return a >= 0 ? a / b : -((-a + b - 1) / b);
}

template <int N> struct Plane {
    QRect rect;
    std::vector<std::uint16_t> v;

    bool empty() const { return v.empty(); }
    bool contains(int x, int y) const { return rect.contains(x, y); }
    const std::uint16_t* at(int x, int y) const {
        return &v[(static_cast<std::size_t>(y - rect.top()) * rect.width() + (x - rect.left())) * N];
    }
    std::uint16_t* at(int x, int y) { return &v[(static_cast<std::size_t>(y - rect.top()) * rect.width() + (x - rect.left())) * N]; }

    struct Cursor {
        const Plane* plane;
        const std::uint16_t* at(int x, int y) { return plane->at(x, y); }
    };
    Cursor cursor() const { return {this}; }
    // The parts of its rect, a tile at a time (a mask's plane is the canvas's size at most).
    template <typename Visit> void forEachTile(Visit&& visit) const {
        for (int ty = floorDiv(rect.top(), kTile); ty <= floorDiv(rect.bottom(), kTile); ++ty)
            for (int tx = floorDiv(rect.left(), kTile); tx <= floorDiv(rect.right(), kTile); ++tx)
                visit(TiledImage::tileRect(tx, ty).intersected(rect));
    }
};

// A layer's pixels, read in its tiles where they are: content moved far apart holds mostly nothing between, so the box round it
// isn't copied whole. A cursor keeps the last tile it read, as neighbouring pixels read the same one.
struct ImageSource {
    const TiledImage* image = nullptr;
    QRect rect;

    bool empty() const { return rect.isEmpty(); }
    bool contains(int x, int y) const { return rect.contains(x, y); }

    struct Cursor {
        const TiledImage* image;
        int tx = std::numeric_limits<int>::min(), ty = std::numeric_limits<int>::min();
        const Tile* tile = nullptr;
        const std::uint16_t* at(int x, int y) {
            static const std::array<std::uint16_t, 4> kNothing{};
            const int cx = floorDiv(x, kTile), cy = floorDiv(y, kTile);
            if (cx != tx || cy != ty) {
                tx = cx, ty = cy;
                tile = image->tile(cx, cy);
            }
            return tile ? tile->at(x - cx * kTile, y - cy * kTile) : kNothing.data();
        }
    };
    Cursor cursor() const { return {image}; }
    // The parts of its rect the image has tiles in.
    template <typename Visit> void forEachTile(Visit&& visit) const {
        for (const auto& [c, t] : image->tiles())
            if (const QRect part = c.pixelRect().intersected(rect); t && !part.isEmpty())
                visit(part);
    }
};

ImageSource planeOf(const TiledImage& img, const QRect& clip) {
    return {&img, img.contentBounds(false).intersected(clip)};
}

std::int64_t tilesIn(const QRect& r) {
    return std::int64_t{floorDiv(r.right(), kTile) - floorDiv(r.left(), kTile) + 1} * (floorDiv(r.bottom(), kTile) - floorDiv(r.top(), kTile) + 1);
}

void addTilesOf(const QRect& r, std::vector<TileCoord>& out) {
    for (int ty = floorDiv(r.top(), kTile); ty <= floorDiv(r.bottom(), kTile); ++ty)
        for (int tx = floorDiv(r.left(), kTile); tx <= floorDiv(r.right(), kTile); ++tx)
            out.push_back({tx, ty});
}

void sortUnique(std::vector<TileCoord>& tiles) {
    std::sort(tiles.begin(), tiles.end(), [](TileCoord a, TileCoord b) { return a.y != b.y ? a.y < b.y : a.x < b.x; });
    tiles.erase(std::unique(tiles.begin(), tiles.end()), tiles.end());
}

Plane<1> planeOf(const SelectionMask& m, const QRect& clip) {
    Plane<1> p;
    p.rect = m.bounds().intersected(clip);
    if (p.rect.isEmpty())
        return p;
    p.v.assign(static_cast<std::size_t>(p.rect.width()) * p.rect.height(), 0);
    for (int y = p.rect.top(); y <= p.rect.bottom(); ++y)
        m.row(y, p.rect.left(), p.rect.width(), p.at(p.rect.left(), y));
    return p;
}

double cross(const QPointF& a, const QPointF& b) {
    return a.x() * b.y() - a.y() * b.x();
}

struct QuadMap {
    QPointF a, e, f, g;
    QTransform projective;  // quad → unit square, for the Perspective blend
    QTransform toQuad;      // unit square → quad, its inverse
    bool hasProjective = false;
    double perspective = 0.0;

    QuadMap(const Quad& q, double persp) : perspective(std::clamp(persp, 0.0, 1.0)) {
        a = q.corner[0];
        e = q.corner[1] - q.corner[0];
        f = q.corner[3] - q.corner[0];
        g = q.corner[0] - q.corner[1] + q.corner[2] - q.corner[3];
        if (perspective > 0.0)
            hasProjective = QTransform::quadToSquare(QPolygonF({q.corner[0], q.corner[1], q.corner[2], q.corner[3]}), projective);
        if (hasProjective)
            toQuad = projective.inverted();
    }

    QPointF bilinear(double u, double v) const { return a + e * u + f * v + g * (u * v); }

    // Where the destination pixels reading from the part (u0, v0) – (u1, v1) of the unit square lie. The bilinear mapping keeps a
    // rectangle's corners outermost; a blend towards the projective one lands near the line between where each puts a point,
    // and a margin of their distance apart covers a blend that leans off that line.
    QRectF lands(double u0, double v0, double u1, double v1) const {
        const std::array<QPointF, 4> corners{QPointF(u0, v0), QPointF(u1, v0), QPointF(u1, v1), QPointF(u0, v1)};
        QPolygonF points;
        double margin = 0.0;
        for (const QPointF& c : corners) {
            const QPointF b = bilinear(c.x(), c.y());
            points << b;
            if (hasProjective) {
                const QPointF p = toQuad.map(c);
                points << p;
                margin = std::max(margin, std::hypot(p.x() - b.x(), p.y() - b.y()));
            }
        }
        return points.boundingRect().adjusted(-margin, -margin, margin, margin);
    }

    bool inverse(double x, double y, double& u, double& v) const {
        const QPointF h(x - a.x(), y - a.y());
        const double k2 = cross(g, f), k1 = cross(e, f) + cross(h, g), k0 = cross(h, e);
        const double scale = std::max(1e-12, std::hypot(e.x(), e.y()) * std::hypot(f.x(), f.y()));
        double roots[2];
        int n = 0;
        if (std::abs(k2) < 1e-9 * scale) {  // a parallelogram: linear
            if (std::abs(k1) < 1e-12 * scale)
                return false;
            roots[n++] = -k0 / k1;
        } else {
            const double disc = k1 * k1 - 4.0 * k0 * k2;
            if (disc < 0.0)
                return false;
            const double s = std::sqrt(disc);
            roots[n++] = (-k1 - s) / (2.0 * k2);
            roots[n++] = (-k1 + s) / (2.0 * k2);
        }
        double best = 1e300;
        for (int i = 0; i < n; ++i) {
            const double vv = roots[i];
            const double dx = e.x() + g.x() * vv, dy = e.y() + g.y() * vv;
            if (std::max(std::abs(dx), std::abs(dy)) < 1e-12)
                continue;
            const double uu = std::abs(dx) >= std::abs(dy) ? (h.x() - f.x() * vv) / dx : (h.y() - f.y() * vv) / dy;
            const double outside = std::max({0.0, -uu, uu - 1.0}) + std::max({0.0, -vv, vv - 1.0});
            if (outside < best) {
                best = outside;
                u = uu;
                v = vv;
            }
        }
        if (best == 1e300)
            return false;
        if (hasProjective) {
            const QPointF p = projective.map(QPointF(x, y));
            u += (p.x() - u) * perspective;
            v += (p.y() - v) * perspective;
        }
        return true;
    }
};

template <int N, typename Fill, typename Emit> void renderTiles(const std::vector<TileCoord>& tiles, Fill&& fill, Emit&& deliver) {
    parallelFor(static_cast<int>(tiles.size()), [&](int index) {
        const TileCoord c = tiles[static_cast<std::size_t>(index)];
        std::vector<std::uint16_t> values(static_cast<std::size_t>(kTile) * kTile * N, 0);
        if (fill(c.x, c.y, values.data()))
            deliver(c.x, c.y, values);
    });
}

template <int N, typename Source, typename Emit>
void resamplePlane(const Source& source, const QRect& box, const Quad& dest, double perspective, const QRect& limit,
                   std::int64_t denseTiles, Emit& deliver) {
    if (source.empty() || box.isEmpty())
        return;
    const QRect landing =
        QPolygonF({dest.corner[0], dest.corner[1], dest.corner[2], dest.corner[3]}).boundingRect().toAlignedRect() & limit;
    if (landing.isEmpty())
        return;
    const QuadMap map(dest, perspective);
    std::vector<TileCoord> tiles;
    if (!visitOnlyLandingTiles() && tilesIn(landing) <= denseTiles) {
        addTilesOf(landing, tiles);
    } else {
        const double w = box.width(), h = box.height();
        source.forEachTile([&](const QRect& part) {
            const QRectF lands = map.lands((part.left() - box.left()) / w, (part.top() - box.top()) / h,
                                           (part.right() + 1 - box.left()) / w, (part.bottom() + 1 - box.top()) / h);
            // A pixel takes what is under its centre, half a pixel in from its edge.
            if (const QRect r = lands.toAlignedRect().adjusted(-1, -1, 1, 1) & landing; !r.isEmpty())
                addTilesOf(r, tiles);
        });
        sortUnique(tiles);
    }
    renderTiles<N>(
        tiles,
        [&](int tx, int ty, std::uint16_t* values) {
            const QRect part = TiledImage::tileRect(tx, ty) & landing;
            auto read = source.cursor();
            bool any = false;
            for (int y = part.top(); y <= part.bottom(); ++y)
                for (int x = part.left(); x <= part.right(); ++x) {
                    double u, v;
                    if (!map.inverse(x + 0.5, y + 0.5, u, v) || u < 0.0 || u > 1.0 || v < 0.0 || v > 1.0)
                        continue;
                    const int sx = std::min(box.left() + int(std::floor(u * box.width())), box.right());
                    const int sy = std::min(box.top() + int(std::floor(v * box.height())), box.bottom());
                    if (!source.contains(sx, sy))
                        continue;
                    const std::uint16_t* q = read.at(sx, sy);
                    std::copy_n(q, N, values + (static_cast<std::size_t>(y - ty * kTile) * kTile + (x - tx * kTile)) * N);
                    any = any || q[N - 1] != 0;
                }
            return any;
        },
        deliver);
}

QPoint sourceOf(const QRect& area, Turn turn, const QPoint& offset, int x, int y) {
    const int w = area.width(), h = area.height();
    const int X = x - offset.x(), Y = y - offset.y();
    const QPoint origin = movedArea(area, turn).topLeft();
    switch (turn) {
    case Turn::None: return {X, Y};
    case Turn::FlipHorizontal: return {2 * area.left() + w - 1 - X, Y};
    case Turn::FlipVertical: return {X, 2 * area.top() + h - 1 - Y};
    case Turn::Rotate180: return {2 * area.left() + w - 1 - X, 2 * area.top() + h - 1 - Y};
    case Turn::Clockwise: return {area.left() + (Y - origin.y()), area.top() + (h - 1 - (X - origin.x()))};
    case Turn::CounterClockwise: return {area.left() + (w - 1 - (Y - origin.y())), area.top() + (X - origin.x())};
    }
    return {X, Y};
}

// Where sourceOf()'s pixel (sx, sy) lands: its inverse.
QPoint destinationOf(const QRect& area, Turn turn, const QPoint& offset, int sx, int sy) {
    const int w = area.width(), h = area.height();
    const QPoint origin = movedArea(area, turn).topLeft();
    QPoint d(sx, sy);
    switch (turn) {
    case Turn::None: break;
    case Turn::FlipHorizontal: d = {2 * area.left() + w - 1 - sx, sy}; break;
    case Turn::FlipVertical: d = {sx, 2 * area.top() + h - 1 - sy}; break;
    case Turn::Rotate180: d = {2 * area.left() + w - 1 - sx, 2 * area.top() + h - 1 - sy}; break;
    case Turn::Clockwise: d = {origin.x() + h - 1 - (sy - area.top()), origin.y() + (sx - area.left())}; break;
    case Turn::CounterClockwise: d = {origin.x() + (sy - area.top()), origin.y() + w - 1 - (sx - area.left())}; break;
    }
    return d + offset;
}

template <int N, typename Source, typename Emit>
void moveExactlyPlane(const Source& source, const QRect& area, Turn turn, const QPoint& offset, const QRect& limit,
                      std::int64_t denseTiles, Emit& deliver) {
    if (source.empty())
        return;
    const QRect target = movedArea(area, turn, offset).intersected(limit);
    if (target.isEmpty())
        return;
    std::vector<TileCoord> tiles;
    if (!visitOnlyLandingTiles() && tilesIn(target) <= denseTiles) {
        addTilesOf(target, tiles);
    } else {
        source.forEachTile([&](const QRect& part) {
            const QPoint a = destinationOf(area, turn, offset, part.left(), part.top());
            const QPoint b = destinationOf(area, turn, offset, part.right(), part.bottom());
            const QRect lands(QPoint(std::min(a.x(), b.x()), std::min(a.y(), b.y())), QPoint(std::max(a.x(), b.x()), std::max(a.y(), b.y())));
            if (const QRect r = lands & target; !r.isEmpty())
                addTilesOf(r, tiles);
        });
        sortUnique(tiles);
    }
    renderTiles<N>(
        tiles,
        [&](int tx, int ty, std::uint16_t* values) {
            const QRect part = TiledImage::tileRect(tx, ty).intersected(target);
            auto read = source.cursor();
            bool any = false;
            for (int y = part.top(); y <= part.bottom(); ++y)
                for (int x = part.left(); x <= part.right(); ++x) {
                    const QPoint s = sourceOf(area, turn, offset, x, y);
                    if (!source.contains(s.x(), s.y()))
                        continue;
                    const std::uint16_t* q = read.at(s.x(), s.y());
                    std::uint16_t* out = values + (static_cast<std::size_t>(y - ty * kTile) * kTile + (x - tx * kTile)) * N;
                    std::copy_n(q, N, out);
                    any = any || q[N - 1] != 0;
                }
            return any;
        },
        deliver);
}

struct ImageSink {
    explicit ImageSink(const QSize& size) : out(size) {}
    void operator()(int tx, int ty, const std::vector<std::uint16_t>& values) {
        auto tile = std::make_shared<Tile>();
        std::ranges::copy(values, tile->px.begin());
        const std::scoped_lock guard(lock);
        out.setTile(tx, ty, std::move(tile));
    }
    TiledImage out;
    std::mutex lock;
};

struct MaskSink {
    explicit MaskSink(const QSize& size) : out(size) {}
    void operator()(int tx, int ty, const std::vector<std::uint16_t>& values) {
        auto tile = std::make_shared<SelectionTile>();
        std::ranges::copy(values, tile->v.begin());
        const std::scoped_lock guard(lock);
        out.setTile(tx, ty, std::move(tile));
    }
    SelectionMask out;
    std::mutex lock;
};

}

std::atomic<bool>& visitOnlyLandingTiles() {
    static std::atomic<bool> on{false};
    return on;
}

Interpolation interactiveQuality(Interpolation final, Interpolation preview, Interpolation dragging, bool isDragging) {
    const Interpolation q = std::min(final, preview);
    return isDragging ? std::min(q, dragging) : q;
}

Quad Quad::of(const QRectF& r) {
    return {{r.topLeft(), r.topRight(), r.bottomRight(), r.bottomLeft()}};
}

Quad Quad::transformed(const QTransform& t) const {
    return {{t.map(corner[0]), t.map(corner[1]), t.map(corner[2]), t.map(corner[3])}};
}

TiledImage resample(const TiledImage& src, const QRect& sourceRect, const Quad& dest, Interpolation, double perspective) {
    ImageSink sink(src.size());
    // The canvas, and what lands past its edge, kept as paint is. The corner stays on a tile's edge.
    const QRect landing = QPolygonF({dest.corner[0], dest.corner[1], dest.corner[2], dest.corner[3]}).boundingRect().toAlignedRect();
    const QPoint corner(floorDiv(landing.left(), kTile) * kTile, floorDiv(landing.top(), kTile) * kTile);
    const QRect reach = (src.rect() | QRect(corner, landing.bottomRight())).intersected(src.paintArea());
    resamplePlane<4>(planeOf(src, sourceRect), sourceRect, dest, perspective, reach, tilesIn(src.nearArea()), sink);
    sink.out.squeeze();
    return std::move(sink.out);
}

SelectionMask resample(const SelectionMask& src, const QRect& sourceRect, const Quad& dest, Interpolation, double perspective) {
    MaskSink sink(src.size());
    resamplePlane<1>(planeOf(src, sourceRect), sourceRect, dest, perspective, src.rect(), tilesIn(src.rect()), sink);
    sink.out.squeeze();
    return std::move(sink.out);
}

QRect movedArea(const QRect& area, Turn turn, const QPoint& offset) {
    QRect r = area;
    if (turn == Turn::Clockwise || turn == Turn::CounterClockwise) {
        const int w = area.width(), h = area.height();
        r = QRect(area.left() + floorDiv(w - h, 2), area.top() + floorDiv(h - w, 2), h, w);
    }
    return r.translated(offset);
}

TiledImage moveExactly(const TiledImage& src, const QRect& area, Turn turn, const QPoint& offset) {
    return moveExactly(src, area, turn, offset, src.size());
}

SelectionMask moveExactly(const SelectionMask& src, const QRect& area, Turn turn, const QPoint& offset) {
    return moveExactly(src, area, turn, offset, src.size());
}

TiledImage moveExactly(const TiledImage& src, const QRect& area, Turn turn, const QPoint& offset, const QSize& target) {
    ImageSink sink(target);
    moveExactlyPlane<4>(planeOf(src, area), area, turn, offset, sink.out.paintArea(), tilesIn(sink.out.nearArea()), sink);
    sink.out.squeeze();
    return std::move(sink.out);
}

SelectionMask moveExactly(const SelectionMask& src, const QRect& area, Turn turn, const QPoint& offset, const QSize& target) {
    MaskSink sink(target);
    const QRect canvas(QPoint(0, 0), target);
    moveExactlyPlane<1>(planeOf(src, area), area, turn, offset, canvas, tilesIn(canvas), sink);
    sink.out.squeeze();
    return std::move(sink.out);
}

}
