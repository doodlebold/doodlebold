#pragma once

#include "core/raster/TiledImage.h"
#include "core/selection/SelectionMask.h"
#include "core/stroke/PaperGrain.h"
#include "core/stroke/StrokeBuffer.h"

#include <array>
#include <cmath>
#include <functional>
#include <memory>
#include <vector>

// docs/brush-engine/stroke-merge.md says what the named values here are for and how they were chosen.
namespace doodlebold {

// How paint meets what's already on a pixel: laid over it, taking it away, recolouring it with its alpha left alone, or not at
// all (an eraser can't take anything away while alpha is preserved).
enum class LayMode {
    Over,
    Erase,
    KeepAlpha,
    Untouched
};

// std::lround's integer, halves away from 0, without the library call: a float's whole part and what's left over are exact, as is
// comparing that with a half. The same as std::lround for every float of magnitude up to 2^17, which the merge's channels never pass
// (a unit test goes through them all).
inline long roundToNearest(float value) {
    const float size = std::abs(value);
    const long whole = static_cast<long>(size);
    const long rounded = size - static_cast<float>(whole) >= 0.5f ? whole + 1 : whole;
    return value < 0.0f ? -rounded : rounded;
}

// What a stroke's density turns into: a straight colour, 0 to 1 in the tiles' encoding, and the most coverage it can reach.
struct StrokePaint {
    std::array<float, 3> color{0.0f, 0.0f, 0.0f};
    float opacity = 1.0f;
    LayMode mode = LayMode::Over;
    float fullAt = 1.0f;  // the point on 1 - exp(-density) that counts as full coverage: lower keeps soft edges soft
    // The Clone Stamp: in place of the colour, each pixel lays the pixel of this picture that is `sourceOffset` back from it.
    std::shared_ptr<const TiledImage> source{};
    QPoint sourceOffset{};
    // A texture: each pixel shows this share of the stroke, beside a selection's.
    std::shared_ptr<const PaperGrain> grain{};
    // How soft the tip's edge is, for the paper to break it into its tops: 1 the softest Shape, 0 a hard edge or a form.
    float softEdge = 0.0f;
    // On a firmer soft Shape, how far below the soft bar its hard part fades out, times the bar.
    float softHardDepth = 1.0f;
    // A hard Shape's edge, without a form, which the paper breaks into its tops too.
    bool hardEdge = false;
    // A mixing tool's painted tile is a coat the paper thins as a stamp tool's: the Brush. Blur's goes the paper's share of
    // the way, as before.
    bool thinsCoat = false;
    // A mixing tool whose stroke is a whole coat through the paper (the grain's wholeCoat): its painted tile goes the grain's
    // share of its way times this, from `wholeCoatClear` where nothing was under it to `wholeCoatOver` where the layer was whole, by
    // the alpha before. 0 for the others.
    float wholeCoatClear = 0.0f;
    float wholeCoatOver = 0.0f;
    float wholeCoatDensity = 0.0f;  // the whole coat's density against the paper's share (wholeCoatThroughPaper), kept with the grain
    // The Brush (thinsCoat), where its alpha can't say how much it coated: a whole coat through the paper, and on its edge a
    // bar the paper's tops alone take.
    float uncoatedDensity = 0.0f;
    float uncoatedTop = 1.0f;
    float edgeBarDepth = 0.0f;
    float edgeBarWidth = 1.0f;
    // Scratch Prs.: above 0, how deep a light coat's bar into the paper reaches at the lightest touch, kScratchDepth times Scratch Prs.
    // to kScratchPower.
    float scratchDepth = 0.0f;
    // It picks up what's under it as the merge shows it, through the paper (DabPainter::shownAt).
    bool picksUpThroughPaper = false;
    // Trial: the paper is cut into each dab's coverage as it's laid, before its opacity, so the merge adds none.
    bool paperPerDab = false;
};

// How much of a pixel the paint hides. Density here is an average across the pixel, and only `shape` of the pixel holds any
// of it: opacity · shape · min(1, (1 - exp(-density / shape)) / fullAt).
float coverageOf(float density, float shape, float opacity, float fullAt = 1.0f);
// The least density over shape from which coverageOf's min(1, …) is 1, so that coverage is opacity · shape and expm1 needn't be
// worked out: infinite where it never is. In floats, as the merge works it out, and in doubles, as a mixing stroke does.
float fullCoverageFrom(float fullAt);
double fullCoverageFromInDoubles(float fullAt);

// Lays one tile's density on `tile`. A selection tile, if given, scales the coverage pixel by pixel.
void mergeDensityTile(const DensityTile& density, const StrokePaint& paint, Tile& tile, const SelectionTile* selection = nullptr,
                      TileCoord coord = {});

// A tile being worked out afresh from how it was before the stroke.
struct TileMergeJob {
    TileCoord coord;
    const DensityTile* density = nullptr;
    const SelectionTile* selected = nullptr;
    TilePtr before;
    TilePtr fresh;
    // A mixing stroke's tile, painted already: it's shown as it is, within the selection. Without one the tile itself is shown, not
    // a copy: the stroke copies a tile that's shared before it paints on it again.
    TilePtr painted;
    // A plain grid's tile as it was last merged, if it was. Then only its cells changed since are worked out again.
    TilePtr previous{};
    // Fills in a mixing stroke's outline at each pixel of the tile, for the paper to break its edge (StrokePaint::edgeBarDepth), on the
    // worker that merges the tile; false where the stroke keeps none there. Without one the outline counts as whole.
    std::function<bool(float*)> outlineOf{};
};

// The most paint density a coat holds against the paper, as -ln(1 - coverage): a full coat keeps the paper's darkest pits open, as the
// reference's does. Fitted to its textured Pencil (ramp-full, ramp-pressure, ramp-density40 on Doodlebold's own
// plain coverage): 7.5 to 9 alike; the Pencil's own full coat is about 6 and never reaches it.
inline constexpr float kPaperFullDensity = 8.0f;
// Erasing through the paper, a coat counts as full from this density.
inline constexpr float kPaperFullDensityErasing = 6.0f;
// Erasing through a full coat, the paper bites by this share of the density the stamps laid there, summed.
inline constexpr float kErasingDensityShare = 0.9f;

// How far a mixing tool's painted pixel goes from the one before it through the paper, the paper's share there given, its alpha a0
// before and a1 painted, and the stroke's outline there. The merge lays a painted tile back by it, and the
// Brush picks up what's under it through it, as it's shown.
float mixingWayThroughPaper(const StrokePaint& paint, float share, float a0, float a1, float outline);
// mixingWayThroughPaper for `count` pixels of the tile at `coord` from its pixel `index` on, along a row, with no outline (1): their alphas
// before (`a0`) and painted (`a1`) in, their ways in `ways`. Four at a time, as the merge lays a tile back, the same floats.
void mixingWaysThroughPaper(const StrokePaint& paint, TileCoord coord, int index, const float* a0, const float* a1, int count, float* ways);
// A whole coat of `density` through the paper's share: 1 - exp(-density · share), as the merge works it out.
float wholeCoatThroughPaper(float share, float density);

// What a plain grid's tile would reduce to (Compositor::reduceTile) once merged over `before`, without merging it: each pixel's
// coverage as the merge reads it, laid over the pixel as before in floats, and averaged over blocks of 2^level pixels a side.
void reduceGridTile(const DensityTile& density, const StrokePaint& paint, const Tile* before, const SelectionTile* selection,
                    TileCoord coord, int level, float* out);

// For tests: what the merge lays on a pixel, Over and Erase, by its four channels at once where the processor has SSE2, or channel by
// channel, to prove the two give the same pixels.
// How far a mixing tool's coat goes through a paper that lets `share` of a coat through, as a share of the coat, its alpha a0 before and
// a1 painted.
float mixingCoatShareForTesting(float a0, float a1, float share);
void overPixelForTesting(Channel* pixel, const std::array<float, 3>& premultiplied, float paintAlpha, float coverage, bool byChannel);
void erasePixelForTesting(Channel* pixel, float coverage, bool byChannel);

// For tests: a plain grid's tile is worked out again whole from how it was before the stroke, not only its cells changed since it was
// last merged, to prove the two give the same pixels.
void setGridMergeRebuildsWhole(bool on);

// Fills in each job's fresh tile, on the shared workers if there are enough of them. A tile has one worker, so they don't meet.
void mergeTiles(std::vector<TileMergeJob>& jobs, const StrokePaint& paint);

// Lays the stroke over `target` with premultiplied over. Only tiles the stroke touched change.
void mergeStroke(const StrokeBuffer& buffer, const StrokePaint& paint, TiledImage& target);

// The picture while the stroke runs. It's the same conversion as the merge, so lifting the pen changes no pixel.
TiledImage liveFrame(const TiledImage& before, const StrokeBuffer& buffer, const StrokePaint& paint);

}
