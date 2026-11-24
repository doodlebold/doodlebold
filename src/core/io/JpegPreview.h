#pragma once

#include "core/io/FileError.h"
#include "core/io/JpegCodec.h"

#include <QImage>
#include <QSize>
#include <QString>

#include <array>

// What the "JPEG Quality" dialog works out for a picture and a setting: the size of the file the settings would produce, the
// picture as that file shows it, and the preview scales. No widgets.
namespace doodlebold::io {

struct CompressionResult {
    QSize pixels;
    qint64 fileSize = 0;

    // "767B" below 10 KB, else kilobytes (1024 bytes) with two decimals: "16.89KB" (5246 bytes show as "5246B" and 13552
    // as "13.23KB"), then megabytes the same way.
    QString fileSizeText() const;
    // Compression Rate: raw RGB bytes over the file size, shown as "1/11.37". The file size is taken less the
    // two bytes of the start and end markers.
    double rate() const;
    QString rateText() const;
};

struct JpegPreview {
    CompressionResult result;
    QImage decoded;
};

FileResult<JpegPreview> makeJpegPreview(const QImage& flat, const JpegSettings& settings, bool decode = true);

// The Scale list of the dialog, in percent (0.78 % … 3200 %: halvings down from 100 % and doublings up), and its labels.
inline constexpr int kPreviewScaleCount = 13;
inline constexpr int kPreviewScale100 = 7;
const std::array<double, kPreviewScaleCount>& previewScales();
QString previewScaleLabel(int index);
// The "=" button: the largest scale of the list at which the whole picture fits `viewport`, or the smallest scale if none does.
int fitScaleIndex(const QSize& image, const QSize& viewport);

}
