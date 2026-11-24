#include "core/transform/Resample.h"

#include "core/util/ParallelFor.h"

#include <algorithm>
#include <cmath>
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

    bool contains(int x, int y) const { return rect.contains(x, y); }
    const std::uint16_t* at(int x, int y) const {
        return &v[(static_cast<std::size_t>(y - rect.top()) * rect.width() + (x - rect.left())) * N];
    }
    std::uint16_t* at(int x, int y) { return &v[(static_cast<std::size_t>(y - rect.top()) * rect.width() + (x - rect.left())) * N]; }
};

Plane<4> planeOf(const TiledImage& img, const QRect& clip) {
    Plane<4> p;
    p.rect = img.contentBounds(false).intersected(clip);
    if (p.rect.isEmpty())
        return p;
    p.v.assign(static_cast<std::size_t>(p.rect.width()) * p.rect.height() * 4, 0);
    for (const auto& [c, t] : img.tiles()) {
        const QRect part = c.pixelRect().intersected(p.rect);
        if (!t || part.isEmpty())
            continue;
        for (int y = part.top(); y <= part.bottom(); ++y)
            std::copy_n(t->at(part.left() - c.x * kTile, y - c.y * kTile), part.width() * 4, p.at(part.left(), y));
    }
    return p;
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
    bool hasProjective = false;
    double perspective = 0.0;

    QuadMap(const Quad& q, double persp) : perspective(std::clamp(persp, 0.0, 1.0)) {
        a = q.corner[0];
        e = q.corner[1] - q.corner[0];
        f = q.corner[3] - q.corner[0];
        g = q.corner[0] - q.corner[1] + q.corner[2] - q.corner[3];
        if (perspective > 0.0)
            hasProjective = QTransform::quadToSquare(QPolygonF({q.corner[0], q.corner[1], q.corner[2], q.corner[3]}), projective);
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

template <int N, typename Emit>
void resamplePlane(const Plane<N>& plane, const QRect& box, const Quad& dest, double perspective, const QRect& limit, Emit& deliver) {
    if (plane.v.empty() || box.isEmpty())
        return;
    const QRect landing =
        QPolygonF({dest.corner[0], dest.corner[1], dest.corner[2], dest.corner[3]}).boundingRect().toAlignedRect() & limit;
    if (landing.isEmpty())
        return;
    const QuadMap map(dest, perspective);
    std::vector<TileCoord> tiles;
    for (int ty = floorDiv(landing.top(), kTile); ty <= floorDiv(landing.bottom(), kTile); ++ty)
        for (int tx = floorDiv(landing.left(), kTile); tx <= floorDiv(landing.right(), kTile); ++tx)
            tiles.push_back({tx, ty});
    renderTiles<N>(
        tiles,
        [&](int tx, int ty, std::uint16_t* values) {
            const QRect part = TiledImage::tileRect(tx, ty) & landing;
            bool any = false;
            for (int y = part.top(); y <= part.bottom(); ++y)
                for (int x = part.left(); x <= part.right(); ++x) {
                    double u, v;
                    if (!map.inverse(x + 0.5, y + 0.5, u, v) || u < 0.0 || u > 1.0 || v < 0.0 || v > 1.0)
                        continue;
                    const int sx = std::min(box.left() + int(std::floor(u * box.width())), box.right());
                    const int sy = std::min(box.top() + int(std::floor(v * box.height())), box.bottom());
                    if (!plane.contains(sx, sy))
                        continue;
                    const std::uint16_t* q = plane.at(sx, sy);
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

template <int N, typename Emit>
void moveExactlyPlane(const Plane<N>& plane, const QRect& area, Turn turn, const QPoint& offset, const QRect& limit, Emit& deliver) {
    if (plane.v.empty())
        return;
    const QRect target = movedArea(area, turn, offset).intersected(limit);
    if (target.isEmpty())
        return;
    std::vector<TileCoord> tiles;
    for (int ty = floorDiv(target.top(), kTile); ty <= floorDiv(target.bottom(), kTile); ++ty)
        for (int tx = floorDiv(target.left(), kTile); tx <= floorDiv(target.right(), kTile); ++tx)
            tiles.push_back({tx, ty});
    renderTiles<N>(
        tiles,
        [&](int tx, int ty, std::uint16_t* values) {
            const QRect part = TiledImage::tileRect(tx, ty).intersected(target);
            bool any = false;
            for (int y = part.top(); y <= part.bottom(); ++y)
                for (int x = part.left(); x <= part.right(); ++x) {
                    const QPoint s = sourceOf(area, turn, offset, x, y);
                    if (!plane.contains(s.x(), s.y()))
                        continue;
                    const std::uint16_t* q = plane.at(s.x(), s.y());
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
    resamplePlane<4>(planeOf(src, sourceRect), sourceRect, dest, perspective, reach, sink);
    sink.out.squeeze();
    return std::move(sink.out);
}

SelectionMask resample(const SelectionMask& src, const QRect& sourceRect, const Quad& dest, Interpolation, double perspective) {
    MaskSink sink(src.size());
    resamplePlane<1>(planeOf(src, sourceRect), sourceRect, dest, perspective, src.rect(), sink);
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
    moveExactlyPlane<4>(planeOf(src, area), area, turn, offset, sink.out.paintArea(), sink);
    sink.out.squeeze();
    return std::move(sink.out);
}

SelectionMask moveExactly(const SelectionMask& src, const QRect& area, Turn turn, const QPoint& offset, const QSize& target) {
    MaskSink sink(target);
    moveExactlyPlane<1>(planeOf(src, area), area, turn, offset, QRect(QPoint(0, 0), target), sink);
    sink.out.squeeze();
    return std::move(sink.out);
}

}
