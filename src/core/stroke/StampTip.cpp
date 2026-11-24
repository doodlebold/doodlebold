#include "core/stroke/StampTip.h"
#include "core/stroke/BrushDab.h"

#include <map>
#include <tuple>

#include "core/stroke/PaperGrain.h"
#include "core/stroke/PressureCurve.h"
#include "core/stroke/StrokeMerge.h"
#include "core/util/WorkerPool.h"

#include <immintrin.h>
// Doodlebold needs AVX2 on x86-64 (src/CMakeLists.txt builds the painting files with it); the app refuses other processors at
// start-up (platform::processorIsSupported).
#ifndef __AVX2__
#error "built without AVX2: the painting files need -mavx2 (GCC, Clang) or /arch:AVX2 (MSVC)"
#endif
#include <algorithm>
#include <array>
#include <cmath>
#include <atomic>
#include <bit>
#include <limits>
#include <mutex>
#include <cstdlib>
#include <numbers>
#include <utility>

// docs/brush-engine/stamp-tip.md says what the named values here are for and how they were chosen.
namespace doodlebold {

namespace {

// Past this many deviations Φ is 0 or 1: from 5.42 the table's top entry is exactly 1 in float, as the deep reach and the whole
// chords need; 6 is the first whole number past it, where Φ(-6) is 6.5e-5 of a 16-bit level.
constexpr float kNormalReach = 6.0f;
constexpr float kBlurReach = 2.5f;  // a blurred outline stops this many deviations out, where it's under 2 levels in 255
// A straight ramp this many deviations wide fades from 10 % to 90 % over the same width as a blur.
constexpr double kRampPerBlur = 3.2;
// An eased ramp, the Brush's soft Shapes', is this much wider again, so its foot fades out as the mocks' do.
constexpr double kEasedRampWidth = 1.3;
// a thinner stamp is spaced as if this wide. Anything up to 2.5 px spaces the same while kLeastSpacing is 0.25 and no share is
// over 0.05; on the Pencil's tip, with its 0.1, anything up to 1 px.
constexpr double kThinnestRadius = 0.5;
// How far an inset outline sits inside the radius, px, by radius, px, straight between: fitted to the widths of
// the reference's ramps at two sizes and its quick line. Past the last it stays the same.
constexpr std::array<std::pair<double, double>, 8> kInsetByRadius{
    {{0.0, 0.0}, {0.5, 0.12}, {1.0, 0.28}, {1.5, 0.33}, {2.5, 0.33}, {3.5, 0.40}, {5.0, 0.44}, {15.0, 0.25}}};
// px: the closest stamps go, on every tip but the Pencil's, which sets its own leastSpacing, kPencilLeastSpacing. No reading of
// the other tools' thin lines settles 0.25.
constexpr double kLeastSpacing = 0.25;
// px: a pen that travels less than this has tapped (the Pencil's tip only).
constexpr double kLongestTap = 1.5;
constexpr float kSolidDensity = 12.0f;  // 1 - exp(-12) rounds to full in 16 bits
constexpr int kLineSamples = 48;  // across a stamp, to add up a line's coverage
constexpr int kOpacitySteps = 24;
// per doubling of radius, about a quarter of a percent each way: half or twice moves no pixel by more than 0.14 of an 8-bit level, but
// in one 4 px grid cell of the Eraser at 1000 px by 1 level, where 144 is the one apart from a near-exact table; a fifth fewer or a
// third more solves.
constexpr double kRadiusSteps = 144.0;
// and target to 1/2048, so a stamp's coverage is off by at most 16 in 65535: against the Brush's mocks 2048 scores as well as 1024,
// 4096 and 8192.
constexpr double kTargetSteps = 2048.0;
constexpr std::int64_t kParallelPixels = 4096;
constexpr std::size_t kMostSkirtNeighbours =
    3;  // the most neighbours either side a skirt is cut by that the whole chords are kept for; the 5th Shape has 3
constexpr int kSpanLanes = 8;  // AVX2's floats at once
// px: a cell passed as out of a stamp's reach is this much further than its reach.
constexpr float kReachSpare = 1.0f;
constexpr std::array<double, 2> kLightFullerShares{0.7, 0.9};  // the density shares between which lightFuller comes up to fuller

int floorDiv(int v, int by) {
    return v >= 0 ? v / by : -((-v + by - 1) / by);
}

constexpr double kLeastPaintRamp =
    1.0;  // px: a step closer than this leaves a stamp's edge no ramp. The largest step whose rings stay under 2 levels of 255 without it
// A stamp's skirt: a Gaussian of kSkirtWidth of its step past the radius, ending at kSkirtReach steps.
constexpr double kSkirtWidth = 0.575;  // steps, its standard deviation: the best of 0.50 to 0.65 by 0.005 on that fit
// Where it ends: the reading's skirt is none by 1.8 steps (54 px). At 0.575 steps wide the fit's worst is 0.0201 of a stamp's density
// ending at 1.8 steps, 0.0202 at 1.9, 0.0204 at 2, 0.0206 at 2.2; 2 is taken as there the Gaussian has fallen to 0.24 % (at 1.8 still
// 0.74 %), so taking what's left off the whole curve, which keeps it ending without a step, shifts it by a quarter of a percent.
constexpr double kSkirtReach = 2.0;  // steps
// Entries in its table over its reach, read between: steps of 1/512 of a step, where the Gaussian's curve (at most 1/kSkirtWidth^2)
// leaves at most 1.5e-6 of a stamp's skirt between them, about a thirtieth of a 16-bit step at full density. At 256 it would be about
// half a step; the table is 4 KB.
constexpr int kSkirtSteps = 1024;  // entries
// A plain tool on a grid: the points' spacing by the tip's diameter, as the Brush's grid (DabTip.cpp): the per-pixel stamps
// below kStampGridTwoFrom, then 2, 4 from kStampGridFourFrom and 8 from kStampGridEightFrom.
constexpr double kStampGridTwoFrom = 500.0;  // px
// A tip this wide and wider holds its side's tone out to its edge (evenSideFrom); below it the final drop is already a few px.
constexpr double kEvenSideFrom = 500.0;  // px
constexpr double kStampGridFourFrom = 1000.0;  // px
constexpr double kStampGridEightFrom = 3000.0;  // px
constexpr int kStampGridTwo = 2, kStampGridFour = 4, kStampGridEight = 8;  // px between points
// A cell is read between its points where its middle pixel's coverage differs from the reading by no more than this.
constexpr float kStampSmoothRange = 1.0f / 255.0f;
// A cell the outline crosses is read between its points only where the outline's share differs by no more than this.
constexpr float kStampShapeSpread = 1.0f / 32.0f;
// Nor where its points' coverage differs by more than this, 16 8-bit levels.
constexpr float kStampSpreadCap = 16.0f / 255.0f;

double distanceBetween(QPointF a, QPointF b) {
    return std::hypot(a.x() - b.x(), a.y() - b.y());
}

double lerp(double a, double b, double t) {
    return a + (b - a) * t;
}

float lerp(float a, float b, float t) {
    return a + (b - a) * t;
}

// From 1 where the rim starts to 0 at its outside, t from 0 to 1.
float rimFalloff(int edge, float t) {
    const float t2 = t * t;
    const float t4 = t2 * t2;
    switch (edge) {
    case 0: return (1.0f - t) * (1.0f - t);
    case 1: return 1.0f - t;
    case 2: return 1.0f - t2;
    case 3: return 1.0f - t4;
    default: return 1.0f - t4 * t4;
    }
}

struct PixelBounds {
    int left, top, right, bottom;  // right and bottom are past the end
};

PixelBounds boundsOf(const Stamp& stamp) {
    double reach = stamp.radius + std::max({0.5f, kBlurReach * stamp.edgeBlur, stamp.rampWidth / 2.0f}) + 0.5;
    reach = std::max(reach, static_cast<double>(stamp.formReach) + 0.5);  // a form's picture, to its corners
    if (stamp.skirt > 0.0f)  // and its skirt's, to where it ends
        reach = std::max(reach, static_cast<double>(reachOf(stamp)) + 0.5);
    return {static_cast<int>(std::floor(stamp.centre.x() - reach)), static_cast<int>(std::floor(stamp.centre.y() - reach)),
            static_cast<int>(std::ceil(stamp.centre.x() + reach)) + 1, static_cast<int>(std::ceil(stamp.centre.y() + reach)) + 1};
}

bool overlaps(const PixelBounds& bounds, const QRect& area) {
    return bounds.left <= area.right() && bounds.right > area.left() && bounds.top <= area.bottom() && bounds.bottom > area.top();
}

QRect rectOf(const PixelBounds& bounds) {
    return QRect(QPoint(bounds.left, bounds.top), QPoint(bounds.right - 1, bounds.bottom - 1));
}

bool isSolid(const DensityTile* tile, int index) {
    return tile && tile->shape[index] >= 1.0f && tile->value[index] >= kSolidDensity;
}

// The standard normal's cumulative distribution, read from a table, since a blurred outline asks for it twice a pixel a stamp. Read
// straight between, Φ errs by at most 0.017 of a 16-bit level at 4096 steps (2048: 0.068); doubling it moves only last bits.
constexpr int kNormalSteps = 4096;

// Made once as the program starts, so a pixel doesn't ask whether it's made yet, as a table inside the function would.
const std::array<float, kNormalSteps + 1> kNormalTable = [] {
    std::array<float, kNormalSteps + 1> t{};
    for (int i = 0; i <= kNormalSteps; ++i)
        t[static_cast<std::size_t>(i)] =
            static_cast<float>(0.5 * std::erfc(-(2.0 * kNormalReach * i / kNormalSteps - kNormalReach) / std::numbers::sqrt2));
    return t;
}();

float normalBelow(float x) {
    const auto& table = kNormalTable;
    const float at = std::clamp((x + kNormalReach) / (2.0f * kNormalReach), 0.0f, 1.0f) * kNormalSteps;
    const auto i = std::min(static_cast<int>(at), kNormalSteps - 1);
    return lerp(table[static_cast<std::size_t>(i)], table[static_cast<std::size_t>(i) + 1], at - static_cast<float>(i));
}

// The skirt's share at kSkirtSteps places over its reach, made once as the program starts, as kNormalTable is.
const std::array<float, kSkirtSteps + 1> kSkirtTable = [] {
    std::array<float, kSkirtSteps + 1> t{};
    const auto gaussian = [](double steps) { return std::exp(-steps * steps / (2.0 * kSkirtWidth * kSkirtWidth)); };
    const double end = gaussian(kSkirtReach);
    for (int i = 0; i <= kSkirtSteps; ++i)
        t[static_cast<std::size_t>(i)] = static_cast<float>((gaussian(kSkirtReach * i / kSkirtSteps) - end) / (1.0 - end));
    return t;
}();

// The ramp, `in` px inside the outer end: a stamp narrower than a pixel lays no more than its width.
float rampAt(const Stamp& stamp, float in) {
    const float t = std::min(in / std::max(stamp.rampWidth, 1.0f), 1.0f);
    return std::min(stamp.easedRamp ? t * t * (3.0f - 2.0f * t) : t, stamp.radius * 2.0f);
}

// A box of the radius blurred by edgeBlur, as a line through the stamp's centre would show it: the two sides of the box, so a thin
// stamp lays as much ink as it's wide but no darker than its width allows. Along that line, the box runs from -radius to +radius; blurred, a pixel `distance` out gets the normal's share
// below each side, the near side's less what's past the far one. Far from the far side, as almost everywhere, that's 1.
float blurredOutline(const Stamp& stamp, float distance) {
    const float sigma = stamp.edgeBlur;
    const float near = normalBelow((stamp.radius - distance) / sigma);
    const float farSide = stamp.radius + distance;
    return farSide >= kNormalReach * sigma ? near : std::max(near + normalBelow(farSide / sigma) - 1.0f, 0.0f);
}

}

float outerOf(const Stamp& stamp) {
    return stamp.radius + (stamp.edgeBlur > 0.0f ? kBlurReach * stamp.edgeBlur : std::max(stamp.rampWidth, 1.0f) / 2.0f);
}

namespace {
float rimCeiling(const Stamp& stamp, float distance);
}

float outlineShareOf(const Stamp& stamp, float distance) {
    const float outer = outerOf(stamp);
    if (distance >= outer)
        return 0.0f;
    const float share = stamp.edgeBlur > 0.0f ? blurredOutline(stamp, distance) : rampAt(stamp, outer - distance);
    return stamp.profile >= 0 ? std::min(share, rimCeiling(stamp, distance)) : share;
}

float skirtShare(const Stamp& stamp, float outside) {
    if (stamp.skirt <= 0.0f)
        return 0.0f;
    const float at = std::max(outside, 0.0f) / (static_cast<float>(kSkirtReach) * stamp.skirt) * kSkirtSteps;
    if (at >= static_cast<float>(kSkirtSteps))
        return 0.0f;
    const auto i = static_cast<int>(at);
    return lerp(kSkirtTable[static_cast<std::size_t>(i)], kSkirtTable[static_cast<std::size_t>(i) + 1], at - static_cast<float>(i));
}

float reachOf(const Stamp& stamp) {
    return stamp.skirt > 0.0f ? std::max(outerOf(stamp), stamp.radius + static_cast<float>(kSkirtReach) * stamp.skirt) : outerOf(stamp);
}

namespace {

// The reference's round dab on the 1st to 4th Shapes, read from one 200 px click a Shape (the
// same shape at 50 px): its coverage every 0.025 of the nominal radius from the centre, to 1.375; and where each ends.
constexpr int kProfileSteps = 56;
constexpr float kProfileStep = 0.025f;
constexpr float kProfile[4][kProfileSteps] = {
    {1.0000f, 0.9992f, 0.9942f, 0.9865f, 0.9752f, 0.9619f, 0.9460f, 0.9266f, 0.9064f, 0.8830f, 0.8583f, 0.8318f, 0.8039f, 0.7750f,
     0.7442f, 0.7130f, 0.6810f, 0.6487f, 0.6162f, 0.5834f, 0.5500f, 0.5169f, 0.4848f, 0.4528f, 0.4213f, 0.3909f, 0.3607f, 0.3317f,
     0.3037f, 0.2770f, 0.2514f, 0.2269f, 0.2040f, 0.1819f, 0.1617f, 0.1426f, 0.1249f, 0.1087f, 0.0939f, 0.0803f, 0.0679f, 0.0570f,
     0.0473f, 0.0387f, 0.0312f, 0.0249f, 0.0194f, 0.0148f, 0.0111f, 0.0080f, 0.0056f, 0.0038f, 0.0024f, 0.0014f, 0.0008f, 0.0000f},
    {1.0000f, 1.0000f, 1.0000f, 1.0000f, 1.0000f, 1.0000f, 1.0000f, 1.0000f, 1.0000f, 1.0000f, 1.0000f, 1.0000f, 1.0000f, 1.0000f,
     1.0000f, 1.0000f, 1.0000f, 1.0000f, 1.0000f, 1.0000f, 0.9997f, 0.9908f, 0.9708f, 0.9397f, 0.8994f, 0.8518f, 0.7968f, 0.7373f,
     0.6747f, 0.6102f, 0.5453f, 0.4803f, 0.4183f, 0.3578f, 0.3021f, 0.2502f, 0.2030f, 0.1616f, 0.1254f, 0.0944f, 0.0684f, 0.0481f,
     0.0319f, 0.0201f, 0.0115f, 0.0060f, 0.0026f, 0.0009f, 0.0000f, 0.0000f, 0.0000f, 0.0000f, 0.0000f, 0.0000f, 0.0000f, 0.0000f},
    {1.0000f, 1.0000f, 1.0000f, 1.0000f, 1.0000f, 1.0000f, 1.0000f, 1.0000f, 1.0000f, 1.0000f, 1.0000f, 1.0000f, 1.0000f, 1.0000f,
     1.0000f, 1.0000f, 1.0000f, 1.0000f, 1.0000f, 1.0000f, 1.0000f, 1.0000f, 1.0000f, 1.0000f, 1.0000f, 1.0000f, 1.0000f, 1.0000f,
     1.0000f, 1.0000f, 0.9980f, 0.9616f, 0.8886f, 0.7835f, 0.6640f, 0.5363f, 0.4116f, 0.2993f, 0.2031f, 0.1265f, 0.0698f, 0.0337f,
     0.0125f, 0.0032f, 0.0000f, 0.0000f, 0.0000f, 0.0000f, 0.0000f, 0.0000f, 0.0000f, 0.0000f, 0.0000f, 0.0000f, 0.0000f, 0.0000f},
    {1.0000f, 1.0000f, 1.0000f, 1.0000f, 1.0000f, 1.0000f, 1.0000f, 1.0000f, 1.0000f, 1.0000f, 1.0000f, 1.0000f, 1.0000f, 1.0000f,
     1.0000f, 1.0000f, 1.0000f, 1.0000f, 1.0000f, 1.0000f, 1.0000f, 1.0000f, 1.0000f, 1.0000f, 1.0000f, 1.0000f, 1.0000f, 1.0000f,
     1.0000f, 1.0000f, 1.0000f, 1.0000f, 1.0000f, 1.0000f, 1.0000f, 1.0000f, 1.0000f, 0.9415f, 0.6523f, 0.3040f, 0.0756f, 0.0062f,
     0.0000f, 0.0000f, 0.0000f, 0.0000f, 0.0000f, 0.0000f, 0.0000f, 0.0000f, 0.0000f, 0.0000f, 0.0000f, 0.0000f, 0.0000f, 0.0000f}};
constexpr std::array<float, 4> kProfileReach{1.37f, 1.19f, 1.10f, 1.04f};
constexpr std::array<float, 4> kProfileCore{0.0f, 0.50f, 0.74f, 0.91f};  // whole to here

}

float profilePaint(int profile, float along) {
    const float at = along / kProfileStep;
    const int i = static_cast<int>(at);
    if (i >= kProfileSteps - 1)
        return 0.0f;
    const float* row = kProfile[std::clamp(profile, 0, 3)];
    return row[i] + (row[i + 1] - row[i]) * (at - static_cast<float>(i));
}

float profileReach(int profile) {
    return kProfileReach[static_cast<std::size_t>(std::clamp(profile, 0, 3))];
}

float profileLineShare(int profile) {
    static const std::array<float, 4> shares = [] {
        std::array<float, 4> s{};
        for (std::size_t e = 0; e < 4; ++e) {
            double sum = 0.0;
            for (int i = 0; i + 1 < kProfileSteps; ++i)
                sum += 0.5 * (double(kProfile[e][i]) + double(kProfile[e][i + 1])) * kProfileStep;
            s[e] = static_cast<float>(std::sqrt(sum));
        }
        return s;
    }();
    return shares[static_cast<std::size_t>(std::clamp(profile, 0, 3))];
}

float profileCore(int profile) {
    return kProfileCore[static_cast<std::size_t>(std::clamp(profile, 0, 3))];
}

// The firmer soft Shapes at small sizes: the reference's 8 px lines on the 3rd and 4th Shapes keep their edge row at
// about one dab's coverage of the pixel however many dabs overlap there, where ours built up to nearly solid (241 against 157 on the
// 4th). So a dab's outline, the most a pixel's coverage can come to, is its profile averaged across a band about the pixel: whole at a
// nominal radius of kRimCeilingFull px and below, gone by kRimCeilingNone. Fitted on the reference's 8 px lines: each Shape's profile
// read kRimCeilingScale times as far out, over kRimCeilingWidth px. The 1st and 2nd Shapes' lines build up from many soft dabs, which a
// dab's own profile would cap far too low, so they keep the whole outline.
namespace {

constexpr float kRimCeilingFull = 4.0f;
constexpr float kRimCeilingNone = 10.0f;
constexpr std::array<float, 4> kRimCeilingScale{1.0f, 1.0f, 0.85f, 1.03f};
constexpr std::array<float, 4> kRimCeilingWidth{1.0f, 1.0f, 1.5f, 1.2f};

bool rimCapped(const Stamp& stamp) {
    return stamp.profile >= 2 && stamp.radius < kRimCeilingNone * profileReach(stamp.profile);
}

float rimCeiling(const Stamp& stamp, float distance) {
    if (!rimCapped(stamp))
        return 1.0f;
    const auto shape = static_cast<std::size_t>(stamp.profile);  // NOLINT(bugprone-signed-char-misuse): 0 to 3, a stamp with a profile
    const float nominal = stamp.radius / profileReach(stamp.profile);
    float sum = 0.0f;
    for (int k = -4; k <= 4; ++k)
        sum += profilePaint(stamp.profile, std::max(distance + kRimCeilingWidth[shape] * 0.125f * static_cast<float>(k), 0.0f) *
                                               kRimCeilingScale[shape] / nominal);
    const float whole = std::clamp((kRimCeilingNone - nominal) / (kRimCeilingNone - kRimCeilingFull), 0.0f, 1.0f);
    return 1.0f - (1.0f - sum / 9.0f) * whole;
}

}  // namespace

float densityOf(float opacity, float paint) {
    const float laid = opacity * paint;
    return laid >= 1.0f ? kSolidDensity : std::min(-std::log1p(-laid), kSolidDensity);
}

float oneStrokeOutline(float opacity, double fullAt, float strength) {
    return std::clamp(opacity / static_cast<float>(fullAt) * strength, 0.0f, 1.0f);
}

float oneStrokeDensity(double fullAt, float light) {
    return densityOf(static_cast<float>(fullAt), light);
}

// The rim alone, without the outline: all a neighbour's limit needs.
float stampPaint(const Stamp& stamp, int edge, float distance, float cosine = 1.0f) {
    const float outer = outerOf(stamp);
    if (distance >= outer)
        return 0.0f;
    if (stamp.profile >= 0)
        return profilePaint(stamp.profile, distance * profileReach(stamp.profile) / stamp.radius);
    const float rim = stamp.band <= 1.0f ? 1.0f : rimFalloff(edge, std::clamp(1.0f - (outer - distance) / stamp.band, 0.0f, 1.0f));
    return stamp.paintRamp > 0.0f && cosine > 0.0f ? std::min(rim, (outer - distance) / (stamp.paintRamp * cosine)) : rim;
}

StampMark stampMark(const Stamp& stamp, int edge, float distance, float cosine) {
    const float outer = outerOf(stamp);
    if (distance >= outer)
        return {};
    float ramp = stamp.edgeBlur > 0.0f ? blurredOutline(stamp, distance) : rampAt(stamp, outer - distance);
    if (stamp.profile >= 0)
        ramp = std::min(ramp, rimCeiling(stamp, distance));
    return {stampPaint(stamp, edge, distance, cosine), ramp};
}

float stampCoverage(const Stamp& stamp, int edge, float distance) {
    const StampMark mark = stampMark(stamp, edge, distance);
    return std::min(mark.paint, mark.shape);
}

// The density a long line lays along its centre: each stamp's density across it, added up and divided by the spacing.
double lineDensity(const Stamp& stamp, int edge, double spacing) {
    const double outer = outerOf(stamp);
    const double step = outer / kLineSamples;
    double sum = 0.0;
    for (int i = 0; i < kLineSamples; ++i) {
        const StampMark mark = stampMark(stamp, edge, static_cast<float>((i + 0.5) * step));
        sum += densityOf(stamp.opacity, mark.paint) * mark.shape;
    }
    if (stamp.skirt <= 0.0f)
        return 2.0 * sum * step / spacing;
    // Its skirt, past what its outline lays, all of which a line's centre shows, the stamps either side of it covering it.
    const double reach = reachOf(stamp) - stamp.radius;
    const double skirtStep = reach / kLineSamples;
    const float density = densityOf(stamp.opacity, 1.0f);
    double skirt = 0.0;
    for (int i = 0; i < kLineSamples; ++i) {
        const auto outside = static_cast<float>((i + 0.5) * skirtStep);
        skirt += density * std::max(skirtShare(stamp, outside) - outlineShareOf(stamp, stamp.radius + outside), 0.0f);
    }
    return 2.0 * (sum * step + skirt * skirtStep) / spacing;
}

// A line's density only grows with opacity, so halving finds it.
double opacityForLine(double radius, double band, int edge, double spacing, double target, double edgeBlur, double rampWidth,
                      bool easedRamp, bool ramped, bool skirted, int profile) {
    if (target <= 0.0)
        return 0.0;
    const double wanted = target >= 1.0 ? double(kSolidDensity) : std::min(-std::log1p(-target), double(kSolidDensity));
    Stamp stamp{QPointF(), static_cast<float>(radius),   static_cast<float>(band),      1.0f,
                1.0,       static_cast<float>(edgeBlur), static_cast<float>(rampWidth), easedRamp};
    stamp.paintRamp = ramped && !skirted ? paintRampFor(spacing) : 0.0f;
    stamp.skirt = skirted ? static_cast<float>(spacing) : 0.0f;
    stamp.profile = static_cast<std::int8_t>(profile);
    double low = 0.0;
    double high = 1.0;
    for (int i = 0; i < kOpacitySteps; ++i) {
        const double middle = (low + high) / 2.0;
        stamp.opacity = static_cast<float>(middle);
        (lineDensity(stamp, edge, spacing) < wanted ? low : high) = middle;
    }
    return high;
}

float paintRampFor(double step) {
    return step > kLeastPaintRamp ? static_cast<float>(step) : 0.0f;
}

double stampSpacing(double radius, double share, double least) {
    return std::max(share * 2.0 * std::max(radius, kThinnestRadius), least > 0.0 ? least : kLeastSpacing);
}

// A soft stamp's profile is limited to the highest of its neighbours' profiles, compared at the same distance from the centre. So
// one that sticks out past the stamps around it, as where a taper crosses from a soft rim to a plain ramp, doesn't leave a seam,
// and an even line is left as it is.
StampMark limitedMark(const std::deque<Stamp>& stamps, std::size_t i, int edge, float distance, float cosine) {
    const Stamp& stamp = stamps[i];
    StampMark mark = stampMark(stamp, edge, distance, cosine);
    if (mark.shape <= 0.0f || stamp.band <= 1.0f)
        return mark;
    const std::size_t first = i >= kStampNeighbours ? i - kStampNeighbours : 0;
    const std::size_t last = std::min(i + kStampNeighbours, stamps.size() - 1);
    float neighbours = -1.0f;
    for (std::size_t j = first; j <= last; ++j) {
        if (j != i)
            neighbours = std::max(neighbours, stampPaint(stamps[j], edge, distance, cosine));
    }
    if (neighbours >= 0.0f)
        mark.paint = std::min(mark.paint, neighbours);
    return mark;
}

float deepReach(const std::deque<Stamp>& stamps, std::size_t i, StampMark& mark) {
    const Stamp& stamp = stamps[i];
    if (stamp.hard) {  // whole inside its radius, and nothing it lays is limited by its neighbours
        mark = {1.0f, 1.0f, 0.0f};
        return stamp.radius;
    }
    if (stamp.radius * 2.0f < 1.0f)
        return -1.0f;
    if (stamp.profile >= 0) {  // whole only in its core, if it has one, and not under the small sizes' ceiling
        if (rimCapped(stamp))
            return -1.0f;
        mark = {1.0f, 1.0f, 0.0f};
        const float core = std::min(profileCore(stamp.profile) * stamp.radius / profileReach(stamp.profile), outerOf(stamp) - 1.0f);
        return core > 0.0f ? core : -1.0f;
    }
    // Its outline: a blur is at its most past kNormalReach deviations in, where normalBelow stops; a ramp is whole a ramp's width in.
    float reach = stamp.edgeBlur > 0.0f ? stamp.radius - kNormalReach * stamp.edgeBlur : outerOf(stamp) - std::max(stamp.rampWidth, 1.0f);
    reach = std::min(reach, outerOf(stamp) - stamp.paintRamp);
    mark = {1.0f, stamp.edgeBlur > 0.0f ? normalBelow(kNormalReach) : 1.0f, 0.0f};
    if (stamp.band <= 1.0f)
        return reach;  // its paint is 1 wherever it lays any, and limitedMark leaves a stamp with no rim to speak of as it is
    // Its rim lays 1 a band in from the outer end; and its paint is held to the most any neighbour's rim lays at the same distance,
    // which is 1 inside that neighbour's band.
    reach = std::min(reach, outerOf(stamp) - stamp.band);
    const std::size_t first = i >= kStampNeighbours ? i - kStampNeighbours : 0;
    const std::size_t last = std::min(i + kStampNeighbours, stamps.size() - 1);
    if (first == last)
        return reach;
    float neighbours = -1.0f;
    for (std::size_t j = first; j <= last; ++j)
        if (j != i)
            neighbours =
                std::max(neighbours, outerOf(stamps[j]) - std::max(stamps[j].band > 1.0f ? stamps[j].band : 0.0f, stamps[j].paintRamp));
    return std::min(reach, neighbours);
}

struct StampLayer::Job {
    TileCoord coord;
    DensityTile* tile = nullptr;
    std::unique_ptr<DensityTile> fresh;  // made here if the buffer had no tile yet
    bool changed = false;

    bool isWhollySolid() const;
};

// Below the thinnest radius a stamp keeps that width and lays less, so a fine line keeps its mass.
StampWalk::Walked StampWalk::stampAt(const Place& place) {
    const double radius = radiusOf(place.shares.size * place.widthShare);
    const double drawn = std::max(radius, m_tip.thinnestRadius);
    const Outline outline = outlineOf(drawn);
    const double opacity = opacityAt(place, drawn);
    Walked walked{{place.pos, static_cast<float>(drawn), static_cast<float>(bandOf(drawn, place.pressure)), static_cast<float>(opacity),
                   place.pressure, static_cast<float>(outline.blur), static_cast<float>(outline.ramp), outline.eased},
                  drawn,
                  opacity};
    walked.stamp.hard = m_tip.hard;
    walked.stamp.profile = static_cast<std::int8_t>(m_tip.profile);
    if (m_tip.eachStampAlone) {  // the Marker's share, opacityAt's target before its ceiling takes the pressure's share again
        const double fade = std::clamp(radiusOf(place.shares.size) / m_tip.thinnestRadius, 0.0, 1.0);
        walked.stamp.share = static_cast<float>(std::min(m_tip.density * place.shares.density * fade, 1.0));
    }
    walked.stamp.nominalRadius = static_cast<float>(std::max(m_tip.radius * place.shares.size * place.widthShare, m_tip.thinnestRadius));
    if (m_tip.form)
        walked.stamp.formReach = m_tip.form->reachFor(m_tip.form->fixedToCanvas() ? walked.stamp.nominalRadius : walked.stamp.radius);
    return walked;
}

// A soft Shape's blur grows with the radius, on top of the tip's own blur or ramp. The radius here is the outline's, which already
// sits softInset of the radius inside, so the share is raised to match. Without a blur, the ramp that stands in for it eases in and
// out, kEasedRampWidth wider.
StampWalk::Outline StampWalk::outlineOf(double radius) const {
    if (m_tip.profile >= 0)  // its profile is in its paint; the outline is whole to its reach, a pixel's ramp at the end
        return {0.0, 1.0};
    if (m_tip.softShare <= 0.0) {
        if (m_tip.solidFrom > 0.0 && radius >= m_tip.solidFrom)  // solid to its radius, a pixel's ramp at the edge
            return {0.0, 1.0};
        return {m_tip.edgeBlur, m_tip.rampWidth};
    }
    const double soft = m_tip.softShare * radius / (1.0 - m_tip.softInset);
    const double base = m_tip.edgeBlur > 0.0 ? m_tip.edgeBlur : m_tip.rampWidth / kRampPerBlur;
    const double blur = std::hypot(base, soft);
    if (m_tip.edgeBlur <= 0.0)
        return {0.0, blur * kRampPerBlur * kEasedRampWidth, true};
    return {blur, m_tip.rampWidth};
}

namespace {

double insetAt(double radius) {
    for (std::size_t i = 1; i < kInsetByRadius.size(); ++i) {
        const auto [r0, inset0] = kInsetByRadius[i - 1];
        const auto [r1, inset1] = kInsetByRadius[i];
        if (radius <= r1)
            return lerp(inset0, inset1, (radius - r0) / (r1 - r0));
    }
    return kInsetByRadius.back().second;
}

}

// The outline can sit a little inside the radius, more on a wide stamp than a fine one, which keeps a hairline's mass.
double StampWalk::radiusOf(double share) const {
    const double radius = m_tip.radius * share;
    if (m_tip.profile >= 0)  // the profile's reach, from the nominal radius
        return radius * profileReach(m_tip.profile);
    return (m_tip.insetOutline ? radius - insetAt(radius) : radius) - m_tip.softInset * radius + m_tip.edgeShift;
}

double StampWalk::bandOf(double radius, double pressure) const {
    double reach = std::min(radius, m_tip.rimReach);
    if (m_tip.rimHardensFrom >= 0.0)
        reach = lerp(radius, reach, std::clamp((pressure - m_tip.rimHardensFrom) / (m_tip.rimHardensTo - m_tip.rimHardensFrom), 0.0, 1.0));
    return std::max(m_tip.rimShare * reach, 1.0);
}

// As the end closes after a lift, the stamps only narrow: they lay what the stroke's full width lays along each pixel of path, shared
// out over their closer spacing. Solved for their own width, the small ones near the point would each lay what a whole line of them
// does, and pile up into a bright nub.
double StampWalk::opacityAt(const Place& place, double drawn) {
    if (m_tip.brushDabRow >= 0) {
        // The Brush laid in groups: solved as the reference's, so a line of its stamps builds to Density times the pressure's share.
        // The target is read to a kBrushTargetSteps-th, so a stroke's few targets are each solved once and kept.
        // Water Color's: the stamp at full pressure, times kWaterStampShare and pressure's share of Density, not solved for it.
        const double targetShare =
            std::clamp(m_tip.density, 0.0, 1.0) * (m_tip.brushDabLinear ? 1.0 : brushTargetShare(m_tip.brushDabRow, place.pressure));
        const double densityShare = std::clamp(place.shares.density, 0.0, 1.0);
        const double power = kWaterShapePressurePower[static_cast<std::size_t>(std::clamp(m_tip.brushDabRow, 0, 4))];
        const double linear = m_tip.brushDabLinear ? m_tip.brushDabScale * std::pow(densityShare, power) : 1.0;
        const auto steps = static_cast<std::int64_t>(std::lround(targetShare * kBrushTargetSteps));
        static thread_local std::map<std::tuple<int, double, std::int64_t>, double> solved;
        const auto key = std::make_tuple(m_tip.brushDabRow, m_tip.spacingShare, steps);
        auto found = solved.find(key);
        if (found == solved.end())
            found =
                solved.emplace(key, brushDabOpacity(m_tip.brushDabRow, m_tip.spacingShare, static_cast<double>(steps) / kBrushTargetSteps))
                    .first;
        return m_tip.brushDabLinear ? std::min(1.0, found->second * linear) : found->second;
    }
    if (m_tip.hard)
        return 1.0;  // every stamp solid
    const double body = radiusOf(place.shares.size);
    const double fade = std::clamp(body / m_tip.thinnestRadius, 0.0, 1.0);
    const bool firming = m_tip.firmPower > 0.0;
    double target = m_tip.density * place.shares.density * fade;
    if (m_tip.eachStampAlone) {  // the Marker's ceiling follows pressure strongly, as its square; on its soft Shapes too
        return std::min(target * place.shares.density, 1.0);
    }
    if (m_tip.profile >= 0 && target < 1.0)  // a line of the reference's dabs is that much lighter, in density, than its target
        target = -std::expm1(std::log1p(-target) * profileLineShare(m_tip.profile));
    const double bodyDrawn = std::max(body, m_tip.thinnestRadius);
    // Disable Density Control Assist: the full stamp's strength times pressure's share of Density, a line building up past it. On the
    // reference's slow ramp, Density on pressure, it leaves the 5th and 1st Shapes 0.73 and 1.93 levels off.
    if (!m_tip.densityAssist)
        return opacityFor(bodyDrawn, fade) * std::clamp(m_tip.density * place.shares.density, 0.0, 1.0);
    double whole = opacityFor(bodyDrawn, target);
    if (firming) {  // the solved share, which below firmFrom only melts what's under, fades out as the firm one comes in
        const double firm = std::clamp((place.shares.density - m_tip.firmFrom) / std::max(1.0 - m_tip.firmFrom, 1e-6), 0.0, 1.0);
        whole = std::max(opacityFor(bodyDrawn, m_tip.density * fade) * std::pow(firm, m_tip.firmPower), whole * (1.0 - firm));
    }
    const double opacity = -std::expm1(std::log1p(-std::min(whole, 1.0 - 1e-12)) * fullerAt(place.shares.density));
    if (place.widthShare >= 1.0)
        return opacity;
    const double share =
        stampSpacing(drawn, m_tip.spacingShare, m_tip.leastSpacing) / stampSpacing(bodyDrawn, m_tip.spacingShare, m_tip.leastSpacing);
    return -std::expm1(std::log1p(-std::min(opacity, 1.0 - 1e-12)) * share);
}

// Solving takes a while and a stroke asks the same thing over and over, so each answer is kept, by radius rounded to kRadiusSteps a
// doubling and target to 1 / kTargetSteps.
// A stamp's fullness at this density share: lightFuller below the first of kLightFullerShares, fuller past the second, straight between.
double StampWalk::fullerAt(double share) const {
    if (m_tip.lightFuller <= 0.0)
        return m_tip.fuller;
    return lerp(m_tip.lightFuller, m_tip.fuller,
                std::clamp((share - kLightFullerShares[0]) / (kLightFullerShares[1] - kLightFullerShares[0]), 0.0, 1.0));
}

double StampWalk::opacityFor(double radius, double target) {
    const auto radiusStep = static_cast<std::int64_t>(std::lround(std::log2(radius) * kRadiusSteps));
    const auto targetStep = static_cast<std::int64_t>(std::lround(target * kTargetSteps));
    const std::uint64_t key = (static_cast<std::uint64_t>(radiusStep + (1 << 20)) << 32) | static_cast<std::uint64_t>(targetStep);
    const auto found = m_opacities.find(key);
    if (found != m_opacities.end())
        return found->second;
    const double rounded = std::exp2(static_cast<double>(radiusStep) / kRadiusSteps);
    const double band = bandOf(rounded);
    const Outline outline = outlineOf(rounded);
    const double solved = opacityForLine(rounded, band, m_tip.edge, stampSpacing(rounded, m_tip.spacingShare, m_tip.leastSpacing),
                                         static_cast<double>(targetStep) / kTargetSteps, outline.blur, outline.ramp, outline.eased,
                                         !m_tip.eachStampAlone, m_tip.skirtNeighbours > 0, m_tip.profile);
    const double opacity = std::min(solved, m_tip.mostOpacity);
    m_opacities.emplace(key, opacity);
    return opacity;
}

namespace {

// With Density on pressure the hairs lay the pressure's share of Density to this power more than a plain stamp: the reference's Marker
// with Flat Face goes as the pressure to the 3.3 where its plain line goes as its square (pressure 0.5 to 0.78 along the C, 3.26 to
// 3.63), so 1.3 more; Water Color, which also has Density on pressure, fits with it.
constexpr double kHairThinPower = 1.3;
// A hair is crossed by fewer stamps than a line's middle, so each lays more: times (stamp / hair radius) to this power.
constexpr double kHairToneExponent = 0.7;
// The exponent grows by this much a doubling of the tool's size past kHairToneSize, px: the reference's Water Color hairs are
// darker the larger the size (Flat Face 0.35 / 0.42 / 0.52 at 35 / 100 / 400 px), ours were the same at every size. Fitted on Water
// Color's 27 bristle cases: 0 / 0.15 / 0.2: 23.0 / 19.7 / 21.0 levels; the Brush, Pencil and Eraser within 0.2 either way.
constexpr double kHairToneBySize = 0.15;
constexpr double kHairToneSize = 100.0;
// The way a stroke that never moved lays its hairs: up the canvas, the map as it's drawn.
constexpr QPointF kHairStillWay{0.0, -1.0};
// With Direction None the map lies as for a stroke going right, east, whichever way the stroke goes: the reference's.
constexpr QPointF kHairFixedWay{1.0, 0.0};
// Direction Auto: over how much path, as a share of the size, the map's turn closes most of its gap to the stroke's way.
constexpr double kHairFollowShare = 0.05;
// A hair's run from its last place is split into pieces that turn the map by at most this, radians (5 degrees), so on a tight curve its
// places come closer and the hair stays smooth; at most kHairMostPieces pieces, so a corner's swing costs a bounded amount.
// Chosen so a 100 px brush's outer hair, 60 px out, steps at most 5 px a piece; untuned.
constexpr double kHairMostTurnStep = 0.0873;
constexpr int kHairMostPieces = 36;
// Pen Direction: below this tilt, degrees, the pen gives no lean and the map lies as None. Qt reports 0 for a pen without tilt.
constexpr double kHairLeastLean = 0.5;
// Detect Turn: a turn this sharp within this much path ends the hairs; they start afresh once the way settles.
constexpr double kHairCornerTurn = std::numbers::pi / 4.0;
constexpr double kHairCornerWindow = 0.15;
constexpr double kHairSettleTurn = std::numbers::pi / 12.0;
constexpr double kHairSettleWindow = 0.05;
// Randomize: how often a hair's wander takes a new aim, how far it wanders, and how much more it lags the turn.
constexpr double kHairWanderLength = 2.3;
constexpr double kHairWanderMost = 0.025;
constexpr double kHairLagMost = 2.0;

QPointF unitOf(QPointF v, QPointF otherwise) {
    const double length = std::hypot(v.x(), v.y());
    return length > 0.0 ? v / length : otherwise;
}

}

namespace {

// splitmix64 (Steele, Lea and Flood, "Fast splittable pseudorandom number generators", 2014), the jitters' random numbers: its
// published step, mixing multipliers and shifts, so it gives the same numbers on every machine and build, as std's engines and
// distributions needn't. The step is also what a stroke's landing place is mixed by into the first state.
constexpr std::uint64_t kSplitMixStep = 0x9E3779B97F4A7C15ull;
constexpr std::uint64_t kSplitMixFirst = 0xBF58476D1CE4E5B9ull, kSplitMixSecond = 0x94D049BB133111EBull;
constexpr int kSplitMixShifts[] = {30, 27, 31};
constexpr int kDoubleBits = 53;  // a double's precision: the top 53 bits make an even number from 0 to 1

}

// A hair's place: its place in the map, wandered by `wander`, turned so the map's top points `turn` and its right is the stroke's
// right, and spread `spread` px a half of the map's span about `centre`.
QPointF StampWalk::hairAt(std::size_t i, QPointF centre, double turn, double spread, QPointF wander) const {
    const Hair& hair = m_tip.bristles->hairs[i];
    const QPointF ahead(std::cos(turn), std::sin(turn));
    const QPointF right(-ahead.y(), ahead.x());  // y down the canvas
    return centre + (right * (hair.across + wander.x()) - ahead * (hair.along + wander.y())) * spread;
}

void StampWalk::beginHairs(const InputSample& first) {
    m_hairsBegun = false;
    m_hairs.assign(m_tip.bristles ? m_tip.bristles->hairs.size() : 0, {});
    m_hairWays.clear();
    m_hairPath = 0.0;
    m_hairCut = false;
    m_hairWandered = 0.0;
    m_hairKnot = 0;
    if (m_tip.hairWander <= 0.0)
        return;
    // From the stroke's own landing place and time, so a stroke replays the same and the same stroke painted again wanders anew.
    m_wanderState = (std::bit_cast<std::uint64_t>(first.pos.x()) * kSplitMixStep ^ std::bit_cast<std::uint64_t>(first.pos.y())) +
                    first.timestampMs * kSplitMixFirst;
    const auto either = [this] { return 2.0 * nextFrom(m_wanderState) - 1.0; };
    for (HairTrack& hair : m_hairs) {
        hair.lag = m_tip.hairWander * kHairLagMost * nextFrom(m_wanderState);
        hair.knots[0] = QPointF(either(), either());
        hair.knots[1] = QPointF(either(), either());
    }
}

// Hair i's wander now, in halves of the map's span: between its two knots by where the stroke's wander is between them, eased in and
// out, times Randomize and how near the map's middle the hair is.
QPointF StampWalk::wanderOf(std::size_t i) const {
    if (m_tip.hairWander <= 0.0)
        return {};
    const Hair& hair = m_tip.bristles->hairs[i];
    const double middle = std::clamp(1.0 - std::hypot(hair.across, hair.along), 0.0, 1.0);
    const double spacing = kHairWanderLength * 2.0 * m_tip.radius;
    const double u = std::clamp(m_hairWandered / spacing - static_cast<double>(m_hairKnot), 0.0, 1.0);
    const double eased = u * u * (3.0 - 2.0 * u);
    const HairTrack& track = m_hairs[i];
    return (track.knots[0] + (track.knots[1] - track.knots[0]) * eased) * (m_tip.hairWander * kHairWanderMost * middle);
}

// The stroke's wander goes on by `by` px; past each knot spacing every hair takes a new knot.
void StampWalk::advanceWander(double by) {
    if (m_tip.hairWander <= 0.0)
        return;
    m_hairWandered += by;
    const double spacing = kHairWanderLength * 2.0 * m_tip.radius;
    const auto either = [this] { return 2.0 * nextFrom(m_wanderState) - 1.0; };
    while (m_hairWandered >= static_cast<double>(m_hairKnot + 1) * spacing) {
        ++m_hairKnot;
        for (HairTrack& hair : m_hairs) {
            hair.knots[0] = hair.knots[1];
            hair.knots[1] = QPointF(either(), either());
        }
    }
}

// The hairs at a stamp going `heading`: the map's turn there, by Direction; with Auto it follows the stroke's way over
// kHairFollowShare of the tool's size, and each hair lags a little more with Randomize; with Detect Turn a turn much sharper than the
// stroke has been taking starts every hair afresh along the new way. Each hair runs on from its place at the last stamp, in
// pieces that turn by kHairMostTurnStep at the most, so a curve's hairs stay smooth; whole whichever way it runs. Its opacity
// lays as much on a hair as the round stamp's on a line (kHairToneExponent), less with Density on pressure (kHairThinPower).
void StampWalk::withHairs(Stamp& stamp, QPointF heading, const Place& here, double densityShare) {
    const double spread = m_tip.bristleSpread * here.shares.size * here.widthShare;
    const QPointF way = unitOf(heading, kHairStillWay);
    const double headingTurn = std::atan2(way.y(), way.x());
    const double step = m_hairsBegun ? distanceBetween(stamp.centre, m_hairCentre) : 0.0;
    const double size = 2.0 * m_tip.radius;
    const bool alongStroke = m_tip.hairWay == StampTip::HairWay::AlongStroke;
    double target = headingTurn;
    if (m_tip.hairWay == StampTip::HairWay::Fixed)
        target = std::atan2(kHairFixedWay.y(), kHairFixedWay.x());
    if (m_tip.hairWay == StampTip::HairWay::WithPen)  // the way the pen leans; without a lean, as None
        target = std::hypot(here.lean.x(), here.lean.y()) > kHairLeastLean ? std::atan2(here.lean.y(), here.lean.x())
                                                                           : std::atan2(kHairFixedWay.y(), kHairFixedWay.x());
    const double headingStep = m_hairsBegun ? std::abs(std::remainder(headingTurn - m_hairHeading, 2.0 * std::numbers::pi)) : 0.0;
    bool corner = false;  // the hairs start afresh here
    bool cut = false;  // or lay none here: cut off at a corner, the way not settled yet
    if (alongStroke && m_tip.hairDetectTurn) {  // the way's turn over the last kHairCornerWindow of the size's path
        m_hairPath += step;
        while (!m_hairWays.empty() && m_hairWays.front().first < m_hairPath - kHairCornerWindow * size)
            m_hairWays.pop_front();
        if (!m_hairCut && m_hairsBegun && !m_hairWays.empty() &&
            std::abs(std::remainder(headingTurn - m_hairWays.front().second, 2.0 * std::numbers::pi)) >= kHairCornerTurn) {
            m_hairCut = true;  // the hairs end at the corner, cut off, and the map doesn't sweep round it
            m_hairWays.clear();
        }
        m_hairWays.emplace_back(m_hairPath, headingTurn);
        if (m_hairCut) {  // once the way turns no more than kHairSettleTurn, since the corner over kHairSettleWindow of the size and on to
            // the next pen sample, afresh along it: a corner's next stamp, a bend's end
            const double settle = kHairSettleWindow * size;
            const auto since =
                std::find_if(m_hairWays.begin(), m_hairWays.end(), [&](const auto& kept) { return kept.first >= m_hairPath - settle; });
            const auto turned = [&](double a, double b) { return std::abs(std::remainder(a - b, 2.0 * std::numbers::pi)); };
            if (turned(headingTurn, since->second) < kHairSettleTurn && turned(m_hairNextHeading, headingTurn) < kHairSettleTurn) {
                m_hairCut = false;
                corner = true;
            } else {
                cut = true;
            }
        }
    }
    // The wander goes on with the path, and the faster the more the map turns: by the arc the map's edge sweeps.
    advanceWander(step + headingStep * spread);
    auto dots = std::make_shared<std::vector<Stamp::HairDot>>();
    float reach = 0.0f;
    const auto addRun = [&](QPointF from, QPointF to) {
        const QPointF a = from - stamp.centre, b = to - stamp.centre;
        dots->push_back({static_cast<float>(b.x()), static_cast<float>(b.y()), static_cast<float>(a.x()), static_cast<float>(a.y())});
        reach = std::max({reach, static_cast<float>(std::hypot(a.x(), a.y())), static_cast<float>(std::hypot(b.x(), b.y()))});
    };
    for (std::size_t i = 0; i < m_hairs.size(); ++i) {
        HairTrack& track = m_hairs[i];
        double turn = target;
        if (m_hairsBegun && !corner) {
            const double follow = kHairFollowShare * size * (1.0 + track.lag);
            const double share = alongStroke && follow > 0.0 ? -std::expm1(-step / follow) : 1.0;
            turn = track.turn + std::remainder(target - track.turn, 2.0 * std::numbers::pi) * share;
        }
        const QPointF wander = wanderOf(i);
        const QPointF place = hairAt(i, stamp.centre, turn, spread, wander);
        if (cut) {  // laid nowhere here; its place kept, for no run starts from it
        } else if (!m_hairsBegun || corner) {  // where it lands, or afresh past a corner: no run from before
            addRun(place, place);
        } else {
            const int pieces = std::clamp(static_cast<int>(std::ceil(std::abs(turn - track.turn) / kHairMostTurnStep)), 1, kHairMostPieces);
            QPointF from = track.place;
            for (int k = 1; k < pieces; ++k) {
                const double t = static_cast<double>(k) / pieces;
                const QPointF to = hairAt(i, m_hairCentre + (stamp.centre - m_hairCentre) * t, track.turn + (turn - track.turn) * t,
                                          m_hairSpread + (spread - m_hairSpread) * t, track.wander + (wander - track.wander) * t);
                addRun(from, to);
                from = to;
            }
            addRun(from, place);
        }
        track.turn = turn;
        track.place = place;
        track.wander = wander;
    }
    m_hairsBegun = true;
    m_hairCentre = stamp.centre;
    m_hairSpread = spread;
    m_hairHeading = headingTurn;
    stamp.hairs = std::move(dots);
    // Hair Min: the hair's width follows the size's pressure, from Hair Min's share of it at none to all of it at full.
    stamp.hairRadius = static_cast<float>(m_tip.hairRadius *
                                          pressureShare(stamp.pressure, m_tip.hairFollowsPressure, m_tip.hairLeast, m_tip.hairSensitivity));
    stamp.formReach = reach + stamp.hairRadius + 1.0f;  // and a pixel's antialiasing
    if (!m_tip.eachStampAlone && stamp.opacity > 0.0f && stamp.opacity < 1.0f) {  // the Marker's is a ceiling, whatever crosses it
        // Its tone as at Hair Min 0, which changes only the hair's width: on the reference's Brush
        // with a light touch a whole-width hair stays as dark (its plates 15.67 / 24.81 -> 7.68 /
        // 16.42 levels; the Pencil's the same).
        const double toneRadius = m_tip.hairRadius * pressureShare(stamp.pressure, m_tip.hairFollowsPressure, 0.0, m_tip.hairSensitivity);
        stamp.opacity =
            static_cast<float>(-std::expm1(std::log1p(-double(stamp.opacity)) * std::pow(stamp.radius / toneRadius, hairToneExponent())));
    }
    const auto lighter = static_cast<float>(std::pow(std::clamp(densityShare, 0.0, 1.0), kHairThinPower));
    stamp.opacity *= lighter;
    if (stamp.share > 0.0f)
        stamp.share *= lighter;
}

double StampWalk::hairToneExponent() const {
    const double size = 2.0 * m_tip.bristleSpread / kHairSpreadShare;  // px, the tool's size
    return kHairToneExponent + kHairToneBySize * std::log2(size / kHairToneSize);
}

float hairCoverage(const Stamp& stamp, float dx, float dy) {
    // Each hair's run from its last place to this one, antialiased: whole to half a pixel inside its edge, none from half a pixel past
    // it, straight between.
    const float edge = stamp.hairRadius + 0.5f;
    float most = 0.0f;
    for (const Stamp::HairDot& dot : *stamp.hairs) {
        if (most >= 1.0f || dx < std::min(dot.dx, dot.fromX) - edge || dx > std::max(dot.dx, dot.fromX) + edge ||
            dy < std::min(dot.dy, dot.fromY) - edge || dy > std::max(dot.dy, dot.fromY) + edge)
            continue;
        const float runX = dot.dx - dot.fromX, runY = dot.dy - dot.fromY, runSquared = runX * runX + runY * runY;
        const float t = runSquared > 0.0f ? std::clamp(((dx - dot.fromX) * runX + (dy - dot.fromY) * runY) / runSquared, 0.0f, 1.0f) : 0.0f;
        const float x = dx - (dot.fromX + runX * t), y = dy - (dot.fromY + runY * t);
        const float squared = x * x + y * y;
        if (squared < edge * edge)
            most = std::max(most, std::clamp(edge - std::sqrt(squared), 0.0f, 1.0f));
    }
    return most;
}

void StampWalk::begin(const InputSample& first, PressureShares shares) {
    beginHairs(first);
    m_last = {first.pos, shares, first.widthShare, first.pressure, QPointF(first.tiltX, first.tiltY)};
    // The jitters' random numbers start from where the stroke lands, so a stroke replays the same and two strokes differ.
    m_jitter = std::bit_cast<std::uint64_t>(first.pos.x()) * kSplitMixStep ^ std::bit_cast<std::uint64_t>(first.pos.y());
    m_before.reset();
    m_waiting.reset();
    m_landing = m_last;
    m_firstMove.reset();
    m_peak = shares;
    m_peakPressure = first.pressure;
    m_untilNext = 0.0;
    m_travelled = 0.0;
    m_lastStamp.reset();
    m_firstTwice = m_tip.penLaysFirstTwice && sensesPressure(first.kind);
    m_placed.clear();
}

std::vector<Stamp> StampWalk::add(const InputSample& sample, PressureShares shares) {
    m_peak = {std::max(m_peak.size, shares.size), std::max(m_peak.density, shares.density)};
    m_peakPressure = std::max(m_peakPressure, sample.pressure);
    const Place to{sample.pos, shares, sample.widthShare, sample.pressure, QPointF(sample.tiltX, sample.tiltY)};
    if (!m_firstMove)
        m_firstMove = to;
    Place& newest = m_waiting ? *m_waiting : m_last;
    if (distanceBetween(newest.pos, to.pos) < 1e-9) {
        // The same place again: it keeps the firmer of the two, so a pen pressing harder in place lays more, and the pressure 0 a lift
        // adds where the pen last was doesn't take away the last sample's.
        if (to.pressure >= newest.pressure)
            newest = to;
        return {};
    }
    if (m_waiting)
        walkTo(*m_waiting, &to);
    m_waiting = to;
    return handOut();
}

// A click is one stamp where the pen landed, at the most pressure it reached.
std::vector<Stamp> StampWalk::end() {
    std::vector<Stamp> stamps = endStamps();
    // A click's or a tap's stamps take the hairs afresh, with Auto as the map is drawn, the stroke going up.
    for (Stamp& stamp : stamps)
        if (m_tip.bristles && !stamp.hairs) {
            m_hairsBegun = false;
            withHairs(stamp, kHairStillWay, {stamp.centre, m_peak, 1.0, m_peakPressure, m_landing.lean}, m_peak.density);
        }
    return stamps;
}

std::vector<Stamp> StampWalk::endStamps() {
    if (m_waiting)
        walkTo(*m_waiting, nullptr);
    m_waiting.reset();
    if (m_travelled <= 0.0 && m_tip.clickShare > 0.0)
        m_placed.push_back(clickStamp());
    else if (m_travelled < kLongestTap && !m_tip.tapIsWalked)
        return tapStamps();
    return std::exchange(m_placed, {});
}

Stamp StampWalk::clickStamp() {
    Stamp stamp = stampAt({m_landing.pos, m_peak, 1.0, m_peakPressure}).stamp;
    stamp.opacity = static_cast<float>(m_tip.clickShare * std::pow(double(stamp.opacity), m_tip.clickPower));
    stamp.click = true;
    if (m_tip.pictureStamps)
        asPicture(stamp, 1.0, 0.0, m_peak.density);
    return stamp;
}

// The stroke's next random number for the jitters, 0 to 1.
double StampWalk::nextJitter() {
    return nextFrom(m_jitter);
}

double StampWalk::nextFrom(std::uint64_t& state) {
    std::uint64_t z = (state += kSplitMixStep);
    z = (z ^ (z >> kSplitMixShifts[0])) * kSplitMixFirst;
    z = (z ^ (z >> kSplitMixShifts[1])) * kSplitMixSecond;
    z ^= z >> kSplitMixShifts[2];
    return std::ldexp(static_cast<double>(z >> (std::numeric_limits<std::uint64_t>::digits - kDoubleBits)), -kDoubleBits);
}

void StampWalk::asPicture(Stamp& stamp, double headingX, double headingY, double densityShare) {
    // The picture spans the tool's size itself, on every tool alike, as the reference's Pencil and Brush clicks are the same to the
    // pixel: not the Pencil's outline, which sits a little inside it (insetOutline).
    if (stamp.nominalRadius > 0.0f)
        stamp.radius = stamp.nominalRadius;
    // Its opacity, the shape's Density times the tool's: Dens. is in the picture's strength, the tool's here.
    stamp.opacity = static_cast<float>(std::clamp(m_tip.density * densityShare, 0.0, 1.0));
    stamp.share = -1.0f;
    stamp.skirt = 0.0f;
    stamp.paintRamp = 0.0f;
    stamp.chordRadius = 0.0f;
    double angle = m_tip.pictureAngle + (m_tip.pictureAlongStroke ? std::atan2(headingY, headingX) : 0.0);
    if (m_tip.pictureAngleJitter > 0.0 || m_tip.pictureSizeJitter > 0.0 || m_tip.pictureScatter > 0.0) {
        // Four draws a stamp whichever jitters are on, so turning one on leaves the others' draws where they were.
        const auto either = [this] { return 2.0 * nextJitter() - 1.0; };
        const double turn = either(), size = either(), along = either(), across = either();
        const double length = std::hypot(headingX, headingY);
        const double ux = length > 0.0 ? headingX / length : 1.0, uy = length > 0.0 ? headingY / length : 0.0;
        const double spread = 2.0 * stamp.radius * m_tip.pictureScatter;
        stamp.centre += QPointF(ux * along - uy * across, uy * along + ux * across) * spread;
        stamp.radius = static_cast<float>(stamp.radius * (1.0 + m_tip.pictureSizeJitter * size));
        angle += m_tip.pictureAngleJitter * turn;
    }
    stamp.formReach = m_tip.form ? m_tip.form->reachFor(stamp.radius) : 0.0f;
    stamp.turnCos = static_cast<float>(std::cos(angle));
    stamp.turnSin = static_cast<float>(std::sin(angle));
}

std::vector<Stamp> StampWalk::tapStamps() {
    Stamp peak = stampAt({m_landing.pos, m_peak, 1.0, m_peakPressure}).stamp;
    if (m_tip.pictureStamps) {
        asPicture(peak, 1.0, 0.0, m_peak.density);
        return {peak};
    }
    if (m_tip.tapShare <= 0.0 || !m_firstMove)
        return {peak};
    const double fade = std::clamp(radiusOf(m_peak.size) / m_tip.thinnestRadius, 0.0, 1.0);
    peak.opacity = static_cast<float>(tapOpacity(peak.radius, m_peak.density) * fade);
    Stamp first = stampAt(*m_firstMove).stamp;
    first.opacity = std::min(peak.opacity, static_cast<float>(tapOpacity(first.radius, m_firstMove->shares.density)));
    return {first, peak};
}

double StampWalk::tapOpacity(double radius, double densityShare) const {
    const double wide = std::pow(std::min(m_tip.tapFullRadius / radius, 1.0), m_tip.tapFalloff);
    return m_tip.tapShare * wide * m_tip.density * densityShare;
}

std::vector<Stamp> StampWalk::handOut() {
    return m_travelled < kLongestTap && !m_tip.tapIsWalked ? std::vector<Stamp>() : std::exchange(m_placed, {});
}

// Along a straight line to the next sample, or along a curve through it as a line of short straight pieces.
void StampWalk::walkTo(const Place& to, const Place* after) {
    if (m_tip.bristles) {  // the way on to the sample after, for Detect Turn's settling
        const QPointF on = after ? after->pos - to.pos : to.pos - m_last.pos;
        m_hairNextHeading = std::atan2(on.y(), on.x());
    }
    Place from = m_last;
    const std::vector<Place> pieces = m_tip.followsCurve ? curveTo(m_last, to, after) : std::vector<Place>{to};
    m_sampleLength = 0.0;
    for (const Place& next : pieces) {
        m_sampleLength += distanceBetween(from.pos, next.pos);
        from = next;
    }
    m_sampleWalked = 0.0;
    from = m_last;
    for (const Place& next : pieces) {
        walkLine(from, next);
        m_sampleWalked += distanceBetween(from.pos, next.pos);
        from = next;
    }
    m_before = m_last;
    m_last = to;
}

// The centripetal Catmull-Rom curve from the last sample to `to`, with the shares changing evenly along its length. With no sample
// on one side, the one on the other is mirrored.
std::vector<StampWalk::Place> StampWalk::curveTo(const Place& from, const Place& to, const Place* after) const {
    const QPointF before = m_before ? m_before->pos : 2.0 * from.pos - to.pos;
    const QPointF next = after ? after->pos : 2.0 * to.pos - from.pos;
    const std::vector<QPointF> line = curveBetween({before, from.pos, to.pos, next});
    std::vector<double> along(line.size(), 0.0);
    for (std::size_t i = 1; i < line.size(); ++i)
        along[i] = along[i - 1] + distanceBetween(line[i - 1], line[i]);
    std::vector<Place> places;
    for (std::size_t i = 1; i < line.size(); ++i)
        places.push_back(placeAlong(from, to, along[i] / along.back(), line[i]));
    if (!places.empty())
        places.back() = to;
    return places;
}

// The shares a share `t` of the way along the curve to `to`, changing evenly.
StampWalk::Place StampWalk::placeAlong(const Place& from, const Place& to, double t, QPointF pos) const {
    return {pos,
            {lerp(from.shares.size, to.shares.size, t), lerp(from.shares.density, to.shares.density, t)},
            lerp(from.widthShare, to.widthShare, t),
            lerp(from.pressure, to.pressure, t),
            from.lean + (to.lean - from.lean) * t};
}

// Each stamp goes its own spacing past the one before, with the shares changing evenly along the line.
void StampWalk::walkLine(const Place& from, const Place& to) {
    const double length = distanceBetween(from.pos, to.pos);
    if (length <= 0.0)
        return;
    double at = m_untilNext;
    while (at <= length) {
        const double t = at / length;
        const Place here{from.pos + (to.pos - from.pos) * t,
                         {lerp(from.shares.size, to.shares.size, t), lerp(from.shares.density, to.shares.density, t)},
                         lerp(from.widthShare, to.widthShare, t),
                         lerp(from.pressure, to.pressure, t),
                         from.lean + (to.lean - from.lean) * t};
        const Walked walked = stampAt(here);
        Stamp stamp = walked.stamp;
        stamp.penSamplesAcross = m_sampleLength > 0.0 ? static_cast<float>(2.0 * walked.radius / m_sampleLength) : 0.0f;
        if (m_tip.gatherWidth > 0.0) {  // how far along the path from the nearest pen sample, in the stamp's diameters
            const double along = m_sampleWalked + at;
            const double share = std::min(along, std::max(m_sampleLength - along, 0.0)) / (2.0 * walked.radius);
            stamp.opacity *= static_cast<float>(1.0 / (1.0 + share / m_tip.gatherWidth));
            stamp.samplesAcross = static_cast<float>(2.0 * walked.radius / m_sampleLength);
        }
        if (m_tip.startFuller > 0.0) {  // how far along the stroke, in startReach of the stamp's diameters
            const double share = (m_travelled + at) / (m_tip.startReach * 2.0 * walked.radius);
            if (share < 1.0)
                stamp.opacity = static_cast<float>(1.0 - std::pow(1.0 - double(stamp.opacity), 1.0 + m_tip.startFuller * (1.0 - share)));
        }
        const double spacing = std::min(stampSpacing(m_tip.softSpacedWide ? walked.radius / (1.0 - m_tip.softInset) : walked.radius,
                                                     m_tip.spacingShare, m_tip.leastSpacing),
                                        m_tip.mostSpacing);
        const std::size_t filledFrom = m_placed.size();
        const int steps = fillIn(here, walked.opacity);
        if (m_tip.bristles)  // the stamps put in between take the hairs too
            for (std::size_t k = filledFrom; k < m_placed.size(); ++k)
                withHairs(m_placed[k], to.pos - from.pos, here, here.shares.density);
        stamp.weight = 1.0 / steps;
        stamp.step = spacing / steps;
        if (!m_tip.eachStampAlone && !m_tip.hard) {  // a hard disc's edge is the same wherever it stands
            if (m_tip.skirtNeighbours > 0)  // whole to its outline, with a skirt past it in place of a ramp inside it
                stamp.skirt = static_cast<float>(stamp.step);
            else
                stamp.paintRamp = paintRampFor(stamp.step);
            stamp.headingX = static_cast<float>((to.pos.x() - from.pos.x()) / length);
            stamp.headingY = static_cast<float>((to.pos.y() - from.pos.y()) / length);
            if (m_tip.evenAcross) {
                stamp.chordRadius = outerOf(stamp);
            } else if (m_tip.evenSideFrom > 0.0 && 2.0 * m_tip.radius >= kEvenSideFrom) {
                stamp.chordRadius = outerOf(stamp);
                stamp.evenFrom = static_cast<float>(m_tip.evenSideFrom);
                stamp.leastChord =
                    static_cast<float>(std::sqrt(std::min(2.0 * m_tip.evenSideTo / stamp.chordRadius, 1.0)));  // a row that far in
            }
        }
        if (m_tip.pictureStamps)
            asPicture(stamp, to.pos.x() - from.pos.x(), to.pos.y() - from.pos.y(), here.shares.density);
        if (m_tip.bristles)
            withHairs(stamp, to.pos - from.pos, here, here.shares.density);
        m_placed.push_back(stamp);
        if (std::exchange(m_firstTwice, false))  // a pen lays its first stamp again as it first moves, a mouse doesn't
            m_placed.push_back(stamp);
        m_lastStamp = here;
        at += spacing;
    }
    m_untilNext = at - length;
    m_travelled += length;
}

// With fadeStep, where the opacity changes by more than that from the last stamp to the one at `here`, stamps go in between, evenly, so
// a fade climbs or falls in steps of about fadeStep, never closer than closestStep. Each stands for its share of the step. Returns
// how many steps the gap now takes.
int StampWalk::fillIn(const Place& here, double opacity) {
    if (m_tip.fadeStep <= 0.0 || !m_lastStamp)
        return 1;
    const Place from = *m_lastStamp;
    const double gap = distanceBetween(from.pos, here.pos);
    const double before = stampAt(from).opacity;
    const int most = std::max(1, static_cast<int>(gap / m_tip.closestStep));
    const int steps = std::clamp(static_cast<int>(std::ceil(std::abs(opacity - before) / m_tip.fadeStep)), 1, most);
    for (int i = 1; i < steps; ++i) {
        const double t = static_cast<double>(i) / steps;
        Stamp between = stampAt(placeAlong(from, here, t, from.pos + (here.pos - from.pos) * t)).stamp;
        between.weight = 1.0 / steps;
        between.step = gap / steps;
        m_placed.push_back(between);
    }
    return steps;
}

StampLayer::StampLayer(const StampTip& tip, bool onWorkers) : m_tip(tip), m_onWorkers(onWorkers), m_walk(tip) {
    const double diameter = 2.0 * tip.radius;
    if (tip.onGrid && !tip.form && !tip.paperPerStamp && diameter >= kStampGridTwoFrom)  // a form's picture isn't smooth between points
        m_gridCell = diameter >= kStampGridEightFrom ? kStampGridEight : diameter >= kStampGridFourFrom ? kStampGridFour : kStampGridTwo;
    // With a skirt, the 5th Shape's whole stamps are laid faster pixel by pixel, as spans, than on the 2 px grid, which takes them a
    // cell and a decision at a time: the grid starts there at the 4 px grid's size.
    if (m_gridCell == kStampGridTwo && tip.skirtNeighbours > 0)
        m_gridCell = 0;
    if (tip.layDirect)
        m_gridCell = 0;
}

void StampLayer::begin(const InputSample& first, PressureShares shares) {
    m_walk.begin(first, shares);
    m_stamps.clear();
    m_stampSample.clear();
    m_sample = 0;
    m_nextToLay = 0;
}

void StampLayer::add(const InputSample& sample, PressureShares shares) {
    for (const Stamp& stamp : m_walk.add(sample, shares)) {
        m_stamps.push_back(stamp);
        m_stampSample.push_back(m_sample);
    }
    ++m_sample;
}

QRect StampLayer::takeDirty() {
    return std::exchange(m_dirty, QRect());
}

std::vector<TileCoord> StampLayer::takeChangedTiles() {
    return std::exchange(m_changed, {});
}

std::vector<std::size_t> StampLayer::readyStamps(bool ending) const {
    std::vector<std::size_t> ready;
    // With a skirt, a stamp waits until skirtNeighbours more are placed, as the Brush's dabs do.
    const std::size_t held = ending ? 0 : static_cast<std::size_t>(std::max(kStampNeighbours, m_tip.skirtNeighbours));
    for (std::size_t i = m_nextToLay; i + held < m_stamps.size(); ++i)
        ready.push_back(i);
    return ready;
}

void StampLayer::lay(StrokeBuffer& buffer, bool ending) {
    if (ending) {
        for (const Stamp& stamp : m_walk.end()) {
            m_stamps.push_back(stamp);
            m_stampSample.push_back(m_sample);
        }
        ++m_sample;
    }
    std::vector<std::size_t> ready = readyStamps(ending);
    if (m_direct) {
        // A pen sample's stamps go on together, once all of them are ready: what an update holds doesn't change the pixels.
        if (!ending)
            while (!ready.empty() &&
                   (ready.back() + 1 >= m_stamps.size() || m_stampSample[ready.back() + 1] == m_stampSample[ready.back()])) {
                const std::uint32_t sample = m_stampSample[ready.back()];
                while (!ready.empty() && m_stampSample[ready.back()] == sample)
                    ready.pop_back();
            }
        if (ready.empty())
            return;
        for (std::size_t first = 0; first < ready.size();) {
            std::size_t end = first + 1;
            while (end < ready.size() && m_stampSample[ready[end]] == m_stampSample[ready[first]])
                ++end;
            layDirectBatch(ready[first], ready[end - 1] + 1);
            first = end;
        }
        m_nextToLay = ready.back() + 1;
        forgetOldStamps();
        return;
    }
    if (ready.empty())
        return;
    layStamps(ready, buffer);
    m_nextToLay = ready.back() + 1;
    forgetOldStamps();
}

void StampLayer::forgetOldStamps() {
    while (m_nextToLay > static_cast<std::size_t>(std::max(kStampNeighbours, m_tip.skirtNeighbours))) {
        m_stamps.pop_front();
        if (!m_stampSample.empty())
            m_stampSample.pop_front();
        --m_nextToLay;
    }
}

void StampLayer::layStamps(const std::vector<std::size_t>& ready, StrokeBuffer& buffer) {
    QRect area;
    std::int64_t pixels = 0;
    for (const std::size_t i : ready) {
        const PixelBounds bounds = boundsOf(m_stamps[i]);
        area |= rectOf(bounds);
        pixels += static_cast<std::int64_t>(bounds.right - bounds.left) * (bounds.bottom - bounds.top);
    }
    m_dirty |= area;
    m_deep.resize(m_stamps.size());
    if (m_tip.skirtNeighbours > 0) {  // every stamp's frame, as a skirt's cut reads the outlines of those either side of it
        m_frames.clear();
        for (const Stamp& stamp : m_stamps)
            m_frames.emplace_back(stamp.centre);
    }
    for (const std::size_t i : ready) {
        StampMark mark;
        // A formed stamp's inside isn't even, so it has no deep reach, and its picture can reach past its round outline.
        const bool formed = m_stamps[i].formReach > 0.0f || m_tip.paperPerStamp != nullptr;  // each stamp cut by the paper
        const float reach = formed ? -1.0f : deepReach(m_stamps, i, mark) - kDeepMargin;
        const float outside = std::max(outerOf(m_stamps[i]), m_stamps[i].formReach) + kDeepMargin;
        m_deep[i] = {PixelFrame(m_stamps[i].centre), reach > 0.0f ? reach * reach : -1.0f, outside * outside,
                     densityOf(m_stamps[i].opacity, mark.paint), mark.shape};
        m_deep[i].skirtSquared = m_stamps[i].skirt > 0.0f ? reachOf(m_stamps[i]) * reachOf(m_stamps[i]) : 0.0f;
        const PixelBounds b = boundsOf(m_stamps[i]);
        m_deep[i].left = b.left, m_deep[i].top = b.top, m_deep[i].right = b.right, m_deep[i].bottom = b.bottom;
    }
    std::vector<Job> jobs;
    for (int ty = TiledImage::tileOf(area.top()); ty <= TiledImage::tileOf(area.bottom()); ++ty) {
        for (int tx = TiledImage::tileOf(area.left()); tx <= TiledImage::tileOf(area.right()); ++tx) {
            const auto found = buffer.tiles().find(TileCoord{tx, ty});
            jobs.push_back({TileCoord{tx, ty}, found == buffer.tiles().end() ? nullptr : found->second.get(), nullptr, false});
        }
    }
    const auto layOne = [&](int i) {
        if (m_gridCell > 0)
            layGridInTile(jobs[static_cast<std::size_t>(i)], ready);
        else
            layInOneTile(jobs[static_cast<std::size_t>(i)], ready);
    };
    if (m_onWorkers && pixels >= kParallelPixels && jobs.size() > 1)
        WorkerPool::forStrokes().run(static_cast<int>(jobs.size()), layOne);
    else
        for (int i = 0; i < static_cast<int>(jobs.size()); ++i)
            layOne(i);
    for (Job& job : jobs) {
        if (job.changed)
            m_changed.push_back(job.coord);
        if (job.fresh)
            buffer.adopt(job.coord, std::move(job.fresh));
    }
}

namespace {

// Density is kept times shape, since the merge divides by the pixel's shape.
void addTo(DensityTile*& tile, std::unique_ptr<DensityTile>& fresh, int index, float density, float shape, float skirt) {
    if (!tile) {
        fresh = std::make_unique<DensityTile>();
        tile = fresh.get();
    }
    tile->value[index] += density * shape;
    if (skirt > 0.0f)
        tile->value[index] += skirt;
    tile->shape[index] = std::max(tile->shape[index], shape);
    if (isSolid(tile, index))
        ++tile->solidPixels;
}

// addTo for eight pixels in a row, with no skirt: each lane takes the same steps as addTo, the pixels already solid passed. Says whether
// any took paint.
bool addEight(DensityTile& tile, int index, float density, float shape) {
    float* value = tile.value.data() + index;
    float* shapes = tile.shape.data() + index;
    const __m256 v = _mm256_loadu_ps(value), s = _mm256_loadu_ps(shapes);
    const __m256 one = _mm256_set1_ps(1.0f), solidDensity = _mm256_set1_ps(kSolidDensity);
    const __m256 wasSolid = _mm256_and_ps(_mm256_cmp_ps(s, one, _CMP_GE_OQ), _mm256_cmp_ps(v, solidDensity, _CMP_GE_OQ));
    const __m256 takes = _mm256_andnot_ps(wasSolid, _mm256_castsi256_ps(_mm256_set1_epi32(-1)));
    const __m256 nv = _mm256_blendv_ps(v, _mm256_add_ps(v, _mm256_set1_ps(density * shape)), takes);
    const __m256 ns = _mm256_blendv_ps(s, _mm256_max_ps(s, _mm256_set1_ps(shape)), takes);
    _mm256_storeu_ps(value, nv);
    _mm256_storeu_ps(shapes, ns);
    const __m256 nowSolid =
        _mm256_and_ps(takes, _mm256_and_ps(_mm256_cmp_ps(ns, one, _CMP_GE_OQ), _mm256_cmp_ps(nv, solidDensity, _CMP_GE_OQ)));
    tile.solidPixels += std::popcount(static_cast<unsigned>(_mm256_movemask_ps(nowSolid)));
    return _mm256_movemask_ps(takes) != 0;
}

}

namespace {

// Laying over: each channel moves `mask` of its way to the paint's, rounded, (p + ((s - p) * mask + half) >> kMaskBits), the
// colour kept within the alpha. For a whole tile at one mask, four pixels (sixteen channels) at a time in 32-bit lanes.
void layOverTile(const Channel* from, Channel* px, const std::array<std::int32_t, 4>& source, std::int32_t mask) {
    const __m256i src = _mm256_setr_epi32(source[0], source[1], source[2], source[3], source[0], source[1], source[2], source[3]);
    const __m256i m = _mm256_set1_epi32(mask), half = _mm256_set1_epi32(kMaskOne >> 1), zero = _mm256_setzero_si256();
    for (int p = 0; p < Tile::kSize * Tile::kSize; p += 2) {  // two pixels: eight channels
        const __m256i below = _mm256_cvtepu16_epi32(_mm_loadu_si128(reinterpret_cast<const __m128i*>(from + p * 4)));
        __m256i v = _mm256_add_epi32(
            below, _mm256_srai_epi32(_mm256_add_epi32(_mm256_mullo_epi32(_mm256_sub_epi32(src, below), m), half), kMaskBits));
        const __m256i alpha = _mm256_shuffle_epi32(v, _MM_SHUFFLE(3, 3, 3, 3));
        v = _mm256_max_epi32(_mm256_min_epi32(v, alpha), zero);
        const __m256i packed = _mm256_packus_epi32(v, v);  // lanes 0-3 and 4-7 each packed in their own half
        _mm_storel_epi64(reinterpret_cast<__m128i*>(px + p * 4), _mm256_castsi256_si128(packed));
        _mm_storel_epi64(reinterpret_cast<__m128i*>(px + p * 4 + 4), _mm256_extracti128_si256(packed, 1));
    }
}

// Laying over along a row, each pixel at its own mask (0 leaves it as it is), two pixels at a time.
void layOverRow(const Channel* from, Channel* px, const std::array<std::int32_t, 4>& source, const std::int32_t* masks, int count) {
    const __m256i src = _mm256_setr_epi32(source[0], source[1], source[2], source[3], source[0], source[1], source[2], source[3]);
    const __m256i half = _mm256_set1_epi32(kMaskOne >> 1), zero = _mm256_setzero_si256();
    for (int p = 0; p < count; p += 2) {
        const __m256i m = _mm256_setr_epi32(masks[p], masks[p], masks[p], masks[p], masks[p + 1], masks[p + 1], masks[p + 1], masks[p + 1]);
        const __m256i below = _mm256_cvtepu16_epi32(_mm_loadu_si128(reinterpret_cast<const __m128i*>(from + p * 4)));
        __m256i v = _mm256_add_epi32(
            below, _mm256_srai_epi32(_mm256_add_epi32(_mm256_mullo_epi32(_mm256_sub_epi32(src, below), m), half), kMaskBits));
        const __m256i alpha = _mm256_shuffle_epi32(v, _MM_SHUFFLE(3, 3, 3, 3));
        v = _mm256_max_epi32(_mm256_min_epi32(v, alpha), zero);
        const __m256i packed = _mm256_packus_epi32(v, v);
        _mm_storel_epi64(reinterpret_cast<__m128i*>(px + p * 4), _mm256_castsi256_si128(packed));
        _mm_storel_epi64(reinterpret_cast<__m128i*>(px + p * 4 + 4), _mm256_extracti128_si256(packed, 1));
    }
}

// Erasing: each channel keeps `keep` of itself, rounded, (p * keep + half) >> kMaskBits; along a row, each pixel at its own keep.
void layEraseRow(const Channel* from, Channel* px, const std::int32_t* keeps, int count) {
    const __m256i half = _mm256_set1_epi32(kMaskOne >> 1);
    for (int p = 0; p < count; p += 2) {
        const __m256i k = _mm256_setr_epi32(keeps[p], keeps[p], keeps[p], keeps[p], keeps[p + 1], keeps[p + 1], keeps[p + 1], keeps[p + 1]);
        __m256i v = _mm256_srai_epi32(
            _mm256_add_epi32(_mm256_mullo_epi32(_mm256_cvtepu16_epi32(_mm_loadu_si128(reinterpret_cast<const __m128i*>(from + p * 4))), k),
                             half),
            kMaskBits);
        // Erased paint that fades below a 1024th is gone, or a faint trace would keep tiles alive (the merge's kNothingLeft).
        v = _mm256_andnot_si256(_mm256_cmpgt_epi32(_mm256_set1_epi32(kChannelOne / 1024), _mm256_shuffle_epi32(v, _MM_SHUFFLE(3, 3, 3, 3))),
                                v);
        const __m256i packed = _mm256_packus_epi32(v, v);
        _mm_storel_epi64(reinterpret_cast<__m128i*>(px + p * 4), _mm256_castsi256_si128(packed));
        _mm_storel_epi64(reinterpret_cast<__m128i*>(px + p * 4 + 4), _mm256_extracti128_si256(packed, 1));
    }
}

void layEraseTile(const Channel* from, Channel* px, std::int32_t keep) {
    const __m256i k = _mm256_set1_epi32(keep), half = _mm256_set1_epi32(kMaskOne >> 1);
    for (int p = 0; p < Tile::kSize * Tile::kSize; p += 2) {
        __m256i v = _mm256_srai_epi32(
            _mm256_add_epi32(_mm256_mullo_epi32(_mm256_cvtepu16_epi32(_mm_loadu_si128(reinterpret_cast<const __m128i*>(from + p * 4))), k),
                             half),
            kMaskBits);
        // Erased paint that fades below a 1024th is gone, or a faint trace would keep tiles alive (the merge's kNothingLeft).
        v = _mm256_andnot_si256(_mm256_cmpgt_epi32(_mm256_set1_epi32(kChannelOne / 1024), _mm256_shuffle_epi32(v, _MM_SHUFFLE(3, 3, 3, 3))),
                                v);
        const __m256i packed = _mm256_packus_epi32(v, v);
        _mm_storel_epi64(reinterpret_cast<__m128i*>(px + p * 4), _mm256_castsi256_si128(packed));
        _mm_storel_epi64(reinterpret_cast<__m128i*>(px + p * 4 + 4), _mm256_extracti128_si256(packed, 1));
    }
}

// A whole pixel's coverage from its density, as coverageOf with shape 1, from a table read between its steps.
struct CoverageTable {
    static constexpr float kStep = 1.0f / 256.0f;
    static constexpr int kSteps = 16 * 256;
    std::array<float, kSteps + 2> at{};
    float opacity = -1.0f, fullAt = -1.0f;
    void fill(float o, float f) {
        opacity = o, fullAt = f;
        for (int i = 0; i < kSteps + 2; ++i)
            at[static_cast<std::size_t>(i)] = coverageOf(static_cast<float>(i) * kStep, 1.0f, o, f);
    }
    float of(float density) const {
        const float t = density / kStep;
        if (!(t < static_cast<float>(kSteps)))
            return at[kSteps];
        const int i = static_cast<int>(t);
        return at[static_cast<std::size_t>(i)] +
               (at[static_cast<std::size_t>(i) + 1] - at[static_cast<std::size_t>(i)]) * (t - static_cast<float>(i));
    }
};

// A row's whole pixels' masks from their densities by the table, eight at a time (CoverageTable::of, then maskOf).
void tableMasks(const CoverageTable& table, const float* value, std::int32_t* masks, int count) {
    const __m256 perStep = _mm256_set1_ps(1.0f / CoverageTable::kStep), top = _mm256_set1_ps(static_cast<float>(CoverageTable::kSteps));
    const __m256 maskOne = _mm256_set1_ps(static_cast<float>(kMaskOne)), half = _mm256_set1_ps(0.5f);
    for (int x = 0; x < count; x += 8) {
        const __m256 t = _mm256_min_ps(_mm256_max_ps(_mm256_mul_ps(_mm256_loadu_ps(value + x), perStep), _mm256_setzero_ps()), top);
        const __m256i i = _mm256_cvttps_epi32(t);
        const __m256 a = _mm256_i32gather_ps(table.at.data(), i, 4), b = _mm256_i32gather_ps(table.at.data() + 1, i, 4);
        const __m256 coverage = _mm256_add_ps(a, _mm256_mul_ps(_mm256_sub_ps(b, a), _mm256_sub_ps(t, _mm256_cvtepi32_ps(i))));
        __m256i m = _mm256_cvttps_epi32(_mm256_add_ps(_mm256_mul_ps(coverage, maskOne), half));
        m = _mm256_min_epi32(_mm256_max_epi32(m, _mm256_setzero_si256()), _mm256_set1_epi32(kMaskOne));
        _mm256_storeu_si256(reinterpret_cast<__m256i*>(masks + x), m);
    }
}

// A skirt's share at each flagged pixel of a row, the pixel `firstDx` + x across from the stamp's centre and `dy` down, times
// `density` and `most`, added to the row's value; its shape raised to `most` where it lays (skirtOf's steps inside a whole neighbour).
void skirtRow(std::uint64_t bits, float firstDx, float dy, float radius, float perStep, float density, float most, float* value,
              float* shape, int count) {
    const __m256 steps = _mm256_set1_ps(static_cast<float>(kSkirtSteps)), dy2 = _mm256_set1_ps(dy * dy);
    for (int x = 0; x < count; x += 8) {
        const int byte = static_cast<int>((bits >> x) & 0xFF);
        if (byte == 0)
            continue;
        const __m256i lane = _mm256_setr_epi32(1, 2, 4, 8, 16, 32, 64, 128);
        const __m256 on = _mm256_castsi256_ps(_mm256_cmpeq_epi32(_mm256_and_si256(_mm256_set1_epi32(byte), lane), lane));
        const __m256 dx = _mm256_add_ps(_mm256_set1_ps(firstDx), _mm256_setr_ps(float(x), float(x + 1), float(x + 2), float(x + 3),
                                                                                float(x + 4), float(x + 5), float(x + 6), float(x + 7)));
        const __m256 d = _mm256_sqrt_ps(_mm256_add_ps(_mm256_mul_ps(dx, dx), dy2));
        const __m256 at =
            _mm256_mul_ps(_mm256_max_ps(_mm256_sub_ps(d, _mm256_set1_ps(radius)), _mm256_setzero_ps()), _mm256_set1_ps(perStep));
        const __m256 inside = _mm256_and_ps(on, _mm256_cmp_ps(at, steps, _CMP_LT_OQ));
        const __m256i i = _mm256_cvttps_epi32(_mm256_min_ps(at, _mm256_set1_ps(static_cast<float>(kSkirtSteps - 1))));
        const __m256 a = _mm256_mask_i32gather_ps(_mm256_setzero_ps(), kSkirtTable.data(), i, inside, 4);
        const __m256 b = _mm256_mask_i32gather_ps(_mm256_setzero_ps(), kSkirtTable.data() + 1, i, inside, 4);
        const __m256 share =
            _mm256_and_ps(inside, _mm256_add_ps(a, _mm256_mul_ps(_mm256_sub_ps(b, a), _mm256_sub_ps(at, _mm256_cvtepi32_ps(i)))));
        const __m256 lays = _mm256_and_ps(inside, _mm256_cmp_ps(share, _mm256_setzero_ps(), _CMP_GT_OQ));
        _mm256_storeu_ps(value + x, _mm256_add_ps(_mm256_loadu_ps(value + x),
                                                  _mm256_mul_ps(_mm256_set1_ps(density), _mm256_mul_ps(share, _mm256_set1_ps(most)))));
        _mm256_storeu_ps(
            shape + x, _mm256_blendv_ps(_mm256_loadu_ps(shape + x), _mm256_max_ps(_mm256_loadu_ps(shape + x), _mm256_set1_ps(most)), lays));
    }
}

// A soft Shape's stamp at each flagged pixel of a row: its density by distance from `table` (steps `perStep` a pixel), read between its
// steps, added to the row's value, its shape whole there.
void profileRow(std::uint64_t bits, float firstDx, float dy, const float* table, float perStep, float* value, float* shape, int count) {
    const __m256 dy2 = _mm256_set1_ps(dy * dy), one = _mm256_set1_ps(1.0f);
    for (int x = 0; x < count; x += 8) {
        const int byte = static_cast<int>((bits >> x) & 0xFF);
        if (byte == 0)
            continue;
        const __m256i lane = _mm256_setr_epi32(1, 2, 4, 8, 16, 32, 64, 128);
        const __m256 on = _mm256_castsi256_ps(_mm256_cmpeq_epi32(_mm256_and_si256(_mm256_set1_epi32(byte), lane), lane));
        const __m256 dx = _mm256_add_ps(_mm256_set1_ps(firstDx), _mm256_setr_ps(float(x), float(x + 1), float(x + 2), float(x + 3),
                                                                                float(x + 4), float(x + 5), float(x + 6), float(x + 7)));
        const __m256 t = _mm256_mul_ps(_mm256_sqrt_ps(_mm256_add_ps(_mm256_mul_ps(dx, dx), dy2)), _mm256_set1_ps(perStep));
        const __m256i i = _mm256_cvttps_epi32(t);
        const __m256 a = _mm256_mask_i32gather_ps(_mm256_setzero_ps(), table, i, on, 4);
        const __m256 b = _mm256_mask_i32gather_ps(_mm256_setzero_ps(), table + 1, i, on, 4);
        const __m256 density =
            _mm256_and_ps(on, _mm256_add_ps(a, _mm256_mul_ps(_mm256_sub_ps(b, a), _mm256_sub_ps(t, _mm256_cvtepi32_ps(i)))));
        _mm256_storeu_ps(value + x, _mm256_add_ps(_mm256_loadu_ps(value + x), density));
        _mm256_storeu_ps(shape + x, _mm256_blendv_ps(_mm256_loadu_ps(shape + x), one, on));
    }
}

// The step from the stroke's coverage so far to the new at whole pixels, eight at a time, each by the table: (new - so far) /
// (1 - so far), the memory moved on. Lanes not `on` are left as they are.
inline __m256 tableCover(const CoverageTable& table, __m256 v) {
    const __m256 t = _mm256_min_ps(_mm256_max_ps(_mm256_mul_ps(v, _mm256_set1_ps(1.0f / CoverageTable::kStep)), _mm256_setzero_ps()),
                                   _mm256_set1_ps(static_cast<float>(CoverageTable::kSteps)));
    const __m256i i = _mm256_cvttps_epi32(t);
    const __m256 a = _mm256_i32gather_ps(table.at.data(), i, 4), b = _mm256_i32gather_ps(table.at.data() + 1, i, 4);
    return _mm256_add_ps(a, _mm256_mul_ps(_mm256_sub_ps(b, a), _mm256_sub_ps(t, _mm256_cvtepi32_ps(i))));
}

int wholeSteps(const CoverageTable& table, float* value, float* shape, const float* density, const char* on, std::int32_t* masks,
               int count) {
    int nowFull = 0;
    const __m256 one = _mm256_set1_ps(1.0f), maskOne = _mm256_set1_ps(static_cast<float>(kMaskOne)), half = _mm256_set1_ps(0.5f),
                 zero = _mm256_setzero_ps();
    const auto cover = [&](__m256 v) { return tableCover(table, v); };
    for (int x = 0; x < count; x += 8) {
        const __m256 lanes = _mm256_castsi256_ps(
            _mm256_cmpgt_epi32(_mm256_cvtepi8_epi32(_mm_loadl_epi64(reinterpret_cast<const __m128i*>(on + x))), _mm256_setzero_si256()));
        if (_mm256_movemask_ps(lanes) == 0)
            continue;
        const __m256 v0 = _mm256_loadu_ps(value + x), s0 = _mm256_loadu_ps(shape + x), d = _mm256_loadu_ps(density + x);
        const __m256 c0 = _mm256_and_ps(_mm256_cmp_ps(s0, zero, _CMP_GT_OQ), cover(v0));
        const __m256 v1 = _mm256_add_ps(v0, d);
        const __m256 c1 = cover(v1);
        const __m256 left = _mm256_sub_ps(one, c0);
        __m256 m = _mm256_div_ps(_mm256_max_ps(_mm256_sub_ps(c1, c0), zero), _mm256_max_ps(left, _mm256_set1_ps(1e-30f)));
        m = _mm256_and_ps(_mm256_cmp_ps(left, zero, _CMP_GT_OQ), _mm256_min_ps(m, one));
        const __m256i mask = _mm256_cvttps_epi32(_mm256_add_ps(_mm256_mul_ps(m, maskOne), half));
        _mm256_maskstore_epi32(masks + x, _mm256_castps_si256(lanes), mask);
        _mm256_maskstore_ps(value + x, _mm256_castps_si256(lanes), v1);
        _mm256_maskstore_ps(shape + x, _mm256_castps_si256(lanes), _mm256_max_ps(s0, one));
        nowFull += std::popcount(static_cast<unsigned>(_mm256_movemask_ps(
            _mm256_and_ps(lanes, _mm256_and_ps(_mm256_cmp_ps(c1, one, _CMP_GE_OQ), _mm256_cmp_ps(c0, one, _CMP_LT_OQ))))));
    }
    return nowFull;
}

constexpr int kProfileTableSteps = 256;

// Erased paint that fades below this is gone, as the merge's eraseKeep.
std::int32_t eraseKeepOf(float coverage) {
    return coverage > 1.0f - 1.0f / 1024.0f ? 0 : kMaskOne - maskOf(coverage);
}

}

// The direct lay: the layer as the stroke began, copied, and the paint in whole numbers.
struct StampLayer::Direct {
    TiledImage layer;
    StrokePaint paint;
    std::array<std::int32_t, 4> source{};  // the paint's premultiplied channels, 0 to 65535
    std::unordered_map<TileCoord, bool, TileCoordHash> painted;
    CoverageTable table;
    // A tile where any pixel was covered in part keeps the stroke's density and outline so far at each pixel, as the stroke-wide merge
    // does, so each sample lays the step from the coverage so far to the new: an edge pixel stays capped by its outline however many
    // samples overlap there. Whole pixels need none: there each step's coverage multiplies out the same.
    struct EdgeState {
        std::array<float, Tile::kSize * Tile::kSize> value{}, shape{};
        int partial = 0;  // pixels covered in part, 0 < shape < 1
        int full = 0;  // pixels at full coverage, which no more paint changes
        // While only even steps have reached the tile, one value for all its pixels; spread to the arrays when anything else does.
        bool uniform = true;
        float uniformValue = 0.0f, uniformShape = 0.0f;
        void spread() {
            if (!uniform)
                return;
            value.fill(uniformValue);
            shape.fill(uniformShape);
            uniform = false;
        }
    };
    std::unordered_map<TileCoord, std::unique_ptr<EdgeState>, TileCoordHash> edgeStates;
    std::mutex edgeStatesMutex;
    EdgeState* edgeStateOf(TileCoord coord, bool make) {
        std::scoped_lock lock(edgeStatesMutex);
        auto found = edgeStates.find(coord);
        if (found != edgeStates.end())
            return found->second.get();
        if (!make)
            return nullptr;
        return edgeStates.emplace(coord, std::make_unique<EdgeState>()).first->second.get();
    }
};

void StampLayer::layDirectOnto(const TiledImage& before, const StrokePaint& paint) {
    m_direct = std::make_shared<Direct>();
    m_direct->layer = before;
    m_direct->paint = paint;
    for (int c = 0; c < 3; ++c)
        m_direct->source[static_cast<std::size_t>(c)] =
            static_cast<std::int32_t>(roundToNearest(paint.color[static_cast<std::size_t>(c)] * kChannelOne));
    m_direct->source[3] = kChannelOne;
    m_direct->table.fill(paint.opacity, paint.fullAt);
}

TilePtr StampLayer::paintedTile(TileCoord coord) const {
    if (!m_direct || !m_direct->painted.contains(coord))
        return nullptr;
    return m_direct->layer.tileShared(coord.x, coord.y);
}

// One pen sample's stamps [first, end) onto the layer. A tile every stamp that reaches it covers with its even middle takes one
// mask; elsewhere each pixel's density is added up as the per-pixel model's, and its coverage made one mask there.
void StampLayer::layDirectBatch(std::size_t first, std::size_t end) {
    Direct& direct = *m_direct;
    QRect area;
    m_deep.resize(m_stamps.size());
    for (std::size_t i = first; i < end; ++i) {
        StampMark mark;
        const float reach = deepReach(m_stamps, i, mark) - kDeepMargin;
        const float outside = outerOf(m_stamps[i]) + kDeepMargin;
        m_deep[i] = {PixelFrame(m_stamps[i].centre), reach > 0.0f ? reach * reach : -1.0f, outside * outside,
                     densityOf(m_stamps[i].opacity, mark.paint), mark.shape};
        m_deep[i].skirtSquared = m_stamps[i].skirt > 0.0f ? reachOf(m_stamps[i]) * reachOf(m_stamps[i]) : 0.0f;
        const PixelBounds b = boundsOf(m_stamps[i]);
        m_deep[i].left = b.left, m_deep[i].top = b.top, m_deep[i].right = b.right, m_deep[i].bottom = b.bottom;
        area |= rectOf(b);
    }
    if (m_tip.skirtNeighbours > 0) {  // every stamp's frame, as a skirt's cut reads the outlines of those either side of it
        m_frames.clear();
        for (const Stamp& stamp : m_stamps)
            m_frames.emplace_back(stamp.centre);
    }
    // A soft Shape's stamp, where its outline is whole: its density by distance, a table read between its steps (plainMarkOf's steps there).
    std::vector<std::vector<float>> tables(end - first);
    std::vector<float> tableReach(end - first, -1.0f);
    for (std::size_t i = first; i < end; ++i) {
        const Stamp& stamp = m_stamps[i];
        if (stamp.profile < 0 || stamp.chordRadius > 0.0f || rimCapped(stamp) || stamp.radius * 2.0f < 1.0f)
            continue;
        const float reach = outerOf(stamp) - std::max(stamp.rampWidth, 1.0f);  // the outline's ramp is whole a ramp's width in
        if (reach <= 1.0f)
            continue;
        std::vector<float>& t = tables[i - first];
        t.resize(kProfileTableSteps + 2);
        for (int k = 0; k <= kProfileTableSteps + 1; ++k) {
            const float distance = std::min(reach, reach * static_cast<float>(k) / kProfileTableSteps);
            t[static_cast<std::size_t>(k)] =
                densityOf(stamp.opacity, profilePaint(stamp.profile, distance * profileReach(stamp.profile) / stamp.radius));
        }
        tableReach[i - first] = reach;
    }
    area &= direct.layer.paintArea();
    if (area.isEmpty())
        return;
    m_dirty |= area;
    // Each tile: the even density of the stamps covering it whole, and those whose edge crosses it.
    struct TileWork {
        TileCoord coord;
        float even = 0.0f;
        bool covered = false;
        std::vector<std::size_t> edges{};
        Tile* tile = nullptr;
    };
    std::vector<TileWork> work;
    for (int ty = TiledImage::tileOf(area.top()); ty <= TiledImage::tileOf(area.bottom()); ++ty) {
        for (int tx = TiledImage::tileOf(area.left()); tx <= TiledImage::tileOf(area.right()); ++tx) {
            TileWork w{TileCoord{tx, ty}};
            const QRect r = w.coord.pixelRect();
            for (std::size_t i = first; i < end; ++i) {
                const Deep& d = m_deep[i];
                if (m_stamps[i].opacity <= 0.0f || d.right <= r.left() || d.left > r.right() || d.bottom <= r.top() || d.top > r.bottom())
                    continue;
                const float x0 = d.frame.dx(r.left()), x1 = d.frame.dx(r.right()), y0 = d.frame.dy(r.top()), y1 = d.frame.dy(r.bottom());
                const float nx = x0 > 0.0f ? x0 : x1 < 0.0f ? -x1 : 0.0f, ny = y0 > 0.0f ? y0 : y1 < 0.0f ? -y1 : 0.0f;
                const float fx = std::max(std::abs(x0), std::abs(x1)), fy = std::max(std::abs(y0), std::abs(y1));
                if (nx * nx + ny * ny >= std::max(d.outsideSquared, d.skirtSquared))
                    continue;
                if (fx * fx + fy * fy < d.reachSquared && m_stamps[i].chordRadius <= 0.0f && d.shape >= 1.0f) {
                    w.even += d.density * d.shape;
                    w.covered = true;
                } else {
                    w.edges.push_back(i);
                }
            }
            if (w.covered || !w.edges.empty())
                work.push_back(std::move(w));
        }
    }
    for (TileWork& w : work) {  // the layer's map gains tiles only here, on this thread; each is made writable on the workers
        if (!direct.layer.tile(w.coord.x, w.coord.y))
            w.tile = direct.layer.writableTile(w.coord.x, w.coord.y);
        direct.painted[w.coord] = true;
        m_changed.push_back(w.coord);
    }
    const bool erase = direct.paint.mode == LayMode::Erase;
    const float opacity = direct.paint.opacity, fullAt = direct.paint.fullAt;
    // With a tile's memory, the mask that takes a pixel from the stroke's coverage so far to the new, as the stroke-wide merge would
    // have it after this sample: (new - so far) / (1 - so far).
    // Where coverage is full only at 1 and the pixel is whole, before and after this sample, the step's mask is the sample's own:
    // the memory isn't needed there, and the pixel keeps the mask worked out without it (`own`).
    const bool wholeMultiplies = fullAt >= 0.999f && opacity >= 1.0f;
    const auto coverAt = [&](float density, float shape) {
        return shape >= 1.0f && opacity >= 1.0f ? direct.table.of(density) : coverageOf(density, shape, opacity, fullAt);
    };
    const auto stepMask = [&](Direct::EdgeState& state, int p, float density, float shape, std::int32_t own) {
        const std::size_t i = static_cast<std::size_t>(p);
        const float was = state.shape[i];
        if (wholeMultiplies && !(was > 0.0f && was < 1.0f) && !(shape > 0.0f && shape < 1.0f)) {
            if (shape > was)
                state.shape[i] = shape;
            return own;
        }
        if (shape <= 0.0f && density <= 0.0f)
            return 0;
        const float before = was > 0.0f ? coverAt(state.value[i], was) : 0.0f;
        state.value[i] += density;
        state.shape[i] = std::max(was, shape);
        state.partial += (state.shape[i] > 0.0f && state.shape[i] < 1.0f) - (was > 0.0f && was < 1.0f);
        if (state.shape[i] <= 0.0f || before >= 1.0f)
            return 0;
        const float after = coverAt(state.value[i], state.shape[i]);
        state.full += after >= 1.0f;
        return maskOf(std::clamp((after - before) / (1.0f - before), 0.0f, 1.0f));
    };
    // A row through the memory: where coverage is full short of 1 its whole pixels go eight at a time (wholeSteps), those covered in
    // part one by one; otherwise every pixel by stepMask, which leaves the whole ones their own mask.
    const auto stepRow = [&](Direct::EdgeState& state, int row, const float* density, const float* shape, std::int32_t* masks) {
        const int at = row * Tile::kSize;
        if (wholeMultiplies) {
            for (int x = 0; x < Tile::kSize; ++x)
                masks[x] = stepMask(state, at + x, density[x], shape[x], masks[x]);
            return;
        }
        std::array<char, Tile::kSize> whole{};
        if (state.partial == 0 && std::all_of(shape, shape + Tile::kSize, [&](float sh) { return sh >= 1.0f; }) && opacity >= 1.0f) {
            whole.fill(1);
            state.full +=
                wholeSteps(direct.table, state.value.data() + at, state.shape.data() + at, density, whole.data(), masks, Tile::kSize);
            return;
        }
        for (int x = 0; x < Tile::kSize; ++x) {
            const float was = state.shape[static_cast<std::size_t>(at + x)];
            if (opacity >= 1.0f && shape[x] >= 1.0f && !(was > 0.0f && was < 1.0f))
                whole[static_cast<std::size_t>(x)] = 1;
            else
                masks[x] = stepMask(state, at + x, density[x], shape[x], masks[x]);
        }
        state.full += wholeSteps(direct.table, state.value.data() + at, state.shape.data() + at, density, whole.data(), masks, Tile::kSize);
    };
    const auto layOne = [&](int k) {
        TileWork& w = work[static_cast<std::size_t>(k)];
        // Out of place: read the tile as it was, write the result into a fresh one where the view may hold it, so it never sees a
        // sample half laid and nothing is copied first.
        if (!wholeMultiplies)
            if (Direct::EdgeState* known = direct.edgeStateOf(w.coord, false); known && known->full >= Tile::kSize * Tile::kSize)
                return;  // every pixel at full coverage: more paint changes nothing
        TilePtr was;
        if (!w.tile)
            w.tile = direct.layer.freshExistingTile(w.coord.x, w.coord.y, was);
        Channel* px = w.tile->px.data();
        const Channel* from = was ? was->px.data() : px;
        if (w.edges.empty()) {
            Direct::EdgeState* state = direct.edgeStateOf(w.coord, !wholeMultiplies);
            if (state && state->uniform) {  // one value: one step across the tile
                const float before = state->uniformShape > 0.0f ? coverAt(state->uniformValue, state->uniformShape) : 0.0f;
                state->uniformValue += w.even;
                state->uniformShape = std::max(state->uniformShape, 1.0f);
                const float after = coverAt(state->uniformValue, state->uniformShape);
                if (after >= 1.0f && before < 1.0f)
                    state->full = Tile::kSize * Tile::kSize;
                const float coverage = before >= 1.0f ? 0.0f : std::clamp((after - before) / (1.0f - before), 0.0f, 1.0f);
                if (erase)
                    layEraseTile(from, px, eraseKeepOf(coverage));
                else
                    layOverTile(from, px, direct.source, maskOf(coverage));
                return;
            }
            if (state) {
                std::array<std::int32_t, Tile::kSize> masks;
                const std::int32_t own = maskOf(coverAt(w.even, 1.0f));
                std::array<float, Tile::kSize> evenRow, wholeRow;
                evenRow.fill(w.even);
                wholeRow.fill(1.0f);
                for (int row = 0; row < Tile::kSize; ++row) {
                    masks.fill(own);
                    stepRow(*state, row, evenRow.data(), wholeRow.data(), masks.data());
                    if (erase)
                        for (int x = 0; x < Tile::kSize; ++x)
                            masks[static_cast<std::size_t>(x)] = masks[static_cast<std::size_t>(x)] > kMaskOne - kMaskOne / 1024
                                                                     ? 0
                                                                     : kMaskOne - masks[static_cast<std::size_t>(x)];
                    if (erase)
                        layEraseRow(from + row * Tile::kSize * 4, px + row * Tile::kSize * 4, masks.data(), Tile::kSize);
                    else
                        layOverRow(from + row * Tile::kSize * 4, px + row * Tile::kSize * 4, direct.source, masks.data(), Tile::kSize);
                }
                return;
            }
            const float coverage = coverageOf(w.even, 1.0f, opacity, fullAt);
            if (erase)
                layEraseTile(from, px, eraseKeepOf(coverage));
            else if (from != px || maskOf(coverage) > 0)
                layOverTile(from, px, direct.source, maskOf(coverage));
            return;
        }
        // Row by row: each stamp adds its even density over the span of its middle, and works out only the pixels between that and
        // its outline; the rest of the row takes what the stamps covering the tile whole lay.
        const QRect r = w.coord.pixelRect();
        std::array<float, Tile::kSize> value, shape;
        thread_local std::array<float, Tile::kSize * Tile::kSize> tileValue, tileShape;
        // Each edge stamp's neighbours, once for the tile: one whose whole middle covers it makes the skirt's cut whole across the tile;
        // those that can't reach it are left out; only those crossing it are looked at row by row (the reference's short list).
        struct NeighbourPlan {
            bool wholeTile = false;
            float wholeShare = 0.0f;
            std::array<std::size_t, 2 * kMostSkirtNeighbours> crossing{};
            int count = 0;
        };
        thread_local std::vector<NeighbourPlan> plans;
        plans.assign(w.edges.size(), NeighbourPlan{});
        {
            const QRect tileRect = w.coord.pixelRect();
            const auto around = static_cast<std::size_t>(m_tip.skirtNeighbours);
            for (std::size_t e = 0; e < w.edges.size(); ++e) {
                const std::size_t i = w.edges[e];
                if (m_stamps[i].skirt <= 0.0f)
                    continue;
                NeighbourPlan& plan = plans[e];
                for (std::size_t step = 1; step <= around && !plan.wholeTile; ++step)
                    for (const std::size_t j : {i - step, i + step}) {
                        if (j >= m_stamps.size())
                            continue;
                        const Stamp& n = m_stamps[j];
                        const float x0 = m_frames[j].dx(tileRect.left()), x1 = m_frames[j].dx(tileRect.right()),
                                    y0 = m_frames[j].dy(tileRect.top()), y1 = m_frames[j].dy(tileRect.bottom());
                        const float nx = x0 > 0.0f ? x0 : x1 < 0.0f ? -x1 : 0.0f, ny = y0 > 0.0f ? y0 : y1 < 0.0f ? -y1 : 0.0f;
                        const float fx = std::max(std::abs(x0), std::abs(x1)), fy = std::max(std::abs(y0), std::abs(y1));
                        const float outerN = outerOf(n) + 1.0f;
                        if (nx * nx + ny * ny >= outerN * outerN)
                            continue;  // can't reach the tile
                        const float whole = n.chordRadius > 0.0f ? -1.0f
                                                                 : (n.edgeBlur > 0.0f ? n.radius - kNormalReach * n.edgeBlur
                                                                                      : outerOf(n) - std::max(n.rampWidth, 1.0f)) -
                                                                       1.0f;
                        if (whole > 0.0f && fx * fx + fy * fy < whole * whole) {
                            plan.wholeTile = true;
                            plan.wholeShare = std::max(plan.wholeShare, outlineShareOf(n, 0.0f));
                            break;
                        }
                        if (plan.count < static_cast<int>(plan.crossing.size()))
                            plan.crossing[static_cast<std::size_t>(plan.count++)] = j;
                    }
            }
        }
        for (int row = 0; row < Tile::kSize; ++row) {
            const int y = r.top() + row;
            value.fill(w.even);
            shape.fill(w.covered ? 1.0f : 0.0f);
            for (std::size_t e = 0; e < w.edges.size(); ++e) {
                const std::size_t i = w.edges[e];
                const NeighbourPlan& plan = plans[e];
                const Deep& d = m_deep[i];
                const float dy = d.frame.dy(y);
                const float laysTo = std::max(d.outsideSquared, d.skirtSquared);
                if (dy * dy >= laysTo || y < d.top || y >= d.bottom)
                    continue;
                // Pixel x's offset is x + (dx(0)): its middle and outline chords as whole pixels, those with |dx| under each.
                const float x0 = d.frame.dx(0);
                const float outer = std::sqrt(laysTo - dy * dy);
                const int oLeft = std::max(r.left(), static_cast<int>(std::floor(-outer - x0))),
                          oRight = std::min(r.right(), static_cast<int>(std::ceil(outer - x0)));
                int cLeft = oRight + 1, cRight = oRight;  // empty unless it has an even middle on this row
                if (m_stamps[i].chordRadius <= 0.0f && d.reachSquared > dy * dy) {
                    const float inner = std::sqrt(d.reachSquared - dy * dy);
                    cLeft = std::max(oLeft, static_cast<int>(std::ceil(-inner - x0)));
                    cRight = std::min(oRight, static_cast<int>(std::floor(inner - x0)));
                    while (cLeft <= cRight) {  // the strict test the per-pixel model makes at its edges
                        const float dx = d.frame.dx(cLeft);
                        if (dx * dx + dy * dy < d.reachSquared)
                            break;
                        ++cLeft;
                    }
                    while (cRight >= cLeft) {
                        const float dx = d.frame.dx(cRight);
                        if (dx * dx + dy * dy < d.reachSquared)
                            break;
                        --cRight;
                    }
                }
                const float even = d.density * d.shape;
                for (int x = cLeft; x <= cRight; ++x) {
                    value[static_cast<std::size_t>(x - r.left())] += even;
                    shape[static_cast<std::size_t>(x - r.left())] = std::max(shape[static_cast<std::size_t>(x - r.left())], d.shape);
                }
                // The row's pixels as bits: its even middle (added above), its own outline, and where its neighbours' outlines are,
                // and whole. Past its own outline only its skirt lays, which is nothing where no neighbour's outline is, and inside a
                // neighbour's whole outline the stamp's density times its share at that distance (skirtOf's steps): only the pixels
                // left are worked out one by one.
                const auto bitsOf = [&](int a, int b) -> std::uint64_t {
                    a = std::max(a, r.left()), b = std::min(b, r.right());
                    if (a > b)
                        return 0;
                    const int start = a - r.left(), count = b - a + 1;
                    return (count >= 64 ? ~0ULL : ((1ULL << count) - 1)) << start;
                };
                const std::uint64_t range = bitsOf(oLeft, oRight), core = cLeft <= cRight ? bitsOf(cLeft, cRight) : 0;
                std::uint64_t own = range, inOuter = 0, inWhole = 0;
                float wholeShare = 0.0f;
                const Stamp& stamp = m_stamps[i];
                if (stamp.skirt > 0.0f) {
                    const float ownReach = outerOf(stamp);
                    own = 0;
                    if (dy * dy < ownReach * ownReach) {
                        const float ownChord = std::sqrt(ownReach * ownReach - dy * dy);
                        own = bitsOf(static_cast<int>(std::floor(-ownChord - x0)), static_cast<int>(std::ceil(ownChord - x0))) & range;
                    }
                    if (plan.wholeTile) {
                        inOuter = inWhole = ~0ULL;
                        wholeShare = plan.wholeShare;
                    }
                    for (int c = 0; c < (plan.wholeTile ? 0 : plan.count); ++c) {
                        const std::size_t j = plan.crossing[static_cast<std::size_t>(c)];
                        const Stamp& n = m_stamps[j];
                        const float ndy = m_frames[j].dy(y), nx0 = m_frames[j].dx(0);
                        const float outerN = outerOf(n) + 1.0f;  // a pixel to spare
                        if (ndy * ndy >= outerN * outerN)
                            continue;
                        const float oc = std::sqrt(outerN * outerN - ndy * ndy);
                        inOuter |= bitsOf(static_cast<int>(std::floor(-oc - nx0)), static_cast<int>(std::ceil(oc - nx0)));
                        if (n.chordRadius > 0.0f)
                            continue;
                        // A blur is at its most past kNormalReach deviations in; a ramp is whole a ramp's width in.
                        const float whole =
                            n.edgeBlur > 0.0f ? n.radius - kNormalReach * n.edgeBlur : outerOf(n) - std::max(n.rampWidth, 1.0f);
                        if (whole <= 0.0f || ndy * ndy >= whole * whole)
                            continue;
                        const float chord = std::sqrt(whole * whole - ndy * ndy) - 1.0f;  // a pixel to spare
                        const std::uint64_t wholeBits =
                            bitsOf(static_cast<int>(std::ceil(-chord - nx0)), static_cast<int>(std::floor(chord - nx0)));
                        if (wholeBits) {
                            inWhole |= wholeBits;
                            wholeShare = std::max(wholeShare, outlineShareOf(n, 0.0f));
                        }
                    }
                }
                const std::uint64_t byTable = stamp.skirt > 0.0f ? inWhole & inOuter & range & ~own & ~core : 0;
                std::uint64_t byProfile = 0;
                if (const float tr = tableReach[i - first]; tr > 0.0f && dy * dy < tr * tr) {
                    const float chord = std::sqrt(tr * tr - dy * dy);
                    byProfile = bitsOf(static_cast<int>(std::ceil(-chord - x0)), static_cast<int>(std::floor(chord - x0))) & range & ~core;
                    while (byProfile) {  // the strict test at its ends, as the per-pixel model's
                        const int lo = std::countr_zero(byProfile);
                        const float ldx = d.frame.dx(r.left() + lo);
                        if (ldx * ldx + dy * dy < tr * tr)
                            break;
                        byProfile &= byProfile - 1;
                    }
                    while (byProfile) {
                        const int hi = 63 - std::countl_zero(byProfile);
                        const float hdx = d.frame.dx(r.left() + hi);
                        if (hdx * hdx + dy * dy < tr * tr)
                            break;
                        byProfile &= ~(1ULL << hi);
                    }
                }
                std::uint64_t oneByOne = range & ~core & ~byProfile & (own | (inOuter & ~inWhole));
                if (byProfile) {
                    profileRow(byProfile, d.frame.dx(r.left()), dy, tables[i - first].data(),
                               static_cast<float>(kProfileTableSteps) / tableReach[i - first], value.data(), shape.data(), Tile::kSize);
                }
                while (oneByOne) {
                    const int bit = std::countr_zero(oneByOne);
                    oneByOne &= oneByOne - 1;
                    const int x = r.left() + bit;
                    float density = 0.0f, sh = 0.0f, skirt = 0.0f;
                    if (markOf(i, x, y, density, sh, skirt)) {
                        value[static_cast<std::size_t>(bit)] += density * sh + skirt;
                        shape[static_cast<std::size_t>(bit)] = std::max(shape[static_cast<std::size_t>(bit)], sh);
                    }
                }
                if (byTable) {
                    skirtRow(byTable, d.frame.dx(r.left()), dy, stamp.radius,
                             static_cast<float>(kSkirtSteps) / (static_cast<float>(kSkirtReach) * stamp.skirt),
                             densityOf(stamp.opacity, 1.0f), wholeShare, value.data(), shape.data(), Tile::kSize);
                }
            }
            std::copy(value.begin(), value.end(), tileValue.begin() + row * Tile::kSize);
            std::copy(shape.begin(), shape.end(), tileShape.begin() + row * Tile::kSize);
        }
        Direct::EdgeState* state = direct.edgeStateOf(w.coord, !wholeMultiplies);
        if (!state && std::any_of(tileShape.begin(), tileShape.end(), [](float sh) { return sh > 0.0f && sh < 1.0f; }))
            state = direct.edgeStateOf(w.coord, true);
        if (state)
            state->spread();
        std::array<std::int32_t, Tile::kSize> masks;
        for (int row = 0; row < Tile::kSize; ++row) {
            const float* rowValue = tileValue.data() + row * Tile::kSize;
            const float* rowShape = tileShape.data() + row * Tile::kSize;
            if (opacity >= 1.0f)
                tableMasks(direct.table, rowValue, masks.data(), Tile::kSize);
            for (int x = 0; x < Tile::kSize; ++x) {
                const float sh = rowShape[x];
                if (opacity >= 1.0f && sh >= 1.0f)
                    continue;
                masks[static_cast<std::size_t>(x)] = sh <= 0.0f ? 0 : maskOf(coverageOf(rowValue[x], sh, opacity, fullAt));
            }
            if (state)
                stepRow(*state, row, rowValue, rowShape, masks.data());
            if (erase)  // what's kept, erased paint that fades below a 1024th gone (eraseKeepOf)
                for (int x = 0; x < Tile::kSize; ++x)
                    masks[static_cast<std::size_t>(x)] =
                        masks[static_cast<std::size_t>(x)] > kMaskOne - kMaskOne / 1024 ? 0 : kMaskOne - masks[static_cast<std::size_t>(x)];
            Channel* rowPx = px + row * Tile::kSize * 4;
            const Channel* rowFrom = from + row * Tile::kSize * 4;
            if (erase)
                layEraseRow(rowFrom, rowPx, masks.data(), Tile::kSize);
            else
                layOverRow(rowFrom, rowPx, direct.source, masks.data(), Tile::kSize);
        }
    };
    if (m_onWorkers && work.size() > 1)
        WorkerPool::forStrokes().run(static_cast<int>(work.size()), layOne);
    else
        for (int k = 0; k < static_cast<int>(work.size()); ++k)
            layOne(k);
}

// Stamps go on in the order they were laid, so a replay gives the same pixels.
void StampLayer::layInOneTile(Job& job, const std::vector<std::size_t>& ready) const {
    for (const std::size_t i : ready)
        layStampInTile(job, i);
}

bool StampLayer::Job::isWhollySolid() const {
    return tile && tile->solidPixels >= Tile::kSize * Tile::kSize;
}

void StampLayer::layStampInTile(Job& job, std::size_t i) const {
    const QRect area = job.coord.pixelRect();
    const PixelBounds bounds{m_deep[i].left, m_deep[i].top, m_deep[i].right, m_deep[i].bottom};
    if (m_stamps[i].opacity <= 0.0f || !overlaps(bounds, area) || job.isWhollySolid())
        return;
    const Deep& deep = m_deep[i];
    // Inside its deep reach a stamp not evened across lays its one density at every pixel: on a row that's a span, found from its ends by
    // the same test markOf makes, its distance only growing away from the centre, and laid with the same additions.
    const bool spans = m_stamps[i].chordRadius <= 0.0f && deep.reachSquared > 0.0f;
    const int left = std::max(bounds.left, area.left()), right = std::min(bounds.right, area.left() + Tile::kSize);
    // A paper-cut stamp (no deep reach, skirt or evening) eight pixels at a time, the rest of a row one by one.
    if (m_tip.paperPerStamp && m_stamps[i].profile >= 0 && m_stamps[i].chordRadius <= 0.0f && !rimCapped(m_stamps[i])) {
        const float* darks = m_tip.paperPerStamp->keptDarksOf(job.coord);
        for (int y = std::max(bounds.top, area.top()); y < std::min(bounds.bottom, area.top() + Tile::kSize); ++y) {
            int x = left;
            for (; x + kSpanLanes <= right; x += kSpanLanes)
                if (paperEight(job, i, x, y, darks))
                    job.changed = true;
            for (; x < right; ++x)
                layPixel(job, i, x, y);
        }
        return;
    }
    // The neighbours a skirt is cut by, where each one's blurred outline is whole: past kNormalReach deviations inside it the blur's table
    // gives exactly 1, and nothing gives more, so a skirt pixel inside one of these takes the cut as whole without working the outlines
    // out. On a row each is a chord, a pixel to spare at either end (kReachSpare).
    struct Whole {
        std::size_t j;
        float centreX, radius;
    };
    std::array<Whole, 2 * kMostSkirtNeighbours> wholes{};
    std::size_t wholeCount = 0;
    if (m_stamps[i].skirt > 0.0f && left < right) {
        const std::size_t around = std::min(static_cast<std::size_t>(std::max(m_tip.skirtNeighbours, 0)), kMostSkirtNeighbours);
        for (std::size_t step = 1; step <= around; ++step)
            for (const std::size_t j : {i - step, i + step}) {
                if (j >= m_stamps.size() || m_stamps[j].edgeBlur <= 0.0f)
                    continue;
                const float whole = m_stamps[j].radius - kNormalReach * m_stamps[j].edgeBlur - kReachSpare;
                if (whole > 0.0f)
                    wholes[wholeCount++] = {j, static_cast<float>(left) - m_frames[j].dx(left), whole};
            }
    }
    for (int y = std::max(bounds.top, area.top()); y < std::min(bounds.bottom, area.top() + Tile::kSize); ++y) {
        std::array<std::pair<int, int>, 2 * kMostSkirtNeighbours> chords{};
        std::size_t chordCount = 0;
        for (std::size_t w = 0; w < wholeCount; ++w) {
            const float dy = m_frames[wholes[w].j].dy(y), rest = wholes[w].radius * wholes[w].radius - dy * dy;
            if (rest <= 0.0f)
                continue;
            const float half = std::sqrt(rest) - kReachSpare;
            if (half > 0.0f)
                chords[chordCount++] = {static_cast<int>(std::ceil(wholes[w].centreX - half)),
                                        static_cast<int>(std::floor(wholes[w].centreX + half))};
        }
        const auto cutWhole = [&](int x) {
            for (std::size_t k = 0; k < chordCount; ++k)
                if (x >= chords[k].first && x <= chords[k].second)
                    return true;
            return false;
        };
        int from = right, to = right;  // the span inside the deep reach, [from, to)
        if (spans && left < right) {
            const float dy = deep.frame.dy(y);
            const auto inside = [&](int x) {
                const float dx = deep.frame.dx(x);
                return dx * dx + dy * dy < deep.reachSquared;
            };
            const int middle = std::clamp(left + static_cast<int>(std::lround(-deep.frame.dx(left))), left, right - 1);
            if (inside(middle)) {
                const float rest = deep.reachSquared - dy * dy;
                const int half = rest > 0.0f ? static_cast<int>(std::sqrt(rest)) : 0;
                from = std::clamp(middle - half, left, middle);
                while (from < middle && !inside(from))
                    ++from;
                while (from > left && inside(from - 1))
                    --from;
                to = std::clamp(middle + half + 1, middle + 1, right);
                while (to > middle + 1 && !inside(to - 1))
                    --to;
                while (to < right && inside(to))
                    ++to;
            }
        }
        const auto layRun = [&](int start, int end) {  // eight at a time where each is a whole-cut skirt pixel, else one by one
            for (int x = start; x < end;) {
                bool allCut = x + kSpanLanes <= end;
                for (int k = 0; allCut && k < kSpanLanes; ++k)
                    allCut = cutWhole(x + k);
                if (allCut && skirtEight(job, i, x, y)) {
                    x += kSpanLanes;
                    continue;
                }
                layPixel(job, i, x, y, cutWhole(x));
                ++x;
            }
        };
        layRun(left, from);
        const int row = (y - area.top()) * Tile::kSize - area.left();
        int x = from;
        if (to - from >= kSpanLanes) {  // eight at a time, the same steps
            if (!job.tile) {
                job.fresh = std::make_unique<DensityTile>();
                job.tile = job.fresh.get();
            }
            for (; x + kSpanLanes <= to; x += kSpanLanes)
                if (addEight(*job.tile, row + x, deep.density, deep.shape))
                    job.changed = true;
        }
        for (; x < to; ++x) {
            if (isSolid(job.tile, row + x))
                continue;
            addTo(job.tile, job.fresh, row + x, deep.density, deep.shape, 0.0f);
            job.changed = true;
        }
        layRun(to, right);
    }
}

void StampLayer::layPixel(Job& job, std::size_t i, int x, int y, bool cutWhole) const {
    const QRect area = job.coord.pixelRect();
    const int index = (y - area.top()) * Tile::kSize + (x - area.left());
    if (isSolid(job.tile, index))
        return;
    float density = 0.0f, shape = 0.0f, skirt = 0.0f;
    if (!markOf(i, x, y, density, shape, skirt, cutWhole))
        return;
    addTo(job.tile, job.fresh, index, density, shape, skirt);
    job.changed = true;
}

// Eight pixels of a row, each past the stamp's own outline (markOf's first two cases lay nothing there) and inside a neighbour's whole
// outline, so its skirt's cut is whole: each lane takes markOf's and addTo's steps, its distance, its share from the table read between,
// the skirt as density times that share, its outline raised whole, in the same order. Its caller has found all eight inside a
// neighbour's whole chord. Says whether it took them; if any is inside the outline's own reach, none is touched.
bool StampLayer::skirtEight(Job& job, std::size_t i, int x, int y) const {
    if (m_stamps[i].skirt <= 0.0f)
        return false;
    return skirtEightAvx2(job, i, x, y);
}

bool StampLayer::skirtEightAvx2(Job& job, std::size_t i, int x, int y) const {
    const Deep& deep = m_deep[i];
    const Stamp& stamp = m_stamps[i];
    alignas(32) float dxs[kSpanLanes];
    for (int k = 0; k < kSpanLanes; ++k)
        dxs[k] = deep.frame.dx(x + k);
    const float dy = deep.frame.dy(y);
    const __m256 dx = _mm256_load_ps(dxs);
    const __m256 d2 = _mm256_add_ps(_mm256_mul_ps(dx, dx), _mm256_set1_ps(dy * dy));
    if (_mm256_movemask_ps(_mm256_cmp_ps(d2, _mm256_set1_ps(deep.outsideSquared), _CMP_LT_OQ)) != 0)
        return false;  // inside the outline's reach: markOf's own cases
    const __m256 inSkirt = _mm256_cmp_ps(d2, _mm256_set1_ps(deep.skirtSquared), _CMP_LT_OQ);
    const __m256 outside = _mm256_sub_ps(_mm256_sqrt_ps(d2), _mm256_set1_ps(stamp.radius));
    const __m256 at = _mm256_mul_ps(
        _mm256_div_ps(_mm256_max_ps(outside, _mm256_setzero_ps()), _mm256_set1_ps(static_cast<float>(kSkirtReach) * stamp.skirt)),
        _mm256_set1_ps(static_cast<float>(kSkirtSteps)));
    const __m256 within = _mm256_and_ps(inSkirt, _mm256_cmp_ps(at, _mm256_set1_ps(static_cast<float>(kSkirtSteps)), _CMP_LT_OQ));
    const __m256i step = _mm256_cvttps_epi32(_mm256_and_ps(at, within));  // 0 where it isn't used, to read inside the table
    const __m256 a = _mm256_i32gather_ps(kSkirtTable.data(), step, 4);
    const __m256 b = _mm256_i32gather_ps(kSkirtTable.data() + 1, step, 4);
    const __m256 share = _mm256_add_ps(a, _mm256_mul_ps(_mm256_sub_ps(b, a), _mm256_sub_ps(at, _mm256_cvtepi32_ps(step))));
    const __m256 skirt = _mm256_mul_ps(_mm256_set1_ps(densityOf(stamp.opacity, 1.0f)), share);
    const __m256 lays = _mm256_and_ps(within, _mm256_cmp_ps(skirt, _mm256_setzero_ps(), _CMP_GT_OQ));
    if (_mm256_movemask_ps(lays) == 0)
        return true;  // none lays anything, as markOf would find
    if (!job.tile) {
        job.fresh = std::make_unique<DensityTile>();
        job.tile = job.fresh.get();
    }
    const QRect area = job.coord.pixelRect();
    const int index = (y - area.top()) * Tile::kSize + (x - area.left());
    float* value = job.tile->value.data() + index;
    float* shapes = job.tile->shape.data() + index;
    const __m256 v = _mm256_loadu_ps(value), s = _mm256_loadu_ps(shapes);
    const __m256 one = _mm256_set1_ps(1.0f), solidDensity = _mm256_set1_ps(kSolidDensity);
    const __m256 wasSolid = _mm256_and_ps(_mm256_cmp_ps(s, one, _CMP_GE_OQ), _mm256_cmp_ps(v, solidDensity, _CMP_GE_OQ));
    const __m256 takes = _mm256_andnot_ps(wasSolid, lays);
    // addTo: its density (0) times its shape (1), then the skirt; the shape raised to 1.
    const __m256 nv = _mm256_blendv_ps(v, _mm256_add_ps(_mm256_add_ps(v, _mm256_setzero_ps()), skirt), takes);
    const __m256 ns = _mm256_blendv_ps(s, _mm256_max_ps(s, one), takes);
    _mm256_storeu_ps(value, nv);
    _mm256_storeu_ps(shapes, ns);
    const __m256 nowSolid =
        _mm256_and_ps(takes, _mm256_and_ps(_mm256_cmp_ps(ns, one, _CMP_GE_OQ), _mm256_cmp_ps(nv, solidDensity, _CMP_GE_OQ)));
    job.tile->solidPixels += std::popcount(static_cast<unsigned>(_mm256_movemask_ps(nowSolid)));
    if (_mm256_movemask_ps(takes) != 0)
        job.changed = true;
    return true;
}

bool StampLayer::markOf(std::size_t i, int x, int y, float& density, float& shape, float& skirt, bool cutWhole) const {
    if (!m_tip.paperPerStamp)
        return plainMarkOf(i, x, y, density, shape, skirt, cutWhole);
    // The stamp's coverage there, its profile, less the paper's darkness times Intensity, before its opacity. Its stamps
    // have no deep reach, no skirt, no blur and no rim, so plainMarkOf's steps come to these, taken here in the same order: its outline's
    // ramp, its paint the profile, its density from those.
    const Stamp& stamp = m_stamps[i];
    const Deep& deep = m_deep[i];
    skirt = 0.0f;
    const float dx = deep.frame.dx(x), dy = deep.frame.dy(y);
    const float distanceSquared = dx * dx + dy * dy;
    if (!(distanceSquared < deep.outsideSquared))
        return false;
    const float distance = std::sqrt(distanceSquared);
    const float outer = outerOf(stamp);
    if (distance >= outer)
        return false;
    const float outline = std::min(rampAt(stamp, outer - distance), rimCeiling(stamp, distance));
    const float profile = profilePaint(stamp.profile, distance * profileReach(stamp.profile) / stamp.radius);
    if (!(outline > 0.0f && profile > 0.0f))
        return false;
    // The cut coverage straight into one density: the same rule as cutting the density's coverage afterwards, in one logarithm
    // rather than two and an exponential (the trial's output moves in the last bits).
    const float cut = profile - m_tip.paperPerStamp->intensity() * m_tip.paperPerStamp->keptDarkAt(x, y);
    if (cut <= 0.0f)
        return false;
    density = densityOf(stamp.opacity, cut) * acrossFactor(stamp, dx, dy);
    shape = outline;
    return true;
}

namespace {

// log1p(-laid) for laid in [0, 1), eight at once: the logarithm of u = 1 - laid from its exponent and a polynomial in its mantissa
// (Cephes' single-precision one, within about a unit in the last place), times laid / (1 - u), which takes back what rounding u lost.
// Multiplies and adds only, so no fused multiply-adds.
__m256 logOneLess(__m256 laid) {
    const __m256 one = _mm256_set1_ps(1.0f);
    const __m256 u = _mm256_sub_ps(one, laid);
    const __m256i bits = _mm256_castps_si256(u);
    __m256 e = _mm256_cvtepi32_ps(_mm256_sub_epi32(_mm256_srli_epi32(bits, 23), _mm256_set1_epi32(126)));
    __m256 m = _mm256_castsi256_ps(_mm256_or_si256(_mm256_and_si256(bits, _mm256_set1_epi32(0x007fffff)), _mm256_set1_epi32(0x3f000000)));
    const __m256 small = _mm256_cmp_ps(m, _mm256_set1_ps(0.707106781186547524f), _CMP_LT_OQ);
    e = _mm256_sub_ps(e, _mm256_and_ps(small, one));
    m = _mm256_sub_ps(_mm256_add_ps(m, _mm256_and_ps(small, m)), one);
    const __m256 z = _mm256_mul_ps(m, m);
    __m256 y = _mm256_set1_ps(7.0376836292e-2f);
    for (const float c : {-1.1514610310e-1f, 1.1676998740e-1f, -1.2420140846e-1f, 1.4249322787e-1f, -1.6668057665e-1f, 2.0000714765e-1f,
                          -2.4999993993e-1f, 3.3333331174e-1f})
        y = _mm256_add_ps(_mm256_mul_ps(y, m), _mm256_set1_ps(c));
    y = _mm256_mul_ps(_mm256_mul_ps(y, m), z);
    y = _mm256_add_ps(y, _mm256_mul_ps(e, _mm256_set1_ps(-2.12194440e-4f)));
    y = _mm256_sub_ps(y, _mm256_mul_ps(z, _mm256_set1_ps(0.5f)));
    __m256 logU = _mm256_add_ps(_mm256_add_ps(m, y), _mm256_mul_ps(e, _mm256_set1_ps(0.693359375f)));
    const __m256 lost = _mm256_sub_ps(u, one);  // -laid as u holds it
    const __m256 exact = _mm256_cmp_ps(lost, _mm256_setzero_ps(), _CMP_EQ_OQ);
    const __m256 negLaid = _mm256_sub_ps(_mm256_setzero_ps(), laid);
    logU = _mm256_mul_ps(logU, _mm256_div_ps(negLaid, _mm256_blendv_ps(lost, one, exact)));
    return _mm256_blendv_ps(logU, negLaid, exact);
}

}

bool StampLayer::paperEight(Job& job, std::size_t i, int x, int y, const float* darks) const {
    const Stamp& stamp = m_stamps[i];
    const Deep& deep = m_deep[i];
    const QRect area = job.coord.pixelRect();
    const int index = (y - area.top()) * Tile::kSize + (x - area.left());
    alignas(32) float dxs[kSpanLanes];
    for (int k = 0; k < kSpanLanes; ++k)
        dxs[k] = deep.frame.dx(x + k);
    const __m256 dx = _mm256_load_ps(dxs), dy = _mm256_set1_ps(deep.frame.dy(y));
    const __m256 d2 = _mm256_add_ps(_mm256_mul_ps(dx, dx), _mm256_mul_ps(dy, dy));
    __m256 take = _mm256_cmp_ps(d2, _mm256_set1_ps(deep.outsideSquared), _CMP_LT_OQ);
    if (_mm256_movemask_ps(take) == 0)
        return false;
    const __m256 distance = _mm256_sqrt_ps(d2);
    const float outer = outerOf(stamp);
    take = _mm256_and_ps(take, _mm256_cmp_ps(distance, _mm256_set1_ps(outer), _CMP_LT_OQ));
    // Its outline's ramp, rampAt's steps (a straight ramp a pixel or more wide, no wider than the stamp).
    const __m256 one = _mm256_set1_ps(1.0f);
    __m256 outline =
        _mm256_min_ps(_mm256_div_ps(_mm256_sub_ps(_mm256_set1_ps(outer), distance), _mm256_set1_ps(std::max(stamp.rampWidth, 1.0f))), one);
    outline = _mm256_min_ps(outline, _mm256_set1_ps(stamp.radius * 2.0f));
    // Its profile, read between the table's steps.
    const __m256 along = _mm256_div_ps(_mm256_mul_ps(distance, _mm256_set1_ps(profileReach(stamp.profile))), _mm256_set1_ps(stamp.radius));
    const __m256 at = _mm256_div_ps(along, _mm256_set1_ps(kProfileStep));
    const __m256i i0 = _mm256_min_epi32(_mm256_cvttps_epi32(_mm256_min_ps(at, _mm256_set1_ps(float(kProfileSteps)))),
                                        _mm256_set1_epi32(kProfileSteps - 2));
    const float* row = kProfile[std::clamp(int(stamp.profile), 0, 3)];
    const __m256 p0 = _mm256_i32gather_ps(row, i0, 4), p1 = _mm256_i32gather_ps(row, _mm256_add_epi32(i0, _mm256_set1_epi32(1)), 4);
    const __m256 inTable = _mm256_cmp_ps(at, _mm256_set1_ps(float(kProfileSteps - 1)), _CMP_LT_OQ);
    const __m256 profile =
        _mm256_and_ps(inTable, _mm256_add_ps(p0, _mm256_mul_ps(_mm256_sub_ps(p1, p0), _mm256_sub_ps(at, _mm256_cvtepi32_ps(i0)))));
    // Its coverage less the paper's, before its opacity, into one density (densityOf's steps).
    const __m256 cut =
        _mm256_sub_ps(profile, _mm256_mul_ps(_mm256_set1_ps(m_tip.paperPerStamp->intensity()), _mm256_loadu_ps(darks + index)));
    const __m256 zero = _mm256_setzero_ps();
    take = _mm256_and_ps(take, _mm256_and_ps(_mm256_cmp_ps(outline, zero, _CMP_GT_OQ), _mm256_cmp_ps(profile, zero, _CMP_GT_OQ)));
    take = _mm256_and_ps(take, _mm256_cmp_ps(cut, zero, _CMP_GT_OQ));
    if (_mm256_movemask_ps(take) == 0)
        return false;
    const __m256 laid = _mm256_mul_ps(_mm256_set1_ps(stamp.opacity), cut);
    const __m256 solidDensity = _mm256_set1_ps(kSolidDensity);
    const __m256 whole = _mm256_cmp_ps(laid, one, _CMP_GE_OQ);
    __m256 density = _mm256_min_ps(_mm256_sub_ps(zero, logOneLess(_mm256_min_ps(laid, _mm256_set1_ps(0.99999994f)))), solidDensity);
    density = _mm256_blendv_ps(density, solidDensity, whole);
    // addTo's steps, the pixels already solid passed, as addEight's.
    if (!job.tile) {
        job.fresh = std::make_unique<DensityTile>();
        job.tile = job.fresh.get();
    }
    float* value = job.tile->value.data() + index;
    float* shapes = job.tile->shape.data() + index;
    const __m256 v = _mm256_loadu_ps(value), s = _mm256_loadu_ps(shapes);
    const __m256 wasSolid = _mm256_and_ps(_mm256_cmp_ps(s, one, _CMP_GE_OQ), _mm256_cmp_ps(v, solidDensity, _CMP_GE_OQ));
    take = _mm256_andnot_ps(wasSolid, take);
    const __m256 nv = _mm256_blendv_ps(v, _mm256_add_ps(v, _mm256_mul_ps(density, outline)), take);
    const __m256 ns = _mm256_blendv_ps(s, _mm256_max_ps(s, outline), take);
    _mm256_storeu_ps(value, nv);
    _mm256_storeu_ps(shapes, ns);
    const __m256 nowSolid =
        _mm256_and_ps(take, _mm256_and_ps(_mm256_cmp_ps(ns, one, _CMP_GE_OQ), _mm256_cmp_ps(nv, solidDensity, _CMP_GE_OQ)));
    job.tile->solidPixels += std::popcount(static_cast<unsigned>(_mm256_movemask_ps(nowSolid)));
    return _mm256_movemask_ps(take) != 0;
}

bool StampLayer::plainMarkOf(std::size_t i, int x, int y, float& density, float& shape, float& skirt, bool cutWhole) const {
    skirt = 0.0f;
    const Deep& deep = m_deep[i];
    const float dx = deep.frame.dx(x), dy = deep.frame.dy(y);
    const float distanceSquared = dx * dx + dy * dy;
    if (m_stamps[i].formReach > 0.0f)
        return formedMarkOf(i, dx, dy, distanceSquared, density, shape);
    if (m_stamps[i].hard) {
        // The pixel the centre is in is the one whose dx and dy, from its own centre, are each in (-1/2, 1/2] (PixelFrame).
        const bool underCentre = dx > -0.5f && dx <= 0.5f && dy > -0.5f && dy <= 0.5f;
        if (distanceSquared > m_stamps[i].radius * m_stamps[i].radius && !underCentre)
            return false;
        density = densityOf(m_stamps[i].opacity, 1.0f);
        shape = 1.0f;
        return true;
    }
    if (distanceSquared < deep.reachSquared) {
        density = deep.density * acrossFactor(m_stamps[i], dx, dy);
        shape = deep.shape;
        return true;
    }
    bool laid = false;
    float distance = -1.0f;
    if (distanceSquared < deep.outsideSquared) {  // past its outline limitedMark gives nothing: the square's corners
        distance = std::sqrt(distanceSquared);
        const StampMark mark = limitedMark(m_stamps, i, m_tip.edge, distance, headingCosine(m_stamps[i], dx, dy, distance));
        if (mark.shape > 0.0f && mark.paint > 0.0f) {
            density = densityOf(m_stamps[i].opacity, mark.paint) * acrossFactor(m_stamps[i], dx, dy);
            shape = mark.shape;
            laid = true;
        }
    }
    if (distanceSquared < deep.skirtSquared) {
        if (distance < 0.0f)
            distance = std::sqrt(distanceSquared);
        float raised = 0.0f;
        skirt = skirtOf(i, x, y, distance, raised, cutWhole);
        if (skirt > 0.0f) {
            // Its own paint goes in at its own outline's share, beside the skirt, and the pixel's outline is raised to the cut.
            if (laid)
                skirt += density * shape;
            else
                shape = 0.0f;
            density = 0.0f;
            shape = std::max(shape, raised);
            laid = true;
        }
    }
    return laid;
}

// A formed stamp at a pixel (dx, dy) from its centre: its outline goes Strength of the way from the round stamp's to the picture's,
// and its paint Strength of the way from the round stamp's to all of it, so where the stroke's paint is full its coverage is the most of
// the pictures over it, and where it isn't the tool's own Density and pressure set its tone.
bool StampLayer::formedMarkOf(std::size_t i, float dx, float dy, float distanceSquared, float& density, float& shape) const {
    const Stamp& stamp = m_stamps[i];
    const StampForm& form = *m_tip.form;
    if (form.fixedToCanvas()) {  // a blot map: the cone through the canvas's picture and the Shape's window; its paint as if it weren't there
        const int x = static_cast<int>(std::floor(stamp.centre.x() + dx)), y = static_cast<int>(std::floor(stamp.centre.y() + dy));
        shape =
            form.blotShape(std::sqrt(distanceSquared), (stamp.nominalRadius > 0.0f ? stamp.nominalRadius : stamp.radius), x, y, m_tip.edge);
        if (shape <= 0.0f)
            return false;
        density = densityOf(stamp.opacity, shape) * acrossFactor(stamp, dx, dy);  // its window's share, building up where stamps overlap
        shape = 1.0f;
        return true;
    }
    if (form.stamped()) {  // a brush shape: the picture whole, its darkness times Dens. what it lays, over what's there (StampTip::pictureStamps)
        const float light = form.at(dx, dy, stamp.radius, stamp.turnCos, stamp.turnSin);
        const float picture = light * form.strength();
        if (picture <= 0.0f)
            return false;
        if (form.oneStroke()) {
            shape = oneStrokeOutline(stamp.opacity, m_tip.pictureFullAt, form.strength());
            density = oneStrokeDensity(m_tip.pictureFullAt, light);
            return shape > 0.0f;
        }
        shape = 1.0f;
        density = densityOf(stamp.opacity, picture);
        return true;
    }
    StampMark round;
    if (distanceSquared < outerOf(stamp) * outerOf(stamp)) {
        const float distance = std::sqrt(distanceSquared);
        round = stampMark(stamp, m_tip.edge, distance, headingCosine(stamp, dx, dy, distance));
    }
    const float picture = stamp.hairs ? hairCoverage(stamp, dx, dy) : form.at(dx, dy, stamp.radius);
    shape = round.shape + (picture - round.shape) * form.strength();
    const float paint = round.paint + (1.0f - round.paint) * form.strength();
    if (shape <= 0.0f || paint <= 0.0f)
        return false;
    density = densityOf(stamp.opacity, paint) * acrossFactor(stamp, dx, dy);
    return true;
}

// What stamp i's skirt adds at a pixel `distance` px from its centre, past what its own outline lays: its share from the table, cut by
// the most outline of the skirtNeighbours stamps either side of it, as DabPainter::skirtAt has it on the Brush. `raised` is that
// cut, the outline the pixel is raised to where the skirt adds anything, so the skirt shows from now.
float StampLayer::skirtOf(std::size_t i, int x, int y, float distance, float& raised, bool cutWhole) const {
    const Stamp& stamp = m_stamps[i];
    const float share = skirtShare(stamp, distance - stamp.radius), own = outlineShareOf(stamp, distance);
    if (share <= own)
        return 0.0f;
    const auto around = static_cast<std::size_t>(m_tip.skirtNeighbours);
    float most = cutWhole ? 1.0f : 0.0f;  // a neighbour's outline known whole here gives the most there is
    for (std::size_t step = 1; step <= around && most < 1.0f; ++step)
        for (const std::size_t j : {i - step, i + step}) {
            if (j >= m_stamps.size())  // past either end of those kept (below 0 wraps past the end)
                continue;
            const float jx = m_frames[j].dx(x), jy = m_frames[j].dy(y);
            const float jd = std::sqrt(jx * jx + jy * jy);
            most = std::max(most, outlineShareOf(m_stamps[j], jd));
        }
    if (share * most <= own)
        return 0.0f;
    raised = most;
    return densityOf(stamp.opacity, 1.0f) * (share * most - own);
}

// A tile of a plain tool's grid. First the points take the stamps, as pixels would, then each cell they reach is read between
// them where they're all inside the outline and show the same within kStampSmoothRange; otherwise its pixels take what the points
// were before these stamps, read between them, and take the stamps one by one, until they show what the reading would again.
// On the grid a cell is read between its points or worked out per pixel by what each lot of stamps leaves; a skirt cuts across cells its
// stamps' outlines already left smooth, so with a skirt each tile takes its stamps one at a time, and a stroke painted on its own thread,
// in other lots, gives the same pixels. A tile reads and writes only its own, so the lot goes to the workers once.
void StampLayer::layGridInTile(Job& job, const std::vector<std::size_t>& ready) const {
    if (m_tip.skirtNeighbours <= 0) {
        layGridGroup(job, ready);
        return;
    }
    for (const std::size_t i : ready)
        layGridGroup(job, {i});
}

void StampLayer::layGridGroup(Job& job, const std::vector<std::size_t>& ready) const {
    const int cell = m_gridCell, across = Tile::kSize / cell;
    const QRect area = job.coord.pixelRect();
    std::vector<std::size_t> here;
    for (const std::size_t i : ready) {
        const PixelBounds bounds{m_deep[i].left, m_deep[i].top, m_deep[i].right, m_deep[i].bottom};
        // The tile's points include the next tiles' first column and row.
        if (m_stamps[i].opacity > 0.0f && overlaps(bounds, area.adjusted(0, 0, 1, 1)))
            here.push_back(i);
    }
    if (here.empty())
        return;
    if (!job.tile) {
        job.fresh = std::make_unique<DensityTile>();
        job.tile = job.fresh.get();
    }
    DensityTile& tile = *job.tile;
    if (tile.gridCell == 0)
        tile.makeGrid(cell);
    const int side = across + 1;
    if (tile.solidPoints >= side * side && tile.perPixelCells == 0)
        return;  // every point solid and every cell read between them: more paint shows nothing more
    // The points these stamps can change, and the corners of every cell their bounds touch, a point to spare: only these are kept as they
    // were and worked out, the rest being as they were already.
    int boxLeft = across, boxRight = 0, boxTop = across, boxBottom = 0;
    for (const std::size_t i : ready) {
        const PixelBounds bounds{m_deep[i].left, m_deep[i].top, m_deep[i].right, m_deep[i].bottom};
        boxLeft = std::min(boxLeft, std::max(0, floorDiv(bounds.left - area.left() - cell, cell)));
        boxRight = std::max(boxRight, std::min(across, floorDiv(bounds.right - area.left(), cell) + 1));
        boxTop = std::min(boxTop, std::max(0, floorDiv(bounds.top - area.top() - cell, cell)));
        boxBottom = std::max(boxBottom, std::min(across, floorDiv(bounds.bottom - area.top(), cell) + 1));
    }
    for (int gy = boxTop; gy <= boxBottom; ++gy)
        for (int gx = boxLeft; gx <= boxRight; ++gx) {
            const std::size_t g = static_cast<std::size_t>(gy * side + gx);
            tile.beforeValue[g] = tile.pointValue[g];
            tile.beforeShape[g] = tile.pointShape[g];
        }
    const auto solid = [](float value, float shape) { return shape >= 1.0f && value >= kSolidDensity; };
    const auto lay = [&](std::size_t i, int x, int y, float& value, float& shape) {
        if (solid(value, shape))
            return false;
        float density = 0.0f, mark = 0.0f, skirt = 0.0f;
        if (!markOf(i, x, y, density, mark, skirt))
            return false;
        value += density * mark;
        if (skirt > 0.0f)
            value += skirt;
        shape = std::max(shape, mark);
        return true;
    };
    for (const std::size_t i : here) {
        const PixelBounds bounds{m_deep[i].left, m_deep[i].top, m_deep[i].right, m_deep[i].bottom};
        for (int gy = 0; gy < side; ++gy) {
            const int y = area.top() + gy * cell;
            if (y < bounds.top || y >= bounds.bottom)
                continue;
            for (int gx = 0; gx < side; ++gx) {
                const int x = area.left() + gx * cell;
                if (x < bounds.left || x >= bounds.right)
                    continue;
                const std::size_t g = static_cast<std::size_t>(gy * side + gx);
                const bool wasSolid = solid(tile.pointValue[g], tile.pointShape[g]);
                if (lay(i, x, y, tile.pointValue[g], tile.pointShape[g]) && !wasSolid && solid(tile.pointValue[g], tile.pointShape[g]))
                    ++tile.solidPoints;
            }
        }
        for (int cy = 0; cy < across; ++cy) {
            const int y = area.top() + cy * cell + cell / 2;
            if (y < bounds.top || y >= bounds.bottom)
                continue;
            for (int cx = 0; cx < across; ++cx) {
                const int x = area.left() + cx * cell + cell / 2;
                const std::size_t c = static_cast<std::size_t>(cy * across + cx);
                if (x >= bounds.left && x < bounds.right)
                    lay(i, x, y, tile.middleValue[c], tile.middleShape[c]);
            }
        }
    }
    // Coverage before the stroke's opacity, as the merge works it out: none where the outline hasn't reached.
    const float full = fullCoverageFrom(m_tip.fullAt);
    const auto shows = [&](float value, float shape) {
        if (shape <= 0.0f || value <= 0.0f)
            return 0.0f;
        return value / shape >= full ? shape : coverageOf(value, shape, 1.0f, m_tip.fullAt);
    };
    std::vector<float> pointShows(tile.pointValue.size());
    for (int gy = boxTop; gy <= boxBottom; ++gy)
        for (int gx = boxLeft; gx <= boxRight; ++gx) {
            const std::size_t g = static_cast<std::size_t>(gy * side + gx);
            pointShows[g] = shows(tile.pointValue[g], tile.pointShape[g]);
        }
    // How far each stamp can lay anything, its outline or its skirt, a pixel to spare: a cell read or worked out per pixel that none
    // reaches keeps its points, its middle and what it shows, so it's passed.
    std::vector<std::pair<QPointF, float>> reaches;
    for (const std::size_t i : ready) {
        const float far = std::sqrt(std::max(m_deep[i].outsideSquared, m_deep[i].skirtSquared)) + kReachSpare;
        reaches.emplace_back(m_stamps[i].centre, far * far);
    }
    const auto reachedBy = [&](int x0, int y0) {
        for (const auto& [centre, farSquared] : reaches) {
            const double dx = std::max({double(x0) - centre.x(), centre.x() - double(x0 + cell), 0.0});
            const double dy = std::max({double(y0) - centre.y(), centre.y() - double(y0 + cell), 0.0});
            if (dx * dx + dy * dy <= farSquared)
                return true;
        }
        return false;
    };
    for (int cy = 0; cy < across; ++cy) {
        for (int cx = 0; cx < across; ++cx) {
            const std::size_t c = static_cast<std::size_t>(cy * across + cx);
            const int x0 = area.left() + cx * cell, y0 = area.top() + cy * cell;
            bool touched = false;
            for (const std::size_t i : here) {
                const PixelBounds bounds{m_deep[i].left, m_deep[i].top, m_deep[i].right, m_deep[i].bottom};
                touched = touched || (bounds.left < x0 + cell + 1 && bounds.right > x0 && bounds.top < y0 + cell + 1 && bounds.bottom > y0);
            }
            if (!touched || (tile.cellState[c] != DensityTile::Empty && !reachedBy(x0, y0)))
                continue;
            const std::array<std::size_t, 4> corner{static_cast<std::size_t>(cy * side + cx), static_cast<std::size_t>(cy * side + cx + 1),
                                                    static_cast<std::size_t>((cy + 1) * side + cx),
                                                    static_cast<std::size_t>((cy + 1) * side + cx + 1)};
            const auto inside = [&](const std::vector<float>& shapes) {  // reached by the outline, and it turns slowly here if at all
                float least = 2.0f, most = -1.0f;
                for (const std::size_t g : corner) {
                    least = std::min(least, shapes[g]);
                    most = std::max(most, shapes[g]);
                }
                return least > 0.0f && (least >= 1.0f || most - least <= kStampShapeSpread);
            };
            // The middle pixel, worked out exactly, against the reading there.
            const auto middleAgrees = [&] {
                constexpr float kHalf = 0.5f;  // the middle pixel is halfway between each pair of corners
                const float reading = kHalf * (kHalf * pointShows[corner[0]] + kHalf * pointShows[corner[2]]) +
                                      kHalf * (kHalf * pointShows[corner[1]] + kHalf * pointShows[corner[3]]);
                return tile.middleShape[c] > 0.0f &&
                       std::abs(shows(tile.middleValue[c], tile.middleShape[c]) - reading) <= kStampSmoothRange;
            };
            // And its points differ by no more than kStampSpreadCap, which catches a step near a corner the middle doesn't see.
            const auto spreadOk = [&] {
                float least = 2.0f, most = -1.0f;
                for (const std::size_t g : corner) {
                    least = std::min(least, pointShows[g]);
                    most = std::max(most, pointShows[g]);
                }
                return most - least <= kStampSpreadCap;
            };
            const bool smooth = inside(tile.pointShape) && middleAgrees() && spreadOk();
            std::uint8_t& state = tile.cellState[c];
            const bool cornersMoved = std::any_of(corner.begin(), corner.end(), [&](std::size_t g) {
                return tile.pointValue[g] != tile.beforeValue[g] || tile.pointShape[g] != tile.beforeShape[g];
            });
            if (smooth) {  // read between the points, a cell worked out per pixel going back to it
                if (state != DensityTile::Read || cornersMoved)
                    tile.cellChanged[c] = 1;
                if (state == DensityTile::PerPixel)
                    --tile.perPixelCells;
                state = DensityTile::Read;
                continue;
            }
            const auto weights = [&](int x, int y) {
                const float fx = static_cast<float>(x - x0) / static_cast<float>(cell),
                            fy = static_cast<float>(y - y0) / static_cast<float>(cell);
                return std::array<float, 4>{(1.0f - fx) * (1.0f - fy), fx * (1.0f - fy), (1.0f - fx) * fy, fx * fy};
            };
            if (state != DensityTile::PerPixel) {
                // Its pixels take up what the points were before these stamps, read between them.
                for (int y = y0; y < y0 + cell; ++y)
                    for (int x = x0; x < x0 + cell; ++x) {
                        const std::array<float, 4> w = weights(x, y);
                        float value = 0.0f, shape = 0.0f;
                        for (std::size_t k = 0; k < 4; ++k) {
                            value += w[k] * tile.beforeValue[corner[k]];
                            shape += w[k] * tile.beforeShape[corner[k]];
                        }
                        const std::size_t index = static_cast<std::size_t>((y - area.top()) * Tile::kSize + (x - area.left()));
                        tile.value[index] = value;
                        tile.shape[index] = shape;
                    }
                state = DensityTile::PerPixel;
                ++tile.perPixelCells;
            }
            tile.cellChanged[c] = 1;
            for (const std::size_t i : here) {
                const PixelBounds bounds{m_deep[i].left, m_deep[i].top, m_deep[i].right, m_deep[i].bottom};
                for (int y = std::max(y0, bounds.top); y < std::min(y0 + cell, bounds.bottom); ++y)
                    for (int x = std::max(x0, bounds.left); x < std::min(x0 + cell, bounds.right); ++x) {
                        const std::size_t index = static_cast<std::size_t>((y - area.top()) * Tile::kSize + (x - area.left()));
                        lay(i, x, y, tile.value[index], tile.shape[index]);
                    }
            }
        }
    }
    job.changed = true;
}

}
