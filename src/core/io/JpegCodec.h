#pragma once

#include "core/io/FileError.h"

#include <QByteArray>
#include <QImage>

namespace doodlebold::io {

struct JpegSettings {
    int quality = 90;
    bool chromaHorizontal = false;     // Chroma Downsampling → Horizontal: colour at half the horizontal resolution
    bool chromaVertical = false;       // … → Vertical: colour at half the vertical resolution
    double resolution = 72.0;

    bool operator==(const JpegSettings&) const = default;
};

// The luma sampling factors for the Chroma Downsampling checkboxes (the colour components are always 1 × 1): none gives 1 × 1
// (4:4:4), Horizontal 2 × 1, Vertical 1 × 2 and both 2 × 2 (4:2:0).
struct JpegSampling {
    int horizontal = 1;
    int vertical = 1;
    bool operator==(const JpegSampling&) const = default;
};
JpegSampling samplingFor(const JpegSettings& settings);

// A baseline (sequential, standard Huffman tables) JPEG of an opaque picture, written with libjpeg-turbo: JFIF 1.02 with the
FileResult<QByteArray> encodeJpeg(const QImage& picture, const JpegSettings& settings);

}
