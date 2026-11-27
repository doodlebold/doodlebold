#include "core/transform/Lift.h"

#include "core/selection/SelectionOps.h"

#include <algorithm>
#include <vector>

namespace doodlebold::transform {

Lifted liftSelection(const TiledImage& layer, const SelectionMask& selection) {
    Lifted out{TiledImage(layer.size()), TiledImage(layer.size())};
    for (const auto& [c, tile] : layer.tiles()) {
        if (!tile)
            continue;
        const SelectionTile* sel = selection.tile(c.x, c.y);
        if (!sel) {
            out.rest.setTile(c.x, c.y, tile);
        } else if (SelectionMask::isFull(sel)) {
            out.part.setTile(c.x, c.y, tile);
        } else {
            auto part = std::make_shared<Tile>();
            auto rest = std::make_shared<Tile>();
            for (std::size_t i = 0; i < std::size_t(SelectionTile::kValues); ++i)
                for (std::size_t ch = 0; ch < 4; ++ch) {
                    const int v = tile->px[i * 4 + ch];
                    const int taken = mulChannel(v, sel->v[i]);
                    part->px[i * 4 + ch] = static_cast<Channel>(taken);
                    rest->px[i * 4 + ch] = static_cast<Channel>(v - taken);
                }
            out.part.setTile(c.x, c.y, std::move(part));
            out.rest.setTile(c.x, c.y, std::move(rest));
        }
    }
    out.part.squeeze();
    out.rest.squeeze();
    return out;
}

TiledImage composeOver(const TiledImage& base, const TiledImage& top) {
    TiledImage out = base;
    for (const auto& [c, t] : top.tiles()) {
        const Tile* b = base.tile(c.x, c.y);
        if (!t)
            continue;
        if (!b) {
            out.setTile(c.x, c.y, t);
            continue;
        }
        Tile* o = out.writableTile(c.x, c.y);
        for (std::size_t i = 0; i < std::size_t(Tile::kSize * Tile::kSize); ++i) {
            const int inverseAlpha = kChannelOne - t->px[i * 4 + 3];
            for (std::size_t ch = 0; ch < 4; ++ch)
                o->px[i * 4 + ch] =
                    static_cast<Channel>(std::min(kChannelOne, t->px[i * 4 + ch] + mulChannel(b->px[i * 4 + ch], inverseAlpha)));
        }
    }
    return out;
}

SelectionMask carryMask(const SelectionMask& mask, const std::function<SelectionMask(const SelectionMask&)>& move) {
    const SelectionMask moved = move(mask);
    // How much of each pixel the moved part covers: the whole canvas moved the same way.
    const SelectionMask landed = move(selection::rectangle(mask.size(), QRectF(mask.rect()), false));
    SelectionMask out = mask;
    const QRect area = landed.bounds();
    if (area.isEmpty())
        return out;
    const int width = area.width();
    std::vector<std::uint16_t> old(static_cast<std::size_t>(width)), part(old.size()), covered(old.size()), carried(old.size());
    for (int y = area.top(); y <= area.bottom(); ++y) {
        mask.row(y, area.left(), width, old.data());
        moved.row(y, area.left(), width, part.data());
        landed.row(y, area.left(), width, covered.data());
        for (std::size_t i = 0; i < carried.size(); ++i)
            carried[i] = static_cast<std::uint16_t>(
                std::min(SelectionMask::kOne, part[i] + mulChannel(old[i], SelectionMask::kOne - covered[i])));
        out.writeRow(y, area.left(), width, carried.data());
    }
    out.squeeze();
    return out;
}

}
