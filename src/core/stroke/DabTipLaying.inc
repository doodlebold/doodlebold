// Part of DabTip.cpp, which includes it where it stands: which tiles and rows a dab lays over, its outlines, coverage and brush forms.

// A tile the stroke reaches for the first time gets its Reach, cleared, and a tile of the layer that's shown, and so shared, has to be
// copied before it's written to; for a wide dab that's many at once. Both are made on the workers, each into its own place, and put
// in the maps here: the same tiles writableTile would make one by one.
// Whether a stamp's outline, a pixel to spare, comes into the tile: a tile none comes into keeps every pixel as it is.
bool DabPainter::reaches(const QRect& tile) const {
    if (m_tip.skirtNeighbours > 0) {  // a skirted dab leaves alone what it lays nothing on
        for (const Outline& outline : m_layOutlines) {
            const auto near = [&](QPointF c, double outer) {
                const double dx = std::max({tile.left() - c.x(), c.x() - (tile.right() + 1.0), 0.0});
                const double dy = std::max({tile.top() - c.y(), c.y() - (tile.bottom() + 1.0), 0.0});
                const double reach =
                    outer + kTileSpare +
                    (m_gridCell > kGridOne ? kReadReachCells * static_cast<float>(m_gridCell) : 0.0);  // and the margin's tiles
                return dx * dx + dy * dy < reach * reach;
            };
            if (near(outline.centre, outline.outer) && (outline.cutOuter < 0.0 || near(outline.cutCentre, outline.cutOuter)))
                return true;
        }
        return false;
    }
    for (const Outline& outline : m_outlines) {
        const double dx = std::max({tile.left() - outline.centre.x(), outline.centre.x() - (tile.right() + 1.0), 0.0});
        const double dy = std::max({tile.top() - outline.centre.y(), outline.centre.y() - (tile.bottom() + 1.0), 0.0});
        const double reach = outline.outer + kTileSpare;
        if (dx * dx + dy * dy < reach * reach)
            return true;
    }
    return false;
}

// Whether the dab writes every pixel of the tile from the Reach alone. Then the tile's pixels aren't read, and a copy of them, for a frame
// still showing the tile, would be wasted (a wide dab's middle is most of its tiles, and every tile is shown between dabs). No pixel
// may be new to the stroke, as those read the tile for what's under them, and all of the tile must lie where one stamp's mark is whole:
// from there each tool's laying writes it. Marks grow as a pixel nears a stamp's centre, so the tile's corners decide for all of it.
bool DabPainter::writesWhole(const Dab& dab, TileCoord coord, Reach& reach) const {
    const bool marker = m_mixing.kind == TipMixing::Kind::Marker && !m_mixing.transparentColour;
    const bool blur = m_mixing.kind == TipMixing::Kind::Blur;
    if (blur && !m_wet && !m_coarseWet)
        return false;
    if (!reach.keepsUnderAsPixels && !reach.allReached) {  // the Brush's under is the layer's as it was, so a new pixel reads nothing here
        const auto positive = [](float v) { return v > 0.0f; };
        if (reach.gridCell == 1 ? !std::all_of(reach.gridValue.begin(), reach.gridValue.end(), positive)
                                : reach.gridCell == 0 && reach.markerCell == 0 &&
                                      !std::all_of((marker || blur ? reach.held : reach.value).begin(),
                                                   (marker || blur ? reach.held : reach.value).end(), positive))
            return false;
        reach.allReached = true;  // these only ever grow; a coarser grid's record keeps what's under from the start
    }
    const QRect tile = coord.pixelRect().adjusted(-kWholeMargin, -kWholeMargin, kWholeMargin, kWholeMargin);
    for (std::size_t k = 0; k < dab.count; ++k) {
        const Stamp& stamp = m_stamps[dab.first + k];
        const PixelFrame& frame = marker ? m_markerStamps[k].frame : m_wholeStamps[k].frame;
        float solid = marker ? m_markerStamps[k].solidSquared : m_wholeStamps[k].solidSquared;
        if (blur)
            solid = std::min(solid, static_cast<float>(stamp.radius * stamp.radius));
        if (solid <= 0.0f || (marker ? m_markerStamps[k].laidWhole : m_wholeStamps[k].paint) <= 0.0f)
            continue;
        const float dx = std::max(std::abs(frame.dx(tile.left())), std::abs(frame.dx(tile.right())));
        const float dy = std::max(std::abs(frame.dy(tile.top())), std::abs(frame.dy(tile.bottom())));
        if (dx * dx + dy * dy < solid)
            return true;
    }
    return false;
}

std::vector<DabPainter::Laying> DabPainter::layingsFor(const QRect& area, bool onlyReached, const Dab* dab) {
    std::vector<std::pair<std::unique_ptr<Reach>*, TileCoord>> fresh;
    std::vector<TileCoord> coords;
    struct Copy {
        TileCoord coord;
        TilePtr from;  // null for a tile the layer doesn't have yet
        TilePtr made;
        bool writtenWhole = false;
    };
    std::vector<Copy> copies;
    for (int ty = TiledImage::tileOf(area.top()); ty <= TiledImage::tileOf(area.bottom()); ++ty) {
        for (int tx = TiledImage::tileOf(area.left()); tx <= TiledImage::tileOf(area.right()); ++tx) {
            if (onlyReached && !reaches(TileCoord{tx, ty}.pixelRect()))
                continue;
            std::unique_ptr<Reach>& reach = m_reach[TileCoord{tx, ty}];
            if (!reach)
                fresh.emplace_back(&reach, TileCoord{tx, ty});
            coords.push_back({tx, ty});
            const auto tile = m_layer.tiles().find(TileCoord{tx, ty});
            if (tile == m_layer.tiles().end() || !tile->second)
                copies.push_back({{tx, ty}, nullptr, nullptr});
            else if (tile->second.use_count() > 1)
                copies.push_back({{tx, ty}, tile->second, nullptr, dab && reach && writesWhole(*dab, TileCoord{tx, ty}, *reach)});
        }
    }
    const auto makeOne = [&](int i) {
        if (i < static_cast<int>(fresh.size())) {
            const auto& [slot, coord] = fresh[static_cast<std::size_t>(i)];
            *slot = std::make_unique<Reach>(keepsUnderAsPixels(), m_mixing.kind != TipMixing::Kind::Blur,
                                            m_mixing.passLength > 0.0 || m_mixing.outlineAlongPath, m_before.tile(coord.x, coord.y),
                                            underCanStayEmpty(coord), m_gridCell, m_markerCell);
            return;
        }
        Copy& copy = copies[static_cast<std::size_t>(i) - fresh.size()];
        copy.made = copy.writtenWhole ? std::make_shared<Tile>(Tile::Unset{})
                    : copy.from       ? std::make_shared<Tile>(*copy.from)
                                      : std::make_shared<Tile>();
    };
    const int count = static_cast<int>(fresh.size() + copies.size());
    if (m_onWorkers && count > 1)
        WorkerPool::forStrokes().run(count, makeOne);
    else
        for (int i = 0; i < count; ++i)
            makeOne(i);
    for (Copy& copy : copies) {
        copy.from.reset();
        m_layer.setTile(copy.coord.x, copy.coord.y, std::move(copy.made));
    }
    std::vector<Laying> layings;
    layings.reserve(coords.size());
    for (const TileCoord coord : coords) {
        Reach* reach = m_reach.at(coord).get();
        if (m_wetHasPaint && !reach->under.owned())
            reach->under.own();  // what the blur moves in stops it being empty
        reach->lastDab = m_dabNumber;
        layings.push_back({coord, m_layer.writableTile(coord.x, coord.y), reach});
    }
    return layings;
}

void DabPainter::layInTile(const Dab& dab, const Premultiplied& paint, Laying& laying) const {
    const QRect area = dab.area.intersected(laying.coord.pixelRect());
    layRows(dab, paint, laying, area.top(), area.bottom());
}

// Rows `top` to `bottom` of the dab, in the laying's tile.
// Rows `top` to `bottom` of the dab, in the laying's tile. A pixel no stamp reaches is left as it is by any tool's laying, so only the
// part of each row the stamps reach is visited.
void DabPainter::layRows(const Dab& dab, const Premultiplied& paint, Laying& laying, int top, int bottom) const {
    const QRect area = dab.area.intersected(laying.coord.pixelRect());
    // Which stamps can reach this band, and which are whole over all of it: a pixel asks only those. Inside every stamp's whole mark a
    // band's pixels get the same marks, added in the same order, so they're worked out once.
    const bool blurOrSmudge = m_mixing.kind == TipMixing::Kind::Blur || m_mixing.kind == TipMixing::Kind::Smudge;
    std::vector<BandStamp> stamps;
    if (!blurOrSmudge)
        stampsOverBand(dab, QRect(area.left(), top, area.width(), bottom - top + 1), stamps);
    const bool whole = !blurOrSmudge && stamps.size() == dab.count &&
                       std::all_of(stamps.begin(), stamps.end(), [](const BandStamp& s) { return s.whole; });
    const bool marker = m_mixing.kind == TipMixing::Kind::Marker && !m_mixing.transparentColour;
    const bool unevened = std::all_of(stamps.begin(), stamps.end(), [](const BandStamp& s) { return s.unevened; });
    const std::optional<StampMark> mark = whole && unevened && !marker ? wholeDabMark(dab) : std::nullopt;
    MarkerClimb climb;
    for (int y = top; y <= bottom; ++y) {
        const auto [left, right] = reachedOnRow(area, y);
        for (int x = left; x <= right; ++x)
            layPixel(dab, paint, laying, x, y, mark ? &*mark : nullptr, whole ? &climb : nullptr, stamps);
    }
}

// Marks only grow toward a stamp's centre, so the band's nearest place to it and its farthest decide.
void DabPainter::stampsOverBand(const Dab& dab, const QRect& band, std::vector<BandStamp>& out) const {
    const bool marker = m_mixing.kind == TipMixing::Kind::Marker && !m_mixing.transparentColour;
    out.clear();
    for (std::size_t k = 0; k < dab.count; ++k) {
        const PixelFrame& frame = marker ? m_markerStamps[k].frame : m_wholeStamps[k].frame;
        const float solid = marker ? m_markerStamps[k].solidSquared : m_wholeStamps[k].solidSquared;
        const float outside = marker ? m_markerStamps[k].outsideSquared : m_wholeStamps[k].reachSquared;  // its skirt's too
        const float dxA = frame.dx(band.left()), dxB = frame.dx(band.right()), dyA = frame.dy(band.top()), dyB = frame.dy(band.bottom());
        const float nearX = dxA <= 0.0f && dxB >= 0.0f ? 0.0f : std::min(std::abs(dxA), std::abs(dxB));
        const float nearY = dyA <= 0.0f && dyB >= 0.0f ? 0.0f : std::min(std::abs(dyA), std::abs(dyB));
        if (nearX * nearX + nearY * nearY >= outside)
            continue;
        const float farX = std::max(std::abs(dxA), std::abs(dxB)), farY = std::max(std::abs(dyA), std::abs(dyB));
        const float farthest = farX * farX + farY * farY;
        out.push_back({static_cast<std::uint16_t>(k), farthest < solid && farthest < outside,
                       unevenedOver(m_stamps[dab.first + k], dxA, dxB, dyA, dyB)});
    }
}

// dabMark's sum where every stamp is whole and none is evened over the band (the caller sees to that past evenFrom); empty if one is evened
// from its middle, or the outline adds up along the path, as then it differs by pixel.
std::optional<StampMark> DabPainter::wholeDabMark(const Dab& dab) const {
    if (m_mixing.outlineAlongPath)
        return std::nullopt;
    StampMark total;
    for (std::size_t i = dab.first; i < dab.first + dab.count; ++i) {
        const WholeStamp& whole = m_wholeStamps[i - dab.first];
        if (m_stamps[i].chordRadius > 0.0f && m_stamps[i].evenFrom <= 0.0f)
            return std::nullopt;
        total.paint += whole.paint;
        total.shape = std::max(total.shape, 1.0f);
        if (m_mixing.passLength > 0.0)
            total.passed += whole.passed;
    }
    return total;
}

void DabPainter::prepareOutlines(const Dab& dab) {
    m_outlines.clear();
    m_layOutlines.clear();
    const auto around = static_cast<std::size_t>(std::max(m_tip.skirtNeighbours, 0));
    for (std::size_t i = dab.first; i < dab.first + dab.count; ++i) {
        const Stamp& stamp = m_stamps[i];
        m_outlines.push_back({stamp.centre, std::max(reachOf(stamp), stamp.formReach), QPointF(), -1.0});
        m_layOutlines.push_back({stamp.centre, layReachOf(stamp), QPointF(), -1.0});
        if (stamp.skirt <= 0.0f)
            continue;
        const std::size_t from = i >= around ? i - around : 0, to = std::min(i + around + 1, m_stamps.size());
        for (std::size_t j = from; j < to; ++j)
            if (j != i)
                m_layOutlines.push_back({stamp.centre, reachOf(stamp), m_stamps[j].centre, outerOf(m_stamps[j])});
    }
}

bool DabPainter::Outline::onRow(double y, double spare, double& left, double& right) const {
    const auto across = [&](QPointF c, double r, double& from, double& to) {
        const double reach = r + spare;
        const double dy = y - c.y();
        if (std::abs(dy) >= reach)
            return false;
        const double half = std::sqrt(reach * reach - dy * dy);
        from = c.x() - half;
        to = c.x() + half;
        return true;
    };
    if (!across(centre, outer, left, right))
        return false;
    if (cutOuter < 0.0)
        return true;
    double cutLeft = 0.0, cutRight = 0.0;
    if (!across(cutCentre, cutOuter, cutLeft, cutRight))
        return false;
    left = std::max(left, cutLeft);
    right = std::min(right, cutRight);
    return left <= right;
}

std::pair<int, int> DabPainter::laysOnRow(int y) const {
    double left = 1e300, right = -1e300;
    for (const Outline& outline : m_layOutlines) {
        double from = 0.0, to = 0.0;
        if (!outline.onRow(y + 0.5, kRowSpare, from, to))
            continue;
        left = std::min(left, from);
        right = std::max(right, to);
    }
    if (left > right)
        return {0, -1};
    return {static_cast<int>(std::floor(left)) - 1, static_cast<int>(std::ceil(right)) + 1};
}

void DabPainter::prepareRows(const Dab& dab, float widen) {
    m_rowsTop = dab.area.top();
    m_rows.assign(static_cast<std::size_t>(dab.area.height()), RowReach{});
    const auto one = [&](int i) {
        const int y = m_rowsTop + i;
        RowReach& row = m_rows[static_cast<std::size_t>(i)];
        if (m_tip.skirtNeighbours > 0) {  // as reaches
            for (const Outline& outline : m_layOutlines) {
                double from = 0.0, to = 0.0;
                if (outline.onRow(y + 0.5, kRowSpare, from, to)) {
                    row.reachLeft = std::min(row.reachLeft, from);
                    row.reachRight = std::max(row.reachRight, to);
                }
                if (outline.onRow(y + 0.5, kRowSpare + widen, from, to)) {
                    row.readLeft = std::min(row.readLeft, from);
                    row.readRight = std::max(row.readRight, to);
                }
            }
            std::tie(row.laysFrom, row.laysTo) = laysOnRow(y);
            return;
        }
        for (const Outline& outline : m_outlines) {
            // reachedOnRow's, and showGridRows' with what a changed point is read by, in the same steps.
            const double centreY = y + 0.5;
            const double reach = outline.outer + kRowSpare;
            const double dy = centreY - outline.centre.y();
            if (std::abs(dy) < reach) {
                const double half = std::sqrt(reach * reach - dy * dy);
                row.reachLeft = std::min(row.reachLeft, outline.centre.x() - half);
                row.reachRight = std::max(row.reachRight, outline.centre.x() + half);
            }
            const double reachOut = outline.outer + kRowSpare + widen;
            const double dyOut = y + 0.5 - outline.centre.y();
            if (std::abs(dyOut) < reachOut) {
                const double half = std::sqrt(reachOut * reachOut - dyOut * dyOut);
                row.readLeft = std::min(row.readLeft, outline.centre.x() - half);
                row.readRight = std::max(row.readRight, outline.centre.x() + half);
            }
        }
        std::tie(row.laysFrom, row.laysTo) = laysOnRow(y);
    };
    const int rows = static_cast<int>(m_rows.size());
    if (m_onWorkers && rows >= kRowsPerJob * kLeastRowJobs)
        WorkerPool::forStrokes().run((rows + kRowsPerJob - 1) / kRowsPerJob, [&](int job) {
            for (int i = job * kRowsPerJob; i < std::min(rows, (job + 1) * kRowsPerJob); ++i)
                one(i);
        });
    else
        for (int i = 0; i < rows; ++i)
            one(i);
}

std::pair<int, int> DabPainter::reachedOnRow(const QRect& area, int y) const {
    const double centreY = y + 0.5;
    double left = 1e300, right = -1e300;
    if (m_tip.skirtNeighbours > 0) {  // as reaches
        for (const Outline& outline : m_layOutlines) {
            double from = 0.0, to = 0.0;
            if (outline.onRow(centreY, kRowSpare, from, to)) {
                left = std::min(left, from);
                right = std::max(right, to);
            }
        }
        if (left > right)
            return {area.left(), area.left() - 1};
        return {std::max(area.left(), static_cast<int>(std::floor(left)) - 1),
                std::min(area.right(), static_cast<int>(std::ceil(right)) + 1)};
    }
    for (const Outline& outline : m_outlines) {
        const double reach = outline.outer + kRowSpare;
        const double dy = centreY - outline.centre.y();
        if (std::abs(dy) >= reach)
            continue;
        const double half = std::sqrt(reach * reach - dy * dy);
        left = std::min(left, outline.centre.x() - half);
        right = std::max(right, outline.centre.x() + half);
    }
    if (left > right)
        return {area.left(), area.left() - 1};
    return {std::max(area.left(), static_cast<int>(std::floor(left)) - 1), std::min(area.right(), static_cast<int>(std::ceil(right)) + 1)};
}

// The most shape any stamp gave the pixel, or with passLength, its soft edges added up over the passes, if that's more. One straight
// pass never lays more than passLength outer radii of path over a pixel, so it keeps the most. Passes add up to less than 1, so a
// pixel a stamp has covered whole is whole.
float DabPainter::shapeOf(const Reach& reach, std::size_t index) const {
    const float most = reach.shape[index];
    if ((m_mixing.passLength <= 0.0 && !m_mixing.outlineAlongPath) || most >= 1.0f)
        return most;
    return std::max(most, m_mixing.outlineAlongPath ? -keptLaneExpm1<0>(-reach.passed[index]) : -keptExpm1<0>(-reach.passed[index]));
}

// The stroke's coverage at a pixel, as the merge of a stamped stroke would give it: from m_fullFrom it's full.
float DabPainter::coverageOf(float value, float shape) const {
    if (shape <= 0.0f || value <= 0.0f)
        return 0.0f;
    const float perShape = value / shape;
    if (perShape >= m_fullFrom)
        return m_paint.opacity * shape;
    const float curve = m_mixing.outlineAlongPath ? -keptLaneExpm1<1>(-perShape) : -keptExpm1<1>(-perShape);
    return m_paint.opacity * shape * std::min(1.0f, curve / m_paint.fullAt);
}

// The dab adds to the stroke's coverage at the pixel, and mixes its paint into the stroke's paint there by as much as it lays, all of
// it the first time.
void DabPainter::layPixel(const Dab& dab, const Premultiplied& paint, Laying& laying, int x, int y, const StampMark* wholeMark,
                          MarkerClimb* climb, const std::vector<BandStamp>& band) const {
    if (m_mixing.kind == TipMixing::Kind::Marker && !m_mixing.transparentColour) {
        layMarkerPixel(dab, paint, laying, x, y, climb, band);
        return;
    }
    if (m_mixing.kind == TipMixing::Kind::Blur) {
        layBlurPixel(dab, laying, x, y);
        return;
    }
    const bool oneStroke = m_tip.form && m_tip.form->oneStroke();
    float oneStrokeValue = 0.0f;  // a shape laid as one stroke: what it adds to the coverage; its stamps are never whole
    const StampMark mark = wholeMark ? *wholeMark : dabMark(dab, x, y, &band, true, oneStroke ? &oneStrokeValue : nullptr);
    if (mark.shape <= 0.0f || mark.paint <= 0.0f)
        return;
    const std::size_t index = static_cast<std::size_t>(indexIn(laying.coord, x, y));
    Reach& reach = *laying.reach;
    Channel* px = laying.tile->px.data() + index * 4;
    // The first time the stroke reaches the pixel, what's there is what's under the stroke.
    const bool first = reach.value[index] <= 0.0f;
    if (first)
        reach.keepUnder(index, px);
    const auto show = [&] {
        writePixel(px, shown(reach.underAt(index), reach.paint[index], coverageOf(reach.value[index], shapeOf(reach, index))));
    };
    // Water Color: what's under melts toward its blur, as far as the dab covers the pixel, 1 - exp(-paint) of its shape, times the melt.
    const bool wet = m_wet || m_coarseWet;
    const auto wetAt = [&] { return m_wet ? m_wet->at(x, y) : m_coarseWet->at(x, y); };
    if (wet) {
        const float melt = meltOf(mark);
        soften(reach, index, wetAt(), melt);
    }
    if (m_mixing.layOnlyAbove && onlyMoving(dab.pressure)) {
        show();
        return;
    }
    // The dab's paint mixes into the stroke's by as much as it lays, then adds to the coverage and outline.
    const bool detail = wet && m_mixing.wetDetail;
    const float laid = mark.paint * static_cast<float>(layingAt(dab.pressure));
    const auto wideAt = [&] { return m_wide ? m_wide->at(x, y) : m_coarseWide->at(x, y); };
    mixInto(reach, index, detail ? withWhatsUnder(paint, wetAt(), wideAt()) : paint, laid, first ? 0.0f : reach.held[index]);
    reach.value[index] += oneStroke ? oneStrokeValue * static_cast<float>(layingAt(dab.pressure)) : laid;
    // A shape laid as one stroke keeps its outline, its tone, with the soft Shapes' outline along the path too.
    const float most = oneStroke ? mark.shape : mostOf(mark.shape);
    if (most > reach.shape[index])
        reach.shape[index] = most;
    addPasses(reach, index, mark.passed);
    show();
}

// The stroke's paint at a pixel takes on a dab's paint by as much as the dab lays there against what the pixel holds already, so where
// a dab lays little, at its edge, it changes little. All of it the first time.
StampMark DabPainter::formedMark(const Stamp& stamp, float dx, float dy) const {
    StampMark mark;
    const float distance = std::sqrt(dx * dx + dy * dy);
    if (m_tip.form->fixedToCanvas()) {  // a blot map (StampLayer::formedMarkOf)
        const int x = static_cast<int>(std::floor(stamp.centre.x() + dx)), y = static_cast<int>(std::floor(stamp.centre.y() + dy));
        // Water Color takes the stamp tools' rule: each Shape's window, its share laid as paint inside the round stamp, so the picture
        // shows through the whole dab and builds up where dabs overlap (blotmix readings, Mess. 26 %: Water Color 54.96, 77.28, 67.24
        // and 57.27 levels off → 42.76, 65.15, 27.70 and 12.25). The Brush's 5th Shape took it too, but the reference's lays
        // nothing between its blots: it takes a window as its outline, as its soft Shapes do, its own a
        // little wider (StampForm::BlotWindow::BrushFifth), the eight Blots cases 80.3 → 15.1 levels off.
        if (m_mixing.kind == TipMixing::Kind::WaterColor && m_tip.edge < 4) {
            const float share =
                m_tip.form->blotShape(distance, (stamp.nominalRadius > 0.0f ? stamp.nominalRadius : stamp.radius), x, y, m_tip.edge);
            const StampMark round = distance < outerOf(stamp) ? stampMark(stamp, m_tip.edge, distance) : StampMark{};
            mark.shape = round.shape;
            // Water Color's picture shows harder: the reference's blot nearly halves its mean alpha (5th Shape ramp 142 → 77, C stroke
            // 104 → 51), where the window's share alone took ours 143 → 127. So its share goes to kWaterBlotPower: on the 5th
            // Shape 42.76 / 65.15 levels off → 25.10 / 35.98, on the 2nd 27.70 / 12.25 → 20.36 / 10.43. The 1st, 3rd and 4th, unread, take
            // the 2nd's.
            const float taken = std::pow(share, m_tip.edge >= 4 ? kWaterBlotPower[1] : kWaterBlotPower[0]);
            mark.paint = round.shape > 0.0f ? taken * round.paint : 0.0f;
            return mark;
        }
        mark.shape = m_tip.form->blotShape(distance, (stamp.nominalRadius > 0.0f ? stamp.nominalRadius : stamp.radius), x, y, m_tip.edge,
                                           blotWindow());
        // On the 1st to 4th Shapes the blot is held inside the round stamp's soft outline, which already reaches well past the radius:
        // the Brush's 2nd Shape 20.03 and 15.47 levels off → 11.47 and 14.48, the Marker's 1st and 3rd 31.44 and 19.23 → 23.39 and 12.76.
        // On the 5th Shape the cone reaches past it, as the reference's mask does, which tears the Pastel Marker's edge over paint.
        if (m_tip.edge < 4 && mark.shape > 0.0f)
            mark.shape *= distance < outerOf(stamp) ? stampMark(stamp, m_tip.edge, distance).shape : 0.0f;
        const float bySamples = std::max(0.0f, 1.0f + kWaterBlotBySamples * (stamp.penSamplesAcross - kWaterBlotSamplesAt));
        mark.paint = mark.shape > 0.0f ? (m_mixing.kind == TipMixing::Kind::WaterColor ? kWaterBlotTone * bySamples : 1.0f) : 0.0f;
        // The Brush's soft Shapes: the picture reaches into the body as Strength grows: from the outline alone, as above, to
        // the stamp tools' own rule (each Shape's window, its share laid as paint, the outline whole), smoothly between kBrushBlotBodyFrom
        // and kBrushBlotBodyTo.
        if (m_mixing.kind == TipMixing::Kind::Brush && m_tip.edge < 4) {
            const float t = std::clamp((m_tip.form->strength() - kBrushBlotBodyFrom) / (kBrushBlotBodyTo - kBrushBlotBodyFrom), 0.0f, 1.0f);
            const float body = t * t * (3.0f - 2.0f * t);
            if (body > 0.0f) {
                // The cut as crisp on the softer Shapes as on the 5th: only the stroke's own edge softens.
                const float share = m_tip.form->blotShape(distance, (stamp.nominalRadius > 0.0f ? stamp.nominalRadius : stamp.radius), x, y,
                                                          std::max(m_tip.edge, kBrushBlotCutShape));
                const StampMark whole{share, share > 0.0f ? 1.0f : 0.0f, 0.0f};
                mark.paint = mark.paint + (whole.paint - mark.paint) * body;
                mark.shape = mark.shape + (whole.shape - mark.shape) * body;
            }
        }
        return mark;
    }
    const StampForm& form = *m_tip.form;
    if (form.stamped()) {  // a brush shape: the picture whole, its darkness times Dens. what it lays, over what's there (StampTip::pictureStamps)
        mark.paint = form.at(dx, dy, stamp.radius, stamp.turnCos, stamp.turnSin) * form.strength();
        mark.shape = mark.paint > 0.0f ? 1.0f : 0.0f;
        return mark;
    }
    if (distance < outerOf(stamp))
        mark = stampMark(stamp, m_tip.edge, distance, headingCosine(stamp, dx, dy, distance));
    const float picture = stamp.hairs ? hairCoverage(stamp, dx, dy) : form.at(dx, dy, stamp.radius);
    mark.shape = mark.shape + (picture - mark.shape) * form.strength();
    mark.paint = mark.paint + (1.0f - mark.paint) * form.strength();
    return mark;
}

// The window a mixing tool's dab takes a blot map's outline through: the Brush's 5th Shape's own, else the mixing tools'.
StampForm::BlotWindow DabPainter::blotWindow() const {
    if ((m_mixing.kind == TipMixing::Kind::Brush || m_mixing.kind == TipMixing::Kind::WaterColor) && m_tip.edge >= 4)
        return StampForm::BlotWindow::BrushFifth;
    return m_mixing.kind == TipMixing::Kind::Marker && m_tip.edge < 4 ? StampForm::BlotWindow::MarkerSoft : StampForm::BlotWindow::Mixing;
}

float DabPainter::formShapeAt(const Stamp& stamp, float dx, float dy) const {
    if (!m_tip.form)
        return 1.0f;
    if (m_tip.form->fixedToCanvas()) {  // a blot map: its outline against the round one's whole
        const int x = static_cast<int>(std::floor(stamp.centre.x() + dx)), y = static_cast<int>(std::floor(stamp.centre.y() + dy));
        return m_tip.form->blotShape(std::sqrt(dx * dx + dy * dy), (stamp.nominalRadius > 0.0f ? stamp.nominalRadius : stamp.radius), x, y,
                                     m_tip.edge, blotWindow());
    }
    if (m_tip.form->stamped())
        return m_tip.form->at(dx, dy, stamp.radius, stamp.turnCos, stamp.turnSin) * m_tip.form->strength();
    return 1.0f + ((stamp.hairs ? hairCoverage(stamp, dx, dy) : m_tip.form->at(dx, dy, stamp.radius)) - 1.0f) * m_tip.form->strength();
}

void DabPainter::mixInto(Reach& reach, std::size_t index, const Premultiplied& paint, float laid, float heldBefore) const {
    reach.paint[index] = heldBefore <= 0.0f ? paint : lerp(reach.paint[index], paint, paintShare(heldBefore, laid));
    reach.held[index] = heldBefore + laid;
}

// How much of a pixel's paint a dab's takes the place of, the pixel holding `held` density before it lays `laid`: as much as it lays
// against what the pixel holds, up to the dab's memory. With depositsTowardPaint, so that the pixel moves the dab's share of the
// way toward its paint, what it lays covers over what the pixel covers after it.
// How far what's under melts at a pixel: 1 - exp(-paint) of the outline's share there, times the melt. With meltsOwnPaint a pixel the outline
// covers only in part melts as one inside does: it's the stamp's water, not its outline, so the side of a line gone over below full pressure
// doesn't keep a darker pixel, softened less than those beside it.
float DabPainter::meltOf(const StampMark& mark) const {
    if (m_mixing.meltsOwnPaint)
        return mark.shape > 0.0f ? std::min(-std::expm1(-mark.paint / mark.shape) * m_melt, 1.0f) : 0.0f;
    return std::min(mark.shape * -std::expm1(-mark.paint) * m_melt, 1.0f);
}

float DabPainter::paintShare(float held, float laid) const {
    if (m_mixing.depositsTowardPaint)
        return static_cast<float>(std::expm1(-double(laid)) / std::expm1(-double(held + laid)));
    return laid / (std::min(held, m_dabMemory) + laid);
}

namespace {

int floorDiv(int v, int by) {
    return v >= 0 ? v / by : -((-v + by - 1) / by);
}

}
