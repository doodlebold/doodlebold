#pragma once

#include "core/document/DocumentContent.h"
#include "core/io/DocumentFormat.h"

#include <QImage>

// Conversions between pictures (straight alpha, 8 or 16 bits) and the layers' premultiplied 16-bit pixels.
namespace doodlebold::io {

// Layer pixels (canvas = the picture's size) from a picture, 8 or 16 bits per channel.
TiledImage layerFromImage(const QImage& picture);

TiledImage placeOnCanvas(const TiledImage& pixels, const QSize& canvas, const QPoint& topLeft);

// The whole document as one 8-bit picture, ready for a picture file, rounded. With `keepAlpha` and
// a transparent background the layers are flattened over transparency (RGBA8888, straight, RGB 0 where alpha is 0); otherwise
// they are composited over the background colour as the canvas shows them (RGB888). A transparent background in a format
// without alpha is composited over white. The picture carries the document's
// resolution as its dots per metre.
QImage flattenedPicture(const DocumentContent& content, bool keepAlpha);

// Pixels per inch as the dots per metre picture files store: 72 ppi is 2835.
int dotsPerMeter(double pixelsPerInch);
// ... and back, as pixels per inch (2835 gives 72.009; a reader rounds what it shows).
double pixelsPerInch(int dotsPerMeter);

inline bool hasTransparentBackground(const DocumentContent& content) {
    return content.background().isTransparent();
}

// The visible content of the document as the pixels of one layer (canvas-sized): a lone Normal raster layer as it is, anything
// else flattened over transparency. "Import as Layer" of any file.
TiledImage mergedLayer(const DocumentContent& content);

// A new one-layer document holding `picture` (white background, 72 ppi, layer "Layer1").
DocumentContent contentFromPicture(const QImage& picture);

}
