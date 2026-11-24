#include "core/io/JpegPreview.h"

namespace doodlebold::io {

namespace {

constexpr qint64 kKilobyte = 1024;
constexpr qint64 kBytesBelowKilobytes = 10 * kKilobyte;

}

QString CompressionResult::fileSizeText() const {
    if (fileSize < kBytesBelowKilobytes)
        return QStringLiteral("%1B").arg(fileSize);
    if (fileSize < kKilobyte * kKilobyte)
        return QStringLiteral("%1KB").arg(static_cast<double>(fileSize) / kKilobyte, 0, 'f', 2);
    return QStringLiteral("%1MB").arg(static_cast<double>(fileSize) / (kKilobyte * kKilobyte), 0, 'f', 2);
}

double CompressionResult::rate() const {
    const qint64 coded = fileSize - 2;
    if (coded <= 0)
        return 0.0;
    return static_cast<double>(pixels.width()) * pixels.height() * 3.0 / static_cast<double>(coded);
}

QString CompressionResult::rateText() const {
    return QStringLiteral("1/%1").arg(rate(), 0, 'f', 2);
}

FileResult<JpegPreview> makeJpegPreview(const QImage& flat, const JpegSettings& settings, bool decode) {
    using R = FileResult<JpegPreview>;
    const auto data = encodeJpeg(flat, settings);
    if (!data)
        return R::failure(data.error());
    JpegPreview out;
    out.result.pixels = flat.size();
    out.result.fileSize = data.value().size();
    if (decode) {
        out.decoded = QImage::fromData(data.value(), "JPEG");
        if (out.decoded.isNull())
            return R::failure({FileError::Kind::Corrupt, {}});
        out.decoded = out.decoded.convertToFormat(QImage::Format_RGB888);
    }
    return out;
}

const std::array<double, kPreviewScaleCount>& previewScales() {
    static const std::array<double, kPreviewScaleCount> scales{0.78125, 1.5625, 3.125, 6.25, 12.5, 25, 50, 100, 200, 400, 800, 1600, 3200};
    return scales;
}

QString previewScaleLabel(int index) {
    static const char* labels[kPreviewScaleCount] = {"0.78%", "1.56%", "3.12%", "6.25%", "12.5%", "25%",  "50%",
                                                     "100%",  "200%",  "400%",  "800%",  "1600%", "3200%"};
    return QString::fromLatin1(labels[index]);
}

int fitScaleIndex(const QSize& image, const QSize& viewport) {
    int best = 0;
    for (int i = 0; i < kPreviewScaleCount; ++i) {
        const double s = previewScales()[static_cast<std::size_t>(i)] / 100.0;
        if (image.width() * s <= viewport.width() && image.height() * s <= viewport.height())
            best = i;
    }
    return best;
}

}
