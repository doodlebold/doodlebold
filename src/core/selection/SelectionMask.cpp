#include "core/selection/SelectionMask.h"

#include <algorithm>
#include <cstring>

namespace doodlebold {

namespace {

constexpr int kTile = SelectionTile::kSize;
constexpr std::uint16_t kOne = SelectionMask::kOne;

const SelectionTilePtr& fullTile() {
    static const SelectionTilePtr tile = [] {
        auto t = std::make_shared<SelectionTile>();
        t->v.fill(kOne);
        return SelectionTilePtr(std::move(t));
    }();
    return tile;
}

bool allEqual(const SelectionTile& t, std::uint16_t value) {
    return std::ranges::all_of(t.v, [value](std::uint16_t v) { return v == value; });
}

}

QRect SelectionMask::tileRange(const QRect& r) const {
    const QRect c = r.intersected(rect());
    if (c.isEmpty())
        return {};
    return QRect(QPoint(c.left() / kTile, c.top() / kTile), QPoint(c.right() / kTile, c.bottom() / kTile));
}

bool SelectionMask::isFull(const SelectionTile* t) {
    return t == fullTile().get();
}

const SelectionTile* SelectionMask::tile(int tx, int ty) const {
    const auto it = m_tiles.find(TileCoord{tx, ty});
    return it == m_tiles.end() ? nullptr : it->second.get();
}

SelectionTilePtr SelectionMask::tileShared(int tx, int ty) const {
    const auto it = m_tiles.find(TileCoord{tx, ty});
    return it == m_tiles.end() ? nullptr : it->second;
}

SelectionTile* SelectionMask::writableTile(int tx, int ty) {
    SelectionTilePtr& slot = m_tiles[TileCoord{tx, ty}];
    if (!slot)
        slot = std::make_shared<SelectionTile>();
    else if (slot.use_count() > 1)  // shared with a snapshot, or the full tile
        slot = std::make_shared<SelectionTile>(*slot);
    return const_cast<SelectionTile*>(slot.get());  // the map owns the only reference now
}

void SelectionMask::setTile(int tx, int ty, SelectionTilePtr t) {
    if (t)
        m_tiles[TileCoord{tx, ty}] = std::move(t);
    else
        m_tiles.erase(TileCoord{tx, ty});
}

bool SelectionMask::isEdgeTile(int tx, int ty) const {
    return (tx + 1) * kTile > m_size.width() || (ty + 1) * kTile > m_size.height();
}

void SelectionMask::setUniform(int tx, int ty, int value) {
    if (value <= 0) {
        m_tiles.erase(TileCoord{tx, ty});
    } else if (value >= kOne && !isEdgeTile(tx, ty)) {
        m_tiles[TileCoord{tx, ty}] = fullTile();
    } else {
        SelectionTile* t = writableTile(tx, ty);
        const QRect in = tileRect(tx, ty).intersected(rect());
        t->v.fill(0);
        for (int y = in.top(); y <= in.bottom(); ++y)
            std::fill_n(t->row(y - ty * kTile), in.width(), static_cast<std::uint16_t>(std::min(value, kOne)));
    }
}

void SelectionMask::writeRow(int y, int x0, int n, const std::uint16_t* values) {
    if (y < 0 || y >= m_size.height())
        return;
    const int xa = std::max(x0, 0), xb = std::min(x0 + n, m_size.width());
    for (int x = xa; x < xb;) {
        const int tx = x / kTile, ty = y / kTile;
        const int end = std::min(xb, (tx + 1) * kTile);
        const std::uint16_t* src = values + (x - x0);
        const std::size_t count = static_cast<std::size_t>(end - x);
        if (tile(tx, ty) || std::any_of(src, src + count, [](std::uint16_t v) { return v != 0; }))
            std::memcpy(writableTile(tx, ty)->row(y % kTile) + (x - tx * kTile), src, count * sizeof(std::uint16_t));
        x = end;
    }
}

void SelectionMask::squeeze() {
    for (auto it = m_tiles.begin(); it != m_tiles.end();) {
        const SelectionTilePtr& t = it->second;
        if (isFull(t.get())) {
            ++it;
        } else if (!t || allEqual(*t, 0)) {
            it = m_tiles.erase(it);
        } else {
            if (!isEdgeTile(it->first.x, it->first.y) && allEqual(*t, kOne))
                it->second = fullTile();
            ++it;
        }
    }
}

int SelectionMask::at(int x, int y) const {
    if (!rect().contains(x, y))
        return 0;
    const SelectionTile* t = tile(x / kTile, y / kTile);
    return t ? t->row(y % kTile)[x % kTile] : 0;
}

void SelectionMask::row(int y, int x0, int n, std::uint16_t* out) const {
    std::fill(out, out + n, std::uint16_t{0});
    if (y < 0 || y >= m_size.height())
        return;
    const int xa = std::max(x0, 0), xb = std::min(x0 + n, m_size.width());
    for (int x = xa; x < xb;) {
        const int tx = x / kTile;
        const int end = std::min(xb, (tx + 1) * kTile);
        if (const SelectionTile* t = tile(tx, y / kTile))
            std::memcpy(out + (x - x0), t->row(y % kTile) + (x - tx * kTile), static_cast<std::size_t>(end - x) * sizeof(std::uint16_t));
        x = end;
    }
}

QRect SelectionMask::bounds(bool exact) const {
    QRect b;
    for (const auto& [coord, t] : m_tiles) {
        const QRect tr = coord.pixelRect().intersected(rect());
        if (!exact || isFull(t.get())) {
            b |= tr;
            continue;
        }
        int x0 = kTile, y0 = kTile, x1 = -1, y1 = -1;
        for (int y = 0; y < kTile; ++y) {
            const std::uint16_t* r = t->row(y);
            for (int x = 0; x < kTile; ++x)
                if (r[x]) {
                    x0 = std::min(x0, x);
                    x1 = std::max(x1, x);
                    y0 = std::min(y0, y);
                    y1 = std::max(y1, y);
                }
        }
        if (x1 >= 0)
            b |= QRect(QPoint(coord.x * kTile + x0, coord.y * kTile + y0), QPoint(coord.x * kTile + x1, coord.y * kTile + y1))
                     .intersected(rect());
    }
    return b;
}

QImage SelectionMask::toImage(const QRect& r) const {
    QImage img(r.size(), QImage::Format_Alpha8);
    img.fill(0);
    const QRect tr = tileRange(r);
    if (tr.isEmpty())
        return img;
    for (int ty = tr.top(); ty <= tr.bottom(); ++ty)
        for (int tx = tr.left(); tx <= tr.right(); ++tx) {
            const SelectionTile* t = tile(tx, ty);
            if (!t)
                continue;
            const QRect part = tileRect(tx, ty).intersected(r).intersected(rect());
            for (int y = part.top(); y <= part.bottom(); ++y) {
                std::uint8_t* dst = img.scanLine(y - r.top()) + (part.left() - r.left());
                const std::uint16_t* src = t->row(y - ty * kTile) + (part.left() - tx * kTile);
                for (int i = 0; i < part.width(); ++i)
                    dst[i] = toUnorm8(src[i]);
            }
        }
    return img;
}

QRect SelectionMask::differingArea(const SelectionMask* o) const {
    static const std::unordered_map<TileCoord, SelectionTilePtr, TileCoordHash> kNone;
    const auto& other = o ? o->m_tiles : kNone;
    const auto differs = [](const SelectionTilePtr& a, const SelectionTilePtr& b) { return a != b && (!a || !b || a->v != b->v); };
    QRect area;
    for (const auto& [coord, t] : m_tiles) {
        const auto it = other.find(coord);
        if (differs(t, it == other.end() ? nullptr : it->second))
            area |= coord.pixelRect();
    }
    for (const auto& [coord, t] : other)
        if (!m_tiles.contains(coord))
            area |= coord.pixelRect();
    return area;
}

QRect selectionChangedArea(const std::optional<SelectionMask>& a, const std::optional<SelectionMask>& b) {
    if (a)
        return a->differingArea(b ? &*b : nullptr);
    return b ? b->differingArea(nullptr) : QRect();
}

}
