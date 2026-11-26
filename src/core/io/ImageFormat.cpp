#include "core/io/ImageFormat.h"

#include "core/io/ByteIO.h"
#include "core/io/ImageConvert.h"
#include "core/io/JpegCodec.h"

#include <QImageReader>
#include <QImageWriter>

#include <algorithm>
#include <cstdint>
#include <mutex>

namespace doodlebold {

namespace {

// Qt refuses pictures above 256 MB by default; a 16384² picture is 1 GB.
void raiseAllocationLimit() {
    static std::once_flag once;
    std::call_once(once, [] {
        if (QImageReader::allocationLimit() != 0 && QImageReader::allocationLimit() < 2048)
            QImageReader::setAllocationLimit(2048);
    });
}

// The most pixels one run-length packet of a TGA covers: its 7-bit count, plus one (the TGA specification).
constexpr std::size_t kTgaPacketPixels = 128;

// A picture read to be scaled down is decoded whole first (but a JPEG, which Qt decodes at the smaller size), up to 4 GB:
// about 32768², twice the canvas limit each way.
constexpr int kLargestScaledSourceMb = 4096;
constexpr std::int64_t kLargestScaledSourcePixels = std::int64_t(kLargestScaledSourceMb) * 1024 * 1024 / 4;

class AllocationLimitFor {
public:
    explicit AllocationLimitFor(int mb) : m_was(QImageReader::allocationLimit()) {
        if (m_was != 0 && m_was < mb)
            QImageReader::setAllocationLimit(mb);
    }
    ~AllocationLimitFor() { QImageReader::setAllocationLimit(m_was); }
    AllocationLimitFor(const AllocationLimitFor&) = delete;
    AllocationLimitFor& operator=(const AllocationLimitFor&) = delete;

private:
    int m_was;
};

QImage scaledToFit(const QImage& picture) {
    const QSize fitted = fittedToCanvasLimit(picture.size());
    return fitted == picture.size() ? picture : picture.scaled(fitted, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
}

FileError readerError(const QImageReader& reader) {
    switch (reader.error()) {
    case QImageReader::FileNotFoundError:
    case QImageReader::DeviceError: return {FileError::Kind::CannotOpen, reader.errorString()};
    case QImageReader::UnsupportedFormatError: return {FileError::Kind::UnknownFormat, reader.errorString()};
    default: return {FileError::Kind::Corrupt, reader.errorString()};
    }
}

// Truecolour TGA (uncompressed or run-length encoded, 24 or 32 bits, any origin) read here: Qt's TGA plugin refuses files
// without the TGA 2.0 footer. A null image means "not a file this reader handles": the caller falls
// back to Qt. `tooLarge` is set to the picture's size when it is beyond `largestSide`.
QImage readTruecolourTga(const QByteArray& data, QSize* tooLarge, int largestSide = kMaxCanvasSide) {
    io::ByteReader r(data, io::ByteOrder::Little);
    const int idLength = r.u8(), colourMapType = r.u8(), imageType = r.u8();
    r.skip(5);
    r.skip(4);
    const int width = r.u16(), height = r.u16(), depth = r.u8(), descriptor = r.u8();
    if (!r.ok() || colourMapType != 0 || (imageType != 2 && imageType != 10) || (depth != 24 && depth != 32) || width == 0 || height == 0)
        return {};
    if (width > largestSide || height > largestSide) {
        *tooLarge = QSize(width, height);
        return {};
    }
    r.skip(static_cast<std::size_t>(idLength));
    const int bytes = depth / 8;
    const bool alpha = depth == 32 && (descriptor & 0x0F) != 0;
    const bool topDown = (descriptor & 0x20) != 0, rightToLeft = (descriptor & 0x10) != 0;
    const std::size_t pixels = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    // Checked before the picture is made, as a header can claim any size: the file must hold at least what that size takes. A
    // run-length packet is a header byte and one pixel at least, for up to kTgaPacketPixels pixels.
    const std::size_t least = imageType == 2 ? pixels * static_cast<std::size_t>(bytes)
                                             : (pixels + kTgaPacketPixels - 1) / kTgaPacketPixels * (1 + static_cast<std::size_t>(bytes));
    if (!r.ok() || r.remaining() < least)
        return {};
    QImage out(width, height, alpha ? QImage::Format_RGBA8888 : QImage::Format_RGB888);
    if (out.isNull())
        return {};
    const auto store = [&](std::size_t index, const std::uint8_t* p) {
        const int y0 = static_cast<int>(index / static_cast<std::size_t>(width));
        const int x0 = static_cast<int>(index % static_cast<std::size_t>(width));
        const int y = topDown ? y0 : height - 1 - y0;
        const int x = rightToLeft ? width - 1 - x0 : x0;
        std::uint8_t* d = out.scanLine(y) + x * (alpha ? 4 : 3);
        d[0] = p[2];
        d[1] = p[1];
        d[2] = p[0];
        if (alpha)
            d[3] = p[3];
    };
    std::size_t done = 0;
    if (imageType == 2) {
        const std::uint8_t* raw = r.span(pixels * static_cast<std::size_t>(bytes));
        if (!raw)
            return {};
        for (; done < pixels; ++done)
            store(done, raw + done * static_cast<std::size_t>(bytes));
    } else {
        while (done < pixels) {
            const int header = r.u8();
            const std::size_t count = static_cast<std::size_t>(header & 0x7F) + 1;
            if (!r.ok() || done + count > pixels)
                return {};
            if (header & 0x80) {
                const std::uint8_t* p = r.span(static_cast<std::size_t>(bytes));
                if (!p)
                    return {};
                for (std::size_t i = 0; i < count; ++i)
                    store(done++, p);
            } else {
                const std::uint8_t* p = r.span(count * static_cast<std::size_t>(bytes));
                if (!p)
                    return {};
                for (std::size_t i = 0; i < count; ++i)
                    store(done++, p + i * static_cast<std::size_t>(bytes));
            }
        }
    }
    return out;
}

}

bool ImageFormat::recognizes(QIODevice& device) const {
    return !QImageReader(&device).format().isEmpty();
}

FileResult<LoadedDocument> ImageFormat::read(QIODevice& device) const {
    using R = FileResult<LoadedDocument>;
    raiseAllocationLimit();
    if (m_spec.encoder == Encoder::Tga) {
        QSize tooLarge;
        const QImage tga = readTruecolourTga(device.readAll(), &tooLarge);
        if (tooLarge.isValid())
            return R::failure(FileError::canvasTooLarge(tooLarge, true));
        if (!tga.isNull())
            return LoadedDocument{io::contentFromPicture(tga), {}};
        if (!device.reset())
            return R::failure({FileError::Kind::CannotOpen, device.errorString()});
    }
    // TGA has no magic number, so Qt only accepts it when told what to expect; a file that is not TGA after all falls back to
    // Qt's own detection.
    QImageReader reader(&device, m_spec.encoder == Encoder::Tga ? QByteArray("tga") : QByteArray());
    reader.setAutoTransform(true);  // camera pictures carry their orientation as metadata
    const QSize announced = reader.size();
    if (announced.isValid() && beyondCanvasLimit(announced))
        return R::failure(FileError::canvasTooLarge(announced, true));
    QImage picture = reader.read();
    if (picture.isNull() && m_spec.encoder == Encoder::Tga && device.reset()) {
        QImageReader generic(&device);
        picture = generic.read();
    }
    if (picture.isNull())
        return R::failure(readerError(reader));
    if (beyondCanvasLimit(picture.size()))
        return R::failure(FileError::canvasTooLarge(picture.size(), true));
    return LoadedDocument{io::contentFromPicture(picture), {}};
}

FileResult<LoadedDocument> ImageFormat::readScaledToFit(QIODevice& device) const {
    using R = FileResult<LoadedDocument>;
    raiseAllocationLimit();
    const AllocationLimitFor limit(kLargestScaledSourceMb);
    const auto tooLargeToScale = [](QSize size) {
        return R::failure(
            {FileError::Kind::TooLarge, QStringLiteral("(%1 × %2 pixels, too large to scale down)").arg(size.width()).arg(size.height())});
    };
    if (m_spec.encoder == Encoder::Tga) {
        QSize tooLarge;
        const QImage tga = readTruecolourTga(device.readAll(), &tooLarge, 65535);
        if (!tga.isNull() && std::int64_t(tga.width()) * tga.height() > kLargestScaledSourcePixels)
            return tooLargeToScale(tga.size());
        if (!tga.isNull())
            return LoadedDocument{io::contentFromPicture(scaledToFit(tga)), {}};
        if (!device.reset())
            return R::failure({FileError::Kind::CannotOpen, device.errorString()});
    }
    QImageReader reader(&device, m_spec.encoder == Encoder::Tga ? QByteArray("tga") : QByteArray());
    reader.setAutoTransform(true);
    const QSize announced = reader.size();
    if (announced.isValid()) {
        // Before the camera's turn: the scaled size is the stored one's, and the turn swaps it with the picture.
        if (std::int64_t(announced.width()) * announced.height() > kLargestScaledSourcePixels)
            return tooLargeToScale(announced);
        reader.setScaledSize(fittedToCanvasLimit(announced));
    }
    const QImage picture = reader.read();
    if (picture.isNull())
        return R::failure(readerError(reader));
    return LoadedDocument{io::contentFromPicture(scaledToFit(picture)), {}};
}

bool writeTga(const QImage& picture, QIODevice& device) {
    const bool alpha = picture.hasAlphaChannel();
    const QImage src = picture.convertToFormat(alpha ? QImage::Format_RGBA8888 : QImage::Format_RGB888);
    if (src.isNull() || src.width() > 65535 || src.height() > 65535)
        return false;
    const int bytes = alpha ? 4 : 3;
    io::ByteWriter w(io::ByteOrder::Little);
    w.u8(0);                       // no image id
    w.u8(0);                       // no colour map
    w.u8(2);                       // uncompressed truecolour
    w.zeros(5);                    // colour map specification
    w.u16(0);
    w.u16(0);
    w.u16(static_cast<std::uint32_t>(src.width()));
    w.u16(static_cast<std::uint32_t>(src.height()));
    w.u8(static_cast<std::uint32_t>(bytes * 8));
    w.u8(alpha ? 8 : 0);           // bottom-left origin, alpha bits
    if (device.write(w.data()) != w.data().size())
        return false;
    QByteArray row(src.width() * bytes, Qt::Uninitialized);
    for (int y = src.height() - 1; y >= 0; --y) {
        const std::uint8_t* s = src.constScanLine(y);
        auto* d = reinterpret_cast<std::uint8_t*>(row.data());
        for (int x = 0; x < src.width(); ++x, s += bytes, d += bytes) {
            d[0] = s[2];
            d[1] = s[1];
            d[2] = s[0];
            if (alpha)
                d[3] = s[3];
        }
        if (device.write(row) != row.size())
            return false;
    }
    return true;
}

bool writeBmp(const QImage& picture, QIODevice& device) {
    const bool alpha = picture.hasAlphaChannel();
    const QImage src = picture.convertToFormat(alpha ? QImage::Format_RGBA8888 : QImage::Format_RGB888);
    if (src.isNull())
        return false;
    const int bytes = alpha ? 4 : 3;
    const std::uint64_t rowSize = (static_cast<std::uint64_t>(src.width()) * bytes + 3) / 4 * 4;
    const std::uint64_t pixelBytes = rowSize * static_cast<std::uint64_t>(src.height());
    const std::uint32_t headerSize = alpha ? 108 : 40;
    const std::uint64_t fileSize = 14 + headerSize + pixelBytes;
    if (fileSize > 0xFFFFFFFFull)
        return false;
    io::ByteWriter w(io::ByteOrder::Little);
    w.bytes("BM", 2);
    w.u32(static_cast<std::uint32_t>(fileSize));
    w.u32(0);
    w.u32(14 + headerSize);
    w.u32(headerSize);
    w.u32(static_cast<std::uint32_t>(src.width()));
    w.u32(static_cast<std::uint32_t>(src.height()));
    w.u16(1);
    w.u16(static_cast<std::uint32_t>(bytes * 8));
    w.u32(alpha ? 3 : 0);                             // BI_BITFIELDS (the masks follow) / BI_RGB
    w.u32(static_cast<std::uint32_t>(pixelBytes));
    w.u32(static_cast<std::uint32_t>(std::max(0, src.dotsPerMeterX())));
    w.u32(static_cast<std::uint32_t>(std::max(0, src.dotsPerMeterY())));
    w.u32(0);
    w.u32(0);
    if (alpha) {
        w.u32(0x00FF0000);  // red, green, blue and alpha masks of a BGRA pixel
        w.u32(0x0000FF00);
        w.u32(0x000000FF);
        w.u32(0xFF000000);
        w.zeros(4 + 36 + 12);  // colour space type (none), CIE endpoints, gamma: all unused
    }
    if (device.write(w.data()) != w.data().size())
        return false;
    QByteArray row(static_cast<qsizetype>(rowSize), '\0');
    for (int y = src.height() - 1; y >= 0; --y) {
        const std::uint8_t* s = src.constScanLine(y);
        auto* d = reinterpret_cast<std::uint8_t*>(row.data());
        for (int x = 0; x < src.width(); ++x, s += bytes, d += bytes) {
            d[0] = s[2];
            d[1] = s[1];
            d[2] = s[0];
            if (alpha)
                d[3] = s[3];
        }
        if (device.write(row) != row.size())
            return false;
    }
    return true;
}

FileResult<> ImageFormat::write(const DocumentContent& content, QIODevice& device, const ExportOptions& options) const {
    using R = FileResult<>;
    if (!canWrite())
        return R::failure({FileError::Kind::UnknownFormat, name()});
    const QImage picture = io::flattenedPicture(content, m_spec.features.alpha);
    if (picture.isNull())
        return R::failure({FileError::Kind::TooLarge, {}});
    switch (m_spec.encoder) {
    case Encoder::Tga:
        if (!writeTga(picture, device))
            return R::failure({FileError::Kind::CannotWrite, device.errorString()});
        return {};
    case Encoder::Bmp:
        if (!writeBmp(picture, device))
            return R::failure({FileError::Kind::CannotWrite, device.errorString()});
        return {};
    case Encoder::Jpeg: {
        io::JpegSettings settings;
        settings.quality = options.quality;
        settings.chromaHorizontal = options.chromaHorizontal;
        settings.chromaVertical = options.chromaVertical;
        settings.resolution = content.resolution;
        const auto data = io::encodeJpeg(picture, settings);
        if (!data)
            return R::failure(data.error());
        if (device.write(data.value()) != data.value().size())
            return R::failure({FileError::Kind::CannotWrite, device.errorString()});
        return {};
    }
    case Encoder::Qt: break;
    }
    QImageWriter writer(&device, m_spec.qtFormat);
    if (!writer.write(picture))
        return R::failure({FileError::Kind::CannotWrite, writer.errorString()});
    return {};
}

namespace {
const QString kOtherPicturesId = QStringLiteral("pictures");
}

bool ImageFormat::yieldsToPlugins() const {
    return m_spec.id == kOtherPicturesId;
}

std::vector<std::unique_ptr<DocumentFormat>> builtInImageFormats() {
    using F = DocumentFormat::Features;
    const QList<QByteArray> writers = QImageWriter::supportedImageFormats();
    const auto has = [&](const char* f) { return writers.contains(QByteArray(f)); };  // PNG only: the rest is written here
    std::vector<std::unique_ptr<DocumentFormat>> out;
    QStringList claimed{QStringLiteral("psd"), QStringLiteral("doodle")};

    const auto add = [&](ImageFormat::Spec spec) {
        claimed << spec.extensions;
        out.push_back(std::make_unique<ImageFormat>(std::move(spec)));
    };
    const auto tr = [](const char* s) { return QCoreApplication::translate("ImageFormat", s); };
    if (has("png"))
        add({QStringLiteral("png"), tr("PNG image"), {QStringLiteral("png")}, "png", ImageFormat::Encoder::Qt, F{.alpha = true}});
    add({QStringLiteral("jpeg"),
         tr("JPEG image"),
         {QStringLiteral("jpg"), QStringLiteral("jpeg")},
         {},
         ImageFormat::Encoder::Jpeg,
         F{.quality = true}});
    add({QStringLiteral("bmp"), tr("Bitmap image"), {QStringLiteral("bmp")}, {}, ImageFormat::Encoder::Bmp, F{.alpha = true}});
    add({QStringLiteral("tga"), tr("TARGA image"), {QStringLiteral("tga")}, {}, ImageFormat::Encoder::Tga, F{.alpha = true}});
    // The writable picture formats are psd, psb, bmp, jpg, png and tga. Everything else Qt can decode, WebP included, opens
    // read-only.
    QStringList others;
    for (const QByteArray& f : QImageReader::supportedImageFormats()) {
        const QString ext = QString::fromLatin1(f).toLower();
        if (!claimed.contains(ext) && !others.contains(ext))
            others << ext;
    }
    if (!others.isEmpty())
        add({kOtherPicturesId, tr("Other pictures"), others, {}, ImageFormat::Encoder::Qt, F{}});
    return out;
}

}
