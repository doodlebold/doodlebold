#include "core/stroke/RebuiltStroke.h"

#include "core/brush/BrushResources.h"
#include "core/stroke/Bristles.h"
#include "core/util/WorkerPool.h"

#include <QPointer>

#include <algorithm>
#include <numbers>
#include <array>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <memory>
#include <system_error>
#include <utility>
#include <vector>

// docs/brush-engine/rebuilt-stroke.md says what the named values here are for and how they were chosen.
namespace doodlebold {

namespace {

// Every `/ 100.0` below turns a panel setting's percent into a share, and every `/ 255.0` an 8-bit value into one.
// Sharpness's range: the full point F = 1 - exp(-kSoftDensity · (kHardDensity / kSoftDensity)^s), a "really soft" at 0 % with
// the centre still solid.
constexpr double kSoftDensity =
    1.5;  // the density that counts as full at Sharpness 0. Only the Eraser's Shapes 1 to 4 show it, through kEraserHardness: 0.95 to 1.9 paint them alike
constexpr double kHardDensity = 12.0;  // and at Sharpness 100, where 1 - exp(-12) rounds to 1 in 16 bits
// The Pencil's outline, fitted to the reference's: a box of the radius blurred by this much, px, so a thin
// line greys as it thins, a stamp this thin, px, before it fades too, and the radius past which its rim stops growing. It also sits
// a little inside the radius: StampTip.cpp has the table.
constexpr double kPencilBlur = 0.45;  // at Sharpness 50 %
// At Sharpness 0 % and 100 % the blur is these, px, and the outline moves out by kSharpEdgeShift at 100 %, in by it at 0 %, straight
// between: fitted to the reference's 5th Shape at each Sharpness.
constexpr double kSoftestPencilBlur = 0.6;
constexpr double kSharpestPencilBlur = 0.2;
constexpr double kSharpEdgeShift = 0.15;
// How much a soft Shape blurs the outline, and how far it sits inside the radius, both as shares of the radius, from the 1st button
// to the 5th. Fitted to the reference's Shapes: the 1st fades from 10 % to 90 % over about 30 % of the width.
constexpr std::array<double, 5> kShapeSoftness{0.235, 0.1275, 0.0775, 0.04, 0.0};
constexpr std::array<double, 5> kShapeInset{0.23, 0.12, 0.07, 0.03, 0.0};
constexpr int kSoftShapes = 3;
// The 1st to 4th Shapes' blend through the paper, from the hard edge's thinning to the soft edge's tops, and the
// bar for its hard part, times the soft bar.
constexpr std::array<float, 5> kSoftEdgeBlend{1.0f, 0.4f, 0.25f, 0.1f, 0.0f};
constexpr std::array<float, 5> kSoftEdgeHardDepth{1.0f, 2.0f, 1.5f, 1.0f, 1.0f};  // the 1st to the 3rd
// On the soft Shapes a stroke's first stamps are fuller, StampTip::startFuller, over kBrushStartReach of their diameter, so the start shows
// at its tone from the first frame rather than coming in pale while too few stamps overlap it. Fitted to the reference's start frames on
// the donut, the blob's width against its darkest point, at Shapes 1 to 3.
constexpr double kBrushStartFuller = 0.75;
constexpr double kBrushStartReach = 0.75;  // stamp diameters
constexpr double kPencilThinnest = 0.1;
// px: the closest the Pencil's tip's stamps go (the Pencil, Eraser and Clone Stamp), in place of StampTip's kLeastSpacing, which every
// other tip keeps ("Pencil family only"). Fitted to the reference's thin Pencil lines (reading 3) and the Pencil's
// mocks: a 1 px line carries 95 % of the reference's ink (81 % at 0.25), and below 0.1 almost nothing changes.
constexpr double kPencilLeastSpacing = 0.1;
constexpr double kPencilRimReach = 2.0;
// The Brush's brush colour comes in as the pressure past Blur Pressure to this power, and it keeps Persistence of what it carries over
// this many stamps, not 4, fitted to the reference's. While pressure falls, the most a pixel's paint counts as
// holding comes down toward kBrushFallMemory, from kBrushMixMemory.
constexpr double kBrushFadePower = 1.25;
constexpr double kBrushSettledPower = 2.0;  // round 1's power, whose share at pressure 0.8 the cross settled right with
constexpr double kBrushPersistenceStamps = 2.0;
// Soft edges from parts of a stroke this many outer radii of path apart add up, as passes do, so a loop's middle fills and where a
// stroke runs over itself its soft edge is fuller; a straight pass keeps the most of its stamps' edges.
constexpr double kSoftPassLength = 0.75;
constexpr double kBrushFallMemory = 1.0;
// The most a pixel's paint counts as holding when a Brush dab mixes in, in place of kMixMemory ("Brush only"): the best
// single value over the Brush's mocks (2.33 levels off, 2.75 at 3) and the reference's drags of readings 4 and C.
constexpr double kBrushMixMemory = 2.0;
// Fitted to the reference's ramp over a field, cross, drag-out and ramp: at full pressure the Brush keeps
// Persistence over this share of those stamps, and its outline's straight ramp is this wide, px. The share past Blur Pressure where
// the cross settles, at pressure 0.8, is what the candidates keep.
constexpr double kBrushFirmLetGo = 0.3;
constexpr double kBrushRampWidth = 1.6;
// The Brush's and the Pencil's sides: what a stamp lays is evened across the stroke past these shares of its radius from its middle, so
// the side holds the tone it has there, and drops over kEvenSideTo px at its edge at any size. Fitted to the reference's
// columns across the side at 200 and 1000 px.
constexpr double kBrushEvenSideFrom = 0.955;
constexpr double kPencilEvenSideFrom = 0.88;
constexpr double kEvenSideTo = 0.5;  // px
// Water Color's paint gathers at each pen sample (StampTip::gatherWidth): samples far apart for its size leave a lighter line with a
// bead at each, as the reference's, and close ones change little. Fitted to its ramp strokes at 50 and 200 px, slow and fast.
constexpr double kWaterGatherWidth = 0.12;  // of the stamp's diameter
// At full pressure, where the gather can't lighten paint that already covers, its paint is thinner where few pen samples fall within
// its width, as the reference's is: from kWaterDilutionBySamplesFrom pressure its Dilution comes to be times the figure at that many
// samples here. Fitted to the reference's full-pressure lines at 1000 px, samples 50 to 1000 px apart.
constexpr double kWaterDilutionBySamplesFrom = 0.85;  // pressure
constexpr std::array<TipMixing::SamplesDilution, 2> kWaterDilutionsBySamples{{{4.0, 1.45}, {8.0, 1.0}}};  // {samples, times}
// The Brush's share past Blur Pressure, at pressure 0.8, where the cross settles: fitted to the reference's cross with
// kBrushSettledPower.
constexpr double kBrushSettledShare = 0.6;
// A Brush click that never moves, the mouse's or the pen's, leaves one faint stamp where it landed, kBrushClickShare times its line's stamp's opacity to
// kBrushClickPower: fitted to the reference's clicks at 1000 px, the mouse's 74 and the pen's at 0.5 18.
constexpr double kBrushClickShare = 0.53;
constexpr double kBrushClickPower = 0.8;
// The Brush's 5th Shape: a full stamp, whole to its outline, with a skirt past it shown only inside its neighbours'.
constexpr double kBrushFullStamp = 18964.0 / 65535.0;  // its opacity: the reference's click, alpha in 16 bits
constexpr int kBrushSkirtNeighbours = 3;
// The same click for the Pencil, Water Color and the Marker, each fitted to the reference's mouse click and pen tap at 0.5 at 1000 px:
// the Pencil's 74 and 8, Water Color's 58 (its pen tap, at Blur Pressure, lays
// nothing anyway), the Marker's 255 and 32. A Marker stamp of opacity 1 alone lays 247, so its share goes past 1.
constexpr double kPencilClickShare = 0.558;
// Scratch Prs.'s bar at its lightest touch, as kScratchDepth times the control to kScratchPower (StrokeMerge.cpp, kScratchWidth).
constexpr double kScratchDepth = 1.2;
constexpr double kScratchPower = 0.4;
// The Pencil's dab on the 1st to 4th Shapes, the reference's: its strength at full pressure and Density, and its
// spacing in nominal diameters.
constexpr std::array<double, 4> kPencilDabStrength{243.4 / 255.0, 144.1 / 255.0, 88.6 / 255.0, 56.2 / 255.0};
constexpr std::array<double, 4> kPencilDabSpacing{0.185, 0.09, 0.053, 0.0325};
constexpr double kPencilClickPower = 0.864;
// The Eraser's, fitted the same way to the reference's on paint: it takes away 74 for the mouse and 8 for
// the pen, as the Pencil lays. The Clone Stamp's is the Pencil's, as it starts like the Pencil.
constexpr double kEraserClickShare = 0.87;
constexpr double kEraserClickPower = 1.0;
constexpr double kWaterClickShare = 0.34;  // Water Color's click, fitted with the Pencil's above
// Water Color's 5th Shape (task 57): each stamp takes a pixel this share of the way toward the colour and opacity the brush carries,
// at full pressure, as its most opacity: the reference's press point at Dilution 0 reads 102, 164, 200, 222, 235, 243, 248 of 255,
// each 0.402 of what was left.
constexpr double kWaterDepositShare = 0.402;
// And what it carries has what's missing of the opacity under it raised by (1 - Dilution) to this power: 1, 0.565, 0.231 and 0 at Dilution
// 0, 30, 60 and 100, the reference's clicks 102.5, 57.9, 23.7 and 0 over 0.402 of 255 (30 and 60 each give 1.60).
constexpr double kWaterDilutionPower = 1.6;
// What it carries follows what it picks up at Persistence to the power of the stamps over this many: fitted to the press point's second
// step at Persistence 50, 58 to 94 of 255, so it keeps 0.83 of what it carried a stamp; at 0 it follows at once and at 100 never, as the
// reference's.
constexpr double kWaterPersistenceStamps = 3.7;
// And it picks up from this far out, in the stamp's radii, so it takes in the empty canvas beside the line as the reference's does: fitted
// to the long line's middle at Dilution 30, 221.7 of 255 (222.2), with Dilution 60's 147.3 (146.9) and Persistence 0's 222.7 (223.8)
// following. At the Brush's 1.02 the middle was 236, at 1.1 229.9.
constexpr double kWaterPickUpReach = 1.2;
// Under the pen, its stamps' share is the whole of kWaterDepositShare times the pressure's firmness, from Blur Pressure, to this power,
// in place of the share a line solves for: fitted to the reference's pen line at 0.6, 81 of 255 at its middle (83; 75 at 1.4), and its tap
// at 0.6 follows, 7.0 (7.1), and the pen circle at 0.6, 145 at its centre falling to 72 near its edge (144 to 74).
constexpr double kWaterFirmPower = 1.3;
// On the 5th Shape what's under melts toward its blur by this times the dab's coverage, in place of kWaterMelt: fitted to the reference's
// pen at 0.3 across the edge of black paint, which it softens into a slope 101 px wide from 90 to 10 % (105; 84 at kWaterMelt's 5, 129 at
// 12), the same as over red and blue (102).
constexpr double kWaterFirmMelt = 8.0;
// Below full pressure Water Color's melt is times (1 - firmness) to this power.
constexpr double kWaterMeltFirmPower = 2.5;
// Each stamp picks up where the pen is, but no further ahead than this many of its diameters.
constexpr double kWaterPenReach = 0.3;
// Its outline sits this far in from the radius, px, and falls over a straight ramp this wide, px, in place of kBrushRampWidth: fitted to
// the reference's clicks, whose paint makes a disc of radius 9.33, 24.18, 49.18 and 299.17 px at 20, 50, 100 and 600 px (9.26, 24.25,
// 49.25, 299.25; 0.64 to 0.83 px wider with neither), and whose rim falls 56.8, 12.0, 0.2 at 50 px (57.9, 12.3, 0).
constexpr double kWaterFirmEdgeShift = -0.7;
constexpr double kWaterFirmRampWidth = 1.0;
// Under the pen its brush colour comes in as the firmness to this power, slower than kWaterFadePower: fitted to the reference's pen line
// over red and blue at 0.8, its middle 102 of 255 in red at x 1350 (101; 92 at 1.0, 107 at 1.5), with 0.6 following.
constexpr double kWaterFirmFadePower = 1.3;
// Over paint the brush colour's share of a dab is (1 - Blending) to this power: the reference's click over full paint moves each pixel
// 0.402 of the way toward the paint mixed with the colour under it, the paint's share 1.000, 0.396, 0.155 and 0 at Blending 0, 30, 52
// and 100 (30 gives 2.60, 52 2.54).
constexpr double kWaterBlendingPower = 2.6;
// Water Color, fitted to the reference's: the blur it melts toward, in its stamps' radii, how fast, by coverage,
// its stamps' fullness, the power its brush colour comes in at above Blur Pressure and the most of it, and over which pressures its
// edge hardens from soft to the Brush's.
constexpr double kWaterMeltRadii = 0.5;
constexpr double kWaterMelt = 5.0;
constexpr double kWaterFuller = 2.6;
constexpr double kWaterLightFuller = 1.4;  // and at light pressure, so its tone rises as steeply as SAI's from 0.7 to 0.9
constexpr double kWaterFadePower = 0.5;
constexpr double kWaterFadeMost = 0.75;
constexpr double kWaterHardensFrom = 0.5;
constexpr double kWaterHardensTo = 0.8;
constexpr double kWaterHold =
    0.0;  // none: over paint it lays as much brush colour as the Brush does. With today's kWaterBlendingPower 0 fits the water mocks best and keeps the wash check; 0.7 with kWaterBlendingPower 1.5 fits the reference's clicks better, still to be decided
// Over paint thinner than what it carries, its persistence stamps count (1 - (carried - picked-up opacity))^this, so on empty
// paper the colour it carries thins out gradually: fitted to the reference's drag-out.
constexpr double kWaterThinKeep = 4.0;
// The Marker: its colour's share by its dabs' ceiling, how that share lags, its Persistence and its pickup.
constexpr double kMarkerShareByCeiling = 1.25;
constexpr double kMarkerShareLag = 40.0;  // 20 until round 6, so its colour held less far into a falling end
constexpr double kMarkerPersistenceStamps = 8.0;
constexpr int kMarkerPickUpAcross = 32;  // in place of 8, which aliased with fine stripes
// Blur: its reach and passes, how far further it reaches at full pressure, how pressure and Density set what it
// lays, and its bell across the stroke.
constexpr double kBlurReachShare = 0.533;  // the 13 Blur mocks' least is 0.51 to 0.533, within 1.3 %; the six wide ones favour 0.533
constexpr int kBlurPasses = 3;  // the Blur mocks, small and wide, fit 3 closer than 2 or 4
constexpr double kBlurReachAtFull = 1.3;
constexpr double kBlurPower = 1.4;  // the 13 Blur mocks' least of 1 to 2
constexpr double kBlurDensityPower = 1.3;  // the three Blur mocks below full Density, least of 1 to 2
constexpr double kBlurBell = 0.5;
constexpr double kBlurGrow = 1.3;
// Smudge, fitted to the reference's: how far its middle moves of each step at pressure 0.8, how far out it
// reaches, a share of the radius, and how it falls off across the stroke.
constexpr double kSmudgeShare = 0.75;
constexpr double kSmudgeReach = 0.75;
constexpr double kSmudgeFalloff = 0.7;
constexpr double kSmudgeStartingDensity = 90.0;  // %: Smudge's starting Density, which gives all of kSmudgeShare
constexpr double kEraserHardness =
    0.8;  // of the way from the softest coverage to the hardest. The opacity solve's 1/2048 steps give 0.756 to 0.824 one look; the step below fits the reference's Eraser a little better, still to be decided
// The Eraser's coverage counts as full at this share of what a line's centre reaches, so a little off the centre it still clears to
// transparent, where its softer coverage would leave a trace of about 1/255.
constexpr double kEraserFullAt = 0.99;
// On the 5th Shape the Eraser counts this share of the merge's full point as full, so one firm stroke clears paint.
constexpr double kEraserFifthFullAt = 0.97;
constexpr double kWaterLayRamp = 0.1;  // pressure above Blur Pressure over which what it lays comes in
// A Pencil tap's two stamps: their share of a line's coverage, the radius, px, up to which they keep it all, and how fast it falls
// past there. Fitted to the reference's taps.
constexpr double kTapShare = 0.37;
constexpr double kTapFullRadius = 3.0;
constexpr double kTapFalloff = 0.6;

double sharpnessOf(const BrushSettings& settings) {
    return std::clamp(settings.sharpness / 100.0, 0.0, 1.0);
}

// High density hardens a soft edge, since 1 - exp(-density) reaches nearly 1 long before the rim. So a softer tip counts a lower
// density as full, which keeps the conversion close to a straight line and its centre just as solid.
// The Pencil's, Brush's and Water Color's lines at full Density are solid to within a pixel of their edge at any Sharpness, as
// the reference's Pencil, Brush and Water Color show.
// The Eraser is the Pencil's tip without pressure: its outline, inset and taps, and its lift, in
// StrokeController.cpp. Its coverage counts as full a little later than the Pencil's, kEraserHardness of the way from the softest to
// the hardest, so its edge is the mock's, about 0.56 erased at the outermost pixel and 0.97 at the next, and its round ends fade in
// over about 8 px along the line.
// The form kPlainForm is the round tip with no tool's shaping or mixing: what every tool builds on. The app never offers it; the
// tests of the tip itself paint with it, since every tool now shapes or mixes its stamps.
bool isPlain(const BrushSettings& settings) {
    return settings.form == RebuiltStroke::kPlainForm;
}

// On the 5th Shape the Eraser takes away what the Pencil lays, deposit for deposit, its line's middle too, as the reference's does:
// it takes the Pencil's coverage, not its own softer one.
bool eraserAsPencil(const BrushSettings& settings) {
    return settings.tool == ToolType::Eraser && settings.edge == kEdgePresets - 1;
}

double fullAtFrom(const BrushSettings& settings) {
    const bool hard = settings.tool == ToolType::Pencil || settings.tool == ToolType::Clone || settings.tool == ToolType::Brush ||
                      settings.tool == ToolType::WaterColor || settings.tool == ToolType::PixelArtPen || eraserAsPencil(settings);
    const double sharpness = hard ? 1.0 : settings.tool == ToolType::Eraser ? kEraserHardness : sharpnessOf(settings);
    const double density = kSoftDensity * std::pow(kHardDensity / kSoftDensity, sharpness);
    return -std::expm1(-density);
}

// The five Shape buttons, from soft to hard, for the Pencil, and the Brush follows.
void shapeEdge(RoundTip& tip, const BrushSettings& settings) {
    const auto shape = static_cast<std::size_t>(std::clamp(settings.edge, 0, kEdgePresets - 1));
    tip.softShare = kShapeSoftness[shape];
    tip.softInset = kShapeInset[shape];
}

void shapePencil(RoundTip& tip, const BrushSettings& settings) {
    tip.insetOutline = true;
    const double sharpness = std::clamp(settings.sharpness / 50.0 - 1.0, -1.0, 1.0);  // -1 at 0 %, 1 at 100 %
    tip.edgeBlur = sharpness < 0.0 ? kPencilBlur + (kSoftestPencilBlur - kPencilBlur) * -sharpness
                                   : kPencilBlur + (kSharpestPencilBlur - kPencilBlur) * sharpness;
    tip.rimShare = 0.0;  // Sharpness is the blur's
    shapeEdge(tip, settings);
    if (tip.softShare <= 0.0)  // on a soft Shape, Sharpness moves nothing by more than about 0.03
        tip.edgeShift = kSharpEdgeShift * sharpness;
    tip.thinnestRadius = kPencilThinnest;
    tip.leastSpacing = kPencilLeastSpacing;
    tip.rimReach = kPencilRimReach;
    tip.evenSideFrom = kPencilEvenSideFrom;
    tip.evenSideTo = kEvenSideTo;
    const bool eraser = settings.tool == ToolType::Eraser;  // the Eraser and the Clone Stamp share the Pencil's tip
    tip.clickShare = eraser ? kEraserClickShare : kPencilClickShare;
    tip.clickPower = eraser ? kEraserClickPower : kPencilClickPower;
    tip.tapShare = kTapShare;
    tip.tapFullRadius = kTapFullRadius;
    tip.tapFalloff = kTapFalloff;
    // The Clone Stamp's tip is the Pencil's, and the Eraser's: the reference's Eraser takes away what its Pencil lays, click, profile,
    // reach and line alike, on every Shape, on a solid layer.
    if (settings.edge < kEdgePresets - 1) {  // the reference's dab
        const auto e = static_cast<std::size_t>(std::clamp(settings.edge, 0, kEdgePresets - 2));
        tip.profile = static_cast<int>(e);
        tip.mostOpacity = kPencilDabStrength[e];
        tip.spacingShare = kPencilDabSpacing[e] / profileReach(tip.profile);
        // As the 5th Shape's Pencil; the Clone Stamp alike here, so its tip stays the Pencil's (unread on these Shapes); the Eraser lays its
        // first once, as on the 5th Shape.
        tip.penLaysFirstTwice = !eraser;
        tip.evenSideFrom = 0.0;
        tip.clickShare = 1.0;  // a click is the one stamp, as it is
        tip.clickPower = 1.0;
    }
    if (settings.edge == kEdgePresets - 1) {  // the 5th Shape lays the Brush's rule, as the reference's Pencil does
        // Its skirt is Anti-Ripple: off, the edge falls away over its outline's ramp alone.
        tip.skirtNeighbours = settings.antiRipple ? kBrushSkirtNeighbours : 0;
        tip.mostOpacity = kBrushFullStamp;
        tip.penLaysFirstTwice = settings.tool == ToolType::Pencil;  // the reference's Eraser and Clone Stamp lay their first once
        tip.evenSideFrom = 0.0;  // the skirts give the side its tone
        tip.clickShare = 1.0;  // a click is the one stamp, as it is
        tip.clickPower = 1.0;
    }
}

// The Brush: light pressure on an empty layer lays pale paint, as dense as Density says and no denser,
// a tap lays next to nothing, a click that never moves a faint stamp, and the outline is as hard as the Pencil's, with a ramp in
// place of its blur, which costs too much
// on a mixing tip. On a soft Shape the ramp eases in and out, and the stamps go as far apart as the radius before the inset, so
// its middle is paler at light pressure. A firm touch lets go of what it carries sooner, and its own paint gives way sooner
// as pressure falls. The brush colour comes in as the pressure past Blur Pressure to kBrushFadePower, scaled to keep the share it
// had at pressure 0.8, where the cross settles. Its soft edges add up over the stroke, a pass every kSoftPassLength outer radii.
void shapeBrush(RoundTip& tip, const BrushSettings& settings) {
    tip.mixing.emptyIsOwnColour = true;
    tip.mixing.fuller = 1.0;
    tip.mixing.colourFadePower = kBrushFadePower;
    tip.mixing.colourFadeMost = std::pow(kBrushSettledShare, kBrushSettledPower - tip.mixing.colourFadePower);
    tip.mixing.persistenceStamps = kBrushPersistenceStamps;
    tip.mixing.firmLetGo = kBrushFirmLetGo;
    tip.mixing.mixMemory = kBrushMixMemory;
    tip.mixing.fallMemory = kBrushFallMemory;
    tip.tapIsWalked = true;
    tip.clickShare = kBrushClickShare;
    tip.clickPower = kBrushClickPower;
    tip.rimReach = kPencilRimReach;
    tip.rampWidth = kBrushRampWidth;
    tip.softSpacedWide = true;
    tip.mixing.passLength = kSoftPassLength;
    tip.mixing.onGrid = true;  // its mixing on a grid at every size, a pixel apart below kGridTwoFrom
    tip.evenSideFrom = kBrushEvenSideFrom;
    tip.evenSideTo = kEvenSideTo;
    shapeEdge(tip, settings);
    if (settings.edge == kEdgePresets - 1) {  // the 5th Shape's stamps, whose skirts give the side its tone without evening it
        tip.skirtNeighbours = kBrushSkirtNeighbours;
        tip.mostOpacity = kBrushFullStamp;
        tip.penLaysFirstTwice = true;
        tip.evenSideFrom = 0.0;
        tip.clickShare = 1.0;  // a click is the one stamp, as it is
        tip.clickPower = 1.0;
    }
    tip.mixing.outlineAlongPath = settings.edge < kSoftShapes;  // the soft Shapes' outline adds up along the path
    if (tip.mixing.outlineAlongPath) {  // the soft Shapes start at their tone
        tip.startFuller = kBrushStartFuller;
        tip.startReach = kBrushStartReach;
    }
}

// Water Color melts what it crosses into one even colour: what's under moves toward a blur of
// the dab's half-width by kWaterMelt times its coverage, and its paint carries no detail. What it picks up counts as paint at
// 1 - Dilution, or its own opacity if that's more, so passes build toward a ceiling. Below Blur Pressure it lays no paint, only
// melts, and above it what it lays comes in over kWaterLayRamp of pressure. Its edge is the Brush's, soft at half pressure and
// hardening by kWaterHardensTo, and a firm touch lets go of what it carries sooner. Its brush colour comes in as the pressure past
// Blur Pressure to kWaterFadePower, at most kWaterFadeMost of it, and over paint thinner than it carries it keeps what it carries
// longer, by kWaterThinKeep.
void shapeWater(RoundTip& tip, const BrushSettings& settings) {
    tip.mixing.wetBlurRadii = kWaterMeltRadii;
    tip.mixing.wetDetail = false;
    tip.mixing.paintCeiling = true;
    tip.mixing.lightFuller = kWaterLightFuller;
    tip.mixing.evenAcross = true;
    tip.mixing.wetHold = kWaterHold;
    tip.tapIsWalked = true;  // a tap leaves nothing, a click that never moves one stamp
    tip.mixing.colourFadePower = kWaterFadePower;
    tip.mixing.colourFadeMost = kWaterFadeMost;
    tip.mixing.fuller = kWaterFuller;
    tip.mixing.meltRate = kWaterMelt;
    tip.mixing.layOnlyAbove = true;
    tip.mixing.layRamp = kWaterLayRamp;
    tip.mixing.firmLetGo = kBrushFirmLetGo;
    tip.rimReach = kPencilRimReach;
    tip.rampWidth = kBrushRampWidth;
    tip.rimHardensFrom = kWaterHardensFrom;
    tip.rimHardensTo = kWaterHardensTo;
    tip.mixing.thinKeep = kWaterThinKeep;
    tip.gatherWidth = kWaterGatherWidth;
    tip.clickShare = kWaterClickShare;
    tip.mixing.dilutionBySamplesFrom = kWaterDilutionBySamplesFrom;
    tip.mixing.dilutionBySamples.assign(kWaterDilutionsBySamples.begin(), kWaterDilutionsBySamples.end());
    tip.mixing.onGrid = true;  // its mixing on a grid at every size, a pixel apart below kGridTwoFrom
    if (settings.edge == kEdgePresets - 1) {  // the 5th Shape lays the Brush's rule, and builds its tone as the reference's does
        tip.skirtNeighbours = kBrushSkirtNeighbours;
        tip.mostOpacity = kWaterDepositShare;
        tip.penLaysFirstTwice = true;
        tip.clickShare = 1.0;  // a click is the one stamp, as it is
        tip.clickPower = 1.0;
        tip.rimHardensFrom = -1.0;  // as crisp at every pressure as at full
        tip.mixing.evenAcross = false;  // the skirts give the side its tone
        tip.edgeShift = kWaterFirmEdgeShift;  // its outline where the reference's is
        tip.rampWidth = kWaterFirmRampWidth;
        tip.gatherWidth = 0.0;  // each stamp takes the pixel the same share, wherever the pen's samples fall
        tip.mixing.pickUpEachStamp = true;  // it picks up at every stamp, in dabs of the usual count
        tip.mixing.pickUpAtPen = true;  // where the pen is, so samples far apart for its size lay a lighter line
        tip.mixing.penReach = kWaterPenReach;
        tip.mixing.dilutionBySamplesFrom = 0.0;
        tip.mixing.fuller = 1.0;
        tip.mixing.lightFuller = 0.0;
        tip.mixing.depositsTowardPaint = true;
        tip.mixing.dilutionPower = kWaterDilutionPower;
        tip.mixing.persistenceStamps = kWaterPersistenceStamps;
        tip.mixing.firmLetGo = 1.0;
        tip.mixing.thinKeep = 0.0;
        tip.mixing.pickUpReach = kWaterPickUpReach;
        tip.mixing.blendingPower = kWaterBlendingPower;
        tip.firmPower = kWaterFirmPower;
        tip.mixing.meltFirmPower = kWaterMeltFirmPower;
        tip.mixing.meltRate = kWaterFirmMelt;
        tip.mixing.colourFadePower = kWaterFirmFadePower;
        tip.mixing.meltsOwnPaint = true;  // below full pressure its melt softens the stroke's own paint too, as the reference's does
        tip.mixing.colourFadeMost = 1.0;  // at full pressure all of Blending's share, as the reference's
    }
}

// The Marker: a pixel's opacity is the most of what's there and each stamp's ceiling, so passes don't build up.
// px: its outline sits this far inside where the Brush's edge would put it.
constexpr double kMarkerEdgeShift = -0.5;

void shapeMarker(RoundTip& tip, const BrushSettings&) {
    tip.tapIsWalked = true;
    tip.clickShare = 1.0;  // a click is the one stamp under the stroke's own top, as the reference's
    tip.clickPower = 1.0;
    tip.mixing.ceilingOnly = true;
    tip.mixing.shareByCeiling = kMarkerShareByCeiling;
    tip.mixing.persistenceStamps = kMarkerPersistenceStamps;
    tip.mixing.pickUpAcross = kMarkerPickUpAcross;
    tip.rimReach = kPencilRimReach;
    tip.rampWidth = kBrushRampWidth;
    tip.insetOutline = true;
    tip.mixing.meltRate = kWaterMelt;
    tip.mixing.wetBlurRadii = kWaterMeltRadii;
    tip.mixing.wetDetail = false;
    tip.mixing.meltColourOnly = true;
    tip.mixing.shareLag = kMarkerShareLag;
    tip.mixing.keepsBodyColour = true;
    tip.mixing.holdsColour = true;
    tip.mixing.onGrid = true;  // it reads between grid points from kGridTwoFrom
    tip.edgeShift = kMarkerEdgeShift;
}

// Blur lays no colour: it moves what was under the stroke toward a Gaussian blur of it, as wide as
// kBlurPasses passes of a box each way Blur Width times kBlurReachShare of the radius wide, so an even colour stays as it is and a
// boundary spreads into a gradient without moving. Blur Width sets how soft it goes; Density sets how much of the blur is laid over
// what was there, to kBlurDensityPower, so the stripes keep their edges and lose contrast, as the reference's do. At full
// pressure and Density it's all blur. Across the stroke it's a bell kBlurBell of the radius wide, on stamps kBlurGrow times as wide,
// so it fades out a little past the size, as the mock's does. Past pressure 0.8 the blur reaches further, kBlurReachAtFull times at
// full, so a firm stroke spreads a boundary wider.
void shapeBlur(RoundTip& tip, const BrushSettings& settings) {
    const double width = std::clamp(settings.blurWidth / 100.0, 0.0, 1.0);
    tip.mixing.meltRate = 1.0;
    tip.mixing.wetDetail = false;
    tip.mixing.wetBlurRadii = width * kBlurReachShare;
    tip.mixing.blurPasses = kBlurPasses;
    tip.mixing.gaussian = true;
    tip.mixing.blurStrength = std::pow(std::clamp(settings.density / 100.0, 0.0, 1.0), kBlurDensityPower);
    if (width <= 0.0)
        tip.mixing.blurStrength = 0.0;  // Blur Width 0 doesn't blur, as the reference's doesn't
    tip.mixing.blurPower = kBlurPower;
    tip.mixing.blurFollowsPressure = settings.pressureDensity;
    tip.mixing.blurReachAtFull = kBlurReachAtFull;
    tip.tapIsWalked = true;
    tip.radius *= kBlurGrow;
    tip.mixing.wetBlurRadii /= kBlurGrow;
    tip.mixing.blurBell = kBlurBell / kBlurGrow;
}

// Smudge only moves what's there, with no brush colour while Coloring is off: each stamp pushes
// what's under it along its step, most in the middle, where paint rides with the tip a short way and smears, and less toward the edge
// of its reach, where detail only bends.
void shapeSmudge(RoundTip& tip, const BrushSettings& settings) {
    tip.mixing.smudgeShare = kSmudgeShare;
    tip.mixing.smudgeReach = kSmudgeReach;
    tip.mixing.smudgeFalloff = kSmudgeFalloff;
    tip.mixing.smudgeShare *= std::clamp(settings.density / kSmudgeStartingDensity, 0.0, 1.0);  // the starting 90 % is all of it
    tip.tapIsWalked = true;
}

// The Pixel Art Pen: a hard disc of the Size, every pixel whose centre is inside it laid whole and the pixel under the pen
// always, so the finest pen marks one pixel. It ignores pressure and Density; its opacity is the Opacity setting times the colour's
// alpha (paintFrom). Its stamps lay from the pen's first pixel, and a click is its one stamp.
void shapePixelArt(RoundTip& tip) {
    tip.hard = true;
    tip.followsPressure = false;
    tip.densityFollowsPressure = false;
    tip.thinnestRadius = 0.0;  // drawn at its own radius however fine: the pixel under its centre is always laid
    tip.tapIsWalked = true;
    tip.clickShare = 1.0;  // a click is the one stamp
    tip.clickPower = 1.0;
}

RoundTip tipFrom(const BrushSettings& settings) {
    RoundTip tip;
    tip.radius = settings.sizePx() / 2.0;
    tip.followsPressure = settings.pressureSize;
    tip.leastWidth = std::clamp(settings.minSize / 100.0, 0.0, 1.0);
    tip.sizeSensitivity = settings.sensitivitySize;
    tip.densityFollowsPressure = settings.pressureDensity;
    tip.leastDensity = std::clamp(settings.minDensity / 100.0, 0.0, 1.0);
    tip.densitySensitivity = settings.sensitivityDensity;
    tip.coverage = std::clamp(settings.density / 100.0, 0.0, 1.0) * fullAtFrom(settings);
    tip.edge = settings.edge;
    tip.rimShare = 1.0 - sharpnessOf(settings);
    // With the Stabilizer on, the stamps follow a curve through the smoothed samples, with no corner at each; at 0 the path is
    // straight from sample to sample, as the pen gave it. The Pencil's tip too: it followed the curve at every level, and at
    // Stabilizer 0 the reference's Pencil goes straight: its plain C 20.5 / 7.5 levels off at 35 / 100 px curving, 3.0 / 1.1 straight,
    // its flick 14.3 -> 3.6.
    tip.followsCurve = settings.stabilizer && settings.stabilizer->value > 0;
    if ((settings.tool == ToolType::Pencil || settings.tool == ToolType::Clone || settings.tool == ToolType::Eraser) && !isPlain(settings))
        shapePencil(tip, settings);  // the Clone Stamp's tip is the Pencil's
    if (settings.tool == ToolType::PixelArtPen)
        shapePixelArt(tip);
    if (!settings.curveInterpolation)  // Curve Interpolation off: straight from sample to sample
        tip.followsCurve = false;
    if (settings.tool == ToolType::Pencil || settings.tool == ToolType::Clone || settings.tool == ToolType::Eraser)
        tip.densityAssist = !settings.disableDensityAssist;  // the stamp tools; the mixing tools keep their own
    return tip;
}

// The painting tools mix, a dab at a time: the Brush, Water Color and the Marker.
TipMixing::Kind mixingOf(ToolType tool) {
    switch (tool) {
    case ToolType::Brush: return TipMixing::Kind::Brush;
    case ToolType::WaterColor: return TipMixing::Kind::WaterColor;
    case ToolType::Marker: return TipMixing::Kind::Marker;
    case ToolType::Blur: return TipMixing::Kind::Blur;
    case ToolType::Smudge: return TipMixing::Kind::Smudge;
    default: return TipMixing::Kind::None;
    }
}

// A transparent colour doesn't erase with a painting tool: it mixes toward transparent. The Marker has no Dilution, Blur Pressure or
// Keep Opacity.
void addMixing(RoundTip& tip, const BrushSettings& settings, StrokePaint& paint) {
    const TipMixing::Kind kind = isPlain(settings) ? TipMixing::Kind::None : mixingOf(settings.tool);
    if (kind == TipMixing::Kind::None || paint.mode == LayMode::Untouched)
        return;
    const bool marker = kind == TipMixing::Kind::Marker;
    tip.mixing.kind = kind;
    tip.mixing.transparentColour = paint.mode == LayMode::Erase;
    if (tip.mixing.transparentColour || (marker && paint.mode == LayMode::KeepAlpha))
        paint.mode = LayMode::Over;
    tip.mixing.blending = settings.blending / 100.0;
    tip.mixing.blendingFollowsPressure = settings.pressureBlending;
    tip.mixing.blendingSensitivity = settings.sensitivityBlending;
    tip.mixing.dilution = marker ? 0.0 : settings.dilution / 100.0;
    tip.mixing.dilutionFollowsPressure = !marker && settings.pressureDilution;
    tip.mixing.persistence = settings.persistence / 100.0;
    tip.mixing.blurPressure = marker ? 0.0 : settings.blurPressure / 100.0;
    if (kind == TipMixing::Kind::Brush)
        shapeBrush(tip, settings);
    if (kind == TipMixing::Kind::WaterColor) {
        shapeWater(tip, settings);
        if (brushLaidInGroups(tip.mixing, formKeepsOffGroups(tip.form, tip.mixing, tip.edge), paint.mode)) {
            // Laid in groups, its stamps are the Brush's: no lighter between the pen's samples (gatherWidth), as crisp at every
            // pressure, and the Brush's edge for its Shape. Master's took its soft Shapes' lines lighter at 50 px than at 200 px (the 1st
            // at pressure 0.6 0.122 against 0.148); the reference's don't change with size (0.205 at both), ours now 0.198 / 0.207.
            tip.gatherWidth = 0.0;
            tip.rimHardensFrom = -1.0;
            tip.edgeShift = 0.0;
            tip.rampWidth = kBrushRampWidth;
            shapeEdge(tip, settings);
            tip.softSpacedWide =
                true;  // its soft Shapes' stamps as far apart as the Brush's: 170 / 348 / 601 / 974 on the C, the reference's 170 / 349 / 602 / 977
        }
    }
    if (marker)
        shapeMarker(tip, settings);
    if (kind == TipMixing::Kind::Blur)
        shapeBlur(tip, settings);
    if (kind == TipMixing::Kind::Smudge)
        shapeSmudge(tip, settings);
}

LayMode modeFrom(const QColor& color, const BrushSettings& settings, bool preserveOpacity) {
    if (settings.tool == ToolType::Clone)  // it lays copied pixels, whatever the colour
        return preserveOpacity ? LayMode::KeepAlpha : LayMode::Over;
    if (settings.tool == ToolType::Eraser || color.alpha() == 0)
        return preserveOpacity ? LayMode::Untouched : LayMode::Erase;
    return preserveOpacity ? LayMode::KeepAlpha : LayMode::Over;
}

StrokePaint paintFrom(const QColor& color, const BrushSettings& settings, bool preserveOpacity) {
    StrokePaint paint;
    paint.color = {static_cast<float>(color.red() / 255.0), static_cast<float>(color.green() / 255.0),
                   static_cast<float>(color.blue() / 255.0)};
    paint.opacity = static_cast<float>(std::clamp(settings.opacity / 255.0, 0.0, 1.0));
    paint.mode = modeFrom(color, settings, preserveOpacity);
    // The Pixel Art Pen lays the colour's own alpha too, as it always has; a colour with none erases, by the Opacity alone.
    if (settings.tool == ToolType::PixelArtPen && paint.mode != LayMode::Erase && paint.mode != LayMode::Untouched)
        paint.opacity *= color.alphaF();
    // The Selection Eraser clears what it crosses as the Eraser does, rather than leaving a trace of the selection.
    const bool clears = settings.tool == ToolType::Eraser || settings.tool == ToolType::SelectionEraser;
    paint.fullAt =
        static_cast<float>(fullAtFrom(settings) * (clears ? (eraserAsPencil(settings) ? kEraserFifthFullAt : kEraserFullAt) : 1.0));
    return paint;
}

bool layDownTool(ToolType tool) {
    switch (tool) {
    case ToolType::Pencil:
    case ToolType::Clone:
    case ToolType::Brush:
    case ToolType::WaterColor:
    case ToolType::Marker:
    case ToolType::Eraser:
    case ToolType::Blur:
    case ToolType::Smudge:
    case ToolType::SelectionPen:  // the selection pens lay the plain round tip, white or erasing, on the selection's picture
    case ToolType::SelectionEraser:
    case ToolType::PixelArtPen:  // a hard disc
        return true;
    default: return false;
    }
}

bool isEmptyTile(const Tile& tile) {
    return std::all_of(tile.px.begin(), tile.px.end(), [](Channel value) { return value == 0; });
}

QPointer<const BrushResourceLibrary> s_resources;

// The tools a form shapes, as their stamps' outlines: the stamp tools, and the mixing tools' dabs.
bool takesForm(ToolType tool) {
    switch (tool) {
    case ToolType::Pencil:
    case ToolType::Eraser:
    case ToolType::Clone:
    case ToolType::SelectionPen:
    case ToolType::SelectionEraser:
    case ToolType::Brush:  // and the mixing tools' dabs
    case ToolType::WaterColor:
    case ToolType::Marker:
    case ToolType::Blur:
    case ToolType::Smudge: return true;
    default: return false;
    }
}

bool isRound(const BrushSettings& settings) {
    return settings.form == BrushResourceLibrary::kSimpleCircle || isPlain(settings);
}

// A brush shape is stamped as the reference stamps it (StampTip::pictureStamps), its Spacing a percentage of the size apart, turned by
// its Angle, and along the stroke with Angle Control at "along the stroke"; "with the pen" isn't read yet, so it's turned by Angle alone.
// Its jitters: Angle Jitter at 100 % turns a stamp anywhere round, half a turn either way; Size Jitter and Scattering are shares
// of the size, either way. Untuned until the reference's pack (whose pixel cases have the jitters off): the plainest reading of each.
void shapePictureStamps(RoundTip& tip, const BrushSettings& settings) {
    tip.pictureStamps = true;
    tip.pictureAngle = settings.shapeAngle * std::numbers::pi / 180.0;
    tip.pictureAlongStroke = settings.shapeAngleControl == 1;
    tip.pictureSpacing = settings.shapeSpacing / 100.0;
    tip.pictureAngleJitter = settings.shapeAngleJitter / 100.0 * std::numbers::pi;
    tip.pictureSizeJitter = settings.shapeSizeJitter / 100.0;
    tip.pictureScatter = settings.shapeScattering / 100.0;
    tip.pictureFullAt = fullAtFrom(settings);
}

// The share of the outline a form is: the panel's bar for its kind.
int formStrengthOf(const BrushSettings& settings, ResourceKind kind) {
    if (kind == ResourceKind::Shape)
        return settings.shapeDensity;
    return settings.formStrength;
}

// The stroke's form, or none: Round, a name the library doesn't hold, or a tool it doesn't shape yet.
// `erasing`: the tool paints the transparent colour, and the tip shape's Invert for Transparency turns its Invert over, as the
// texture's switch does.
std::shared_ptr<const StampForm> formFrom(const BrushSettings& settings, bool erasing = false) {
    if (!s_resources || isRound(settings) || !takesForm(settings.tool))
        return nullptr;
    const BrushResource* form = s_resources->form(settings.form);
    if (!form || form->image.isNull() || form->kind == ResourceKind::Bristle)  // a bristle map is hairs, not an outline
        return nullptr;
    // A brush shape is stamped whole, so turned over it would erase the square around it: it erases as it paints, by Invert alone.
    const bool erasingTurn = erasing && settings.formEraseLow && form->kind != ResourceKind::Shape;
    const bool invert = settings.formInvert != erasingTurn;
    // A brush shape's size is its own Scaling, a blot map's the panel's Scale.
    const int scale = form->kind == ResourceKind::Shape ? settings.shapeScaling : settings.formScale;
    return std::make_shared<const StampForm>(form->image, scale / 100.0, formStrengthOf(settings, form->kind) / 100.0, invert,
                                             form->kind == ResourceKind::BlotMap, settings.formSharpen, form->kind == ResourceKind::Shape,
                                             settings.shapeHighQuality, erasingTurn && !settings.formInvert,
                                             form->shapeSettings && form->shapeSettings->oneStroke, groundLaysNothing(*form));
}

// The stroke's bristle map's hairs, or none.
std::shared_ptr<const Bristles> bristlesFrom(const BrushSettings& settings) {
    if (!s_resources || isRound(settings) || !takesForm(settings.tool))
        return nullptr;
    const BrushResource* form = s_resources->form(settings.form);
    if (!form || form->image.isNull() || form->kind != ResourceKind::Bristle)
        return nullptr;
    return bristlesOf(form->image);
}

// Whether the stroke's form is a brush shape.
bool isBrushShape(const BrushSettings& settings) {
    if (!s_resources || isRound(settings) || !takesForm(settings.tool))
        return false;
    const BrushResource* form = s_resources->form(settings.form);
    return form && form->kind == ResourceKind::Shape;
}

// The stand-in form a bristle map's stamps take: a picture at Strength 1, whose picture each stamp's hairs are.
std::shared_ptr<const StampForm> hairsForm() {
    QImage none(1, 1, QImage::Format_Grayscale8);
    none.fill(255);  // white: a stamp without hairs lays nothing
    return std::make_shared<const StampForm>(none, 1.0, 1.0, false);
}

// A mixing tool's stroke as a whole coat through the paper: the coat's density against the grain's share, and how far its
// painted tile goes where the paper lets all of it through, over clear paper and over a whole layer. Fitted to the reference's Marker
// and Water Color (the C at 50 and 200 px, and Water Color's ramp and two-stroke readings), simulating
// the merge on Doodlebold's own plain renders: the Marker 11.2 and 9.5 levels of 8 bits off (floors 13.4 and 8.6) where the
// earlier merge left
// it 32.6 and 46.0, Water Color 13.1, 12.7, 11.0, 10.2 (15.1, 9.4, 5.1, 3.4) where it left 13.2, 19.5, 20.5, 22.8.
struct PaperCoat {
    float density = 0.0f, clear = 0.0f, over = 0.0f;
};
constexpr PaperCoat kMarkerPaperCoat{16.0f, 1.0f, 0.8f};  // 12 to 32 alike; the top 0.75 to 0.85
constexpr PaperCoat kWaterColorPaperCoat{6.0f, 0.8f, 0.8f};  // 5 to 8 alike; the top 0.75 to 0.85, the same over a whole layer

// The Brush through the paper: over paint where its alpha can't say how much it coated, a whole coat thinned as a stamp tool's
// full coat (kPaperFullDensity, its top 1); on its hard edge, the paper's tops alone take it, a bar edgeBarDepth times the soft edge's
// faded over edgeBarWidth; and it picks up what's shown, through the paper (DabPainter::shownAt). Fitted to the reference's Brush
// (its ramp, two strokes, and the C at 50 and 200 px) against the margin: 4.25, 3.18, 9.74 and 6.76 levels
// of 8 bits (limits 5.96, 3.66, 11.92 and 6.66). The density 8 to 9, the top 1 (0.93 and 0.96 worse), the bar 1.5 over 0.4 (1.75 over
// 0.6 alike, the Pencil's 1.25 over 0.4 brush-two 3.67).
struct BrushPaperCoat {
    float density, top, edgeBarDepth, edgeBarWidth;
};
constexpr BrushPaperCoat kBrushPaperCoat{kPaperFullDensity, 1.0f, 1.5f, 0.4f};
constexpr float kWaterEdgeBarShare = 0.75f;  // Water Color's 5th Shape's edge bar, of the Brush's depth

PaperCoat paperCoatOf(ToolType tool) {
    if (tool == ToolType::Marker)
        return kMarkerPaperCoat;
    if (tool == ToolType::WaterColor)
        return kWaterColorPaperCoat;
    return {};
}

// The stroke's texture, or none: no texture, `Layer paper`, which has no picture of its own, a name the library doesn't hold, or the
// Pixel Art Pen, which reads none, the Smudge, which the reference's paper leaves as it is, to the bit, or the selection pens,
// which never read it. Painting the transparent colour with the switch on (its start) turns the paper over, as the reference's
// does: it takes most where painting would take least, the paper's low places.
std::shared_ptr<const PaperGrain> grainFrom(const BrushSettings& settings, bool erasing, std::function<float(float)> alsoKeep = {}) {
    if (!s_resources || settings.tool == ToolType::PixelArtPen || settings.tool == ToolType::Smudge ||
        settings.tool == ToolType::SelectionPen || settings.tool == ToolType::SelectionEraser)
        return nullptr;
    const BrushResource* texture = s_resources->texture(settings.texture);
    if (!texture || texture->image.isNull())
        return nullptr;
    auto grain = std::make_shared<PaperGrain>(texture->image, settings.textureScale / 100.0, settings.textureStrength / 100.0,
                                              settings.textureInvert != erasing, erasing && !settings.textureInvert);
    if (alsoKeep)
        grain->keepAlso(std::move(alsoKeep));
    return grain;
}

}

void RebuiltStroke::setResources(const BrushResourceLibrary* library) {
    s_resources = library;
}

bool RebuiltStroke::canPaint(const BrushSettings& settings) {
    if (settings.tool == ToolType::PixelArtPen)
        return true;  // it reads no form or texture
    // A texture's grain is laid at the merge, on any tool; a form is every tool's outline.
    return layDownTool(settings.tool) && (isRound(settings) || takesForm(settings.tool));
}

void RebuiltStroke::paintInBackground(std::function<void()> onFrame, std::function<void()> whileCatchingUp) {
    m_inBackground = true;
    m_onFrame = std::move(onFrame);
    m_whileCatchingUp = std::move(whileCatchingUp);
}

QRect RebuiltStroke::begin(TiledImage* target, const QColor& color, const BrushSettings& given, const InputSample& first,
                           bool preserveOpacity, const SelectionMask* selection) {
    // With a bristle map each stamp's outline is its hairs, the same at every Shape: the tool's 5th, its hardest. A brush
    // shape's stamps are its picture, the same at every Shape too: Water Color's Water Blur lays 0.27 on its 1st and 3rd Shapes
    // where the reference's lays 0.17 at all three.
    const std::shared_ptr<const Bristles> bristles = bristlesFrom(given.clamped());
    BrushSettings settings = given;
    if (bristles || isBrushShape(given.clamped()))
        settings.edge = kEdgePresets - 1;
    waitUntilRetired();
    m_worker.reset();
    m_shownBlocks.clear();  // the last stroke's lift and end show frames without their blocks being taken
    m_target = target;
    m_selection = selection;
    m_paint = paintFrom(color, settings.clamped(), preserveOpacity);
    // Painting the transparent colour turns the paper over with the switch on, as the reference's does; the Eraser always paints it.
    // The Brush's coat
    // is thinned by what its alpha says it covered; the Marker's and Water Color's are whole coats through the paper.
    const PaperCoat coat = paperCoatOf(settings.tool);
    // A mixing tool's whole coat, kept with its grain.
    const float wholeDensity = settings.tool == ToolType::Brush ? kBrushPaperCoat.density : coat.density;
    std::function<float(float)> alsoKeep;
    if (wholeDensity > 0.0f)
        alsoKeep = [wholeDensity](float share) { return wholeCoatThroughPaper(share, wholeDensity); };
    m_paint.grain = grainFrom(settings.clamped(), settings.textureEraseLow && m_paint.mode == LayMode::Erase, std::move(alsoKeep));
    if (m_paint.grain && settings.clamped().textureScratch > 0)  // Scratch Prs.
        m_paint.scratchDepth = static_cast<float>(kScratchDepth * std::pow(settings.clamped().textureScratch / 100.0, kScratchPower));
    m_paint.thinsCoat = settings.tool == ToolType::Brush;

    // The coat's top through the paper comes in with the paper's Intensity: at Intensity 0 the paper takes nothing, as the
    // reference's, where Water Color's top of 0.8, fitted at 95 and 100, took a fifth of its coat whatever the Intensity (with the
    // texture at Strength 0 its line 0.325 against its plain 0.406; at 60, the held-out check, 11 to 15 levels light).
    const float intensity = m_paint.grain ? m_paint.grain->intensity() : 1.0f;
    m_paint.wholeCoatClear = coat.clear > 0.0f ? 1.0f + (coat.clear - 1.0f) * intensity : coat.clear;
    m_paint.wholeCoatOver = coat.over > 0.0f ? 1.0f + (coat.over - 1.0f) * intensity : coat.over;
    m_paint.wholeCoatDensity = coat.density;
    if (settings.tool == ToolType::Clone) {
        m_paint.source = m_cloneSource;
        m_paint.sourceOffset = m_cloneOffset;
    }
    RoundTip tip = tipFrom(settings.clamped());
    tip.form = formFrom(settings.clamped(), m_paint.mode == LayMode::Erase);
    if (bristles) {
        tip.form = hairsForm();
        tip.bristles = bristles;
        tip.bristleSpread = kHairSpreadShare * settings.clamped().sizePx() / 2.0;
        tip.hairRadius = kHairWidthShare * settings.clamped().sizePx() * settings.clamped().bristleSize / 100.0 / 2.0;
        tip.hairLeast = settings.clamped().bristleMinimum / 100.0;
        tip.hairFollowsPressure = settings.clamped().pressureSize;
        tip.hairSensitivity = settings.clamped().sensitivitySize;
        const int direction = settings.clamped().bristleDirection;
        tip.hairWay = direction == kBristleDirectionAuto  ? StampTip::HairWay::AlongStroke
                      : direction == kBristleDirectionPen ? StampTip::HairWay::WithPen
                                                          : StampTip::HairWay::Fixed;
        tip.hairWander = settings.clamped().bristleRandomize / double(kBristleRandomizeMost);
        tip.hairDetectTurn = settings.clamped().bristleDetectTurn;
    }
    if (m_paint.grain && tip.profile >= 0 && !tip.form) {  // the reference's dab takes the paper dab by dab
        tip.paperPerStamp = m_paint.grain;
        m_paint.paperPerDab = true;
    }
    // The Pencil's, Eraser's and Clone Stamp's 1st to 4th Shapes, found by the tip's own softness (the Clone Stamp's tip may be softer
    // than its setting reads); a form's outline is its picture, not a fading edge.
    const std::size_t shapeIndex =
        static_cast<std::size_t>(std::find(kShapeSoftness.begin(), kShapeSoftness.end(), tip.softShare) - kShapeSoftness.begin());
    const bool softShape = !tip.form && tip.softShare > 0.0 && shapeIndex < kShapeSoftness.size();
    m_paint.softEdge = softShape ? kSoftEdgeBlend[shapeIndex] : 0.0f;
    m_paint.hardEdge = !tip.form && tip.softShare <= 0.0;  // the 5th Shape's
    m_paint.softHardDepth = softShape ? kSoftEdgeHardDepth[shapeIndex] : 1.0f;
    if (m_paint.thinsCoat) {  // the Brush
        m_paint.uncoatedDensity = kBrushPaperCoat.density;
        m_paint.uncoatedTop = kBrushPaperCoat.top;
        m_paint.edgeBarDepth = m_paint.hardEdge ? kBrushPaperCoat.edgeBarDepth : 0.0f;
        m_paint.edgeBarWidth = kBrushPaperCoat.edgeBarWidth;
        m_paint.picksUpThroughPaper = true;
    }
    // Water Color's 5th Shape takes the Brush's edge bar too, kWaterEdgeBarShare of its depth: with a texture the reference's fringe
    // keeps only the paper's tops, ours laid it at every paper; its C 5 % too many pixels at 35 px. Fitted on its texture C (Canvas and
    // Paper Grain, 35 / 100 / 400 px, Strength 95 and 100) and the texture readings: 0 / 0.5 / 0.75 / 1.0 5.81 + 5.95 / 5.41 + 5.62 /
    // 5.31 + 5.53 / 5.56 + 5.80 levels, readings 730.82 / 728.76 / 727.52 / 728.71 summed (watercolor-ramp, -two, c5-watercolor-50 and
    // -200 each better). The Marker keeps none: laid by events it keeps no outline for the bar to read.
    if (settings.tool == ToolType::WaterColor && settings.clamped().edge == kEdgePresets - 1 && !tip.form) {
        m_paint.edgeBarDepth = kWaterEdgeBarShare * kBrushPaperCoat.edgeBarDepth;
        m_paint.edgeBarWidth = kBrushPaperCoat.edgeBarWidth;
    }

    // Water Color with a blot map keeps its 5th Shape's skirt and cap: the reference's band over a blot map is its plain line's
    // tone, 0.41 to 0.49 inside at Strength 50, where without them ours was 0.2 to 0.3.
    const bool waterOverBlots = tip.form && tip.form->fixedToCanvas() && settings.tool == ToolType::WaterColor;
    if (tip.form && !waterOverBlots) {
        // The picture is the outline: no skirt past it, and so no cap on a stamp's opacity, which the 5th Shape's whole stamps take
        // with their skirts filling the line between them: a formed line is solved to its Density as any other.
        tip.skirtNeighbours = 0;
        tip.mostOpacity = 1.0;
    }
    addMixing(tip, settings.clamped(), m_paint);
    // addMixing shapes the Brush's tip again, its 5th Shape's skirt and stamp cap with it: with a form they go again. Left on,
    // the skirt laid the round stamp's paint between a blot map's blots, a wash of 0.4 where the reference lays none, ringed by the
    // stamps (Blots at 35 px 178,269 px painted against 86,759). Water Color's 5th Shape keeps them for
    // then; the eraser gets the same.
    if (tip.form && settings.tool == ToolType::Brush) {
        tip.skirtNeighbours = 0;
        tip.mostOpacity = 1.0;
    }
    // Water Color's 5th Shape the same, its skirt going again with a form; its stamps keep Water's cap (kWaterDepositShare).
    if (tip.form && settings.tool == ToolType::WaterColor && !waterOverBlots)
        tip.skirtNeighbours = 0;
    if (tip.form && tip.form->stamped())
        shapePictureStamps(tip, settings.clamped());
    // The Brush and Water Color laid in groups take the paper dab by dab on their soft Shapes, as the Pencil's do: the reference's
    // fringe at Shape 1 with a texture keeps only the paper's tops, its cut moving in toward the edge.
    // The Marker laid by events the same, on the soft Shapes its stamps take the reference's dab.
    if (m_paint.grain && !tip.form && tip.mixing.isOn() && settings.clamped().edge < kEdgePresets - 1 &&
        (brushLaidInGroups(tip.mixing, false, m_paint.mode) ||
         (tip.mixing.kind == TipMixing::Kind::Marker && !tip.mixing.transparentColour)))
        m_paint.paperPerDab = true;
    tip.mixing.antiRipple = settings.clamped().antiRipple;
    // The Marker laid in groups takes its soft Shapes' coverage from the reference's dab, as the Pencil's do: each stamp adds to
    // the group's mask by its Shape's soft profile, so the edge is as soft as the Shape and the wide spacing doesn't show.
    if (settings.tool == ToolType::Marker && !tip.form && !tip.mixing.transparentColour && settings.edge < kEdgePresets - 1)
        tip.profile = std::clamp(settings.edge, 0, kEdgePresets - 2);
    // The Pencil, Eraser and Clone Stamp lay their stamps on a grid from kStampGridTwoFrom.
    if ((settings.tool == ToolType::Pencil || settings.tool == ToolType::Eraser || settings.tool == ToolType::Clone)) {
        tip.mixing.onGrid = true;
        tip.fullAt = m_paint.fullAt;
    }
    // The plain Pencil and Eraser lay each pen sample straight onto the layer. The 5th Shape with Anti-Ripple is the reference's:
    // each stamp solid to its radius, a one-pixel edge, its skirt outside it; a stamp under kSolidFromRadius keeps the blurred edge, or
    // a pen's tapering tail steps from pixel to pixel, and 12 px lines lose the reference's faint outer row.
    constexpr double kRimCeilingNoneRadius =
        10.0;  // px: below this the firmer soft Shapes' rim is a ceiling (StampTip.cpp, kRimCeilingNone)
    constexpr double kSolidFromRadius = 8.0;  // px
    if ((settings.tool == ToolType::Pencil || settings.tool == ToolType::Eraser) && !tip.form && !m_paint.grain && !m_paint.source &&
        m_paint.opacity >= 1.0f && (m_paint.mode == LayMode::Over || m_paint.mode == LayMode::Erase) &&
        !(tip.profile >= 2 && tip.radius < kRimCeilingNoneRadius)) {  // the firmer soft Shapes' ceiling holds across the whole stroke
        tip.layDirect = true;
        if (settings.edge == kEdgePresets - 1 && settings.antiRipple)
            tip.solidFrom = kSolidFromRadius;
    }
    if (!sensesPressure(first.kind)) {  // a mouse's button is full pressure
        tip.mixing.blendingFollowsPressure = false;
        tip.mixing.dilutionFollowsPressure = false;
    }
    m_buffer.clear();
    m_before.clear();
    m_last = inRange(first);
    m_lifted = false;
    if (m_inBackground) {
        m_worker =
            std::make_unique<StrokeWorker>(tip, m_paint, *target, selection ? std::optional<SelectionMask>(*selection) : std::nullopt,
                                           m_onFrame, m_stillFits, m_memoryPressed, m_mayGroupDabs);
        m_worker->postBegin(m_last);
        return {};
    }
    m_evaluator.emplace(tip);
    if (tip.mixing.isOn() || tip.layDirect) {
        m_layerBefore = *target;
        m_evaluator->readFrom(&*m_layerBefore, m_paint);
    }
    m_evaluator->begin(m_last, m_buffer);
    return show();
}

void RebuiltStroke::post(const InputSample& sample) {
    if (m_worker)
        m_worker->post(sample);
    else
        m_evaluator->add(sample, m_buffer);
}

QRect RebuiltStroke::add(const InputSample& sample) {
    if (!active() || m_lifted)
        return {};
    m_last = inRange(sample);
    post(m_last);
    return m_worker ? QRect() : show();
}

InputSample RebuiltStroke::inRange(InputSample sample) const {
    if (sensesPressure(sample.kind)) {
        // the narrowest range Options' whole-percent spin boxes can set, so a range set to nothing or backwards works as that one,
        // and never divides by 0.
        constexpr double kLeastPressureSpan = 0.01;
        const double span = std::max(m_range.high - m_range.low, kLeastPressureSpan);
        sample.pressure = std::clamp((sample.pressure - m_range.low) / span, 0.0, 1.0);
    }
    return sample;
}

// Not every system sends a last sample at pressure 0, so a lift means pressure 0 at the last place the pen was.
void RebuiltStroke::postPressureZero() {
    if (sensesPressure(m_last.kind)) {
        InputSample lifted = m_last;
        lifted.pressure = 0.0;
        post(lifted);
    }
}

QRect RebuiltStroke::lift() {
    if (!active() || m_lifted)
        return {};
    postPressureZero();
    return layWholePath();
}

bool RebuiltStroke::liftInBackground() {
    if (!active() || m_lifted || !m_worker)
        return false;
    postPressureZero();
    m_lifted = true;
    m_worker->postEnd();
    return true;
}

void RebuiltStroke::catchUp() {
    if (m_worker)
        m_worker->waitUntilDone(m_whileCatchingUp);
}

QRect RebuiltStroke::layWholePath() {
    if (!active() || m_lifted)
        return {};
    m_lifted = true;
    if (m_worker) {
        m_worker->postEnd();
        m_worker->waitUntilDone(m_whileCatchingUp);
        return showNow();
    }
    m_evaluator->end(m_buffer);
    return show();
}

void RebuiltStroke::waitUntilCaughtUp() {
    if (m_worker)
        m_worker->waitUntilDone({});
}

TileMap RebuiltStroke::abandon() {
    m_worker.reset();
    m_lifted = m_lifted || !m_evaluator;
    return end();
}

TileMap RebuiltStroke::end() {
    lift();
    catchUp();
    showNow();
    dropEmptyNewTiles();
    retireWorker();
    m_target = nullptr;
    m_selection = nullptr;
    m_evaluator.reset();
    m_buffer.clear();
    return std::exchange(m_before, {});
}

QRect RebuiltStroke::show() {
    const QRect dirty = m_evaluator->takeDirty().intersected(m_target->paintArea());
    std::vector<TileMergeJob> jobs;
    for (const TileCoord coord : m_evaluator->takeChangedTiles()) {
        if (dirty.intersects(coord.pixelRect()))
            listTileToShow(coord, jobs);
    }
    if (dirty.isEmpty())
        return {};
    mergeTiles(jobs, m_paint);
    for (TileMergeJob& job : jobs)
        m_target->setTile(job.coord.x, job.coord.y, std::move(job.fresh));
    return dirty;
}

void RebuiltStroke::listTileToShow(TileCoord coord, std::vector<TileMergeJob>& jobs) {
    const TilePtr painted = m_evaluator->paintedTile(coord);
    const auto density = m_buffer.tiles().find(coord);
    if (!painted && density == m_buffer.tiles().end())
        return;
    const SelectionTile* selected = m_selection ? m_selection->tile(coord.x, coord.y) : nullptr;
    if (m_selection && !selected)
        return;
    const auto [before, isNew] = m_before.try_emplace(coord, m_target->tileShared(coord.x, coord.y));
    jobs.push_back({coord, painted ? nullptr : density->second.get(), selected, before->second, nullptr, painted});
    if (painted && m_paint.grain && m_paint.edgeBarDepth > 0.0f)
        jobs.back().outlineOf = m_evaluator->paintedOutline(coord);  // read between dabs, as the painter rests
}

// Puts what the worker has finished on the layer. Only this thread touches the layer, so nothing else can see a tile change.
QRect RebuiltStroke::showNow() {
    if (!active() || !m_worker)
        return {};
    StrokeFrame frame = m_worker->takeFrame();
    m_shownFrameLevel = frame.level;
    // Blocks are kept at one level only: they're taken as the last frame's, and one of another size overruns the compositor's.
    if (frame.level != m_shownBlocksLevel) {
        m_shownBlocks.clear();
        m_shownBlocksLevel = frame.level;
    }
    for (auto& [coord, block] : frame.blocks)
        m_shownBlocks[coord] = std::move(block);
    const QRect area = frame.area.intersected(m_target->paintArea());
    // The shown region is made once from all the tiles' rects: adding them one by one costs more with each.
    std::vector<QRect> shown;
    shown.reserve(frame.tiles.size());
    for (auto& [coord, tile] : frame.tiles) {
        m_before.try_emplace(coord, m_target->tileShared(coord.x, coord.y));
        m_target->setTile(coord.x, coord.y, std::move(tile));
        if (const QRect part = coord.pixelRect().intersected(area); !part.isEmpty())
            shown.push_back(part);
    }
    // setRects wants them by row then column, a row's touching rects joined into one.
    std::sort(shown.begin(), shown.end(),
              [](const QRect& a, const QRect& b) { return a.top() != b.top() ? a.top() < b.top() : a.left() < b.left(); });
    std::vector<QRect> rows;
    for (const QRect& r : shown) {
        if (!rows.empty() && rows.back().top() == r.top() && rows.back().height() == r.height() && rows.back().right() + 1 == r.left())
            rows.back().setRight(r.right());
        else
            rows.push_back(r);
    }
    m_shownTiles = QRegion();
    m_shownTiles.setRects(rows.data(), static_cast<int>(rows.size()));
    if (frame.newestStampUs > 0)
        m_shownStampUs = frame.newestStampUs;
    return area;
}

// A tile that didn't exist before the stroke and holds nothing after it shouldn't stay, or erasing would grow the layer.
void RebuiltStroke::dropEmptyNewTiles() {
    // Which of the tiles the stroke made are empty, looked at on the workers: a wide stroke makes thousands.
    std::vector<TileCoord> made;
    for (const auto& [coord, before] : m_before)
        if (!before)
            made.push_back(coord);
    std::vector<char> empty(made.size(), 0);
    WorkerPool::forStrokes().run(static_cast<int>(made.size()), [&](int i) {
        const TileCoord coord = made[static_cast<std::size_t>(i)];
        const TilePtr now = m_target->tileShared(coord.x, coord.y);
        empty[static_cast<std::size_t>(i)] = !now || isEmptyTile(*now);
    });
    for (std::size_t i = 0; i < made.size(); ++i)
        if (empty[i]) {
            m_target->setTile(made[i].x, made[i].y, nullptr);
            m_before.erase(made[i]);
        }
}

// The stroke's thread is stopped and its records freed on a thread of their own: a wide stroke's take tens of milliseconds, which
// the next stroke would otherwise wait for. That stroke waits for it before it starts (waitUntilRetired).
void RebuiltStroke::retireWorker() {
    waitUntilRetired();
    if (!m_worker)
        return;
    try {
        m_retiring = std::async(std::launch::async, [worker = std::move(m_worker)]() mutable { worker.reset(); }).share();
    } catch (const std::system_error&) {
        // No thread to spare: the worker went with the task that couldn't start, freed here.
    }
}

}
