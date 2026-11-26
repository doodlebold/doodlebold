#include "core/selection/SelectionOps.h"

#include "core/util/ParallelFor.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <iterator>
#include <unordered_set>

namespace doodlebold::selection {

namespace {

constexpr int kTile = SelectionTile::kSize;
constexpr int kOne = SelectionMask::kOne;

int uniformValue(const SelectionTile* t) {
    if (!t)
        return 0;
    return SelectionMask::isFull(t) ? kOne : -1;
}

void clearOutside(SelectionMask& m, int tx, int ty) {
    if (!m.isEdgeTile(tx, ty))
        return;
    SelectionTile* t = m.writableTile(tx, ty);
    const QRect in = SelectionMask::tileRect(tx, ty).intersected(m.rect());
    for (int y = 0; y < kTile; ++y)
        for (int x = 0; x < kTile; ++x)
            if (!in.contains(tx * kTile + x, ty * kTile + y))
                t->row(y)[x] = 0;
}

// Combines two masks pixel by pixel with f(a, b) → 0…kOne, which must map (0, 0) to 0. Tiles that are uniform in both
template <typename F> SelectionMask zip(const SelectionMask& a, const SelectionMask& b, F f) {
    Q_ASSERT(a.size() == b.size());
    SelectionMask out(a.size());
    std::unordered_set<TileCoord, TileCoordHash> coords;
    for (const auto& [c, t] : a.tiles())
        coords.insert(c);
    for (const auto& [c, t] : b.tiles())
        coords.insert(c);
    for (const TileCoord c : coords) {
        const SelectionTile* ta = a.tile(c.x, c.y);
        const SelectionTile* tb = b.tile(c.x, c.y);
        const int ua = uniformValue(ta), ub = uniformValue(tb);
        if (ua >= 0 && ub >= 0) {
            out.setUniform(c.x, c.y, f(ua, ub));
            continue;
        }
        SelectionTile* t = out.writableTile(c.x, c.y);
        for (int i = 0; i < SelectionTile::kValues; ++i) {
            const int va = ua >= 0 ? ua : ta->v[static_cast<std::size_t>(i)];
            const int vb = ub >= 0 ? ub : tb->v[static_cast<std::size_t>(i)];
            t->v[static_cast<std::size_t>(i)] = static_cast<std::uint16_t>(f(va, vb));
        }
    }
    out.squeeze();
    return out;
}

void windowExtremum(const std::uint16_t* in, std::uint16_t* out, int n, std::ptrdiff_t stride, int half, bool maximum,
                    std::vector<int>& deque) {
    deque.resize(static_cast<std::size_t>(n));
    int head = 0, tail = 0;
    const auto better = [maximum](int a, int b) { return maximum ? a >= b : a <= b; };
    int next = 0;
    for (int i = 0; i < n; ++i) {
        for (; next <= std::min(n - 1, i + half); ++next) {
            while (tail > head && better(in[next * stride], in[deque[static_cast<std::size_t>(tail - 1)] * stride]))
                --tail;
            deque[static_cast<std::size_t>(tail++)] = next;
        }
        while (deque[static_cast<std::size_t>(head)] < i - half)
            ++head;
        out[i * stride] = in[deque[static_cast<std::size_t>(head)] * stride];
    }
}

// Max (or min) over the disc of radius n, in place on a w×h buffer. The disc is cut into rows: for each distinct
void discExtremum(std::vector<std::uint16_t>& buf, int w, int h, int n, bool maximum) {
    std::vector<std::vector<int>> offsetsOfWidth(static_cast<std::size_t>(n + 1));
    for (int dy = -n; dy <= n; ++dy)
        offsetsOfWidth[static_cast<std::size_t>(std::floor(std::sqrt(double(n * n - dy * dy))))].push_back(dy);
    std::vector<std::uint16_t> result(buf.size(), maximum ? std::uint16_t{0} : std::uint16_t(kOne));
    std::vector<std::uint16_t> filtered(buf.size());
    for (int half = 0; half <= n; ++half) {
        const std::vector<int>& offsets = offsetsOfWidth[static_cast<std::size_t>(half)];
        if (offsets.empty())
            continue;
        parallelFor(h, [&](int y) {
            std::vector<int> deque;
            windowExtremum(&buf[static_cast<std::size_t>(y) * w], &filtered[static_cast<std::size_t>(y) * w], w, 1, half, maximum, deque);
        });
        parallelFor(h, [&](int y) {
            std::uint16_t* dst = &result[static_cast<std::size_t>(y) * w];
            for (const int dy : offsets) {
                if (y + dy < 0 || y + dy >= h)
                    continue;
                const std::uint16_t* src = &filtered[static_cast<std::size_t>(y + dy) * w];
                for (int x = 0; x < w; ++x)
                    dst[x] = maximum ? std::max(dst[x], src[x]) : std::min(dst[x], src[x]);
            }
        });
    }
    buf.swap(result);
}

// Dilation or erosion: the buffer covers the mask's bounds plus n, cells beyond the canvas hold the neutral value (dilation
// sees nothing, erosion sees "selected"), so the border does not erode.
SelectionMask morphology(const SelectionMask& m, int n, bool dilation) {
    if (n <= 0 || m.isEmpty())
        return m;
    const QRect region = m.bounds().adjusted(-n, -n, n, n);
    const int w = region.width(), h = region.height();
    const std::uint16_t outside = dilation ? 0 : std::uint16_t(kOne);
    std::vector<std::uint16_t> buf(static_cast<std::size_t>(w) * h, outside);
    const QRect inside = region.intersected(m.rect());
    for (int y = inside.top(); y <= inside.bottom(); ++y)
        m.row(y, inside.left(), inside.width(), &buf[static_cast<std::size_t>(y - region.top()) * w + (inside.left() - region.left())]);
    discExtremum(buf, w, h, n, dilation);
    SelectionMask out(m.size());
    for (int y = inside.top(); y <= inside.bottom(); ++y)
        out.writeRow(y, inside.left(), inside.width(),
                     &buf[static_cast<std::size_t>(y - region.top()) * w + (inside.left() - region.left())]);
    out.squeeze();
    return out;
}

double intervalCoverage(int i, double lo, double hi, bool antialias) {
    if (!antialias)
        return (i + 0.5 >= lo && i + 0.5 < hi) ? 1.0 : 0.0;
    return std::clamp(std::min(hi, i + 1.0) - std::max(lo, double(i)), 0.0, 1.0);
}

std::uint16_t toValue(double coverage) {
    return static_cast<std::uint16_t>(std::lround(std::clamp(coverage, 0.0, 1.0) * kOne));
}

struct Edge {
    double x0, y0, x1, y1;
    int direction;
};

class RowAccumulator {
public:
    RowAccumulator(int x0, int width)
        : m_x0(x0), m_partial(static_cast<std::size_t>(width), 0.0f), m_run(static_cast<std::size_t>(width) + 1, 0) {}

    void addSpan(double xa, double xb) {
        const int n = static_cast<int>(m_partial.size());
        xa = std::clamp(xa - m_x0, 0.0, double(n));
        xb = std::clamp(xb - m_x0, 0.0, double(n));
        if (xb <= xa)
            return;
        const int ia = static_cast<int>(std::floor(xa)), ib = static_cast<int>(std::floor(xb));
        if (ia == ib) {
            m_partial[static_cast<std::size_t>(ia)] += static_cast<float>(xb - xa);
            return;
        }
        m_partial[static_cast<std::size_t>(ia)] += static_cast<float>(ia + 1 - xa);
        if (ib < n)
            m_partial[static_cast<std::size_t>(ib)] += static_cast<float>(xb - ib);
        ++m_run[static_cast<std::size_t>(ia) + 1];
        --m_run[static_cast<std::size_t>(ib)];
    }

    void values(int samples, std::vector<std::uint16_t>& out) const {
        out.resize(m_partial.size());
        int run = 0;
        for (std::size_t i = 0; i < m_partial.size(); ++i) {
            run += m_run[i];
            out[i] = toValue((m_partial[i] + static_cast<float>(run)) / static_cast<float>(samples));
        }
    }

private:
    int m_x0;
    std::vector<float> m_partial;
    std::vector<int> m_run;
};

}

SelectionMask all(const QSize& canvas) {
    SelectionMask m(canvas);
    for (int ty = 0; ty < m.tilesY(); ++ty)
        for (int tx = 0; tx < m.tilesX(); ++tx)
            m.setUniform(tx, ty, kOne);
    return m;
}

SelectionMask invert(const SelectionMask& m) {
    SelectionMask out(m.size());
    for (int ty = 0; ty < m.tilesY(); ++ty)
        for (int tx = 0; tx < m.tilesX(); ++tx) {
            const SelectionTile* t = m.tile(tx, ty);
            const int u = uniformValue(t);
            if (u >= 0) {
                out.setUniform(tx, ty, kOne - u);
                continue;
            }
            SelectionTile* o = out.writableTile(tx, ty);
            for (std::size_t i = 0; i < std::size_t(SelectionTile::kValues); ++i)
                o->v[i] = static_cast<std::uint16_t>(kOne - t->v[i]);
            clearOutside(out, tx, ty);
        }
    out.squeeze();
    return out;
}

SelectionMask rectangle(const QSize& canvas, const QRectF& r, bool antialias) {
    SelectionMask m(canvas);
    const QRectF c = r.normalized().intersected(QRectF(QPointF(0, 0), QSizeF(canvas)));
    if (c.isEmpty())
        return m;
    const QRect px = QRect(QPoint(int(std::floor(c.left())), int(std::floor(c.top()))),
                           QPoint(int(std::ceil(c.right())) - 1, int(std::ceil(c.bottom())) - 1))
                         .intersected(m.rect());
    if (px.isEmpty())
        return m;
    std::vector<double> cx(static_cast<std::size_t>(px.width())), cy(static_cast<std::size_t>(px.height()));
    for (int i = 0; i < px.width(); ++i)
        cx[static_cast<std::size_t>(i)] = intervalCoverage(px.left() + i, c.left(), c.right(), antialias);
    for (int i = 0; i < px.height(); ++i)
        cy[static_cast<std::size_t>(i)] = intervalCoverage(px.top() + i, c.top(), c.bottom(), antialias);
    for (int ty = px.top() / kTile; ty <= px.bottom() / kTile; ++ty)
        for (int tx = px.left() / kTile; tx <= px.right() / kTile; ++tx) {
            const QRect part = SelectionMask::tileRect(tx, ty).intersected(px);
            const auto one = [](double v) { return v >= 1.0; };
            const bool solid = part == SelectionMask::tileRect(tx, ty).intersected(m.rect()) &&
                               std::all_of(cx.begin() + (part.left() - px.left()), cx.begin() + (part.right() - px.left() + 1), one) &&
                               std::all_of(cy.begin() + (part.top() - px.top()), cy.begin() + (part.bottom() - px.top() + 1), one);
            if (solid) {
                m.setUniform(tx, ty, kOne);
                continue;
            }
            SelectionTile* t = m.writableTile(tx, ty);
            for (int y = part.top(); y <= part.bottom(); ++y)
                for (int x = part.left(); x <= part.right(); ++x)
                    t->row(y - ty * kTile)[x - tx * kTile] =
                        toValue(cx[static_cast<std::size_t>(x - px.left())] * cy[static_cast<std::size_t>(y - px.top())]);
        }
    m.squeeze();
    return m;
}

SelectionMask polygonCoverage(const QSize& canvas, const QPolygonF& p, Qt::FillRule rule) {
    SelectionMask m(canvas);
    std::vector<Edge> edges;
    for (int i = 0; i < p.size(); ++i) {
        const QPointF a = p[i], b = p[(i + 1) % p.size()];
        if (a.y() != b.y())
            edges.push_back(a.y() < b.y() ? Edge{a.x(), a.y(), b.x(), b.y(), 1} : Edge{b.x(), b.y(), a.x(), a.y(), -1});
    }
    const QRectF box = p.boundingRect().intersected(QRectF(QPointF(0, 0), QSizeF(canvas)));
    if (edges.empty() || box.isEmpty())
        return m;
    const QRect px = QRect(QPoint(int(std::floor(box.left())), int(std::floor(box.top()))),
                           QPoint(int(std::ceil(box.right())) - 1, int(std::ceil(box.bottom())) - 1))
                         .intersected(m.rect());
    constexpr int samples = 64;
    std::ranges::sort(edges, [](const Edge& a, const Edge& b) { return a.y0 < b.y0; });
    std::size_t nextEdge = 0;
    std::vector<const Edge*> active;
    std::vector<std::pair<double, int>> crossings;
    std::vector<std::uint16_t> values;
    for (int y = px.top(); y <= px.bottom(); ++y) {
        while (nextEdge < edges.size() && edges[nextEdge].y0 < y + 1)
            active.push_back(&edges[nextEdge++]);
        std::erase_if(active, [y](const Edge* e) { return e->y1 <= y; });
        RowAccumulator row(px.left(), px.width());
        for (int k = 0; k < samples; ++k) {
            const double sy = y + (k + 0.5) / samples;
            crossings.clear();
            for (const Edge* e : active)
                if (e->y0 <= sy && sy < e->y1)
                    crossings.emplace_back(e->x0 + (sy - e->y0) * (e->x1 - e->x0) / (e->y1 - e->y0), e->direction);
            std::ranges::sort(crossings);
            int winding = 0;
            double start = 0.0;
            const auto inside = [rule](int w) { return rule == Qt::OddEvenFill ? (w & 1) != 0 : w != 0; };
            for (const auto& [x, direction] : crossings) {
                const bool before = inside(winding);
                winding += rule == Qt::OddEvenFill ? 1 : direction;
                const bool after = inside(winding);
                if (!before && after)
                    start = x;
                else if (before && !after)
                    row.addSpan(start, x);
            }
        }
        row.values(samples, values);
        m.writeRow(y, px.left(), px.width(), values.data());
    }
    m.squeeze();
    return m;
}

SelectionMask polygon(const QSize& canvas, const QPolygonF& p, Qt::FillRule rule) {
    SelectionMask m = polygonCoverage(canvas, p, rule);
    for (const auto& [coord, shared] : std::vector<std::pair<TileCoord, SelectionTilePtr>>(m.tiles().begin(), m.tiles().end())) {
        if (SelectionMask::isFull(shared.get()))
            continue;
        SelectionTile* t = m.writableTile(coord.x, coord.y);
        for (std::uint16_t& v : t->v)
            v = v >= kOne / 2 ? std::uint16_t(kOne) : std::uint16_t(0);
    }
    m.squeeze();
    return m;
}

SelectionMask marquee(const QSize& canvas, const QPoint& a, const QPoint& b) {
    const int x0 = std::min(a.x(), b.x()), y0 = std::min(a.y(), b.y());
    const int x1 = std::max(a.x(), b.x()), y1 = std::max(a.y(), b.y());
    return rectangle(canvas, QRectF(x0, y0, x1 - x0 + 1, y1 - y0 + 1), false);
}

// Only the canvas: a layer keeps pixels past its edge, but a selection is 0 there (SelectionMask), and a file holding more
// doesn't open.
SelectionMask fromLayerAlpha(const TiledImage& layer) {
    SelectionMask m(layer.size());
    for (const auto& [c, t] : layer.tiles()) {
        if (!t || c.x < 0 || c.y < 0 || c.x >= m.tilesX() || c.y >= m.tilesY())
            continue;
        const QRect tile = SelectionMask::tileRect(c.x, c.y);
        const QRect inside = tile.intersected(m.rect()).translated(-tile.topLeft());
        SelectionTile* o = m.writableTile(c.x, c.y);
        bool any = false;
        for (int y = inside.top(); y <= inside.bottom(); ++y)
            for (int x = inside.left(); x <= inside.right(); ++x) {
                const std::size_t i = std::size_t(y * SelectionTile::kSize + x);
                const Channel a = t->px[i * 4 + 3];
                o->v[i] = static_cast<std::uint16_t>(std::min<int>(a, kOne));
                any = any || a != 0;
            }
        if (!any)
            m.setTile(c.x, c.y, nullptr);
    }
    m.squeeze();
    return m;
}

SelectionMask combine(const std::optional<SelectionMask>& current, const SelectionMask& shape, CombineMode mode) {
    if (mode == CombineMode::Replace)
        return shape;
    const SelectionMask none(shape.size());
    const SelectionMask& a = current ? *current : none;
    switch (mode) {
    case CombineMode::Add: return zip(a, shape, [](int x, int y) { return std::min(x + y, kOne); });
    case CombineMode::Subtract: return zip(a, shape, [](int x, int y) { return std::max(x - y, 0); });
    case CombineMode::Intersect: return zip(a, shape, [](int x, int y) { return std::min(x, y); });
    case CombineMode::Replace: break;
    }
    return shape;
}

SelectionMask dilate(const SelectionMask& m, int n) {
    return morphology(m, n, true);
}

SelectionMask erode(const SelectionMask& m, int n) {
    return morphology(m, n, false);
}

}
