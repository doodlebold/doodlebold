#pragma once

#include "core/layers/CanvasBackground.h"
#include "core/layers/LayerStack.h"

#include <QColor>
#include <QImage>
#include <QRect>

#include <unordered_map>
#include <vector>

namespace doodlebold {

// Flattens a layer tree on the CPU, float per channel, one 64×64 tile at a time, tiles in parallel. Siblings go bottom → top; a
// folder in Pass Through mode composites its children straight onto the backdrop (then cross-fades by its opacity), any other
// folder is composited in isolation and blended as one layer. Clipping layers are composited onto their base (the nearest
// non-clipping sibling below) with source-atop, and the group is then blended with the base's mode and opacity.
// A layer's paper texture is applied to its own colour when its content is read, before clipping,
// opacity, mode and mask: the layer's pixels stay plain.
class Compositor {
public:
    // Blends the layers of rect onto the opaque paper colour, the bottom-most backdrop for every blend mode. out
    // must be an RGBA8888_Premultiplied image of the document's size; every pixel of rect is overwritten.
    static void renderOpaque(const LayerStack& stack, const QRect& rect, const QColor& paper, QImage* out);
    // The canvas of a transparent background as displayed: the layers composited over transparency (so blend modes see no
    // backdrop, as in an export) and then over `backdrop`. Same output contract as renderOpaque().
    static void renderOverBackdrop(const LayerStack& stack, const QRect& rect, const Backdrop& backdrop, QImage* out);
    static void renderDisplay(const LayerStack& stack, const QRect& rect, const CanvasBackground& background, QImage* out);
    static void renderDisplayTiles(const LayerStack& stack, const std::vector<QPoint>& tiles, const CanvasBackground& background,
                                   QImage* out);
    // The same tiles for a view zoomed out by 2^level, each layer averaged over blocks of 2^level pixels a side first and the
    // blocks composited as pixels, into `out`, the canvas at that scale. Only for showing, at a fraction of the work.
    // In place of one layer's tiles, a running stroke's blocks for them, as reduceTile() makes them, where it has them.
    struct ReducedOverride {
        LayerId layer;
        const std::unordered_map<TileCoord, std::vector<float>, TileCoordHash>* blocks = nullptr;
    };
    static void renderDisplayTilesReduced(const LayerStack& stack, const std::vector<QPoint>& tiles, const CanvasBackground& background,
                                          int level, QImage* out, const ReducedOverride* override = nullptr);
    // A tile averaged over blocks of 2^level pixels a side: (64 >> level)^2 pixels of four floats, premultiplied, 0 to 1.
    static void reduceTile(const Tile& tile, int level, float* out);
    // The same canvas as renderOpaque() draws, at the layers' own precision: fully opaque tiles. For exports that keep more than 8 bits.
    static TiledImage flattenOverPaper(const LayerStack& stack, const QColor& paper);
    // The composite of `nodes` (bottom → top) over transparency, as a tiled image (Merge Down). Where the nodes are not opaque
    // this differs from the displayed canvas for most blend modes.
    static TiledImage flatten(const std::vector<const LayerNode*>& nodes, const QSize& size);

private:
    static void renderOpaqueTiles(const LayerStack& stack, const std::vector<QPoint>& tiles, const QRect& r, const QColor& paper,
                                  QImage* out);
    static void renderOverBackdropTiles(const LayerStack& stack, const std::vector<QPoint>& tiles, const QRect& r, const Backdrop& backdrop,
                                        QImage* out);
};

}
