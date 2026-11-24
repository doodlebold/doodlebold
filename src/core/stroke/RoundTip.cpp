#include <cstdlib>
#include "core/stroke/RoundTip.h"
#include "core/stroke/BrushDab.h"

#include "core/stroke/PressureCurve.h"
#include "core/util/WorkerPool.h"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <optional>
#include <utility>

namespace doodlebold {

namespace {

// Where a Marker stamp's opacity is more than this from the last one's, stamps go in between, so a fade changes in steps this small
// and shows no rings.
constexpr double kMarkerFadeStep = 0.02;
// Stamps further apart than this, px, change the fade in steps smaller by as much, so a wide stamp's step stays as faint.
// The ring test (426 px, the pen slowing as it lifts) holds up to 8 and fails from 8.5; below 4.5 it steps finer. From 3 to 8
// recorded stroke 6 at 1000 px paints within 1.4 % of the time, so time doesn't place it either.
constexpr double kMarkerFadeEvenSpacing = 6.1;

}

// Width and coverage each follow pressure by their own switch, minimum and sensitivity.
PressureShares sharesFor(double pressure, const RoundTip& tip) {
    return {pressureShare(pressure, tip.followsPressure, tip.leastWidth, tip.sizeSensitivity),
            pressureShare(pressure, tip.densityFollowsPressure, tip.leastDensity, tip.densitySensitivity)};
}

namespace {

// A brush shape's stamps (StampTip::pictureStamps), as the reference lays them: each the picture whole, its
// lightness the coverage, whatever the tool's dab or Strength (the reference's Pencil and Brush clicks at Strength 50 and 100 are the
// same to the pixel). They go the shape's own Spacing of their diameter apart (the reference's Soft Edge outline scalloped every
// 0.24 of the line's width, at the reference's starting Spacing, 25 %), with the shape's jitters. Nothing of the tool's own stamp rules:
// no skirt, fill-in, gather, fuller start or evening across.

void asPictureStamps(StampTip& stamps, const RoundTip& tip) {
    if (!tip.pictureStamps)
        return;
    stamps.pictureStamps = true;
    stamps.pictureAngle = tip.pictureAngle;
    stamps.pictureAlongStroke = tip.pictureAlongStroke;
    stamps.pictureAngleJitter = tip.pictureAngleJitter;
    stamps.pictureSizeJitter = tip.pictureSizeJitter;
    stamps.pictureScatter = tip.pictureScatter;
    stamps.pictureFullAt = tip.pictureFullAt;
    stamps.spacingShare = tip.pictureSpacing;
    stamps.leastSpacing = -1.0;
    stamps.mostSpacing = 1e9;
    stamps.softSpacedWide = false;
    stamps.fadeStep = 0.0;
    stamps.gatherWidth = 0.0;
    stamps.startFuller = 0.0;
    stamps.skirtNeighbours = 0;
    stamps.mostOpacity = 1.0;
    stamps.evenAcross = false;
    stamps.evenSideFrom = 0.0;
    stamps.eachStampAlone = false;
    stamps.profile = -1;
    stamps.brushDabRow = -1;
    stamps.fuller = 1.0;
    stamps.lightFuller = -1.0;
    stamps.tapShare = 0.0;
    stamps.tapIsWalked = false;
    stamps.clickShare = 1.0;
    stamps.clickPower = 1.0;
    stamps.paperPerStamp = nullptr;
}

StampTip stampTipOf(const RoundTip& tip) {
    StampTip stamps{tip.radius, tip.coverage, tip.edge, tip.rimShare};
    stamps.followsCurve = tip.followsCurve;
    stamps.layDirect = tip.layDirect;
    stamps.solidFrom = tip.solidFrom;
    stamps.insetOutline = tip.insetOutline;
    stamps.rimReach = tip.rimReach;
    stamps.rimHardensFrom = tip.rimHardensFrom;
    stamps.rimHardensTo = tip.rimHardensTo;
    stamps.edgeBlur = tip.edgeBlur;
    stamps.rampWidth = tip.rampWidth;
    stamps.softShare = tip.softShare;
    stamps.softInset = tip.softInset;
    stamps.edgeShift = tip.edgeShift;
    stamps.evenSideFrom = tip.evenSideFrom;
    stamps.evenSideTo = tip.evenSideTo;
    stamps.gatherWidth = tip.gatherWidth;
    stamps.startFuller = tip.startFuller;
    stamps.startReach = tip.startReach;
    stamps.skirtNeighbours = tip.skirtNeighbours;
    stamps.mostOpacity = tip.mostOpacity;
    stamps.profile = tip.profile;
    stamps.densityAssist = tip.densityAssist;
    stamps.spacingShare = tip.spacingShare;
    stamps.penLaysFirstTwice = tip.penLaysFirstTwice;
    stamps.firmPower = tip.firmPower;
    stamps.firmFrom = tip.mixing.blurPressure;
    stamps.softSpacedWide = tip.softSpacedWide;
    stamps.thinnestRadius = tip.thinnestRadius;
    stamps.leastSpacing = tip.leastSpacing;
    stamps.tapShare = tip.tapShare;
    stamps.tapFullRadius = tip.tapFullRadius;
    stamps.tapFalloff = tip.tapFalloff;
    stamps.tapIsWalked = tip.tapIsWalked;
    stamps.clickShare = tip.clickShare;
    stamps.clickPower = tip.clickPower;
    stamps.onGrid = tip.mixing.onGrid && tip.mixing.kind == TipMixing::Kind::None;
    stamps.fullAt = tip.fullAt;
    stamps.hard = tip.hard;
    stamps.form = tip.form;
    stamps.paperPerStamp = tip.paperPerStamp;
    stamps.bristles = tip.bristles;
    stamps.bristleSpread = tip.bristleSpread;
    stamps.hairRadius = tip.hairRadius;
    stamps.hairLeast = tip.hairLeast;
    stamps.hairFollowsPressure = tip.hairFollowsPressure;
    stamps.hairSensitivity = tip.hairSensitivity;
    stamps.hairWay = tip.hairWay;
    stamps.hairWander = tip.hairWander;
    stamps.hairDetectTurn = tip.hairDetectTurn;
    asPictureStamps(stamps, tip);
    return stamps;
}

}

RoundTipEvaluator::RoundTipEvaluator(const RoundTip& tip, Threads threads) : m_tip(tip), m_threads(threads) {
    if (laysStamps(tip))
        m_stamps.emplace(stampTipOf(tip), threads == Threads::Workers);
}

void RoundTipEvaluator::begin(const InputSample& first, StrokeBuffer&) {
    if (m_dabs)
        m_dabs->begin(first, sharesFor(first.pressure, m_tip));
    else if (m_stamps)
        m_stamps->begin(first, sharesFor(first.pressure, m_tip));
}

void RoundTipEvaluator::takeSample(const InputSample& sample) {
    if (m_dabs)
        m_dabs->add(sample, sharesFor(sample.pressure, m_tip));
    else if (m_stamps)
        m_stamps->add(sample, sharesFor(sample.pressure, m_tip));
}

void RoundTipEvaluator::add(const InputSample& sample, StrokeBuffer& buffer) {
    takeSample(sample);
    layNewPoints(buffer);
}

void RoundTipEvaluator::addAll(const std::vector<InputSample>& samples, StrokeBuffer& buffer, bool groupDabs) {
    if (m_dabs && groupDabs) {
        std::vector<std::pair<InputSample, PressureShares>> together;
        together.reserve(samples.size());
        for (const InputSample& sample : samples)
            together.emplace_back(sample, sharesFor(sample.pressure, m_tip));
        m_dabs->addTogether(together);
        return;
    }
    for (const InputSample& sample : samples)
        takeSample(sample);
    layNewPoints(buffer);
}

void RoundTipEvaluator::end(StrokeBuffer& buffer) {
    if (m_dabs)
        m_dabs->end();
    else if (m_stamps)
        m_stamps->lay(buffer, true);
}

QRect RoundTipEvaluator::takeDirty() {
    if (m_dabs)
        return m_dabs->takeDirty();
    return m_stamps ? m_stamps->takeDirty() : QRect();
}

std::vector<TileCoord> RoundTipEvaluator::takeChangedTiles() {
    if (m_dabs)
        return m_dabs->takeChangedTiles();
    return m_stamps ? m_stamps->takeChangedTiles() : std::vector<TileCoord>{};
}

void RoundTipEvaluator::setBetweenDabs(std::function<void()> betweenDabs) {
    if (m_dabs)
        m_dabs->setBetweenDabs(std::move(betweenDabs));
}

void RoundTipEvaluator::setMemoryPressed(std::function<bool()> pressed) {
    if (m_dabs)
        m_dabs->setMemoryPressed(std::move(pressed));
}

void RoundTipEvaluator::layNewPoints(StrokeBuffer& buffer) {
    if (!m_dabs && m_stamps)
        m_stamps->lay(buffer, false);
}

// A Marker's stamps go further apart, and its Density is a ceiling each stamp reaches for alone, so it isn't counted short as a soft
// tip's line is. The Brush and Water Color lay fuller stamps than the Pencil.
void RoundTipEvaluator::readFrom(const TiledImage* layer, const StrokePaint& paint) {
    if (m_stamps && m_tip.layDirect && layer)
        m_stamps->layDirectOnto(*layer, paint);
    if (!m_tip.mixing.isOn())
        return;
    StampTip tip = stampTipOf(m_tip);
    if (m_tip.mixing.kind == TipMixing::Kind::Marker) {
        tip.density = m_tip.coverage / std::max(static_cast<double>(paint.fullAt), 1e-6);
        tip.spacingShare = markerSpacingShare(m_tip.edge);
        tip.eachStampAlone = true;
        tip.fadeStep = kMarkerFadeStep * std::min(1.0, kMarkerFadeEvenSpacing / (tip.spacingShare * 2.0 * m_tip.radius));
        if (!m_tip.mixing.transparentColour && !m_tip.form) {
            // Laid in groups, as the reference's: its stamps as far apart as the reference's, by Shape, never closer than
            // kMarkerEventLeastSpacing px, and no stamps put in between where its strength changes, which the reference doesn't lay.
            tip.spacingShare = markerEventSpacingShare(m_tip.edge);
            tip.leastSpacing = kMarkerEventLeastSpacing;
            tip.fadeStep = 0.0;
        }
    } else {
        if (brushLaidInGroups(m_tip.mixing, formKeepsOffGroups(m_tip.form, m_tip.mixing, m_tip.edge), paint.mode)) {
            // Laid in groups, as the reference's: its stamps as far apart as the reference's, each one's opacity solved on the
            // reference's dab for its Shape (BrushDab.h), and a click the one stamp as it is, as the reference's clicks are.
            tip.spacingShare = brushGroupSpacingShare(m_tip.edge, m_tip.mixing.antiRipple);
            tip.brushDabRow = brushDabRow(std::clamp(m_tip.edge, 0, 4), m_tip.mixing.antiRipple);
            tip.clickShare = 1.0;
            tip.clickPower = 1.0;
            if (m_tip.mixing.kind == TipMixing::Kind::WaterColor) {
                tip.brushDabLinear = true;
                tip.brushDabScale = kWaterStampShare[static_cast<std::size_t>(std::clamp(m_tip.edge, 0, 4))];
            }
        }
        tip.fuller = m_tip.mixing.fuller;
        tip.lightFuller = m_tip.mixing.lightFuller;
        tip.evenAcross = m_tip.mixing.evenAcross;
    }
    asPictureStamps(tip, m_tip);
    m_stamps.reset();
    m_dabs.emplace(tip, m_tip.mixing, paint, *layer, m_threads == Threads::Workers);
}

TilePtr RoundTipEvaluator::paintedTile(TileCoord coord) const {
    if (m_stamps && m_stamps->laysDirect())
        return m_stamps->paintedTile(coord);
    return m_dabs ? m_dabs->tile(coord) : nullptr;
}

std::function<bool(float*)> RoundTipEvaluator::paintedOutline(TileCoord coord) const {
    if (!m_dabs)
        return {};
    return [dabs = &*m_dabs, coord](float* out) { return dabs->outline(coord, out); };
}

}
