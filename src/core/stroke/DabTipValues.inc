// Part of DabTip.cpp, which includes it where it stands: the constants and small helpers the rest of the painter shares.
// docs/brush-engine/dab-tip.md says what the named values here are for and how they were chosen.

namespace {

// By Shape, softest first: a fast event's stamps mix in dabs of at most this many (the Brush has its own, below).
constexpr std::array<std::size_t, 5> kStampsPerDab{16, 12, 8, 6, 4};
// The Brush's own ("Brush only"): its 1st Shape's 2 and 3rd's 6 are its best fit to the reference's quick line at 18 px
// an event (7.94 levels off against 9.62 at 16; 2.70 against 6.14 at 8). Water Color, the Marker, Blur and Smudge keep kStampsPerDab:
// Blur and Smudge start on the 1st Shape, and the line wasn't read for them.
constexpr std::array<std::size_t, 5> kBrushStampsPerDab{2, 12, 6, 6, 4};
// The most a pass's shape counts as, so its -ln(1 - shape) stays finite (ad8db61): 0.99 to 0.9999 paint the reference set the same;
// on the 4th Shape's grid at 1500 px they move a few pixels by 36 in 65535 at most.
constexpr float kMostPassedShape = 0.999f;
// The outline along the path (alongTable): its table's places and steps, and where a pass's profile turns whole.
constexpr int kAlongSamples = 256;
constexpr int kAlongSteps = 512;
constexpr double kAlongRatioSteps = 256.0;
constexpr double kAlongKneeShape = 0.95;
constexpr float kWholeAlongShape = 0.995f;
const float kWholeAlongPasses = -std::log1p(-kWholeAlongShape);
constexpr double kSettleStamps =
    6.0;  // Persistence settles down to its setting over this many stamps from the start of a stroke; fitted on the Brush, whose mocks fit alike from 0 to 12
constexpr double kPersistenceSpacing =
    0.05;  // counted as stamps this share of the diameter apart, so sparser stamps forget as fast. The Brush's own spacing, so its stamps count one each and every count of stamps in mixing is in them
// A pixel fainter than this counts as empty when picked up: alpha 2 of 255 is empty, 3 is paint.
constexpr float kEmptyAlpha = 3.0f / 256.0f;
// The least 16-bit alpha readPixel doesn't read as empty (below kEmptyAlpha).
const int kEmptyChannel = [] {
    int channel = 0;
    while (static_cast<float>(channel) * kChannelToFloat < kEmptyAlpha)
        ++channel;
    return channel;
}();
// Water Color's detail is its blur less one this many times wider; no tool turns the detail on, so today it only widens how
// far the Marker, from 500 px, looks for paint under its melt, and which records a stroke short of memory packs; 1 to 4 paint the same.
constexpr double kWideBlur = 3.0;
// Above Blending 50 % the Marker's brush colour falls away faster and its carry follows what's under it sooner.
constexpr std::array<std::pair<double, double>, 5> kMarkerBlendingCurveAbove50{
    {{0.5, 1.6}, {0.6, 2.6}, {0.7, 2.7}, {0.85, 3.3}, {0.95, 7.0}}};
constexpr double kMarkerKeepFasterAt100 = 1.3;
constexpr std::array<float, 2> kWaterBlotPower{1.5f, 12.0f};
// The Brush's 1st to 4th Shapes with a blot map: from these Strengths the picture reaches into the stroke's body.
constexpr float kBrushBlotBodyFrom = 0.25f, kBrushBlotBodyTo = 0.5f;
// The Shape whose blot cut the Brush's softer Shapes take, counted from 0: the 5th.
constexpr int kBrushBlotCutShape = 4;
// Water Color's 5th Shape with a blot map takes the Brush's outline, each stamp laying this much paint inside it.
constexpr float kWaterBlotTone = 0.35f;  // a blot map's share on Water Color's 5th Shape
// Over a blot map Water Color's tone follows how close its pen samples fall, as its plain line's does.
constexpr float kWaterBlotBySamples = 0.25f, kWaterBlotSamplesAt = 1.2f;
// Water Color's 1st to 4th Shapes over a blot map: each stamp's dab times min(1, floor + by-samples n).
constexpr float kWaterSoftBlotFloor = 0.4f, kWaterSoftBlotBySamples = 0.45f;
constexpr int kWaterSoftBlotFromShape = 1;  // from the 2nd Shape, counted from 0
constexpr double kBlendingCurve = 1.6;  // Blending 50 % lays 0.33 of the brush colour: 84 in 255, white over red, as the test cases
// A Marker's stamps go this share of their diameter apart, by edge from the softest. The 5th Shape's is fitted to the rim of SAI's Marker
// (2.3 to 2.4 % at 250 to 1200 px); the others keep their old proportion to it, measured on no reference.
constexpr std::array<double, 5> kMarkerSpacing{0.0424, 0.0348, 0.0311, 0.0273, 0.0235};
// How many of these stamps lay what one stamp at its old spacing did (0.203 to 0.1125 of the diameter): what a Marker stamp
// climbs, shows and lays is that much smaller, so a stroke builds as it did per pixel of path.
constexpr double kMarkerStampsPerOldStamp = 4.787;
// How many of the Marker's stamps stand for one of the reference's, by size.
constexpr double kMarkerSpacingDiameter = 85.0;  // on the 1st Shape; toward kMarkerSpacingDiameterFirm on the 5th, in even steps between
// The 5th Shape's, as the reference spaces a firmer Shape's stamps tighter: fitted on its readings (the Marker's mocks at 50 to 120 px,
// the flat-red line, the field, the C stroke and the 20 lines at 200 px), their mean 13.06 levels off at 25 px (18 13.12, 32 13.11,
// 55 13.85, and 85, the 1st Shape's, 14.59), against 15.94 before the reorder; the 20 lines within 0.3 of a level.
constexpr double kMarkerSpacingDiameterFirm = 25.0;
constexpr double kMarkerSpacingPower = 0.9;
constexpr double kMarkerSpacingLeast = 0.2;
// Its colour comes in this much faster than its stamps' opacity, so over paint it mixes as the Brush does at a firm touch, and holds to
// where a falling end is light (3 until round 6).
constexpr float kMarkerColourStrength = 12.0f;
// How much of Persistence a Marker stamp's mix counts for in what the Marker carries.
constexpr double kMarkerKeepPerStamp = 0.08;
// Laid in groups, the Marker's stamps lie as far apart as the reference's, as a share of their diameter by Shape.
constexpr std::array<double, 5> kMarkerEventSpacing{0.148, 0.0786, 0.0511, 0.0329, 0.0235};
// The most stamps in a group, by Shape (1st to 5th), and on the 5th with Anti-Ripple on: the reference's, counted on the C:
// 1 / 1 / 8 / 10 / 12, and 10. Ours with them: 1st 647 groups (646), 3rd 97 (96), 4th 126 (123), 5th at 50 / 200 / 689 / 1500 /
// 4000 px 266 / 167 / 75 / 77 / 70 (264 / 159 / 74 / 73 / 70).
constexpr std::array<std::size_t, 5> kMarkerGroupMost{1, 1, 8, 10, 12};
constexpr std::size_t kMarkerGroupMostAntiRipple = 10;
// How far the stamps recent events brought follow each new event's count, for the Marker's group size.
constexpr double kMarkerRecentFollow = 0.5;
// The carry, fitted per old stamp (kMarkerKeepPerStamp), counts each stamp by its spacing against the old to this power: 0 / 0.3 / 0.5 /
// 1 give the 36 Marker readings 8.99 / 8.57 / 9.08 / 11.02. At 1 the 1st Shape's sparse stamps let go too soon (marker/cross 30.3,
// stripes-cross 37.9); at 0 the 5th Shape's floored stamps at 50 px hold too long (c5-marker-50-plain 13.3).
constexpr double kMarkerKeepSpacingPower = 0.3;
// A group of n Marker stamps keeps Persistence as kMarkerKeepPerStamp times n to this power.
constexpr double kMarkerKeepCountPower = 1.1;
// px: the narrowest diameter spacingRatio divides by, so a stamp with none still has a ratio: any positive width does.
constexpr double kThinnestDiameter = 1e-6;
// The alpha from which a premultiplied colour is read back straight, dividing by it: below it the colour is lost to rounding, and the
// other colour stands in. layMarkerAt's own guard, 1e-6, the value it has always had there; the event laying shows a pixel as it does.
constexpr float kLeastReadableAlpha = 1e-6f;
// A whole Marker stamp's paint over paint counts as if its cover were at most this.
constexpr float kMostMarkerCover = 0.999f;
// A Marker stamp's share of the dab's firmest stamp divides by at least this, so a dab of no opacity isn't 0 / 0 (d6cb8c6); 0 to
// 1e-6 paint the reference set the same, the 4 cases it binds in too.
constexpr float kLeastFirmest = 1e-6f;
constexpr double kBlurFirmFrom = 0.8;  // the pressure from which Blur's blur reaches further
// Smudge's v at the middle is smudgeShare at pressure kSmudgeShareAt, in proportion to it, and never past kSmudgeMostShare, so paint
// at the middle never quite rides with the tip forever.
constexpr double kSmudgeShareAt =
    0.8;  // the pressure the mocks' cross and quick line hold; with kSmudgeShare the six Smudge mocks' least of 0.6 to 1.0
constexpr double kSmudgeMostShare =
    0.99;  // never reached, as v is at most kSmudgeShare / kSmudgeShareAt = 0.9375; any cap from there up paints the same
// Smudge's warp is centred half a pixel past each stamp's centre, on the pixel, so its band is even about the pixel row the stroke
// runs along, as the mock's is; the other tools' marks are centred on the stroke itself.
constexpr double kSmudgeCentreShift = 0.5;  // px
constexpr std::int64_t kParallelPixels = 4096;
// px kept inside a solid reach, so rounding can never take a pixel past where it's whole, as StampTip's kDeepMargin
constexpr float kSolidMargin = 1.0f / 1024.0f;
// A dab is laid on the workers in bands of rows of a tile, so a dab of a few tiles still keeps them busy. Reason for 16 not
// recorded.
constexpr int kRowsPerJob = 16;
// A brush shape's dab goes to the workers from this many pixels, in bands of kPictureRowsPerJob rows.
constexpr std::int64_t kParallelPicturePixels = 256;
constexpr int kPictureRowsPerJob = 2;
// A stroke near its memory limit packs the records of tiles it has left alone this many dabs, with zlib at this level, at most this many
// a dab. The decision measures these values; none other was compared.
constexpr std::uint32_t kDabsBeforePacking = 8;
constexpr std::size_t kMostPackedPerDab = 2048;
constexpr int kPackLevel = 1;
// When behind, a dab takes as many stamps as its spacing fits into this share of their diameter.
constexpr double kBehindDabWidthShare = 0.5;
// px: a Marker wider than this puts more stamps in a dab, as the square of its size over this, up to kMostDabGrowth times as many, so one
// pen event's stamps are one dab, mixed and melted once. The decision measures these values; none other was compared.
constexpr double kWideDabFrom = 500.0;
constexpr double kMostDabGrowth = 4.0;
// A wide Marker's melt is blurred on cells this many times smaller than its reach, what's under each cell taken at kCellSamples places
// a side (a blur that wide is smooth at that scale; checked against the mocks, other values not compared).
constexpr int kCoarseReachInCells = 32;
constexpr double kLeastSigma = 0.5;  // px: the recursive Gaussian's coefficients hold from here
// sigmas read past the dab each way, where the Gaussian has fallen to 1/3000: against Blur's 13 mocks 4 scores better than 3,
// 5 and 6. Blur's Gaussian is on every dab whose blur reaches under 32 px: all below 385 px, and from there the small dabs
// pressure makes. It also sizes the Marker's read from 500 px, where 3 to 5 paint the same.
constexpr double kGaussianReach = 4.0;
// added before Blur's Gaussian and taken off after, so nothing in it nears 0 and no float goes denormal: at 1 every value
// rides between 1 and 2, where floats step evenly by 1.2e-7.
constexpr float kGaussianLift = 1.0f;
constexpr int kCellSamples = 2;  // and each cell is what's under it at this many places a side
// px: Blur's stamps this wide and wider blur on cells a kBlurReachInCells-th of the blur's reach across (measured at these values).
constexpr double kWideBlurFrom = 500.0;
constexpr int kBlurReachInCells = 16;
// How far apart the Brush's grid points are, by the tip's diameter: a pixel apart below kGridTwoFrom.
constexpr double kGridTwoFrom = 500.0;  // px
constexpr double kGridFourFrom = 1000.0;  // px
constexpr double kGridEightFrom = 3000.0;  // px
constexpr int kGridOne = 1, kGridTwo = 2, kGridFour = 4, kGridEight = 8;  // px between points
// A stamp's edge that rises over fewer cells than this is worked out per pixel.
constexpr float kSharpCells = 4.0f;
// A pixel whose four points are all reached reads their weights adding up to exactly 1 (multiples of 1/8 are exact); one with a
// weighted corner missing reads at most 63/64. Any value between decides alike; this one is inside.
constexpr float kAllReached = 0.9999f;
// cells: past a stamp's outline, a point it changed is still read by pixels this far out: a pixel reads points up to a cell away
// along each axis, √2 cells on a diagonal, and 2 is the least whole number that covers it.
constexpr float kReadReachCells = 2.0f;
constexpr double kRowSpare = 1.0;  // px: a row's reach goes this far past an outline, as reachedOnRow's does
// px: a tile counts as reached when an outline comes this near it: a row's kRowSpare, and the pixel a row's ends are rounded out by
// (reachedOnRow's floor less one and ceiling plus one), so no pixel a row may visit lies in a tile left out. Today's value, now named.
constexpr double kTileSpare = 2.0;
// A dab's rows are worked out on the workers only from this many jobs of kRowsPerJob rows: with one, there's nothing to share.
constexpr int kLeastRowJobs = 2;
// px: a band counts as inside a skirt, and inside a stamp's whole outline, only this far within them.
constexpr float kSkirtWholeSpare = 1.0f;
// The most stamps over a band whose distances dabMark keeps on the stack for skirtAt.
constexpr std::size_t kMostSkirtedBand = 64;
// cells: where a skirt is cut inside the stroke, the cut is a ramp this wide, so the grid reads it.
constexpr float kSkirtCutRampCells = kSharpCells;
// cells: the band where a stamp's sharp edge is worked out per pixel reaches this far outside it.
constexpr float kSharpOutsideCells = 2.0f;
// px: and this far inside, past kSharpCells, for the edge's own ramp. Measured on 2026-11-13 and rechecked on 2026-11-15:
// 0 to 4 px trade the Brush's flecks for 2 to 6 % of the exact marks a pixel, the most at small sizes; 4 px takes the reference
// set's worst at 1500 px from 2.5 to 1.6 levels, but tested no better on the recorded fast strokes (2026-11-13).
constexpr float kSharpInsideSpare = 2.0f;
// px: the strip across a stamp's sides is widened this much each way, so rounding loses no pixel; 0 to 2 px paint the same.
constexpr float kSlabSpare = 1.0f;
// a heading with less than this across x runs straight down a column, so the strip never divides by 0; 0 to 1e-3 paint the
// same, the near-vertical line too.
constexpr float kNoSidewaysHeading = 1e-6f;
// as shown(): a pixel fainter than this isn't scaled up to what was under it; 0 to 3e-8 paint the same, and from 1e-7
// Water Color erases paint at alpha 1/255 under it.
constexpr float kLeastShownAlpha = 1e-9f;
constexpr int kLanes = 4;  // pixels the read-back works out at once with SSE2
// A Marker cell whose points differ by more than this in opacity, colour strength or any channel of the stroke's paint is worked
// out per pixel, and goes back to being read between them once every pixel is within it of that reading again: an 8-bit level. With the
// finished stroke shown again from its points, cells within it of each other come out within 0.9 levels of the per-pixel model's.
constexpr float kMarkerSmoothRange = 1.0f / 255.0f;
constexpr int kWideLanes = 8;  // and with AVX2, where the grid's points are at least this far apart
// A stamp whose outline ramps over at least this many cells is read between grid points.
constexpr float kSmoothRimCells = 6.0f;
// px: Water Color's stamps this wide and wider blur what's under on cells, as the Marker (at 555 px, which keeps up, nothing changes)
constexpr double kWideWaterFrom = 800.0;

double lerp(double a, double b, double t) {
    return a + (b - a) * t;
}

float lerp(float a, float b, float t) {
    return a + (b - a) * t;
}

Premultiplied lerp(const Premultiplied& a, const Premultiplied& b, float t) {
    return {lerp(a[0], b[0], t), lerp(a[1], b[1], t), lerp(a[2], b[2], t), lerp(a[3], b[3], t)};
}

// How far a stamp that stands for `weight` of a step takes a pixel, where a whole one takes it `share` of the way.
float overStep(float share, float weight) {
    // 0.99999994 is the largest float below 1, so the log stays finite.
    return weight >= 1.0f ? share : -std::expm1(std::log1p(-std::min(share, 0.99999994f)) * weight);
}

// Every result is rounded the same way, to the nearest step, because rounding shows in slow gradients.
Channel toChannel(float v) {
    return static_cast<Channel>(
        static_cast<int>(std::clamp(v, 0.0f, 1.0f) * kChannelOne + 0.5f));  // never negative, so truncating is flooring
}

Premultiplied readPixel(const Channel* px) {
    return {px[0] * kChannelToFloat, px[1] * kChannelToFloat, px[2] * kChannelToFloat, px[3] * kChannelToFloat};
}

void writePixel(Channel* px, const Premultiplied& p) {
    const Channel alpha = toChannel(p[3]);
    for (std::size_t c = 0; c < 3; ++c)
        px[c] = std::min(toChannel(p[c]), alpha);
    px[3] = alpha;
}

// Every pixel the stamp lays anything on, out to the end of a soft Shape's ramp.
// How far a stamp lays anything: its outline's outer edge, or with a form its picture's corners.
float layReachOf(const Stamp& stamp) {
    return std::max(outerOf(stamp), stamp.formReach);
}

QRect areaOf(const Stamp& stamp) {
    const double reach = std::max(double(layReachOf(stamp)), stamp.radius + 1.0);
    return QRect(QPoint(static_cast<int>(std::floor(stamp.centre.x() - reach)), static_cast<int>(std::floor(stamp.centre.y() - reach))),
                 QPoint(static_cast<int>(std::ceil(stamp.centre.x() + reach)), static_cast<int>(std::ceil(stamp.centre.y() + reach))));
}

// A Marker stamp's soft edge (markerEdgeOf): read() is the distance its profile is read at for a pixel `distance` px from its
// centre, and reach where that read comes to the stamp's outer end. Not moved, it reads the distance as it is.
struct MarkerEdge {
    bool moved = false;
    float half = 0.0f, shift = 0.0f, stretch = 1.0f, reach = 0.0f;  // px, px, -, px
    float read(float distance) const { return moved ? std::max(half + (distance + shift - half) * stretch, 0.0f) : distance; }
};
MarkerEdge markerEdgeOf(const Stamp& stamp);

// The Marker's: out to its soft edge's, which its blur takes a little past the stamp's outer end.
QRect markerAreaOf(const Stamp& stamp) {
    const MarkerEdge edge = markerEdgeOf(stamp);
    const QRect area = areaOf(stamp);
    if (!edge.moved)
        return area;
    const int past = static_cast<int>(std::ceil(std::max(edge.reach - std::max(double(layReachOf(stamp)), stamp.radius + 1.0), 0.0)));
    return area.adjusted(-past, -past, past, past);
}

// The same out to the end of its skirt, if it has one.
QRect reachAreaOf(const Stamp& stamp) {
    const double reach = std::max({double(reachOf(stamp)), double(stamp.formReach), stamp.radius + 1.0});
    return QRect(QPoint(static_cast<int>(std::floor(stamp.centre.x() - reach)), static_cast<int>(std::floor(stamp.centre.y() - reach))),
                 QPoint(static_cast<int>(std::ceil(stamp.centre.x() + reach)), static_cast<int>(std::ceil(stamp.centre.y() + reach))));
}

// expm1 with the last answer kept, per site and thread: across a wide stroke neighbouring pixels ask the same question.
// The same for log1p: most of a Brush's outline is the same shape, so the same question comes straight after itself (98 % of them).
template <int Site> float keptLog1p(float v) {
    static thread_local std::uint32_t in = 0x7fc0dead;
    static thread_local float out = 0.0f;
    const std::uint32_t bits = std::bit_cast<std::uint32_t>(v);
    if (bits != in) {
        in = bits;
        out = std::log1p(v);
    }
    return out;
}

template <int Site> float keptExpm1(float v) {
    static thread_local std::uint32_t in = 0x7fc0dead;
    static thread_local float out = 0.0f;
    const std::uint32_t bits = std::bit_cast<std::uint32_t>(v);
    if (bits != in) {
        in = bits;
        out = std::expm1(v);
    }
    return out;
}

// expm1 for 0 or less in plain float steps, the same in one lane as in four: its series, floor and ln 2 split.
constexpr float kSeriesUpTo = 0.35f;  // |x| below this takes the series
constexpr int kSeriesTerms = 8;
constexpr float kExpFloor = -87.0f;  // exp is under the least normal float from here: ln of it is -87.34
constexpr float kLog2e = 1.44269504088896341f;  // log2(e)
constexpr float kLn2High = 0.693359375f, kLn2Low = -2.12194440e-4f;  // ln 2 in two parts, so x - k ln 2 keeps its bits: Cephes's
// Cephes's expf polynomial for e^r on |r| ≤ ln 2 / 2, highest power first.
constexpr std::array<float, 6> kExpPoly{1.9875691500e-4f, 1.3981999507e-3f, 8.3334519073e-3f,
                                        4.1665795894e-2f, 1.6666665459e-1f, 5.0000001201e-1f};
constexpr int kFloatExponentBias = 127, kFloatMantissaBits = 23;  // IEEE 754 single precision's
// 1 / n for the series' terms from n = 2: multiplied by, where dividing would take far longer.
constexpr std::array<float, kSeriesTerms - 1> kSeriesSteps = [] {
    std::array<float, kSeriesTerms - 1> steps{};
    for (int n = 2; n <= kSeriesTerms; ++n)
        steps[static_cast<std::size_t>(n - 2)] = 1.0f / static_cast<float>(n);
    return steps;
}();

float laneExpm1(float x) {
    if (x <= kExpFloor)
        return -1.0f;
    if (x > -kSeriesUpTo) {
        float term = x, sum = x;
        for (const float step : kSeriesSteps) {
            term = term * x * step;
            sum = sum + term;
        }
        return sum;
    }
    const float k = std::nearbyint(x * kLog2e);
    const float r = (x - k * kLn2High) - k * kLn2Low;
    float p = kExpPoly[0];
    for (std::size_t i = 1; i < kExpPoly.size(); ++i)
        p = p * r + kExpPoly[i];
    const float e = (p * r * r + r) + 1.0f;
    const float scale = std::bit_cast<float>(static_cast<std::int32_t>(k + kFloatExponentBias) << kFloatMantissaBits);
    return e * scale - 1.0f;
}

__m128 laneExpm1(__m128 x) {
    const __m128 floor = _mm_cmple_ps(x, _mm_set1_ps(kExpFloor));
    const __m128 near = _mm_cmpgt_ps(x, _mm_set1_ps(-kSeriesUpTo));
    __m128 term = x, sum = x;
    for (const float step : kSeriesSteps) {
        term = _mm_mul_ps(_mm_mul_ps(term, x), _mm_set1_ps(step));
        sum = _mm_add_ps(sum, term);
    }
    const __m128 xs = _mm_max_ps(x, _mm_set1_ps(kExpFloor));  // the far lanes' answer is -1 anyway
    const __m128i ki = _mm_cvtps_epi32(_mm_mul_ps(xs, _mm_set1_ps(kLog2e)));  // to nearest, ties to even, as nearbyint
    const __m128 k = _mm_cvtepi32_ps(ki);
    const __m128 r = _mm_sub_ps(_mm_sub_ps(xs, _mm_mul_ps(k, _mm_set1_ps(kLn2High))), _mm_mul_ps(k, _mm_set1_ps(kLn2Low)));
    __m128 p = _mm_set1_ps(kExpPoly[0]);
    for (std::size_t i = 1; i < kExpPoly.size(); ++i)
        p = _mm_add_ps(_mm_mul_ps(p, r), _mm_set1_ps(kExpPoly[i]));
    const __m128 e = _mm_add_ps(_mm_add_ps(_mm_mul_ps(_mm_mul_ps(p, r), r), r), _mm_set1_ps(1.0f));
    const __m128 scale = _mm_castsi128_ps(_mm_slli_epi32(_mm_add_epi32(ki, _mm_set1_epi32(kFloatExponentBias)), kFloatMantissaBits));
    const __m128 far = _mm_sub_ps(_mm_mul_ps(e, scale), _mm_set1_ps(1.0f));
    const __m128 answer = _mm_or_ps(_mm_and_ps(near, sum), _mm_andnot_ps(near, far));
    return _mm_or_ps(_mm_and_ps(floor, _mm_set1_ps(-1.0f)), _mm_andnot_ps(floor, answer));
}

template <int Site> float keptLaneExpm1(float v) {
    static thread_local std::uint32_t in = 0x7fc0dead;
    static thread_local float out = 0.0f;
    const std::uint32_t bits = std::bit_cast<std::uint32_t>(v);
    if (bits != in) {
        in = bits;
        out = laneExpm1(v);
    }
    return out;
}

// How many pixels across a grid cell is at a stamp's diameter: Smudge's falloff, from d6cb8c6, the single-precision trial the
// baseline took in. From 800 px cells of 2 are within about a level of the per-pixel result (1.16 at 800 px, 3.0 at 700); cells of 4
// are within 0.58 of a level at 2000 px and would be within a level from about 1500. Each saves the same share of a stroke's time at
// every size (cells of 2 about 12 %, of 4 about 5 %), so 800 is set by the error and 2000 is a choice, not an edge.
int markCellFor(double diameter) {
    constexpr double kCellTwoFrom = 800.0, kCellFourFrom = 2000.0;  // px, the stamp's diameter
    constexpr int kCellOne = 1, kCellTwo = 2, kCellFour = 4;  // px across
    return diameter >= kCellFourFrom ? kCellFour : diameter >= kCellTwoFrom ? kCellTwo : kCellOne;
}

int cellOfPixel(int v, int cell) {
    return v >= 0 ? v / cell : -((-v + cell - 1) / cell);
}

// px past a tile a stamp's solid must cover too for the tile to be written whole; 0 to 2 px paint the same.
constexpr int kWholeMargin = 1;
// Smudge: past this share of its reach, where its falloff is steepest, it's worked out per pixel (d6cb8c6, the single-precision trial
// the baseline took in). Up to 0.95 it adds no error past the grid's own at 800 to 3000 px, at 0.98 it does. Each 0.05 is worth 2 to
// 3 % of a wide Smudge's time: 0.75 costs 4 to 6 %, 0.95 saves 4 to 6 % for more pixels near half a level on 4 px cells.
constexpr float kExactFromReach = 0.85f;

int indexIn(TileCoord coord, int x, int y) {
    return (y - coord.y * Tile::kSize) * Tile::kSize + (x - coord.x * Tile::kSize);
}

int floorDiv(int v, int by);

}

namespace {

const std::array<Premultiplied, Tile::kSize * Tile::kSize> kNothing{};  // an empty what's-under, shared by every tile that has one

std::atomic<bool> s_onePixelAtATime{false};  // see setGridOnePixelAtATime

// All ones, shared by every tile where a column has reached it and can't change.
const std::array<float, Tile::kSize * Tile::kSize>& allZeros() {
    static const std::array<float, Tile::kSize * Tile::kSize> zeros{};
    return zeros;
}

const std::array<float, Tile::kSize * Tile::kSize>& allOnes() {
    static const auto ones = [] {
        std::array<float, Tile::kSize * Tile::kSize> all;
        all.fill(1.0f);
        return all;
    }();
    return ones;
}

}

double solidReach(const std::deque<Stamp>& stamps, std::size_t i) {
    StampMark mark;
    const double reach = deepReach(stamps, i, mark);
    return mark.shape == 1.0 && mark.paint == 1.0 ? reach : -1.0;
}

void setGridOnePixelAtATime(bool on) {
    s_onePixelAtATime.store(on, std::memory_order_relaxed);
}

// The Brush laid in groups: its stamps as far apart as the reference's, as a share of their diameter, by Shape.
constexpr std::array<double, 5> kBrushGroupSpacing{0.187, 0.091, 0.0527, 0.0325, 0.0498};
constexpr double kBrushGroupSpacingAntiRippleOff = 0.0234;
// The most stamps in a group, by Shape (1st to 5th), and on the 5th with Anti-Ripple on: the reference's, counted on the C:
// 3 / 7 / 11 / 16 / 20, and 15. Below it a group holds as many as recent events brought, as the Marker's (kMarkerRecentFollow). On the C
// at 200 px that gives 83 / 87 / 96 / 97 / 87 groups, the reference's 83 / 81 / 93 / 94 / 78.
constexpr std::array<std::size_t, 5> kBrushGroupMost{3, 7, 11, 16, 20};
constexpr std::size_t kBrushGroupMostAntiRipple = 15;
// px: a group's box reaches this far past its stamps' dabs, so a pixel whose middle lies just inside one is in it.
constexpr double kBrushGroupBoxMargin = 1.0;
// px: on the 5th Shape with Anti-Ripple each group's mask is cut at the plate's edge, ending here, this wide.
constexpr float kBrushPlateEdgeEnd = 0.13f;
constexpr float kBrushPlateEdgeWidth = 1.0f;
// Below Sharpness 50 the plate's edge widens inward, to this many px at Sharpness 0.
constexpr float kBrushPlateEdgeWidthSoftest = 1.6f;
// The Brush's plate edge by Sharpness: its end, width, tail and how far it pulls in, and at which sizes.
constexpr std::array<float, 3> kBrushPlateEndBySharpness{0.22f, 0.2f, 0.35f};
constexpr std::array<float, 3> kBrushPlateWidthBySharpness{1.35f, 1.0f, 1.0f};
constexpr float kBrushPlateTailKnee = 0.3f;
constexpr std::array<float, 3> kBrushPlateTailBySharpness{0.75f, 0.6f, 0.35f};
constexpr float kBrushPlatePullIn = 0.3f;
constexpr float kBrushPlateSmallFrom = 8.8f;
constexpr float kBrushPlateSmallTo = 15.0f;
// The Brush's 1st to 4th Shapes at small sizes: the dab's edge with a fixed blur added, by Sharpness.
constexpr std::array<float, 3> kBrushDabBlurBySharpness{1.15f, 0.95f, 0.7f};
constexpr std::array<float, 4> kBrushDabHalfAt{0.545f, 0.762f, 0.882f, 0.961f};
constexpr std::array<float, 4> kBrushDabEdgeWidth{0.417f, 0.235f, 0.145f, 0.0877f};
constexpr std::array<float, 4> kBrushDabBlurShift{0.7f, -0.2f, -0.55f, -0.47f};
// A value at Sharpness 0 / 50 / 100, straight between.
float bySharpness(const std::array<float, 3>& values, double rimShare) {
    const float sharp = 1.0f - static_cast<float>(std::clamp(rimShare, 0.0, 1.0));
    return sharp < 0.5f ? values[0] + (values[1] - values[0]) * sharp * 2.0f : values[1] + (values[2] - values[1]) * (sharp - 0.5f) * 2.0f;
}
// The Brush laid in groups mixes in the reference's order: the brush colour's share, and Blending's curve.
constexpr double kBrushGroupShareScale = 1.7;
constexpr double kBrushGroupBlendingCurve = 2.3;
// The carry step: one stamp keeps 1 - (1 - Persistence) to this power of what it carried.
constexpr double kBrushCarryStepPower = 2.5;
// Laid in groups, the groups' size follows the average of the stamps recent events brought, by this an event.
constexpr double kBrushGroupFollow = 0.25;
// No group holds more than the Shape's most; below kBrushGroupFullFrom px it shrinks toward this share of it.
constexpr double kBrushGroupFullFrom = 30.0;
constexpr double kBrushGroupLeastShare = 0.5;
// Water Color's pickup also counts the bare paper beside and ahead of its mask, out to this many radii.
constexpr std::array<double, 5> kWaterPickUpReach{1.2, 1.2, 1.5, 1.5, 1.2};
constexpr std::array<double, 5> kWaterPickUpReachSmall{1.2, 1.2, 1.5, 1.2, 1.2};
constexpr double kWaterPickUpSmallRadius = 17.5;
constexpr double kWaterPickUpLargeRadius = 25.0;
namespace {
double waterPickUpReach(int edge, double radius) {
    const auto shape = static_cast<std::size_t>(std::clamp(edge, 0, 4));
    const double large = std::clamp((radius - kWaterPickUpSmallRadius) / (kWaterPickUpLargeRadius - kWaterPickUpSmallRadius), 0.0, 1.0);
    return kWaterPickUpReachSmall[shape] + (kWaterPickUpReach[shape] - kWaterPickUpReachSmall[shape]) * large;
}
}  // namespace
// Water Color laid in groups mixes in the reference's order: Dilution, Blending, the share and the fade.
constexpr double kWaterDilutionPower = 1.43;
constexpr float kWaterNearOpaque = 0.05f;
constexpr double kWaterShareScale = 1.243;
constexpr double kWaterBlendingCurve = 3.0;
constexpr double kWaterFadePower = 1.3;
constexpr double kWaterThickShare = 0.5;

// 1 - e^-density from a table, in steps of kCoverStep, read between them.
constexpr float kCoverStep = 1.0f / 256.0f;
constexpr int kCoverSteps = 16 * 256;  // kCoverTop = 16
float coverOfDensity(float density) {
    static const std::array<float, kCoverSteps + 2> table = [] {
        std::array<float, kCoverSteps + 2> values{};
        for (int i = 0; i < kCoverSteps + 2; ++i)
            values[static_cast<std::size_t>(i)] = static_cast<float>(-std::expm1(-static_cast<double>(i) * kCoverStep));
        return values;
    }();
    const float t = std::max(density, 0.0f) / kCoverStep;
    if (!(t < static_cast<float>(kCoverSteps)))
        return table[kCoverSteps];
    const int i = static_cast<int>(t);
    return table[static_cast<std::size_t>(i)] +
           (table[static_cast<std::size_t>(i) + 1] - table[static_cast<std::size_t>(i)]) * (t - static_cast<float>(i));
}

double markerSpacingShare(int edge) {
    return kMarkerSpacing[static_cast<std::size_t>(std::clamp(edge, 0, 4))];
}

double markerEventSpacingShare(int edge) {
    return kMarkerEventSpacing[static_cast<std::size_t>(std::clamp(edge, 0, 4))];
}

double brushGroupSpacingShare(int edge, bool antiRipple) {
    const int shape = std::clamp(edge, 0, 4);
    return shape == 4 && !antiRipple ? kBrushGroupSpacingAntiRippleOff : kBrushGroupSpacing[static_cast<std::size_t>(shape)];
}

DabPainter::DabPainter(const StampTip& tip, const TipMixing& mixing, const StrokePaint& paint, const TiledImage& before, bool onWorkers)
    : m_tip(tip),
      m_mixing(mixing),
      m_paint(paint),
      m_fullFrom(fullCoverageFrom(paint.fullAt)),
      m_onWorkers(onWorkers),
      m_walk(tip),
      m_layer(before),
      m_before(before) {
    m_own = mixing.transparentColour ? Premultiplied{} : Premultiplied{paint.color[0], paint.color[1], paint.color[2], 1.0f};
    const double diameter = 2.0 * tip.radius;
    // A form's picture isn't smooth between a grid's points: it's laid a pixel at a time.
    // The Brush laid in groups works out every pixel of its mask, so it's never on a grid (the skirt's grid-only cut stays off, as at 50 and
    // 200 px, where its fits were made).
    if ((mixing.kind == TipMixing::Kind::Brush || mixing.kind == TipMixing::Kind::WaterColor) && mixing.onGrid && !tip.form &&
        !brushLaidInGroups(mixing, formKeepsOffGroups(tip.form, mixing, tip.edge), paint.mode))
        m_gridCell = diameter >= kGridEightFrom  ? kGridEight
                     : diameter >= kGridFourFrom ? kGridFour
                     : diameter >= kGridTwoFrom  ? kGridTwo
                                                 : kGridOne;
    // Below kGridTwoFrom a pixel apart is the per-pixel Marker itself, so it paints per pixel.
    if (mixing.kind == TipMixing::Kind::Marker && !mixing.transparentColour && mixing.onGrid && !tip.form && diameter >= kGridTwoFrom)
        m_markerCell = diameter >= kGridEightFrom ? kGridEight : diameter >= kGridFourFrom ? kGridFour : kGridTwo;
}
