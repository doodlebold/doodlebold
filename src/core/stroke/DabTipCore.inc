// Part of DabTip.cpp, which includes it where it stands: beginning, adding to and ending a stroke, and laying its dabs one by one.

void DabPainter::begin(const InputSample& first, PressureShares shares) {
    m_walk.begin(first, shares);
    m_eventMemory.clear();
    m_stamps.clear();
    m_painted = 0;
    m_waitingDabs.clear();
    m_groupOutline.clear();
    m_carried = {};
    m_stampsCarried = 0.0;
    m_recentStamps = 1.0;
    m_groupSize = 1;
    m_groupAverage = 1.0;
    m_laggingCeiling = -1.0;
    m_firmest = 0.0;
    m_hasLaid = false;
    m_hasLastCentre = false;
    m_lastDabPressure = -1.0;
    m_dabMemory = static_cast<float>(m_mixing.mixMemory);
}

// The Marker and the Brush laid in groups lay as the reference's: one pen event's stamps, in groups (stage 3). paint() steps through an event a
// group at a time, so a group is never fewer than one stamp, even for an event that brings none.
bool DabPainter::dabPerEvent() const {
    return (m_mixing.kind == TipMixing::Kind::Marker && !m_mixing.transparentColour) || laysBrushGroups();
}

bool DabPainter::laysBrushGroups() const {
    return brushLaidInGroups(m_mixing, formKeepsOffGroups(m_tip.form, m_mixing, m_tip.edge), m_paint.mode);
}

// How many of an event's stamps go into one group, one mask, one pickup, one carry step and one laying, as the reference's:
// as many as recent events have brought (m_recentStamps, rounded down), up to the Shape's kMarkerGroupMost, or the Brush's kBrushGroupMost.
std::size_t DabPainter::groupStamps() const {
    const int edge = std::clamp(m_tip.edge, 0, 4);
    const bool lastShape = edge == 4 && m_mixing.antiRipple;
    const std::size_t most = laysBrushGroups() ? (lastShape ? kBrushGroupMostAntiRipple : kBrushGroupMost[static_cast<std::size_t>(edge)])
                             : lastShape       ? kMarkerGroupMostAntiRipple
                                               : kMarkerGroupMost[static_cast<std::size_t>(edge)];
    return std::clamp(static_cast<std::size_t>(std::floor(m_recentStamps)), std::size_t{1}, most);
}

std::size_t DabPainter::groupMost() const {
    const int edge = std::clamp(m_tip.edge, 0, 4);
    return edge == 4 && m_mixing.antiRipple ? kBrushGroupMostAntiRipple : kBrushGroupMost[static_cast<std::size_t>(edge)];
}

// An event's stamps laid in groups, as the reference's (kBrushGroupFollow).
void DabPainter::paintGroups(const std::vector<Stamp>& stamps, bool ending) {
    const std::size_t most = groupMost();
    if (!stamps.empty()) {
        m_groupAverage += kBrushGroupFollow * (static_cast<double>(stamps.size()) - m_groupAverage);
        if (std::abs(m_groupAverage - static_cast<double>(m_groupSize)) > 1.0)
            m_groupSize = std::clamp(static_cast<std::size_t>(std::lround(m_groupAverage)), std::size_t{1}, most);
    }
    // No group larger than the Shape's most, which its smallest stamp shrinks toward kBrushGroupLeastShare of it below
    // kBrushGroupFullFrom px across.
    const auto limited = [&](std::size_t first, std::size_t wanted) {  // m_stamps from `first`
        double smallest = std::numeric_limits<double>::max();
        for (std::size_t n = 0; n < wanted; ++n) {
            smallest = std::min(smallest, 2.0 * m_stamps[first + n].radius);
            const double share = std::clamp(smallest / kBrushGroupFullFrom, kBrushGroupLeastShare, 1.0);
            const auto cap = std::max<std::size_t>(1, static_cast<std::size_t>(std::lround(static_cast<double>(most) * share)));
            if (n + 1 > cap)
                return std::max<std::size_t>(n, 1);
        }
        return wanted;
    };
    if (m_tip.skirtNeighbours <= 0) {
        m_stamps.insert(m_stamps.end(), stamps.begin(), stamps.end());
        for (std::size_t from = 0; from < stamps.size();) {
            const std::size_t first = m_stamps.size() - (stamps.size() - from);  // old stamps may have been let go
            const std::size_t count = limited(first, std::min(m_groupSize, stamps.size() - from));
            if (from > 0 && m_betweenDabs)
                m_betweenDabs();
            layDab(first, count, stamps.size() - 1 - from);
            from += count;
        }
        return;
    }
    m_stamps.insert(m_stamps.end(), stamps.begin(), stamps.end());
    const std::size_t ahead = ending ? 0 : static_cast<std::size_t>(m_tip.skirtNeighbours);
    std::size_t size = m_groupSize;
    for (bool laid = false; m_stamps.size() > m_painted + ahead; laid = true) {
        if (laid && m_betweenDabs)
            m_betweenDabs();
        const std::size_t wanted = std::min(size, m_stamps.size() - ahead - m_painted);
        const std::size_t count = limited(m_painted, wanted);
        layDab(m_painted, count, m_stamps.size() - 1 - m_painted);
        if (size < most && m_stamps.size() >= m_painted + ahead + size + 1)
            ++size;
    }
}

void DabPainter::add(const InputSample& sample, PressureShares shares) {
    const std::vector<Stamp> stamps = m_walk.add(sample, shares);
    if (laysBrushGroups()) {
        paintGroups(stamps, false);
        return;
    }
    paint(stamps, dabPerEvent() ? groupStamps() : stampsPerDab());
    if (dabPerEvent() && !stamps.empty())  // after its groups: the next event's follow what this one brought
        m_recentStamps += kMarkerRecentFollow * (static_cast<double>(stamps.size()) - m_recentStamps);
}

void DabPainter::addTogether(const std::vector<std::pair<InputSample, PressureShares>>& samples) {
    if (dabPerEvent()) {  // each event its own dab, however many come together, so a stroke paints the same whenever it's shown
        for (const auto& [sample, shares] : samples)
            add(sample, shares);
        return;
    }
    std::vector<Stamp> stamps;
    for (const auto& [sample, shares] : samples) {
        const std::vector<Stamp> laid = m_walk.add(sample, shares);
        stamps.insert(stamps.end(), laid.begin(), laid.end());
    }
    paint(stamps, stampsPerDabWhenBehind());
}

void DabPainter::end() {
    const std::vector<Stamp> stamps = m_walk.end();
    if (laysBrushGroups())
        paintGroups(stamps, true);
    else
        paint(stamps, dabPerEvent() ? groupStamps() : stampsPerDab(), true);
}

TilePtr DabPainter::tile(TileCoord coord) const {
    return m_layer.tileShared(coord.x, coord.y);
}

bool DabPainter::outline(TileCoord coord, float* out) const {
    if (m_markerCell > 0 || m_mixing.kind == TipMixing::Kind::Blur)
        return false;
    if (laysBrushGroups()) {  // laid in groups: the outline its groups kept
        const auto kept = m_groupOutline.find(coord);
        if (kept == m_groupOutline.end())
            return false;
        std::copy(kept->second.begin(), kept->second.end(), out);
        return true;
    }
    // A tile left alone a while may have its record packed away (packIdleReach); a copy is unpacked to read it, the same numbers.
    std::unique_ptr<Reach> unpackedCopy;
    const Reach* found = nullptr;
    if (const auto here = m_reach.find(coord); here != m_reach.end())
        found = here->second.get();
    else if (const auto packed = m_packed.find(coord); packed != m_packed.end())
        found = (unpackedCopy = unpacked(packed->second, coord)).get();
    if (!found)
        return false;
    const Reach& reach = *found;
    const bool passes = reach.passed.data() != nullptr;
    // shapeOf, without its exponential where nothing has passed, which gives the most shape alone.
    for (std::size_t i = 0; i < static_cast<std::size_t>(Tile::kSize * Tile::kSize); ++i)
        out[i] = passes && reach.passed[i] > 0.0f && reach.shape[i] < 1.0f ? shapeOf(reach, i) : reach.shape[i];
    return true;
}

Premultiplied DabPainter::shownAt(int x, int y) const {
    const Premultiplied painted = readPixel(m_layer.pixel(x, y).data());
    if (!m_paint.grain || !m_paint.picksUpThroughPaper || m_paint.paperPerDab)
        return painted;
    const Premultiplied before = readPixel(m_before.pixel(x, y).data());
    float outline = 1.0f;
    const TileCoord coord{TiledImage::tileOf(x), TiledImage::tileOf(y)};
    if (const auto found = m_reach.find(coord); found != m_reach.end() && m_markerCell == 0)
        outline =
            shapeOf(*found->second, static_cast<std::size_t>((y - coord.y * Tile::kSize) * Tile::kSize + (x - coord.x * Tile::kSize)));
    const float way = mixingWayThroughPaper(m_paint, m_paint.grain->shareAt(x, y), before[3], painted[3], outline);
    return lerp(before, painted, way);
}

QRect DabPainter::takeDirty() {
    return std::exchange(m_dirty, QRect());
}

std::vector<TileCoord> DabPainter::takeChangedTiles() {
    std::vector<TileCoord> changed(m_changed.begin(), m_changed.end());
    m_changed.clear();
    return changed;
}

// One event's stamps, in dabs of at most the edge's number of stamps. With a skirt a dab waits until skirtNeighbours more stamps are
// placed after it, or the stroke ends, as its skirts show inside their outlines too; the dabs are the same, painted in the same order,
// so each picks up what it did.
void DabPainter::paint(const std::vector<Stamp>& stamps, std::size_t most, bool ending) {
    // With a blot map the Marker picks up as the reference's does: once an event, before it lays, over every pixel its cut mask touches,
    // which reaches past the round stamp as the blot's cone does. Picking up only inside the round outline read the blue the event's
    // last dabs had laid there and none of the field just past it: the Pastel Marker over the field 48.66 levels off → 33.28, its edge
    // torn as the reference's. The plain Marker keeps its own pickup, which its carry was fitted with: this one leaves its
    // readings over the field 6.0 → 17.0 and 11.3 → 20.9.
    m_eventPicked.reset();
    if (m_tip.form && m_tip.form->fixedToCanvas() && m_mixing.kind == TipMixing::Kind::Marker && !m_mixing.transparentColour &&
        !stamps.empty())
        m_eventPicked = pickUpOverEvent(stamps);
    for (std::size_t from = 0; from < stamps.size(); from += most) {
        const std::size_t count = std::min(most, stamps.size() - from);
        m_stamps.insert(m_stamps.end(), stamps.begin() + static_cast<std::ptrdiff_t>(from),
                        stamps.begin() + static_cast<std::ptrdiff_t>(from + count));
        const std::size_t pen = stamps.size() - 1 - from;  // the event's last stamp, from this dab's first
        if (m_tip.skirtNeighbours > 0) {
            m_waitingDabs.emplace_back(count, pen);
            continue;
        }
        layDab(m_stamps.size() - count, count, pen);
        if (m_betweenDabs && from + most < stamps.size())
            m_betweenDabs();
    }
    const std::size_t ahead = ending ? 0 : static_cast<std::size_t>(std::max(m_tip.skirtNeighbours, 0));
    for (bool laid = false; !m_waitingDabs.empty() && m_painted + m_waitingDabs.front().first + ahead <= m_stamps.size(); laid = true) {
        if (laid && m_betweenDabs)
            m_betweenDabs();
        const auto [count, pen] = m_waitingDabs.front();
        m_waitingDabs.pop_front();
        layDab(m_painted, count, pen);
    }
}

void DabPainter::layDab(std::size_t first, std::size_t count, std::size_t pen) {
    Dab dab{first, count, QRect(), 0.0, 0.0};
    dab.pen = pen;
    double sampled = 0.0;  // stamps with a samplesAcross
    for (std::size_t i = first; i < first + count; ++i) {
        const Stamp& stamp = m_stamps[i];
        dab.area |= reachAreaOf(stamp);
        if (laysEvents())
            dab.area |= markerAreaOf(stamp);
        dab.pressure += stamp.pressure / static_cast<double>(dab.count);
        dab.ceiling += stamp.opacity / static_cast<double>(dab.count);
        dab.steps += stamp.weight;
        if (stamp.samplesAcross > 0.0f) {
            dab.samplesAcross += stamp.samplesAcross;
            sampled += 1.0;
        }
        m_firmest = std::max(m_firmest, double(stamp.opacity));
    }
    if (sampled > 0.0)
        dab.samplesAcross /= sampled;
    // A skirted dab on a grid also shows again the pixels up to kReadReachCells cells past its box, which read its grid points too, so none
    // is left stale until a later dab happens to pass over it.
    if (m_gridCell > kGridOne && m_tip.skirtNeighbours > 0) {
        const int margin = static_cast<int>(kReadReachCells) * m_gridCell;
        dab.area.adjust(-margin, -margin, margin, margin);
    }
    paintDab(dab);
    m_painted = first + count;
    forgetOldStamps();
}

// A Marker wider than kWideDabFrom mixes and melts once for more of its stamps, up to kMostDabGrowth times as many, as the square of
// its size over that: each dab goes over a million pixels at 1000 px, and fewer of them keep up with the pen.
std::size_t DabPainter::stampsPerDab() const {
    const auto& byEdge = m_mixing.kind == TipMixing::Kind::Brush ? kBrushStampsPerDab : kStampsPerDab;
    const std::size_t stamps = byEdge[static_cast<std::size_t>(std::clamp(m_tip.edge, 0, 4))];
    if (m_mixing.stampPerDab)  // each stamp picks up and mixes on its own
        return 1;
    if (m_mixing.kind != TipMixing::Kind::Marker)
        return stamps;
    const std::size_t atOldSpacing = static_cast<std::size_t>(
        std::lround(static_cast<double>(stamps) * kMarkerStampsPerOldStamp));  // it mixes as often along the way as it did
    const double over = 2.0 * m_tip.radius / kWideDabFrom;
    if (over <= 1.0)
        return atOldSpacing;
    return static_cast<std::size_t>(std::lround(static_cast<double>(atOldSpacing) * std::min(over * over, kMostDabGrowth)));
}

// As many stamps as a dab's spacing fits into a share of the stamp's diameter, or the dab's usual count if that's more.
std::size_t DabPainter::stampsPerDabWhenBehind() const {
    const double spacing = m_mixing.kind == TipMixing::Kind::Marker ? markerSpacingShare(m_tip.edge) : m_tip.spacingShare;
    return std::max(stampsPerDab(), static_cast<std::size_t>(std::max(kBehindDabWidthShare / std::max(spacing, 1e-3), 1.0)));
}

void DabPainter::forgetOldStamps() {
    const auto kept =
        static_cast<std::size_t>(std::max(kStampNeighbours, m_tip.skirtNeighbours));  // as neighbours of the next dab's stamps
    while (m_painted > kept) {
        m_stamps.pop_front();
        --m_painted;
    }
}
