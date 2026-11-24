#pragma once

#include "core/effects/EffectSettings.h"
#include "core/raster/TiledImage.h"
#include "core/selection/SelectionMask.h"

#include <QRect>

#include <atomic>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

namespace doodlebold {

// What an effect reads: the layer's pixels as they were before it, and where it may change them.
struct EffectSource {
    TiledImage pixels;                       // a copy of the layer's pixels (its tiles are shared, not copied)
    std::optional<SelectionMask> selection;  // only the selection changes, weighted by its coverage; none: all of the layer
    bool keepAlpha = false;                  // the layer's Preserve Opacity: every pixel keeps its alpha
    QSize canvas;                            // where Radial Blur's and Chromatic Aberration's centre lies
};

// Runs a job of n items, each item(i) once, possibly on several threads. The effect job passes its own, slower threads.
using ParallelRun = std::function<void(int n, const std::function<void(int)>& item)>;

class TileGrid;
class BlurField;

// One effect over one layer, tile by tile, so a job can cancel between tiles and show each finished tile at once. The colour
// effects change all of the layer; the ones that read their neighbours (the blurs, Sharpen, Chromatic Aberration) change the
// canvas, and read past its border as the border carried on, so a painted edge doesn't fade. The source must outlive
// the renderer.
class EffectRenderer {
public:
    EffectRenderer(const EffectSource& source, const EffectSettings& settings);
    ~EffectRenderer();
    EffectRenderer(const EffectRenderer&) = delete;
    EffectRenderer& operator=(const EffectRenderer&) = delete;

    // What every tile shares: a large blur's reduced picture. Call once, before render(); `parallel` may be null.
    void prepare(const ParallelRun& parallel);
    // The tiles the effect may change, in no order.
    const std::vector<TileCoord>& tiles() const { return m_tiles; }
    // A tile's new pixels: null when it ends transparent, the source's own tile when nothing in it changed. Thread-safe.
    TilePtr render(TileCoord c) const;

private:
    void planTiles();
    QRect reachArea() const;
    void effectPatch(TileCoord c, float* out) const;
    void pointPatch(TileCoord c, float* out) const;
    void motionPatch(TileCoord c, float* out) const;
    void radialPatch(TileCoord c, float* out) const;
    void chromaticPatch(TileCoord c, float* out) const;
    void unsharpPatch(TileCoord c, float* out) const;
    void coverageRow(int y, int x0, std::uint16_t* out) const;
    TilePtr finish(TileCoord c, float* effect) const;

    const EffectSource& m_source;
    EffectSettings m_settings;
    bool m_canvasOnly;  // reads its neighbours: changes only the canvas, and reads past its border as the border
    std::unique_ptr<TileGrid> m_grid;
    std::unique_ptr<BlurField> m_blur;
    std::vector<TileCoord> m_tiles;
};

// The tiles in the order a preview should fill them: those in `focus` (the part of the canvas on screen) first, nearest its
// centre first.
void orderTiles(std::vector<TileCoord>& tiles, const QRect& focus);

// The whole effect at once, on the calling thread: the layer's new pixels (tests, and small layers).
TiledImage renderEffect(const EffectSource& source, const EffectSettings& settings);

}
