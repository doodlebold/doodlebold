// Part of DabTip.cpp, which includes it where it stands: the Marker's stamps laid a pen event at a time (layEvent), and the edges of the
// reference's dab profiles.
// docs/brush-engine/dab-tip.md says what the named values here are for and how they were chosen.

// How much further apart the event-laid Marker's stamps lie than its old ones (markerSpacingShare), at this stamp's diameter:
// what its old climb and carry, fitted per old stamp, are scaled by.
double DabPainter::spacingRatio(const Stamp& stamp) const {
    const double diameter = std::max(2.0 * static_cast<double>(stamp.radius), kThinnestDiameter);
    return std::max(markerEventSpacingShare(m_tip.edge) * diameter, kMarkerEventLeastSpacing) / (markerSpacingShare(m_tip.edge) * diameter);
}

bool DabPainter::laysEvents() const {  // the Marker's own laying: not every tool laid by events will lay as the Marker does
    // A bristle map's stamps too: off the events the Marker's hairs grew lighter as the size fell (Flat Face 0.18 at 35 px
    // against the reference's 0.32); a blot map and a brush shape stay off them, as fitted.
    const bool hairs = m_tip.form && !m_tip.form->fixedToCanvas() && !m_tip.form->stamped();
    return m_mixing.kind == TipMixing::Kind::Marker && !m_mixing.transparentColour && (!m_tip.form || hairs);
}

// The Marker laid a pen event at a time: one mask a group, each pixel's top never falling. The soft Shapes' edge sits
// kMarkerEdgeShiftSmall / Large px further in, between these radii.
constexpr std::array<float, 4> kMarkerEdgeShiftSmall{0.2f, 0.9f, 0.9f, 0.5f};
constexpr std::array<float, 4> kMarkerEdgeShiftLarge{-1.1f, 0.3f, 0.3f, 0.5f};
constexpr float kMarkerEdgeSmallRadius = 17.5f;
constexpr float kMarkerEdgeLargeRadius = 100.0f;
constexpr float kMarkerEdgeFullRadius = 8.0f;
constexpr std::array<float, 4> kMarkerEdgeBlur{0.0f, 1.0f, 1.0f, 0.5f};

namespace {
// A soft Shape's profile edge, in the profile's own units (nominal radii): where it's half, and its width, from where it's 0.9 to where
// it's 0.1, over 2.563, as a normal curve's deviation.
struct ProfileEdge {
    float half, width;
};
ProfileEdge profileEdgeOf(int profile) {
    const float reach = profileReach(profile);
    float outer = -1.0f, half = -1.0f, inner = -1.0f;
    constexpr int kSteps = 4000;
    for (int i = 0; i <= kSteps; ++i) {
        const float along = reach * static_cast<float>(i) / kSteps, paint = profilePaint(profile, along);
        if (inner < 0.0f && paint < 0.9f)
            inner = along;
        if (half < 0.0f && paint < 0.5f)
            half = along;
        if (outer < 0.0f && paint < 0.1f)
            outer = along;
    }
    return {half, (outer - inner) / 2.563f};
}

MarkerEdge markerEdgeOf(const Stamp& stamp) {
    if (stamp.profile < 0 || stamp.profile > 3)
        return {};
    static const std::array<ProfileEdge, 4> edges{profileEdgeOf(0), profileEdgeOf(1), profileEdgeOf(2), profileEdgeOf(3)};
    const auto p = static_cast<std::size_t>(stamp.profile);  // NOLINT(bugprone-signed-char-misuse): 0 to 3, a stamp with a profile
    const float nominal = stamp.radius / profileReach(stamp.profile);
    const float large = std::clamp((nominal - kMarkerEdgeSmallRadius) / (kMarkerEdgeLargeRadius - kMarkerEdgeSmallRadius), 0.0f, 1.0f);
    const float width = edges[p].width * nominal;
    // below kMarkerEdgeFullRadius both in proportion to the radius, the edge's shape against the stamp's size kept as there
    const float smaller = std::min(nominal / kMarkerEdgeFullRadius, 1.0f), blur = kMarkerEdgeBlur[p] * smaller;
    MarkerEdge edge;
    edge.moved = true;
    edge.half = edges[p].half * nominal;
    edge.shift = (kMarkerEdgeShiftSmall[p] + (kMarkerEdgeShiftLarge[p] - kMarkerEdgeShiftSmall[p]) * large) * smaller;
    edge.stretch = width > 0.0f ? width / std::sqrt(width * width + blur * blur) : 1.0f;
    // where the read reaches the stamp's outer end, past which its profile lays none
    edge.reach = edge.half + (outerOf(stamp) - edge.half) / edge.stretch - edge.shift;
    return edge;
}
}  // namespace

void DabPainter::layEvent(const Dab& dab, const Premultiplied& paint) {
    struct EventStamp {
        PixelFrame frame;
        float solidSquared, outsideSquared, top, share;  // its top, and its own share in the mask (Stamp::share)
        std::size_t i;
        MarkerEdge edge;  // a soft Shape's edge
    };
    std::vector<EventStamp> stamps;
    for (std::size_t k = 0; k < dab.count; ++k) {
        const std::size_t i = dab.first + k;
        const Stamp& stamp = m_stamps[i];
        const MarkerEdge edge = markerEdgeOf(stamp);
        const float solid = static_cast<float>(solidReach(m_stamps, i)) - kSolidMargin,
                    outside = std::max(layReachOf(stamp), edge.moved ? edge.reach : 0.0f) + kSolidMargin;
        // A soft stamp is a full share nowhere: its share is its opacity times its profile, so none of it is laid as solid. Nor is
        // a bristle map's, whose cover is its hairs (laid as a round stamp's, the Marker's hairs didn't show).
        const bool soft = stamp.profile >= 0 || stamp.hairs;
        stamps.push_back({PixelFrame(stamp.centre), !soft && solid > 0.0f ? solid * solid : -1.0f, outside * outside, stamp.opacity,
                          stamp.share >= 0.0f ? stamp.share : stamp.opacity, i, edge});
    }
    struct Job {
        TileCoord coord;
        Tile* tile;
        EventMemory* memory;
    };
    // Only the tiles some stamp's outline reaches: a fast event's stamps lie far apart, and most of the box around them is untouched.
    std::vector<TileCoord> reached;
    for (const EventStamp& stamp : stamps) {
        const Stamp& one = m_stamps[stamp.i];
        const QRect box = markerAreaOf(one).intersected(dab.area);
        if (box.isEmpty())
            continue;
        for (int ty = TiledImage::tileOf(box.top()); ty <= TiledImage::tileOf(box.bottom()); ++ty)
            for (int tx = TiledImage::tileOf(box.left()); tx <= TiledImage::tileOf(box.right()); ++tx) {
                const QRect rect = TileCoord{tx, ty}.pixelRect();
                const float nx = std::clamp(0.0f, stamp.frame.dx(rect.left()), stamp.frame.dx(rect.right()));
                const float ny = std::clamp(0.0f, stamp.frame.dy(rect.top()), stamp.frame.dy(rect.bottom()));
                if (nx * nx + ny * ny < stamp.outsideSquared)
                    reached.push_back({tx, ty});
            }
    }
    std::sort(reached.begin(), reached.end(), [](TileCoord a, TileCoord b) { return a.y != b.y ? a.y < b.y : a.x < b.x; });
    reached.erase(std::unique(reached.begin(), reached.end()), reached.end());
    std::vector<Job> jobs;
    for (const TileCoord coord : reached) {
        const int tx = coord.x, ty = coord.y;
        std::unique_ptr<EventMemory>& memory = m_eventMemory[coord];
        if (!memory)
            memory = std::make_unique<EventMemory>();
        // A tile the layer has is made writable as it's laid, on the workers.
        jobs.push_back({coord, m_layer.tile(tx, ty) ? nullptr : m_layer.writableTile(tx, ty), memory.get()});
        m_changed.insert(coord);
    }
    const float opacityScale = m_paint.opacity;
    const bool ceilingOnly = m_mixing.ceilingOnly;
    // A pixel as shown: its opacity over what was under the stroke, in the colour the event carries.
    // The colour the event carries, straight, where it has some: every pixel it touches takes it, whatever's under (as below).
    const bool ownColour = paint[3] > kLeastReadableAlpha;
    const std::array<float, 3> straight{ownColour ? paint[0] / paint[3] : 0.0f, ownColour ? paint[1] / paint[3] : 0.0f,
                                        ownColour ? paint[2] / paint[3] : 0.0f};
    const bool inLanes = ownColour && !s_onePixelAtATime.load(std::memory_order_relaxed);
    // A pixel's step, as the reference's: its top rises to the highest of the stamps' over it and never falls, and the pixel moves
    // toward the colour the event carries, at an alpha of the higher of its top and its own (or with !ceilingOnly, its own with the top
    // laid over it), by the group's mask there. Over paint a faint group tints what's there; over nothing a click reaches its top times
    // its share, Density times Density at its middle.
    const auto toward = [&](Channel* px, float& memoryTop, float top, std::int32_t mask) {
        memoryTop = std::max(memoryTop, top);
        const Premultiplied current = readPixel(px);
        const float laid = memoryTop * opacityScale;
        const float alpha = ceilingOnly ? std::max(current[3], laid) : current[3] + (1.0f - current[3]) * laid;
        Premultiplied target{0.0f, 0.0f, 0.0f, alpha};
        for (std::size_t c = 0; c < 3; ++c)  // carrying nothing readable, it keeps the pixel's own colour
            target[c] = (ownColour ? straight[c] : current[3] > kLeastReadableAlpha ? current[c] / current[3] : 0.0f) * alpha;
        writePixel(px, lerp(current, target, static_cast<float>(mask) / static_cast<float>(kMaskOne)));
    };
    // Through the paper dab by dab on a soft Shape, as the Pencil's (StampLayer::markOf): a stamp's profile less the paper's darkness times
    // Intensity, down to nothing.
    const bool cutsPaper = m_paint.paperPerDab && m_paint.grain;
    const auto paperCutAt = [&](int x, int y) { return cutsPaper ? m_paint.grain->intensity() * m_paint.grain->keptDarkAt(x, y) : 0.0f; };
    // A stamp's cover at a pixel past its solid, `dx`, `dy` from its centre: its rim as its own mark, the groups' mask a plain union, as the
    // reference's; a bristle map's stamp its hairs.
    const auto rimCover = [&](const EventStamp& stamp, float dx, float dy, float distanceSquared, int x, int y) {
        const Stamp& own = m_stamps[stamp.i];
        const StampMark mark =
            own.hairs ? formedMark(own, dx, dy) : stampMark(own, m_tip.edge, stamp.edge.read(std::sqrt(distanceSquared)));
        return std::max(mark.paint - paperCutAt(x, y), 0.0f) * mark.shape;
    };
    // A stamp's share of the mask: its own share times its cover there, each taking its share of what's left.
    const auto addShare = [](std::int32_t& mask, float stampShare, float cover) {
        const std::int32_t share = maskOf(stampShare * cover);
        if (share > 0)
            mask += ((kMaskOne - mask) * share + (kMaskOne >> 1)) >> kMaskBits;
        return share > 0;
    };
    // A run of pixels taking one mask and top: eight at a time where AVX2 has them.
    const auto towardRun = [&](Channel* px, float* tops, int count, float top, std::int32_t mask) {
        int i = 0;
        if (inLanes)
            for (; i + kWideLanes <= count; i += kWideLanes)
                markerTowardEight(px + i * 4, tops ? tops + i : nullptr, top, static_cast<float>(mask) / static_cast<float>(kMaskOne),
                                  straight.data(), opacityScale, ceilingOnly);
        for (; i < count; ++i) {
            float runTop = top;
            toward(px + i * 4, tops ? tops[i] : runTop, top, mask);
        }
    };
    const auto layTile = [&](int j) {
        const Job& job = jobs[static_cast<std::size_t>(j)];
        const QRect area = dab.area.intersected(job.coord.pixelRect());
        if (area.isEmpty())
            return;
        // The stamps that may reach the tile's part of the dab, in order; and whether each covers all of it, its farthest pixel inside
        // its solid (a disc's distance is most at a corner).
        std::vector<const EventStamp*> here;
        bool allWhole = true;
        for (const EventStamp& stamp : stamps) {
            const float nx = std::clamp(0.0f, stamp.frame.dx(area.left()), stamp.frame.dx(area.right()));
            const float ny = std::clamp(0.0f, stamp.frame.dy(area.top()), stamp.frame.dy(area.bottom()));
            if (nx * nx + ny * ny >= stamp.outsideSquared)
                continue;
            here.push_back(&stamp);
            float farthest = 0.0f;
            for (const int x : {area.left(), area.right()})
                for (const int y : {area.top(), area.bottom()}) {
                    const float dx = stamp.frame.dx(x), dy = stamp.frame.dy(y);
                    farthest = std::max(farthest, dx * dx + dy * dy);
                }
            allWhole = allWhole && farthest < stamp.solidSquared;
        }
        if (here.empty())
            return;
        EventMemory& memory = *job.memory;
        const bool wholeTile = allWhole && area == job.coord.pixelRect();
        // The pixels as they are now are read, so a tile shared with the frame on show is copied, on the worker.
        Tile* tile = job.tile ? job.tile : m_layer.writableExistingTile(job.coord.x, job.coord.y);
        if (wholeTile) {
            // Every pixel: every stamp whole over it, the same mask and top.
            float top = 0.0f;
            std::int32_t mask = 0;
            for (const EventStamp* stamp : here)
                if (addShare(mask, stamp->share, 1.0f))
                    top = std::max(top, stamp->top);
            if (mask <= 0)
                return;
            if (memory.uniform) {
                memory.uniformTop = std::max(memory.uniformTop, top);
                towardRun(tile->px.data(), nullptr, Tile::kSize * Tile::kSize, memory.uniformTop, mask);
                return;
            }
            towardRun(tile->px.data(), memory.top.get(), Tile::kSize * Tile::kSize, top, mask);
            return;
        }
        memory.spread();
        // Along a row a stamp's distance falls then rises, so the pixels inside its solid, and those it may reach, are each one run.
        // Each run is found from the circle and its ends settled by the very test the pixel makes, so a pixel is in it exactly when
        // the test says. Where no stamp's rim (reached, not solid) takes a pixel, the stamps over it are whole there, and the pixels
        // of a run between ends share one mask and top, worked out once, the same numbers.
        struct Run {
            int solidFrom, solidTo, reachFrom, reachTo;
        };
        std::vector<Run> runs(here.size());
        enum class Crossing {
            IntoSolid,
            OutOfSolid,
            IntoReach,
            OutOfReach
        };
        struct RowEdge {
            int x, k;
            Crossing crossing;
        };
        std::vector<RowEdge> edges;
        struct KnownSet {
            std::uint64_t solid;
            float top;
            std::int32_t mask;
        };
        std::vector<KnownSet> sets;
        // A stamp a bit of a 64-bit set; with more stamps over a tile than it holds, its every pixel goes stamp by stamp.
        const bool bitsFit = here.size() <= static_cast<std::size_t>(std::numeric_limits<std::uint64_t>::digits);
        const auto runOf = [&](const EventStamp& stamp, float dy, float limit, int& from, int& to) {
            const auto inside = [&](int x) {
                const float dx = stamp.frame.dx(x);
                return dx * dx + dy * dy < limit;
            };
            const float room = limit - dy * dy;
            from = area.right() + 1;
            to = area.left() - 1;
            if (room <= 0.0f)
                return;
            const double half = std::sqrt(static_cast<double>(room)),
                         middle = area.left() - static_cast<double>(stamp.frame.dx(area.left()));
            int a = std::max(area.left(), static_cast<int>(std::floor(middle - half))),
                b = std::min(area.right(), static_cast<int>(std::ceil(middle + half)));
            while (a <= b && !inside(a))
                ++a;
            while (b >= a && !inside(b))
                --b;
            if (a > b)
                return;
            while (a > area.left() && inside(a - 1))
                --a;
            while (b < area.right() && inside(b + 1))
                ++b;
            from = a;
            to = b;
        };
        for (int y = area.top(); y <= area.bottom(); ++y) {
            edges.clear();
            for (std::size_t k = 0; k < (bitsFit ? here.size() : 0); ++k) {
                const float dy = here[k]->frame.dy(y);
                Run& run = runs[k];
                runOf(*here[k], dy, here[k]->solidSquared, run.solidFrom, run.solidTo);
                runOf(*here[k], dy, here[k]->outsideSquared, run.reachFrom, run.reachTo);
                const int index = static_cast<int>(k);
                if (run.solidFrom <= run.solidTo) {
                    edges.push_back({run.solidFrom, index, Crossing::IntoSolid});
                    edges.push_back({run.solidTo + 1, index, Crossing::OutOfSolid});
                }
                if (run.reachFrom <= run.reachTo) {
                    edges.push_back({run.reachFrom, index, Crossing::IntoReach});
                    edges.push_back({run.reachTo + 1, index, Crossing::OutOfReach});
                }
            }
            // Along the row, the stamps inside their solid and those reaching it at each run between ends, as bits in stamp order; each
            // set's mask and top worked out once, in stamp order, the same numbers.
            std::sort(edges.begin(), edges.end(), [](const RowEdge& a, const RowEdge& b) { return a.x < b.x; });
            std::uint64_t solid = 0, reach = 0;
            std::size_t next = 0;
            int from = area.left();
            while (from <= area.right()) {
                while (next < edges.size() && edges[next].x <= from) {
                    const std::uint64_t bit = std::uint64_t{1} << edges[next].k;
                    switch (edges[next].crossing) {
                    case Crossing::IntoSolid: solid |= bit; break;
                    case Crossing::OutOfSolid: solid &= ~bit; break;
                    case Crossing::IntoReach: reach |= bit; break;
                    case Crossing::OutOfReach: reach &= ~bit; break;
                    }
                    ++next;
                }
                const int to = (next < edges.size() ? std::min(edges[next].x, area.right() + 1) : area.right() + 1) - 1;
                const bool rim = !bitsFit || (reach & ~solid) != 0, any = solid != 0;
                const int runFrom = from;
                from = to + 1;
                if (!rim && !any)
                    continue;  // reached by none: nothing laid
                if (!rim) {
                    float top = 0.0f;
                    std::int32_t mask = 0;
                    const auto known = std::find_if(sets.begin(), sets.end(), [&](const KnownSet& set) { return set.solid == solid; });
                    if (known != sets.end()) {
                        top = known->top;
                        mask = known->mask;
                    } else {
                        for (std::size_t k = 0; k < here.size(); ++k)
                            if (((solid >> k) & 1u) && addShare(mask, here[k]->share, 1.0f))
                                top = std::max(top, here[k]->top);
                        sets.push_back({solid, top, mask});
                    }
                    if (mask <= 0)
                        continue;
                    const std::size_t index = static_cast<std::size_t>(indexIn(job.coord, runFrom, y));
                    towardRun(tile->px.data() + index * 4, memory.top.get() + index, to - runFrom + 1, top, mask);
                    continue;
                }
                if (bitsFit && inLanes) {
                    // kWideLanes pixels of the run at a time, each taking the stamps as one pixel takes them below.
                    for (int x0 = runFrom; x0 <= to; x0 += kWideLanes) {
                        const int lanes = std::min(kWideLanes, to - x0 + 1);
                        const unsigned inRun = (1u << lanes) - 1u;
                        alignas(32) std::int32_t mask[kWideLanes] = {};
                        alignas(32) float top[kWideLanes] = {}, kept[kWideLanes];  // kept: rimAccumulateLanes' climb, not read here
                        std::fill_n(kept, kWideLanes, 1.0f);
                        for (std::uint64_t over = reach; over != 0; over &= over - 1) {
                            const std::size_t k = static_cast<std::size_t>(std::countr_zero(over));
                            const EventStamp* stamp = here[k];
                            alignas(32) std::int32_t share[kWideLanes] = {};
                            alignas(32) float cover[kWideLanes];
                            std::fill_n(cover, kWideLanes, 1.0f);
                            const std::int32_t solidShare = maskOf(stamp->share);  // its share, the cover whole
                            if ((solid >> k) & 1u) {
                                for (int i = 0; i < lanes; ++i)
                                    share[i] = solidShare;
                            } else {
                                alignas(32) float dx[kWideLanes] = {}, distanceSquared[kWideLanes];
                                for (int i = 0; i < lanes; ++i)
                                    dx[i] = stamp->frame.dx(x0 + i);
                                unsigned solidLanes = 0, rimLanes = 0;
                                rimDistanceLanes(dx, stamp->frame.dy(y), stamp->solidSquared, stamp->outsideSquared, distanceSquared,
                                                 solidLanes, rimLanes);
                                for (unsigned on = solidLanes & inRun; on != 0; on &= on - 1)
                                    share[std::countr_zero(on)] = solidShare;
                                for (unsigned on = rimLanes & inRun; on != 0; on &= on - 1) {
                                    const int i = std::countr_zero(on);
                                    cover[i] = rimCover(*stamp, dx[i], stamp->frame.dy(y), distanceSquared[i], x0 + i, y);
                                    share[i] = maskOf(stamp->share * cover[i]);
                                }
                            }
                            rimAccumulateLanes(mask, top, kept, share, cover, stamp->top, 1.0f);
                        }
                        for (int i = 0; i < lanes; ++i) {
                            if (mask[i] <= 0)
                                continue;
                            const std::size_t index = static_cast<std::size_t>(indexIn(job.coord, x0 + i, y));
                            toward(tile->px.data() + index * 4, memory.top[index], top[i], mask[i]);
                        }
                    }
                    continue;
                }
                for (int x = runFrom; x <= to; ++x) {
                    std::int32_t mask = 0;
                    float top = 0.0f;
                    // Only the stamps reaching the run, in stamp order: the rest the test would pass over; those whose solid holds it cover it.
                    std::uint64_t over = bitsFit ? reach : 0;
                    for (std::size_t n = 0; bitsFit ? over != 0 : n < here.size(); ++n) {
                        std::size_t k = n;
                        if (bitsFit) {
                            k = static_cast<std::size_t>(std::countr_zero(over));
                            over &= over - 1;
                        }
                        const EventStamp* stamp = here[k];
                        float cover = 1.0f;
                        if (!(bitsFit && ((solid >> k) & 1u))) {
                            const float dx = stamp->frame.dx(x), dy = stamp->frame.dy(y), distanceSquared = dx * dx + dy * dy;
                            if (distanceSquared >= stamp->outsideSquared)
                                continue;
                            if (distanceSquared >= stamp->solidSquared)
                                cover = rimCover(*stamp, dx, dy, distanceSquared, x, y);
                        }
                        if (addShare(mask, stamp->share, cover))
                            top = std::max(top, stamp->top);
                    }
                    if (mask <= 0)
                        continue;
                    const std::size_t index = static_cast<std::size_t>(indexIn(job.coord, x, y));
                    toward(tile->px.data() + index * 4, memory.top[index], top, mask);
                }
            }
        }
    };
    const std::int64_t pixels = static_cast<std::int64_t>(dab.area.width()) * dab.area.height();
    if (m_onWorkers && pixels >= kParallelPixels && jobs.size() > 1)
        WorkerPool::forStrokes().run(static_cast<int>(jobs.size()), layTile);
    else
        for (int j = 0; j < static_cast<int>(jobs.size()); ++j)
            layTile(j);
}
