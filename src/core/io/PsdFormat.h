#pragma once

#include "core/io/DocumentFormat.h"

#include <QCoreApplication>

namespace doodlebold {

// Adobe Photoshop documents. Reads RGB files of 8 or 16 bits per channel, layered or flat; writes layered RGB at 8 bits with a
// merged image. Kept: layer names (Unicode luni), opacity, visibility, blend mode, clipping, folders (lsct), transparency /
// pixel locks (lspf), Fill opacity (iOpa) and layer masks (channel −2). Vector masks, effects, adjustment
// layers and text are not (the reader says so in the import notes). Blend modes Photoshop has no key for are written with the
// nearest key; a Doodlebold-private block (apmd) lets Doodlebold read its own file back with the exact mode. PsdFormat(true) is
// PSB (version 2: some lengths and the RLE row counts are wider); either reads both versions.
class PsdFormat : public DocumentFormat {
public:
    explicit PsdFormat(bool large = false) : m_large(large) {}

    QString id() const override { return m_large ? QStringLiteral("psb") : QStringLiteral("psd"); }
    QString name() const override {
        return m_large ? QCoreApplication::translate("PsdFormat", "Photoshop large document")
                       : QCoreApplication::translate("PsdFormat", "Photoshop document");
    }
    QStringList extensions() const override { return {m_large ? QStringLiteral("psb") : QStringLiteral("psd")}; }
    bool canRead() const override { return true; }
    bool canWrite() const override { return true; }
    Features features() const override { return {.layers = true}; }
    bool recognizes(QIODevice& device) const override;

    FileResult<LoadedDocument> read(QIODevice& device) const override;
    FileResult<> write(const DocumentContent& content, QIODevice& device, const ExportOptions& options) const override;

private:
    bool m_large = false;
};

namespace psd {

QByteArray packBitsRow(const std::uint8_t* row, int size);
bool unpackBitsRow(const std::uint8_t* in, std::size_t inSize, std::uint8_t* out, int size);

}
}
