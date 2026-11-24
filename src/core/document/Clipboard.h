#pragma once

#include "core/raster/TiledImage.h"
#include "core/selection/SelectionMask.h"

#include <QImage>
#include <QPoint>

#include <optional>

// Copy, cut and paste of layer pixels. The clipboard holds a straight-alpha 8-bit picture so it can go to and from the system
// clipboard; the layer's 16-bit pixels are only rounded at this boundary.
namespace doodlebold {

struct ClipboardImage {
    QImage image;   // Format_RGBA8888 (straight, not premultiplied)
    QPoint offset;
    // What other programs get from Copy: the same pixels over white, 24-bit without alpha (offered as a 24-bit
    // image/bmp). Null for a picture that came from outside.
    QImage onWhite;

    bool isNull() const { return image.isNull(); }
    // A picture from outside the application: placed so that it is centred on the canvas (it may stick out).
    static ClipboardImage centred(const QImage& picture, const QSize& canvas);
};

namespace clipboard {

std::optional<ClipboardImage> copy(const TiledImage& pixels, const SelectionMask* selection);

TiledImage toPixels(const ClipboardImage& clip, const QSize& canvas);

}
}
