#pragma once

#include "core/io/DocumentFormat.h"

#include <QByteArray>
#include <QCoreApplication>

#include <memory>
#include <vector>

namespace doodlebold {

// A picture file as a one-layer document (reading: whatever Qt's plugins decode) and the flattened canvas as a picture
// (writing: BMP, TGA and JPEG are written here so that the headers and JPEG options are exactly as chosen; PNG by QImageWriter).
class ImageFormat : public DocumentFormat {
public:
    enum class Encoder {
        Qt,
        Tga,
        Bmp,
        Jpeg
    };

    struct Spec {
        QString id;
        QString name;
        QStringList extensions;
        QByteArray qtFormat;  // QImageWriter format name; empty for a read-only format
        Encoder encoder = Encoder::Qt;
        Features features;
    };

    explicit ImageFormat(Spec spec) : m_spec(std::move(spec)) {}

    QString id() const override { return m_spec.id; }
    QString name() const override { return m_spec.name; }
    QStringList extensions() const override { return m_spec.extensions; }
    bool canRead() const override { return true; }
    bool canWrite() const override { return !m_spec.qtFormat.isEmpty() || m_spec.encoder != Encoder::Qt; }
    Features features() const override { return m_spec.features; }
    bool yieldsToPlugins() const override;
    bool recognizes(QIODevice& device) const override;

    FileResult<LoadedDocument> read(QIODevice& device) const override;
    FileResult<LoadedDocument> readScaledToFit(QIODevice& device) const override;
    FileResult<> write(const DocumentContent& content, QIODevice& device, const ExportOptions& options) const override;

private:
    Spec m_spec;
};

// Uncompressed 24-bit (opaque) or 32-bit (with alpha) truecolour TGA, rows bottom to top and no footer: byte for byte the
bool writeTga(const QImage& picture, QIODevice& device);

// Windows bitmap, rows bottom to top: 24 bits with the 40-byte header for an opaque picture; 32 bits with the 108-byte V4
// header and BGRA masks for one with alpha. The picture's dots per metre are the pixel
// density.
bool writeBmp(const QImage& picture, QIODevice& device);

std::vector<std::unique_ptr<DocumentFormat>> builtInImageFormats();

}
