#pragma once

#include "core/raster/TiledImage.h"
#include "core/brush/BrushSettings.h"
#include "core/stroke/StampForm.h"
#include "core/stroke/StampTip.h"
#include "core/stroke/StrokeMerge.h"

#include <QByteArray>
#include <QRect>

#include <array>
#include <mutex>
#include <atomic>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace doodlebold {

// Mixing's starting values, where a tool doesn't set its own.
inline constexpr double kColourFadePower =
    3.0;  // no tool's pixels depend on it. The Brush and Water Color set their own, the Marker has no Blur Pressure, Smudge never reads it, and Blur reads it (with Blur Pressure above 0) for a dab paint it never lays
inline constexpr double kPickUpReach =
    1.02;  // a ring past the outline, beside the stroke, comes into the pickup; fitted on the Brush, its mocks and drags best from 1.01 to 1.02 together
inline constexpr double kFuller =
    1.5;  // no tool's pixels depend on it. The Brush and Water Color set their own, the Marker's stamps don't take it, and Blur's and Smudge's stamps take it but their pixels never read a stamp's opacity
// The most a pixel's paint counts as holding when a dab mixes in: how far a carried colour lingers in the laid paint. Water
// Color on Shapes 1 to 4 and the Marker keep 3, which no reading of theirs has tested; the Brush has its own, kBrushMixMemory
// ("Brush only").
inline constexpr double kMixMemory = 3.0;

// How a painting tool mixes. It mixes once per dab: the stamps one pen event lays, up to a number set by the edge.
struct TipMixing {
    enum class Kind {
        None,
        Brush,
        WaterColor,
        Marker,
        Blur,
        Smudge
    };
    Kind kind = Kind::None;
    bool antiRipple = true;  // the setting: the Marker's 5th Shape groups fewer stamps with it on
    double blending = 0.0;  // how much of what it picks up goes into a dab, 0 to 1, before its curve. At 0 it lays plain brush colour
    bool blendingFollowsPressure = false;
    int blendingSensitivity = 100;  // %, see pressureResponse()
    double dilution = 0.0;  // how far paint it picks up keeps the layer's transparency, rather than counting as opaque, 0 to 1
    bool dilutionFollowsPressure = false;
    double persistence = 0.0;  // how long it keeps colour it's picked up, 0 to 1
    double blurPressure = 0.0;  // below this pressure it adds no colour and only moves paint around. Above it, colour fades in
    double colourFadePower =
        kColourFadePower;  // how the brush colour fades in above Blur Pressure: 1 is straight, higher is slower at first
    double colourFadeMost = 1.0;  // and the most of it at full pressure
    bool emptyIsOwnColour = false;  // below Blur Pressure, empty canvas it picks up counts as its own colour, so it paints there
    double pickUpReach = kPickUpReach;  // how far out it picks up, in the middle stamp's radii, each place inside counting the same
    double fuller = kFuller;  // the Brush and Water Color's stamps are 1 - (1 - opacity)^this: fuller than the Pencil's
    double persistenceStamps = 4.0;  // a dab of this many stamps keeps exactly Persistence of what it carried
    double wetBlurRadii = 0.0;  // what's under a dab is blurred this many of its radii wide, for Water Color's wetness and the
    // melt of Water Color, the Marker and Blur, and read that far around it (readsAround). Those three
    // set their own (kWaterMeltRadii; Blur's width) and the Brush and Smudge never read it, so it starts
    // at none, as the other optional fields do
    bool wetDetail = true;  // and carries that blur's detail in its paint, so what's under shows through
    double thinKeep = 0.0;  // and it keeps what it carries longer over paint thinner than it: its stamps count
    // (1 - (carried - picked opacity))^this
    double shareByCeiling = 0.0;  // the brush colour's share is times the dab's ceiling, over Density, to this power: a lighter touch
    // lays less
    bool ceilingOnly = false;  // the Marker: a pixel's opacity is the most of what's there and the stroke's, so strokes don't build up
    double layRamp = 0.0;  // with layOnlyAbove, what it lays comes in over this much pressure above Blur Pressure, not at once
    bool layOnlyAbove = false;  // below Blur Pressure it lays no paint of its own, carried or picked up, and only melts what's under
    bool meltColourOnly = false;  // the melt changes what's under in colour only, keeping its opacity, so empty canvas nearby doesn't
    // thin paint
    int pickUpAcross = 8;  // places across the middle stamp it picks up from
    // Blur: it lays no colour, and moves what's under toward its blur, as far as blurStrength times the density share to
    // blurPower times the stamp's shape: above 0, blurBell makes that shape a bell of this share of the stamp's radius.
    int blurPasses = 1;  // passes of the box each way for the blur of what's under: 1 for Water Color and the Marker
    bool gaussian = false;  // Blur's: a Gaussian as wide as blurPasses passes of the box, which leaves no stripes flat
    double blurStrength = 0.0;
    double blurPower = 1.0;
    bool blurFollowsPressure = false;
    double blurBell = 0.0;
    double blurReachAtFull = 1.0;
    // Smudge: each stamp moves what's under it along its step by v = smudgeShare (at pressure kSmudgeShareAt, in proportion)
    // times (1 - d / reach)^smudgeFalloff, out to smudgeReach of the radius.
    double smudgeShare = 0.0;
    double smudgeReach = 0.0;
    double smudgeFalloff = 1.0;  // how much further the blur reaches at full pressure than from kBlurFirmFrom down
    bool keepsBodyColour = false;  // a stamp lighter than the stroke's firmest so far mixes its paint in only that much
    bool holdsColour = false;  // and a dab lighter than it lays a colour only that much changed from the dab before's
    double passLength = 0.0;  // above 0, soft edges add up over the stroke, a pass every this many outer radii of path
    double shareLag = 0.0;  // above 0, the share by ceiling follows the dabs' ceiling over this many stamps, so it lags pressure
    double meltRate = -1.0;  // what's under moves toward the blur by this times the dab's coverage; below 0, by its mixing
    double lightFuller = -1.0;  // above 0, stamps' fullness at light pressure: see StampTip::lightFuller
    bool evenAcross = false;  // what a stamp lays is evened out across the stroke: Water Color's flat body and crisp sides
    double wetHold = 0.0;  // Water Color's share of brush colour is (1 - mixing) to the power 1 + this times what it picked up's
    // opacity, so above 0 it lays less over paint. Only Water Color reads it, and it sets its own, kWaterHold,
    // 0; so it starts at none, as the other optional fields do
    // Above 0, from this pressure up to full Dilution comes to be times what dilutionBySamples gives at the dab's pen samples within a
    // stamp's diameter (Dab::samplesAcross), read straight between its points and held past its ends.
    double dilutionBySamplesFrom = 0.0;
    struct SamplesDilution {
        double samples = 0.0;
        double times = 1.0;
    };
    std::vector<SamplesDilution> dilutionBySamples;
    // A soft Shape's outline adds up along the path, as overlapping paint does, in place of taking the most any stamp gave a pixel: each
    // stamp lays outline at a pixel as the share of its step of path a straight pass needs to give the stamp's own side profile, so a
    // straight pass keeps it, while a start, an end and a crossing add up softly.
    bool outlineAlongPath = false;
    // Water Color's 5th Shape: what a dab lays has the opacity of what it carries, whatever Blending mixes into its colour, so each
    // stamp takes a pixel a share of the way toward one colour and one opacity; and it melts what's under only as pressure falls from full.
    bool depositsTowardPaint = false;
    double dilutionPower = 0.0;  // with it above 0, what it picks up has what's missing of its opacity raised by (1 - Dilution) to this
    double blendingPower = 0.0;  // with it above 0, the brush colour's share of a dab is (1 - Blending) to this, in place of the curve
    double meltFirmPower = 1.0;  // with depositsTowardPaint, the melt is times (1 - firmness from Blur Pressure) to this
    bool pickUpAtPen = false;  // it picks up under its pen sample's last stamp, where the pen is, not the dab's middle one
    double penReach = 0.0;  // with pickUpAtPen, at most this many stamp diameters ahead of the dab
    bool stampPerDab = false;  // each stamp is a dab of its own, so it picks up and mixes at every stamp
    bool pickUpEachStamp = false;  // a dab picks up at each of its stamps, what it carries following each
    bool meltsOwnPaint = false;  // the melt reads the layer as it shows and softens the stroke's own paint with what's under
    bool paintCeiling = false;  // what it picks up counts as paint at 1 - Dilution, or its own opacity if that's more, so passes
    // over thin paint build toward a ceiling rather than toward opaque, and opaque paint stays opaque
    double mixMemory = kMixMemory;  // the most density a pixel's paint counts as already holding when a dab mixes into it
    double fallMemory = -1.0;  // while pressure falls, mixMemory comes down toward this, all the way at fallRate. Below 0, it doesn't
    double fallRate = 0.003;  // pressure per pixel travelled, fitted to the reference's ramp over a field
    double firmLetGo =
        1.0;  // at full pressure persistenceStamps is this share of itself, straight from Blur Pressure up, so it lets go sooner
    bool transparentColour = false;  // painting with a colour with no alpha: it mixes toward transparent
    // The Brush, Water Color and the Marker: a dab's density, what it holds and its paint are kept on a grid of points, as far apart as the
    // tip's size says, and read between them, while the outline stays per pixel. The plain tools' stamps go on a grid the same way.
    bool onGrid = false;

    bool isOn() const { return kind != Kind::None; }
};

// How far apart a Marker's stamps go, as a share of their diameter: further the softer its edge.
double markerSpacingShare(int edge);
// The event-laid Marker's: a stamp's distance from the last, as a share of its diameter, by Shape; and the closest, px.
double markerEventSpacingShare(int edge);
// px: the closest the event-laid Marker's stamps go. The reference's 5th Shape at 50 px lays 2403 stamps on the C, about 2.7 px apart;
// 2.35 gives 2381 (2.0 2798, 2.5 2238, 3.0 1865). The 200 px C (1397 against 1318) doesn't reach it at any value tried.
inline constexpr double kMarkerEventLeastSpacing = 2.35;

// Whether a Brush or Water Color stroke is laid in groups, as the reference's: one mask, one pickup, one carry step and one
// laying a group, the mask each stamp's opacity times the reference's own dab. A see-through colour (Dilution), a tip shape and Keep Alpha
// keep the dab laying.
// Whether a tip's form keeps the Brush and Water Color off their groups (brushLaidInGroups). Every form does but a blot map on Water
// Color's 1st to 4th Shapes, whose groups take the map dab by dab: off them, its tone was half the reference's.
inline bool formKeepsOffGroups(const std::shared_ptr<const StampForm>& form, const TipMixing& mixing, int edge) {
    return form && !(form->fixedToCanvas() && mixing.kind == TipMixing::Kind::WaterColor && edge < kEdgePresets - 1);
}
inline bool brushLaidInGroups(const TipMixing& mixing, bool formed, LayMode mode) {
    return (mixing.kind == TipMixing::Kind::Brush || mixing.kind == TipMixing::Kind::WaterColor) && !mixing.transparentColour && !formed &&
           mode != LayMode::KeepAlpha;
}
// The Brush laid in groups: a stamp's distance from the last, as a share of its diameter, by Shape and Anti-Ripple.
double brushGroupSpacingShare(int edge, bool antiRipple);

// For tests: the grids' read-back a pixel at a time, without its four- and eight-at-once paths, to prove those give the same pixels.
void setGridOnePixelAtATime(bool on);

// How far from stamps[i]'s centre limitedMark gives it exactly paint 1 and shape 1: its deepReach where that mark is whole; below 0,
// nowhere.
double solidReach(const std::deque<Stamp>& stamps, std::size_t i);

// Red, green and blue times alpha, then alpha, 0 to 1 in the tiles' encoding.
using Premultiplied = std::array<float, 4>;

// Paints a mixing stroke a dab at a time, straight onto its own copy of the layer, so each dab picks up what's there, the stroke's own
// paint included. Each pixel the stroke reaches keeps two things apart: the stroke's coverage, which is what the stamps' density
// gives, as for any stamped stroke, and the stroke's own paint there, which each dab mixes into by as much as it lays. The pixel
// shows the stroke's paint over what was under it, at that coverage. So the colour mixes dab by dab, the shape is the same as a
// stroke that doesn't mix, and a pixel the stroke already covers fully still takes on new paint.
class DabPainter {
public:
    DabPainter(const StampTip& tip, const TipMixing& mixing, const StrokePaint& paint, const TiledImage& before, bool onWorkers);

    void begin(const InputSample& first, PressureShares shares);
    void add(const InputSample& sample, PressureShares shares);
    // The pen events' stamps, grouped into fewer dabs across events, for a stroke that has fallen behind: the same stamps at the
    // same places, each dab reaching a share of the stroke's width (kBehindDabWidthShare).
    void addTogether(const std::vector<std::pair<InputSample, PressureShares>>& samples);
    void end();
    // Called between dabs, for whoever shows the stroke: a wide dab is a good part of a frame by itself. Only finished dabs are shown.
    void setBetweenDabs(std::function<void()> betweenDabs) { m_betweenDabs = std::move(betweenDabs); }
    // Asked before each dab: while it says yes, what the stroke holds for tiles it hasn't touched for a while is compressed, so that a
    // long wide stroke fits in the memory there is. A tile the stroke comes back to is unpacked first, the same numbers.
    void setMemoryPressed(std::function<bool()> pressed) { m_pressed = std::move(pressed); }
    // The copy's tile, painted so far.
    TilePtr tile(TileCoord coord) const;
    // The stroke's outline at each pixel of that tile so far (shapeOf), for the paper to break its edge; null where the stroke
    // keeps none there (Blur, the Marker on its grid, a tile packed away).
    bool outline(TileCoord coord, float* out) const;
    QRect takeDirty();
    std::vector<TileCoord> takeChangedTiles();
    // What it's picked up and kept, for tests.
    const Premultiplied& carried() const { return m_carried; }

private:
    // One number per pixel of a tile, kept in an array of its own or, where it is the same everywhere, in one shared by every tile.
    template <typename T> class Column {
    public:
        T& operator[](std::size_t i) { return m_at[i]; }
        const T& operator[](std::size_t i) const { return m_at[i]; }
        T* begin() { return m_at; }
        T* end() { return m_at + Tile::kSize * Tile::kSize; }
        const T* begin() const { return m_at; }
        const T* end() const { return m_at + Tile::kSize * Tile::kSize; }
        const T* data() const { return m_at; }
        T* data() { return m_at; }
        bool owned() const { return m_own != nullptr; }
        void own() {
            m_own = std::make_unique<T[]>(Tile::kSize * Tile::kSize);
            m_at = m_own.get();
        }
        void share(const T* all) {
            m_own.reset();
            m_at = const_cast<T*>(all);
        }

    private:
        std::unique_ptr<T[]> m_own;
        T* m_at = nullptr;
    };
    // Each pixel the stroke has reached: its density so far, times shape, and the most shape it's had, or for the Marker the opacity
    // it's given and how strongly its colour shows; the stroke's own paint there, how much of that it holds, and what was under the
    // stroke, premultiplied. A stroke that melts what's under changes it, so keeps it as numbers; one that doesn't, the Brush, only
    // ever reads it as the pixel was, so keeps the pixel, a quarter the size, and reads it as the same numbers (A9).
    struct Reach {
        Reach(bool keepsUnderAsPixels, bool usesShape, bool usesPassed, const Tile* before, bool underStaysEmpty, int gridSpacing = 0,
              int markerSpacing = 0);
        Column<float> value;
        Column<float> shape;
        Column<float> held;
        Column<float> passed;  // only a tool with passLength has these
        Column<Premultiplied> paint;
        std::uint32_t lastDab = 0;  // the number of the last dab that laid here
        bool allReached = false;  // every pixel has been reached before, so none will read the tile for what's under
        Column<Premultiplied> under;  // none with keepsUnderAsPixels: it's what the layer had before the stroke. Shared and all empty where
        // the layer had nothing and nothing has melted in
        bool keepsUnderAsPixels = false;
        const Tile* before = nullptr;  // that tile, if the layer had one; the layer as it was holds it for the whole stroke
        // On a grid (onGrid): the density, what it holds and the paint, at every gridCell-th pixel of the tile, in place of value, held and
        // paint per pixel, the density as a pixel's own record counts it, times the outline's share. Shape and passed stay per pixel.
        std::vector<float> gridValue, gridHeld, gridCover;  // gridCover: the coverage curve at gridValue, before the outline
        std::vector<Premultiplied> gridPaint;
        std::vector<float> gridLaid;  // what the last dab to reach a point laid there, and that dab's number
        std::vector<std::uint32_t> gridDab;
        std::vector<char> gridWhole;  // the point has been inside a stamp's whole outline
        std::vector<float> gridMelt;  // Water Color: how far what's under melts at a point, by the last dab to reach it
        int gridCell = 0;  // the grid's spacing this record is for, 0 for the per-pixel model
        // Per pixel: what its density differs by from the grid's, where a stamp's edge is sharper than it. All 0, shared, until a pixel has
        // some: then made, once, though several workers lay a tile's rows at once.
        class FineValues {
        public:
            float operator[](std::size_t i) const { return data()[i]; }
            const float* data() const { return m_at.load(std::memory_order_acquire); }
            void share(const float* zeros) { m_at.store(zeros, std::memory_order_release); }
            // What a pixel's write goes to: its own array, made the first time a pixel of the tile needs it.
            float* own() {
                if (float* made = m_made.load(std::memory_order_acquire))
                    return made;
                std::scoped_lock lock(m_making);
                if (!m_own) {
                    m_own = std::make_unique<float[]>(Tile::kSize * Tile::kSize);
                    m_at.store(m_own.get(), std::memory_order_release);
                    m_made.store(m_own.get(), std::memory_order_release);
                }
                return m_own.get();
            }

        private:
            std::atomic<const float*> m_at{nullptr};
            std::atomic<float*> m_made{nullptr};
            std::unique_ptr<float[]> m_own;
            std::mutex m_making;
        };
        FineValues fineValue;
        // The Marker on a grid (markerCell px apart), in place of value, shape, held and paint per pixel: its points' records,
        // each laid as a pixel's is, and the pixels between read between them, except in a cell worked out per pixel, which keeps a
        // record for each of its pixels but its point. Each point keeps what it was before the last dab that laid it, and that dab's
        // number. What's under stays per pixel.
        struct FineCell {
            std::vector<float> value, shape, held;
            std::vector<Premultiplied> paint;
        };
        int markerCell = 0;
        std::vector<float> pointValue, pointShape, pointHeld;
        std::vector<Premultiplied> pointPaint;
        std::vector<float> middleValue, middleShape,
            middleHeld;  // and each cell's middle pixel, laid as a point is, to check the reading by
        std::vector<Premultiplied> middlePaint;
        std::vector<std::unique_ptr<FineCell>> fineCells;
        std::vector<std::uint32_t> markerLaidDab;
        std::vector<float> beforeValue, beforeShape, beforeHeld;
        std::vector<Premultiplied> beforePaint;

        Premultiplied underAt(std::size_t index) const;
        void keepUnder(std::size_t index, const Channel* px);
    };
    bool keepsUnderAsPixels() const;
    bool underCanStayEmpty(TileCoord coord) const;
    bool blurHasPaint() const;
    struct Dab {
        std::size_t first = 0;
        std::size_t count = 0;
        QRect area;
        double pressure = 1.0;
        double ceiling = 1.0;  // its stamps' opacity, on average
        double steps = 0.0;  // its stamps' weights added up: its count, unless they went closer
        double samplesAcross = 0.0;  // its stamps' Stamp::samplesAcross, on average over those that have one; 0 if none does
        std::size_t pen = 0;  // its pen sample's last stamp, counted from its first: where pickUpAtPen picks up
    };
    struct Laying {
        TileCoord coord;
        Tile* tile = nullptr;
        Reach* reach = nullptr;
    };

    // `ending`: the stroke's last stamps, after which no dab waits for more (StampTip::skirtNeighbours).
    void paint(const std::vector<Stamp>& stamps, std::size_t most, bool ending = false);
    // The dab of `count` stamps from m_stamps[first], which are placed already, worked out and painted.
    void layDab(std::size_t first, std::size_t count, std::size_t pen);
    std::size_t stampsPerDab() const;
    bool dabPerEvent() const;
    bool laysBrushGroups() const;
    // The Brush laid a group at a time, as the reference's: its mask, its pickup over it, what it carries, and its laying.
    void layBrushGroup(const Dab& dab);
    Premultiplied brushGroupPaintFor(const Premultiplied& picked, const Dab& dab);
    Premultiplied waterGroupMix(const Premultiplied& picked, const Dab& dab, double& share) const;
    double pastBlurPressure(double pressure) const;
    std::size_t groupStamps() const;
    double spacingRatio(const Stamp& stamp) const;
    // The Marker laid an event at a time through one mask, as the reference's (stage 3).
    bool laysEvents() const;
    void layEvent(const Dab& dab, const Premultiplied& paint);
    std::size_t stampsPerDabWhenBehind() const;
    void paintDab(const Dab& dab);
    // Over a band of a tile, the stamps of the dab that can reach it, in the dab's order, and which of those are whole over all of it.
    struct BandStamp {
        std::uint16_t k = 0;
        bool whole = false;
        bool unevened = true;  // it lays as much across the band as without evening (unevenedOver)
    };
    // `withPasses`: whether the mark's passes are added up; the grid's points and its exact marks don't read them.
    // `oneStroke`, for a shape laid as one stroke (StampForm::oneStroke): what its stamps add to the stroke's coverage there,
    // oneStrokeDensity times oneStrokeOutline each, where the mark's paint is what they lay as ever, which mixes and melts by it.
    StampMark dabMark(const Dab& dab, int x, int y, const std::vector<BandStamp>* band = nullptr, bool withPasses = true,
                      float* oneStroke = nullptr) const;
    // With a skirt, the density the dab's stamps' skirts add at (x, y) past what their outlines lay, each times the most of the outlines
    // of the stamps around it on the path (StampTip::skirtNeighbours). `outline` is raised to that most where a skirt adds anything, as
    // the pixel's outline will be once those stamps are laid, so the skirt shows from now.
    // With `outlineOnly`, only the outline: it stops once that's whole, and what it returns isn't the density.
    // `distancesSquared`, if given: each band stamp's distance squared from (x, y), below 0 for one whole over the band (dabMark's).
    float skirtAt(const Dab& dab, int x, int y, const std::vector<BandStamp>* band, float& outline, bool outlineOnly = false,
                  const float* distancesSquared = nullptr) const;
    // Whether every pixel of the rect has its outline raised whole by one of the band's stamps' skirts: the rect lies inside that stamp's
    // reach and wholly inside the outline of a stamp around it.
    bool skirtRaisesWhole(const Dab& dab, const std::vector<BandStamp>& band, const QRect& rect) const;
    // The outline of m_stamps[i], one of the stamps around the dab, at (x, y).
    float outlineAround(std::size_t i, int x, int y) const;
    // Whether markEight can stand for dabMark over a band, and dabMark, without passes, at eight pixels of a row from x, `stride` px apart:
    // neighbouring pixels or a row's grid points.
    bool marksInLanes(const std::vector<BandStamp>& band) const;
    // `cutWhole`, if given: for each band stamp, its skirt's cut is whole over the band (cutsWholeOver).
    void markEight(const Dab& dab, int x, int y, const std::vector<BandStamp>& band, StampMark* out, int stride = 1,
                   const char* cutWhole = nullptr) const;
    std::vector<char> cutsWholeOver(const Dab& dab, const std::vector<BandStamp>& band, const QRect& rect) const;
    // Marks the pixels of row y, `left` to `right`, where a skirted stamp's mark is sharper than the grid can follow.
    void markSkirtedEdges(const Dab& dab, const BandStamp& band, int y, int left, int right, std::vector<char>& sharp) const;
    Premultiplied pickUp(const Dab& dab) const;
    Premultiplied pickUpAt(const Dab& dab, std::size_t k) const;
    template <typename Read> Premultiplied averageUnder(const Dab& dab, Read read) const;
    template <typename Read> Premultiplied averageOverMask(const Dab& dab, Read read) const;
    static void addPicked(Premultiplied& total, float& weight, float w, const Premultiplied& under);
    Premultiplied paintFor(const Premultiplied& picked, const Dab& dab);
    Premultiplied markerPaintFor(const Premultiplied& picked, const Dab& dab);
    Premultiplied asPaint(const Premultiplied& picked, const Dab& dab) const;
    Premultiplied movedPaint(const Premultiplied& picked) const;
    Premultiplied carriedColour(const Premultiplied& picked) const;
    double persistenceSoFar() const;
    double persistenceStampsAt(double pressure) const;
    float paintShare(float held, float laid) const;
    float meltOf(const StampMark& mark) const;
    double memoryFor(const Dab& dab) const;
    double mixingAt(double pressure) const;
    double brushShare(double pressure, double pickedAlpha) const;
    double colourFadeIn(double pressure) const;
    double dilutionBySamplesAt(const Dab& dab) const;
    bool onlyMoving(double pressure) const;
    double layingAt(double pressure) const;
    // What a Marker's whole stamps did to the last pixel's opacity and strength, kept for neighbours that start the same.
    struct MarkerClimb {
        bool valid = false;
        std::uint32_t inOpacity = 0, inStrength = 0;
        float outOpacity = 0.0f, outStrength = 0.0f, laid = 0.0f;
    };
    void layMarkerPixel(const Dab& dab, const Premultiplied& paint, Laying& laying, int x, int y, MarkerClimb* climb,
                        const std::vector<BandStamp>& band) const;
    // A Marker pixel's own numbers, wherever they're kept: its opacity, colour strength, how much paint it holds and the paint.
    struct MarkerRecord {
        float& value;
        float& shape;
        float& held;
        Premultiplied& paint;
    };
    void layMarkerAt(const Dab& dab, const Premultiplied& paint, Laying& laying, std::size_t index, MarkerRecord record, int x, int y,
                     MarkerClimb* climb, const std::vector<BandStamp>& band) const;
    void prepareMarkerStamps(const Dab& dab);
    void layBlurPixel(const Dab& dab, Laying& laying, int x, int y) const;
    void smudgeDab(const Dab& dab);
    double blurReachAt(double pressure) const;
    void mixInto(Reach& reach, std::size_t index, const Premultiplied& paint, float laid, float heldBefore) const;
    // A formed stamp's mark at (dx, dy) from its centre: its round mark's outline Strength of the way to the picture, and its paint
    // Strength of the way to all of it, as a formed stamp tool's (StampLayer::formedMarkOf).
    StampMark formedMark(const Stamp& stamp, float dx, float dy) const;
    // The picture's share of a formed stamp at (dx, dy): 1 inside the round stamp, Strength of the way to the picture; 1 without a form.
    StampForm::BlotWindow blotWindow() const;
    float formShapeAt(const Stamp& stamp, float dx, float dy) const;
    // A block of the layer's pixels, as numbers. Its values may hold more than its area: a block made from one that's done with keeps
    // that one's storage, and only the area's values are read or written.
    struct Pixels {
        QRect area;
        std::vector<Premultiplied> values;
        Premultiplied& at(int x, int y);
        const Premultiplied& at(int x, int y) const;
    };
    Pixels blurUnder(const Dab& dab, double radii) const;
    void soften(Reach& reach, std::size_t index, const Premultiplied& blurred, float cover) const;
    void softenUnder(Premultiplied& under, const Premultiplied& blurred, float cover) const;
    Premultiplied withWhatsUnder(const Premultiplied& paint, const Premultiplied& blurred, const Premultiplied& wide) const;
    static Premultiplied wholeColour(Premultiplied colour);
    Pixels readAround(const QRect& area) const;
    // The pixel as the stroke shows it: on a textured Brush, what the merge lays back through the paper, which is what it picks up;
    // otherwise its copy's.
    Premultiplied shownAt(int x, int y) const;
    void readRow(Pixels& pixels, int y) const;
    static Premultiplied underAt(const Tile* tile, const Reach* reach, int tx, int ty, int x, int y);
    Premultiplied blurSourceAt(const Tile* tile, const Reach* reach, int tx, int ty, int x, int y) const;
    // A block of `area`, its values not yet set: every caller writes them all. Its storage is a done block's, if there is one.
    Pixels sized(const QRect& area) const;
    // A block that's done with gives its storage to the next one, so a dab never waits for tens of megabytes to be given and cleared.
    void recycle(Pixels&& pixels) const;
    void recycle(std::optional<Pixels>& pixels) const;
    void boxBlur(const Pixels& from, Pixels& to, int reach, bool across) const;
    static void blurLine(const Pixels& from, Pixels& to, int reach, bool across, int at);
    void gaussianBlur(const Pixels& from, Pixels& to, double sigma, bool across) const;
    static void gaussianLine(const Pixels& from, Pixels& to, double sigma, bool across, int at);
    // The tiles of `area`; with `onlyReached`, only those one of the dab's stamps reaches (prepareOutlines first). Given `dab`, a shared
    // tile it writes whole starts from an unset one, not a copy (prepare its stamps first).
    std::vector<Laying> layingsFor(const QRect& area, bool onlyReached = false, const Dab* dab = nullptr);
    bool writesWhole(const Dab& dab, TileCoord coord, Reach& reach) const;
    bool reaches(const QRect& tile) const;
    void layInTile(const Dab& dab, const Premultiplied& paint, Laying& laying) const;
    void layRows(const Dab& dab, const Premultiplied& paint, Laying& laying, int top, int bottom) const;
    void stampsOverBand(const Dab& dab, const QRect& band, std::vector<BandStamp>& out) const;
    std::optional<StampMark> wholeDabMark(const Dab& dab) const;
    void layOnWorkers(const Dab& dab, const Premultiplied& paint, std::vector<Laying>& layings) const;
    void layPixel(const Dab& dab, const Premultiplied& paint, Laying& laying, int x, int y, const StampMark* wholeMark, MarkerClimb* climb,
                  const std::vector<BandStamp>& band) const;
    // On a grid (onGrid): the dab's mixing at the grid's points, then each pixel's outline and what it shows, read between them. On a grid a
    // pixel apart every pixel is a point, and layPointRows does both at once, as layPixel does.
    void layOnGrid(const Dab& dab, const Premultiplied& paint, std::vector<Laying>& layings);
    void layMarkerOnGrid(const Dab& dab, const Premultiplied& paint, std::vector<Laying>& layings);
    bool nothingUnder(const Dab& dab) const;
    // ReachedOnRow over the dab's area, a row at a time from m_markerRowsTop, worked out once a dab for every tile.
    std::vector<std::pair<int, int>> m_markerRows;
    int m_markerRowsTop = 0;
    std::pair<int, int> markerRow(const QRect& area, int y) const;
    void layMarkerPoints(const Dab& dab, const Premultiplied& paint, Laying& laying, int top, int bottom) const;
    void showMarkerCells(const Dab& dab, const Premultiplied& paint, Laying& laying, int top, int bottom) const;
    void layGridRows(const Dab& dab, const Premultiplied& paint, Laying& laying, int top, int bottom) const;
    void layPointRows(const Dab& dab, const Premultiplied& paint, Laying& laying, int top, int bottom) const;
    void showGridRows(const Dab& dab, const Premultiplied& paint, Laying& laying, int top, int bottom) const;
    // A column of the grid at a row: its two points read down to the row, each counting only if the stroke has reached it.
    struct GridColumn {
        float weight = 0.0f, cover = 0.0f, value = 0.0f, laid = 0.0f, melt = 0.0f;  // melt: Water Color's, this dab's
        Premultiplied kept{};
        bool whole = true;  // its reached points' outlines are whole, so their coverage curve can be read between them
    };
    bool showFour(Laying& laying, const std::vector<GridColumn>& columns, const std::vector<char>& sharp, int fromLeft, std::size_t index,
                  int intoTile) const;
    // The same for four pixels of a soft Shape's outline adding up along the path, where the outline isn't whole: each works out its
    // outline's share and its curve from the density there, as the scalar read-back does, in the same steps.
    bool showFourSoft(Laying& laying, const std::vector<GridColumn>& columns, const std::vector<char>& sharp, int fromLeft,
                      std::size_t index, int intoTile) const;
    // The same for eight pixels, with AVX2, on a grid at least eight apart; chosen only on a processor that has it.
    bool showEight(Laying& laying, const std::vector<GridColumn>& columns, const std::vector<char>& sharp, int fromLeft, std::size_t index,
                   int intoTile) const;
    // Water Color's: what's under is its own numbers, melted toward `blurred` (eight pixels' blur, or null where nothing melts) first.
    bool showEightWet(Laying& laying, const std::vector<GridColumn>& columns, const std::vector<char>& sharp, int fromLeft,
                      std::size_t index, int intoTile, const Premultiplied* blurred) const;
    float shapeOf(const Reach& reach, std::size_t index) const;
    float coverageOf(float value, float shape) const;
    Premultiplied shown(const Premultiplied& under, const Premultiplied& paint, float coverage) const;
    Reach* reachAt(TileCoord coord) const;
    void shareSettledColumns(std::vector<Laying>& layings) const;
    void packIdleReach(const Dab& dab);
    void unpackAround(const Dab& dab);
    int readsAround(const Dab& dab) const;  // how many pixels past a dab its blur of what's under can read what the stroke has laid
    QByteArray packed(const Reach& reach) const;
    std::unique_ptr<Reach> unpacked(const QByteArray& bytes, TileCoord coord) const;
    void forgetOldStamps();

    StampTip m_tip;
    TipMixing m_mixing;
    StrokePaint m_paint;
    float m_fullFrom;  // density over shape from which coverage is full (fullCoverageFrom)
    int m_markerCell = 0;  // px between the Marker's grid points (onGrid, from kGridTwoFrom); 0 for the per-pixel model
    int m_gridCell = 0;  // px between the Brush's grid points when it paints on a grid (onGrid); 0 for the per-pixel model
    std::vector<float> m_wholeOutline;  // and within this, squared, of each of the dab's stamps, its outline is whole
    bool m_onWorkers;
    std::function<void()> m_betweenDabs;
    StampWalk m_walk;
    TiledImage m_layer;
    TiledImage m_before;  // the layer as it was, sharing its tiles
    std::unordered_map<TileCoord, std::unique_ptr<Reach>, TileCoordHash> m_reach;
    // laysEvents: at each pixel the stroke has reached, the most opacity it's given and the highest top of the stamps that reached it.
    // One value for the whole tile until a pixel needs its own (spread).
    // The event-laid Marker's top at each pixel of a tile, the highest of the stamps' that reached it, never falling: one value
    // while the whole tile shares it.
    struct EventMemory {
        bool uniform = true;
        float uniformTop = 0.0f;
        std::unique_ptr<float[]> top;
        void spread() {
            if (!uniform)
                return;
            // Filled below, so not cleared first.
            top = std::make_unique_for_overwrite<float[]>(Tile::kSize * Tile::kSize);
            std::fill_n(top.get(), Tile::kSize * Tile::kSize, uniformTop);
            uniform = false;
        }
    };
    std::unordered_map<TileCoord, std::unique_ptr<EventMemory>, TileCoordHash> m_eventMemory;
    // Laid in groups on paper with an edge bar: each pixel's outline, the most of its groups' edge there, for outline().
    std::unordered_map<TileCoord, std::vector<float>, TileCoordHash> m_groupOutline;
    std::unordered_map<TileCoord, QByteArray, TileCoordHash> m_packed;  // the same, of tiles left alone a while
    std::function<bool()> m_pressed;
    bool m_wetHasPaint = false;  // the blur of what's under this dab is somewhere not nothing, so what's under a tile may stop being empty
    std::uint32_t m_dabNumber = 0;
    std::deque<Stamp> m_stamps;  // the last few painted, as neighbours, then those being painted
    std::size_t m_painted = 0;  // how many of m_stamps are painted
    std::deque<std::pair<std::size_t, std::size_t>>
        m_waitingDabs;  // with a skirt: the stamps in each dab placed but not painted yet, in order, and its Dab::pen
    // With a skirt: each stamp's place and outline from skirtNeighbours before the dab being painted to as many after it, for the
    // outlines its stamps' skirts show inside, from m_stamps[m_aroundFirst] on.
    struct AroundStamp {
        PixelFrame frame;
        float wholeSquared = -1.0f;  // within this, squared, its outline is whole
        float outerSquared = 0.0f;  // and from this far it's none
    };
    std::vector<AroundStamp> m_around;
    std::size_t m_aroundFirst = 0;
    Premultiplied m_own{};
    std::optional<Pixels> m_wet;  // Water Color: what's under the dab being painted, blurred
    float m_wetness = 0.0f;  // and how much of that blur it mixes in
    float m_melt = 0.0f;  // and how fast what's under moves toward it
    Premultiplied m_picked{};  // what the dab being painted picked up
    // The Marker's pickup, once a pen event before it lays anything: equal weights over every pixel its stamps' cut mask touches.
    std::optional<Premultiplied> m_eventPicked;
    Premultiplied pickUpOverEvent(const std::vector<Stamp>& stamps) const;
    std::optional<Pixels> m_wide;  // and blurred wider, so the detail it carries is the difference
    mutable std::vector<std::vector<Premultiplied>> m_spare;  // the storage of blocks that are done with, for the next ones
    // What each of a Marker dab's stamps lays, worked out once for the dab rather than at every pixel: the same numbers. Within
    // solidSquared of its centre, squared, its mark is whole, so what it lays there is the same at every pixel too.
    struct MarkerStamp {
        PixelFrame frame;
        float opacity = 1.0f;
        float weight = 1.0f;
        float solidSquared = -1.0f;
        float outsideSquared = 0.0f;  // and from this far, squared, it lays nothing
        float climb = 0.0f;
        float colourWhole = 0.0f;
        float laidWhole = 0.0f;
        float body = 1.0f;
    };
    std::vector<MarkerStamp> m_markerStamps;
    // The same for the other mixing tools: where a stamp's mark is whole, what it adds to a pixel's density and passes.
    struct WholeStamp {
        PixelFrame frame;
        float solidSquared = -1.0f;
        float outsideSquared = 0.0f;  // from this far, squared, the stamp's outline lays nothing
        float reachSquared = 0.0f;  // and its skirt, if it has one, nothing from this far
        float paint = 0.0f;
        float passed = 0.0f;
        const std::vector<float>* along = nullptr;  // with outlineAlongPath, its outline along the path (alongTable), times its radius
    };
    std::vector<WholeStamp> m_wholeStamps;
    // With outlineAlongPath: for a stamp whose outline ramps over `ratio` of its radius, the outline it lays along the path at each of
    // kAlongSamples places across its ramp, times its radius, kept by ratio.
    std::map<int, std::vector<float>> m_alongTables;
    const std::vector<float>& alongTable(double ratio);
    // What a stamp adds to a pixel's passes `distance` from its centre, where its outline is `shape`, and what the pixel's most keeps:
    // with outlineAlongPath, nothing, as the passes are all of it.
    float passOf(const Stamp& stamp, const WholeStamp& whole, float distance, float shape) const;
    float mostOf(float shape) const { return m_mixing.outlineAlongPath ? 0.0f : shape; }
    // With outlineAlongPath, a pixel whose passes come to kWholeAlongShape counts as whole.
    void addPasses(Reach& reach, std::size_t index, float passes) const;
    void prepareWholeStamps(const Dab& dab);
    float m_meltAllWhole = 0.0f;
    float m_blurPressed =
        0.0f;  // Blur: its strength at the dab's pressure, before its cover  // how far what's under melts where every stamp of the dab is whole
    // A wide Marker's blur of what's under a dab, worked out on a grid of cells and read between them, as a blur that wide is smooth at
    // that scale: a dab's melt then costs far less than the pixels it covers.
    struct CoarseBlur {
        Pixels cells;  // the blur at each cell's centre; its area is in cells
        int cell = 1;  // px across a cell
        // For each column and row of the dab: the cell left of or above it and how far it is toward the next, worked out once
        // rather than at every pixel, the same numbers.
        struct Between {
            int first = 0;
            float share = 0.0f;
        };
        QRect dab;
        std::vector<Between> columns, rows;
        Premultiplied at(int x, int y) const;  // at a pixel of the dab
        Between between(int v, int firstCell, int lastCell) const;
    };
    std::optional<CoarseBlur> m_coarseWet;
    std::optional<CoarseBlur> m_coarseWide;  // Water Color's wider blur, on cells, for the detail it carries
    int coarseCellFor(const Dab& dab, double radii) const;
    CoarseBlur coarseBlurUnder(const Dab& dab, double radii) const;
    // The tile a cell's place falls in, and its Reach: the cells along a row read the same tile many times over.
    struct TileUnder {
        int tx = 0, ty = 0;
        bool found = false;
        const Tile* tile = nullptr;
        const Reach* reach = nullptr;
    };
    const TileUnder& tileUnder(int tx, int ty, TileUnder& last) const;
    Premultiplied cellAverage(int cx, int cy, int cell, TileUnder& last) const;
    // Each of the dab's stamps' centre and how far out it lays anything: a row's pixels past them all are left as they are.
    struct Outline {
        QPointF centre;
        double outer = 0.0;
        QPointF cutCentre;  // with cutOuter at 0 or more, only where this disc reaches too
        double cutOuter = -1.0;
        // The part of the row whose middle is at `y` this reaches, `spare` px further out all round; false if none.
        bool onRow(double y, double spare, double& left, double& right) const;
    };
    std::vector<Outline> m_outlines;
    // Where the dab lays anything at all, outline or skirt: each stamp's outline, and with a skirt its reach cut to each outline around
    // it. Past these its marks are none, so the work that would find that out is skipped there.
    std::vector<Outline> m_layOutlines;
    void prepareOutlines(const Dab& dab);
    // The pixels of row y, a pixel to spare, where the dab may lay anything (m_layOutlines); empty if none.
    std::pair<int, int> laysOnRow(int y) const;
    // On a grid from kGridTwoFrom, each row of the dab's area worked out once for every tile it crosses, from m_rowsTop: the part the stamps
    // reach (reachedOnRow's, before it's held to a tile), the part where what a changed point shows may be read, and where the dab lays
    // anything (laysOnRow's). The same numbers each tile would work out for itself.
    struct RowReach {
        double reachLeft = 1e300, reachRight = -1e300;
        double readLeft = 1e300, readRight = -1e300;
        int laysFrom = 0, laysTo = -1;
    };
    std::vector<RowReach> m_rows;
    int m_rowsTop = 0;
    void prepareRows(const Dab& dab, float widen);
    // The part of row `y` of `area` that any of the dab's stamps can reach, a pixel to spare; empty if none can.
    std::pair<int, int> reachedOnRow(const QRect& area, int y) const;
    float m_dabShare = 0.0f;  // and its share of brush colour
    Premultiplied m_carried{};  // what it's picked up and kept
    double m_lastDabPressure = -1.0;  // the dab before's, below 0 at the start
    QPointF m_lastDabCentre;
    float m_dabMemory = static_cast<float>(kMixMemory);  // mixMemory for the dab being painted
    double m_stampsCarried = 0.0;  // so far in the stroke, counted at the Brush's spacing: none means the next dab loads it
    double m_recentStamps = 1.0;
    // Laid in groups: the groups' size, which moves only when the average of recent events' stamps is more than a whole stamp away
    std::size_t m_groupSize = 1;
    double m_groupAverage = 1.0;
    void paintGroups(const std::vector<Stamp>& stamps, bool ending);
    std::size_t groupMost() const;  // how many stamps recent events have brought, followed by kMarkerRecentFollow
    QPointF m_lastCentre;  // Smudge: the last stamp's centre, for its step
    std::vector<Premultiplied> m_smudgeNext;  // and what each pixel of a stamp's square moves to, kept from stamp to stamp
    std::vector<float> m_smudgeGrid;  // how far back each grid point reads, at large sizes
    std::vector<char> m_smudgeMoved;  // and whether it moves: all 0 between stamps
    bool m_hasLastCentre = false;
    double m_firmest = 0.0;  // the most opacity a stamp has had so far in the stroke
    Premultiplied m_lastLaid{};  // the paint the dab before laid, with holdsColour
    bool m_hasLaid = false;
    double m_laggingCeiling = -1.0;  // the dab ceiling the brush colour's share follows, with shareLag; below 0, none yet
    QRect m_dirty;
    std::unordered_set<TileCoord, TileCoordHash> m_changed;
};

}
