// Part of DabTip.cpp, which includes it where it stands: laying on a grid: the dab's mixing at the grid's points, each pixel read between
// them.

// First the grid's points take the dab, as a pixel would, with its density counted per shape so a point by the outline holds what
// the paint inside has. Then every pixel takes its outline from the stamps and what it shows from the points around it.
void DabPainter::layOnGrid(const Dab& dab, const Premultiplied& paint, std::vector<Laying>& layings) {
    struct Band {
        Laying* laying;
        int top;
        int bottom;
    };
    std::vector<Band> bands;
    for (Laying& laying : layings) {
        const QRect area = dab.area.intersected(laying.coord.pixelRect());
        for (int top = area.top(); top <= area.bottom(); top += kRowsPerJob)
            bands.push_back({&laying, top, std::min(top + kRowsPerJob - 1, area.bottom())});
    }
    const std::int64_t pixels = static_cast<std::int64_t>(dab.area.width()) * dab.area.height();
    const auto run = [&](const auto& one) {
        if (m_onWorkers && pixels >= kParallelPixels && bands.size() > 1)
            WorkerPool::forStrokes().run(static_cast<int>(bands.size()), one);
        else
            for (int i = 0; i < static_cast<int>(bands.size()); ++i)
                one(i);
    };
    if (m_gridCell == kGridOne) {
        run([&](int i) { layPointRows(dab, paint, *bands[i].laying, bands[i].top, bands[i].bottom); });
        return;
    }
    m_wholeOutline.assign(dab.count, -1.0f);
    for (std::size_t k = 0; k < dab.count; ++k) {
        const Stamp& stamp = m_stamps[dab.first + k];
        if (stamp.edgeBlur > 0.0f)
            continue;  // a blurred outline is whole nowhere exactly: only within the paint's solid, as dabMark has it
        // A soft rim's paint is limited to its neighbours' (limitedMark), which is none only past a neighbour's own outer end: inside the
        // nearest of those ends, less the outline's ramp, the stamp's outline is whole and its paint some.
        float outer = outerOf(stamp);
        if (stamp.band > 1.0f) {
            const std::size_t i = dab.first + k;
            const std::size_t first = i >= static_cast<std::size_t>(kStampNeighbours) ? i - kStampNeighbours : 0;
            const std::size_t last = std::min(i + kStampNeighbours, m_stamps.size() - 1);
            for (std::size_t j = first; j <= last; ++j)
                outer = std::min(outer, outerOf(m_stamps[j]));
        }
        const float in = outer - std::max(stamp.rampWidth, 1.0f) - kSolidMargin;
        m_wholeOutline[k] = in > 0.0f ? in * in : -1.0f;
    }
    prepareRows(dab, kReadReachCells * static_cast<float>(m_gridCell));
    run([&](int i) { layGridRows(dab, paint, *bands[i].laying, bands[i].top, bands[i].bottom); });
    run([&](int i) { showGridRows(dab, paint, *bands[i].laying, bands[i].top, bands[i].bottom); });
}

// A grid a pixel apart: each pixel is a point, and takes the dab as layRows and layPixel lay it, the same steps in the same order, with
// what it holds kept in the grid's arrays.
void DabPainter::layPointRows(const Dab& dab, const Premultiplied& paint, Laying& laying, int top, int bottom) const {
    const QRect area = dab.area.intersected(laying.coord.pixelRect());
    std::vector<BandStamp> stamps;
    stampsOverBand(dab, QRect(area.left(), top, area.width(), bottom - top + 1), stamps);
    const bool whole = stamps.size() == dab.count && std::all_of(stamps.begin(), stamps.end(), [](const BandStamp& s) { return s.whole; });
    const bool unevened = std::all_of(stamps.begin(), stamps.end(), [](const BandStamp& s) { return s.unevened; });
    const std::optional<StampMark> wholeMark = whole && unevened ? wholeDabMark(dab) : std::nullopt;
    Reach& reach = *laying.reach;
    for (int y = top; y <= bottom; ++y) {
        const auto [left, right] = reachedOnRow(area, y);
        for (int x = left; x <= right; ++x) {
            const StampMark mark = wholeMark ? *wholeMark : dabMark(dab, x, y, &stamps);
            if (mark.shape <= 0.0f || mark.paint <= 0.0f)
                continue;
            const std::size_t index = static_cast<std::size_t>(indexIn(laying.coord, x, y));
            Channel* px = laying.tile->px.data() + index * 4;
            const bool first = reach.gridValue[index] <= 0.0f;
            if (first)
                reach.keepUnder(index, px);
            const auto show = [&] {
                writePixel(px,
                           shown(reach.underAt(index), reach.gridPaint[index], coverageOf(reach.gridValue[index], shapeOf(reach, index))));
            };
            if (m_wet || m_coarseWet) {
                const float melt = meltOf(mark);
                soften(reach, index, m_wet ? m_wet->at(x, y) : m_coarseWet->at(x, y), melt);
            }
            if (m_mixing.layOnlyAbove && onlyMoving(dab.pressure)) {
                show();
                continue;
            }
            const float laid = mark.paint * static_cast<float>(layingAt(dab.pressure));
            const float heldBefore = first ? 0.0f : reach.gridHeld[index];
            reach.gridPaint[index] =
                heldBefore <= 0.0f ? paint
                                   : lerp(reach.gridPaint[index], paint, paintShare(heldBefore, laid));  // as the grid's points take it
            reach.gridHeld[index] = heldBefore + laid;
            reach.gridValue[index] += laid;
            if (mostOf(mark.shape) > reach.shape[index])
                reach.shape[index] = mostOf(mark.shape);
            addPasses(reach, index, mark.passed);
            show();
        }
    }
}

namespace {

// The Marker grid's numbers at a row, down one side of a cell: its opacity, colour strength, what the dab laid and the stroke's paint.
struct MarkerColumn {
    float value = 0.0f, shape = 0.0f, laid = 0.0f;
    Premultiplied paint{};
};

template <typename Corner> MarkerColumn markerColumn(const Corner& top, const Corner& bottom, float fy, bool laidToo) {
    const float wa = 1.0f - fy, wb = fy;
    MarkerColumn column;
    column.value = wa * top.value + wb * bottom.value;
    column.shape = wa * top.shape + wb * bottom.shape;
    if (laidToo)
        column.laid = wa * (top.laid ? top.held - top.beforeHeld : 0.0f) + wb * (bottom.laid ? bottom.held - bottom.beforeHeld : 0.0f);
    for (std::size_t c = 0; c < 4; ++c)
        column.paint[c] = wa * top.paint[c] + wb * bottom.paint[c];
    return column;
}

// wa · a + wb · b in each lane, as markerColumn's numbers are read across.
__m256 markerAcross(__m256 wa, __m256 wb, float a, float b) {
    return _mm256_add_ps(_mm256_mul_ps(wa, _mm256_set1_ps(a)), _mm256_mul_ps(wb, _mm256_set1_ps(b)));
}

// A row of a cell kWideLanes wide with nothing under the stroke, the scalar read-back's steps in each lane: across between the two
// columns, then shown as layMarkerPixel shows a pixel, its first lane the cell's point, which comes out as that point showed itself.
void showMarkerEight(Channel* px, const MarkerColumn& left, const MarkerColumn& right, float opacityScale, bool ceilingOnly) {
    alignas(32) float fxs[kWideLanes];
    for (int i = 0; i < kWideLanes; ++i)
        fxs[i] = static_cast<float>(i) / static_cast<float>(kWideLanes);
    const __m256 wb = _mm256_load_ps(fxs), wa = _mm256_sub_ps(_mm256_set1_ps(1.0f), wb);
    const __m256 opacity = markerAcross(wa, wb, left.value, right.value), strength = markerAcross(wa, wb, left.shape, right.shape);
    __m256 mine[4];
    for (std::size_t c = 0; c < 4; ++c)
        mine[c] = markerAcross(wa, wb, left.paint[c], right.paint[c]);
    const __m256 zero = _mm256_setzero_ps(), one = _mm256_set1_ps(1.0f), least = _mm256_set1_ps(1e-6f);
    const __m256 underAlpha = zero;  // nothing under
    const __m256 laidAlpha = _mm256_mul_ps(opacity, _mm256_set1_ps(opacityScale));
    const __m256 alpha = ceilingOnly ? _mm256_max_ps(underAlpha, laidAlpha)
                                     : _mm256_add_ps(underAlpha, _mm256_mul_ps(_mm256_mul_ps(_mm256_sub_ps(one, underAlpha), opacity),
                                                                               _mm256_set1_ps(opacityScale)));
    // With nothing under, the colour under is the stroke's own, so the strength between them changes nothing: the stroke's colour, mine
    // over its alpha, kept from 1e-6 as layMarkerPixel keeps it, by one division as the scalar read-back does it there.
    (void)strength;
    const __m256 over = _mm256_div_ps(one, _mm256_max_ps(mine[3], least));
    __m256 o[4];
    for (std::size_t c = 0; c < 3; ++c)
        o[c] = _mm256_mul_ps(_mm256_mul_ps(mine[c], over), alpha);
    o[3] = alpha;
    // As writePixel: each channel to the nearest step, no colour over its alpha; packed four pixels at a time as showEight packs them.
    const __m256 scale = _mm256_set1_ps(static_cast<float>(kChannelOne)), half = _mm256_set1_ps(0.5f);
    __m256 t[4];
    for (int c = 0; c < 4; ++c)
        t[c] = _mm256_add_ps(_mm256_mul_ps(_mm256_min_ps(_mm256_max_ps(o[c], zero), one), scale), half);
    for (int c = 0; c < 3; ++c)
        t[c] = _mm256_min_ps(t[c], t[3]);
    constexpr int kHalfRange = 32768;
    const __m128i bias = _mm_set1_epi32(kHalfRange), flip = _mm_set1_epi16(static_cast<short>(kHalfRange));
    const auto pack = [&](__m128 p, __m128 q) {
        const __m128i ip = _mm_sub_epi32(_mm_cvttps_epi32(p), bias), iq = _mm_sub_epi32(_mm_cvttps_epi32(q), bias);
        return _mm_xor_si128(_mm_packs_epi32(ip, iq), flip);
    };
    for (int h = 0; h < 2; ++h) {
        __m128 q[4];
        for (int c = 0; c < 4; ++c)
            q[c] = h == 0 ? _mm256_castps256_ps128(t[c]) : _mm256_extractf128_ps(t[c], 1);
        _MM_TRANSPOSE4_PS(q[0], q[1], q[2], q[3]);
        _mm_storeu_si128(reinterpret_cast<__m128i*>(px + h * 16), pack(q[0], q[1]));
        _mm_storeu_si128(reinterpret_cast<__m128i*>(px + h * 16 + 8), pack(q[2], q[3]));
    }
}

}

// The Marker on a grid. First its points, each a pixel laid as the per-pixel Marker lays one, then its cells: each pixel between points is read
// between the four around it, or in a cell they don't agree on, worked out as the per-pixel Marker's pixels are.
void DabPainter::layMarkerOnGrid(const Dab& dab, const Premultiplied& paint, std::vector<Laying>& layings) {
    struct Band {
        Laying* laying;
        int top;
        int bottom;
    };
    // Bands of whole rows of cells, so a cell is decided in one band: kRowsPerJob is a whole number of cells.
    std::vector<Band> bands;
    for (Laying& laying : layings) {
        const QRect tile = laying.coord.pixelRect();
        const QRect area = dab.area.adjusted(-m_markerCell, -m_markerCell, m_markerCell, m_markerCell).intersected(tile);
        for (int top = tile.top() + (area.top() - tile.top()) / kRowsPerJob * kRowsPerJob; top <= area.bottom(); top += kRowsPerJob)
            bands.push_back({&laying, top, std::min(top + kRowsPerJob - 1, tile.bottom())});
    }
    const std::int64_t pixels = static_cast<std::int64_t>(dab.area.width()) * dab.area.height();
    const auto run = [&](const auto& one) {
        if (m_onWorkers && pixels >= kParallelPixels && bands.size() > 1)
            WorkerPool::forStrokes().run(static_cast<int>(bands.size()), one);
        else
            for (int i = 0; i < static_cast<int>(bands.size()); ++i)
                one(i);
    };
    m_markerRowsTop = dab.area.top();
    m_markerRows.resize(static_cast<std::size_t>(dab.area.height()));
    for (int y = dab.area.top(); y <= dab.area.bottom(); ++y)
        m_markerRows[static_cast<std::size_t>(y - m_markerRowsTop)] = reachedOnRow(dab.area, y);
    run([&](int i) { layMarkerPoints(dab, paint, *bands[i].laying, bands[i].top, bands[i].bottom); });
    run([&](int i) { showMarkerCells(dab, paint, *bands[i].laying, bands[i].top, bands[i].bottom); });
}

// reachedOnRow of `area`, part of the dab's: the dab's row, cut to it.
std::pair<int, int> DabPainter::markerRow(const QRect& area, int y) const {
    const auto [left, right] = m_markerRows[static_cast<std::size_t>(y - m_markerRowsTop)];
    return {std::max(area.left(), left), std::min(area.right(), right)};
}

// The points of rows `top` to `bottom`, as layRows lays a pixel, keeping what each was before.
void DabPainter::layMarkerPoints(const Dab& dab, const Premultiplied& paint, Laying& laying, int top, int bottom) const {
    const int cell = m_markerCell, across = Tile::kSize / cell;
    const QRect tile = laying.coord.pixelRect();
    const QRect area = dab.area.intersected(tile);
    top = std::max(top, area.top());
    bottom = std::min(bottom, area.bottom());
    if (top > bottom)
        return;
    std::vector<BandStamp> stamps;
    stampsOverBand(dab, QRect(area.left(), top, area.width(), bottom - top + 1), stamps);
    const bool whole = stamps.size() == dab.count && std::all_of(stamps.begin(), stamps.end(), [](const BandStamp& s) { return s.whole; });
    Reach& reach = *laying.reach;
    MarkerClimb climb;
    const int middle = cell / 2;
    for (int y = top; y <= bottom; ++y) {
        if ((y - tile.top()) % cell == middle) {  // the cells' middles
            const auto [left, right] = markerRow(area, y);
            for (int x = tile.left() + floorDiv(left - tile.left() - middle + cell - 1, cell) * cell + middle; x <= right; x += cell) {
                const std::size_t c = static_cast<std::size_t>((y - tile.top()) / cell * across + (x - tile.left()) / cell);
                layMarkerAt(dab, paint, laying, static_cast<std::size_t>(indexIn(laying.coord, x, y)),
                            MarkerRecord{reach.middleValue[c], reach.middleShape[c], reach.middleHeld[c], reach.middlePaint[c]}, x, y,
                            whole ? &climb : nullptr, stamps);
            }
            continue;
        }
        if ((y - tile.top()) % cell != 0)
            continue;
        const auto [left, right] = markerRow(area, y);
        for (int x = tile.left() + floorDiv(left - tile.left() + cell - 1, cell) * cell; x <= right; x += cell) {
            const std::size_t index = static_cast<std::size_t>(indexIn(laying.coord, x, y));
            const std::size_t g = static_cast<std::size_t>((y - tile.top()) / cell * across + (x - tile.left()) / cell);
            reach.beforeValue[g] = reach.pointValue[g];
            reach.beforeShape[g] = reach.pointShape[g];
            reach.beforeHeld[g] = reach.pointHeld[g];
            reach.beforePaint[g] = reach.pointPaint[g];
            reach.markerLaidDab[g] = m_dabNumber;
            layMarkerAt(dab, paint, laying, index,
                        MarkerRecord{reach.pointValue[g], reach.pointShape[g], reach.pointHeld[g], reach.pointPaint[g]}, x, y,
                        whole ? &climb : nullptr, stamps);
        }
    }
}

// The cells of rows `top` to `bottom`, whole cells of the tile. A cell is read between its points where they were all reached and agree
// within kMarkerSmoothRange; otherwise its pixels take what its points were before the dab, read between them, and are worked out one by
// one from then, until they're within that of the reading again.
void DabPainter::showMarkerCells(const Dab& dab, const Premultiplied& paint, Laying& laying, int top, int bottom) const {
    const int cell = m_markerCell, across = Tile::kSize / cell;
    const QRect tile = laying.coord.pixelRect();
    const QRect area = dab.area.intersected(tile);
    const QRect near = dab.area.adjusted(-cell, -cell, cell, cell).intersected(tile);
    Reach& reach = *laying.reach;
    const std::array<const Reach*, 4> grids{&reach, reachAt({laying.coord.x + 1, laying.coord.y}),
                                            reachAt({laying.coord.x, laying.coord.y + 1}),
                                            reachAt({laying.coord.x + 1, laying.coord.y + 1})};
    struct Corner {
        bool reached = false, laid = false, reachedBefore = false;
        float value = 0.0f, shape = 0.0f, held = 0.0f;
        Premultiplied paint{};
        float beforeValue = 0.0f, beforeShape = 0.0f, beforeHeld = 0.0f;
        Premultiplied beforePaint{};
    };
    const auto cornerAt = [&](int gx, int gy) {
        Corner corner;
        const Reach* from = grids[(gx == across ? 1 : 0) + (gy == across ? 2 : 0)];
        if (!from)
            return corner;
        const std::size_t g = static_cast<std::size_t>((gy == across ? 0 : gy) * across + (gx == across ? 0 : gx));
        corner.value = from->pointValue[g];
        corner.shape = from->pointShape[g];
        corner.held = from->pointHeld[g];
        corner.paint = from->pointPaint[g];
        corner.reached = corner.held > 0.0f;
        corner.laid = from->markerLaidDab[g] == m_dabNumber;
        corner.beforeValue = corner.laid ? from->beforeValue[g] : corner.value;
        corner.beforeShape = corner.laid ? from->beforeShape[g] : corner.shape;
        corner.beforeHeld = corner.laid ? from->beforeHeld[g] : corner.held;
        corner.beforePaint = corner.laid ? from->beforePaint[g] : corner.paint;
        corner.reachedBefore = corner.beforeHeld > 0.0f;
        return corner;
    };
    const auto within = [](float a, float b) { return std::abs(a - b) <= kMarkerSmoothRange; };
    const bool eightAtOnce = !s_onePixelAtATime.load(std::memory_order_relaxed) && cell == kWideLanes;
    // Which stamps reach the band's rows of the dab, for the pixels worked out one by one, as layRows has them.
    const int dabTop = std::max(top, area.top()), dabBottom = std::min(bottom, area.bottom());
    std::vector<BandStamp> stamps;
    bool whole = false;
    std::vector<std::pair<int, int>> spans(static_cast<std::size_t>(bottom - top + 1), {0, -1});
    if (dabTop <= dabBottom) {
        stampsOverBand(dab, QRect(area.left(), dabTop, area.width(), dabBottom - dabTop + 1), stamps);
        whole = stamps.size() == dab.count && std::all_of(stamps.begin(), stamps.end(), [](const BandStamp& s) { return s.whole; });
        for (int y = dabTop; y <= dabBottom; ++y)
            spans[static_cast<std::size_t>(y - top)] = markerRow(area, y);
    }
    MarkerClimb climb;
    // The points along the top and the bottom of a row of cells, each worked out once for the cells either side of it.
    const int firstColumn = (near.left() - tile.left()) / cell, lastColumn = (near.right() - tile.left()) / cell;
    std::vector<Corner> above(static_cast<std::size_t>(lastColumn - firstColumn + 2)), below(above.size());
    const auto pointRow = [&](std::vector<Corner>& row, int gy) {
        for (int gx = firstColumn; gx <= lastColumn + 1; ++gx)
            row[static_cast<std::size_t>(gx - firstColumn)] = cornerAt(gx, gy);
    };
    const int firstRow = (std::max(top, near.top()) - tile.top()) / cell;
    pointRow(below, firstRow);
    for (int cy = firstRow; cy * cell + tile.top() <= std::min(bottom, near.bottom()); ++cy) {
        const int y0 = tile.top() + cy * cell;
        std::swap(above, below);
        pointRow(below, cy + 1);
        for (int cx = firstColumn; cx <= lastColumn; ++cx) {
            const int x0 = tile.left() + cx * cell;
            const std::size_t c = static_cast<std::size_t>(cy * across + cx);
            const std::size_t k = static_cast<std::size_t>(cx - firstColumn);
            const bool anyLaid = above[k].laid || above[k + 1].laid || below[k].laid || below[k + 1].laid;
            bool touched = false;
            for (int y = y0; y < y0 + cell && !touched; ++y) {
                const auto [left, right] = spans[static_cast<std::size_t>(y - top)];
                touched = left <= x0 + cell - 1 && right >= x0;
            }
            std::unique_ptr<Reach::FineCell>& fine = reach.fineCells[c];
            if (!fine && !anyLaid && !touched)
                continue;
            const bool laidToo = (m_wet || m_coarseWet) && reach.under.owned();  // what the dab laid, read only to melt what's under
            const std::array<const Corner*, 4> corners{&above[k], &above[k + 1], &below[k], &below[k + 1]};
            const auto agree = [&](auto valueOf) {
                for (std::size_t i = 1; i < 4; ++i)
                    if (!within(valueOf(*corners[i]), valueOf(*corners[0])))
                        return false;
                return true;
            };
            const bool allReached = std::all_of(corners.begin(), corners.end(), [](const Corner* corner) { return corner->reached; });
            bool smooth = allReached;
            if (smooth) {
                // The middle pixel, worked out exactly, against the reading there. The colour's strength climbs too steeply at a soft rim
                // for its middle to tell, so where something is under for it to show against, its points must agree.
                constexpr float kHalf = 0.5f;
                const MarkerColumn left = markerColumn(*corners[0], *corners[2], kHalf, laidToo),
                                   right = markerColumn(*corners[1], *corners[3], kHalf, laidToo);
                smooth = reach.middleHeld[c] > 0.0f && within(reach.middleValue[c], kHalf * left.value + kHalf * right.value) &&
                         (reach.under.owned() ? agree([](const Corner& corner) { return corner.shape; })
                                              : within(reach.middleShape[c], kHalf * left.shape + kHalf * right.shape));
                for (std::size_t ch = 0; ch < 4 && smooth; ++ch)
                    smooth = within(reach.middlePaint[c][ch], kHalf * left.paint[ch] + kHalf * right.paint[ch]);
            }
            const auto weightsAt = [&](int x, int y) {
                const float fx = static_cast<float>(x - x0) / static_cast<float>(cell),
                            fy = static_cast<float>(y - y0) / static_cast<float>(cell);
                return std::array<float, 4>{(1.0f - fx) * (1.0f - fy), fx * (1.0f - fy), (1.0f - fx) * fy, fx * fy};
            };
            const auto isPoint = [&](int x, int y) { return x == x0 && y == y0; };
            const auto isMiddle = [&](int x, int y) { return x == x0 + cell / 2 && y == y0 + cell / 2; };
            const auto inCell = [&](int x, int y) { return static_cast<std::size_t>((y - y0) * cell + (x - x0)); };
            if (!fine && !smooth) {
                // Its pixels take up what the points were before this dab, read between those reached then.
                fine = std::make_unique<Reach::FineCell>();
                const std::size_t pixels = static_cast<std::size_t>(cell) * static_cast<std::size_t>(cell);
                fine->value.resize(pixels);
                fine->shape.resize(pixels);
                fine->held.resize(pixels);
                fine->paint.resize(pixels);
                for (int y = y0; y < y0 + cell; ++y)
                    for (int x = x0; x < x0 + cell; ++x) {
                        if (isPoint(x, y) || isMiddle(x, y))
                            continue;
                        const std::array<float, 4> w = weightsAt(x, y);
                        float value = 0.0f, held = 0.0f, shape = 0.0f, counted = 0.0f;
                        Premultiplied mine{};
                        for (std::size_t i = 0; i < 4; ++i) {
                            if (!corners[i]->reachedBefore)
                                continue;
                            value += w[i] * corners[i]->beforeValue;
                            held += w[i] * corners[i]->beforeHeld;
                            shape += w[i] * corners[i]->beforeShape;
                            counted += w[i];
                            for (std::size_t ch = 0; ch < 4; ++ch)
                                mine[ch] += w[i] * corners[i]->beforePaint[ch];
                        }
                        const std::size_t i = inCell(x, y);
                        fine->value[i] = value;
                        fine->held[i] = held;
                        fine->shape[i] = counted > 0.0f ? shape / counted : 0.0f;
                        for (std::size_t ch = 0; ch < 4; ++ch)
                            mine[ch] = counted > 0.0f ? mine[ch] / counted : 0.0f;
                        fine->paint[i] = mine;
                    }
            }
            if (fine) {
                for (int y = y0; y < y0 + cell; ++y) {
                    const auto [left, right] = spans[static_cast<std::size_t>(y - top)];
                    for (int x = std::max(x0, left); x <= std::min(x0 + cell - 1, right); ++x) {
                        if (isPoint(x, y) || isMiddle(x, y))
                            continue;
                        const std::size_t i = inCell(x, y);
                        layMarkerAt(dab, paint, laying, static_cast<std::size_t>(indexIn(laying.coord, x, y)),
                                    MarkerRecord{fine->value[i], fine->shape[i], fine->held[i], fine->paint[i]}, x, y,
                                    whole ? &climb : nullptr, stamps);
                    }
                }
                if (!allReached)
                    continue;
                // Back to being read between the points once every pixel is within kMarkerSmoothRange of that reading.
                bool back = true;
                for (int y = y0; y < y0 + cell && back; ++y)
                    for (int x = x0; x < x0 + cell && back; ++x) {
                        if (isPoint(x, y) || isMiddle(x, y))
                            continue;
                        const std::array<float, 4> w = weightsAt(x, y);
                        const std::size_t i = inCell(x, y);
                        float value = 0.0f, shape = 0.0f;
                        Premultiplied mine{};
                        for (std::size_t corner = 0; corner < 4; ++corner) {
                            value += w[corner] * corners[corner]->value;
                            shape += w[corner] * corners[corner]->shape;
                            for (std::size_t ch = 0; ch < 4; ++ch)
                                mine[ch] += w[corner] * corners[corner]->paint[ch];
                        }
                        back = within(fine->value[i], value) && within(fine->shape[i], shape);
                        for (std::size_t ch = 0; ch < 4 && back; ++ch)
                            back = within(fine->paint[i][ch], mine[ch]);
                    }
                if (back)
                    fine.reset();
                continue;
            }
            if (!anyLaid)
                continue;
            // Read between the points: down each side of the cell, then across, as layMarkerPixel shows a pixel, with what's under it
            // melted by what the dab laid there. Eight pixels at once where the cell is eight wide and nothing is under the stroke.
            for (int y = y0; y < y0 + cell; ++y) {
                const float fy = static_cast<float>(y - y0) / static_cast<float>(cell);
                const MarkerColumn left = markerColumn(*corners[0], *corners[2], fy, laidToo),
                                   right = markerColumn(*corners[1], *corners[3], fy, laidToo);
                const std::size_t rowIndex = static_cast<std::size_t>(indexIn(laying.coord, x0, y));
                if (eightAtOnce && !reach.under.owned()) {
                    showMarkerEight(laying.tile->px.data() + rowIndex * 4, left, right, m_paint.opacity, m_mixing.ceilingOnly);
                    continue;
                }
                for (int x = x0; x < x0 + cell; ++x) {
                    if (isPoint(x, y) && reach.under.owned())
                        continue;
                    const float fx = static_cast<float>(x - x0) / static_cast<float>(cell);
                    const float wa = 1.0f - fx, wb = fx;
                    const float opacity = wa * left.value + wb * right.value, strength = wa * left.shape + wb * right.shape;
                    const float laid = wa * left.laid + wb * right.laid;
                    Premultiplied mine;
                    for (std::size_t ch = 0; ch < 4; ++ch)
                        mine[ch] = wa * left.paint[ch] + wb * right.paint[ch];
                    const std::size_t index = rowIndex + static_cast<std::size_t>(x - x0);
                    // Only in the dab's area, where alone it lays anything and its blur is worked out; the middle melted as it was laid.
                    if ((m_wet || m_coarseWet) && laid > 0.0f && area.contains(x, y) && !isMiddle(x, y) &&
                        !(m_mixing.meltColourOnly && reach.under[index] == Premultiplied{}))
                        soften(reach, index, m_wet ? m_wet->at(x, y) : m_coarseWet->at(x, y), std::min(-std::expm1(-laid) * m_melt, 1.0f));
                    const Premultiplied& under = reach.under[index];
                    const float alpha = m_mixing.ceilingOnly ? std::max(under[3], opacity * m_paint.opacity)
                                                             : under[3] + (1.0f - under[3]) * opacity * m_paint.opacity;
                    Premultiplied out{0.0f, 0.0f, 0.0f, alpha};
                    if (!reach.under.owned()) {
                        // Nothing under: the stroke's own colour, as showMarkerEight works it out, the point's too.
                        const float over = 1.0f / std::max(mine[3], 1e-6f);
                        for (std::size_t ch = 0; ch < 3; ++ch)
                            out[ch] = mine[ch] * over * alpha;
                    } else {
                        for (std::size_t ch = 0; ch < 3; ++ch) {
                            const float underColour = under[3] > 1e-6f ? under[ch] / under[3] : mine[ch] / std::max(mine[3], 1e-6f);
                            const float myColour = mine[3] > 1e-6f ? mine[ch] / mine[3] : underColour;
                            out[ch] = (strength > 0.0f ? myColour : underColour) * alpha;
                        }
                    }
                    writePixel(laying.tile->px.data() + index * 4, out);
                }
            }
        }
    }
}

void DabPainter::layGridRows(const Dab& dab, const Premultiplied& paint, Laying& laying, int top, int bottom) const {
    const int cell = m_gridCell, across = Tile::kSize / cell;
    const QRect tile = laying.coord.pixelRect();
    const QRect area = dab.area.intersected(tile);
    std::vector<BandStamp> stamps;
    stampsOverBand(dab, QRect(area.left(), top, area.width(), bottom - top + 1), stamps);
    Reach& reach = *laying.reach;
    // A skirted band's points take their marks eight at a time along the row, as its marked pixels do.
    const bool marksEight = !s_onePixelAtATime.load(std::memory_order_relaxed) && marksInLanes(stamps);
    std::array<StampMark, kWideLanes> eight;
    const std::vector<char> cutWhole =
        marksEight ? cutsWholeOver(dab, stamps, QRect(area.left(), top, area.width(), bottom - top + 1)) : std::vector<char>{};
    for (int y = top; y <= bottom; ++y) {
        if ((y - tile.top()) % cell != 0)
            continue;
        const RowReach& row = m_rows[static_cast<std::size_t>(y - m_rowsTop)];
        if (row.reachLeft > row.reachRight)
            continue;  // as reachedOnRow's empty part
        const int left = std::max(area.left(), static_cast<int>(std::floor(row.reachLeft)) - 1);
        const int right = std::min(area.right(), static_cast<int>(std::ceil(row.reachRight)) + 1);
        const int laysFrom = row.laysFrom, laysTo = row.laysTo;
        const int first = tile.left() + floorDiv(left - tile.left() + cell - 1, cell) * cell;
        int eightFrom = right + 1;  // the first of the eight points whose marks are in `eight`, past the row while there are none
        for (int x = first; x <= right; x += cell) {
            if (x < laysFrom || x > laysTo)
                continue;  // its mark is none, as below
            if (marksEight && x >= eightFrom + kWideLanes * cell)
                eightFrom = right + 1;
            if (marksEight && eightFrom > right && x + (kWideLanes - 1) * cell <= std::min(right, laysTo)) {
                markEight(dab, x, y, stamps, eight.data(), cell, cutWhole.data());
                eightFrom = x;
            }
            const bool inEight = marksEight && x >= eightFrom && x < eightFrom + kWideLanes * cell;
            const StampMark mark = inEight ? eight[static_cast<std::size_t>((x - eightFrom) / cell)]
                                           : dabMark(dab, x, y, &stamps, false);  // a point keeps no passes
            if (mark.shape <= 0.0f || mark.paint <= 0.0f)
                continue;
            const std::size_t g = static_cast<std::size_t>((y - tile.top()) / cell * across + (x - tile.left()) / cell);
            reach.gridDab[g] = m_dabNumber;
            if (m_wet || m_coarseWet) {
                reach.gridMelt[g] = meltOf(mark);
                if (m_mixing.meltsOwnPaint && reach.gridMelt[g] > 0.0f)  // its own paint, kept at the points here, melts with what's under
                    reach.gridPaint[g] = lerp(reach.gridPaint[g], m_wet ? m_wet->at(x, y) : m_coarseWet->at(x, y), reach.gridMelt[g]);
            }
            if (m_mixing.layOnlyAbove && onlyMoving(dab.pressure)) {
                reach.gridLaid[g] = 0.0f;
                continue;
            }
            const float laid =
                mark.paint *
                static_cast<float>(layingAt(dab.pressure));  // as a pixel's own record has it: density times the outline's share
            const float held = reach.gridHeld[g];
            reach.gridPaint[g] = held <= 0.0f ? paint : lerp(reach.gridPaint[g], paint, paintShare(held, laid));
            reach.gridHeld[g] = held + laid;
            reach.gridValue[g] += laid;
            reach.gridLaid[g] = laid;
            reach.gridWhole[g] = reach.gridWhole[g] || mark.shape >= 1.0f;
            reach.gridCover[g] = coverageOf(reach.gridValue[g], 1.0f) / m_paint.opacity;  // read only where the point's outline is whole
        }
    }
}

void DabPainter::showGridRows(const Dab& dab, const Premultiplied& paint, Laying& laying, int top, int bottom) const {
    const int cell = m_gridCell, across = Tile::kSize / cell;
    const QRect tile = laying.coord.pixelRect();
    const QRect area = dab.area.intersected(tile);
    std::vector<BandStamp> stamps;
    stampsOverBand(dab, QRect(area.left(), top, area.width(), bottom - top + 1), stamps);
    Reach& reach = *laying.reach;
    const bool wet = m_wet || m_coarseWet;  // Water Color, melting what's under
    // The points of this tile and of its neighbours to the right and below, as a cell at the tile's edge reads theirs.
    const std::array<const Reach*, 4> grids{&reach, reachAt({laying.coord.x + 1, laying.coord.y}),
                                            reachAt({laying.coord.x, laying.coord.y + 1}),
                                            reachAt({laying.coord.x + 1, laying.coord.y + 1})};
    // A GridColumn's sums are not yet divided by how much counted.
    std::vector<GridColumn> columns(static_cast<std::size_t>(across) + 1);
    const auto addPoint = [&](GridColumn& column, int gx, int gy, float w) {
        if (w <= 0.0f)
            return;
        const Reach* from = grids[(gx == across ? 1 : 0) + (gy == across ? 2 : 0)];
        if (!from)
            return;
        const std::size_t g = static_cast<std::size_t>((gy == across ? 0 : gy) * across + (gx == across ? 0 : gx));
        if (from->gridHeld[g] <= 0.0f)
            return;
        column.weight += w;
        column.whole = column.whole && from->gridWhole[g];
        column.cover += w * from->gridCover[g];
        column.value += w * from->gridValue[g];
        column.laid += from->gridDab[g] == m_dabNumber ? w * from->gridLaid[g] : 0.0f;
        if (wet)
            column.melt += from->gridDab[g] == m_dabNumber ? w * from->gridMelt[g] : 0.0f;
        for (std::size_t c = 0; c < 4; ++c)
            column.kept[c] += w * from->gridPaint[g][c];
    };
    const int cellShift = std::countr_zero(static_cast<unsigned>(cell));  // the cell is 2 or 4
    const float perCell = 1.0f / static_cast<float>(cell);
    std::vector<char> sharp;
    // The outline, per pixel, from the stamps: as dabMark's shape and passes, without their paint. Once a pixel a dab, before what it shows.
    // Notes how much of the pixel this dab's outline covers (dabShape), and raises the pixel's own outline when the dab lays paint: Water
    // Color below Blur Pressure only melts, as layPixel has it. Says whether the dab covers it only in part, where density is worked out per
    // pixel, as at a sharp edge.
    const bool laysPaint = !(m_mixing.layOnlyAbove && onlyMoving(dab.pressure));
    std::vector<float> dabShape;
    int dabShapeLeft = 0;
    // The dab's outline at a place from the stamps over it: the most of their shapes, and the passes they add.
    const auto markOver = [&](const std::vector<BandStamp>& over, int x, int y, float& most, float& passes) {
        most = 0.0f;
        passes = 0.0f;
        for (const BandStamp& band : over) {
            const std::size_t i = dab.first + band.k;
            const WholeStamp& whole = m_wholeStamps[band.k];
            const float dx = whole.frame.dx(x), dy = whole.frame.dy(y);
            const float distanceSquared = dx * dx + dy * dy;
            // Whole there. With the outline along the path its passes go on adding up, from every stamp.
            const bool wholeHere =
                (band.whole && (m_stamps[i].chordRadius <= 0.0f || m_stamps[i].evenFrom > 0.0f))  // evening moves paint, not the outline
                || (distanceSquared < whole.outsideSquared &&
                    (distanceSquared < whole.solidSquared || distanceSquared < m_wholeOutline[band.k]));
            if (wholeHere) {
                most = 1.0f;
                if (!m_mixing.outlineAlongPath)
                    break;
                passes += passOf(m_stamps[i], whole, std::sqrt(distanceSquared), 1.0f);
                if (passes >= kWholeAlongPasses)
                    break;  // whole already, whatever the rest add
                continue;
            }
            if (distanceSquared >= whole.outsideSquared)
                continue;
            const float distance = std::sqrt(distanceSquared);
            // limitedMark, as dabMark asks: a soft stamp its neighbours limit to no paint lays no outline either.
            const StampMark mark = limitedMark(m_stamps, i, m_tip.edge, distance, headingCosine(m_stamps[i], dx, dy, distance));
            if (mark.shape <= 0.0f || mark.paint <= 0.0f)
                continue;
            most = std::max(most, mark.shape);
            if (m_mixing.passLength > 0.0 || m_mixing.outlineAlongPath)
                passes += passOf(m_stamps[i], whole, distance, mark.shape);
        }
        if (m_tip.skirtNeighbours > 0 && most < 1.0f)
            skirtAt(dab, x, y, &over, most, true);  // a skirt shows where it lays, inside the outlines around its stamp
    };
    // Where every stamp over the band has an outline ramping over kSmoothRimCells cells or more, the outline is worked out at the grid's
    // points, the tile's and the next tiles' first column and row, and read between them. Such a ramp covers its pixels only in part,
    // smoothly, so their density is read between the points too.
    const int pointsTop = (top - tile.top()) / cell, side = across + 1;
    bool smoothOutline = !stamps.empty();
    for (const BandStamp& band : stamps) {
        const Stamp& stamp = m_stamps[dab.first + band.k];
        const float ramp = stamp.edgeBlur > 0.0f ? stamp.edgeBlur : stamp.rampWidth;
        if ((stamp.chordRadius > 0.0f && stamp.evenFrom <= 0.0f) || ramp < kSmoothRimCells * static_cast<float>(cell)) {
            smoothOutline = false;
            break;
        }
    }
    std::vector<float> pointMost, pointPasses;
    if (smoothOutline) {
        std::vector<BandStamp> overPoints;  // the stamps reaching the band and the points past it, below and to the right
        stampsOverBand(dab, QRect(area.left(), top, area.width() + cell, bottom - top + 1 + cell), overPoints);
        const int rows = (bottom - tile.top()) / cell - pointsTop + 2;
        pointMost.resize(static_cast<std::size_t>(rows * side));
        pointPasses.resize(pointMost.size());
        for (int j = 0; j < rows; ++j)
            for (int i = 0; i < side; ++i) {
                const std::size_t at = static_cast<std::size_t>(j * side + i);
                markOver(overPoints, tile.left() + i * cell, tile.top() + (pointsTop + j) * cell, pointMost[at], pointPasses[at]);
            }
    }
    // With a skirt, the part of the row being shown where the dab lays anything: past it a pixel's outline and mark from the dab are none.
    int laysFrom = 0, laysTo = -1;
    const bool skirted = m_tip.skirtNeighbours > 0;
    const auto laysAt = [&](int x) { return !skirted || (x >= laysFrom && x <= laysTo); };
    // Where the band lies inside one of a skirted stamp's skirt and wholly inside the outline of a stamp around it, every pixel's outline
    // comes out whole from markOver, the skirt raising it there: so it's set whole without asking pixel by pixel.
    const bool raisedWhole = skirted && laysPaint && skirtRaisesWhole(dab, stamps, QRect(area.left(), top, area.width(), bottom - top + 1));
    const auto outlineAt = [&](int x, int y, std::size_t index) {
        if (reach.shape[index] >= 1.0f)
            return false;
        if (!laysAt(x)) {  // markOver would find nothing, and nothing would change
            dabShape[static_cast<std::size_t>(x - dabShapeLeft)] = 0.0f;
            return false;
        }
        if (raisedWhole) {  // as markOver would have it: whole, which also leaves its passes unadded and the pixel unmarked
            dabShape[static_cast<std::size_t>(x - dabShapeLeft)] = 1.0f;
            reach.shape[index] = 1.0f;
            return false;
        }
        float most = 0.0f, passes = 0.0f;
        if (smoothOutline) {
            const int gx = (x - tile.left()) / cell, gy = (y - tile.top()) / cell;
            const float fx = static_cast<float>(x - tile.left() - gx * cell) / static_cast<float>(cell);
            const float fy = static_cast<float>(y - tile.top() - gy * cell) / static_cast<float>(cell);
            const std::size_t at = static_cast<std::size_t>((gy - pointsTop) * side + gx);
            const auto between = [&](const std::vector<float>& v) {
                return (1.0f - fy) * ((1.0f - fx) * v[at] + fx * v[at + 1]) + fy * ((1.0f - fx) * v[at + side] + fx * v[at + side + 1]);
            };
            most = between(pointMost);
            passes = between(pointPasses);
        } else {
            markOver(stamps, x, y, most, passes);
        }
        dabShape[static_cast<std::size_t>(x - dabShapeLeft)] = most;
        if (!laysPaint)
            return false;
        if (mostOf(most) > reach.shape[index])
            reach.shape[index] = mostOf(most);
        if (m_mixing.outlineAlongPath || most < 1.0f)
            addPasses(reach, index, passes);
        return !smoothOutline && most > 0.0f && most < 1.0f;
    };
    // outlineAt for four pixels from x, a cell's start, where the outline is read between the points and adds up along the path: the same
    // steps lane by lane. Pixels already whole keep everything as it is.
    const bool fourOutlines = smoothOutline && laysPaint && m_mixing.outlineAlongPath;
    const auto outlineFour = [&](int x, int y, std::size_t index) {
        const __m128 shapeNow = _mm_loadu_ps(reach.shape.data() + index);
        const __m128 open = _mm_cmplt_ps(shapeNow, _mm_set1_ps(1.0f));
        const int openLanes = _mm_movemask_ps(open);
        if (openLanes == 0)
            return;
        const int gy = (y - tile.top()) / cell;
        const float fy = static_cast<float>(y - tile.top() - gy * cell) / static_cast<float>(cell);
        alignas(16) float fxs[kLanes], c00[kLanes], c01[kLanes], c10[kLanes], c11[kLanes], p00[kLanes], p01[kLanes], p10[kLanes],
            p11[kLanes];
        for (int i = 0; i < kLanes; ++i) {
            const int gx = (x + i - tile.left()) / cell;
            fxs[i] = static_cast<float>(x + i - tile.left() - gx * cell) / static_cast<float>(cell);
            const std::size_t at = static_cast<std::size_t>((gy - pointsTop) * side + gx);
            c00[i] = pointMost[at], c01[i] = pointMost[at + 1], c10[i] = pointMost[at + side], c11[i] = pointMost[at + side + 1];
            p00[i] = pointPasses[at], p01[i] = pointPasses[at + 1], p10[i] = pointPasses[at + side], p11[i] = pointPasses[at + side + 1];
        }
        const __m128 fx = _mm_load_ps(fxs), ux = _mm_sub_ps(_mm_set1_ps(1.0f), fx);
        const __m128 vfy = _mm_set1_ps(fy), uy = _mm_set1_ps(1.0f - fy);
        const auto between = [&](const float* a, const float* b, const float* cc, const float* d) {
            const __m128 above = _mm_add_ps(_mm_mul_ps(ux, _mm_load_ps(a)), _mm_mul_ps(fx, _mm_load_ps(b)));
            const __m128 down = _mm_add_ps(_mm_mul_ps(ux, _mm_load_ps(cc)), _mm_mul_ps(fx, _mm_load_ps(d)));
            return _mm_add_ps(_mm_mul_ps(uy, above), _mm_mul_ps(vfy, down));
        };
        alignas(16) float most[kLanes];
        _mm_store_ps(most, between(c00, c01, c10, c11));
        const __m128 passed = _mm_add_ps(_mm_loadu_ps(reach.passed.data() + index), between(p00, p01, p10, p11));
        const __m128 whole = _mm_cmpge_ps(passed, _mm_set1_ps(kWholeAlongPasses));
        _mm_storeu_ps(reach.passed.data() + index,
                      _mm_or_ps(_mm_and_ps(open, passed), _mm_andnot_ps(open, _mm_loadu_ps(reach.passed.data() + index))));
        const __m128 nowWhole = _mm_and_ps(open, whole);
        _mm_storeu_ps(reach.shape.data() + index, _mm_or_ps(_mm_and_ps(nowWhole, _mm_set1_ps(1.0f)), _mm_andnot_ps(nowWhole, shapeNow)));
        for (int i = 0; i < kLanes; ++i)
            if (openLanes & (1 << i))
                dabShape[static_cast<std::size_t>(x + i - dabShapeLeft)] = most[i];
    };
    const bool scalarOnly = s_onePixelAtATime.load(std::memory_order_relaxed);
    const bool fourAtOnce = !scalarOnly && m_paint.mode != LayMode::KeepAlpha && reach.keepsUnderAsPixels;  // the Brush: under is the tile
    const bool eightAtOnce = fourAtOnce && cell >= kWideLanes;
    const bool melts = wet && reach.under.owned();
    const bool eightWet = !scalarOnly && m_paint.mode != LayMode::KeepAlpha && !reach.keepsUnderAsPixels;  // at any grid
    // A skirted band's marked pixels lie in runs along the row: eight at a time they take their marks in lanes.
    const bool marksEight = !scalarOnly && marksInLanes(stamps);
    std::vector<StampMark> laneMarks;
    std::vector<char> laneMarked;
    std::vector<Premultiplied> rowBlur;  // the melt's coarse blur at this row, at each cell column the row passes
    int blurFirst = 0;
    for (int y = top; y <= bottom; ++y) {
        // Every pixel a changed point can reach, so what it shows follows the points it reads.
        const RowReach& row = m_rows[static_cast<std::size_t>(y - m_rowsTop)];
        const double rowLeft = row.readLeft, rowRight = row.readRight;
        if (rowLeft > rowRight)
            continue;
        laysFrom = row.laysFrom;
        laysTo = row.laysTo;
        const int left = std::max(area.left(), static_cast<int>(std::floor(rowLeft)) - 1);
        const int right = std::min(area.right(), static_cast<int>(std::ceil(rowRight)) + 1);
        if (left > right)
            continue;
        if (melts && m_coarseWet) {  // read down between the blur's two cell rows once per cell column, then across per pixel
            const CoarseBlur& blur = *m_coarseWet;
            const CoarseBlur::Between& down = blur.rows[static_cast<std::size_t>(y - blur.dab.top())];
            blurFirst = blur.columns[static_cast<std::size_t>(left - blur.dab.left())].first;
            const int blurLast = blur.columns[static_cast<std::size_t>(right - blur.dab.left())].first + 1;
            rowBlur.resize(static_cast<std::size_t>(blurLast - blurFirst + 1));
            for (int k = blurFirst; k <= blurLast; ++k)
                rowBlur[static_cast<std::size_t>(k - blurFirst)] =
                    lerp(blur.cells.at(k, down.first), blur.cells.at(k, down.first + 1), down.share);
        }
        const auto blurAt = [&](int x) {
            if (!m_coarseWet)
                return m_wet->at(x, y);
            const CoarseBlur::Between& column = m_coarseWet->columns[static_cast<std::size_t>(x - m_coarseWet->dab.left())];
            return lerp(rowBlur[static_cast<std::size_t>(column.first - blurFirst)],
                        rowBlur[static_cast<std::size_t>(column.first + 1 - blurFirst)], column.share);
        };
        const int gy = (y - tile.top()) / cell;
        const float fy = static_cast<float>(y - tile.top() - gy * cell) / static_cast<float>(cell);
        const int firstColumn = (left - tile.left()) / cell, lastColumn = (right - tile.left()) / cell + 1;
        for (int gx = firstColumn; gx <= lastColumn; ++gx) {
            GridColumn& column = columns[static_cast<std::size_t>(gx)];
            column = {};
            addPoint(column, gx, gy, 1.0f - fy);
            addPoint(column, gx, gy + 1, fy);
        }
        // Where a stamp's edge is sharper than the grid can follow, at the sides of the stroke, its density is worked out per pixel.
        sharp.assign(static_cast<std::size_t>(right - left + 1), 0);
        dabShape.assign(static_cast<std::size_t>(right - left + 1), 0.0f);
        dabShapeLeft = left;
        for (const BandStamp& band : stamps) {
            const Stamp& stamp = m_stamps[dab.first + band.k];
            if (stamp.skirt > 0.0f) {
                markSkirtedEdges(dab, band, y, left, right, sharp);
                continue;
            }
            const float outer = outerOf(stamp);
            const float outside = outer + kSharpOutsideCells * static_cast<float>(cell);
            const float inside = std::max(outer - kSharpCells * static_cast<float>(cell) - kSharpInsideSpare, 0.0f);
            const WholeStamp& whole = m_wholeStamps[band.k];
            const float dy = whole.frame.dy(y);
            if (std::abs(dy) >= outside)
                continue;
            const float far = std::sqrt(outside * outside - dy * dy);
            const float near = std::abs(dy) < inside ? std::sqrt(inside * inside - dy * dy) : 0.0f;
            // dx runs from -far to -near and near to far: the pixels' x is the stamp's frame's dx back to a place.
            const float x0 = static_cast<float>(left) - whole.frame.dx(left);  // where dx is 0
            // A ramp along the way is narrow only at the stamp's sides: within `slab` of the line through its centre across the way.
            float slabFrom = -std::numeric_limits<float>::infinity(), slabTo = std::numeric_limits<float>::infinity();
            if (stamp.paintRamp > 0.0f) {
                const float slab = kSharpCells * static_cast<float>(cell) * outside / stamp.paintRamp + kSlabSpare;
                const float onLine = dy * stamp.headingY;
                if (std::abs(stamp.headingX) > kNoSidewaysHeading) {
                    const float p = (-slab - onLine) / stamp.headingX, q = (slab - onLine) / stamp.headingX;
                    slabFrom = std::min(p, q) - kSlabSpare;
                    slabTo = std::max(p, q) + kSlabSpare;
                } else if (std::abs(onLine) >= slab) {
                    continue;
                }
            }
            for (const auto& [ringFrom, ringTo] : {std::pair{-far, -near}, std::pair{near, far}}) {
                const float from = std::max(ringFrom, slabFrom), to = std::min(ringTo, slabTo);
                if (from > to)
                    continue;
                const int first = std::max(left, static_cast<int>(std::floor(x0 + from))),
                          last = std::min(right, static_cast<int>(std::ceil(x0 + to)));
                for (int px = first; px <= last; ++px) {
                    const float dx = whole.frame.dx(px), distance = std::sqrt(dx * dx + dy * dy);
                    // Near its outline a stamp's paint is the lesser of its rim, which falls steeply at the very edge, and its ramp along the
                    // way: where that ramp is wide it's the lesser, and as smooth as the grid can follow.
                    const float width = stamp.paintRamp > 0.0f ? stamp.paintRamp * headingCosine(stamp, dx, dy, distance) : 0.0f;
                    if (distance < outside && distance >= inside && width < kSharpCells * static_cast<float>(cell))
                        sharp[static_cast<std::size_t>(px - left)] = 1;
                }
            }
        }
        const std::size_t rowIndex = static_cast<std::size_t>(indexIn(laying.coord, tile.left(), y));
        if (marksEight) {
            laneMarked.assign(sharp.size(), 0);
            laneMarks.resize(sharp.size());
            for (std::size_t q = 0; q < sharp.size();) {
                std::size_t end = q;
                while (end < sharp.size() && sharp[end])
                    ++end;
                for (; q + kWideLanes <= end; q += kWideLanes) {
                    markEight(dab, left + static_cast<int>(q), y, stamps, laneMarks.data() + q);
                    std::fill_n(laneMarked.begin() + static_cast<std::ptrdiff_t>(q), kWideLanes, 1);
                }
                q = std::max(end, q) + 1;
            }
        }
        const auto laneMarkAt = [&](int x) { return marksEight && laneMarked[static_cast<std::size_t>(x - left)]; };
        // What this dab lays at a pixel, exactly, less what the points around it say it does (`laid`), is kept with the pixel.
        const auto addExactMark = [&](int x, std::size_t index, float laid) {
            // its passes aren't read here
            const StampMark mark = !laysAt(x)      ? StampMark{}
                                   : laneMarkAt(x) ? laneMarks[static_cast<std::size_t>(x - left)]
                                                   : dabMark(dab, x, y, &stamps, false);
            // As the stroke's own record has it: density times the outline's share at each dab, over the outline it has now.
            // What the dab lays here, as the grid's points count it: none in a dab that only melts, and times layingAt as Water Color's
            // paint comes in above Blur Pressure.
            const float exact =
                laysPaint && mark.shape > 0.0f && mark.paint > 0.0f ? mark.paint * static_cast<float>(layingAt(dab.pressure)) : 0.0f;
            if (exact != laid)
                reach.fineValue.own()[index] += exact - laid;
            return mark;
        };
        // Four pixels from x that the read-back for four can't show as the points say, as for a marked one: if every one is reached,
        // each marked one takes its exact mark now, as the scalar code below would, and is no longer marked, so all four can be shown
        // four at once, each working its curve out from the density it has now.
        const auto marksFour = [&](int x, std::size_t index) {
            float weights[kLanes], laids[kLanes];
            for (int i = 0; i < kLanes; ++i) {
                const int gx = (x + i - tile.left()) >> cellShift;
                const float fx = static_cast<float>((x + i - tile.left()) & (cell - 1)) * perCell;
                const GridColumn& a = columns[static_cast<std::size_t>(gx)];
                const GridColumn& b = columns[static_cast<std::size_t>(gx) + 1];
                const float wa = 1.0f - fx, wb = fx;
                weights[i] = wa * a.weight + wb * b.weight;
                laids[i] = wa * a.laid + wb * b.laid;
                if (weights[i] <= 0.0f)
                    return false;  // unreached: the scalar code lays it exactly
            }
            for (int i = 0; i < kLanes; ++i) {
                char& marked = sharp[static_cast<std::size_t>(x + i - left)];
                if (marked) {
                    addExactMark(x + i, index + static_cast<std::size_t>(i), laids[i]);
                    marked = 0;
                }
            }
            return true;
        };
        int outlined = left;  // pixels before this have had this dab's outline
        for (int x = left; x <= right; ++x) {
            const std::size_t index = rowIndex + static_cast<std::size_t>(x - tile.left());
            if (eightWet && (x - tile.left()) % kWideLanes == 0 && x + kWideLanes - 1 <= right) {
                for (int i = 0; i < kWideLanes; ++i)
                    if (outlineAt(x + i, y, index + static_cast<std::size_t>(i)))
                        sharp[static_cast<std::size_t>(x + i - left)] = 1;
                outlined = x + kWideLanes;
                alignas(32) Premultiplied blurred[kWideLanes];
                if (melts)
                    for (int i = 0; i < kWideLanes; ++i)
                        blurred[i] = blurAt(x + i);
                if (showEightWet(laying, columns, sharp, x - left, index, x - tile.left(), melts ? blurred : nullptr)) {
                    x += kWideLanes - 1;
                    continue;
                }
            }
            if (eightAtOnce && (x - tile.left()) % kWideLanes == 0 && x + kWideLanes - 1 <= right) {
                if (fourOutlines) {
                    outlineFour(x, y, index);
                    outlineFour(x + kLanes, y, index + kLanes);
                } else {
                    for (int i = 0; i < kWideLanes; ++i)
                        if (outlineAt(x + i, y, index + static_cast<std::size_t>(i)))
                            sharp[static_cast<std::size_t>(x + i - left)] = 1;
                }
                outlined = x + kWideLanes;
                if (showEight(laying, columns, sharp, x - left, index, x - tile.left())) {
                    x += kWideLanes - 1;
                    continue;
                }
                // A soft outline not yet whole: four and four, as the read-back for four does it.
                if (m_mixing.outlineAlongPath && !melts && showFourSoft(laying, columns, sharp, x - left, index, x - tile.left())) {
                    const bool second = showFourSoft(laying, columns, sharp, x + kLanes - left, index + kLanes, x + kLanes - tile.left());
                    x += (second ? kWideLanes : kLanes) - 1;
                    continue;
                }
            }
            if (fourAtOnce && (x - tile.left()) % kLanes == 0 && x + kLanes - 1 <= right) {
                if (fourOutlines && x >= outlined)
                    outlineFour(x, y, index);
                else
                    for (int i = 0; i < kLanes; ++i)
                        if (x + i >= outlined && outlineAt(x + i, y, index + static_cast<std::size_t>(i)))
                            sharp[static_cast<std::size_t>(x + i - left)] = 1;
                outlined = std::max(outlined, x + kLanes);
                if (showFour(laying, columns, sharp, x - left, index, x - tile.left())) {
                    x += kLanes - 1;
                    continue;
                }
                if (m_mixing.outlineAlongPath && !melts && showFourSoft(laying, columns, sharp, x - left, index, x - tile.left())) {
                    x += kLanes - 1;
                    continue;
                }
                if (!m_mixing.outlineAlongPath && !melts && marksFour(x, index) &&
                    showFourSoft(laying, columns, sharp, x - left, index, x - tile.left())) {
                    x += kLanes - 1;
                    continue;
                }
            }
            if (x >= outlined && outlineAt(x, y, index))
                sharp[static_cast<std::size_t>(x - left)] = 1;
            // What it shows, read between the columns either side of it.
            const int gx = (x - tile.left()) >> cellShift;
            const float fx = static_cast<float>((x - tile.left()) & (cell - 1)) * perCell;
            const GridColumn& a = columns[static_cast<std::size_t>(gx)];
            const GridColumn& b = columns[static_cast<std::size_t>(gx) + 1];
            // Density reads a point the stroke hasn't reached as none, which it is, so what's kept per pixel below stays true as more
            // points are reached; paint and the coverage curve read only the points that are reached.
            const float wa = 1.0f - fx, wb = fx;
            const float weight = wa * a.weight + wb * b.weight;
            const float value = wa * a.value + wb * b.value, laid = wa * a.laid + wb * b.laid;
            float cover = 0.0f;
            Premultiplied kept = paint;  // where no point around it is reached yet, at the very edge: the dab's own paint
            if (weight > 0.0f) {
                const float over = 1.0f / weight;
                cover = (wa * a.cover + wb * b.cover) * over;
                for (std::size_t c = 0; c < 4; ++c)
                    kept[c] = (wa * a.kept[c] + wb * b.kept[c]) * over;
            }
            std::optional<StampMark> exactMark;
            if (weight <= 0.0f || sharp[static_cast<std::size_t>(x - left)])
                exactMark = addExactMark(x, index, laid);
            // Kept above even where the stroke hasn't reached yet: the points around it may hold paint it doesn't have.
            const float shape = shapeOf(reach, index);
            if (shape <= 0.0f && dabShape[static_cast<std::size_t>(x - left)] <= 0.0f)
                continue;
            if (melts) {  // Water Color: what's under melts toward its blur, by what the dab lays here, as layPixel has it
                const float melt = exactMark ? meltOf(*exactMark) : wa * a.melt + wb * b.melt;
                if (melt > 0.0f)  // soften leaves it as it is otherwise: the blur needn't be read
                    soften(reach, index, blurAt(x), melt);
            }
            if (shape <= 0.0f) {  // reached by a dab that only melts: it shows what's under, as layPixel's show() does with no coverage
                writePixel(laying.tile->px.data() + index * 4, shown(reach.underAt(index), kept, 0.0f));
                continue;
            }
            if (reach.fineValue[index] != 0.0f || weight < kAllReached || shape < 1.0f || !a.whole || !b.whole) {
                // By the edge of what's reached, or with what's kept, the curve is worked out from the density here.
                const float total = value + reach.fineValue[index];
                if (total <= 0.0f)
                    continue;
                cover = coverageOf(total, shape) / (m_paint.opacity * shape);
            }
            Channel* px = laying.tile->px.data() + index * 4;
            const float coverage = m_paint.opacity * shape * cover;
            if (m_paint.mode == LayMode::KeepAlpha || !reach.keepsUnderAsPixels) {
                writePixel(px, shown(reach.underAt(index), kept, coverage));
                continue;
            }
            // shown() and writePixel(), worked out here channel by channel, the same steps.
            const Channel* before = reach.before ? reach.before->px.data() + index * 4 : nullptr;
            const float u0 = before ? before[0] * kChannelToFloat : 0.0f, u1 = before ? before[1] * kChannelToFloat : 0.0f;
            const float u2 = before ? before[2] * kChannelToFloat : 0.0f, u3 = before ? before[3] * kChannelToFloat : 0.0f;
            float o0 = lerp(u0, kept[0], coverage), o1 = lerp(u1, kept[1], coverage), o2 = lerp(u2, kept[2], coverage),
                  o3 = lerp(u3, kept[3], coverage);
            if (!m_mixing.transparentColour && o3 < u3 && o3 > kLeastShownAlpha) {
                const float up = u3 / o3;
                o0 *= up;
                o1 *= up;
                o2 *= up;
                o3 *= up;
            }
            const Channel alpha = toChannel(o3);
            px[0] = std::min(toChannel(o0), alpha);
            px[1] = std::min(toChannel(o1), alpha);
            px[2] = std::min(toChannel(o2), alpha);
            px[3] = alpha;
        }
    }
}

// Four neighbouring pixels of a row, from x (a multiple of 4 into the tile) on, where all four show what the points around them say,
// with nothing kept per pixel: each lane takes the scalar code's steps on its pixel, in the same order, so the pixels are the same. No
// if any of them needs more.
bool DabPainter::showFour(Laying& laying, const std::vector<GridColumn>& columns, const std::vector<char>& sharp, int fromLeft,
                          std::size_t index, int intoTile) const {
    const Reach& reach = *laying.reach;
    const int cell = m_gridCell, shift = std::countr_zero(static_cast<unsigned>(cell));
    const float perCell = 1.0f / static_cast<float>(cell);
    // All four inside a whole outline, with nothing kept per pixel and no sharp edge.
    const __m128 shape = _mm_loadu_ps(reach.shape.data() + index);
    if (_mm_movemask_ps(_mm_cmplt_ps(shape, _mm_set1_ps(1.0f))) != 0)
        return false;
    if (_mm_movemask_ps(_mm_cmpneq_ps(_mm_loadu_ps(reach.fineValue.data() + index), _mm_setzero_ps())) != 0)
        return false;
    std::uint32_t anySharp = 0;
    std::memcpy(&anySharp, sharp.data() + fromLeft, sizeof anySharp);
    if (anySharp != 0)
        return false;
    // The columns either side of each pixel. The four start a cell, so on a grid of four they share the same two; on a grid of two, the
    // first pair reads columns g and g + 1, the second g + 1 and g + 2.
    const std::size_t g = static_cast<std::size_t>(intoTile >> shift);
    const bool pairs = cell < kLanes;
    const GridColumn& c0 = columns[g];
    const GridColumn& c1 = columns[g + 1];
    const GridColumn& c2 = pairs ? columns[g + 2] : c1;
    if (!c0.whole || !c1.whole || !c2.whole)
        return false;
    const auto lanesA = [&](float GridColumn::* f) { return pairs ? _mm_setr_ps(c0.*f, c0.*f, c1.*f, c1.*f) : _mm_set1_ps(c0.*f); };
    const auto lanesB = [&](float GridColumn::* f) { return pairs ? _mm_setr_ps(c1.*f, c1.*f, c2.*f, c2.*f) : _mm_set1_ps(c1.*f); };
    const auto keptA = [&](int c) { return pairs ? _mm_setr_ps(c0.kept[c], c0.kept[c], c1.kept[c], c1.kept[c]) : _mm_set1_ps(c0.kept[c]); };
    const auto keptB = [&](int c) { return pairs ? _mm_setr_ps(c1.kept[c], c1.kept[c], c2.kept[c], c2.kept[c]) : _mm_set1_ps(c1.kept[c]); };
    alignas(16) float fxs[kLanes];
    for (int i = 0; i < kLanes; ++i)
        fxs[i] = static_cast<float>((intoTile + i) & (cell - 1)) * perCell;
    const __m128 vwb = _mm_load_ps(fxs), vwa = _mm_sub_ps(_mm_set1_ps(1.0f), vwb);
    const __m128 weight = _mm_add_ps(_mm_mul_ps(vwa, lanesA(&GridColumn::weight)), _mm_mul_ps(vwb, lanesB(&GridColumn::weight)));
    if (_mm_movemask_ps(_mm_cmplt_ps(weight, _mm_set1_ps(kAllReached))) != 0)
        return false;
    const __m128 over = _mm_div_ps(_mm_set1_ps(1.0f), weight);
    const __m128 cover =
        _mm_mul_ps(_mm_add_ps(_mm_mul_ps(vwa, lanesA(&GridColumn::cover)), _mm_mul_ps(vwb, lanesB(&GridColumn::cover))), over);
    const __m128 coverage = _mm_mul_ps(_mm_mul_ps(_mm_set1_ps(m_paint.opacity), shape), cover);
    // What was under, four pixels of four channels, turned into four channels of four pixels.
    __m128 u[4];
    if (reach.before) {
        const __m128i raw0 = _mm_loadu_si128(reinterpret_cast<const __m128i*>(reach.before->px.data() + index * 4));
        const __m128i raw1 = _mm_loadu_si128(reinterpret_cast<const __m128i*>(reach.before->px.data() + index * 4 + 8));
        const __m128i zero = _mm_setzero_si128();
        const __m128 toFloat = _mm_set1_ps(kChannelToFloat);
        u[0] = _mm_mul_ps(_mm_cvtepi32_ps(_mm_unpacklo_epi16(raw0, zero)), toFloat);
        u[1] = _mm_mul_ps(_mm_cvtepi32_ps(_mm_unpackhi_epi16(raw0, zero)), toFloat);
        u[2] = _mm_mul_ps(_mm_cvtepi32_ps(_mm_unpacklo_epi16(raw1, zero)), toFloat);
        u[3] = _mm_mul_ps(_mm_cvtepi32_ps(_mm_unpackhi_epi16(raw1, zero)), toFloat);
        _MM_TRANSPOSE4_PS(u[0], u[1], u[2], u[3]);
    } else {
        u[0] = u[1] = u[2] = u[3] = _mm_setzero_ps();
    }
    __m128 o[4];
    for (int c = 0; c < 4; ++c) {
        const __m128 kept = _mm_mul_ps(_mm_add_ps(_mm_mul_ps(vwa, keptA(c)), _mm_mul_ps(vwb, keptB(c))), over);
        o[c] = _mm_add_ps(u[c], _mm_mul_ps(_mm_sub_ps(kept, u[c]), coverage));
    }
    if (!m_mixing.transparentColour) {  // a colour never thins what's there, as shown() has it
        const __m128 thinner = _mm_and_ps(_mm_cmplt_ps(o[3], u[3]), _mm_cmpgt_ps(o[3], _mm_set1_ps(kLeastShownAlpha)));
        if (_mm_movemask_ps(thinner) != 0) {
            const __m128 up = _mm_div_ps(u[3], o[3]);
            for (__m128& v : o)
                v = _mm_or_ps(_mm_and_ps(thinner, _mm_mul_ps(v, up)), _mm_andnot_ps(thinner, v));
        }
    }
    // toChannel, and no colour over its alpha: truncation keeps order, so the least of two before it is the least after.
    const __m128 one = _mm_set1_ps(1.0f), scale = _mm_set1_ps(static_cast<float>(kChannelOne)), half = _mm_set1_ps(0.5f);
    __m128 t[4];
    for (int c = 0; c < 4; ++c)
        t[c] = _mm_add_ps(_mm_mul_ps(_mm_min_ps(_mm_max_ps(o[c], _mm_setzero_ps()), one), scale), half);
    for (int c = 0; c < 3; ++c)
        t[c] = _mm_min_ps(t[c], t[3]);
    _MM_TRANSPOSE4_PS(t[0], t[1], t[2], t[3]);
    // SSE2 packs to signed 16 bits only: a channel is moved down by half the range to pack, and its top bit flipped back after.
    constexpr int kHalfRange = 32768;
    const __m128i bias = _mm_set1_epi32(kHalfRange), flip = _mm_set1_epi16(static_cast<short>(kHalfRange));
    const auto pack = [&](__m128 p, __m128 q) {  // two pixels' four channels, 0 to 65535, as 16 bits each
        const __m128i ip = _mm_sub_epi32(_mm_cvttps_epi32(p), bias), iq = _mm_sub_epi32(_mm_cvttps_epi32(q), bias);
        return _mm_xor_si128(_mm_packs_epi32(ip, iq), flip);
    };
    Channel* px = laying.tile->px.data() + index * 4;
    _mm_storeu_si128(reinterpret_cast<__m128i*>(px), pack(t[0], t[1]));
    _mm_storeu_si128(reinterpret_cast<__m128i*>(px + 8), pack(t[2], t[3]));
    return true;
}

// The scalar read-back's steps for a pixel the soft outline doesn't cover whole, four at once: its outline's share, max(most,
// 1 - e^-passes); the density there, the columns' and its own kept; its curve as coverageOf has it, or where the pixel is whole with
// nothing kept, the points' own cover; then as showFour. No if any of the four is sharp, unreached, has no outline or no density.
bool DabPainter::showFourSoft(Laying& laying, const std::vector<GridColumn>& columns, const std::vector<char>& sharp, int fromLeft,
                              std::size_t index, int intoTile) const {
    const Reach& reach = *laying.reach;
    const int cell = m_gridCell, shift = std::countr_zero(static_cast<unsigned>(cell));
    const float perCell = 1.0f / static_cast<float>(cell);
    std::uint32_t anySharp = 0;
    std::memcpy(&anySharp, sharp.data() + fromLeft, sizeof anySharp);
    if (anySharp != 0)
        return false;
    const std::size_t g = static_cast<std::size_t>(intoTile >> shift);
    const bool pairs = cell < kLanes;
    const GridColumn& c0 = columns[g];
    const GridColumn& c1 = columns[g + 1];
    const GridColumn& c2 = pairs ? columns[g + 2] : c1;
    const auto lanesA = [&](float GridColumn::* f) { return pairs ? _mm_setr_ps(c0.*f, c0.*f, c1.*f, c1.*f) : _mm_set1_ps(c0.*f); };
    const auto lanesB = [&](float GridColumn::* f) { return pairs ? _mm_setr_ps(c1.*f, c1.*f, c2.*f, c2.*f) : _mm_set1_ps(c1.*f); };
    const auto keptA = [&](int c) { return pairs ? _mm_setr_ps(c0.kept[c], c0.kept[c], c1.kept[c], c1.kept[c]) : _mm_set1_ps(c0.kept[c]); };
    const auto keptB = [&](int c) { return pairs ? _mm_setr_ps(c1.kept[c], c1.kept[c], c2.kept[c], c2.kept[c]) : _mm_set1_ps(c1.kept[c]); };
    alignas(16) float fxs[kLanes];
    for (int i = 0; i < kLanes; ++i)
        fxs[i] = static_cast<float>((intoTile + i) & (cell - 1)) * perCell;
    const __m128 vwb = _mm_load_ps(fxs), vwa = _mm_sub_ps(_mm_set1_ps(1.0f), vwb);
    const __m128 zero = _mm_setzero_ps(), one = _mm_set1_ps(1.0f);
    const __m128 weight = _mm_add_ps(_mm_mul_ps(vwa, lanesA(&GridColumn::weight)), _mm_mul_ps(vwb, lanesB(&GridColumn::weight)));
    if (_mm_movemask_ps(_mm_cmple_ps(weight, zero)) != 0)
        return false;  // unreached: the scalar code lays it exactly
    // The outline's share, as shapeOf has it; without the outline along the path, shapeOf itself, lane by lane.
    __m128 shape;
    if (m_mixing.outlineAlongPath) {
        const __m128 most = _mm_loadu_ps(reach.shape.data() + index);
        const __m128 passed = _mm_loadu_ps(reach.passed.data() + index);
        const __m128 fromPasses = _mm_sub_ps(zero, laneExpm1(_mm_sub_ps(zero, passed)));
        const __m128 wholeMost = _mm_cmpge_ps(most, one);
        shape = _mm_or_ps(_mm_and_ps(wholeMost, most), _mm_andnot_ps(wholeMost, _mm_max_ps(most, fromPasses)));
    } else {
        alignas(16) float shapes[kLanes];
        for (int i = 0; i < kLanes; ++i)
            shapes[i] = shapeOf(reach, index + static_cast<std::size_t>(i));
        shape = _mm_load_ps(shapes);
    }
    if (_mm_movemask_ps(_mm_cmple_ps(shape, zero)) != 0)
        return false;
    const __m128 fine = _mm_loadu_ps(reach.fineValue.data() + index);
    const __m128 value = _mm_add_ps(_mm_mul_ps(vwa, lanesA(&GridColumn::value)), _mm_mul_ps(vwb, lanesB(&GridColumn::value)));
    const __m128 total = _mm_add_ps(value, fine);
    const __m128 over = _mm_div_ps(one, weight);
    const __m128 pointsCover =
        _mm_mul_ps(_mm_add_ps(_mm_mul_ps(vwa, lanesA(&GridColumn::cover)), _mm_mul_ps(vwb, lanesB(&GridColumn::cover))), over);
    // Where it works the curve out from the density here: something kept, not all reached, an outline not whole, a column not whole.
    const bool columnsWhole = c0.whole && c1.whole && c2.whole;
    __m128 worked =
        _mm_or_ps(_mm_or_ps(_mm_cmpneq_ps(fine, zero), _mm_cmplt_ps(weight, _mm_set1_ps(kAllReached))), _mm_cmplt_ps(shape, one));
    if (!columnsWhole) {
        if (pairs) {
            alignas(16) const float notWhole[kLanes] = {c0.whole && c1.whole ? 0.0f : 1.0f, c0.whole && c1.whole ? 0.0f : 1.0f,
                                                        c1.whole && c2.whole ? 0.0f : 1.0f, c1.whole && c2.whole ? 0.0f : 1.0f};
            worked = _mm_or_ps(worked, _mm_cmpneq_ps(_mm_load_ps(notWhole), zero));
        } else {
            worked = _mm_castsi128_ps(_mm_set1_epi32(-1));
        }
    }
    if (_mm_movemask_ps(_mm_and_ps(worked, _mm_cmple_ps(total, zero))) != 0)
        return false;  // no density there: the scalar code leaves it as it is
    // coverageOf(total, shape) / (opacity * shape), lane by lane; without the outline along the path its curve is the scalar one, so
    // coverageOf itself, in the lanes that work it out.
    const __m128 opacity = _mm_set1_ps(m_paint.opacity);
    __m128 workedCover;
    if (m_mixing.outlineAlongPath) {
        const __m128 perShape = _mm_div_ps(total, shape);
        const __m128 curve = _mm_sub_ps(zero, laneExpm1(_mm_sub_ps(zero, perShape)));
        const __m128 full = _mm_cmpge_ps(perShape, _mm_set1_ps(m_fullFrom));
        const __m128 opacityShape = _mm_mul_ps(opacity, shape);
        const __m128 curved = _mm_mul_ps(opacityShape, _mm_min_ps(one, _mm_div_ps(curve, _mm_set1_ps(m_paint.fullAt))));
        const __m128 coverageOfIt = _mm_or_ps(_mm_and_ps(full, opacityShape), _mm_andnot_ps(full, curved));
        workedCover = _mm_div_ps(coverageOfIt, _mm_mul_ps(opacity, shape));
    } else {
        alignas(16) float totals[kLanes], shapes[kLanes], covers[kLanes] = {};
        _mm_store_ps(totals, total);
        _mm_store_ps(shapes, shape);
        const int workedLanes = _mm_movemask_ps(worked);
        for (int i = 0; i < kLanes; ++i)
            if (workedLanes & (1 << i))
                covers[i] = coverageOf(totals[i], shapes[i]) / (m_paint.opacity * shapes[i]);
        workedCover = _mm_load_ps(covers);
    }
    const __m128 cover = _mm_or_ps(_mm_and_ps(worked, workedCover), _mm_andnot_ps(worked, pointsCover));
    const __m128 coverage = _mm_mul_ps(_mm_mul_ps(opacity, shape), cover);
    __m128 u[4];
    if (reach.before) {
        const __m128i raw0 = _mm_loadu_si128(reinterpret_cast<const __m128i*>(reach.before->px.data() + index * 4));
        const __m128i raw1 = _mm_loadu_si128(reinterpret_cast<const __m128i*>(reach.before->px.data() + index * 4 + 8));
        const __m128i zeroi = _mm_setzero_si128();
        const __m128 toFloat = _mm_set1_ps(kChannelToFloat);
        u[0] = _mm_mul_ps(_mm_cvtepi32_ps(_mm_unpacklo_epi16(raw0, zeroi)), toFloat);
        u[1] = _mm_mul_ps(_mm_cvtepi32_ps(_mm_unpackhi_epi16(raw0, zeroi)), toFloat);
        u[2] = _mm_mul_ps(_mm_cvtepi32_ps(_mm_unpacklo_epi16(raw1, zeroi)), toFloat);
        u[3] = _mm_mul_ps(_mm_cvtepi32_ps(_mm_unpackhi_epi16(raw1, zeroi)), toFloat);
        _MM_TRANSPOSE4_PS(u[0], u[1], u[2], u[3]);
    } else {
        u[0] = u[1] = u[2] = u[3] = zero;
    }
    __m128 o[4];
    for (int c = 0; c < 4; ++c) {
        const __m128 kept = _mm_mul_ps(_mm_add_ps(_mm_mul_ps(vwa, keptA(c)), _mm_mul_ps(vwb, keptB(c))), over);
        o[c] = _mm_add_ps(u[c], _mm_mul_ps(_mm_sub_ps(kept, u[c]), coverage));
    }
    if (!m_mixing.transparentColour) {
        const __m128 thinner = _mm_and_ps(_mm_cmplt_ps(o[3], u[3]), _mm_cmpgt_ps(o[3], _mm_set1_ps(kLeastShownAlpha)));
        if (_mm_movemask_ps(thinner) != 0) {
            const __m128 up = _mm_div_ps(u[3], o[3]);
            for (__m128& v : o)
                v = _mm_or_ps(_mm_and_ps(thinner, _mm_mul_ps(v, up)), _mm_andnot_ps(thinner, v));
        }
    }
    const __m128 scale = _mm_set1_ps(static_cast<float>(kChannelOne)), half = _mm_set1_ps(0.5f);
    __m128 t[4];
    for (int c = 0; c < 4; ++c)
        t[c] = _mm_add_ps(_mm_mul_ps(_mm_min_ps(_mm_max_ps(o[c], zero), one), scale), half);
    for (int c = 0; c < 3; ++c)
        t[c] = _mm_min_ps(t[c], t[3]);
    _MM_TRANSPOSE4_PS(t[0], t[1], t[2], t[3]);
    constexpr int kHalfRange = 32768;
    const __m128i bias = _mm_set1_epi32(kHalfRange), flip = _mm_set1_epi16(static_cast<short>(kHalfRange));
    const auto pack = [&](__m128 p, __m128 q) {
        const __m128i ip = _mm_sub_epi32(_mm_cvttps_epi32(p), bias), iq = _mm_sub_epi32(_mm_cvttps_epi32(q), bias);
        return _mm_xor_si128(_mm_packs_epi32(ip, iq), flip);
    };
    Channel* px = laying.tile->px.data() + index * 4;
    _mm_storeu_si128(reinterpret_cast<__m128i*>(px), pack(t[0], t[1]));
    _mm_storeu_si128(reinterpret_cast<__m128i*>(px + 8), pack(t[2], t[3]));
    return true;
}

namespace {

// wa · a + wb · b in each lane, as the scalar read-back works out a value between two columns.
__m256 blendLanes(__m256 wa, __m256 wb, float a, float b) {
    return _mm256_add_ps(_mm256_mul_ps(wa, _mm256_set1_ps(a)), _mm256_mul_ps(wb, _mm256_set1_ps(b)));
}

// The same, each lane its own pair.
__m256 blendEachLane(__m256 wa, __m256 wb, const float* a, const float* b) {
    return _mm256_add_ps(_mm256_mul_ps(wa, _mm256_load_ps(a)), _mm256_mul_ps(wb, _mm256_load_ps(b)));
}

}

// Eight pixels starting a cell of a grid at least kWideLanes apart: all read the same two columns. Each lane takes the scalar code's steps
// on its pixel in the same order, as showFour's do, so the pixels are the same. No if any of them needs more.
bool DabPainter::showEight(Laying& laying, const std::vector<GridColumn>& columns, const std::vector<char>& sharp, int fromLeft,
                           std::size_t index, int intoTile) const {
    const Reach& reach = *laying.reach;
    const int cell = m_gridCell, shift = std::countr_zero(static_cast<unsigned>(cell));
    const float perCell = 1.0f / static_cast<float>(cell);
    const __m256 shape = _mm256_loadu_ps(reach.shape.data() + index);
    if (_mm256_movemask_ps(_mm256_cmp_ps(shape, _mm256_set1_ps(1.0f), _CMP_LT_OQ)) != 0)
        return false;
    if (_mm256_movemask_ps(_mm256_cmp_ps(_mm256_loadu_ps(reach.fineValue.data() + index), _mm256_setzero_ps(), _CMP_NEQ_OQ)) != 0)
        return false;
    std::uint64_t anySharp = 0;
    std::memcpy(&anySharp, sharp.data() + fromLeft, sizeof anySharp);
    if (anySharp != 0)
        return false;
    const std::size_t g = static_cast<std::size_t>(intoTile >> shift);
    const GridColumn& c0 = columns[g];
    const GridColumn& c1 = columns[g + 1];
    if (!c0.whole || !c1.whole)
        return false;
    alignas(32) float fxs[kWideLanes];
    for (int i = 0; i < kWideLanes; ++i)
        fxs[i] = static_cast<float>((intoTile + i) & (cell - 1)) * perCell;
    const __m256 vwb = _mm256_load_ps(fxs), vwa = _mm256_sub_ps(_mm256_set1_ps(1.0f), vwb);
    const auto blend = [&](float a, float b) { return blendLanes(vwa, vwb, a, b); };
    const __m256 weight = blend(c0.weight, c1.weight);
    if (_mm256_movemask_ps(_mm256_cmp_ps(weight, _mm256_set1_ps(kAllReached), _CMP_LT_OQ)) != 0)
        return false;
    const __m256 over = _mm256_div_ps(_mm256_set1_ps(1.0f), weight);
    const __m256 cover = _mm256_mul_ps(blend(c0.cover, c1.cover), over);
    const __m256 coverage = _mm256_mul_ps(_mm256_mul_ps(_mm256_set1_ps(m_paint.opacity), shape), cover);
    // What was under: eight pixels of four channels, turned into four channels of eight pixels, four pixels at a time as showFour does.
    __m256 u[4];
    if (reach.before) {
        const Channel* from = reach.before->px.data() + index * 4;
        const __m128i zero = _mm_setzero_si128();
        const __m128 toFloat = _mm_set1_ps(kChannelToFloat);
        __m128 half[2][4];
        for (int h = 0; h < 2; ++h) {
            const __m128i raw0 = _mm_loadu_si128(reinterpret_cast<const __m128i*>(from + h * 16));
            const __m128i raw1 = _mm_loadu_si128(reinterpret_cast<const __m128i*>(from + h * 16 + 8));
            half[h][0] = _mm_mul_ps(_mm_cvtepi32_ps(_mm_unpacklo_epi16(raw0, zero)), toFloat);
            half[h][1] = _mm_mul_ps(_mm_cvtepi32_ps(_mm_unpackhi_epi16(raw0, zero)), toFloat);
            half[h][2] = _mm_mul_ps(_mm_cvtepi32_ps(_mm_unpacklo_epi16(raw1, zero)), toFloat);
            half[h][3] = _mm_mul_ps(_mm_cvtepi32_ps(_mm_unpackhi_epi16(raw1, zero)), toFloat);
            _MM_TRANSPOSE4_PS(half[h][0], half[h][1], half[h][2], half[h][3]);
        }
        for (int c = 0; c < 4; ++c)
            u[c] = _mm256_set_m128(half[1][c], half[0][c]);
    } else {
        u[0] = u[1] = u[2] = u[3] = _mm256_setzero_ps();
    }
    __m256 o[4];
    for (int c = 0; c < 4; ++c) {
        const __m256 kept = _mm256_mul_ps(blend(c0.kept[static_cast<std::size_t>(c)], c1.kept[static_cast<std::size_t>(c)]), over);
        o[c] = _mm256_add_ps(u[c], _mm256_mul_ps(_mm256_sub_ps(kept, u[c]), coverage));
    }
    if (!m_mixing.transparentColour) {  // a colour never thins what's there, as shown() has it
        const __m256 thinner =
            _mm256_and_ps(_mm256_cmp_ps(o[3], u[3], _CMP_LT_OQ), _mm256_cmp_ps(o[3], _mm256_set1_ps(kLeastShownAlpha), _CMP_GT_OQ));
        if (_mm256_movemask_ps(thinner) != 0) {
            const __m256 up = _mm256_div_ps(u[3], o[3]);
            for (__m256& v : o)
                v = _mm256_blendv_ps(v, _mm256_mul_ps(v, up), thinner);
        }
    }
    const __m256 one = _mm256_set1_ps(1.0f), scale = _mm256_set1_ps(static_cast<float>(kChannelOne)), half = _mm256_set1_ps(0.5f);
    __m256 t[4];
    for (int c = 0; c < 4; ++c)
        t[c] = _mm256_add_ps(_mm256_mul_ps(_mm256_min_ps(_mm256_max_ps(o[c], _mm256_setzero_ps()), one), scale), half);
    for (int c = 0; c < 3; ++c)
        t[c] = _mm256_min_ps(t[c], t[3]);
    // Back to four pixels at a time, packed to 16 bits as showFour packs them.
    constexpr int kHalfRange = 32768;
    const __m128i bias = _mm_set1_epi32(kHalfRange), flip = _mm_set1_epi16(static_cast<short>(kHalfRange));
    const auto pack = [&](__m128 p, __m128 q) {
        const __m128i ip = _mm_sub_epi32(_mm_cvttps_epi32(p), bias), iq = _mm_sub_epi32(_mm_cvttps_epi32(q), bias);
        return _mm_xor_si128(_mm_packs_epi32(ip, iq), flip);
    };
    Channel* px = laying.tile->px.data() + index * 4;
    for (int h = 0; h < 2; ++h) {
        __m128 q[4];
        for (int c = 0; c < 4; ++c)
            q[c] = h == 0 ? _mm256_castps256_ps128(t[c]) : _mm256_extractf128_ps(t[c], 1);
        _MM_TRANSPOSE4_PS(q[0], q[1], q[2], q[3]);
        _mm_storeu_si128(reinterpret_cast<__m128i*>(px + h * 16), pack(q[0], q[1]));
        _mm_storeu_si128(reinterpret_cast<__m128i*>(px + h * 16 + 8), pack(q[2], q[3]));
    }
    return true;
}
