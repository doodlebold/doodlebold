#pragma once

#include "core/raster/TiledImage.h"
#include "core/timelapse/Timelapse.h"

#include <QImage>
#include <QSize>

#include <unordered_map>
#include <vector>

namespace doodlebold {

// Per tile it knows which steps stored it, so the picture at a step is the latest stored version of each tile and moving
// between steps decodes only the tiles that differ.
class TimelapseReplay {
public:
    explicit TimelapseReplay(Timelapse recording);

    const Timelapse& recording() const { return m_recording; }
    int stepCount() const { return static_cast<int>(m_recording.steps.size()); }
    QSize sizeAt(int step) const;
    // The picture after step `step` (0: the first picture), Format_RGBA8888, opaque. The reference stays valid until the next
    // seek(). A tile that cannot be decoded (a damaged file) shows black.
    const QImage& seek(int step);

private:
    struct Entry {
        int step;
        int tile;
    };
    struct Shown {
        int step = -1;
        int tile = -1;
    };

    Timelapse m_recording;
    std::unordered_map<TileCoord, std::vector<Entry>, TileCoordHash> m_history;
    std::vector<int> m_fullSteps;
    std::unordered_map<TileCoord, Shown, TileCoordHash> m_shown;
    QImage m_image;
};

}
