#include "core/stroke/StrokeMerge.h"

#include "core/util/WorkerPool.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <emmintrin.h>
// Doodlebold needs AVX2 on x86-64 (src/CMakeLists.txt builds the painting files with it); the app refuses other processors at
// start-up (platform::processorIsSupported).
#ifndef __AVX2__
#error "built without AVX2: the painting files need -mavx2 (GCC, Clang) or /arch:AVX2 (MSVC)"
#endif
#include <limits>
#include <mutex>
#include <optional>
#include <vector>

// docs/brush-engine/stroke-merge.md says what the named values here are for and how they were chosen.
namespace doodlebold {

namespace {

std::atomic<bool> s_rebuildsWhole{false};  // see setGridMergeRebuildsWhole

// Fewer than this aren't worth waking the workers for (9ca2888): on 4 cores merges of 2 to 4 tiles are quicker on one thread and of 6
// and more on the workers; on 2 cores the same either way.
constexpr std::size_t kParallelTiles = 4;

Channel overChannel(float source, float keep, Channel below) {
    const float value = source * kChannelOne + keep * below;
    return static_cast<Channel>(std::clamp(roundToNearest(value), 0L, static_cast<long>(kChannelOne)));
}

// Paint less than solid takes the place of what's there in proportion to coverage, so where it's thinner than what's there, it thins it.
void overPixelByChannel(Channel* pixel, const float* premultiplied, float paintAlpha, float coverage) {
    const float keep = 1.0f - coverage;
    const Channel alpha = overChannel(coverage * paintAlpha, keep, pixel[3]);
    for (int channel = 0; channel < 3; ++channel)
        pixel[channel] = std::min(overChannel(premultiplied[channel] * coverage, keep, pixel[channel]), alpha);
    pixel[3] = alpha;
}

// Well under what an 8-bit display can show. Erased paint that fades below this is gone, or a faint trace would keep tiles alive.
constexpr float kNothingLeft = 1.0f / 1024.0f;

float eraseKeep(float coverage) {
    return coverage > 1.0f - kNothingLeft ? 0.0f : 1.0f - coverage;
}

void erasePixelByChannel(Channel* pixel, float coverage) {
    const float keep = eraseKeep(coverage);
    for (int channel = 0; channel < 4; ++channel)
        pixel[channel] = overChannel(0.0f, keep, pixel[channel]);
}

// The four channels of a pixel at once, each lane taking overChannel's steps: the same products and sums, roundToNearest's whole part
// and what's left over against a half, and the clamp. A channel's value is at most 2 · kChannelOne, so it fits the lanes' integers.
constexpr int kHalfRange = 32768;  // SSE2 packs to signed 16 bits only: a channel moves down by this to pack, its top bit flipped after

__m128 belowOf(const Channel* pixel) {
    return _mm_cvtepi32_ps(_mm_unpacklo_epi16(_mm_loadl_epi64(reinterpret_cast<const __m128i*>(pixel)), _mm_setzero_si128()));
}

__m128i roundedChannels(__m128 value) {
    const __m128 size = _mm_andnot_ps(_mm_set1_ps(-0.0f), value);
    const __m128i whole = _mm_cvttps_epi32(size);
    const __m128 left = _mm_sub_ps(size, _mm_cvtepi32_ps(whole));
    const __m128i rounded = _mm_sub_epi32(whole, _mm_castps_si128(_mm_cmpge_ps(left, _mm_set1_ps(0.5f))));  // a true lane is -1
    const __m128i kept = _mm_andnot_si128(_mm_castps_si128(_mm_cmplt_ps(value, _mm_setzero_ps())), rounded);  // below 0 clamps to 0
    const __m128i one = _mm_set1_epi32(kChannelOne);
    const __m128i over = _mm_cmpgt_epi32(kept, one);
    return _mm_or_si128(_mm_and_si128(over, one), _mm_andnot_si128(over, kept));
}

void storeChannels(Channel* pixel, __m128i channels) {
    const __m128i packed = _mm_packs_epi32(_mm_sub_epi32(channels, _mm_set1_epi32(kHalfRange)), _mm_setzero_si128());
    _mm_storel_epi64(reinterpret_cast<__m128i*>(pixel), _mm_xor_si128(packed, _mm_set1_epi16(static_cast<short>(0x8000))));
}

void overPixel(Channel* pixel, const float* premultiplied, float paintAlpha, float coverage) {
    const __m128 paint = _mm_set_ps(paintAlpha, premultiplied[2], premultiplied[1], premultiplied[0]);
    const __m128 source = _mm_mul_ps(paint, _mm_set1_ps(coverage));
    const __m128 value = _mm_add_ps(_mm_mul_ps(source, _mm_set1_ps(static_cast<float>(kChannelOne))),
                                    _mm_mul_ps(_mm_set1_ps(1.0f - coverage), belowOf(pixel)));
    const __m128i laid = roundedChannels(value);
    const __m128i alpha = _mm_shuffle_epi32(laid, _MM_SHUFFLE(3, 3, 3, 3));  // the colour is at most the alpha
    const __m128i above = _mm_cmpgt_epi32(laid, alpha);
    storeChannels(pixel, _mm_or_si128(_mm_and_si128(above, alpha), _mm_andnot_si128(above, laid)));
}

void erasePixel(Channel* pixel, float coverage) {
    const __m128 value = _mm_add_ps(_mm_setzero_ps(), _mm_mul_ps(_mm_set1_ps(eraseKeep(coverage)), belowOf(pixel)));
    storeChannels(pixel, roundedChannels(value));
}

void recolourPixel(Channel* pixel, const float* colour, float coverage) {
    const float keep = 1.0f - coverage;
    const float alpha = pixel[3] * kChannelToFloat;
    for (int channel = 0; channel < 3; ++channel)
        pixel[channel] = std::min(overChannel(colour[channel] * coverage * alpha, keep, pixel[channel]), pixel[3]);
}

// Always made whole by the code that makes one: zero-filling `premultiplied` first would cost a pixel loop time.
struct MixedPaint {  // NOLINT(cppcoreguidelines-pro-type-member-init)
    std::array<float, 3> premultiplied;
    float alpha = 1.0f;
};

void layPixel(Channel* pixel, const StrokePaint& paint, const MixedPaint& mixed, float coverage) {
    switch (paint.mode) {
    case LayMode::Over: {
        overPixel(pixel, mixed.premultiplied.data(), mixed.alpha, coverage);
        return;
    }
    case LayMode::Erase: {
        erasePixel(pixel, coverage);
        return;
    }
    case LayMode::KeepAlpha: {
        const float alpha = std::max(mixed.alpha, 1e-6f);
        const std::array<float, 3> colour{mixed.premultiplied[0] / alpha, mixed.premultiplied[1] / alpha, mixed.premultiplied[2] / alpha};
        recolourPixel(pixel, colour.data(), coverage);
        return;
    }
    case LayMode::Untouched: return;
    }
}

// A soft Shape's edge breaks into the paper's tops: the paper takes paint only above a bar that rises as the outline
// thins.
constexpr float kSoftEdgeBar = 1.1f;
constexpr float kSoftEdgeBarPower = 0.4f;
constexpr float kSoftEdgeRise = 0.6f;
// A hard Shape's edge breaks into the paper's tops too, its bar this many times the soft edge's.
constexpr float kHardEdgeBarDepth = 1.25f;
// On the 2nd to 4th Shapes the hard edge's thinning fades out below its bar over this much of the paper's share.
constexpr float kSoftEdgeHardFade = 0.4f;
constexpr float kHardEdgeBarWidth = 0.4f;

// The Intensity from which the soft edge's bar, and the hard edge's, is whole; below it, it comes in as a share of it, so a line goes from its plain
// self at Intensity 0 without a jump. No reading below half strength: whole from a quarter keeps half strength's fit.
constexpr float kSoftEdgeBarFullFrom = 0.25f;
// Scratch Prs., fitted on the reference's Pencil at 60 px on the round-1 ramp with Paper Grain at full Intensity, at 50 and
// 100 % (layers pencil-scratch*): its bar's ramp over the paper's share. With kScratchDepth 1.2 and kScratchPower 0.4 in RebuiltStroke.cpp,
// 4.51 and 5.57 levels off (0 %: 2.2); depths 1.0 to 2.5, powers 0.1 to 1 and ramps 0.1 to 1.5 tried, 4.96 to 10.4.
constexpr float kScratchWidth = 0.5f;

// Guards for reading a mixing tool's coat from its alpha: a smaller rise is rounding, a higher alpha leaves too little gap.
constexpr float kCoatLeastRise = 1e-4f;
constexpr float kCoatWholeAlpha = 0.999f;

constexpr float kRootTwo = 1.41421356f, kLn2 = 0.693147181f, kTwoOverLn2 = 2.88539008f;  // maths
constexpr float kThird = 1.0f / 3.0f, kFifth = 1.0f / 5.0f, kSeventh = 1.0f / 7.0f;  // the log's series
constexpr float kSixth = 1.0f / 6.0f, kTwentyFourth = 1.0f / 24.0f, kOneHundredTwentieth = 1.0f / 120.0f;  // the exponential's

// log2(x) for x > 0, to about 4e-8: x's exponent, and the series 2 / ln 2 · (y + y^3/3 + y^5/5 + y^7/7) with y = (m - 1) / (m + 1)
// for its mantissa m, taken between √½ and √2. For a mixing tool's coat through the paper, worked out at every pixel of a merge,
// where the library's log1p and expm1 were most of a 4000 px textured Brush's cost. (A division-free polynomial on the
// mantissa, taken twice for log2(1 - a1) - log2(1 - a0), was slower than this and the division for c.)
float log2Near(float x) {
    const std::uint32_t bits = std::bit_cast<std::uint32_t>(x);
    float exponent = static_cast<float>(static_cast<int>((bits >> 23) & 0xffu) - 127);
    float m = std::bit_cast<float>((bits & 0x7fffffu) | 0x3f800000u);
    if (m >= kRootTwo) {
        m *= 0.5f;
        exponent += 1.0f;
    }
    const float y = (m - 1.0f) / (m + 1.0f), y2 = y * y;
    return exponent + y * kTwoOverLn2 * (1.0f + y2 * (kThird + y2 * (kFifth + y2 * kSeventh)));
}

// 2^t for t from -126 to 0, to about 3.3e-6 of itself: 2 to t's whole part, built in the exponent, times √2 · e^g for the rest,
// g = (fraction - 1/2) · ln 2, by its series to g^5.
float exp2Near(float t) {
    const float whole = std::floor(t);
    const float g = (t - whole - 0.5f) * kLn2;
    const float rest = 1.0f + g * (1.0f + g * (0.5f + g * (kSixth + g * (kTwentyFourth + g * kOneHundredTwentieth))));
    return kRootTwo * rest * std::bit_cast<float>(static_cast<std::uint32_t>(static_cast<int>(whole) + 127) << 23);
}

float selectedShare(const SelectionTile* selection, int index) {
    return selection ? selection->v[index] * kChannelToFloat : 1.0f;
}

// The share of the stroke each pixel of a tile shows: a selection's, and a texture's grain. Without a texture it's the
// selection's alone, the same float.
class TileShares {
public:
    TileShares(const SelectionTile* selection, const StrokePaint& paint, TileCoord coord)
        : m_selection(selection),
          m_grain(paint.grain && !paint.paperPerDab ? paint.grain->keptSharesOf(coord) : nullptr),
          m_grained(m_grain != nullptr),
          m_softEdge(paint.softEdge),
          m_hardEdge(paint.hardEdge),
          m_barDepth(paint.grain ? std::min(paint.grain->intensity() / kSoftEdgeBarFullFrom, 1.0f) : 1.0f),
          m_clear(paint.wholeCoatClear),
          m_over(paint.wholeCoatOver),
          m_paint(paint),
          m_whole(paint.grain && (paint.uncoatedDensity > 0.0f || paint.wholeCoatClear > 0.0f) ? paint.grain->keptAlsoOf(coord) : nullptr),
          m_fullFrom(fullCoverageFrom(paint.fullAt)) {}
    // How far a mixing tool's painted tile goes from the tile before, its alpha a0 there before and a1 painted: where the alpha says how
    // much of the pixel the stroke covered, c = (a1 - a0) / (1 - a0), the paper thins that coat as a stamp tool's (thinned(c) / c of
    // the way), so a full coat keeps only the darkest pits; where it can't say, over paint already whole or where the stroke took
    // alpha away, or on Blur, it goes the paper's share of the way. A whole coat (the Marker and Water Color) goes the grain's
    // share, already a whole coat's, times its top by the alpha before. Beside a selection's share.
    float laidBack(int index, float a0, float a1, float outline) const {
        const float selected = selectedShare(m_selection, index);
        if (!m_grained)
            return selected;
        return mixingWayThroughPaper(m_paint, m_grain[static_cast<std::size_t>(index)], a0, a1, outline) * selected;
    }
    // A stamp tool's coverage c through the paper, where its outline covers `shape` of the pixel. Its paint is thinned by the paper's
    // share there: its density, -ln(1 - paint), at most kPaperFullDensity, times the share, so a light coat shows the grain as a share of
    // itself and a full one keeps the paper's darkest pits. The softest Shape's coat is laid through the paper's tops instead,
    // its outline setting how high they must reach (kSoftEdgeBar); a firmer soft Shape goes its share of the way from the hard edge's
    // thinning to that, so the look moves from Shape to Shape as the edge does.
    float cover(int index, float c, float shape, float density = -1.0f) const {
        const float selected = selectedShare(m_selection, index);
        if (!m_grained)
            return c * selected;
        const float share = m_grain[static_cast<std::size_t>(index)];
        if (m_softEdge <= 0.0f) {
            float thin = thinnedHere(c, share, density);
            // Scratch Prs. empties the paper's pits the more, the lighter the coat: those below a bar of its depth times the square root
            // of what the coat leaves uncovered, over kScratchWidth of the paper's share.
            if (m_paint.scratchDepth > 0.0f)
                thin *= std::clamp(1.0f + (share - m_paint.scratchDepth * std::sqrt(std::max(1.0f - c, 0.0f))) / kScratchWidth, 0.0f, 1.0f);
            if (!m_hardEdge || shape >= 1.0f || share >= 1.0f)
                return thin * selected;
            const float bar = m_barDepth * kHardEdgeBarDepth * std::min(kSoftEdgeBar * std::pow(1.0f - shape, kSoftEdgeBarPower), 1.0f);
            return thin * std::clamp(1.0f + (share - bar) / kHardEdgeBarWidth, 0.0f, 1.0f) * selected;
        }
        // Inside a whole outline the bar is 0, as the power would give it, without working it out.
        const float bar = shape >= 1.0f ? 0.0f : m_barDepth * std::min(kSoftEdgeBar * std::pow(1.0f - shape, kSoftEdgeBarPower), 1.0f);
        if (share >= 1.0f)  // the paper's highest tops take all of it, so at Intensity 0 a soft line is its plain self
            return c * selected;
        const float tops = share > bar ? c * std::pow((share - bar) / (1.0f - bar), kSoftEdgeRise) : 0.0f;
        if (m_softEdge >= 1.0f)
            return tops * selected;
        // The hard edge's part fades below the same bar, so a firmer soft Shape leaves no haze past its clumps.
        const float thin =
            thinnedHere(c, share, density) * std::clamp(1.0f + (share - m_paint.softHardDepth * bar) / kSoftEdgeHardFade, 0.0f, 1.0f);
        return (thin + (tops - thin) * m_softEdge) * selected;
    }

    // laidBack for the four pixels from `index`, their alphas before and painted: the same steps in each lane as laidBack, so the same
    // floats.
    __m128 laidBack4(int index, __m128 a0, __m128 a1, const float* outline) const {
        __m128 selected = _mm_set1_ps(1.0f);
        if (m_selection) {
            const __m128i v = _mm_loadl_epi64(reinterpret_cast<const __m128i*>(m_selection->v.data() + index));
            selected = _mm_mul_ps(_mm_cvtepi32_ps(_mm_unpacklo_epi16(v, _mm_setzero_si128())), _mm_set1_ps(kChannelToFloat));
        }
        if (!m_grained)
            return selected;
        const __m128 share = _mm_loadu_ps(m_grain + index);
        const __m128 one = _mm_set1_ps(1.0f);
        __m128 way = share;
        if (m_clear > 0.0f) {
            const __m128 top = _mm_add_ps(_mm_set1_ps(m_clear), _mm_mul_ps(_mm_set1_ps(m_over - m_clear), a0));
            way = _mm_mul_ps(m_whole ? _mm_loadu_ps(m_whole + index) : share, top);
        } else if (!m_paint.thinsCoat) {
            return _mm_mul_ps(share, selected);
        } else {
            const __m128 coated =
                _mm_and_ps(_mm_cmpgt_ps(a1, _mm_add_ps(a0, _mm_set1_ps(kCoatLeastRise))), _mm_cmplt_ps(a0, _mm_set1_ps(kCoatWholeAlpha)));
            if (m_whole)  // kept a tile at a time, as wholeCoatThroughPaper works it out
                way = _mm_mul_ps(_mm_set1_ps(m_paint.uncoatedTop), _mm_loadu_ps(m_whole + index));
            else if (m_paint.uncoatedDensity > 0.0f)
                way = _mm_mul_ps(_mm_set1_ps(m_paint.uncoatedTop),
                                 _mm_sub_ps(one, exp2Near4(_mm_mul_ps(share, _mm_set1_ps(-m_paint.uncoatedDensity / kLn2)))));
            if (_mm_movemask_ps(coated) != 0) {
                const __m128 c = _mm_div_ps(_mm_sub_ps(a1, a0), _mm_sub_ps(one, a0));
                const __m128 fewest = _mm_set1_ps(-kPaperFullDensity / kLn2);
                const __m128 whole = _mm_cmpge_ps(c, one);
                const __m128 bits =
                    _mm_or_ps(_mm_and_ps(whole, fewest), _mm_andnot_ps(whole, _mm_max_ps(log2Near4(_mm_sub_ps(one, c)), fewest)));
                const __m128 coat = _mm_div_ps(_mm_sub_ps(one, exp2Near4(_mm_mul_ps(share, bits))), c);
                way = _mm_or_ps(_mm_and_ps(coated, coat), _mm_andnot_ps(coated, way));
            }
        }
        if (outline && m_paint.edgeBarDepth > 0.0f) {
            const __m128 o = _mm_loadu_ps(outline + index);
            // Only where the stroke reaches: outside it the painted pixel is the one before, whatever the way.
            const __m128 edge = _mm_and_ps(_mm_and_ps(_mm_cmplt_ps(o, one), _mm_cmpgt_ps(o, _mm_setzero_ps())), _mm_cmplt_ps(share, one));
            if (_mm_movemask_ps(edge) != 0) {
                const __m128 soft =
                    _mm_min_ps(_mm_mul_ps(_mm_set1_ps(kSoftEdgeBar),
                                          exp2Near4(_mm_mul_ps(_mm_set1_ps(kSoftEdgeBarPower), log2Near4(_mm_sub_ps(one, o))))),
                               one);
                const __m128 bar = _mm_mul_ps(_mm_set1_ps(m_barDepth * m_paint.edgeBarDepth), soft);
                const __m128 through = _mm_min_ps(
                    _mm_max_ps(_mm_add_ps(one, _mm_div_ps(_mm_sub_ps(share, bar), _mm_set1_ps(m_paint.edgeBarWidth))), _mm_setzero_ps()),
                    one);
                way = _mm_or_ps(_mm_and_ps(edge, _mm_mul_ps(way, through)), _mm_andnot_ps(edge, way));
            }
        }
        return _mm_mul_ps(way, selected);
    }

    // log2Near, four at once.
    static __m128 log2Near4(__m128 x) {
        const __m128i bits = _mm_castps_si128(x);
        __m128 exponent =
            _mm_cvtepi32_ps(_mm_sub_epi32(_mm_and_si128(_mm_srli_epi32(bits, 23), _mm_set1_epi32(0xff)), _mm_set1_epi32(127)));
        __m128 m = _mm_castsi128_ps(_mm_or_si128(_mm_and_si128(bits, _mm_set1_epi32(0x7fffff)), _mm_set1_epi32(0x3f800000)));
        const __m128 big = _mm_cmpge_ps(m, _mm_set1_ps(kRootTwo));
        m = _mm_or_ps(_mm_and_ps(big, _mm_mul_ps(m, _mm_set1_ps(0.5f))), _mm_andnot_ps(big, m));
        exponent = _mm_add_ps(exponent, _mm_and_ps(big, _mm_set1_ps(1.0f)));
        const __m128 one = _mm_set1_ps(1.0f);
        const __m128 y = _mm_div_ps(_mm_sub_ps(m, one), _mm_add_ps(m, one));
        const __m128 y2 = _mm_mul_ps(y, y);
        const __m128 series = _mm_add_ps(
            one, _mm_mul_ps(y2, _mm_add_ps(_mm_set1_ps(kThird),
                                           _mm_mul_ps(y2, _mm_add_ps(_mm_set1_ps(kFifth), _mm_mul_ps(y2, _mm_set1_ps(kSeventh)))))));
        return _mm_add_ps(exponent, _mm_mul_ps(_mm_mul_ps(y, _mm_set1_ps(kTwoOverLn2)), series));
    }

    // exp2Near, four at once, t at most 0: truncating rounds toward 0, so a lane truncated above t is one less.
    static __m128 exp2Near4(__m128 t) {
        const __m128i cut = _mm_cvttps_epi32(t);
        const __m128i above = _mm_castps_si128(_mm_cmpgt_ps(_mm_cvtepi32_ps(cut), t));  // a true lane is -1
        const __m128i whole = _mm_add_epi32(cut, above);
        const __m128 g = _mm_mul_ps(_mm_sub_ps(_mm_sub_ps(t, _mm_cvtepi32_ps(whole)), _mm_set1_ps(0.5f)), _mm_set1_ps(kLn2));
        __m128 rest = _mm_add_ps(_mm_set1_ps(kTwentyFourth), _mm_mul_ps(g, _mm_set1_ps(kOneHundredTwentieth)));
        rest = _mm_add_ps(_mm_set1_ps(kSixth), _mm_mul_ps(g, rest));
        rest = _mm_add_ps(_mm_set1_ps(0.5f), _mm_mul_ps(g, rest));
        rest = _mm_add_ps(_mm_set1_ps(1.0f), _mm_mul_ps(g, rest));
        rest = _mm_add_ps(_mm_set1_ps(1.0f), _mm_mul_ps(g, rest));
        const __m128 power = _mm_castsi128_ps(_mm_slli_epi32(_mm_add_epi32(whole, _mm_set1_epi32(127)), 23));
        return _mm_mul_ps(_mm_mul_ps(_mm_set1_ps(kRootTwo), rest), power);
    }

    // thinned(c) / c for a mixing tool's coat c = (a1 - a0) / (1 - a0), by log2Near and exp2Near: (1 - (1 - c)^share) / c, the density
    // -ln(1 - c) at most kPaperFullDensity.
    static float mixingCoatShare(float a0, float a1, float share) {
        const float c = (a1 - a0) / (1.0f - a0);
        const float bits = c >= 1.0f ? -kPaperFullDensity / kLn2 : std::max(log2Near(1.0f - c), -kPaperFullDensity / kLn2);
        return (1.0f - exp2Near(share * bits)) / c;
    }

private:
    static float thinned(float paint, float share, float most = kPaperFullDensity) {
        const float density = std::min(-std::log1p(-std::min(paint, 1.0f)), most);
        return -std::expm1(-density * share);
    }
    // Taking paint away through the paper (the Eraser, the transparent colour) the paper bites by the density the stamps laid there,
    // summed, kErasingDensityShare of it; without it, a coat counting as full from kPaperFullDensityErasing.
    float thinnedHere(float paint, float share, float density = -1.0f) const {
        if (m_paint.mode == LayMode::Erase && density >= 0.0f) {
            const float summed = std::min(kErasingDensityShare * density, kPaperFullDensity);
            if (density >= m_fullFrom)  // a full coat: by the density summed
                return -std::expm1(-summed * share);
            // Short of full, as at a light touch with Density on pressure, at least what the coat itself says.
            return -std::expm1(-std::max(std::min(-std::log1p(-std::min(paint, 1.0f)), kPaperFullDensityErasing), summed) * share);
        }
        return thinned(paint, share, m_paint.mode == LayMode::Erase ? kPaperFullDensityErasing : kPaperFullDensity);
    }

    const SelectionTile* m_selection;
    const float* m_grain;  // the stroke's, kept for it (PaperGrain::keptSharesOf)
    bool m_grained;
    float m_softEdge;  // how soft the tip's edge is (StrokePaint::softEdge)
    bool m_hardEdge;  // StrokePaint::hardEdge
    float m_barDepth;  // how far the soft edge's bar comes in at the paper's Intensity (kSoftEdgeBarFullFrom)
    float m_clear, m_over;  // StrokePaint::wholeCoatClear and wholeCoatOver
    const StrokePaint& m_paint;  // the stroke's, for a mixing tool's way through the paper (mixingWayThroughPaper)
    const float* m_whole;  // the Brush's whole coat at each pixel, kept with the grain (PaperGrain::keepAlso), or null
    float m_fullFrom;  // the density over shape from which the coat is full (fullCoverageFrom)
};

// What the Clone Stamp lays at a tile's pixels: the source's pixel `sourceOffset` back, premultiplied, with its alpha. The
// source pixels under a tile lie in at most two by two of the source's tiles, looked up once for the tile, not once a pixel. Only
// the canvas is copied from: what lies past its edge doesn't show, so it isn't there to clone.
class ClonedTile {
public:
    ClonedTile(const StrokePaint& paint, TileCoord coord)
        : m_left(coord.x * Tile::kSize - paint.sourceOffset.x()),
          m_top(coord.y * Tile::kSize - paint.sourceOffset.y()),
          m_area(paint.source->rect()),
          m_tileLeft(TiledImage::tileOf(m_left)),
          m_tileTop(TiledImage::tileOf(m_top)) {
        for (int ty = 0; ty < 2; ++ty)
            for (int tx = 0; tx < 2; ++tx)
                m_tiles[ty][tx] = paint.source->tile(m_tileLeft + tx, m_tileTop + ty);
    }

    MixedPaint at(int index) const {
        const int x = m_left + index % Tile::kSize, y = m_top + index / Tile::kSize;
        if (!m_area.contains(x, y))
            return {{0.0f, 0.0f, 0.0f}, 0.0f};
        const int tx = TiledImage::tileOf(x), ty = TiledImage::tileOf(y);
        const Tile* tile = m_tiles[ty - m_tileTop][tx - m_tileLeft];
        if (!tile)
            return {{0.0f, 0.0f, 0.0f}, 0.0f};
        const Channel* p = tile->at(x - tx * Tile::kSize, y - ty * Tile::kSize);
        return {{p[0] * kChannelToFloat, p[1] * kChannelToFloat, p[2] * kChannelToFloat}, p[3] * kChannelToFloat};
    }

private:
    int m_left, m_top;
    QRect m_area;
    int m_tileLeft, m_tileTop;
    std::array<std::array<const Tile*, 2>, 2> m_tiles{};
};

}

float coverageOf(float density, float shape, float opacity, float fullAt) {
    return opacity * shape * std::min(1.0f, -std::expm1(-density / shape) / fullAt);
}

namespace {

// Past the point halving finds, this many values are checked one by one, in case expm1 steps back by a last bit somewhere there.
// Further on its result is more last bits past the point than expm1 is ever out by.
constexpr int kCheckedPastFull = 1 << 16;

template <typename Real, typename Bits> Real findFullCoverage(float fullAt) {
    const auto full = [fullAt](Real perShape) { return -std::expm1(-perShape) / static_cast<Real>(fullAt) >= Real(1); };
    if (!(fullAt > 0.0f) || !full(std::numeric_limits<Real>::max()))
        return std::numeric_limits<Real>::infinity();
    // Positive numbers are in the same order as their bits. 0 isn't full, the largest is.
    Bits notFull = 0;
    Bits isFull = std::bit_cast<Bits>(std::numeric_limits<Real>::max());
    while (isFull - notFull > 1) {
        const Bits middle = notFull + (isFull - notFull) / 2;
        (full(std::bit_cast<Real>(middle)) ? isFull : notFull) = middle;
    }
    for (Bits bits = isFull + 1, checked = 0; checked < kCheckedPastFull; ++bits, ++checked) {
        if (!full(std::bit_cast<Real>(bits))) {
            isFull = bits + 1;
            checked = 0;
        }
    }
    return std::bit_cast<Real>(isFull);
}

// A stroke asks for the same few, so each answer is kept.
template <typename Real, typename Bits> Real fullCoverageKept(float fullAt) {
    static std::mutex guard;
    static std::vector<std::pair<float, Real>> kept;
    const std::scoped_lock lock(guard);
    for (const auto& [at, from] : kept)
        if (std::bit_cast<std::uint32_t>(at) == std::bit_cast<std::uint32_t>(fullAt))
            return from;
    kept.emplace_back(fullAt, findFullCoverage<Real, Bits>(fullAt));
    return kept.back().second;
}

}

float fullCoverageFrom(float fullAt) {
    return fullCoverageKept<float, std::uint32_t>(fullAt);
}

double fullCoverageFromInDoubles(float fullAt) {
    return fullCoverageKept<double, std::uint64_t>(fullAt);
}

void mergeDensityTile(const DensityTile& density, const StrokePaint& paint, Tile& tile, const SelectionTile* selection, TileCoord coord) {
    const float* totals = density.value.data();
    const float full = fullCoverageFrom(paint.fullAt);
    const std::optional<ClonedTile> cloned = paint.source ? std::optional<ClonedTile>(std::in_place, paint, coord) : std::nullopt;
    const TileShares shares(selection, paint, coord);
    for (int i = 0; i < Tile::kSize * Tile::kSize; ++i) {
        const float shape = density.shape[i];
        if (totals[i] <= 0.0f || shape <= 0.0f)  // at a line's very tip, paint can land where its width has reached nothing
            continue;
        // Where the paint is full, as across most of a wide stroke, coverage is opacity times shape, as coverageOf would give it.
        const float laidAll = totals[i] / shape >= full ? paint.opacity * shape : coverageOf(totals[i], shape, paint.opacity, paint.fullAt);
        const float coverage = shares.cover(i, laidAll, shape, totals[i] / shape);
        const MixedPaint mixed = cloned ? cloned->at(i) : MixedPaint{paint.color, 1.0f};
        // Recolouring with a copied pixel goes only as far as it's there, so a clear one leaves the pixel as it was.
        const float laid = paint.source && paint.mode == LayMode::KeepAlpha ? coverage * mixed.alpha : coverage;
        layPixel(tile.px.data() + i * 4, paint, mixed, laid);
    }
}

namespace {

// A plain grid's tile. A cell worked out per pixel merges as mergeDensityTile does; one read between points takes its coverage
// read between its points': down each side, then across. `before` is the tile as it was, for a cell
// worked out again over a tile merged before (onlyChanged); without onlyChanged the tile is the one as it was, and every cell is laid.
void mergeGridTile(const DensityTile& density, const StrokePaint& paint, Tile& tile, const Tile* before, const SelectionTile* selection,
                   TileCoord coord, bool onlyChanged) {
    const int cell = density.gridCell, across = Tile::kSize / cell, side = across + 1;
    const float full = fullCoverageFrom(paint.fullAt);
    const auto shows = [&](std::size_t g) {
        const float value = density.pointValue[g], shape = density.pointShape[g];
        if (value <= 0.0f || shape <= 0.0f)
            return 0.0f;
        return value / shape >= full ? paint.opacity * shape : coverageOf(value, shape, paint.opacity, paint.fullAt);
    };
    const auto outlineAt = [&](std::size_t g) {
        return density.pointValue[g] > 0.0f && density.pointShape[g] > 0.0f ? density.pointShape[g] : 0.0f;
    };
    const auto densityAt = [&](std::size_t g) {
        return density.pointValue[g] > 0.0f && density.pointShape[g] > 0.0f ? density.pointValue[g] / density.pointShape[g] : 0.0f;
    };
    const std::optional<ClonedTile> cloned = paint.source ? std::optional<ClonedTile>(std::in_place, paint, coord) : std::nullopt;
    const TileShares shares(selection, paint, coord);
    // Without a source the paint is the same at every pixel, so a pixel that had what the last one had and takes the same coverage
    // comes out as it did: over an empty layer, most of a wide stroke's pixels.
    std::array<Channel, 4> lastBelow{}, lastLaid{};
    float lastCoverage = -1.0f;
    const auto lay = [&](int i, float coverage) {
        Channel* pixel = tile.px.data() + i * 4;
        if (!cloned) {
            if (coverage == lastCoverage && std::equal(pixel, pixel + 4, lastBelow.begin())) {
                std::copy(lastLaid.begin(), lastLaid.end(), pixel);
                return;
            }
            std::copy(pixel, pixel + 4, lastBelow.begin());
            lastCoverage = coverage;
            layPixel(pixel, paint, MixedPaint{paint.color, 1.0f}, coverage);
            std::copy(pixel, pixel + 4, lastLaid.begin());
            return;
        }
        const MixedPaint mixed = cloned->at(i);
        const float laid = paint.mode == LayMode::KeepAlpha ? coverage * mixed.alpha : coverage;
        layPixel(pixel, paint, mixed, laid);
    };
    for (int cy = 0; cy < across; ++cy) {
        for (int cx = 0; cx < across; ++cx) {
            const std::size_t c = static_cast<std::size_t>(cy * across + cx);
            if (onlyChanged) {
                if (!density.cellChanged[c])
                    continue;
                for (int y = cy * cell; y < (cy + 1) * cell; ++y) {  // back to how it was before the stroke
                    Channel* to = tile.px.data() + static_cast<std::size_t>((y * Tile::kSize + cx * cell) * 4);
                    if (before)
                        std::copy_n(before->px.data() + static_cast<std::size_t>((y * Tile::kSize + cx * cell) * 4), cell * 4, to);
                    else
                        std::fill_n(to, cell * 4, Channel{0});
                }
            }
            density.cellChanged[c] = 0;
            const std::uint8_t state = density.cellState[c];
            if (state == DensityTile::PerPixel) {
                for (int y = cy * cell; y < (cy + 1) * cell; ++y)
                    for (int x = cx * cell; x < (cx + 1) * cell; ++x) {
                        const int i = y * Tile::kSize + x;
                        const float shape = density.shape[static_cast<std::size_t>(i)], value = density.value[static_cast<std::size_t>(i)];
                        if (value <= 0.0f || shape <= 0.0f)
                            continue;
                        const float laidAll =
                            value / shape >= full ? paint.opacity * shape : coverageOf(value, shape, paint.opacity, paint.fullAt);
                        lay(i, shares.cover(i, laidAll, shape, value / shape));
                    }
                continue;
            }
            if (state != DensityTile::Read)
                continue;
            const float c00 = shows(static_cast<std::size_t>(cy * side + cx));
            const float c10 = shows(static_cast<std::size_t>(cy * side + cx + 1));
            const float c01 = shows(static_cast<std::size_t>((cy + 1) * side + cx));
            const float c11 = shows(static_cast<std::size_t>((cy + 1) * side + cx + 1));
            // The outline, read between the points as the coverage is, for the paper's soft edge.
            const float s00 = outlineAt(static_cast<std::size_t>(cy * side + cx)),
                        s10 = outlineAt(static_cast<std::size_t>(cy * side + cx + 1));
            const float s01 = outlineAt(static_cast<std::size_t>((cy + 1) * side + cx)),
                        s11 = outlineAt(static_cast<std::size_t>((cy + 1) * side + cx + 1));
            // And the density the stamps laid, for erasing through the paper.
            const float d00 = densityAt(static_cast<std::size_t>(cy * side + cx)),
                        d10 = densityAt(static_cast<std::size_t>(cy * side + cx + 1));
            const float d01 = densityAt(static_cast<std::size_t>((cy + 1) * side + cx)),
                        d11 = densityAt(static_cast<std::size_t>((cy + 1) * side + cx + 1));
            for (int y = cy * cell; y < (cy + 1) * cell; ++y) {
                const float fy = static_cast<float>(y - cy * cell) / static_cast<float>(cell);
                const float left = (1.0f - fy) * c00 + fy * c01, right = (1.0f - fy) * c10 + fy * c11;
                const float shapeLeft = (1.0f - fy) * s00 + fy * s01, shapeRight = (1.0f - fy) * s10 + fy * s11;
                const float densityLeft = (1.0f - fy) * d00 + fy * d01, densityRight = (1.0f - fy) * d10 + fy * d11;
                for (int x = cx * cell; x < (cx + 1) * cell; ++x) {
                    const float fx = static_cast<float>(x - cx * cell) / static_cast<float>(cell);
                    const int i = y * Tile::kSize + x;
                    lay(i, shares.cover(i, (1.0f - fx) * left + fx * right, (1.0f - fx) * shapeLeft + fx * shapeRight,
                                        (1.0f - fx) * densityLeft + fx * densityRight));
                }
            }
        }
    }
}

}

void reduceGridTile(const DensityTile& density, const StrokePaint& paint, const Tile* before, const SelectionTile* selection,
                    TileCoord coord, int level, float* out) {
    const int cell = density.gridCell, across = Tile::kSize / cell, side = across + 1;
    const int blocks = Tile::kSize >> level, block = 1 << level;
    const float full = fullCoverageFrom(paint.fullAt);
    const auto shows = [&](std::size_t g) {
        const float value = density.pointValue[g], shape = density.pointShape[g];
        if (value <= 0.0f || shape <= 0.0f)
            return 0.0f;
        return value / shape >= full ? paint.opacity * shape : coverageOf(value, shape, paint.opacity, paint.fullAt);
    };
    std::vector<float> pointShows(static_cast<std::size_t>(side * side)), pointOutlines(pointShows.size()),
        pointDensities(pointShows.size());
    for (std::size_t g = 0; g < pointShows.size(); ++g) {
        pointShows[g] = shows(g);
        const bool laid = density.pointValue[g] > 0.0f && density.pointShape[g] > 0.0f;
        pointOutlines[g] = laid ? density.pointShape[g] : 0.0f;
        pointDensities[g] = laid ? density.pointValue[g] / density.pointShape[g] : 0.0f;
    }
    const std::optional<ClonedTile> cloned = paint.source ? std::optional<ClonedTile>(std::in_place, paint, coord) : std::nullopt;
    const TileShares shares(selection, paint, coord);
    std::fill(out, out + blocks * blocks * 4, 0.0f);
    const float share = 1.0f / static_cast<float>(block * block);
    for (int y = 0; y < Tile::kSize; ++y) {
        const int cy = y / cell;
        const float fy = static_cast<float>(y - cy * cell) / static_cast<float>(cell);
        for (int x = 0; x < Tile::kSize; ++x) {
            const int cx = x / cell, i = y * Tile::kSize + x;
            const std::uint8_t state = density.cellState[static_cast<std::size_t>(cy * across + cx)];
            float coverage = 0.0f, outline = 0.0f, laidDensity = 0.0f;
            if (state == DensityTile::PerPixel) {
                const float shape = density.shape[static_cast<std::size_t>(i)], value = density.value[static_cast<std::size_t>(i)];
                if (value > 0.0f && shape > 0.0f) {
                    coverage = value / shape >= full ? paint.opacity * shape : coverageOf(value, shape, paint.opacity, paint.fullAt);
                    outline = shape;
                    laidDensity = value / shape;
                }
            } else if (state == DensityTile::Read) {
                const float fx = static_cast<float>(x - cx * cell) / static_cast<float>(cell);
                const auto between = [&](const std::vector<float>& at) {
                    const float left = (1.0f - fy) * at[static_cast<std::size_t>(cy * side + cx)] +
                                       fy * at[static_cast<std::size_t>((cy + 1) * side + cx)];
                    const float right = (1.0f - fy) * at[static_cast<std::size_t>(cy * side + cx + 1)] +
                                        fy * at[static_cast<std::size_t>((cy + 1) * side + cx + 1)];
                    return (1.0f - fx) * left + fx * right;
                };
                coverage = between(pointShows);
                outline = between(pointOutlines);
                laidDensity = between(pointDensities);
            }
            coverage = shares.cover(i, coverage, outline, laidDensity);
            std::array<float, 4> below{};
            if (before)
                for (std::size_t c = 0; c < 4; ++c)
                    below[c] = before->px[static_cast<std::size_t>(i * 4) + c] * kChannelToFloat;
            std::array<float, 4> laid = below;
            if (coverage > 0.0f) {
                const MixedPaint mixed = cloned ? cloned->at(i) : MixedPaint{paint.color, 1.0f};
                const float keep = 1.0f - coverage;
                switch (paint.mode) {
                case LayMode::Over:
                    laid[3] = std::clamp(coverage * mixed.alpha + keep * below[3], 0.0f, 1.0f);
                    for (std::size_t c = 0; c < 3; ++c)
                        laid[c] = std::min(std::clamp(mixed.premultiplied[c] * coverage + keep * below[c], 0.0f, 1.0f), laid[3]);
                    break;
                case LayMode::Erase: {
                    const float kept = coverage > 1.0f - kNothingLeft ? 0.0f : keep;
                    for (std::size_t c = 0; c < 4; ++c)
                        laid[c] = below[c] * kept;
                    break;
                }
                case LayMode::KeepAlpha: {
                    const float lay = paint.source ? coverage * mixed.alpha : coverage;
                    const float alpha = std::max(mixed.alpha, 1e-6f);
                    for (std::size_t c = 0; c < 3; ++c)
                        laid[c] = std::min(
                            std::clamp(mixed.premultiplied[c] / alpha * lay * below[3] + (1.0f - lay) * below[c], 0.0f, 1.0f), below[3]);
                    break;
                }
                case LayMode::Untouched: break;
                }
            }
            float* o = out + ((y / block) * blocks + x / block) * 4;
            for (std::size_t c = 0; c < 4; ++c)
                o[c] += laid[c] * share;
        }
    }
}

// The alphas of the four pixels from `pixel`, as floats from 0 to 65535: each pixel's alpha is the top 16 bits of its 64.
__m128 alphasOf(const Channel* pixel) {
    const __m128i first = _mm_srli_epi64(_mm_loadu_si128(reinterpret_cast<const __m128i*>(pixel)), 48);
    const __m128i second = _mm_srli_epi64(_mm_loadu_si128(reinterpret_cast<const __m128i*>(pixel + 8)), 48);
    const __m128 both = _mm_shuffle_ps(_mm_castsi128_ps(first), _mm_castsi128_ps(second), _MM_SHUFFLE(2, 0, 2, 0));
    return _mm_cvtepi32_ps(_mm_castps_si128(both));
}

// A pixel laid back toward the one before by `share` (in every lane), its four channels at once: floor(old + (painted - old) · share
// + 1/2). The value is at least 1/2, as the share is 0 to 1 and both pixels are at least 0, and at most 65535.5, so truncating it is
// flooring it.
void layBack(Channel* pixel, const Channel* old, __m128 share) {
    const __m128 was = old ? belowOf(old) : _mm_setzero_ps();
    storeChannels(pixel,
                  _mm_cvttps_epi32(_mm_add_ps(_mm_add_ps(was, _mm_mul_ps(_mm_sub_ps(belowOf(pixel), was), share)), _mm_set1_ps(0.5f))));
}

// Outside a selection the tile stays as it was, and in a soft one it goes part of the way; so does it where a texture's grain shows
// less of the stroke.
void keepSelectedPart(Tile& tile, const Tile* before, const TileShares& shares, const float* outline) {
    // Four pixels' shares at once (laidBack4), then each pixel's four channels at once (layBack), the same steps in each lane.
    const __m128 toFloat = _mm_set1_ps(kChannelToFloat);
    for (int i = 0; i < Tile::kSize * Tile::kSize; i += 4) {
        Channel* px = tile.px.data() + static_cast<std::size_t>(i * 4);
        const Channel* old = before ? before->px.data() + static_cast<std::size_t>(i * 4) : nullptr;
        const __m128 a0 = old ? _mm_mul_ps(alphasOf(old), toFloat) : _mm_setzero_ps();
        const __m128 share = shares.laidBack4(i, a0, _mm_mul_ps(alphasOf(px), toFloat), outline);
        layBack(px, old, _mm_shuffle_ps(share, share, _MM_SHUFFLE(0, 0, 0, 0)));
        layBack(px + 4, old ? old + 4 : nullptr, _mm_shuffle_ps(share, share, _MM_SHUFFLE(1, 1, 1, 1)));
        layBack(px + 8, old ? old + 8 : nullptr, _mm_shuffle_ps(share, share, _MM_SHUFFLE(2, 2, 2, 2)));
        layBack(px + 12, old ? old + 12 : nullptr, _mm_shuffle_ps(share, share, _MM_SHUFFLE(3, 3, 3, 3)));
    }
}

float mixingWayThroughPaper(const StrokePaint& paint, float share, float a0, float a1, float outline) {
    float way = share;
    if (paint.wholeCoatClear > 0.0f) {
        way = wholeCoatThroughPaper(share, paint.wholeCoatDensity) *
              (paint.wholeCoatClear + (paint.wholeCoatOver - paint.wholeCoatClear) * a0);
    } else if (!paint.thinsCoat) {
        return share;
    } else {
        if (paint.uncoatedDensity > 0.0f)
            way = paint.uncoatedTop * wholeCoatThroughPaper(share, paint.uncoatedDensity);
        if (a1 > a0 + kCoatLeastRise && a0 < kCoatWholeAlpha)
            way = TileShares::mixingCoatShare(a0, a1, share);
    }
    if (paint.edgeBarDepth > 0.0f && outline < 1.0f && outline > 0.0f && share < 1.0f) {
        const float depth = paint.grain ? std::min(paint.grain->intensity() / kSoftEdgeBarFullFrom, 1.0f) : 1.0f;
        const float soft = std::min(kSoftEdgeBar * exp2Near(kSoftEdgeBarPower * log2Near(1.0f - outline)), 1.0f);
        const float bar = depth * paint.edgeBarDepth * soft;
        way = way * std::clamp(1.0f + (share - bar) / paint.edgeBarWidth, 0.0f, 1.0f);
    }
    return way;
}

float wholeCoatThroughPaper(float share, float density) {
    return 1.0f - exp2Near(share * (-density / kLn2));
}

void mixingWaysThroughPaper(const StrokePaint& paint, TileCoord coord, int index, const float* a0, const float* a1, int count,
                            float* ways) {
    const TileShares shares(nullptr, paint, coord);
    int i = 0;
    for (; i + 4 <= count; i += 4)
        _mm_storeu_ps(ways + i, shares.laidBack4(index + i, _mm_loadu_ps(a0 + i), _mm_loadu_ps(a1 + i), nullptr));
    for (; i < count; ++i)
        ways[i] = shares.laidBack(index + i, a0[i], a1[i], 1.0f);
}

float mixingCoatShareForTesting(float a0, float a1, float share) {
    return TileShares::mixingCoatShare(a0, a1, share);
}

void overPixelForTesting(Channel* pixel, const std::array<float, 3>& premultiplied, float paintAlpha, float coverage, bool byChannel) {
    (byChannel ? overPixelByChannel : overPixel)(pixel, premultiplied.data(), paintAlpha, coverage);
}

void erasePixelForTesting(Channel* pixel, float coverage, bool byChannel) {
    (byChannel ? erasePixelByChannel : erasePixel)(pixel, coverage);
}

void setGridMergeRebuildsWhole(bool on) {
    s_rebuildsWhole.store(on, std::memory_order_relaxed);
}

void mergeTiles(std::vector<TileMergeJob>& jobs, const StrokePaint& paint) {
    const auto merge = [&paint](TileMergeJob& job) {
        if (job.painted && !job.selected && (!paint.grain || paint.paperPerDab)) {
            job.fresh = job.painted;
            return;
        }
        if (job.painted) {
            const auto kept = std::make_shared<Tile>(*job.painted);
            std::array<float, Tile::kSize * Tile::kSize> outline;
            const bool outlined = job.outlineOf && job.outlineOf(outline.data());
            keepSelectedPart(*kept, job.before.get(), TileShares(job.selected, paint, job.coord), outlined ? outline.data() : nullptr);
            job.fresh = kept;
            return;
        }
        if (job.density->gridCell > 0) {
            // Over the tile as last merged, only the cells changed since.
            const bool onlyChanged = job.previous && !s_rebuildsWhole.load(std::memory_order_relaxed);
            const auto merged = onlyChanged  ? std::make_shared<Tile>(*job.previous)
                                : job.before ? std::make_shared<Tile>(*job.before)
                                             : std::make_shared<Tile>();
            mergeGridTile(*job.density, paint, *merged, job.before.get(), job.selected, job.coord, onlyChanged);
            job.fresh = merged;
            return;
        }
        const auto merged = job.before ? std::make_shared<Tile>(*job.before) : std::make_shared<Tile>();
        mergeDensityTile(*job.density, paint, *merged, job.selected, job.coord);
        job.fresh = merged;
    };
    if (jobs.size() >= kParallelTiles) {
        WorkerPool::forStrokes().run(static_cast<int>(jobs.size()), [&](int i) { merge(jobs[static_cast<std::size_t>(i)]); });
    } else {
        for (TileMergeJob& job : jobs)
            merge(job);
    }
}

void mergeStroke(const StrokeBuffer& buffer, const StrokePaint& paint, TiledImage& target) {
    const QRect area = target.rect();
    for (const auto& [coord, density] : buffer.tiles()) {
        if (area.intersects(coord.pixelRect()))
            mergeDensityTile(*density, paint, *target.writableTile(coord.x, coord.y), nullptr, coord);
    }
}

TiledImage liveFrame(const TiledImage& before, const StrokeBuffer& buffer, const StrokePaint& paint) {
    TiledImage frame = before;
    mergeStroke(buffer, paint, frame);
    return frame;
}

}
