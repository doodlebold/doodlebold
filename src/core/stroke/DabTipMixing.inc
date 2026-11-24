// Part of DabTip.cpp, which includes it where it stands: a dab's mark at a pixel, its pickup of what's under, and the paint it mixes and
// carries.

// The dab's stamps added up at a pixel, as a stamped stroke's are: density times shape, and the most shape.
// Inside a stamp's solid reach its mark is whole, so what it adds there is the dab's own number for it, added in the same order.
StampMark DabPainter::dabMark(const Dab& dab, int x, int y, const std::vector<BandStamp>* band, bool withPasses, float* oneStroke) const {
    StampMark total;
    const std::size_t stamps = band ? band->size() : dab.count;
    // With a skirt, each stamp's distance squared, for skirtAt to take again: below 0 for a stamp whole over the band, which it passes.
    std::array<float, kMostSkirtedBand> distances;
    const bool keepDistances = m_tip.skirtNeighbours > 0 && stamps <= distances.size();
    for (std::size_t j = 0; j < stamps; ++j) {
        const std::size_t i = dab.first + (band ? (*band)[j].k : j);
        const WholeStamp& whole = m_wholeStamps[i - dab.first];
        // With the outline along the path, a stamp's whole mark still adds its passes, by the pixel's distance, until they make it whole.
        const auto alongPasses = [&] {
            if (total.passed >= kWholeAlongPasses)
                return 0.0f;
            const float px = whole.frame.dx(x), py = whole.frame.dy(y);
            return passOf(m_stamps[i], whole, std::sqrt(px * px + py * py), 1.0f);
        };
        if (band && (*band)[j].whole && (*band)[j].unevened) {
            // Whole over the band, as the test below finds at each of its pixels: it adds the dab's own number for it.
            if (keepDistances)
                distances[j] = -1.0f;
            total.paint += whole.paint;
            total.shape = std::max(total.shape, 1.0f);
            if (m_mixing.outlineAlongPath)
                total.passed += alongPasses();
            else if (m_mixing.passLength > 0.0 && withPasses)
                total.passed += whole.passed;
            continue;
        }
        const float dx = whole.frame.dx(x), dy = whole.frame.dy(y);
        const float distanceSquared = dx * dx + dy * dy;
        if (keepDistances)
            distances[j] = distanceSquared;
        if (distanceSquared >= whole.outsideSquared)
            continue;  // past its outline: limitedMark gives nothing there
        if (m_tip.form) {  // a form: the round mark and the picture, a pixel at a time
            const StampMark mark = formedMark(m_stamps[i], dx, dy);
            if (mark.shape <= 0.0f || mark.paint <= 0.0f)
                continue;
            if (oneStroke) {  // its outline the stroke's tone there, no passes to raise that, and the coverage it adds apart
                const float across = acrossFactor(m_stamps[i], dx, dy);
                const float outline = oneStrokeOutline(m_stamps[i].opacity, m_tip.pictureFullAt, m_tip.form->strength());
                total.paint += densityOf(m_stamps[i].opacity, mark.paint) * across;
                total.shape = std::max(total.shape, outline);
                *oneStroke += oneStrokeDensity(m_tip.pictureFullAt, mark.paint / m_tip.form->strength()) * outline * across;
                continue;
            }
            total.paint += densityOf(m_stamps[i].opacity, mark.paint) * mark.shape * acrossFactor(m_stamps[i], dx, dy);
            total.shape = std::max(total.shape, mark.shape);
            if ((m_mixing.passLength > 0.0 && withPasses) || m_mixing.outlineAlongPath)
                total.passed += passOf(m_stamps[i], whole, std::sqrt(distanceSquared), mark.shape);
            continue;
        }
        if (distanceSquared < whole.solidSquared) {
            total.paint += whole.paint * acrossFactor(m_stamps[i], dx, dy);
            total.shape = std::max(total.shape, 1.0f);
            if (m_mixing.outlineAlongPath)
                total.passed += alongPasses();
            else if (m_mixing.passLength > 0.0 && withPasses)
                total.passed += whole.passed;
            continue;
        }
        const float distance = std::sqrt(distanceSquared);
        const StampMark mark = limitedMark(m_stamps, i, m_tip.edge, distance, headingCosine(m_stamps[i], dx, dy, distance));
        if (mark.shape <= 0.0f || mark.paint <= 0.0f)
            continue;
        // Density adds up, times the share of the pixel the outline covers; the outline itself keeps the most, and adds its passes.
        total.paint += densityOf(m_stamps[i].opacity, mark.paint) * mark.shape * acrossFactor(m_stamps[i], dx, dy);
        total.shape = std::max(total.shape, mark.shape);
        if ((m_mixing.passLength > 0.0 && withPasses) || m_mixing.outlineAlongPath)
            total.passed += passOf(m_stamps[i], whole, distance, mark.shape);
    }
    if (m_tip.skirtNeighbours > 0)
        total.paint += skirtAt(dab, x, y, band, total.shape, false, keepDistances ? distances.data() : nullptr);
    return total;
}

// What's under the dab's middle stamp, or its newest with pickUpAtPen, colour and alpha, averaged over the places inside its outline. A pixel that's nearly transparent
// counts as empty.
Premultiplied DabPainter::pickUp(const Dab& dab) const {
    if (m_mixing.kind == TipMixing::Kind::Marker && !m_mixing.transparentColour)  // the reference's Marker: one average over its whole mask
        return averageOverMask(dab, [this](int x, int y) { return shownAt(x, y); });
    return m_mixing.pickUpEachStamp ? pickUpAt(dab, 0) : averageUnder(dab, [this](int x, int y) { return shownAt(x, y); });
}

// One average over every place any of the dab's stamps touches at all, each weighing the same, a faint rim's as much as a solid
// middle's; on a grid pickUpAcross to a stamp's diameter, so a wide dab costs no more than a narrow one.
template <typename Read> Premultiplied DabPainter::averageOverMask(const Dab& dab, Read read) const {
    QRect area;
    double diameter = 1.0;
    for (std::size_t k = 0; k < dab.count; ++k) {
        const Stamp& stamp = m_stamps[dab.first + k];
        area |= areaOf(stamp);
        diameter = std::max(diameter, 2.0 * static_cast<double>(stamp.radius));
    }
    const double step = std::max(1.0, diameter / static_cast<double>(m_mixing.pickUpAcross));
    Premultiplied total{};
    float weight = 0.0f;
    // The sample places step by adding, as the brush pixels were saved with: x0 + i * step would round differently.
    for (double y = area.top() + 0.5; y <= area.bottom(); y += step) {  // NOLINT(bugprone-float-loop-counter)
        for (double x = area.left() + 0.5; x <= area.right(); x += step) {  // NOLINT(bugprone-float-loop-counter)
            bool touched = false;
            for (std::size_t k = 0; k < dab.count && !touched; ++k) {
                const Stamp& stamp = m_stamps[dab.first + k];
                const float distance = static_cast<float>(std::hypot(x - stamp.centre.x(), y - stamp.centre.y()));
                touched = distance < outerOf(stamp) && stampMark(stamp, m_tip.edge, distance).shape > 0.0f;
            }
            if (touched)
                addPicked(total, weight, 1.0f, read(static_cast<int>(x), static_cast<int>(y)));
        }
    }
    if (weight <= 0.0f)
        return {};
    for (float& v : total)
        v /= weight;
    return total;
}

// The reference's Marker picks up once an event, before it lays: one average over every pixel the event's mask touches, each weighing
// the same, the mask being what lays its paint: the blot map's cut where there is one, reaching past the round stamp as its cone does, and
// none of the pixels the cut empties; else the round stamps' outlines. On a grid pickUpAcross to a stamp's diameter, as averageOverMask.
Premultiplied DabPainter::pickUpOverEvent(const std::vector<Stamp>& stamps) const {
    QRect area;
    double diameter = 1.0;
    for (const Stamp& stamp : stamps) {
        area |= m_tip.form ? reachAreaOf(stamp) : areaOf(stamp);
        diameter = std::max(diameter, 2.0 * static_cast<double>(stamp.radius));
    }
    const double step = std::max(1.0, diameter / static_cast<double>(m_mixing.pickUpAcross));
    Premultiplied total{};
    float weight = 0.0f;
    // The sample places step by adding, as the brush pixels were saved with: x0 + i * step would round differently.
    for (double y = area.top() + 0.5; y <= area.bottom(); y += step) {  // NOLINT(bugprone-float-loop-counter)
        for (double x = area.left() + 0.5; x <= area.right(); x += step) {  // NOLINT(bugprone-float-loop-counter)
            bool touched = false;
            for (std::size_t k = 0; k < stamps.size() && !touched; ++k) {
                const Stamp& stamp = stamps[k];
                const float dx = static_cast<float>(x - stamp.centre.x()), dy = static_cast<float>(y - stamp.centre.y());
                if (m_tip.form) {
                    touched = formedMark(stamp, dx, dy).shape > 0.0f;
                } else {
                    const float distance = std::sqrt(dx * dx + dy * dy);
                    touched = distance < outerOf(stamp) && stampMark(stamp, m_tip.edge, distance).shape > 0.0f;
                }
            }
            if (touched)
                addPicked(total, weight, 1.0f, shownAt(static_cast<int>(x), static_cast<int>(y)));
        }
    }
    if (weight <= 0.0f)
        return {};
    for (float& v : total)
        v /= weight;
    return total;
}

// What's under the dab's k-th stamp's pickup, as a dab of that stamp alone would pick up: where the pen is, no further than penReach ahead.
Premultiplied DabPainter::pickUpAt(const Dab& dab, std::size_t k) const {
    Dab one = dab;
    one.first = dab.first + k;
    one.count = 1;
    one.pen = dab.pen >= k ? dab.pen - k : 0;
    return averageUnder(one, [this](int x, int y) { return shownAt(x, y); });
}

template <typename Read> Premultiplied DabPainter::averageUnder(const Dab& dab, Read read) const {
    std::size_t at = dab.count / 2;
    if (m_mixing.pickUpAtPen) {  // where the pen is, but no further ahead than penReach diameters
        const auto reach = static_cast<std::size_t>(std::lround(m_mixing.penReach / std::max(m_tip.spacingShare, 1e-3)));
        at = std::min(dab.pen, dab.count - 1 + reach);
    }
    // No further ahead than the stamps there are yet: a stamp alone (pickUpAt) may ask past the last one laid, which read past the end
    // once a blot map took Water Color's skirt, and the stamps it keeps ahead for it, off.
    at = std::min(at, m_stamps.size() - 1 - dab.first);
    Stamp middle = m_stamps[dab.first + at];
    middle.radius = static_cast<float>(middle.radius * m_mixing.pickUpReach);
    const QRect area = areaOf(middle);
    const double step = std::max(1.0, std::max(area.width(), area.height()) / static_cast<double>(m_mixing.pickUpAcross));
    Premultiplied total{};
    float weight = 0.0f;
    // The sample places step by adding, as the brush pixels were saved with: x0 + i * step would round differently.
    for (double y = area.top() + 0.5; y <= area.bottom(); y += step) {  // NOLINT(bugprone-float-loop-counter)
        for (double x = area.left() + 0.5; x <= area.right(); x += step) {  // NOLINT(bugprone-float-loop-counter)
            const float w =
                stampMark(middle, m_tip.edge, static_cast<float>(std::hypot(x - middle.centre.x(), y - middle.centre.y()))).shape;
            if (w > 0.0f)
                addPicked(total, weight, w, read(static_cast<int>(x), static_cast<int>(y)));
        }
    }
    if (weight <= 0.0f)
        return {};
    for (float& v : total)
        v /= weight;
    return total;
}

void DabPainter::addPicked(Premultiplied& total, float& weight, float w, const Premultiplied& under) {
    if (under[3] >= kEmptyAlpha) {
        for (std::size_t c = 0; c < 4; ++c)
            total[c] += w * under[c];
    }
    weight += w;
}

// What it keeps takes on what it picked up, keeping Persistence of itself for a dab of a few stamps and less for more. The first dab
// keeps none, so a stroke starts loaded with what it lands on. Then the brush colour goes in. Below Blur Pressure none does, and
// what's picked up keeps its transparency, so a light touch only moves the paint that's there.
Premultiplied DabPainter::paintFor(const Premultiplied& picked, const Dab& dab) {
    if (m_mixing.kind == TipMixing::Kind::Marker && !m_mixing.transparentColour)
        return markerPaintFor(picked, dab);
    const bool moving = onlyMoving(dab.pressure);
    if (m_mixing.depositsTowardPaint && moving) {  // it lays nothing, and leaves what it carries as it was, for its first firm dab
        m_dabShare = 0.0f;
        return {};
    }
    const double stamps = dab.steps * m_tip.spacingShare / kPersistenceSpacing;
    m_dabMemory = static_cast<float>(memoryFor(dab));
    m_lastDabPressure = dab.pressure;
    m_lastDabCentre = m_stamps[dab.first + dab.count / 2].centre;
    // With pickUpEachStamp it picks up at each of the dab's stamps in turn, and what it carries follows each, as a dab a stamp would; the
    // dab lays what it carries after its last.
    const std::size_t carries = m_mixing.pickUpEachStamp && !moving ? dab.count : 1;
    for (std::size_t k = 0; k < carries; ++k) {
        if (k > 0)
            m_picked = pickUpAt(dab, k);
        const Premultiplied& under = carries > 1 ? m_picked : picked;
        const double these = carries > 1 ? m_stamps[dab.first + k].weight * m_tip.spacingShare / kPersistenceSpacing : stamps;
        constexpr double kLeastThinner =
            0.05;  // so thinKeep's power never meets 0 and a full load over nothing is never held exactly; every value from 0 to 0.07 paints within half a level of it (b50251a)
        const double thinner = std::clamp(1.0 - double(m_carried[3] - under[3]), kLeastThinner, 1.0);
        const double thin = m_mixing.thinKeep > 0.0 ? std::pow(thinner, m_mixing.thinKeep) : 1.0;
        const double keep = m_stampsCarried == 0.0 ? 0.0 : std::pow(persistenceSoFar(), these * thin / persistenceStampsAt(dab.pressure));
        m_stampsCarried += these;
        m_carried = lerp(moving ? movedPaint(under) : asPaint(under, dab), m_carried, static_cast<float>(keep));
    }
    m_dabShare = moving ? 0.0f : static_cast<float>(brushShare(dab.pressure, (carries > 1 ? m_picked : picked)[3]));
    if (m_mixing.shareByCeiling > 0.0) {  // pressure's part of the ceiling, not Density's: Density doesn't touch colour over paint
        double ceiling = std::clamp(dab.ceiling / std::max(m_tip.density, 1e-6), 0.0, 1.0);
        if (m_mixing.shareLag > 0.0) {  // it follows the ceiling, over shareLag stamps
            // It rises with pressure at once, so the brush's colour counts from the first firm stamp; only a fall comes through slowly.
            m_laggingCeiling = m_laggingCeiling < 0.0
                                   ? ceiling
                                   : std::max(ceiling, lerp(m_laggingCeiling, ceiling, -std::expm1(-stamps / m_mixing.shareLag)));
            ceiling = m_laggingCeiling;
        }
        m_dabShare *= static_cast<float>(std::pow(ceiling, m_mixing.shareByCeiling));
    }
    Premultiplied laid = lerp(m_carried, m_own, m_dabShare);
    if (m_mixing.depositsTowardPaint && m_own[3] > 0.0f) {
        // The colour mixes as Blending says, the opacity stays what it carries, none where it carries none.
        const float alpha = m_carried[3];
        for (std::size_t c = 0; c < 3; ++c)
            laid[c] = alpha > kEmptyAlpha ? lerp(m_carried[c] / alpha, m_own[c] / m_own[3], m_dabShare) * alpha : 0.0f;
        laid[3] = alpha > kEmptyAlpha ? alpha : 0.0f;
    }
    if (m_mixing.holdsColour && m_hasLaid)  // a dab lighter than the stroke's firmest changes its colour only that much
        laid = lerp(m_lastLaid, laid, static_cast<float>(std::min(1.0, dab.ceiling / std::max(m_firmest, 1e-6))));
    m_lastLaid = laid;
    m_hasLaid = true;
    return laid;
}

// The Marker in the reference's order: its dab's mix is what it picked up with the brush colour moved in by Blending's share
// (and the share's own following of the ceiling), what it carries moves toward that mix, and it lays what it carries. The brush colour
// goes into the carry, not each lay; the first dab carries its own mix.
Premultiplied DabPainter::markerPaintFor(const Premultiplied& picked, const Dab& dab) {
    const bool moving = onlyMoving(dab.pressure);
    const double stamps = dab.steps * m_tip.spacingShare / kPersistenceSpacing;
    m_dabMemory = static_cast<float>(memoryFor(dab));
    m_lastDabPressure = dab.pressure;
    m_lastDabCentre = m_stamps[dab.first + dab.count / 2].centre;
    m_dabShare = moving ? 0.0f : static_cast<float>(brushShare(dab.pressure, picked[3]));
    if (m_mixing.shareByCeiling > 0.0) {  // pressure's part of the ceiling, not Density's
        double ceiling = std::clamp(dab.ceiling / std::max(m_tip.density, 1e-6), 0.0, 1.0);
        if (m_mixing.shareLag > 0.0) {  // it follows the ceiling, over shareLag stamps; it rises at once
            m_laggingCeiling = m_laggingCeiling < 0.0
                                   ? ceiling
                                   : std::max(ceiling, lerp(m_laggingCeiling, ceiling, -std::expm1(-stamps / m_mixing.shareLag)));
            ceiling = m_laggingCeiling;
        }
        m_dabShare *= static_cast<float>(std::pow(ceiling, m_mixing.shareByCeiling));
    }
    const Premultiplied loaded = moving ? movedPaint(picked) : asPaint(picked, dab);
    const Premultiplied mix = lerp(loaded, m_own, m_dabShare);
    // It keeps Persistence to kMarkerKeepPerStamp of what it carries for each stamp the dab brings, from the first dab on (the reference's
    // carry takes more of the mix the more stamps an event brings).
    // Above Blending 50 % the carry follows what's under it sooner.
    const double keepFaster = 1.0 + (kMarkerKeepFasterAt100 - 1.0) * std::clamp((m_mixing.blending - 0.5) / 0.5, 0.0, 1.0);
    // Laid in groups, a group keeps Persistence to kMarkerKeepPerStamp a stamp, as the dabs before it did.
    double perStamps = kMarkerKeepPerStamp * static_cast<double>(dab.count);
    if (laysEvents()) {  // each stamp counted by its spacing against the old, to kMarkerKeepSpacingPower
        perStamps = 0.0;
        for (std::size_t k = 0; k < dab.count; ++k)
            perStamps += kMarkerKeepPerStamp * std::pow(spacingRatio(m_stamps[dab.first + k]), kMarkerKeepSpacingPower);
    }
    // Laid in groups, a group of more stamps lets go of a little more each stamp (kMarkerKeepCountPower).
    const double byCount = laysEvents() ? std::pow(static_cast<double>(dab.count), kMarkerKeepCountPower - 1.0) : 1.0;
    const double keep = m_stampsCarried == 0.0 ? 0.0 : std::pow(m_mixing.persistence, keepFaster * perStamps * byCount);
    m_stampsCarried += stamps;
    m_carried = lerp(mix, m_carried, static_cast<float>(keep));
    Premultiplied laid = m_carried;
    if (m_mixing.holdsColour && m_hasLaid)  // a dab lighter than the stroke's firmest changes its colour only that much
        laid = lerp(m_lastLaid, laid, static_cast<float>(std::min(1.0, dab.ceiling / std::max(m_firmest, 1e-6))));
    m_lastLaid = laid;
    m_hasLaid = true;
    return laid;
}

// As the pen eases off, the stroke's own paint takes on what the brush lays sooner: its memory comes down toward fallMemory as fast
// as pressure falls along the way, all the way at fallRate per pixel. Steady pressure, and jitter that comes to little along the
// way, keep mixMemory.
double DabPainter::memoryFor(const Dab& dab) const {
    if (m_mixing.fallMemory < 0.0 || m_lastDabPressure < 0.0)
        return m_mixing.mixMemory;
    const QPointF way = m_stamps[dab.first + dab.count / 2].centre - m_lastDabCentre;
    const double travelled = std::max(std::hypot(way.x(), way.y()), 1.0);
    const double falling = std::clamp((m_lastDabPressure - dab.pressure) / travelled / m_mixing.fallRate, 0.0, 1.0);
    return lerp(m_mixing.mixMemory, m_mixing.fallMemory, falling);
}

// From Blur Pressure up, a firmer touch lets go of what it carries sooner, by firmLetGo at full pressure.
double DabPainter::persistenceStampsAt(double pressure) const {
    const double firm = std::clamp((pressure - m_mixing.blurPressure) / std::max(1.0 - m_mixing.blurPressure, 1e-6), 0.0, 1.0);
    return m_mixing.persistenceStamps * lerp(1.0, m_mixing.firmLetGo, firm);
}

// Persistence settles in over the first stamps of a stroke, rather than all at once: the brush holds what it first loaded fully at
// first, and comes down to Persistence as it goes. Its pickup includes the stroke's own paint, so without this it would load its own
// colour at once and lose what it landed on.
double DabPainter::persistenceSoFar() const {
    if (m_mixing.depositsTowardPaint)  // it follows what it picks up from its second stamp, at Persistence's rate
        return m_mixing.persistence;
    return lerp(1.0, m_mixing.persistence, std::min(1.0, m_stampsCarried / kSettleStamps));
}

// Below Blur Pressure what it picks up keeps its transparency, or with emptyIsOwnColour, the empty part counts as the colour it
// carries: on an empty layer its own, and where it started on paint, what it picked up there, so a light start over a colour and
// an empty part lays that colour thinned, not the brush colour.
Premultiplied DabPainter::movedPaint(const Premultiplied& picked) const {
    if (!m_mixing.emptyIsOwnColour)
        return picked;
    const float empty = 1.0f - picked[3];
    const Premultiplied fill = carriedColour(picked);
    return {picked[0] + fill[0] * empty, picked[1] + fill[1] * empty, picked[2] + fill[2] * empty, picked[3] + fill[3] * empty};
}

// The colour it carries, as opaque as its own colour: what it has carried so far, or on its first dab what it picks up, or with
// neither, its own colour.
Premultiplied DabPainter::carriedColour(const Premultiplied& picked) const {
    const Premultiplied& from = m_stampsCarried > 0.0 && m_carried[3] > kEmptyAlpha ? m_carried : picked;
    if (from[3] <= kEmptyAlpha)
        return m_own;
    return {from[0] / from[3] * m_own[3], from[1] / from[3] * m_own[3], from[2] / from[3] * m_own[3], m_own[3]};
}

// Dilution only matters where there's transparency. At 0, picked-up paint counts as opaque; higher, it keeps the layer's
// transparency. Empty canvas has no colour of its own, so it counts as the brush colour.
Premultiplied DabPainter::asPaint(const Premultiplied& picked, const Dab& dab) const {
    const double touch = m_mixing.dilutionFollowsPressure ? pressureResponse(dab.pressure, 100) : 1.0;
    const double diluted = std::min(m_mixing.dilution * touch * dilutionBySamplesAt(dab), 1.0);
    float alpha =
        static_cast<float>(m_mixing.paintCeiling ? std::max(1.0 - diluted, double(picked[3])) : lerp(1.0, double(picked[3]), diluted));
    if (m_mixing.dilutionPower > 0.0)  // what's under, raised by Dilution's share of what's missing
        alpha = static_cast<float>(picked[3] + (1.0 - picked[3]) * std::pow(1.0 - diluted, m_mixing.dilutionPower));
    if (picked[3] > 1e-6f)
        return {picked[0] / picked[3] * alpha, picked[1] / picked[3] * alpha, picked[2] / picked[3] * alpha, alpha};
    if (m_own[3] <= 0.0f)
        return {};
    return {m_own[0] * alpha, m_own[1] * alpha, m_own[2] * alpha, alpha};
}

// How much of what it picks up goes into a dab: Blending through its curve, so 50 % mixes in two-thirds and lays a third of the brush
// colour. Painting with a transparent colour, the Brush and Water Color use Dilution in its place.
double DabPainter::mixingAt(double pressure) const {
    const bool byDilution = m_mixing.transparentColour && m_mixing.kind != TipMixing::Kind::Marker;
    const double setting =
        byDilution ? m_mixing.dilution * (m_mixing.dilutionFollowsPressure ? pressureResponse(pressure, 100) : 1.0)
                   : m_mixing.blending * pressureShare(pressure, m_mixing.blendingFollowsPressure, 0.0, m_mixing.blendingSensitivity);
    return 1.0 - std::pow(1.0 - std::clamp(setting, 0.0, 1.0), kBlendingCurve);
}

// The brush colour's share of a dab. Water Color lays less of it over paint that's there: about half the Brush's, over solid paint at
// Blending 50 %. At Blending 0 the two are the same.
double DabPainter::brushShare(double pressure, double pickedAlpha) const {
    double rest = 1.0 - mixingAt(pressure);
    if (m_mixing.kind == TipMixing::Kind::Marker && m_mixing.blending > 0.5) {
        const auto& curve = kMarkerBlendingCurveAbove50;
        const double b = std::min(m_mixing.blending, curve.back().first);
        double power = curve.back().second;
        for (std::size_t i = 0; i + 1 < curve.size(); ++i)
            if (b <= curve[i + 1].first) {
                power = curve[i].second +
                        (curve[i + 1].second - curve[i].second) * (b - curve[i].first) / (curve[i + 1].first - curve[i].first);
                break;
            }
        rest = std::pow(1.0 - std::clamp(m_mixing.blending *
                                             pressureShare(pressure, m_mixing.blendingFollowsPressure, 0.0, m_mixing.blendingSensitivity),
                                         0.0, 1.0),
                        power);
    }
    if (m_mixing.blendingPower > 0.0)
        rest = std::pow(1.0 - std::clamp(m_mixing.blending *
                                             pressureShare(pressure, m_mixing.blendingFollowsPressure, 0.0, m_mixing.blendingSensitivity),
                                         0.0, 1.0),
                        m_mixing.blendingPower);
    const double power = m_mixing.kind == TipMixing::Kind::WaterColor ? 1.0 + m_mixing.wetHold * pickedAlpha : 1.0;
    return std::pow(rest, power) * colourFadeIn(pressure);
}

// From Blur Pressure up, the brush colour fades in with pressure, slowly at first: at Blur Pressure 50 % it starts to show at about
// two-thirds pressure.
double DabPainter::colourFadeIn(double pressure) const {
    if (m_mixing.blurPressure <= 0.0)
        return 1.0;
    return m_mixing.colourFadeMost *
           std::pow(std::clamp((pressure - m_mixing.blurPressure) / (1.0 - m_mixing.blurPressure), 0.0, 1.0), m_mixing.colourFadePower);
}

// With dilutionBySamples, from dilutionBySamplesFrom pressure Dilution comes in to be times the table's at the dab's pen samples
// within a stamp's diameter, all of it at full pressure.
double DabPainter::dilutionBySamplesAt(const Dab& dab) const {
    const auto& table = m_mixing.dilutionBySamples;
    if (m_mixing.dilutionBySamplesFrom <= 0.0 || table.empty() || dab.samplesAcross <= 0.0)
        return 1.0;
    double times = table.back().times;
    if (dab.samplesAcross <= table.front().samples) {
        times = table.front().times;
    } else {
        for (std::size_t i = 1; i < table.size(); ++i) {
            if (dab.samplesAcross <= table[i].samples) {
                const double t = (dab.samplesAcross - table[i - 1].samples) / (table[i].samples - table[i - 1].samples);
                times = lerp(table[i - 1].times, table[i].times, t);
                break;
            }
        }
    }
    const double firm = std::clamp((dab.pressure - m_mixing.dilutionBySamplesFrom) / (1.0 - m_mixing.dilutionBySamplesFrom), 0.0, 1.0);
    return lerp(1.0, times, firm);
}

// How much of its paint it lays: with layOnlyAbove and a layRamp, it comes in over that much pressure above Blur Pressure.
double DabPainter::layingAt(double pressure) const {
    if (!m_mixing.layOnlyAbove || m_mixing.layRamp <= 0.0)
        return 1.0;
    return std::clamp((pressure - m_mixing.blurPressure) / m_mixing.layRamp, 0.0, 1.0);
}

bool DabPainter::onlyMoving(double pressure) const {
    return m_mixing.blurPressure > 0.0 && pressure < m_mixing.blurPressure;
}
