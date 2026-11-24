#pragma once

#include "core/layers/CanvasBackground.h"
#include "core/layers/LayerStack.h"

#include <QColor>
#include <QImage>
#include <QRect>

#include <map>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace doodlebold {

// Cache of the composited, opaque canvas as displayed. Areas are marked dirty when layers change and recomposited, in whole
// tiles, the next time the image is requested.
class Projection {
public:
    Projection(const QSize& size, const CanvasBackground& background);

    const CanvasBackground& background() const { return m_background; }
    void setBackground(const CanvasBackground& background);
    void invalidate(const QRect& rect);
    // Takes image (the composite of the tree this projection will be asked about) as up to date, so only what is invalidated
    // afterwards is recomposited.
    void assign(const QImage& image);
    // The image is made on first use, so a projection that is never shown costs no pixels. layers must be the tree the
    // invalidations described.
    const QImage& image(const LayerStack& layers);
    // Brings up to `most` of the image's dirty tiles up to date, as image() would, and says whether any are left: a caller can
    // spread a big catch-up over several turns of the event loop.
    bool catchUpSome(const LayerStack& layers, std::size_t most);
    // Frees the image; the next image() recomposites everything.
    void release();
    // The same, for a view zoomed out by 2^level, composited from the layers averaged in blocks (Compositor), kept and brought
    // up to date apart from image(). Each level asked for is kept, with its own dirty tiles, so a zoom that comes back to a level
    // only brings what changed since up to date. A valid `area` brings only its tiles up to date, so a catch-up can be spread out.
    const QImage& reducedImage(const LayerStack& layers, int level, const QRect& area = QRect());
    // A running stroke's blocks for its layer's tiles at `level`, shown in the reduced image in place of the tiles until
    // clearStrokeBlocks(), while the layer itself waits for the stroke's end.
    void setStrokeBlocks(LayerId layer, int level, std::unordered_map<TileCoord, std::vector<float>, TileCoordHash> blocks);
    void clearStrokeBlocks();
    // For a timelapse frame while a stroke shows as blocks: the stroke's tiles whose blocks changed since the last call,
    // and their neighbours among them, each the reduced image enlarged to full size (opaque RGBA8888_Premultiplied, clipped to
    // the canvas). The layer itself waits for the stroke's end, so image() doesn't show the stroke yet; these do, softly.
    // Empty when nothing changed or no blocks are shown.
    std::unordered_map<TileCoord, QImage, TileCoordHash> takeStrokeFrameTiles(const LayerStack& layers);

private:
    QRect rect() const { return QRect(QPoint(0, 0), m_size); }

    QSize m_size;
    CanvasBackground m_background;
    QImage m_image;
    std::unordered_set<TileCoord, TileCoordHash> m_dirty;
    struct Reduced {
        QImage image;
        std::unordered_set<TileCoord, TileCoordHash> dirty;
        bool allDirty = true;
    };
    std::map<int, Reduced> m_reduced;  // by level
    LayerId m_blocksLayer;
    int m_blocksLevel = 0;
    std::unordered_map<TileCoord, std::vector<float>, TileCoordHash> m_blocks;
    std::unordered_set<TileCoord, TileCoordHash> m_blocksSinceFrame;  // blocks set since takeStrokeFrameTiles
};

// Area whose composite can differ between two layer trees: changed tiles of layers that kept their place and look, and the
// whole content of layers that were added, removed, moved or changed mode, opacity, visibility or clipping.
QRect changedArea(const LayerStack& before, const LayerStack& after);

}
