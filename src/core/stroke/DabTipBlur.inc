// Part of DabTip.cpp, which includes it where it stands: blur, what's under a dab, its blurred and coarse copies, and the pixel buffers
// they use.

// Blur's blur reaches further as pressure goes past kBlurFirmFrom, to blurReachAtFull times as far at full pressure.
double DabPainter::blurReachAt(double pressure) const {
    if (m_mixing.kind != TipMixing::Kind::Blur)
        return 1.0;
    const double firm = std::clamp((pressure - kBlurFirmFrom) / (1.0 - kBlurFirmFrom), 0.0, 1.0);
    return 1.0 + (m_mixing.blurReachAtFull - 1.0) * firm;
}

// Blur moves what was under the stroke toward its blur, as far as the strongest stamp over the pixel has reached, so within a stroke
// it doesn't build up, while passes do. Its own paint is never shown.
void DabPainter::layBlurPixel(const Dab& dab, Laying& laying, int x, int y) const {
    float cover = 0.0f;
    if (m_mixing.blurBell > 0.0 && m_tip.form) {
        // With a form, each stamp's bell times the picture's share there, out to the picture's corners, the most of them.
        const float bell = static_cast<float>(m_mixing.blurBell);
        for (std::size_t i = dab.first; i < dab.first + dab.count; ++i) {
            const WholeStamp& whole = m_wholeStamps[i - dab.first];
            const float dx = whole.frame.dx(x), dy = whole.frame.dy(y);
            const float dist2 = dx * dx + dy * dy;
            if (dist2 >= whole.outsideSquared)
                continue;
            const float sigma = bell * m_stamps[i].radius;
            const float within = dist2 < m_stamps[i].radius * m_stamps[i].radius ? std::exp(-dist2 / (2.0f * sigma * sigma)) : 0.0f;
            const float picture = m_stamps[i].hairs ? hairCoverage(m_stamps[i], dx, dy) : m_tip.form->at(dx, dy, m_stamps[i].radius);
            const float shaped = within + (picture - within) * m_tip.form->strength();
            cover = std::max(cover, shaped);
        }
    } else if (m_mixing.blurBell > 0.0) {
        // A bell of the distance from each stamp's centre, out to its radius, and the most of them. exp only grows, so the most of the
        // bells is the bell of the most of their exponents: one exp a pixel, not one a stamp.
        float most = 0.0f;
        bool any = false;
        const float bell = static_cast<float>(m_mixing.blurBell);
        for (std::size_t i = dab.first; i < dab.first + dab.count; ++i) {
            const WholeStamp& whole = m_wholeStamps[i - dab.first];
            const float dx = whole.frame.dx(x), dy = whole.frame.dy(y);
            const float sigma = bell * m_stamps[i].radius;
            const float dist2 = dx * dx + dy * dy;
            if (dist2 >= m_stamps[i].radius * m_stamps[i].radius)
                continue;
            const float exponent = -dist2 / (2.0f * sigma * sigma);
            most = any ? std::max(most, exponent) : exponent;
            any = true;
        }
        if (any)
            cover = std::max(cover, std::exp(most));
    } else {
        for (std::size_t i = dab.first; i < dab.first + dab.count; ++i) {
            const WholeStamp& whole = m_wholeStamps[i - dab.first];
            const float dx = whole.frame.dx(x), dy = whole.frame.dy(y);
            const float distance = std::hypot(dx, dy);
            const StampMark mark = m_tip.form
                                       ? formedMark(m_stamps[i], dx, dy)
                                       : limitedMark(m_stamps, i, m_tip.edge, distance, headingCosine(m_stamps[i], dx, dy, distance));
            cover = std::max(cover, mark.paint * mark.shape);
        }
    }
    if (cover <= 0.0f || (!m_wet && !m_coarseWet))
        return;
    const std::size_t index = static_cast<std::size_t>(indexIn(laying.coord, x, y));
    Reach& reach = *laying.reach;
    Channel* px = laying.tile->px.data() + index * 4;
    if (reach.held[index] <= 0.0f) {
        reach.under[index] = readPixel(px);
        reach.paint[index] = reach.under[index];
        reach.held[index] = 1.0f;
    }
    const float strength = std::min(1.0f, m_blurPressed * std::min(cover, 1.0f));
    reach.value[index] = std::max(reach.value[index], strength);
    reach.paint[index] = lerp(reach.under[index], m_wet ? m_wet->at(x, y) : m_coarseWet->at(x, y), reach.value[index]);
    writePixel(px, reach.paint[index]);
}

// The stroke's paint over what was under it. With Preserve Opacity, only the colour changes. Paint with a colour never thins what's
// there, however thin it is itself, so a colour over itself leaves no trace; only a transparent colour can.
Premultiplied DabPainter::shown(const Premultiplied& under, const Premultiplied& paint, float coverage) const {
    if (m_paint.mode != LayMode::KeepAlpha) {
        Premultiplied out = lerp(under, paint, coverage);
        if (!m_mixing.transparentColour && out[3] < under[3] && out[3] > 1e-9f) {
            const float up = under[3] / out[3];
            for (float& v : out)
                v *= up;
        }
        return out;
    }
    Premultiplied kept = under;
    for (std::size_t c = 0; c < 3; ++c)
        kept[c] = lerp(under[c], paint[3] > 1e-6f ? paint[c] / paint[3] * under[3] : under[c], coverage);
    return kept;
}

DabPainter::Reach::Reach(bool underAsPixels, bool usesShape, bool usesPassed, const Tile* tileBefore, bool underStaysEmpty, int gridSpacing,
                         int markerSpacing)
    : keepsUnderAsPixels(underAsPixels), before(tileBefore), gridCell(gridSpacing), markerCell(markerSpacing) {
    if (markerSpacing > 0) {
        const std::size_t points =
            static_cast<std::size_t>(Tile::kSize / markerSpacing) * static_cast<std::size_t>(Tile::kSize / markerSpacing);
        pointValue.assign(points, 0.0f);
        pointShape.assign(points, 0.0f);
        pointHeld.assign(points, 0.0f);
        pointPaint.assign(points, Premultiplied{});
        middleValue.assign(points, 0.0f);
        middleShape.assign(points, 0.0f);
        middleHeld.assign(points, 0.0f);
        middlePaint.assign(points, Premultiplied{});
        fineCells.resize(points);
        markerLaidDab.assign(points, 0);
        beforeValue.assign(points, 0.0f);
        beforeShape.assign(points, 0.0f);
        beforeHeld.assign(points, 0.0f);
        beforePaint.assign(points, Premultiplied{});
    } else if (gridSpacing > 0) {
        const std::size_t points =
            static_cast<std::size_t>(Tile::kSize / gridSpacing) * static_cast<std::size_t>(Tile::kSize / gridSpacing);
        gridValue.assign(points, 0.0f);
        gridHeld.assign(points, 0.0f);
        gridPaint.assign(points, Premultiplied{});
        if (gridSpacing > 1) {  // a pixel apart, every pixel is a point and nothing is read between them
            gridCover.assign(points, 0.0f);
            gridLaid.assign(points, 0.0f);
            gridDab.assign(points, 0);
            gridWhole.assign(points, 0);
            gridMelt.assign(points, 0.0f);
            fineValue.share(allZeros().data());
        }
    } else {
        value.own();
        held.own();
        paint.own();
    }
    if (usesShape && markerSpacing == 0)
        shape.own();
    if (usesPassed)
        passed.own();
    if (underStaysEmpty) {
        under.share(kNothing.data());
    } else if (!keepsUnderAsPixels) {
        under.own();
        // On a grid wider than a pixel what's under is kept from the start, the layer as it was, so nothing has to note when a pixel is
        // first reached: until it melts it's what a first visit would keep.
        if ((gridSpacing > 1 || markerSpacing > 0) && tileBefore)
            for (std::size_t i = 0; i < static_cast<std::size_t>(Tile::kSize) * Tile::kSize; ++i)
                under[i] = readPixel(tileBefore->px.data() + i * 4);
    }
}

Premultiplied DabPainter::Reach::underAt(std::size_t index) const {
    if (!keepsUnderAsPixels)
        return under[index];
    return before ? readPixel(before->px.data() + index * 4) : Premultiplied{};
}

void DabPainter::Reach::keepUnder(std::size_t index, const Channel* px) {
    if (under.owned())
        under[index] = readPixel(px);
}

// Whether what's under this tile's pixels stays empty, so no array is needed: there's nothing in the layer, and the Marker never melts
// anything into nothing; Water Color only as long as the blur it mixes toward is nothing too (blurHasPaint).
bool DabPainter::underCanStayEmpty(TileCoord coord) const {
    if (m_mixing.kind != TipMixing::Kind::Marker && m_mixing.kind != TipMixing::Kind::WaterColor)
        return false;
    return !m_before.tile(coord.x, coord.y) && !(m_mixing.kind == TipMixing::Kind::WaterColor && m_wetHasPaint);
}

bool DabPainter::blurHasPaint() const {
    const auto anyPaint = [](const Pixels& pixels) {
        const auto count = static_cast<std::ptrdiff_t>(pixels.area.width()) * pixels.area.height();
        return std::any_of(pixels.values.begin(), pixels.values.begin() + count,
                           [](const Premultiplied& p) { return p != Premultiplied{}; });
    };
    return (m_wet && anyPaint(*m_wet)) || (m_coarseWet && anyPaint(m_coarseWet->cells));
}

// Only the Brush leaves what's under as it was: Water Color, the Marker and Blur melt it, and the Smudge keeps no Reach.
bool DabPainter::keepsUnderAsPixels() const {
    return m_mixing.kind == TipMixing::Kind::Brush;
}

DabPainter::Reach* DabPainter::reachAt(TileCoord coord) const {
    const auto found = m_reach.find(coord);
    return found == m_reach.end() ? nullptr : found->second.get();
}

// Water Color blurs what's under a dab, as far as the dab covers it and as much as it mixes, before any colour goes in. At Blending 0
// it doesn't, so it lays the same pixels as the Brush. The blur is worked out once per dab, over what's there before it, and only
// where it's read: across, on the dab's columns of every row it reaches; down, on the dab itself. Each way it's blurPasses passes of
// the box, one for Water Color and the Marker; Blur's is a Gaussian as wide as three, since a box as wide as a pattern's repeat
// flattens it whole, so Blur Width only took contrast away, as Density does.
DabPainter::Pixels DabPainter::blurUnder(const Dab& dab, double radii) const {
    const int passes = std::max(1, m_mixing.blurPasses);
    if (m_mixing.gaussian) {
        // As wide as the passes of the box: each has a variance of reach (reach + 1) / 3.
        const double reach = m_stamps[dab.first].radius * radii;
        const double sigma = std::max(kLeastSigma, std::sqrt(passes * reach * (reach + 1.0) / 3.0));
        const int far = static_cast<int>(std::ceil(kGaussianReach * sigma));
        Pixels rows = readAround(dab.area.adjusted(-far, -far, far, far));
        Pixels across = sized(QRect(dab.area.left(), rows.area.top(), dab.area.width(), rows.area.height()));
        gaussianBlur(rows, across, sigma, true);
        recycle(std::move(rows));
        Pixels down = sized(dab.area);
        gaussianBlur(across, down, sigma, false);
        recycle(std::move(across));
        return down;
    }
    const int reach = std::max(1, static_cast<int>(std::lround(m_stamps[dab.first].radius * radii)));
    const int far = reach * passes;
    Pixels rows = readAround(dab.area.adjusted(-far, -far, far, far));
    for (int pass = 1; pass < passes; ++pass) {
        Pixels next = sized(rows.area);
        boxBlur(rows, next, reach, true);
        recycle(std::exchange(rows, std::move(next)));
    }
    Pixels across = sized(QRect(dab.area.left(), rows.area.top(), dab.area.width(), rows.area.height()));
    boxBlur(rows, across, reach, true);
    recycle(std::move(rows));
    for (int pass = 1; pass < passes; ++pass) {
        Pixels next = sized(across.area);
        boxBlur(across, next, reach, false);
        recycle(std::exchange(across, std::move(next)));
    }
    Pixels down = sized(dab.area);
    boxBlur(across, down, reach, false);
    recycle(std::move(across));
    return down;
}

// A Marker's melt blur reaching kCoarseReachInCells times a few pixels or more is worked out on cells that wide; 1 is every
// pixel, as for every other tool.
// Blur's stamps kWideBlurFrom across and wider blur on cells kBlurReachInCells times smaller than their reach; Water Color's
// kWideWaterFrom and wider, kCoarseReachInCells times, as the Marker's.
int DabPainter::coarseCellFor(const Dab& dab, double radii) const {
    const int reach = std::max(1, static_cast<int>(std::lround(m_stamps[dab.first].radius * radii)));
    if (m_mixing.kind == TipMixing::Kind::Blur && 2.0 * m_tip.radius >= kWideBlurFrom)
        return std::max(1, reach / kBlurReachInCells);
    if (m_mixing.kind == TipMixing::Kind::WaterColor && 2.0 * m_tip.radius >= kWideWaterFrom)
        return std::max(1, reach / kCoarseReachInCells);
    if (m_mixing.kind != TipMixing::Kind::Marker || m_mixing.blurPasses != 1)
        return 1;
    return reach / kCoarseReachInCells;
}

// What's under the dab, as far round as the blur reaches, averaged over cells, and blurred across and down on them: the blur's reach
// in cells is what it is in pixels over the cell's width.
DabPainter::CoarseBlur DabPainter::coarseBlurUnder(const Dab& dab, double radii) const {
    const int reach = std::max(1, static_cast<int>(std::lround(m_stamps[dab.first].radius * radii)));
    const int cell = coarseCellFor(dab, radii);
    const QRect area = dab.area.adjusted(-reach, -reach, reach, reach);
    const auto cellOf = [cell](int v) { return v >= 0 ? v / cell : -((-v + cell - 1) / cell); };
    const QRect cells(QPoint(cellOf(area.left()), cellOf(area.top())), QPoint(cellOf(area.right()), cellOf(area.bottom())));
    Pixels averaged = sized(cells);
    const auto row = [&](int i) {
        const int cy = cells.top() + i;
        TileUnder last;
        for (int cx = cells.left(); cx <= cells.right(); ++cx)
            averaged.at(cx, cy) = cellAverage(cx, cy, cell, last);
    };
    if (m_onWorkers && cells.height() > 1)
        WorkerPool::forStrokes().run(cells.height(), row);
    else
        for (int i = 0; i < cells.height(); ++i)
            row(i);
    // As many passes of the box each way as the tool blurs with: one for the Marker, three for Blur.
    const int cellReach = std::max(1, static_cast<int>(std::lround(static_cast<double>(reach) / cell)));
    Pixels blurred = std::move(averaged);
    for (const bool across : {true, false})
        for (int pass = 0; pass < std::max(1, m_mixing.blurPasses); ++pass) {
            Pixels next = sized(cells);
            boxBlur(blurred, next, cellReach, across);
            recycle(std::exchange(blurred, std::move(next)));
        }
    CoarseBlur blur{std::move(blurred), cell, dab.area, {}, {}};
    for (int x = dab.area.left(); x <= dab.area.right(); ++x)
        blur.columns.push_back(blur.between(x, cells.left(), cells.right()));
    for (int y = dab.area.top(); y <= dab.area.bottom(); ++y)
        blur.rows.push_back(blur.between(y, cells.top(), cells.bottom()));
    return blur;
}

// Looked up again only for another tile than the last place's. A pen's fade lays several dabs an event at a wide Marker's end, and
// looking up each place's tile was most of their blur.
const DabPainter::TileUnder& DabPainter::tileUnder(int tx, int ty, TileUnder& last) const {
    if (!last.found || last.tx != tx || last.ty != ty)
        last = {tx, ty, true, m_layer.tile(tx, ty), reachAt(TileCoord{tx, ty})};
    return last;
}

Premultiplied DabPainter::cellAverage(int cx, int cy, int cell, TileUnder& last) const {
    Premultiplied total{};
    for (int j = 0; j < kCellSamples; ++j) {
        const int y = cy * cell + (2 * j + 1) * cell / (2 * kCellSamples);
        const int ty = TiledImage::tileOf(y);
        for (int i = 0; i < kCellSamples; ++i) {
            const int x = cx * cell + (2 * i + 1) * cell / (2 * kCellSamples);
            const int tx = TiledImage::tileOf(x);
            const TileUnder& under = tileUnder(tx, ty, last);
            const Premultiplied p = blurSourceAt(under.tile, under.reach, tx, ty, x, y);
            for (std::size_t c = 0; c < 4; ++c)
                total[c] += p[c];
        }
    }
    for (float& v : total)
        v /= kCellSamples * kCellSamples;
    return total;
}

// Between the cells' centres, read along a straight line each way.
DabPainter::CoarseBlur::Between DabPainter::CoarseBlur::between(int v, int firstCell, int lastCell) const {
    const double g = (v + 0.5) / cell - 0.5;
    const int first = std::clamp(static_cast<int>(std::floor(g)), firstCell, lastCell - 1);
    return {first, static_cast<float>(std::clamp(g - first, 0.0, 1.0))};
}

// Only the dab's own pixels are laid, so only they are read, each from its column's and row's place between the cells.
Premultiplied DabPainter::CoarseBlur::at(int x, int y) const {
    const Between& across = columns[static_cast<std::size_t>(x - dab.left())];
    const Between& down = rows[static_cast<std::size_t>(y - dab.top())];
    const int x0 = across.first, y0 = down.first;
    return lerp(lerp(cells.at(x0, y0), cells.at(x0 + 1, y0), across.share),
                lerp(cells.at(x0, y0 + 1), cells.at(x0 + 1, y0 + 1), across.share), down.share);
}

DabPainter::Pixels DabPainter::sized(const QRect& area) const {
    const std::size_t count = static_cast<std::size_t>(area.width()) * static_cast<std::size_t>(area.height());
    std::vector<Premultiplied> values;
    if (!m_spare.empty()) {
        const auto largest =
            std::max_element(m_spare.begin(), m_spare.end(), [](const auto& a, const auto& b) { return a.size() < b.size(); });
        values = std::move(*largest);
        m_spare.erase(largest);
    }
    if (values.size() < count)
        values.resize(count);
    return {area, std::move(values)};
}

// Only its values are kept, moved out of it.
void DabPainter::recycle(Pixels&& pixels) const {  // NOLINT(cppcoreguidelines-rvalue-reference-param-not-moved)
    if (!pixels.values.empty())
        m_spare.push_back(std::move(pixels.values));
}

void DabPainter::recycle(std::optional<Pixels>& pixels) const {
    if (pixels)
        recycle(std::move(*pixels));
    pixels.reset();
}

// Water Color's paint carries the detail of what's under it, blurred: the blur at each pixel less a wider blur, which is smooth from
// pixel to pixel and nothing over an even colour. It comes in as much as the dab mixes, for the part of its paint that isn't brush
// colour. If that takes the paint past opaque, the whole of it is scaled back, so its colour keeps its hue.
Premultiplied DabPainter::withWhatsUnder(const Premultiplied& paint, const Premultiplied& blurred, const Premultiplied& wide) const {
    Premultiplied local = paint;
    for (std::size_t c = 0; c < 4; ++c)
        local[c] += (1.0f - m_dabShare) * m_wetness * (blurred[c] - wide[c]);
    return wholeColour(local);
}

// A premultiplied colour brought back within bounds without changing its hue: past opaque it's scaled back as a whole.
Premultiplied DabPainter::wholeColour(Premultiplied colour) {
    if (colour[3] <= 0.0f)
        return {};
    if (colour[3] > 1.0f) {
        const float over = colour[3];
        for (float& v : colour)
            v /= over;
    }
    for (std::size_t c = 0; c < 3; ++c)
        colour[c] = std::clamp(colour[c], 0.0f, colour[3]);
    return colour;
}

// What's under the stroke moves toward the blur. The stroke's own paint doesn't, so where the stroke covers fully its paint shows as it
// is, and red carried into blue stays red.
void DabPainter::soften(Reach& reach, std::size_t index, const Premultiplied& blurred, float cover) const {
    if (cover <= 0.0f)
        return;
    if (m_mixing.meltsOwnPaint) {  // and with meltsOwnPaint its own paint too, so the pixel as shown moves that far toward the blur
        if (reach.gridCell == 0)
            reach.paint[index] = lerp(reach.paint[index], blurred, cover);
        else if (reach.gridCell == 1)
            reach.gridPaint[index] = lerp(reach.gridPaint[index], blurred, cover);
    }
    if (!reach.under.owned())
        return;
    softenUnder(reach.under[index], blurred, cover);
}

void DabPainter::softenUnder(Premultiplied& under, const Premultiplied& blurred, float cover) const {
    if (!m_mixing.meltColourOnly || blurred[3] <= 1e-6f) {
        if (!m_mixing.meltColourOnly)
            under = lerp(under, blurred, cover);
        return;
    }
    for (std::size_t c = 0; c < 3; ++c)
        under[c] = lerp(under[c], blurred[c] / blurred[3] * under[3], cover);
}

// The rows are read on the workers: each fills only its own row, and only reads the layer and what was under the stroke.
DabPainter::Pixels DabPainter::readAround(const QRect& area) const {
    Pixels pixels = sized(area);
    const auto one = [&](int i) { readRow(pixels, area.top() + i); };
    const std::int64_t count = static_cast<std::int64_t>(area.width()) * area.height();
    if (m_onWorkers && count >= kParallelPixels && area.height() > 1)
        WorkerPool::forStrokes().run(area.height(), one);
    else
        for (int i = 0; i < area.height(); ++i)
            one(i);
    return pixels;
}

// A tile at a time along the row, since looking each pixel up would cost more than reading it. Where the stroke has been, it reads what
// was under the stroke, so the blur never spreads the stroke's own paint.
void DabPainter::readRow(Pixels& pixels, int y) const {
    const int ty = TiledImage::tileOf(y);
    for (int x = pixels.area.left(); x <= pixels.area.right();) {
        const int tx = TiledImage::tileOf(x);
        const int end = std::min(pixels.area.right(), (tx + 1) * Tile::kSize - 1);
        const Tile* tile = m_layer.tile(tx, ty);
        const Reach* reach = reachAt(TileCoord{tx, ty});
        for (; x <= end; ++x)
            pixels.at(x, y) = blurSourceAt(tile, reach, tx, ty, x, y);
    }
}

// What the melt's blur reads: what was under the stroke, or with meltsOwnPaint the layer as it shows now, the stroke's paint in it.
Premultiplied DabPainter::blurSourceAt(const Tile* tile, const Reach* reach, int tx, int ty, int x, int y) const {
    if (!m_mixing.meltsOwnPaint)
        return underAt(tile, reach, tx, ty, x, y);
    return tile ? readPixel(tile->at(x - tx * Tile::kSize, y - ty * Tile::kSize)) : Premultiplied{};
}

Premultiplied DabPainter::underAt(const Tile* tile, const Reach* reach, int tx, int ty, int x, int y) {
    const std::size_t index = static_cast<std::size_t>(indexIn(TileCoord{tx, ty}, x, y));
    // On a coarser grid the record holds what's under from the start, its own or the shared empty one: never the tile, which has the
    // stroke's paint.
    const bool kept = reach && (reach->gridCell > 1 || reach->markerCell > 0 ? true
                                : reach->gridCell == 1                       ? reach->gridValue[index] > 0.0f
                                                                             : reach->value[index] > 0.0f);
    if (kept)
        return reach->under[index];
    return tile ? readPixel(tile->at(x - tx * Tile::kSize, y - ty * Tile::kSize)) : Premultiplied{};
}

Premultiplied& DabPainter::Pixels::at(int x, int y) {
    return values[static_cast<std::size_t>(y - area.top()) * area.width() + (x - area.left())];
}

const Premultiplied& DabPainter::Pixels::at(int x, int y) const {
    return values[static_cast<std::size_t>(y - area.top()) * area.width() + (x - area.left())];
}

// One pass of a box blur, across or down, averaging what's inside `from`'s area, kept where `to`'s area has it. It keeps a running
// sum along each whole line of `from`, so it costs the same however wide the blur, and a line is the same whichever part is kept. The
// lines are independent, so a wide dab's run on the stroke's workers.
void DabPainter::boxBlur(const Pixels& from, Pixels& to, int reach, bool across) const {
    const int lines = across ? to.area.height() : to.area.width();
    const auto one = [&](int i) { blurLine(from, to, reach, across, (across ? to.area.top() : to.area.left()) + i); };
    const std::int64_t pixels = static_cast<std::int64_t>(from.area.width()) * from.area.height();
    if (m_onWorkers && pixels >= kParallelPixels && lines > 1)
        WorkerPool::forStrokes().run(lines, one);
    else
        for (int i = 0; i < lines; ++i)
            one(i);
}

// The line is a row at `at` across, or a column at `at` down. It walks the line by a stride through both blocks, adding up in the same
// order as one pixel at a time would.
void DabPainter::blurLine(const Pixels& from, Pixels& to, int reach, bool across, int at) {
    const int length = across ? from.area.width() : from.area.height();
    const std::ptrdiff_t fromStride = across ? 1 : from.area.width();
    const Premultiplied* line =
        from.values.data() + (across ? std::ptrdiff_t(at - from.area.top()) * from.area.width() : std::ptrdiff_t(at - from.area.left()));
    // The part of the line `to` keeps, as places along it, and where the first of them goes in `to`.
    const int firstKept = across ? to.area.left() - from.area.left() : to.area.top() - from.area.top();
    const int lastKept = firstKept + (across ? to.area.width() : to.area.height()) - 1;
    const std::ptrdiff_t toStride = across ? 1 : to.area.width();
    Premultiplied* out =
        to.values.data() + (across ? std::ptrdiff_t(at - to.area.top()) * to.area.width() : std::ptrdiff_t(at - to.area.left()));
    // Adding and taking away a value is what adding it times +1 and -1 was: the same sums, to the bit.
    double sum0 = 0.0, sum1 = 0.0, sum2 = 0.0, sum3 = 0.0;
    int count = 0;
    for (int i = 0; i <= std::min(reach, length - 1); ++i) {
        const Premultiplied& p = line[i * fromStride];
        sum0 += p[0];
        sum1 += p[1];
        sum2 += p[2];
        sum3 += p[3];
        ++count;
    }
    for (int i = 0; i < length; ++i) {
        if (i >= firstKept && i <= lastKept) {
            Premultiplied& kept = out[(i - firstKept) * toStride];
            kept[0] = static_cast<float>(sum0 / count);
            kept[1] = static_cast<float>(sum1 / count);
            kept[2] = static_cast<float>(sum2 / count);
            kept[3] = static_cast<float>(sum3 / count);
        }
        if (i + reach + 1 < length) {
            const Premultiplied& p = line[(i + reach + 1) * fromStride];
            sum0 += p[0];
            sum1 += p[1];
            sum2 += p[2];
            sum3 += p[3];
            ++count;
        }
        if (i - reach >= 0) {
            const Premultiplied& p = line[(i - reach) * fromStride];
            sum0 -= p[0];
            sum1 -= p[1];
            sum2 -= p[2];
            sum3 -= p[3];
            --count;
        }
    }
}

// Blur's Gaussian across or down, kept where `to`'s area has it; the lines go on the stroke's workers as the box's do.
void DabPainter::gaussianBlur(const Pixels& from, Pixels& to, double sigma, bool across) const {
    const int lines = across ? to.area.height() : to.area.width();
    const auto one = [&](int i) { gaussianLine(from, to, sigma, across, (across ? to.area.top() : to.area.left()) + i); };
    const std::int64_t pixels = static_cast<std::int64_t>(from.area.width()) * from.area.height();
    if (m_onWorkers && pixels >= kParallelPixels && lines > 1)
        WorkerPool::forStrokes().run(lines, one);
    else
        for (int i = 0; i < lines; ++i)
            one(i);
}

// A recursive Gaussian (Young and van Vliet, 1995): one pass forward and one back along the whole line, each feeding back its last
// three values, so it costs the same however wide. Both ends start as if the line went on in its end value. It filters the values
// plus kGaussianLift, which comes back off at the end, as the gains add up to 1: its tails never decay into denormals, which
// cost a hundred times as much as a plain number.
void DabPainter::gaussianLine(const Pixels& from, Pixels& to, double sigma, bool across, int at) {
    const int length = across ? from.area.width() : from.area.height();
    const std::ptrdiff_t fromStride = across ? 1 : from.area.width();
    const Premultiplied* line =
        from.values.data() + (across ? std::ptrdiff_t(at - from.area.top()) * from.area.width() : std::ptrdiff_t(at - from.area.left()));
    const int firstKept = across ? to.area.left() - from.area.left() : to.area.top() - from.area.top();
    const int lastKept = firstKept + (across ? to.area.width() : to.area.height()) - 1;
    const std::ptrdiff_t toStride = across ? 1 : to.area.width();
    Premultiplied* out =
        to.values.data() + (across ? std::ptrdiff_t(at - to.area.top()) * to.area.width() : std::ptrdiff_t(at - to.area.left()));
    // Young and van Vliet's recursive Gaussian (1995): their q from σ and their coefficients, as published.
    const double q = sigma >= 2.5 ? 0.98711 * sigma - 0.96330 : 3.97156 - 4.14554 * std::sqrt(1.0 - 0.26891 * sigma);
    const double b0d = 1.57825 + 2.44413 * q + 1.4281 * q * q + 0.422205 * q * q * q;
    const double b1d = (2.44413 * q + 2.85619 * q * q + 1.26661 * q * q * q) / b0d;
    const double b2d = -(1.4281 * q * q + 1.26661 * q * q * q) / b0d;
    const double b3d = 0.422205 * q * q * q / b0d;
    const float b1 = static_cast<float>(b1d), b2 = static_cast<float>(b2d), b3 = static_cast<float>(b3d);
    const float gain = 1.0f - (b1 + b2 + b3);  // of the floats themselves, so an even colour stays that colour
    thread_local std::vector<Premultiplied> forward;
    forward.resize(static_cast<std::size_t>(length));
    Premultiplied w1, w2, w3;
    for (std::size_t c = 0; c < 4; ++c)
        w1[c] = w2[c] = w3[c] = line[0][c] + kGaussianLift;
    for (int i = 0; i < length; ++i) {
        Premultiplied& w = forward[static_cast<std::size_t>(i)];
        const Premultiplied& x = line[i * fromStride];
        for (std::size_t c = 0; c < 4; ++c) {
            const float next = gain * (x[c] + kGaussianLift) + b1 * w1[c] + b2 * w2[c] + b3 * w3[c];
            w[c] = next;
            w3[c] = w2[c];
            w2[c] = w1[c];
            w1[c] = next;
        }
    }
    Premultiplied y1 = forward.back(), y2 = y1, y3 = y1;
    for (int i = length - 1; i >= 0; --i) {
        Premultiplied y;
        for (std::size_t c = 0; c < 4; ++c) {
            const float next = gain * forward[static_cast<std::size_t>(i)][c] + b1 * y1[c] + b2 * y2[c] + b3 * y3[c];
            y[c] = next;
            y3[c] = y2[c];
            y2[c] = y1[c];
            y1[c] = next;
        }
        if (i >= firstKept && i <= lastKept) {
            Premultiplied& kept = out[(i - firstKept) * toStride];
            for (std::size_t c = 0; c < 4; ++c)
                kept[c] = y[c] - kGaussianLift;
        }
    }
}
