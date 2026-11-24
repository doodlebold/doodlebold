// Part of DabTip.cpp, which includes it where it stands: whole stamps, the AVX2 marks, skirts, passes and the Marker's stamps.

namespace {

// Eight pixels' four channels, as Premultiplied in memory, turned into four channels of eight pixels, and back.
void loadEight(const Premultiplied* from, __m256 (&channels)[4]) {
    __m128 lo[4], hi[4];
    for (int i = 0; i < 4; ++i) {
        lo[i] = _mm_loadu_ps(from[i].data());
        hi[i] = _mm_loadu_ps(from[i + 4].data());
    }
    _MM_TRANSPOSE4_PS(lo[0], lo[1], lo[2], lo[3]);
    _MM_TRANSPOSE4_PS(hi[0], hi[1], hi[2], hi[3]);
    for (int c = 0; c < 4; ++c)
        channels[c] = _mm256_set_m128(hi[c], lo[c]);
}

void storeEight(const __m256 (&channels)[4], Premultiplied* to) {
    __m128 lo[4], hi[4];
    for (int c = 0; c < 4; ++c) {
        lo[c] = _mm256_castps256_ps128(channels[c]);
        hi[c] = _mm256_extractf128_ps(channels[c], 1);
    }
    _MM_TRANSPOSE4_PS(lo[0], lo[1], lo[2], lo[3]);
    _MM_TRANSPOSE4_PS(hi[0], hi[1], hi[2], hi[3]);
    for (int i = 0; i < 4; ++i) {
        _mm_storeu_ps(to[i].data(), lo[i]);
        _mm_storeu_ps(to[i + 4].data(), hi[i]);
    }
}

}

// Water Color's eight pixels: as showEight, but what's under is the record's own numbers, and melts first toward the dab's blur by the
// melt read between the columns, as the scalar read-back does it, lane by lane in the same order.
bool DabPainter::showEightWet(Laying& laying, const std::vector<GridColumn>& columns, const std::vector<char>& sharp, int fromLeft,
                              std::size_t index, int intoTile, const Premultiplied* blurred) const {
    Reach& reach = *laying.reach;
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
    // Each lane reads the columns either side of its own cell: one pair for all eight where the points are at least eight apart, and on a
    // closer grid a pair a cell, each read between in the same steps.
    alignas(32) float fxs[kWideLanes];
    alignas(32) float aw[kWideLanes], bw[kWideLanes], ac[kWideLanes], bc[kWideLanes], am[kWideLanes], bm[kWideLanes];
    alignas(32) float ak[4][kWideLanes], bk[4][kWideLanes];
    for (int i = 0; i < kWideLanes; ++i) {
        const std::size_t g = static_cast<std::size_t>((intoTile + i) >> shift);
        const GridColumn& c0 = columns[g];
        const GridColumn& c1 = columns[g + 1];
        if (!c0.whole || !c1.whole)
            return false;
        fxs[i] = static_cast<float>((intoTile + i) & (cell - 1)) * perCell;
        aw[i] = c0.weight, bw[i] = c1.weight, ac[i] = c0.cover, bc[i] = c1.cover, am[i] = c0.melt, bm[i] = c1.melt;
        for (std::size_t c = 0; c < 4; ++c)
            ak[c][i] = c0.kept[c], bk[c][i] = c1.kept[c];
    }
    const __m256 vwb = _mm256_load_ps(fxs), vwa = _mm256_sub_ps(_mm256_set1_ps(1.0f), vwb);
    const auto between = [&](const float* a, const float* b) { return blendEachLane(vwa, vwb, a, b); };
    const __m256 weight = between(aw, bw);
    if (_mm256_movemask_ps(_mm256_cmp_ps(weight, _mm256_set1_ps(kAllReached), _CMP_LT_OQ)) != 0)
        return false;
    const __m256 over = _mm256_div_ps(_mm256_set1_ps(1.0f), weight);
    const __m256 cover = _mm256_mul_ps(between(ac, bc), over);
    const __m256 coverage = _mm256_mul_ps(_mm256_mul_ps(_mm256_set1_ps(m_paint.opacity), shape), cover);
    __m256 u[4];
    loadEight(reach.under.data() + index, u);
    if (blurred) {
        const __m256 melt = between(am, bm);
        __m256 b[4];
        loadEight(blurred, b);
        for (int c = 0; c < 4; ++c)
            u[c] = _mm256_add_ps(u[c], _mm256_mul_ps(_mm256_sub_ps(b[c], u[c]), melt));
        storeEight(u, reach.under.data() + index);
    }
    __m256 o[4];
    for (int c = 0; c < 4; ++c) {
        const __m256 kept = _mm256_mul_ps(between(ak[c], bk[c]), over);
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

// A Marker pixel's opacity climbs toward each stamp's own, its ceiling, times how much of the pixel it covers, and never past it, nor
// down, so it never goes past Density and the stroke never lightens what it laid. A firm stamp gets there at once, so where pressure
// falls the strength steps down; a light one climbs a share of the way, its share of Density, so a light touch
// and a tap lay little. Below full Density no stamp climbs more than Density of the way, so a stroke approaches Density from below,
// as the M2 case has it. Going over its own stroke, it's already
// there, so it doesn't darken, while a second stroke lays over the first. Over paint its colour shows as strongly as its stamps'
// opacities have built up, so a firm touch goes in at full strength and a light one shows what's under.
// What each stamp adds to a pixel where its mark is whole, as dabMark works it out with paint 1 and shape 1.
void DabPainter::prepareWholeStamps(const Dab& dab) {
    m_wholeStamps.assign(dab.count, WholeStamp{});
    for (std::size_t k = 0; k < dab.count; ++k) {
        const Stamp& stamp = m_stamps[dab.first + k];
        WholeStamp& whole = m_wholeStamps[k];
        whole.frame = PixelFrame(stamp.centre);
        // A form's picture isn't even inside, so no part of it is whole.
        const float reach = stamp.formReach > 0.0f ? 0.0f : static_cast<float>(solidReach(m_stamps, dab.first + k)) - kSolidMargin;
        whole.solidSquared = reach > 0.0f ? reach * reach : -1.0f;
        whole.outsideSquared = (layReachOf(stamp) + kSolidMargin) * (layReachOf(stamp) + kSolidMargin);
        const float skirtReach = std::max(reachOf(stamp), stamp.formReach);
        whole.reachSquared = (skirtReach + kSolidMargin) * (skirtReach + kSolidMargin);
        const StampMark mark{1.0f, 1.0f, 0.0f};
        whole.paint = densityOf(stamp.opacity, mark.paint) * mark.shape;
        if (m_mixing.outlineAlongPath)
            whole.along = &alongTable(std::max(double(stamp.rampWidth), 1.0) /
                                      std::max(double(stamp.radius), 1e-3));  // its core's outline is whole anyway
        else if (m_mixing.passLength > 0.0)
            whole.passed = -std::log1p(-std::min(mark.shape, kMostPassedShape)) *
                           static_cast<float>(stamp.step / (m_mixing.passLength * outerOf(stamp)));
    }
    if (m_tip.skirtNeighbours <= 0)
        return;
    const auto around = static_cast<std::size_t>(m_tip.skirtNeighbours);
    m_aroundFirst = dab.first >= around ? dab.first - around : 0;
    const std::size_t end = std::min(dab.first + dab.count + around, m_stamps.size());
    m_around.assign(end - m_aroundFirst, AroundStamp{});
    for (std::size_t i = m_aroundFirst; i < end; ++i) {
        const Stamp& stamp = m_stamps[i];
        AroundStamp& at = m_around[i - m_aroundFirst];
        at.frame = PixelFrame(stamp.centre);
        // A ramp's outline is whole its width in from its outer end (rampAt); a blurred one, or one thinner than a pixel, nowhere exactly.
        const float whole = outerOf(stamp) - std::max(stamp.rampWidth, 1.0f) - kSolidMargin;
        at.wholeSquared = stamp.edgeBlur <= 0.0f && stamp.radius * 2.0f >= 1.0f && whole > 0.0f ? whole * whole : -1.0f;
        at.outerSquared = outerOf(stamp) * outerOf(stamp);
    }
}

// Whether markEight can stand for dabMark over this band: a skirted tip on the grid whose stamps around the dab have plain outlines,
// no rim, no paint ramp and no evening, as the Brush's 5th Shape's, and no more of them than its arrays hold.
bool DabPainter::marksInLanes(const std::vector<BandStamp>& band) const {
    if (m_tip.skirtNeighbours <= 0 || m_gridCell <= kGridOne || m_mixing.outlineAlongPath || band.size() > kMostSkirtedBand ||
        m_around.size() > kMostSkirtedBand)
        return false;
    for (std::size_t at = m_aroundFirst; at < m_aroundFirst + m_around.size(); ++at) {
        const Stamp& stamp = m_stamps[at];
        if (stamp.edgeBlur > 0.0f || stamp.band > 1.0f || stamp.paintRamp > 0.0f || stamp.chordRadius > 0.0f)
            return false;
    }
    return true;
}

namespace {

// std::max, std::min and std::clamp lane by lane, keeping which operand they return when the two are equal.
__m256 laneMax(__m256 a, __m256 b) {
    return _mm256_blendv_ps(a, b, _mm256_cmp_ps(a, b, _CMP_LT_OQ));
}
__m256 laneMin(__m256 a, __m256 b) {
    return _mm256_blendv_ps(a, b, _mm256_cmp_ps(b, a, _CMP_LT_OQ));
}
__m256 laneClamp(__m256 v, __m256 low, __m256 high) {
    return _mm256_blendv_ps(_mm256_blendv_ps(v, high, _mm256_cmp_ps(high, v, _CMP_LT_OQ)), low, _mm256_cmp_ps(v, low, _CMP_LT_OQ));
}
// rampAt and outlineShareOf for a stamp with a plain outline (no blur).
__m256 laneRamp(const Stamp& stamp, __m256 in) {
    const __m256 t = laneMin(_mm256_div_ps(in, _mm256_set1_ps(std::max(stamp.rampWidth, 1.0f))), _mm256_set1_ps(1.0f));
    const __m256 eased =
        stamp.easedRamp ? _mm256_mul_ps(_mm256_mul_ps(t, t), _mm256_sub_ps(_mm256_set1_ps(3.0f), _mm256_mul_ps(_mm256_set1_ps(2.0f), t)))
                        : t;
    return laneMin(eased, _mm256_set1_ps(stamp.radius * 2.0f));
}
__m256 laneOutline(const Stamp& stamp, __m256 distance) {
    const float outer = outerOf(stamp);
    const __m256 ramp = laneRamp(stamp, _mm256_sub_ps(_mm256_set1_ps(outer), distance));
    return _mm256_andnot_ps(_mm256_cmp_ps(distance, _mm256_set1_ps(outer), _CMP_GE_OQ), ramp);
}
// A frame's dx at eight pixels of a row from x, `stride` px apart.
__m256 laneDx(const PixelFrame& frame, int x, int stride) {
    alignas(32) float dx[kWideLanes];
    for (int l = 0; l < kWideLanes; ++l)
        dx[l] = frame.dx(x + l * stride);
    return _mm256_load_ps(dx);
}

}

// dabMark at eight pixels of a row from x, `stride` px apart, without passes, for a band marksInLanes allows: each lane takes dabMark's
// and skirtAt's steps on its pixel, in the same order, adding nothing where they add nothing, so each gives what one at a time does:
// neighbouring marked pixels, or a row's grid points.
void DabPainter::markEight(const Dab& dab, int x, int y, const std::vector<BandStamp>& band, StampMark* out, int stride,
                           const char* cutWhole) const {
    const __m256 zero = _mm256_setzero_ps(), one = _mm256_set1_ps(1.0f);
    __m256 paint = zero, shape = zero;
    // Plain arrays: GCC drops __m256's alignment attributes when it is a template argument, as in std::array.
    __m256 distances[kMostSkirtedBand];  // squared; for a stamp whole over the band, unused
    for (std::size_t j = 0; j < band.size(); ++j) {
        const std::size_t k = band[j].k, i = dab.first + k;
        const WholeStamp& whole = m_wholeStamps[k];
        const Stamp& stamp = m_stamps[i];
        if (band[j].whole && band[j].unevened) {
            paint = _mm256_add_ps(paint, _mm256_set1_ps(whole.paint));
            shape = laneMax(shape, one);
            continue;
        }
        const __m256 dx = laneDx(whole.frame, x, stride);
        const float dy = whole.frame.dy(y);
        const __m256 squared = _mm256_add_ps(_mm256_mul_ps(dx, dx), _mm256_set1_ps(dy * dy));
        distances[j] = squared;
        const __m256 inside = _mm256_cmp_ps(squared, _mm256_set1_ps(whole.outsideSquared), _CMP_LT_OQ);
        const __m256 solid = _mm256_and_ps(inside, _mm256_cmp_ps(squared, _mm256_set1_ps(whole.solidSquared), _CMP_LT_OQ));
        // Past its solid reach: stampMark, with no rim and no paint ramp, lays paint 1 and its ramp as its outline, inside its outer end.
        const __m256 distance = _mm256_sqrt_ps(squared);
        const float outer = outerOf(stamp);
        const __m256 ramp = laneRamp(stamp, _mm256_sub_ps(_mm256_set1_ps(outer), distance));
        const __m256 marked =
            _mm256_andnot_ps(solid, _mm256_and_ps(_mm256_and_ps(inside, _mm256_cmp_ps(distance, _mm256_set1_ps(outer), _CMP_LT_OQ)),
                                                  _mm256_cmp_ps(ramp, zero, _CMP_GT_OQ)));
        const __m256 laid = _mm256_mul_ps(_mm256_mul_ps(_mm256_set1_ps(densityOf(stamp.opacity, 1.0f)), ramp), one);
        const __m256 term = _mm256_blendv_ps(_mm256_and_ps(marked, laid), _mm256_mul_ps(_mm256_set1_ps(whole.paint), one), solid);
        paint = _mm256_add_ps(paint, term);
        shape = _mm256_blendv_ps(_mm256_blendv_ps(shape, laneMax(shape, ramp), marked), laneMax(shape, one), solid);
    }
    // skirtAt: the outlines of the stamps around at these pixels, how deep each lies inside them, and the skirt's ramps; not needed
    // where every stamp's cut is whole over the band.
    const auto around = static_cast<std::size_t>(m_tip.skirtNeighbours);
    const std::size_t aroundEnd = m_aroundFirst + m_around.size();
    const float cell = static_cast<float>(m_gridCell), spareIn = kSharpCells * cell + kSharpInsideSpare, width = kSkirtCutRampCells * cell;
    __m256 outlines[kMostSkirtedBand], deep[kMostSkirtedBand], ramps[kMostSkirtedBand];
    bool needsAround = false;
    for (std::size_t j = 0; j < band.size() && !needsAround; ++j)
        needsAround = !(band[j].whole && band[j].unevened) && !(cutWhole && cutWhole[j]);
    for (std::size_t at = m_aroundFirst; at < aroundEnd && needsAround; ++at) {
        const AroundStamp& near = m_around[at - m_aroundFirst];
        const Stamp& stamp = m_stamps[at];
        const __m256 dx = laneDx(near.frame, x, stride);
        const float dy = near.frame.dy(y);
        const __m256 squared = _mm256_add_ps(_mm256_mul_ps(dx, dx), _mm256_set1_ps(dy * dy));
        const __m256 distance = _mm256_sqrt_ps(squared);
        const __m256 partial = laneOutline(stamp, distance);
        const __m256 whole = _mm256_cmp_ps(squared, _mm256_set1_ps(near.wholeSquared), _CMP_LT_OQ);
        const __m256 none = _mm256_cmp_ps(squared, _mm256_set1_ps(near.outerSquared), _CMP_GE_OQ);
        outlines[at - m_aroundFirst] = _mm256_blendv_ps(_mm256_andnot_ps(none, partial), one, whole);
        const float inner = outerOf(stamp) - spareIn;
        deep[at - m_aroundFirst] = inner > 0.0f ? _mm256_cmp_ps(squared, _mm256_set1_ps(inner * inner), _CMP_LT_OQ) : zero;
        ramps[at - m_aroundFirst] =
            laneClamp(_mm256_add_ps(_mm256_div_ps(_mm256_sub_ps(_mm256_set1_ps(outerOf(stamp)), distance), _mm256_set1_ps(width)),
                                    _mm256_set1_ps(0.5f)),
                      zero, one);
    }
    __m256 added = zero;
    for (std::size_t j = 0; j < band.size(); ++j) {
        if (band[j].whole && band[j].unevened)
            continue;  // whole over the band
        const std::size_t k = band[j].k, i = dab.first + k;
        const WholeStamp& whole = m_wholeStamps[k];
        const Stamp& stamp = m_stamps[i];
        const __m256 squared = distances[j];
        __m256 valid = _mm256_and_ps(_mm256_cmp_ps(squared, _mm256_set1_ps(whole.reachSquared), _CMP_LT_OQ),
                                     _mm256_cmp_ps(squared, _mm256_set1_ps(whole.solidSquared), _CMP_GE_OQ));
        if (_mm256_movemask_ps(valid) == 0)
            continue;
        const __m256 distance = _mm256_sqrt_ps(squared);
        alignas(32) float outside[kWideLanes], shares[kWideLanes];
        _mm256_store_ps(outside, _mm256_sub_ps(distance, _mm256_set1_ps(stamp.radius)));
        for (int l = 0; l < kWideLanes; ++l)
            shares[l] = skirtShare(stamp, outside[l]);
        const __m256 share = _mm256_load_ps(shares), own = laneOutline(stamp, distance);
        valid = _mm256_andnot_ps(_mm256_cmp_ps(share, own, _CMP_LE_OQ), valid);
        if (_mm256_movemask_ps(valid) == 0)
            continue;
        if (cutWhole && cutWhole[j]) {  // its cut, and the ramp in its place, are whole over the band
            const __m256 adds = _mm256_and_ps(valid, _mm256_cmp_ps(share, own, _CMP_GT_OQ));
            added = _mm256_add_ps(
                added, _mm256_and_ps(adds, _mm256_mul_ps(_mm256_set1_ps(whole.paint), _mm256_sub_ps(_mm256_mul_ps(share, one), own))));
            const __m256 raises = _mm256_andnot_ps(_mm256_cmp_ps(_mm256_mul_ps(share, one), own, _CMP_LE_OQ), valid);
            shape = _mm256_blendv_ps(shape, laneMax(shape, one), raises);
            continue;
        }
        __m256 most = zero, ramp = zero, inside = zero;
        for (std::size_t step = 1; step <= around; ++step) {
            if (i >= m_aroundFirst + step)
                most = laneMax(most, outlines[i - step - m_aroundFirst]);
            if (i + step < aroundEnd)
                most = laneMax(most, outlines[i + step - m_aroundFirst]);
            for (const std::size_t at : {i - step, i + step})
                if (at >= m_aroundFirst && at < aroundEnd)
                    ramp = laneMax(ramp, ramps[at - m_aroundFirst]);
        }
        for (std::size_t at = m_aroundFirst; at < aroundEnd; ++at)
            if (at + around < i || at > i + around)
                inside = _mm256_or_ps(inside, deep[at - m_aroundFirst]);
        const __m256 cut = _mm256_blendv_ps(most, ramp, inside);
        const __m256 cutShare = _mm256_mul_ps(share, cut);
        const __m256 adds = _mm256_and_ps(valid, _mm256_cmp_ps(cutShare, own, _CMP_GT_OQ));
        added = _mm256_add_ps(added, _mm256_and_ps(adds, _mm256_mul_ps(_mm256_set1_ps(whole.paint), _mm256_sub_ps(cutShare, own))));
        const __m256 raises = _mm256_andnot_ps(_mm256_cmp_ps(_mm256_mul_ps(share, most), own, _CMP_LE_OQ), valid);
        shape = _mm256_blendv_ps(shape, laneMax(shape, most), raises);
    }
    paint = _mm256_add_ps(paint, added);
    alignas(32) float paints[kWideLanes], shapes[kWideLanes];
    _mm256_store_ps(paints, paint);
    _mm256_store_ps(shapes, shape);
    for (int l = 0; l < kWideLanes; ++l)
        out[l] = StampMark{paints[l], shapes[l], 0.0f};
}

// For each band stamp, whether its skirt's cut is whole at every pixel of the rect: the rect lies inside the whole outline of one of its
// neighbours, and inside it by half the skirt's ramp, so the ramp in the cut's place is whole too; each by kSkirtWholeSpare, as
// the band test
// keeps.
std::vector<char> DabPainter::cutsWholeOver(const Dab& dab, const std::vector<BandStamp>& band, const QRect& rect) const {
    std::vector<char> whole(band.size(), 0);
    const auto around = static_cast<std::size_t>(m_tip.skirtNeighbours);
    const float halfRamp = kSkirtCutRampCells * static_cast<float>(m_gridCell) / 2.0f;
    for (std::size_t j = 0; j < band.size(); ++j) {
        const std::size_t i = dab.first + band[j].k;
        for (std::size_t at = m_aroundFirst; at < m_aroundFirst + m_around.size() && !whole[j]; ++at) {
            if (at == i || at + around < i || at > i + around || m_around[at - m_aroundFirst].wholeSquared <= 0.0f)
                continue;
            const AroundStamp& near = m_around[at - m_aroundFirst];
            const float r = std::min(std::sqrt(near.wholeSquared), outerOf(m_stamps[at]) - halfRamp) - kSkirtWholeSpare;
            if (r <= 0.0f)
                continue;
            bool inside = true;
            for (const int cx : {rect.left(), rect.right()})
                for (const int cy : {rect.top(), rect.bottom()}) {
                    const float dx = near.frame.dx(cx), dy = near.frame.dy(cy);
                    inside = inside && dx * dx + dy * dy < r * r;
                }
            whole[j] = inside;
        }
    }
    return whole;
}

float DabPainter::outlineAround(std::size_t i, int x, int y) const {
    const AroundStamp& at = m_around[i - m_aroundFirst];
    const float dx = at.frame.dx(x), dy = at.frame.dy(y), distanceSquared = dx * dx + dy * dy;
    if (distanceSquared < at.wholeSquared)
        return 1.0f;
    if (distanceSquared >= at.outerSquared)
        return 0.0f;
    return outlineShareOf(m_stamps[i], std::sqrt(distanceSquared));
}

bool DabPainter::skirtRaisesWhole(const Dab& dab, const std::vector<BandStamp>& band, const QRect& rect) const {
    const auto around = static_cast<std::size_t>(m_tip.skirtNeighbours);
    const std::size_t aroundEnd = m_aroundFirst + m_around.size();
    // Whether every pixel of the rect lies within `squared` of a frame's centre: a disc holds a rect when it holds its four corners.
    const auto holds = [&](const PixelFrame& frame, float squared) {
        for (const int x : {rect.left(), rect.right()})
            for (const int y : {rect.top(), rect.bottom()}) {
                const float dx = frame.dx(x), dy = frame.dy(y);
                if (dx * dx + dy * dy >= squared)
                    return false;
            }
        return true;
    };
    for (const BandStamp& stamp : band) {
        const std::size_t i = dab.first + stamp.k;
        // Inside its reach by a pixel, where its skirt's share is still above none however the table rounds; skirtAt asks less.
        const float inside = reachOf(m_stamps[i]) - kSkirtWholeSpare;
        if (inside <= 0.0f || !holds(m_wholeStamps[stamp.k].frame, inside * inside))
            continue;
        for (std::size_t step = 1; step <= around; ++step)
            for (const std::size_t j : {i >= m_aroundFirst + step ? i - step : aroundEnd, i + step})
                if (j < aroundEnd && m_around[j - m_aroundFirst].wholeSquared > 0.0f) {
                    // Wholly inside its outline by a pixel, so no rounding of a pixel's distance takes one past it where the corners aren't.
                    const float whole = std::sqrt(m_around[j - m_aroundFirst].wholeSquared) - kSkirtWholeSpare;
                    if (whole > 0.0f && holds(m_around[j - m_aroundFirst].frame, whole * whole))
                        return true;
                }
    }
    return false;
}

float DabPainter::skirtAt(const Dab& dab, int x, int y, const std::vector<BandStamp>* band, float& outline, bool outlineOnly,
                          const float* distancesSquared) const {
    float added = 0.0f;
    const std::size_t stamps = band ? band->size() : dab.count;
    const auto around = static_cast<std::size_t>(m_tip.skirtNeighbours);
    const std::size_t aroundEnd = m_aroundFirst + m_around.size();
    // The outlines of the stamps around, as worked out for this pixel; below 0 not yet. Past its room they're worked out each time.
    std::array<float, kMostSkirtedBand> known;
    std::fill_n(known.begin(), std::min(m_around.size(), known.size()), -1.0f);
    for (std::size_t j = 0; j < stamps; ++j) {
        const std::size_t k = band ? (*band)[j].k : j;
        const std::size_t i = dab.first + k;
        const WholeStamp& whole = m_wholeStamps[k];
        float distanceSquared = 0.0f;
        if (distancesSquared) {
            distanceSquared = distancesSquared[j];
            if (distanceSquared < 0.0f)
                continue;  // whole over the band
        } else {
            const float dx = whole.frame.dx(x), dy = whole.frame.dy(y);
            distanceSquared = dx * dx + dy * dy;
        }
        if (distanceSquared >= whole.reachSquared || distanceSquared < whole.solidSquared)
            continue;  // past its skirt, or where its own outline is whole, which no skirt passes
        const Stamp& stamp = m_stamps[i];
        const float distance = std::sqrt(distanceSquared);
        const float share = skirtShare(stamp, distance - stamp.radius), own = outlineShareOf(stamp, distance);
        if (share <= own)
            continue;
        // The most of the outlines around it, nearest first, which mostly cover whole. A dab's stamps share most of theirs, so each is
        // worked out once for the pixel.
        const auto outlineOf = [&](std::size_t at) {
            if (at - m_aroundFirst >= known.size())
                return outlineAround(at, x, y);
            float& kept = known[at - m_aroundFirst];
            if (kept < 0.0f)
                kept = outlineAround(at, x, y);
            return kept;
        };
        float most = 0.0f;
        for (std::size_t step = 1; step <= around && most < 1.0f; ++step) {
            if (i >= m_aroundFirst + step)
                most = std::max(most, outlineOf(i - step));
            if (i + step < aroundEnd && most < 1.0f)
                most = std::max(most, outlineOf(i + step));
        }
        // Deep inside a stamp around the dab that isn't one of its neighbours, where markSkirtedEdges marks nothing, the cut is the
        // neighbours' outlines as a ramp the grid can read; nearer the stroke's side it's the step, as before.
        float cut = most;
        if (!outlineOnly && m_gridCell > kGridOne) {  // a pixel apart, every pixel is worked out and the step is kept
            const float cell = static_cast<float>(m_gridCell), spareIn = kSharpCells * cell + kSharpInsideSpare;
            bool deep = false;
            for (std::size_t at = m_aroundFirst; at < aroundEnd && !deep; ++at)
                if (at + around < i || at > i + around) {
                    const AroundStamp& other = m_around[at - m_aroundFirst];
                    const float dx = other.frame.dx(x), dy = other.frame.dy(y), inner = outerOf(m_stamps[at]) - spareIn;
                    deep = inner > 0.0f && dx * dx + dy * dy < inner * inner;
                }
            if (deep) {
                const float width = kSkirtCutRampCells * cell;
                cut = 0.0f;
                for (std::size_t step = 1; step <= around; ++step)
                    for (const std::size_t at : {i - step, i + step})
                        if (at >= m_aroundFirst && at < aroundEnd) {
                            const AroundStamp& near = m_around[at - m_aroundFirst];
                            const float dx = near.frame.dx(x), dy = near.frame.dy(y);
                            cut = std::max(cut,
                                           std::clamp((outerOf(m_stamps[at]) - std::sqrt(dx * dx + dy * dy)) / width + 0.5f, 0.0f, 1.0f));
                        }
            }
        }
        if (share * cut > own)
            added += whole.paint * (share * cut - own);
        if (share * most <= own)
            continue;
        outline = std::max(outline, most);
        if (outlineOnly && outline >= 1.0f)
            break;  // no stamp can raise it further
    }
    return added;
}

namespace {

// The most StampTip::skirtNeighbours markSkirtedEdges keeps room for on each side, in arrays on the stack. Chosen as a little over
// twice the fitted 3 (kBrushSkirtNeighbours), so a refit trying 4 to 8 needs no change; past it the stamps further along would be left
// out of the marking, so a fit past 8 must raise it.
constexpr int kMostSkirtNeighbours = 8;  // 3 and up paint the same, 2 leaves the third out (6.7 levels at 4000 px)

struct Span {
    float from = 0.0f, to = 0.0f;  // px along a row
};

// The part of a row a ring from `inner` to `outer` px round a centre covers, the centre `at` along the row and `dy` off it: none, one
// span, or two either side of a hole.
int ringOnRow(float at, float dy, float inner, float outer, std::array<Span, 2>& out) {
    if (std::abs(dy) >= outer)
        return 0;
    const float far = std::sqrt(outer * outer - dy * dy);
    if (std::abs(dy) >= inner) {
        out[0] = {at - far, at + far};
        return 1;
    }
    const float near = std::sqrt(inner * inner - dy * dy);
    out[0] = {at - far, at - near};
    out[1] = {at + near, at + far};
    return 2;
}

}

// A skirted stamp's mark is smooth wherever a stamp around it covers whole, as its skirt shows all of itself there, so the grid can
// read it. Elsewhere across its skirt and its own outline's edge, its mark steps at its own outline and at those of the stamps around
// it, which show its skirt or don't: those rings are worked out per pixel.
void DabPainter::markSkirtedEdges(const Dab& dab, const BandStamp& band, int y, int left, int right, std::vector<char>& sharp) const {
    const float cell = static_cast<float>(m_gridCell);
    const float spareOut = kSharpOutsideCells * cell, spareIn = kSharpCells * cell + kSharpInsideSpare;
    const std::size_t i = dab.first + band.k;
    const auto around = static_cast<std::size_t>(std::min(m_tip.skirtNeighbours, kMostSkirtNeighbours));
    const std::size_t from = i >= m_aroundFirst + around ? i - around : m_aroundFirst;
    const std::size_t to = std::min(i + around + 1, m_aroundFirst + m_around.size());
    const auto placeOf = [&](std::size_t j, float& at, float& dy) {
        const PixelFrame& frame = m_around[j - m_aroundFirst].frame;
        at = 0.0f -
             frame.dx(0);  // where dx is 0, counted from the canvas's left edge, so the same pixels are marked however a row is bounded
        dy = frame.dy(y);
    };
    float at = 0.0f, dy = 0.0f;
    placeOf(i, at, dy);
    std::array<Span, 2> zone;
    const int zones = ringOnRow(at, dy, std::max(outerOf(m_stamps[i]) - spareIn, 0.0f), reachOf(m_stamps[i]) + spareOut, zone);
    // Nothing to mark on this tile's part of the row unless the zone comes into it, as `mark` below rounds.
    const auto missesRow = [&](const Span& span) {
        return static_cast<int>(std::floor(span.from)) > right || static_cast<int>(std::ceil(span.to)) < left;
    };
    if (zones == 0 || (missesRow(zone[0]) && (zones == 1 || missesRow(zone[1]))))
        return;
    // The rings' parts inside the zone that come into this tile's part of the row; the stamps' whole insides are left out only if any do.
    std::array<Span, 2 * 2 * (2 * kMostSkirtNeighbours + 1)> pieces;
    int pieceCount = 0;
    for (std::size_t j = from; j < to; ++j) {
        float atJ = 0.0f, dyJ = 0.0f;
        placeOf(j, atJ, dyJ);
        const float outer = outerOf(m_stamps[j]);
        std::array<Span, 2> ring;
        const int rings = ringOnRow(atJ, dyJ, std::max(outer - spareIn, 0.0f), outer + spareOut, ring);
        for (int r = 0; r < rings; ++r)
            for (int z = 0; z < zones; ++z) {
                const Span piece{std::max(ring[static_cast<std::size_t>(r)].from, zone[static_cast<std::size_t>(z)].from),
                                 std::min(ring[static_cast<std::size_t>(r)].to, zone[static_cast<std::size_t>(z)].to)};
                if (piece.from < piece.to && !missesRow(piece))
                    pieces[static_cast<std::size_t>(pieceCount++)] = piece;
            }
    }
    if (pieceCount == 0)
        return;
    // The deep insides of the stamps around it, and of the dab's other stamps, where its cut is a ramp the grid reads. Past the
    // room for them, a stamp's inside is marked too, which works the same pixels out exactly.
    std::array<Span, kMostSkirtedBand + 2 * kMostSkirtNeighbours> deep;
    int deeps = 0;
    for (std::size_t j = m_aroundFirst; j < m_aroundFirst + m_around.size() && deeps < static_cast<int>(deep.size()); ++j) {
        const float inner = outerOf(m_stamps[j]) - spareIn;
        float atJ = 0.0f, dyJ = 0.0f;
        placeOf(j, atJ, dyJ);
        if (j != i && inner > 0.0f && std::abs(dyJ) < inner) {
            const float half = std::sqrt(inner * inner - dyJ * dyJ);
            deep[static_cast<std::size_t>(deeps++)] = {atJ - half, atJ + half};
        }
    }
    std::sort(deep.begin(), deep.begin() + deeps, [](const Span& a, const Span& b) { return a.from < b.from; });
    const auto mark = [&](float a, float b) {
        const int first = std::max(left, static_cast<int>(std::floor(a))), last = std::min(right, static_cast<int>(std::ceil(b)));
        for (int px = first; px <= last; ++px)
            sharp[static_cast<std::size_t>(px - left)] = 1;
    };
    for (int p = 0; p < pieceCount; ++p) {
        const float a = pieces[static_cast<std::size_t>(p)].from, b = pieces[static_cast<std::size_t>(p)].to;
        float now = a;  // what's left of a to b, past the deep spans before it
        for (int d = 0; d < deeps && now < b; ++d) {
            const Span& covered = deep[static_cast<std::size_t>(d)];
            if (covered.to <= now)
                continue;
            if (covered.from >= b)
                break;
            if (covered.from > now)
                mark(now, covered.from);
            now = std::max(now, covered.to);
        }
        if (now < b)
            mark(now, b);
    }
}

// The outline a stamp lays along the path, f, so that a straight pass of such stamps, f added up over the path, gives each pixel the
// stamp's own side profile, F(d) = -ln(1 - shape) at d from the path, up to kAlongKneeShape: f is F's inverse Abel transform,
// f(r) = -1/pi times the integral from r out of F'(d) / sqrt(d^2 - r^2), taken along the line through r as the integral over t of
// -F'(sqrt(r^2 + t^2)) / sqrt(r^2 + t^2). The shape is the eased ramp of `ratio` of the radius, centred on it (rampAt); here the radius is
// 1, and f is kept at kAlongSamples places from the centre out to the ramp's outer end, each the middle of its share.
const std::vector<float>& DabPainter::alongTable(double ratio) {
    const int key = static_cast<int>(std::lround(ratio * kAlongRatioSteps));
    auto [found, made] = m_alongTables.try_emplace(key);
    if (!made)
        return found->second;
    const double width = static_cast<double>(key) / kAlongRatioSteps;
    const double outer = 1.0 + width / 2.0;
    const auto eased = [](double t) { return t * t * (3.0 - 2.0 * t); };
    double low = 0.0, high = 1.0;  // the knee: where the eased ramp reaches kAlongKneeShape, by halving
    for (int i = 0; i < kAlongSteps; ++i)
        (eased((low + high) / 2.0) < kAlongKneeShape ? low : high) = (low + high) / 2.0;
    const double knee = low;
    // -F'(d): the ramp's slope over what's left uncovered, S'(t) / (1 - S(t)) / width, with t = (outer - d) / width, and past the
    // knee its slope there.
    const auto slope = [&](double d) {
        const double t = std::min(std::max((outer - d) / width, 0.0), knee);
        return 6.0 * t * (1.0 - t) / (1.0 - eased(t)) / width;  // 6 t (1 - t) is the smoothstep's slope
    };
    std::vector<float>& table = found->second;
    table.resize(kAlongSamples);
    for (int k = 0; k < kAlongSamples; ++k) {
        const double r = outer * (k + 0.5) / kAlongSamples;
        const double reach = std::sqrt(std::max(outer * outer - r * r, 0.0));
        const double step = reach / kAlongSteps;
        double sum = 0.0;
        for (int j = 0; j < kAlongSteps; ++j) {
            const double t = (j + 0.5) * step;
            const double d = std::hypot(r, t);
            sum += slope(d) / d;
        }
        table[static_cast<std::size_t>(k)] = static_cast<float>(sum * step / std::numbers::pi);
    }
    return table;
}

void DabPainter::addPasses(Reach& reach, std::size_t index, float passes) const {
    if (m_mixing.passLength <= 0.0 && !m_mixing.outlineAlongPath)
        return;
    reach.passed[index] += passes;
    // Only where it changes, as every write to a shape: a tile whose shape is all 1 shares the read-only array of ones
    // (shareSettledColumns), and writing a 1 there again crashed a dab coming back over it.
    if (m_mixing.outlineAlongPath && reach.passed[index] >= kWholeAlongPasses && reach.shape[index] < 1.0f)
        reach.shape[index] = 1.0f;
}

float DabPainter::passOf(const Stamp& stamp, const WholeStamp& whole, float distance, float shape) const {
    if (!m_mixing.outlineAlongPath) {
        // A pass of the soft edge, -ln(1 - shape), for the share of a pass length of path this stamp stands for.
        const float pass = -keptLog1p<0>(-std::min(shape, kMostPassedShape));
        return pass * static_cast<float>(stamp.step / (m_mixing.passLength * outerOf(stamp)));
    }
    if (!whole.along)
        return 0.0f;
    const float place = distance / outerOf(stamp) * static_cast<float>(kAlongSamples) - 0.5f;
    const std::vector<float>& table = *whole.along;
    const float at = std::clamp(place, 0.0f, static_cast<float>(kAlongSamples - 1));
    const auto low = static_cast<std::size_t>(at);
    const std::size_t high = std::min(low + 1, table.size() - 1);
    const float f = lerp(table[low], table[high], at - static_cast<float>(low));
    return f / std::max(stamp.radius, 1e-3f) * static_cast<float>(stamp.step);
}

// A whole mark, paint 1 and shape 1, covers the pixel fully, so there what a stamp lays is the dab's own number for it.
void DabPainter::prepareMarkerStamps(const Dab& dab) {
    m_markerStamps.assign(dab.count, MarkerStamp{});
    for (std::size_t k = 0; k < dab.count; ++k) {
        const Stamp& stamp = m_stamps[dab.first + k];
        MarkerStamp& prepared = m_markerStamps[k];
        prepared.frame = PixelFrame(stamp.centre);
        prepared.opacity = stamp.opacity;
        const double diameter = std::max(2.0 * static_cast<double>(stamp.radius), 1.0);
        const double atEdge =
            kMarkerSpacingDiameter + (kMarkerSpacingDiameterFirm - kMarkerSpacingDiameter) * std::clamp(m_tip.edge, 0, 4) / 4.0;
        const double perOld = kMarkerStampsPerOldStamp * std::max(std::pow(diameter / atEdge, -kMarkerSpacingPower), kMarkerSpacingLeast);
        prepared.weight = static_cast<float>(stamp.weight / perOld);  // the event-laid Marker doesn't climb
        const float own = stamp.opacity;
        const float weight = prepared.weight;
        const float reach = stamp.formReach > 0.0f ? 0.0f : static_cast<float>(solidReach(m_stamps, dab.first + k)) - kSolidMargin;
        prepared.solidSquared = reach > 0.0f ? reach * reach : -1.0f;
        prepared.outsideSquared = (layReachOf(stamp) + kSolidMargin) * (layReachOf(stamp) + kSolidMargin);
        // Its step toward its top is the pressure's share of Density, per stamp at its old spacing (overStep spreads it over the
        // kMarkerStampsPerOldStamp stamps that stand for one now), where its top is that share squared: the reference's. A
        // click's one stamp is one whole stamp, under the stroke's own top. It replaced kMarkerClimb, 3 times the top, and
        // was kept.
        const float shareOfDensity = std::sqrt(std::clamp(own / std::max(float(m_tip.density), 1e-6f), 0.0f, 1.0f));
        prepared.climb = overStep(std::min(float(m_tip.density) * shareOfDensity, 1.0f), stamp.click ? 1.0f : weight);
        prepared.body = m_mixing.keepsBodyColour ? std::min(1.0f, own / std::max(float(m_firmest), kLeastFirmest)) : 1.0f;
        const float cover = 1.0f;
        prepared.colourWhole = overStep(std::min(1.0f, cover * own * kMarkerColourStrength), weight);
        prepared.laidWhole = densityOf(1.0f, std::min(cover, kMostMarkerCover)) * weight * prepared.body;
    }
    float laid = 0.0f;
    for (const MarkerStamp& prepared : m_markerStamps)
        laid += prepared.laidWhole;
    m_meltAllWhole = std::min(-std::expm1(-laid) * m_melt, 1.0f);
}

void DabPainter::layMarkerPixel(const Dab& dab, const Premultiplied& paint, Laying& laying, int x, int y, MarkerClimb* climb,
                                const std::vector<BandStamp>& band) const {
    const std::size_t index = static_cast<std::size_t>(indexIn(laying.coord, x, y));
    Reach& reach = *laying.reach;
    layMarkerAt(dab, paint, laying, index, MarkerRecord{reach.value[index], reach.shape[index], reach.held[index], reach.paint[index]}, x,
                y, climb, band);
}

// The pixel at `index` of the laying's tile, its numbers kept in `record`.
void DabPainter::layMarkerAt(const Dab& dab, const Premultiplied& paint, Laying& laying, std::size_t index, MarkerRecord record, int x,
                             int y, MarkerClimb* climb, const std::vector<BandStamp>& band) const {
    Reach& reach = *laying.reach;
    float opacity = record.value;
    float strength = record.shape;
    const float opacityBefore = opacity, strengthBefore = strength;
    float laid = 0.0f;
    bool allWhole = band.size() == dab.count;  // a stamp not over the band lays nothing at this pixel
    // Where every stamp is whole, what the stamps do depends only on the pixel's opacity and strength, and neighbours usually have the
    // same: the last pixel's answer is kept, the same numbers.
    const bool knownClimb = climb && climb->valid && std::bit_cast<std::uint32_t>(opacity) == climb->inOpacity &&
                            std::bit_cast<std::uint32_t>(strength) == climb->inStrength;
    if (knownClimb) {
        opacity = climb->outOpacity;
        strength = climb->outStrength;
        laid = climb->laid;
    } else {
        for (const BandStamp& stamp : band) {
            const std::size_t i = dab.first + stamp.k;
            const MarkerStamp& prepared = m_markerStamps[stamp.k];
            float distanceSquared = 0.0f;
            bool whole = true;
            if (!stamp.whole) {
                const float dx = prepared.frame.dx(x), dy = prepared.frame.dy(y);
                distanceSquared = dx * dx + dy * dy;
                if (distanceSquared >= prepared.outsideSquared) {
                    allWhole = false;  // past its outline: it lays nothing there
                    continue;
                }
                whole = distanceSquared < prepared.solidSquared;
                allWhole = allWhole && whole;
            }
            const StampMark mark = whole        ? StampMark{1.0f, 1.0f, 0.0f}
                                   : m_tip.form ? formedMark(m_stamps[i], prepared.frame.dx(x), prepared.frame.dy(y))
                                                : limitedMark(m_stamps, i, m_tip.edge, std::sqrt(distanceSquared));
            const float cover = mark.paint * mark.shape;
            if (cover <= 0.0f)
                continue;
            const float own = prepared.opacity;
            const float weight = prepared.weight;
            // Opacity climbs toward the stamp's ceiling where it covers, own · cover, never down; the colour's strength toward 1, faster;
            // and the paint laid adds up, as the other tools' density does.
            opacity += std::max(0.0f, own * cover - opacity) * prepared.climb;
            strength +=
                (1.0f - strength) * (whole ? prepared.colourWhole : overStep(std::min(1.0f, cover * own * kMarkerColourStrength), weight));
            laid += whole ? prepared.laidWhole : densityOf(1.0f, std::min(cover, kMostMarkerCover)) * weight * prepared.body;
        }
    }
    if (climb && !knownClimb) {
        *climb = {true, std::bit_cast<std::uint32_t>(opacityBefore), std::bit_cast<std::uint32_t>(strengthBefore), opacity, strength, laid};
    }
    if (laid <= 0.0f)
        return;
    Channel* px = laying.tile->px.data() + index * 4;
    const bool first = record.held <= 0.0f;
    if (first && reach.under.owned() && reach.markerCell == 0)  // on the grid it's kept from the start
        reach.under[index] = readPixel(px);
    // Where every stamp is whole, the paint laid is the same sum everywhere, so its melt is the dab's. Where nothing was under the
    // stroke, the melt, which changes only colour, leaves nothing as nothing, so the blur isn't read.
    if ((m_wet || m_coarseWet) && !(m_mixing.meltColourOnly && reach.under[index] == Premultiplied{})) {
        const float melt = allWhole ? m_meltAllWhole : std::min(-std::expm1(-laid) * m_melt, 1.0f);
        if (melt > 0.0f && reach.under.owned())
            softenUnder(reach.under[index], m_wet ? m_wet->at(x, y) : m_coarseWet->at(x, y), melt);
    }
    // The reference's Marker sets the pixel to the colour it carries now, with no memory of what it laid before.
    if (laid > 0.0f)
        record.paint = paint;
    record.held += laid;
    // A pixel at its ceiling and full strength keeps them: they're left as they are rather than written again.
    if (opacity != opacityBefore)
        record.value = opacity;
    if (strength != strengthBefore)
        record.shape = strength;
    // Shown: the opacity over what's under, or with ceilingOnly the most of the two; the colour from what's under's toward the stroke's
    // by its strength, each as a straight colour, an empty one taking the other's.
    const Premultiplied& under = reach.under[index];
    const Premultiplied& mine = record.paint;
    const float alpha =
        m_mixing.ceilingOnly ? std::max(under[3], opacity * m_paint.opacity) : under[3] + (1.0f - under[3]) * opacity * m_paint.opacity;
    Premultiplied out{0.0f, 0.0f, 0.0f, alpha};
    // Unrolled: at -O3 GCC otherwise copies this loop for each way its two tests can go (unswitching), which made the whole Marker
    // 6 % slower than at -O2. Written out by hand instead, it's slower again.
#ifdef __GNUC__
#pragma GCC unroll 3
#endif
    for (std::size_t c = 0; c < 3; ++c) {
        const float underColour = under[3] > 1e-6f ? under[c] / under[3] : mine[c] / std::max(mine[3], 1e-6f);
        const float myColour = mine[3] > 1e-6f ? mine[c] / mine[3] : underColour;
        out[c] = (strength > 0.0f ? myColour : underColour) * alpha;  // every pixel its stamps touch takes the colour it carries
    }
    writePixel(px, out);
}
