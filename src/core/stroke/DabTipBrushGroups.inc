// Part of DabTip.cpp, which includes it where it stands: the Brush and Water Color laid in groups (layBrushGroup), their mixing, and
// paintDab, which picks how a dab is laid.

// The Brush's group, as the reference's. Its mask: over every pixel its stamps' dabs reach, each stamp takes its share of what's left
// to full, in whole numbers, its opacity times the reference's dab at the pixel's distance (BrushDab.h); on the 5th Shape with Anti-Ripple
// the dab Brush's mark with its neighbours' skirts, cut at the plate's edge (kBrushPlateEdgeEnd). Then its pickup, the plain average of
// what's shown over every pixel the mask touches, read before it lays; what it carries from that (brushGroupPaintFor); and each pixel moved
// toward that by the mask. Reading every pixel matters: the dab Brush's pickup, 8 places across over its stamps' outlines, missed most of
// a one-stamp group's new ground, so a slow stroke picked up mostly its own paint (A2's slowed line at Persistence 0: 7.56 → 1.91 levels).
void DabPainter::layBrushGroup(const Dab& dab) {
    struct GroupStamp {
        PixelFrame frame;
        float nominal, reachSquared, opacity;
        float stretch = 1.0f,
              out = 0.0f;  // the dab read at its half point + (along - out - half point) * stretch, out a share of the radius
        float penSamples = 0.0f;  // Stamp::penSamplesAcross
    };
    const int row = brushDabRow(m_tip.edge, m_mixing.antiRipple);
    const bool plate = row == kBrushDabAntiRippleRow && m_tip.skirtNeighbours > 0;
    const bool brush = m_mixing.kind == TipMixing::Kind::Brush;
    // The Brush's 1st to 4th Shapes: the dab's edge blurred.
    const bool dabBlurred = brush && row < kBrushDabAntiRippleRow;
    const float dabBlur = dabBlurred ? bySharpness(kBrushDabBlurBySharpness, m_tip.rimShare) : 0.0f;
    const float halfAt = dabBlurred ? kBrushDabHalfAt[static_cast<std::size_t>(row)] : 0.0f;
    std::vector<GroupStamp> stamps;
    QRect area = dab.area;
    for (std::size_t i = dab.first; i < dab.first + dab.count; ++i) {
        const Stamp& stamp = m_stamps[i];
        const float nominal = stamp.nominalRadius > 0.0f ? stamp.nominalRadius : stamp.radius;
        float stretch = 1.0f, out = 0.0f, end = brushDabEnd(row);
        if (dabBlurred && nominal > 0.0f) {
            const float width = kBrushDabEdgeWidth[static_cast<std::size_t>(row)] * nominal,
                        wider = std::sqrt(width * width + dabBlur * dabBlur);
            stretch = width / wider;
            out = kBrushDabBlurShift[static_cast<std::size_t>(row)] * (wider - width) / nominal;
            end = halfAt + (end - halfAt) / stretch + std::max(out, 0.0f);
        }
        const float reach = end * nominal + kSolidMargin;  // past it the dab reads none
        stamps.push_back({PixelFrame(stamp.centre), nominal, reach * reach, std::clamp(stamp.opacity, 0.0f, 1.0f), stretch, out,
                          stamp.penSamplesAcross});
        const double box = std::max(kBrushDabReach, end) * nominal + kBrushGroupBoxMargin;
        area |= QRect(QPoint(static_cast<int>(std::floor(stamp.centre.x() - box)), static_cast<int>(std::floor(stamp.centre.y() - box))),
                      QPoint(static_cast<int>(std::ceil(stamp.centre.x() + box)), static_cast<int>(std::ceil(stamp.centre.y() + box))));
    }
    // The plate's edge by Sharpness: the Brush's with its tail; Water Color's widened inward below 50, kBrushPlateEdgeWidth from 50 up.
    const float softer = static_cast<float>(std::clamp(2.0 * m_tip.rimShare - 1.0, 0.0, 1.0));
    const float plateWidth = brush ? bySharpness(kBrushPlateWidthBySharpness, m_tip.rimShare)
                                   : kBrushPlateEdgeWidth + (kBrushPlateEdgeWidthSoftest - kBrushPlateEdgeWidth) * softer;
    const float plateEnd = brush ? bySharpness(kBrushPlateEndBySharpness, m_tip.rimShare) : kBrushPlateEdgeEnd;
    const float tailLength = brush ? bySharpness(kBrushPlateTailBySharpness, m_tip.rimShare) : 0.0f;
    const float kneeAt = kBrushPlateTailKnee * plateWidth;  // px inside the straight part's end where the tail takes over
    const float tailOut = brush ? std::max(tailLength - kneeAt, 0.0f) : 0.0f;  // px the tail reaches past the straight part's end
    // The plate's edge at a pixel: the most of it any of `edgeStamps` (those the group's skirts see that reach the tile) has there.
    const std::size_t edgeEnd =
        std::min(dab.first + dab.count + static_cast<std::size_t>(std::max(m_tip.skirtNeighbours, 0)), m_stamps.size());
    struct EdgeStamp {
        PixelFrame frame;
        float end;  // px from its centre where its edge is gone
        float core;  // px from its centre where its straight part ends
    };
    std::vector<EdgeStamp> edges;
    for (std::size_t c = 0; c < edgeEnd; ++c) {
        const Stamp& one = m_stamps[c];
        const float radius = one.nominalRadius > 0.0f ? one.nominalRadius : one.radius;
        const float small = std::clamp((kBrushPlateSmallTo - radius) / (kBrushPlateSmallTo - kBrushPlateSmallFrom), 0.0f, 1.0f);
        const float pullIn = brush ? kBrushPlatePullIn * small : 0.0f;
        const float core = radius + plateEnd - pullIn;
        edges.push_back({PixelFrame(one.centre), core + tailOut, core});
    }
    const auto edgeShare = [&](float in) {  // `in` px inside the straight part's end
        if (tailLength <= 0.0f || in >= kneeAt)
            return std::clamp(in / plateWidth, 0.0f, 1.0f);
        return kBrushPlateTailKnee * std::clamp((in - (kneeAt - tailLength)) / tailLength, 0.0f, 1.0f);
    };
    const auto plateEdge = [&](const std::vector<std::size_t>& edgeStamps, int x, int y) {
        float most = 0.0f;
        for (std::size_t n = 0; n < edgeStamps.size() && most < 1.0f; ++n) {
            const EdgeStamp& one = edges[edgeStamps[n]];
            const float dx = one.frame.dx(x), dy = one.frame.dy(y);
            most = std::max(most, edgeShare(one.core - std::sqrt(dx * dx + dy * dy)));
        }
        return most;
    };
    if (plate)
        prepareWholeStamps(dab);
    // The plate's mask in runs: where every stamp of the group lies wholly inside its solid reach or past both its outline and its skirt,
    // dabMark adds the same numbers in the same order at every pixel, and the skirt adds none, so it's worked out once a run. Only without
    // evening across (acrossFactor 1) and without passes along the path.
    const bool inRuns = plate && !m_mixing.outlineAlongPath &&
                        std::all_of(m_stamps.begin() + static_cast<std::ptrdiff_t>(dab.first),
                                    m_stamps.begin() + static_cast<std::ptrdiff_t>(dab.first + dab.count),
                                    [](const Stamp& stamp) { return stamp.chordRadius <= 0.0f; });
    // Where each stamp can lay anything, a circle: its dab's end, or with the plate its mark's outline and its skirt. Past every one of them
    // the mask is none, so a tile's rows are worked out only along them.
    struct Circle {
        double x, y, radius;
    };
    std::vector<Circle> reaches;
    for (std::size_t k = 0; k < dab.count; ++k) {
        const QPointF centre = m_stamps[dab.first + k].centre;
        const float squared = plate ? std::max(m_wholeStamps[k].reachSquared, m_wholeStamps[k].outsideSquared) : stamps[k].reachSquared;
        reaches.push_back({centre.x(), centre.y(), std::sqrt(static_cast<double>(squared))});
    }
    // Water Color's pickup counts the bare paper out to kWaterPickUpReach radii of its stamps, past its mask, as nothing (by Shape
    // and size).
    const double pickReach = m_mixing.kind == TipMixing::Kind::WaterColor ? waterPickUpReach(m_tip.edge, m_tip.radius) : 0.0;
    // The circles the mask can be above none in: past them it's none, so a row's pixels are worked out only inside them; the wider
    // circles, the pickup's, only set what the pickup and the lay look over.
    std::vector<double> maskRadii;
    maskRadii.reserve(reaches.size());
    for (const Circle& c : reaches)
        maskRadii.push_back(c.radius);
    if (pickReach > 0.0)
        for (std::size_t k = 0; k < dab.count; ++k)
            reaches[k].radius = std::max(reaches[k].radius, pickReach * m_stamps[dab.first + k].radius);
    // A row's pixels within the pickup's reach of any stamp, from `from` to `to`: each stamp's are one run along the row (its distance
    // falls then rises), its ends found by the very test a pixel makes, so a pixel is in it exactly when the test says.
    const auto pickRow = [&](int y, int from, int to, std::vector<char>& picked) {
        picked.assign(static_cast<std::size_t>(to - from + 1), 0);
        for (std::size_t k = 0; k < dab.count; ++k) {
            const Stamp& one = m_stamps[dab.first + k];
            const double cx = one.centre.x(), dy = y + 0.5 - one.centre.y(), r = pickReach * one.radius;
            const auto inside = [&](int x) {
                const double dx = x + 0.5 - cx;
                return dx * dx + dy * dy <= r * r;
            };
            if (dy * dy > r * r)
                continue;  // none of the row: dx * dx only adds
            const double half = std::sqrt(r * r - dy * dy);
            int a = std::max(from, static_cast<int>(std::floor(cx - 0.5 - half)) - 1),
                b = std::min(to, static_cast<int>(std::ceil(cx - 0.5 + half)) + 1);
            while (a <= b && !inside(a))
                ++a;
            while (b >= a && !inside(b))
                --b;
            if (a > b)
                continue;
            while (a > from && inside(a - 1))
                --a;
            while (b < to && inside(b + 1))
                ++b;
            std::fill(picked.begin() + (a - from), picked.begin() + (b - from) + 1, char{1});
        }
    };
    // px: the spans and the stamps a tile takes reach this far past each circle, so no pixel a distance test would count is left out.
    constexpr double kSpanMargin = 1.0;
    const auto touchesBox = [](const QRect& box, double x, double y, double radius) {
        const double nearX = std::clamp(x, static_cast<double>(box.left()), static_cast<double>(box.right() + 1));
        const double nearY = std::clamp(y, static_cast<double>(box.top()), static_cast<double>(box.bottom() + 1));
        return (nearX - x) * (nearX - x) + (nearY - y) * (nearY - y) <= (radius + kSpanMargin) * (radius + kSpanMargin);
    };
    // A pixel's plate mark in plain sums, where it can be: off a grid, where every stamp of the group is wholly solid there, past both its
    // outline and its skirt, or past its outline inside its skirt with a neighbour (skirtAt's) wholly covering the pixel, the skirt adds the
    // stamp's paint times its skirt share at that distance, its own outline being none there. dabMark's and skirtAt's sums in their order.
    // False where a stamp's rim crosses the pixel or a skirt has no whole neighbour there: dabMark works that out.
    // `near`: the stamps that reach the pixel's tile, in their order; the others are past their reach all over it and add nothing.
    const auto plainMark = [&](const std::vector<std::size_t>& near, int x, int y, StampMark& mark) {
        mark = StampMark{};
        float added = 0.0f, skirtOutline = 0.0f;
        const auto around = static_cast<std::size_t>(std::max(m_tip.skirtNeighbours, 0));
        const std::size_t aroundEnd = m_aroundFirst + m_around.size();
        for (const std::size_t k : near) {
            const WholeStamp& whole = m_wholeStamps[k];
            const float dx = whole.frame.dx(x), dy = whole.frame.dy(y), distanceSquared = dx * dx + dy * dy;
            if (distanceSquared < whole.outsideSquared) {
                if (distanceSquared >= whole.solidSquared)
                    return false;  // its rim
                mark.paint += whole.paint;
                mark.shape = 1.0f;
                continue;
            }
            if (distanceSquared >= whole.reachSquared || distanceSquared < whole.solidSquared)
                continue;  // past its skirt
            const std::size_t i = dab.first + k;
            const Stamp& stamp = m_stamps[i];
            const float distance = std::sqrt(distanceSquared);
            const float share = skirtShare(stamp, distance - stamp.radius), own = outlineShareOf(stamp, distance);
            if (share <= own)
                continue;
            bool covered = false;
            for (std::size_t step = 1; step <= around && !covered; ++step)
                for (const std::size_t at : {i - step, i + step})
                    if (!covered && ((step <= i && at >= m_aroundFirst) || at > i) && at < aroundEnd) {
                        const AroundStamp& other = m_around[at - m_aroundFirst];
                        const float ax = other.frame.dx(x), ay = other.frame.dy(y);
                        covered = ax * ax + ay * ay < other.wholeSquared;
                    }
            if (!covered)
                return false;
            if (share > own)
                added += whole.paint * (share - own);
            skirtOutline = 1.0f;
        }
        mark.paint += added;
        mark.shape = std::max(mark.shape, skirtOutline);
        return true;
    };
    std::vector<TileCoord> reached;
    for (int ty = TiledImage::tileOf(area.top()); ty <= TiledImage::tileOf(area.bottom()); ++ty)
        for (int tx = TiledImage::tileOf(area.left()); tx <= TiledImage::tileOf(area.right()); ++tx)
            reached.push_back({tx, ty});
    // The tiles as they are, read by the mask and pickup pass; each is made writable on a worker only when the group lays on it, so a
    // tile shared with the frame on show is copied there, not one after another here, and a tile the mask misses isn't copied at all.
    std::vector<const Tile*> tiles;
    for (const TileCoord coord : reached) {
        tiles.push_back(m_layer.tile(coord.x, coord.y));
        m_changed.insert(coord);
    }
    static const Tile kNoTile{};  // what a tile not in the layer holds: nothing
    const auto boxOf = [&](std::size_t j) { return area.intersected(reached[j].pixelRect()); };
    std::vector<std::vector<std::int32_t>> masks(reached.size());
    // Each tile row's first and last pixel the mask may be above none at (first past last where it's none all along).
    std::vector<std::vector<std::pair<int, int>>> rowExtents(reached.size());
    // The same for the mask alone, which the group lays over: past it the pickup's wider reach only reads (layExtents).
    std::vector<std::vector<std::pair<int, int>>> layExtents(reached.size());
    // The pickup, added up a tile at a time as its mask is worked out, before the group lays: in doubles, as a wide group touches millions
    // of pixels, past which a float's total moves in steps coarser than a pixel's value (from about a million on, an eighth; from 8 million
    // on, 1). The tiles' sums are then added in their order, so they come out the same however many workers there are. Empty pixels count,
    // as nothing.
    struct PickedSum {
        std::array<std::uint64_t, 4> channels{};  // off the paper: the 16-bit channels themselves, added exactly
        std::array<double, 4> sum{};  // through the paper, what's shown there
        std::uint64_t count = 0;
    };
    std::vector<PickedSum> picks(reached.size());
    const bool keepsOutline = m_paint.grain && m_paint.edgeBarDepth > 0.0f;
    std::vector<std::vector<float>> outlines(keepsOutline ? reached.size() : 0);
    const bool throughPaper =
        m_paint.grain && m_paint.picksUpThroughPaper && !m_paint.paperPerDab;  // else what's shown is the layer's own pixel
    // Water Color at Dilution 0 picks up a bare pixel as the brush colour, not as nothing.
    std::array<std::uint32_t, 4> bareChannels{};
    const bool bareIsOwn = m_mixing.kind == TipMixing::Kind::WaterColor && m_mixing.dilution <= 0.0;
    if (bareIsOwn)
        for (std::size_t c = 0; c < 4; ++c)
            bareChannels[c] = static_cast<std::uint32_t>(std::lround(std::clamp(m_own[c], 0.0f, 1.0f) * kChannelOne));
    const auto maskTile = [&](int j) {
        const QRect box = boxOf(static_cast<std::size_t>(j));
        std::vector<std::int32_t>& mask = masks[static_cast<std::size_t>(j)];
        mask.assign(static_cast<std::size_t>(box.width()) * static_cast<std::size_t>(box.height()), 0);
        std::vector<std::pair<int, int>>& extents = rowExtents[static_cast<std::size_t>(j)];
        extents.assign(static_cast<std::size_t>(box.height()), {box.right() + 1, box.right()});
        std::vector<std::pair<int, int>>& layAlong = layExtents[static_cast<std::size_t>(j)];
        layAlong.assign(static_cast<std::size_t>(box.height()), {box.right() + 1, box.right()});
        std::vector<std::size_t> near, edgeStamps;  // in their order, as each takes its share of what's left
        for (std::size_t k = 0; k < reaches.size(); ++k)
            if (touchesBox(box, reaches[k].x, reaches[k].y, reaches[k].radius))
                near.push_back(k);
        if (near.empty())
            return;
        // On paper with an edge bar, the group's outline at each pixel it lays on, kept as the mask is worked out: the plate's
        // edge, or where no plate cuts it, whole.
        std::vector<float>* outline = keepsOutline ? &outlines[static_cast<std::size_t>(j)] : nullptr;
        if (outline)
            outline->assign(mask.size(), 0.0f);
        if (plate) {  // the group's own first, which most often cover whole: the most is the same in any order, found sooner
            for (std::size_t c = dab.first; c < dab.first + dab.count && c < edgeEnd; ++c)
                if (touchesBox(box, m_stamps[c].centre.x(), m_stamps[c].centre.y(), edges[c].end))
                    edgeStamps.push_back(c);
            for (std::size_t c = 0; c < edgeEnd; ++c)
                if ((c < dab.first || c >= dab.first + dab.count) &&
                    touchesBox(box, m_stamps[c].centre.x(), m_stamps[c].centre.y(), edges[c].end))
                    edgeStamps.push_back(c);
        }
        // A whole tile: every stamp of the group wholly solid over it or wholly past its outline and skirt, and, where it lays, one stamp's
        // edge whole over all of it. Its mask is one value, a run's. Rounding never makes a larger number smaller, so a pixel's distance
        // as worked out is never past the box's farthest corner's nor short of its nearest: the corners decide for every pixel.
        bool wholeTile = false;
        const auto nearestFarthest = [&](const PixelFrame& frame, float& nearest, float& farthest) {
            const float x0 = frame.dx(box.left()), x1 = frame.dx(box.right()), y0 = frame.dy(box.top()), y1 = frame.dy(box.bottom());
            const float nx = x0 <= 0.0f && x1 >= 0.0f ? 0.0f : std::min(std::abs(x0), std::abs(x1));
            const float ny = y0 <= 0.0f && y1 >= 0.0f ? 0.0f : std::min(std::abs(y0), std::abs(y1));
            const float fx = std::max(std::abs(x0), std::abs(x1)), fy = std::max(std::abs(y0), std::abs(y1));
            nearest = nx * nx + ny * ny;
            farthest = fx * fx + fy * fy;
        };
        // A tile wholly inside one plate stamp's edge, its disc less the edge's width: there the plate can't cut anything, so no pixel
        // looks at the plate.
        const bool edgeWholeTile = plate && std::any_of(edgeStamps.begin(), edgeStamps.end(), [&](std::size_t n) {
                                       float nearest = 0.0f, farthest = 0.0f;
                                       nearestFarthest(edges[n].frame, nearest, farthest);
                                       return (edges[n].core - std::sqrt(farthest)) / plateWidth >= 1.0f;
                                   });
        if (inRuns) {
            StampMark mark;
            bool even = true;
            for (std::size_t k = 0; k < dab.count && even; ++k) {
                const WholeStamp& whole = m_wholeStamps[k];
                float nearest = 0.0f, farthest = 0.0f;
                nearestFarthest(whole.frame, nearest, farthest);
                if (farthest < whole.outsideSquared && farthest < whole.solidSquared) {
                    mark.paint += whole.paint;
                    mark.shape = 1.0f;
                } else if (!(nearest >= whole.outsideSquared && nearest >= whole.reachSquared)) {
                    even = false;
                }
            }
            const bool edgeWhole = mark.shape <= 0.0f || edgeWholeTile;
            if (even && edgeWhole) {
                const std::int32_t value =
                    mark.shape > 0.0f
                        ? maskOf(static_cast<float>(-std::expm1(-static_cast<double>(mark.paint))) * std::min(1.0f, 1.0f / mark.shape))
                        : 0;
                if (value > 0) {
                    std::fill(mask.begin(), mask.end(), value);
                    std::fill(extents.begin(), extents.end(), std::pair{box.left(), box.right()});
                    std::fill(layAlong.begin(), layAlong.end(), std::pair{box.left(), box.right()});
                    if (outline)
                        std::fill(outline->begin(), outline->end(), 1.0f);
                }
                wholeTile = value > 0 || pickReach <= 0.0;
            }
        }
        std::vector<char>
            perPixel;  // with the plate in runs: 1 where a pixel lies within kSpanMargin of a circle a stamp's mark changes on
        std::vector<std::pair<int, int>> spans;
        for (int y = box.top(); y <= box.bottom() && !wholeTile; ++y) {
            const auto spansOf = [&](bool mine) {
                spans.clear();
                for (const std::size_t k : near) {
                    const Circle& c = reaches[k];
                    const double radius = mine ? maskRadii[k] : c.radius;
                    const double across = std::max(0.0, std::abs(y + 0.5 - c.y) - kSpanMargin);
                    if (across >= radius + kSpanMargin)
                        continue;
                    const double half = std::sqrt(std::max(0.0, (radius + kSpanMargin) * (radius + kSpanMargin) - across * across));
                    const int from = std::max(box.left(), static_cast<int>(std::floor(c.x - half - kSpanMargin)));
                    const int to = std::min(box.right(), static_cast<int>(std::ceil(c.x + half + kSpanMargin)));
                    if (from <= to)
                        spans.push_back({from, to});  // NOLINT(modernize-use-emplace): emplace_back(from, to) measured slower here
                }
                std::sort(spans.begin(), spans.end());
            };
            spansOf(false);
            if (!spans.empty())
                extents[static_cast<std::size_t>(y - box.top())] = {
                    spans.front().first,
                    std::max_element(spans.begin(), spans.end(), [](const auto& a, const auto& b) { return a.second < b.second; })->second};
            if (pickReach > 0.0)
                spansOf(true);  // the mask's own, which the pixels below are worked out over
            if (!spans.empty())
                layAlong[static_cast<std::size_t>(y - box.top())] = {
                    spans.front().first,
                    std::max_element(spans.begin(), spans.end(), [](const auto& a, const auto& b) { return a.second < b.second; })->second};
            if (inRuns) {
                perPixel.assign(static_cast<std::size_t>(box.width()), 0);
                for (const std::size_t k : near) {
                    const WholeStamp& whole = m_wholeStamps[k];
                    const double centre = m_stamps[dab.first + k].centre.x(), across = whole.frame.dy(y);
                    for (const float squared : {whole.solidSquared, whole.outsideSquared, whole.reachSquared}) {
                        if (squared <= 0.0f || static_cast<double>(squared) <= across * across)
                            continue;
                        const double half = std::sqrt(static_cast<double>(squared) - across * across);
                        for (const double at : {centre - 0.5 - half, centre - 0.5 + half})
                            for (int x = std::max(box.left(), static_cast<int>(std::floor(at - kSpanMargin)));
                                 x <= std::min(box.right(), static_cast<int>(std::ceil(at + kSpanMargin))); ++x)
                                perPixel[static_cast<std::size_t>(x - box.left())] = 1;
                    }
                }
            }
            // A run's mark, where every stamp is wholly solid there or adds nothing: false where one doesn't.
            const auto runMark = [&](int x, StampMark& mark) {
                mark = StampMark{};
                for (const std::size_t k : near) {  // the others are past their reach all over the tile
                    const WholeStamp& whole = m_wholeStamps[k];
                    const float dx = whole.frame.dx(x), dy = whole.frame.dy(y), distanceSquared = dx * dx + dy * dy;
                    if (distanceSquared < whole.outsideSquared && distanceSquared < whole.solidSquared) {
                        mark.paint += whole.paint;
                        mark.shape = 1.0f;
                    } else if (distanceSquared < whole.outsideSquared ||
                               (distanceSquared < whole.reachSquared && distanceSquared >= whole.solidSquared)) {
                        return false;
                    }
                }
                return true;
            };
            int done = box.left() - 1;  // the last x worked out on this row
            for (const auto& [spanFrom, spanTo] : spans)
                for (int x = std::max(spanFrom, done + 1); x <= spanTo; ++x, done = x - 1) {
                    std::int32_t value = 0;
                    StampMark mark;
                    if (inRuns && !perPixel[static_cast<std::size_t>(x - box.left())] && runMark(x, mark)) {
                        // the run, to its end or the next pixel near a circle
                        int last = x;
                        while (last + 1 <= spanTo && !perPixel[static_cast<std::size_t>(last + 1 - box.left())])
                            ++last;
                        const float whole = static_cast<float>(-std::expm1(-static_cast<double>(mark.paint)));
                        // A stamp's edge share falls off convexly along a row (a circle's distance), so where one stamp's is whole with
                        // kWholeEdgeSpare to spare at both ends of the run, it's whole all along it, and so is the run's mask.
                        constexpr float kWholeEdgeSpare = 1e-3f;
                        const auto wholeAt = [&](const EdgeStamp& one, int at) {
                            const float dx = one.frame.dx(at), dy = one.frame.dy(y);
                            return (one.core - std::sqrt(dx * dx + dy * dy)) / plateWidth >= 1.0f + kWholeEdgeSpare;
                        };
                        const bool edgeWhole =
                            mark.shape > 0.0f && (edgeWholeTile || std::any_of(edgeStamps.begin(), edgeStamps.end(), [&](std::size_t n) {
                                                      return wholeAt(edges[n], x) && wholeAt(edges[n], last);
                                                  }));
                        if (edgeWhole) {
                            const std::int32_t wholeMask = maskOf(whole * std::min(1.0f, 1.0f / mark.shape));
                            std::fill_n(mask.begin() + static_cast<std::ptrdiff_t>((y - box.top()) * box.width() + (x - box.left())),
                                        last - x + 1, wholeMask);
                            if (outline && wholeMask > 0)
                                std::fill_n(outline->begin() +
                                                static_cast<std::ptrdiff_t>((y - box.top()) * box.width() + (x - box.left())),
                                            last - x + 1, 1.0f);
                        } else {
                            for (int at = x; at <= last; ++at) {
                                const std::size_t i = static_cast<std::size_t>((y - box.top()) * box.width() + (at - box.left()));
                                const bool needsEdge = mark.shape > 0.0f || outline;
                                const float edge = needsEdge ? (edgeWholeTile ? 1.0f : plateEdge(edgeStamps, at, y)) : 1.0f;
                                const float cut = mark.shape > 0.0f ? std::min(1.0f, edge / mark.shape) : 1.0f;
                                mask[i] = maskOf(whole * cut);
                                if (outline && mask[i] > 0)
                                    (*outline)[i] = edge;
                            }
                        }
                        x = last;
                        continue;
                    }
                    float edgeHere = 1.0f;
                    if (plate) {
                        // Past the plate's edge the mask is none, as the reference's plate is cut there, so the mark isn't worked out.
                        const float edge = edgeWholeTile ? 1.0f : plateEdge(edgeStamps, x, y);
                        edgeHere = edge;
                        if (edge > 0.0f) {
                            StampMark here;
                            if (!(inRuns && m_gridCell <= kGridOne && plainMark(near, x, y, here)))
                                here = dabMark(dab, x, y, nullptr, false);
                            // the plate's edge stands in for the skirt's outline where it's the lower
                            const float cut = here.shape > 0.0f ? std::min(1.0f, edge / here.shape) : 1.0f;
                            value = maskOf(coverOfDensity(here.paint) * cut);
                        }
                    } else {
                        // Through the paper dab by dab, as the Pencil's soft Shapes (StampLayer::markOf): each stamp's dab less the paper's
                        // darkness times Intensity, down to nothing, before its opacity.
                        const float cutBy =
                            m_paint.paperPerDab && m_paint.grain ? m_paint.grain->intensity() * m_paint.grain->keptDarkAt(x, y) : 0.0f;
                        for (const std::size_t k : near) {
                            const GroupStamp& stamp = stamps[k];
                            const float dx = stamp.frame.dx(x), dy = stamp.frame.dy(y), distanceSquared = dx * dx + dy * dy;
                            if (distanceSquared >= stamp.reachSquared)
                                continue;
                            const float along = std::sqrt(distanceSquared) / stamp.nominal;
                            const float read = dabBlurred ? std::max(halfAt + (along - stamp.out - halfAt) * stamp.stretch, 0.0f) : along;
                            float dabShare = brushDabAt(row, read) - cutBy;
                            if (m_tip.form && m_tip.form->fixedToCanvas())  // a blot map's share for this stamp, its dab times it
                                dabShare *=
                                    m_tip.form->blotShape(std::sqrt(distanceSquared), stamp.nominal, x, y, m_tip.edge, blotWindow()) *
                                    (m_tip.edge >= kWaterSoftBlotFromShape
                                         ? std::min(1.0f, kWaterSoftBlotFloor + kWaterSoftBlotBySamples * stamp.penSamples)
                                         : 1.0f);
                            if (dabShare <= 0.0f)
                                continue;
                            const std::int32_t share = maskOf(stamp.opacity * dabShare);
                            if (share > 0)
                                value += ((kMaskOne - value) * share + (kMaskOne >> 1)) >> kMaskBits;
                        }
                    }
                    mask[static_cast<std::size_t>((y - box.top()) * box.width() + (x - box.left()))] = value;
                    if (outline && value > 0)
                        (*outline)[static_cast<std::size_t>((y - box.top()) * box.width() + (x - box.left()))] = edgeHere;
                }
        }
        PickedSum& picked = picks[static_cast<std::size_t>(j)];
        const TileCoord coord = reached[static_cast<std::size_t>(j)];
        const Channel* px = (tiles[static_cast<std::size_t>(j)] ? tiles[static_cast<std::size_t>(j)] : &kNoTile)->px.data();
        // Through the paper, what's shown is shownAt's: its share from the tile's shares the paper keeps for the stroke, the same numbers
        // as shareAt, and what was under before the stroke and the tile's record, found once a tile.
        const float* shares = throughPaper ? m_paint.grain->keptSharesOf(coord) : nullptr;
        const Tile* beforeTile = throughPaper ? m_before.tile(coord.x, coord.y) : nullptr;
        const Reach* record = nullptr;
        if (throughPaper && m_markerCell == 0)
            if (const auto found = m_reach.find(coord); found != m_reach.end())
                record = found->second.get();
        std::vector<char> pickAt;  // a row's pixels within the pickup's reach (pickRow)
        for (int y = box.top(); y <= box.bottom(); ++y) {
            const auto [from, to] = extents[static_cast<std::size_t>(y - box.top())];
            if (from > to)
                continue;
            const std::size_t rowStart = static_cast<std::size_t>((y - box.top()) * box.width() - box.left());
            const Channel* line =
                px + static_cast<std::size_t>(indexIn(coord, from, y)) * 4;  // the row's pixel at x is line[(x - from) * 4]
            if (!throughPaper) {
                // In whole numbers: a row's sums fit 32 bits (64 pixels of 16 bits), each pixel counted only if its alpha isn't empty.
                std::uint32_t sums[4] = {0, 0, 0, 0}, counted = 0;
                if (pickReach > 0.0 && !wholeTile)
                    pickRow(y, from, to, pickAt);
                const auto withinPickReach = [&](int x) { return pickAt[static_cast<std::size_t>(x - from)] != 0; };
                if (bareIsOwn) {
                    for (int x = from; x <= to; ++x) {
                        const std::uint32_t takes =
                            wholeTile || mask[rowStart + static_cast<std::size_t>(x)] > 0 ||
                                    (pickReach > 0.0 && line[static_cast<std::size_t>(x - from) * 4 + 3] < kEmptyChannel &&
                                     withinPickReach(x))
                                ? 1u
                                : 0u;
                        const Channel* at = line + static_cast<std::size_t>(x - from) * 4;
                        const std::uint32_t full = takes & (at[3] >= kEmptyChannel ? 1u : 0u);
                        counted += takes;
                        for (int c = 0; c < 4; ++c)
                            sums[c] += full * at[c] + (takes - full) * bareChannels[c];
                    }
                } else {
                    for (int x = from; x <= to; ++x) {
                        const std::uint32_t takes =
                            wholeTile || mask[rowStart + static_cast<std::size_t>(x)] > 0 ||
                                    (pickReach > 0.0 && line[static_cast<std::size_t>(x - from) * 4 + 3] < kEmptyChannel &&
                                     withinPickReach(x))
                                ? 1u
                                : 0u;
                        const Channel* at = line + static_cast<std::size_t>(x - from) * 4;
                        const std::uint32_t full = takes & (at[3] >= kEmptyChannel ? 1u : 0u);
                        counted += takes;
                        for (int c = 0; c < 4; ++c)
                            sums[c] += full * at[c];
                    }
                }
                picked.count += counted;
                for (std::size_t c = 0; c < 4; ++c)
                    picked.channels[c] += sums[c];
                continue;
            }
            // The row's ways through the paper at once, four at a time as the merge works them out (mixingWaysThroughPaper), then each
            // pixel's, as shownAt shows it, added in its order.
            const int count = to - from + 1;
            const int first = indexIn(coord, from, y);
            std::array<float, Tile::kSize> before, painted, ways;
            for (int n = 0; n < count; ++n) {
                before[static_cast<std::size_t>(n)] =
                    beforeTile ? beforeTile->px[static_cast<std::size_t>(first + n) * 4 + 3] * kChannelToFloat : 0.0f;
                painted[static_cast<std::size_t>(n)] = px[static_cast<std::size_t>(first + n) * 4 + 3] * kChannelToFloat;
            }
            if (record)  // an outline record, which the groups never keep: shownAt's own steps
                for (int n = 0; n < count; ++n)
                    ways[static_cast<std::size_t>(n)] =
                        mixingWayThroughPaper(m_paint, shares[first + n], before[static_cast<std::size_t>(n)],
                                              painted[static_cast<std::size_t>(n)], shapeOf(*record, static_cast<std::size_t>(first + n)));
            else
                mixingWaysThroughPaper(m_paint, coord, first, before.data(), painted.data(), count, ways.data());
            for (int x = from; x <= to; ++x) {
                if (!wholeTile && mask[rowStart + static_cast<std::size_t>(x)] <= 0)
                    continue;
                ++picked.count;
                const std::size_t index = static_cast<std::size_t>(first + (x - from));
                const Premultiplied below = beforeTile ? readPixel(beforeTile->px.data() + index * 4) : Premultiplied{};
                const Premultiplied under = lerp(below, readPixel(px + index * 4), ways[static_cast<std::size_t>(x - from)]);
                if (under[3] >= kEmptyAlpha)
                    for (std::size_t c = 0; c < 4; ++c)
                        picked.sum[c] += static_cast<double>(under[c]);
            }
        }
    };
    const std::int64_t pixels = static_cast<std::int64_t>(dab.area.width()) * dab.area.height();
    const auto eachTile = [&](const auto& work) {
        if (m_onWorkers && pixels >= kParallelPixels && reached.size() > 1)
            WorkerPool::forStrokes().run(static_cast<int>(reached.size()), work);
        else
            for (int j = 0; j < static_cast<int>(reached.size()); ++j)
                work(j);
    };
    eachTile(maskTile);
    if (keepsOutline)  // kept per tile, the most of every group's, on this thread
        for (std::size_t j = 0; j < reached.size(); ++j) {
            const std::vector<float>& outline = outlines[j];
            if (outline.empty())
                continue;
            const QRect box = boxOf(j);
            const TileCoord coord = reached[j];
            std::vector<float>& kept = m_groupOutline[coord];
            if (kept.empty())
                kept.assign(static_cast<std::size_t>(Tile::kSize * Tile::kSize), 0.0f);
            for (int y = box.top(); y <= box.bottom(); ++y)
                for (int x = box.left(); x <= box.right(); ++x) {
                    float& at = kept[static_cast<std::size_t>((y - coord.y * Tile::kSize) * Tile::kSize + (x - coord.x * Tile::kSize))];
                    at = std::max(at, outline[static_cast<std::size_t>((y - box.top()) * box.width() + (x - box.left()))]);
                }
        }
    // Whether the group lays on each tile at all, and the tiles it lays on that the layer doesn't hold yet, added here: adding changes the
    // layer's map, which only this thread may do.
    std::vector<char> lays(reached.size(), 0);
    for (std::size_t j = 0; j < reached.size(); ++j) {
        lays[j] = std::any_of(layExtents[j].begin(), layExtents[j].end(), [](const std::pair<int, int>& e) { return e.first <= e.second; });
        if (lays[j] && !tiles[j])
            m_layer.writableTile(reached[j].x, reached[j].y);
    }
    std::array<std::uint64_t, 4> channels{};
    std::array<double, 4> sum{};
    std::uint64_t pixelsPicked = 0;
    for (const PickedSum& picked : picks) {
        for (std::size_t c = 0; c < 4; ++c) {
            channels[c] += picked.channels[c];
            sum[c] += picked.sum[c];
        }
        pixelsPicked += picked.count;
    }
    const bool touches = pixelsPicked > 0;
    const double count = static_cast<double>(pixelsPicked);
    for (std::size_t c = 0; c < 4; ++c)
        sum[c] += static_cast<double>(channels[c]) / kChannelOne;
    Premultiplied total{};
    float weight = 0.0f;
    if (count > 0.0) {
        for (std::size_t c = 0; c < 4; ++c)
            total[c] = static_cast<float>(sum[c] / count);
        weight = 1.0f;
    }
    // A group whose mask touches nothing, as at no pressure, lays nothing, and so picks up and carries nothing: the first group that lays
    // loads what it carries from what's under it. Loaded here, it would carry the Brush's own colour onto the colour under its start (the
    // preview).
    if (!touches)
        return;
    m_picked = weight > 0.0f ? total : Premultiplied{};
    const Premultiplied paint = brushGroupPaintFor(m_picked, dab);
    // Water Color: what's there blurred over the group's area, on cells for a wide stamp (coarseCellFor), and each pixel's target
    // that blur moved toward what it carries by the pressure past Blur Pressure at the group's last stamp.
    const bool wet = m_mixing.kind == TipMixing::Kind::WaterColor;
    std::optional<Pixels> wetFine;
    std::optional<CoarseBlur> wetCoarse;
    float deposit = 1.0f;
    if (wet) {
        Dab blurred = dab;
        blurred.area = area;
        if (coarseCellFor(blurred, m_mixing.wetBlurRadii) > 1)
            wetCoarse = coarseBlurUnder(blurred, m_mixing.wetBlurRadii);
        else
            wetFine = blurUnder(blurred, m_mixing.wetBlurRadii);
        deposit = static_cast<float>(pastBlurPressure(m_stamps[dab.first + dab.count - 1].pressure));
    }
    // Eight at a time where AVX2 has them: shown() over an opaque colour, which the groups always lay (brushLaidInGroups).
    const bool inLanes =
        !m_mixing.transparentColour && m_paint.mode != LayMode::KeepAlpha && !s_onePixelAtATime.load(std::memory_order_relaxed);
    const auto layTile = [&](int j) {
        if (!lays[static_cast<std::size_t>(j)])
            return;
        const TileCoord coord = reached[static_cast<std::size_t>(j)];
        Tile* tile = m_layer.writableExistingTile(coord.x, coord.y);
        const QRect box = boxOf(static_cast<std::size_t>(j));
        const std::vector<std::int32_t>& mask = masks[static_cast<std::size_t>(j)];
        if (wet) {  // each pixel toward its own target
            // On cells, a row's two cell rows lerped across once for every row between them: CoarseBlur::at's numbers, in its order.
            std::vector<Premultiplied> above, below;
            int cellRow = std::numeric_limits<int>::min();
            for (int y = box.top(); y <= box.bottom(); ++y) {
                const auto [from, to] = layExtents[static_cast<std::size_t>(j)][static_cast<std::size_t>(y - box.top())];
                if (from > to)
                    continue;
                float down = 0.0f;
                if (wetCoarse) {
                    const CoarseBlur& blur = *wetCoarse;
                    const CoarseBlur::Between& cellOfRow = blur.rows[static_cast<std::size_t>(y - blur.dab.top())];
                    down = cellOfRow.share;
                    if (cellOfRow.first != cellRow) {
                        cellRow = cellOfRow.first;
                        above.resize(static_cast<std::size_t>(box.width()));
                        below.resize(static_cast<std::size_t>(box.width()));
                        for (int x = box.left(); x <= box.right(); ++x) {
                            const CoarseBlur::Between& across = blur.columns[static_cast<std::size_t>(x - blur.dab.left())];
                            const int x0 = across.first;
                            above[static_cast<std::size_t>(x - box.left())] =
                                lerp(blur.cells.at(x0, cellRow), blur.cells.at(x0 + 1, cellRow), across.share);
                            below[static_cast<std::size_t>(x - box.left())] =
                                lerp(blur.cells.at(x0, cellRow + 1), blur.cells.at(x0 + 1, cellRow + 1), across.share);
                        }
                    }
                }
                int x = from;
                if (inLanes)
                    for (; x + kWideLanes - 1 <= to; x += kWideLanes) {
                        const std::int32_t* run = mask.data() + static_cast<std::size_t>((y - box.top()) * box.width() + (x - box.left()));
                        alignas(32) float targets[kWideLanes * 4] = {};
                        bool any = false;
                        for (int k = 0; k < kWideLanes; ++k) {
                            if (run[k] <= 0)
                                continue;
                            any = true;
                            const std::size_t i = static_cast<std::size_t>(x + k - box.left());
                            const Premultiplied under = wetCoarse ? lerp(above[i], below[i], down) : wetFine->at(x + k, y);
                            const Premultiplied target = lerp(under, paint, deposit);
                            for (int c = 0; c < 4; ++c)
                                targets[k * 4 + c] = target[static_cast<std::size_t>(c)];
                        }
                        if (any)
                            waterLayEight(tile->px.data() + static_cast<std::size_t>(indexIn(coord, x, y)) * 4, run, targets);
                    }
                for (; x <= to; ++x) {
                    const std::int32_t value = mask[static_cast<std::size_t>((y - box.top()) * box.width() + (x - box.left()))];
                    if (value <= 0)
                        continue;
                    Channel* px = tile->px.data() + static_cast<std::size_t>(indexIn(coord, x, y)) * 4;
                    const std::size_t i = static_cast<std::size_t>(x - box.left());
                    const Premultiplied under = wetCoarse ? lerp(above[i], below[i], down) : wetFine->at(x, y);
                    writePixel(px,
                               lerp(readPixel(px), lerp(under, paint, deposit), static_cast<float>(value) / static_cast<float>(kMaskOne)));
                }
            }
            return;
        }
        for (int y = box.top(); y <= box.bottom(); ++y) {
            const auto [from, to] = layExtents[static_cast<std::size_t>(j)][static_cast<std::size_t>(y - box.top())];
            int x = from;
            if (inLanes)
                for (; x + kWideLanes - 1 <= to; x += kWideLanes)
                    brushLayEight(tile->px.data() + static_cast<std::size_t>(indexIn(coord, x, y)) * 4,
                                  mask.data() + static_cast<std::size_t>((y - box.top()) * box.width() + (x - box.left())), paint.data());
            for (; x <= to; ++x) {
                const std::int32_t value = mask[static_cast<std::size_t>((y - box.top()) * box.width() + (x - box.left()))];
                if (value <= 0)
                    continue;
                Channel* px = tile->px.data() + static_cast<std::size_t>(indexIn(coord, x, y)) * 4;
                writePixel(px, shown(readPixel(px), paint, static_cast<float>(value) / static_cast<float>(kMaskOne)));
            }
        }
    };
    eachTile(layTile);
    m_dirty |= area;
}

// What the Brush's group lays, in the reference's order: the brush colour mixed into its pickup by its share (kBrushGroupShareScale),
// what it carries moving toward that mix by the carry step (kBrushCarryStepPower), and it lays what it carries.
// The pressure's share past Blur Pressure, straight: Water Color laid in groups moves each pixel's target toward what it carries by it.
double DabPainter::pastBlurPressure(double pressure) const {
    return std::clamp((pressure - m_mixing.blurPressure) / std::max(1.0 - m_mixing.blurPressure, 1e-6), 0.0, 1.0);
}

// Water Color laid in groups: Dilution, then Blending (kWaterDilutionPower). share is set to the brush colour's share.
Premultiplied DabPainter::waterGroupMix(const Premultiplied& picked, const Dab& dab, double& share) const {
    const float pickedAlpha = std::clamp(picked[3], 0.0f, 1.0f);
    const Premultiplied ownStraight =
        m_own[3] > 1e-6f ? Premultiplied{m_own[0] / m_own[3], m_own[1] / m_own[3], m_own[2] / m_own[3], 1.0f} : Premultiplied{};
    const Premultiplied standIn = pickedAlpha >= kWaterNearOpaque
                                      ? Premultiplied{picked[0] / pickedAlpha, picked[1] / pickedAlpha, picked[2] / pickedAlpha, 1.0f}
                                      : m_own;
    const double touch = m_mixing.dilutionFollowsPressure ? pressureResponse(dab.pressure, 100) : 1.0;
    const double diluted = std::clamp(m_mixing.dilution * touch, 0.0, 1.0);
    const Premultiplied loaded = lerp(picked, standIn, static_cast<float>(std::pow(1.0 - diluted, kWaterDilutionPower)));
    const double blending = std::clamp(
        m_mixing.blending * pressureShare(dab.pressure, m_mixing.blendingFollowsPressure, 0.0, m_mixing.blendingSensitivity), 0.0, 1.0);
    share = std::min(1.0, std::pow(1.0 - blending, kWaterBlendingCurve) * std::pow(pastBlurPressure(dab.pressure), kWaterFadePower) *
                              kWaterShareScale) *
            (1.0 - kWaterThickShare * pickedAlpha * m_mixing.blending);
    const float a = loaded[3];
    return lerp(loaded, Premultiplied{ownStraight[0] * a, ownStraight[1] * a, ownStraight[2] * a, a}, static_cast<float>(share));
}

Premultiplied DabPainter::brushGroupPaintFor(const Premultiplied& picked, const Dab& dab) {
    const bool moving = onlyMoving(dab.pressure);
    m_dabMemory = static_cast<float>(memoryFor(dab));
    m_lastDabPressure = dab.pressure;
    m_lastDabCentre = m_stamps[dab.first + dab.count / 2].centre;
    double share = 0.0;
    if (!moving) {
        const double blending = std::clamp(
            m_mixing.blending * pressureShare(dab.pressure, m_mixing.blendingFollowsPressure, 0.0, m_mixing.blendingSensitivity), 0.0, 1.0);
        share = std::min(1.0, std::pow(1.0 - blending, kBrushGroupBlendingCurve) * colourFadeIn(dab.pressure) * kBrushGroupShareScale);
    }
    const bool wet = m_mixing.kind == TipMixing::Kind::WaterColor;
    Premultiplied mixed = moving ? movedPaint(picked)
                          : wet  ? waterGroupMix(picked, dab, share)
                                 : lerp(asPaint(picked, dab), m_own, static_cast<float>(share));
    const double stepKeep = 1.0 - std::pow(1.0 - std::clamp(m_mixing.persistence, 0.0, 1.0), kBrushCarryStepPower);
    const double keep = m_stampsCarried == 0.0 ? 0.0 : std::pow(stepKeep, static_cast<double>(dab.count));
    m_stampsCarried += dab.steps * m_tip.spacingShare / kPersistenceSpacing;
    m_carried = lerp(mixed, m_carried, static_cast<float>(keep));
    m_dabShare = static_cast<float>(share);
    m_lastLaid = m_carried;
    m_hasLaid = true;
    return m_carried;
}

void DabPainter::paintDab(const Dab& dab) {
    if (m_mixing.kind == TipMixing::Kind::Smudge) {
        smudgeDab(dab);
        m_dirty |= dab.area;
        return;
    }
    ++m_dabNumber;
    if (m_betweenDabs)
        m_betweenDabs();  // what's done so far shows before this dab's blur is worked out
    unpackAround(dab);
    if (laysBrushGroups()) {  // it picks up over its own mask, so it works that out first
        layBrushGroup(dab);
        m_dirty |= dab.area;
        return;
    }
    m_picked = m_eventPicked ? *m_eventPicked : pickUp(dab);
    const Premultiplied paint = paintFor(m_picked, dab);
    if (laysEvents()) {
        layEvent(dab, paint);
        m_dirty |= dab.area;
        return;
    }
    recycle(m_wet);
    m_wetness = m_mixing.kind == TipMixing::Kind::WaterColor ? static_cast<float>(mixingAt(dab.pressure)) : 0.0f;
    const bool melts =
        m_mixing.kind == TipMixing::Kind::WaterColor || m_mixing.kind == TipMixing::Kind::Marker || m_mixing.kind == TipMixing::Kind::Blur;
    m_melt = melts && m_mixing.meltRate >= 0.0 ? static_cast<float>(m_mixing.meltRate) : m_wetness;
    if (m_mixing.depositsTowardPaint) {  // all of it at Blur Pressure and below, none at full pressure, (1 - firmness) to meltFirmPower
        const double firm = std::clamp((dab.pressure - m_mixing.blurPressure) / std::max(1.0 - m_mixing.blurPressure, 1e-6), 0.0, 1.0);
        m_melt *= static_cast<float>(std::pow(1.0 - firm, m_mixing.meltFirmPower));
    }
    recycle(m_wide);
    m_coarseWet.reset();
    m_coarseWide.reset();
    // Water Color reads its blur only to melt, and to keep detail: with neither, as its 5th Shape at full pressure, it isn't worked out.
    const bool blurUnread = m_mixing.kind == TipMixing::Kind::WaterColor && m_melt <= 0.0f && !m_mixing.wetDetail;
    if ((m_wetness > 0.0f || m_melt > 0.0f) && !blurUnread && !(m_markerCell > 0 && nothingUnder(dab))) {
        const double radii = m_mixing.wetBlurRadii * blurReachAt(dab.pressure);
        if (coarseCellFor(dab, radii) > 1) {
            m_coarseWet = coarseBlurUnder(dab, radii);
            if (m_mixing.wetDetail)
                m_coarseWide = coarseBlurUnder(dab, m_mixing.wetBlurRadii * kWideBlur);
        } else {
            m_wet = blurUnder(dab, radii);
            if (m_mixing.wetDetail)  // only the detail reads the wider blur, and it's three times as far each way
                m_wide = blurUnder(dab, m_mixing.wetBlurRadii * kWideBlur);
        }
    }
    m_wetHasPaint = m_mixing.kind == TipMixing::Kind::WaterColor && blurHasPaint();
    // Blur's strength at the dab's pressure, the same at every pixel of it.
    const double share = m_mixing.blurFollowsPressure ? std::clamp(dab.pressure, 0.0, 1.0) : 1.0;
    m_blurPressed = static_cast<float>(m_mixing.blurStrength * std::pow(share, m_mixing.blurPower));
    if (m_mixing.kind == TipMixing::Kind::Marker && !m_mixing.transparentColour)
        prepareMarkerStamps(dab);
    else
        prepareWholeStamps(dab);
    prepareOutlines(dab);
    std::vector<Laying> layings = layingsFor(dab.area, true, &dab);
    const std::int64_t pixels = static_cast<std::int64_t>(dab.area.width()) * dab.area.height();
    if (m_gridCell > 0)
        layOnGrid(dab, paint, layings);
    else if (m_markerCell > 0)
        layMarkerOnGrid(dab, paint, layings);
    else if (m_onWorkers && pixels >= (m_tip.pictureStamps ? kParallelPicturePixels : kParallelPixels))
        layOnWorkers(dab, paint, layings);
    else
        for (Laying& laying : layings)
            layInTile(dab, paint, laying);
    for (const Laying& laying : layings)
        m_changed.insert(laying.coord);
    m_dirty |= dab.area.adjusted(-m_markerCell, -m_markerCell, m_markerCell, m_markerCell);  // a Marker cell is shown whole
    shareSettledColumns(layings);
    packIdleReach(dab);
}
