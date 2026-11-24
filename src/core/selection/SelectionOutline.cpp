#include "core/selection/SelectionOutline.h"

#include <algorithm>
#include <cstdint>
#include <vector>

namespace doodlebold::selection {

namespace {

// Directions of a boundary edge, in document coordinates (y down). Each edge keeps the selected pixel on its right, so the
// outer boundary of a region runs clockwise on screen and the boundary of a hole counter-clockwise.
enum Dir : std::uint8_t {
    kRight = 1,
    kDown = 2,
    kLeft = 4,
    kUp = 8
};

constexpr int dx(std::uint8_t d) {
    return d == kRight ? 1 : d == kLeft ? -1 : 0;
}
constexpr int dy(std::uint8_t d) {
    return d == kDown ? 1 : d == kUp ? -1 : 0;
}

}

QPainterPath outline(const SelectionMask& mask) {
    QPainterPath path;
    if (mask.isEmpty())
        return path;
    const QSize size = mask.size();
    const QRect box = mask.bounds().adjusted(-1, -1, 1, 1);
    const int w = box.width(), h = box.height();
    std::vector<std::uint8_t> inside(std::size_t(w) * std::size_t(h), 0);
    std::vector<std::uint16_t> raw(std::size_t(std::max(w, 1)));
    for (int y = std::max(box.top(), 0); y <= std::min(box.bottom(), size.height() - 1); ++y) {
        const int first = std::max(box.left(), 0), last = std::min(box.right() + 1, size.width());
        if (last <= first)
            continue;
        mask.row(y, first, last - first, raw.data());
        for (int x = first; x < last; ++x)
            inside[std::size_t(y - box.top()) * std::size_t(w) + std::size_t(x - box.left())] =
                isInside(raw[std::size_t(x - first)]) ? 1 : 0;
    }
    const auto in = [&](int x, int y) {
        return x >= 0 && y >= 0 && x < w && y < h && inside[std::size_t(y) * std::size_t(w) + std::size_t(x)];
    };

    const int vw = w + 1;
    std::vector<std::uint8_t> out(std::size_t(vw) * std::size_t(h + 1), 0);
    const auto vertex = [&](int x, int y) { return std::size_t(y) * std::size_t(vw) + std::size_t(x); };
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            if (!in(x, y))
                continue;
            if (!in(x, y - 1))
                out[vertex(x, y)] |= kRight;
            if (!in(x + 1, y))
                out[vertex(x + 1, y)] |= kDown;
            if (!in(x, y + 1))
                out[vertex(x + 1, y + 1)] |= kLeft;
            if (!in(x - 1, y))
                out[vertex(x, y + 1)] |= kUp;
        }

    // Trace closed loops. At a vertex where two regions touch only diagonally there are two outgoing edges; taking the right
    // turn keeps such regions apart, so loops never cross themselves.
    const auto rightOf = [](std::uint8_t d) -> std::uint8_t {
        return d == kRight ? kDown : d == kDown ? kLeft : d == kLeft ? kUp : kRight;
    };
    const auto leftOf = [](std::uint8_t d) -> std::uint8_t { return d == kRight ? kUp : d == kUp ? kLeft : d == kLeft ? kDown : kRight; };
    for (int sy = 0; sy <= h; ++sy)
        for (int sx = 0; sx < vw; ++sx) {
            while (out[vertex(sx, sy)]) {
                std::uint8_t dir = out[vertex(sx, sy)] & kRight  ? kRight
                                   : out[vertex(sx, sy)] & kDown ? kDown
                                   : out[vertex(sx, sy)] & kLeft ? kLeft
                                                                 : kUp;
                int x = sx, y = sy;
                path.moveTo(box.left() + x, box.top() + y);
                std::uint8_t previous = 0;
                for (;;) {
                    out[vertex(x, y)] &= std::uint8_t(~dir);
                    if (previous != 0 && dir != previous)
                        path.lineTo(box.left() + x, box.top() + y);
                    previous = dir;
                    x += dx(dir);
                    y += dy(dir);
                    const std::uint8_t here = out[vertex(x, y)];
                    if (here == 0)
                        break;
                    const std::uint8_t straight = dir;
                    if (here & rightOf(dir))
                        dir = rightOf(dir);
                    else if (here & straight)
                        dir = straight;
                    else if (here & leftOf(dir))
                        dir = leftOf(dir);
                    else
                        break;  // cannot happen for a well-formed boundary; stop rather than loop
                }
                path.closeSubpath();
            }
        }
    return path;
}

}
