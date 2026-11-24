#include "core/transform/Lift.h"

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

}
