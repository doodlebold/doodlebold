// Part of DabTip.cpp, which includes it where it stands: the Brush's, Water Color's and the Marker's laying, eight pixels of a row at once
// with AVX2.

// The Brush's group laid over eight pixels of a row at once, the steps of shown() and writePixel in each lane: each pixel moves toward
// `paint` by its mask's share, keeps the alpha it had where that would lower it, and is written; a pixel whose mask is none is left as it was.
void brushLayEight(Channel* px, const std::int32_t* mask, const float* paint) {
    // Two pixels a step, one in each half of a register: their 16-bit channels widened to floats in place, so nothing is gathered.
    const __m256 zero = _mm256_setzero_ps(), one = _mm256_set1_ps(1.0f), toFloat = _mm256_set1_ps(kChannelToFloat);
    const __m256 channelOne = _mm256_set1_ps(static_cast<float>(kChannelOne)), half = _mm256_set1_ps(0.5f), least = _mm256_set1_ps(1e-9f);
    const __m256 colour = _mm256_setr_ps(paint[0], paint[1], paint[2], paint[3], paint[0], paint[1], paint[2], paint[3]);
    const __m256 maskOne = _mm256_set1_ps(static_cast<float>(kMaskOne));
    for (int k = 0; k < kWideLanes; k += 2) {
        if (mask[k] <= 0 && mask[k + 1] <= 0)
            continue;
        Channel* at = px + k * 4;
        const __m256 under =
            _mm256_mul_ps(_mm256_cvtepi32_ps(_mm256_cvtepu16_epi32(_mm_loadu_si128(reinterpret_cast<const __m128i*>(at)))), toFloat);
        const __m256 toward =
            _mm256_div_ps(_mm256_setr_ps(static_cast<float>(mask[k]), static_cast<float>(mask[k]), static_cast<float>(mask[k]),
                                         static_cast<float>(mask[k]), static_cast<float>(mask[k + 1]), static_cast<float>(mask[k + 1]),
                                         static_cast<float>(mask[k + 1]), static_cast<float>(mask[k + 1])),
                          maskOne);
        __m256 out = _mm256_add_ps(under, _mm256_mul_ps(_mm256_sub_ps(colour, under), toward));
        // where the alpha would fall, it's raised back to what was under, the colour with it
        const __m256 outAlpha = _mm256_permute_ps(out, 0xFF), underAlpha = _mm256_permute_ps(under, 0xFF);
        const __m256 lower = _mm256_and_ps(_mm256_cmp_ps(outAlpha, underAlpha, _CMP_LT_OQ), _mm256_cmp_ps(outAlpha, least, _CMP_GT_OQ));
        out = _mm256_blendv_ps(out, _mm256_mul_ps(out, _mm256_div_ps(underAlpha, outAlpha)), lower);
        const __m256 rounded = _mm256_add_ps(_mm256_mul_ps(_mm256_min_ps(_mm256_max_ps(out, zero), one), channelOne), half);
        const __m256i whole = _mm256_cvttps_epi32(_mm256_min_ps(rounded, _mm256_permute_ps(rounded, 0xFF)));  // no channel above its alpha
        const __m256i packed = _mm256_permute4x64_epi64(_mm256_packus_epi32(whole, whole), 0x08);
        const __m128i both = _mm256_castsi256_si128(packed);
        if (mask[k] > 0 && mask[k + 1] > 0) {
            _mm_storeu_si128(reinterpret_cast<__m128i*>(at), both);
        } else if (mask[k] > 0) {
            _mm_storel_epi64(reinterpret_cast<__m128i*>(at), both);
        } else {
            _mm_storel_epi64(reinterpret_cast<__m128i*>(at + 4), _mm_unpackhi_epi64(both, both));
        }
    }
}

// Water Color's group laid over eight pixels of a row at once, the steps of its scalar laying and writePixel in each lane: each
// pixel moves toward its own target (`targets`, four floats a pixel) by its mask's share and is written; a pixel whose mask is none is
// left as it was.
void waterLayEight(Channel* px, const std::int32_t* mask, const float* targets) {
    const __m256 zero = _mm256_setzero_ps(), one = _mm256_set1_ps(1.0f), toFloat = _mm256_set1_ps(kChannelToFloat);
    const __m256 channelOne = _mm256_set1_ps(static_cast<float>(kChannelOne)), half = _mm256_set1_ps(0.5f);
    const __m256 maskOne = _mm256_set1_ps(static_cast<float>(kMaskOne));
    for (int k = 0; k < kWideLanes; k += 2) {
        if (mask[k] <= 0 && mask[k + 1] <= 0)
            continue;
        Channel* at = px + k * 4;
        const __m256 under =
            _mm256_mul_ps(_mm256_cvtepi32_ps(_mm256_cvtepu16_epi32(_mm_loadu_si128(reinterpret_cast<const __m128i*>(at)))), toFloat);
        const __m256 toward =
            _mm256_div_ps(_mm256_setr_ps(static_cast<float>(mask[k]), static_cast<float>(mask[k]), static_cast<float>(mask[k]),
                                         static_cast<float>(mask[k]), static_cast<float>(mask[k + 1]), static_cast<float>(mask[k + 1]),
                                         static_cast<float>(mask[k + 1]), static_cast<float>(mask[k + 1])),
                          maskOne);
        const __m256 target = _mm256_loadu_ps(targets + k * 4);
        const __m256 out = _mm256_add_ps(under, _mm256_mul_ps(_mm256_sub_ps(target, under), toward));
        const __m256 rounded = _mm256_add_ps(_mm256_mul_ps(_mm256_min_ps(_mm256_max_ps(out, zero), one), channelOne), half);
        const __m256i whole = _mm256_cvttps_epi32(_mm256_min_ps(rounded, _mm256_permute_ps(rounded, 0xFF)));  // no channel above its alpha
        const __m256i packed = _mm256_permute4x64_epi64(_mm256_packus_epi32(whole, whole), 0x08);
        const __m128i both = _mm256_castsi256_si128(packed);
        if (mask[k] > 0 && mask[k + 1] > 0) {
            _mm_storeu_si128(reinterpret_cast<__m128i*>(at), both);
        } else if (mask[k] > 0) {
            _mm_storel_epi64(reinterpret_cast<__m128i*>(at), both);
        } else {
            _mm_storel_epi64(reinterpret_cast<__m128i*>(at + 4), _mm_unpackhi_epi64(both, both));
        }
    }
}

// The event-laid Marker over eight pixels of a row at once, the steps of layEvent's markerToward in each lane: each pixel's top rises
// to `runTop` (into `tops`, or with `tops` null, `runTop` as it is), and the pixel moves toward the colour `straight` at an alpha of the
// higher of its top times `scale` and its own alpha (or with !ceilingOnly, its alpha with the top laid over it) by `share`, written as
// writePixel. Two pixels a step, one in each half of a register.
void markerTowardEight(Channel* px, float* tops, float runTop, float share, const float* straight, float scale, bool ceilingOnly) {
    const __m256 zero = _mm256_setzero_ps(), one = _mm256_set1_ps(1.0f), toFloat = _mm256_set1_ps(kChannelToFloat);
    const __m256 channelOne = _mm256_set1_ps(static_cast<float>(kChannelOne)), half = _mm256_set1_ps(0.5f);
    const __m256 colour = _mm256_setr_ps(straight[0], straight[1], straight[2], 1.0f, straight[0], straight[1], straight[2], 1.0f);
    for (int k = 0; k < kWideLanes; k += 2) {
        float t0 = runTop, t1 = runTop;
        if (tops) {
            t0 = tops[k] = std::max(tops[k], runTop);
            t1 = tops[k + 1] = std::max(tops[k + 1], runTop);
        }
        Channel* at = px + k * 4;
        const __m256 current =
            _mm256_mul_ps(_mm256_cvtepi32_ps(_mm256_cvtepu16_epi32(_mm_loadu_si128(reinterpret_cast<const __m128i*>(at)))), toFloat);
        const __m256 laid = _mm256_mul_ps(_mm256_setr_ps(t0, t0, t0, t0, t1, t1, t1, t1), _mm256_set1_ps(scale));
        const __m256 own = _mm256_permute_ps(current, 0xFF);
        const __m256 alpha = ceilingOnly ? _mm256_max_ps(own, laid) : _mm256_add_ps(own, _mm256_mul_ps(_mm256_sub_ps(one, own), laid));
        const __m256 target = _mm256_mul_ps(colour, alpha);
        const __m256 out = _mm256_add_ps(current, _mm256_mul_ps(_mm256_sub_ps(target, current), _mm256_set1_ps(share)));
        const __m256 rounded = _mm256_add_ps(_mm256_mul_ps(_mm256_min_ps(_mm256_max_ps(out, zero), one), channelOne), half);
        const __m256i whole = _mm256_cvttps_epi32(_mm256_min_ps(rounded, _mm256_permute_ps(rounded, 0xFF)));  // no channel above its alpha
        _mm_storeu_si128(reinterpret_cast<__m128i*>(at),
                         _mm256_castsi256_si128(_mm256_permute4x64_epi64(_mm256_packus_epi32(whole, whole), 0x08)));
    }
}

void rimDistanceLanes(const float* dx, float dy, float solidSquared, float outsideSquared, float* distanceSquared, unsigned& solidLanes,
                      unsigned& rimLanes) {
    const __m256 x = _mm256_load_ps(dx);
    const __m256 d = _mm256_add_ps(_mm256_mul_ps(x, x), _mm256_set1_ps(dy * dy));
    _mm256_store_ps(distanceSquared, d);
    const unsigned out = static_cast<unsigned>(_mm256_movemask_ps(_mm256_cmp_ps(d, _mm256_set1_ps(outsideSquared), _CMP_GE_OQ)));
    solidLanes = static_cast<unsigned>(_mm256_movemask_ps(_mm256_cmp_ps(d, _mm256_set1_ps(solidSquared), _CMP_LT_OQ))) & ~out;
    rimLanes = ~out & ~solidLanes & ((1u << kWideLanes) - 1u);
}

// One stamp's part in the rim's lanes, as one pixel takes it: where its share is some, the top rises to its top, the step left shrinks by
// its step as far as it covers, and the mask takes its share of what's left, rounded as the direct lay rounds.
void rimAccumulateLanes(std::int32_t* mask, float* top, float* kept, const std::int32_t* share, const float* cover, float stampTop,
                        float keep) {
    const __m256i s = _mm256_load_si256(reinterpret_cast<const __m256i*>(share));
    const __m256i some = _mm256_cmpgt_epi32(s, _mm256_setzero_si256());
    const __m256 on = _mm256_castsi256_ps(some);
    const __m256 t = _mm256_load_ps(top), k = _mm256_load_ps(kept);
    _mm256_store_ps(top, _mm256_blendv_ps(t, _mm256_max_ps(t, _mm256_set1_ps(stampTop)), on));
    const __m256 c = _mm256_min_ps(_mm256_max_ps(_mm256_load_ps(cover), _mm256_setzero_ps()), _mm256_set1_ps(1.0f));
    const __m256 step = _mm256_sub_ps(_mm256_set1_ps(1.0f), _mm256_mul_ps(_mm256_set1_ps(1.0f - keep), c));
    _mm256_store_ps(kept, _mm256_blendv_ps(k, _mm256_mul_ps(k, step), on));
    const __m256i m = _mm256_load_si256(reinterpret_cast<const __m256i*>(mask));
    const __m256i left = _mm256_sub_epi32(_mm256_set1_epi32(kMaskOne), m);
    const __m256i add = _mm256_srai_epi32(_mm256_add_epi32(_mm256_mullo_epi32(left, s), _mm256_set1_epi32(kMaskOne >> 1)), kMaskBits);
    _mm256_store_si256(reinterpret_cast<__m256i*>(mask), _mm256_blendv_epi8(m, _mm256_add_epi32(m, add), some));
}
