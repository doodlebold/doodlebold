#pragma once

#include "core/stroke/StrokeMerge.h"
#include "core/input/InputSample.h"
#include "core/stroke/Bristles.h"
#include "core/stroke/StampForm.h"
#include "core/stroke/StrokeBuffer.h"
#include "core/stroke/StrokePath.h"

#include <QPointF>
#include <QRect>

#include <array>
#include <cmath>
#include <cstdint>
#include <deque>
#include <optional>
#include <unordered_map>
#include <memory>
#include <utility>
#include <vector>

namespace doodlebold {

// The least share of its chord a row evened across the stroke counts, so the very edge, where a chord is nothing, doesn't lay without limit.
constexpr float kLeastChordShare =
    0.15f;  // the least share of a chord a row evened across the stroke counts: it sets how crisp the side's last rows are; from 0.12 to 0.19 the side's width stays within 0.1 px of it

// One soft round stamp of the tip.
// In floats what goes into every pixel's mark. The centre stays double, as a place on the canvas; pressure, weight and step
// only go into each dab's and each stroke's sums.
struct Stamp {
    QPointF centre;
    float radius = 0.5f;  // px, never under the thinnest a stamp is drawn
    float band = 1.0f;  // how wide its soft rim is, px, at least 1
    float opacity = 1.0f;  // at its core
    double pressure = 1.0;  // the pen's, where it was laid
    float edgeBlur = 0.0f;  // px: above 0, the outline is a box of the radius blurred by this much, in place of a one-pixel ramp
    float rampWidth = 1.0f;  // px: without the blur, how wide the outline's straight antialiased ramp is, centred on the radius
    bool easedRamp = false;  // the ramp eases in and out, as a smoothstep, in place of a straight line
    double weight = 1.0;  // the share of a whole step of path it stands for: under 1 where stamps go closer than their spacing
    bool click = false;  // a click's one stamp, where the pen landed and never moved
    double step = 0.0;  // px of path it stands for, to the next stamp; 0 off a walked line
    float paintRamp =
        0.0f;  // above 0, its paint falls away over this many px at its outer end, so neighbouring stamps' edges add up evenly
    float headingX = 0.0f, headingY = 0.0f;  // which way the walk was going, as a unit vector
    float chordRadius =
        0.0f;  // above 0, what it lays is evened out across the stroke, each row getting the whole of its chord of the stamp
    float evenFrom = 0.0f;  // with a chordRadius: the share of it across from which it's evened; at 0, from its middle
    float leastChord = kLeastChordShare;  // and the least share of a chord it evens to, so the very edge doesn't lay without limit
    float penSamplesAcross = 0.0f;  // how many pen samples its diameter spans: it over the path between the two either side
    float samplesAcross =
        0.0f;  // with a gather, how many pen samples its diameter spans: it over the path between the two either side; 0 without
    // Above 0, it lays a skirt past its outline, shown only inside the outlines of the stamps near it on the path, as wide as skirtShare
    // says for this many px, the step of path it stands for (StampTip::skirtNeighbours).
    float skirt = 0.0f;
    bool hard = false;  // a hard disc (StampTip::hard)
    // At 0 and above, its paint is the reference's dab on that Shape (StampTip::profile): its radius is then the profile's reach, its
    // outline whole to there.
    std::int8_t profile = -1;
    float formReach = 0.0f;  // above 0, with a form (StampTip::form): how far from its centre its picture reaches
    // A bristle map's stamp (StampTip::bristles): its outline is its hairs, each the run hairRadius px across its radius from where it was
    // at the stamp before (fromX, fromY) to where it is now (dx, dy), both from this stamp's centre, whole whichever way it runs.
    struct HairDot {
        float dx = 0.0f, dy = 0.0f;
        float fromX = 0.0f, fromY = 0.0f;
    };
    std::shared_ptr<const std::vector<HairDot>> hairs{};
    float hairRadius = 0.0f;
    float nominalRadius = 0.0f;  // the tip's radius here before its outline's insets: a blot map's cone is measured from it
    float turnCos = 1.0f, turnSin = 0.0f;  // a stamped picture's turn (StampTip::pictureStamps)
    // The event-laid Marker: its own share in its group's mask, Density times the pressure's share; its opacity, the top a pixel
    // under it reaches, is that times the pressure's share again. Below 0, its opacity is its share too.
    float share = -1.0f;
};

// From a pixel to a stamp's centre in floats. A canvas position in a float is only good to about 2 thousandths of a pixel at 16384 px,
// so the centre is kept as the whole pixel it falls in and how far into it: the whole part is an exact subtraction of ints, and each
// distance is then a small number from the start, as good as a float can hold it at any place on any canvas.
class PixelFrame {
public:
    PixelFrame() = default;
    explicit PixelFrame(QPointF centre)
        : m_x(static_cast<int>(std::floor(centre.x()))),
          m_y(static_cast<int>(std::floor(centre.y()))),
          m_toPixelX(0.5f - static_cast<float>(centre.x() - m_x)),
          m_toPixelY(0.5f - static_cast<float>(centre.y() - m_y)) {}

    float dx(int x) const { return static_cast<float>(x - m_x) + m_toPixelX; }
    float dy(int y) const { return static_cast<float>(y - m_y) + m_toPixelY; }

private:
    int m_x = 0;
    int m_y = 0;
    float m_toPixelX = 0.0f;
    float m_toPixelY = 0.0f;
};

// What a stamp lays at `distance` px from its centre: a solid core, then a rim shaped by one of the five edges, from the softest (0)
// to the firmest (4), as paint, and how much of the pixel its outline covers, as shape: the outline's last pixel is a plain
// antialiased ramp. Both are worked out exactly at the pixel's centre, with no supersampling. Keeping the ramp apart means
// overlapping stamps build up paint but not the outline, so a hard edge stays a pixel wide.
struct StampMark {
    float paint = 0.0f;
    float shape = 0.0f;
    float passed = 0.0f;  // with passLength, how much of its soft edge passed over the pixel, as -ln(1 - shape) per pass length
};
// With a paintRamp, `cosine` is how far the pixel lies along the stamp's heading rather than across it, 0 to 1: a stamp moving by a step moves
// its edge out only that much of a step at any angle, so the ramp is that much narrower, and to the side of the stroke there's none.
StampMark stampMark(const Stamp& stamp, int edge, float distance, float cosine = 1.0f);

// A mask's share of the way, in whole numbers, 0 to kMaskOne (2^kMaskBits): the direct lay's and the Marker's event mask (stage 3)
// both lay through it. 15 bits, so a 16-bit channel's move times a share fits 32 bits.
inline constexpr int kMaskBits = 15;
inline constexpr std::int32_t kMaskOne = std::int32_t{1} << kMaskBits;
// A coverage from 0 to 1 as a mask share, to the nearest whole number.
inline std::int32_t maskOf(float coverage) {
    return static_cast<std::int32_t>(std::clamp(roundToNearest(coverage * static_cast<float>(kMaskOne)), 0L, static_cast<long>(kMaskOne)));
}
// How much of a stamp's edge at (dx, dy) from its centre lies along its heading: the cosine stampMark asks for.
inline float headingCosine(const Stamp& stamp, float dx, float dy, float distance) {
    return stamp.paintRamp > 0.0f && distance > 0.0f ? std::abs(dx * stamp.headingX + dy * stamp.headingY) / distance : 1.0f;
}

// With a chordRadius, how much more a stamp lays at (dx, dy) from its centre so that a stroke's rows all get the same: a row near the side
// is crossed by less of the stamp, so it lays that much more. With evenFrom, only the rows past it, each as much as the row there gets,
// so the stroke's fade across stays as it is up to there and holds from there to its side.
inline float acrossFactor(const Stamp& stamp, float dx, float dy) {
    if (stamp.chordRadius <= 0.0f)
        return 1.0f;
    const float across = (dx * stamp.headingY - dy * stamp.headingX) / stamp.chordRadius;
    if (stamp.evenFrom <= 0.0f)
        return 1.0f / std::sqrt(std::max(1.0f - across * across, stamp.leastChord * stamp.leastChord));
    if (std::abs(across) <= stamp.evenFrom)
        return 1.0f;
    return std::max(std::sqrt(1.0f - stamp.evenFrom * stamp.evenFrom) /
                        std::sqrt(std::max(1.0f - across * across, stamp.leastChord * stamp.leastChord)),
                    1.0f);
}

// Whether a stamp lays as much across the whole of a rect as without evening: no evening, or only past the rect's farthest row across.
inline bool unevenedOver(const Stamp& stamp, float dxA, float dxB, float dyA, float dyB) {
    if (stamp.chordRadius <= 0.0f)
        return true;
    if (stamp.evenFrom <= 0.0f)
        return false;
    float farthest = 0.0f;  // across is straight in dx and dy, so a rect's corners hold its most
    for (const float dx : {dxA, dxB})
        for (const float dy : {dyA, dyB})
            farthest = std::max(farthest, std::abs(dx * stamp.headingY - dy * stamp.headingX));
    return farthest <= stamp.evenFrom * stamp.chordRadius;
}

// How far from its centre a stamp lays anything: its radius and its outline's blur or ramp past it.
float outerOf(const Stamp& stamp);
// A bristle map's stamp's outline at (dx, dy) from its centre: the most of its hairs', each an antialiased disc times its fade.
float hairCoverage(const Stamp& stamp, float dx, float dy);
// The share of the pixel a stamp's outline covers `distance` px from its centre: the shape stampMark gives.
float outlineShareOf(const Stamp& stamp, float distance);
// With a skirt, the share of its density a stamp lays `outside` px past its radius, where its outline is half: 1 there, falling to 0
// at its reach.
float skirtShare(const Stamp& stamp, float outside);
// How far from its centre a stamp's skirt reaches, or its outline's outer end without one.
float reachOf(const Stamp& stamp);

// The two together, 0 to 1, as one stamp alone shows.
float stampCoverage(const Stamp& stamp, int edge, float distance);

// How many stamps on each side limit a soft stamp, held back until known. 1 and up paint alike: 1 and 3 paint every wide case
// as 2 to the bit. 0 moves the Marker by up to 48 levels of 255 and Water Color's Shapes 1 to 4 by up to 219.
inline constexpr int kStampNeighbours = 2;

// A soft stamp's paint is limited to the most any of its neighbours, `stamps[i]`'s within kStampNeighbours on each side, would give at
// the same distance from their own centres. `distance` is the pixel's from stamps[i]'s centre.
StampMark limitedMark(const std::deque<Stamp>& stamps, std::size_t i, int edge, float distance, float cosine = 1.0f);

// How far from stamps[i]'s centre limitedMark gives it the same mark at every pixel, whatever the edge, and that mark: paint 1, and
// shape 1, or for a blurred outline the blur's own most, where it's past kNormalReach deviations in. Below 0, nowhere.
float deepReach(const std::deque<Stamp>& stamps, std::size_t i, StampMark& mark);
// Kept inside a deep reach, so rounding can never take a pixel past where its mark is the same. More than a float's rounding of
// a place within 4000 px, which is half a thousandth.
inline constexpr float kDeepMargin = 1.0f / 1024.0f;  // px

// The opacity at which a long straight line of these stamps, `spacing` apart, reaches `target` coverage along its centre, or the
// density that counts as solid if that's less. With `skirted`, each stamp lays a skirt as wide as the spacing says, and it counts too.
double opacityForLine(double radius, double band, int edge, double spacing, double target, double edgeBlur = 0.0, double rampWidth = 1.0,
                      bool easedRamp = false, bool ramped = false, bool skirted = false, int profile = -1);

// What one stamp adds to a pixel's density, which adds up the way layers of paint do: 1 - coverage multiplies.
float densityOf(float opacity, float paint);
// A stamp of a shape laid as one stroke (StampForm::oneStroke): its outline, the most coverage the stroke reaches where it lays, is
// its opacity over the tool's full point (StampTip::pictureFullAt), the tool's Density times the pressure's share of it, times Dens.
// (`strength`). Over that outline it lays the density a stamp at full opacity and Dens. 100 lays over the whole, by the picture's
// lightness `light`: so one stamp fills its outline as one at Dens. 100 fills the whole, close stamps build no further, and at Dens. 100
// and full pressure it's the stamp as it always was.
float oneStrokeOutline(float opacity, double fullAt, float strength);
float oneStrokeDensity(double fullAt, float light);

// The reference's round dab on the 1st to 4th Shapes: its coverage `along` its nominal radius from the centre, 1 at
// the centre, and how far it reaches, in nominal radii.
float profilePaint(int profile, float along);
float profileReach(int profile);
float profileCore(int profile);
// The share of a line's density that a line of the reference's dabs lays against the Pencil's line at the same Density and pressure:
// the square root of the profile's line integral over the radius (the reference's lines at Density 40 % and at half pen
// pressure both give it, to within 0.007).
float profileLineShare(int profile);

// How far apart stamps go: a share of each stamp's own diameter, `share` of it, at least a quarter of a pixel (kLeastSpacing), or
// `least` above 0: a tip's own leastSpacing, the Pencil's.
double stampSpacing(double radius, double share = 0.05, double least = -1.0);
// Over how many px a stamp that stands for `step` px of path lets its paint fall away at its outer end.
float paintRampFor(double step);

struct StampTip {
    double radius = 10.0;  // px, at full size
    double density = 1.0;  // the coverage a long line reaches along its centre, 0 to 1, before the merge counts part of it as full
    int edge = 0;
    double rimShare = 0.0;  // how much of the radius is soft rim: 1 - Sharpness
    double spacingShare = 0.05;  // of each stamp's diameter
    double mostSpacing = 1e9;  // px: stamps never go further apart than this, so a wide stamp's fade isn't a few steps
    double fadeStep = 0.0;  // above 0, stamps go closer where their opacity changes by more than this from one to the next
    bool eachStampAlone = false;  // a stamp's opacity is the target itself, for the Marker, whose Density is a ceiling
    bool evenAcross = false;  // what a stamp lays is evened out across the stroke's width, so its side stays as full as its middle
    // Above 0, what a stamp lays is evened out across only past this share of its radius from its middle, so the stroke's side holds the
    // tone it has there out to evenSideTo px from the edge, and drops there, at any size.
    double evenSideFrom = 0.0;
    double evenSideTo = 1.0;  // px
    // Above 0, the paint gathers at each pen sample: a stamp a share u of its diameter along the path from the nearest sample lays
    // 1 / (1 + u / gatherWidth) of its opacity, so samples close for the size change little and far ones leave a lighter line, beaded
    // at each sample.
    double gatherWidth = 0.0;
    // Above 0, a stroke's first stamps are fuller, so it starts at its tone rather than coming in pale while too few stamps overlap there:
    // a stamp a share u of startReach diameters along the path lays 1 - (1 - opacity)^(1 + startFuller (1 - u)).
    double startFuller = 0.0;
    double startReach = 1.0;  // stamp diameters
    // Above 0, a stamp is whole to its outline, its paint falling away nowhere inside it, and past its outline it lays a skirt, shown
    // only inside the outlines of the stamps this many before and after it on the path; a line is solved with the skirt in.
    int skirtNeighbours = 0;
    double mostOpacity = 1.0;  // a stamp never lays more than this, as at full pressure and Density
    int brushDabRow = -1;
    // Water Color laid in groups: its stamp is the Brush's at full pressure times this, and times pressure's share of Density, not
    // solved for it (BrushDab.h, kWaterStampShare).
    bool brushDabLinear = false;
    double brushDabScale =
        1.0;  // at 0 and above, the Brush laid in groups: each stamp's opacity solved on this row of the reference's dab (BrushDab.h)
    bool penLaysFirstTwice = false;  // a pen's stroke lays its first stamp twice, a mouse's once
    // Above 0, a stamp's opacity isn't solved for pressure's share of Density: it's the whole stamp's, times its firmness from firmFrom
    // to this power, or the solved one times what's left of the firmness if that's more.
    double firmPower = 0.0;
    double firmFrom = 0.0;  // the density share below which a stamp lays nothing: Blur Pressure
    double fuller = 1.0;  // a stamp's opacity is raised to 1 - (1 - opacity)^fuller: above 1, fuller than the Pencil's
    double lightFuller =
        -1.0;  // above 0, fuller is this at light pressure, straight up to fuller over kLightFullerShares of the density share
    bool followsCurve = false;  // stamps go along a curve through the samples, not straight lines between them
    bool insetOutline = false;  // the outline sits a little inside the radius, by a table of radius in StampTip.cpp
    double rimReach = 1e9;  // px: the soft rim grows with the radius only up to this radius
    double rimHardensFrom = -1.0;  // pressure: from here to rimHardensTo, rimReach comes in from none; below 0, it always applies
    double rimHardensTo = 1.0;
    double edgeBlur = 0.0;  // see Stamp
    double rampWidth = 1.0;  // see Stamp
    // A soft Shape: the outline is blurred by this share of the radius more, and sits softInset of the radius further inside it, so
    // the edge fades over a share of the width. Without edgeBlur, a straight ramp as wide, 10 % to 90 %, stands in for the
    // blur. edgeShift moves the outline out by this many px, for Sharpness.
    double softShare = 0.0;
    double softInset = 0.0;
    double edgeShift = 0.0;
    // A soft Shape's stamps go as far apart as the radius before softInset says, so it lays less in its middle: the Brush's.
    bool softSpacedWide = false;
    double thinnestRadius = 0.5;  // px: a thinner stamp is drawn this wide and fades instead
    // At 0 and above, its stamps are the reference's dab on that Shape (profilePaint): their paint falls as that dab's coverage does, the
    // outline whole to its reach, with no inset or soft blur.
    int profile = -1;
    // Off (Disable Density Control Assist): a stamp's opacity isn't solved for a line's tone; it's its full-pressure strength times its
    // share of Density, and a line builds up past it.
    bool densityAssist = true;
    double leastSpacing = -1.0;  // above 0, px: the closest this tip's stamps go, in place of kLeastSpacing: the Pencil's
    double closestStep = 0.5;  // px: with fadeStep, stamps put in between never go closer than this
    // A tap leaves two stamps, at the stroke's first move and where the pen landed at the most pressure it reached, each this share
    // of the line's coverage, less on a wide tap: times (tapFullRadius / r)^tapFalloff past tapFullRadius. At 0 it's one stamp,
    // solved as a line's.
    double tapShare = 0.0;
    double tapFullRadius = 1e9;
    double tapFalloff = 0.0;
    bool tapIsWalked =
        false;  // a tap lays only the stamps its pen's jitter walked, as any stroke does, so a still pen lays next to nothing
    // Above 0, a click, a pen that never moved at all, lays one stamp where it landed, at the most pressure it reached, clickShare times
    // the line's stamp's opacity to clickPower; a tap that wobbles still lays what tapIsWalked or tapShare say.
    double clickShare = 0.0;
    double clickPower = 1.0;
    // A plain tool's stamps laid on a grid of points from kGridTwoFrom (onGrid), its cells judged by what they show, which needs
    // the merge's full point.
    bool onGrid = false;
    float fullAt = 1.0f;
    // A hard disc, the Pixel Art Pen's: each stamp lays the whole of every pixel whose centre is within its radius, and of the
    // pixel its centre is in, and nothing anywhere else, solid, with no outline, rim or ramp, whatever its opacity's solve would say.
    bool hard = false;
    // A form: each stamp's outline is the round one's and the picture's, Strength of the way to the picture, and so is its
    // paint, Strength of the way to all of it inside the picture's reach. Not on the grid, and with no skirt: the picture is the outline.
    std::shared_ptr<const StampForm> form{};
    // A brush shape's stamps, as the reference lays them: each the picture whole at full opacity, laid over what's there, turned by
    // pictureAngle (radians), and by the way the stroke goes with pictureAlongStroke.
    bool pictureStamps = false;
    double pictureAngle = 0.0;
    bool pictureAlongStroke = false;
    // With jitters, each stamp at random: turned up to pictureAngleJitter radians either way, its size up to pictureSizeJitter
    // of it larger or smaller, and moved up to pictureScatter of its size across and along the stroke. Its opacity is the tip's density
    // times the pressure's share of it.
    double pictureAngleJitter = 0.0;
    double pictureSizeJitter = 0.0;
    double pictureScatter = 0.0;
    // The tool's full point (fullAtFrom), which the tip's density, and so a stamp's opacity, is times: a shape laid as one stroke counts
    // its outline against it (oneStrokeOutline), so Density 100 at full pressure is an outline of exactly 1.
    double pictureFullAt = 1.0;
    // With a profile: the paper cut into each stamp's coverage as it's laid, the reference's way: a stamp loses the
    // paper's darkness at the pixel times Intensity (its centre being whole), down to nothing, before its opacity. The merge adds none.
    std::shared_ptr<const class PaperGrain> paperPerStamp{};
    // Each pen sample's stamps go straight onto a copy of the layer as one mask, in whole numbers, a tile every stamp covers whole moved
    // by one factor (StampLayer::layDirectOnto).
    bool layDirect = false;
    double solidFrom = 0.0;  // above 0, px: a hard stamp this wide across its radius or wider is solid to it with a pixel's ramp
    // A bristle map: each stamp's outline is these hairs, discs hairRadius px across their radius, at their places in the map
    // bristleSpread px (half the map's span at full size) times the size's share out from the stamp's centre, turned with the way the
    // stroke goes at each pen sample. The tip's own form is then a stand-in at Strength 1, whose picture the hairs are.
    std::shared_ptr<const Bristles> bristles{};
    double bristleSpread = 0.0;
    double hairRadius = 0.0;
    // Hair Min: with hairFollowsPressure, Size on pressure, a hair's radius is hairRadius times the size's pressure response
    // (hairSensitivity, Size's sensitivity) from hairLeast at no pressure to 1 at full, as Min Size is the size's.
    double hairLeast = 1.0;
    bool hairFollowsPressure = false;
    int hairSensitivity = 100;
    // Direction: which way the map's top points. AlongStroke, Auto, follows the way the stroke goes; Fixed, None,
    // lies as for a stroke going right; WithPen, Pen Direction, points the way the pen leans, as Fixed without a lean.
    enum class HairWay {
        Fixed,
        AlongStroke,
        WithPen
    };
    HairWay hairWay = HairWay::AlongStroke;
    // Randomize, 0 to 1: how far each hair wanders about its place, and how far it lags the map's turn, at the most.
    double hairWander = 0.0;
    // Detect Turn: along the stroke, a turn much sharper than the stroke has been taking starts the hairs afresh.
    bool hairDetectTurn = false;
};

// Places a stroke's stamps along straight lines between the samples. Each stamp goes its own spacing past the one before, and what's
// left over at the end of a line carries on into the next. The line to a sample is only walked once the next one comes, since a
// sample at the same place, such as pressure 0 when the pen lifts, still changes it. Nothing is handed out until the pen has gone
// past a tap, and a stroke that never does is exactly one stamp.
class StampWalk {
public:
    explicit StampWalk(StampTip tip) : m_tip(std::move(tip)) {}

    void begin(const InputSample& first, PressureShares shares);
    // The stamps this sample lets it place.
    std::vector<Stamp> add(const InputSample& sample, PressureShares shares);
    // The rest when the pen lifts.
    std::vector<Stamp> end();
    std::vector<Stamp> endStamps();

private:
    struct Place {
        QPointF pos;
        PressureShares shares;
        double widthShare = 1.0;
        double pressure = 1.0;
        QPointF lean{};  // the pen's tilt, degrees to the right and down the canvas (InputSample::tiltX, tiltY)
    };

    // A stamp, with the numbers the walk itself goes on from: its spacing adds up along the stroke and its fades count their steps
    // from these, so stamps land where they did in doubles.
    struct Walked {
        Stamp stamp;
        double radius = 0.5;
        double opacity = 1.0;
    };
    Walked stampAt(const Place& place);
    double radiusOf(double share) const;
    struct Outline {
        double blur = 0.0;
        double ramp = 1.0;
        bool eased = false;
    };
    Outline outlineOf(double radius) const;
    double bandOf(double radius, double pressure = 1.0) const;
    double opacityAt(const Place& place, double drawn);
    double opacityFor(double radius, double target);
    double fullerAt(double share) const;
    void walkTo(const Place& to, const Place* after);
    std::vector<Place> curveTo(const Place& from, const Place& to, const Place* after) const;
    std::vector<Stamp> tapStamps();
    Stamp clickStamp();
    // A brush shape's stamp (StampTip::pictureStamps): whole, with nothing past its picture, turned by the tip's angle and, along the
    // stroke, by the way (headingX, headingY) it goes.
    void asPicture(Stamp& stamp, double headingX, double headingY, double densityShare);
    double nextJitter();  // the stroke's next random number, 0 to 1
    static double nextFrom(std::uint64_t& state);  // splitmix64's next number from `state`, 0 to 1
    double tapOpacity(double radius, double densityShare) const;
    Place placeAlong(const Place& from, const Place& to, double t, QPointF pos) const;
    void walkLine(const Place& from, const Place& to);
    int fillIn(const Place& here, double opacity);
    std::vector<Stamp> handOut();
    // With bristles, the hairs are each stamp's outline: each hair's run from its place at the last stamp to its place at this
    // one, the map turned at each stamp.
    struct HairTrack {
        double turn = 0.0;  // radians: which way the map's top points, east 0, clockwise on the canvas; never wrapped, so it turns smoothly
        QPointF place;  // on the canvas, at the last stamp
        QPointF wander;  // its wander about its place in the map, at the last stamp, in halves of the map's span
        double lag = 0.0;  // its own share more of the follow length
        std::array<QPointF, 2> knots;  // its wander's two random knots either side of the stroke's wander so far
    };
    void beginHairs(const InputSample& first);
    void withHairs(Stamp& stamp, QPointF heading, const Place& here, double densityShare);
    QPointF hairAt(std::size_t i, QPointF centre, double turn, double spread, QPointF wander) const;
    QPointF wanderOf(std::size_t i) const;
    void advanceWander(double by);
    double hairToneExponent() const;
    std::vector<HairTrack> m_hairs;
    bool m_hairsBegun = false;  // the hairs have a place at a stamp
    QPointF m_hairCentre;  // the last stamp's centre, its spread and the stroke's way there
    double m_hairSpread = 0.0;
    double m_hairHeading = 0.0;
    std::deque<std::pair<double, double>> m_hairWays;  // Detect Turn: the stroke's way at each stamp lately, by path walked
    double m_hairPath = 0.0;  // px of path the hairs have walked
    bool m_hairCut = false;  // Detect Turn: cut off at a corner, the way not settled yet
    double m_hairNextHeading = 0.0;  // radians: the way from the sample being walked to to the one after
    double m_hairWandered = 0.0;  // px of the wander's path so far
    std::int64_t m_hairKnot = 0;  // which knot spacing that's in
    std::uint64_t m_wanderState = 0;  // the wander's random state, from the stroke's first sample

    StampTip m_tip;
    std::uint64_t m_jitter = 0;  // the jitters' random state, set from the stroke's first sample
    std::optional<Place> m_before;  // the sample before m_last, for the curve
    Place m_last;
    std::optional<Place> m_waiting;  // the newest sample, not walked to yet
    Place m_landing;
    std::optional<Place> m_firstMove;
    PressureShares m_peak;
    double m_peakPressure = 0.0;
    double m_untilNext = 0.0;  // how far along the next line its first stamp goes
    double m_travelled = 0.0;
    double m_sampleLength = 0.0;  // the path from the last sample to the next, as walkTo lays it
    double m_sampleWalked = 0.0;  // how much of it the lines laid so far cover
    std::optional<Place> m_lastStamp;  // where the last stamp of a walked line went, for fadeStep
    bool m_firstTwice = false;  // with penLaysFirstTwice, a pen's stroke, whose first walked stamp isn't laid yet
    std::vector<Stamp> m_placed;  // not handed out yet
    std::unordered_map<std::uint64_t, double> m_opacities;  // solved already, by rounded radius and target
};

// Lays a stroke's stamps into its buffer. A soft stamp is held back until the stamps after it are known, because each one is
// limited by its neighbours'.
class StampLayer {
public:
    StampLayer(const StampTip& tip, bool onWorkers);

    void begin(const InputSample& first, PressureShares shares);
    void add(const InputSample& sample, PressureShares shares);
    // Lays the stamps whose neighbours are known. With `ending`, all of them.
    void lay(StrokeBuffer& buffer, bool ending);
    QRect takeDirty();
    std::vector<TileCoord> takeChangedTiles();
    // With StampTip::layDirect, the layer as the stroke began and its paint: the stamps are laid on a copy of it, sample by sample.
    void layDirectOnto(const class TiledImage& before, const struct StrokePaint& paint);
    bool laysDirect() const { return m_direct != nullptr; }
    // A tile the stroke has painted directly, or none.
    std::shared_ptr<const struct Tile> paintedTile(TileCoord coord) const;

private:
    struct Job;
    struct Direct;
    void layDirectBatch(std::size_t first, std::size_t end);

    std::vector<std::size_t> readyStamps(bool ending) const;
    void layStamps(const std::vector<std::size_t>& ready, StrokeBuffer& buffer);
    void layInOneTile(Job& job, const std::vector<std::size_t>& ready) const;
    void layStampInTile(Job& job, std::size_t i) const;
    // A paper-cut stamp (StampTip::paperPerStamp) at eight pixels of a row, `darks` the tile's paper; says whether
    // any took paint.
    bool paperEight(Job& job, std::size_t i, int x, int y, const float* darks) const;
    void layPixel(Job& job, std::size_t i, int x, int y, bool cutWhole = false) const;
    bool skirtEight(Job& job, std::size_t i, int x, int y) const;
    bool skirtEightAvx2(Job& job, std::size_t i, int x, int y) const;
    // What stamp i adds at a pixel: its density and the share of the pixel it covers, and with a skirt what the skirt adds besides, which
    // the record takes as it is; false where it lays nothing.
    bool markOf(std::size_t i, int x, int y, float& density, float& shape, float& skirt, bool cutWhole = false) const;
    bool plainMarkOf(std::size_t i, int x, int y, float& density, float& shape, float& skirt, bool cutWhole = false) const;
    float skirtOf(std::size_t i, int x, int y, float distance, float& raised, bool cutWhole = false) const;
    bool formedMarkOf(std::size_t i, float dx, float dy, float distanceSquared, float& density, float& shape) const;
    void layGridInTile(Job& job, const std::vector<std::size_t>& ready) const;
    void layGridGroup(Job& job, const std::vector<std::size_t>& ready) const;
    int m_gridCell = 0;  // px between points, 0 for the per-pixel model
    void forgetOldStamps();

    StampTip m_tip;
    bool m_onWorkers;
    StampWalk m_walk;
    std::deque<Stamp> m_stamps;  // the last few laid, as neighbours, then those not laid yet
    // For each stamp being laid, by its place in m_stamps: within reachSquared of its centre its mark is the same, and so is what it
    // adds to a pixel's density, worked out once.
    struct Deep {
        PixelFrame frame;
        float reachSquared = -1.0f;
        float outsideSquared = 0.0f;  // and from this far, squared, it lays nothing
        float density = 0.0f;
        float shape = 0.0f;
        float skirtSquared = 0.0f;  // with a skirt, within this squared it may lay one
        int left = 0, top = 0, right = 0, bottom = 0;  // boundsOf, worked out once a stamp, not once a tile
    };
    std::vector<Deep> m_deep;
    std::vector<PixelFrame> m_frames;  // with a skirt, every kept stamp's, by its place in m_stamps
    std::size_t m_nextToLay = 0;
    QRect m_dirty;
    std::vector<TileCoord> m_changed;
    std::deque<std::uint32_t> m_stampSample;  // which pen sample each stamp in m_stamps came from
    std::uint32_t m_sample = 0;
    std::shared_ptr<Direct> m_direct;
};

}
