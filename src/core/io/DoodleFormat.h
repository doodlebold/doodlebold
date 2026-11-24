#pragma once

#include "core/io/DocumentFormat.h"

#include <QCoreApplication>

#include <array>
#include <cstdint>
#include <cstring>

namespace doodlebold {

// Doodlebold's own format, ".doodle": lossless, versioned, chunked and checksummed.
class DoodleFormat : public DocumentFormat {
public:
    // The one version this build reads and writes; a file of any other is refused. Chunks: the manifest (MNFT, compressed JSON
    // with the layer tree; a layer stores its blend mode's name and any paper settings), one LAYR per raster layer in the file's
    // pixel scale (Channel.h), LMSK for masks, SELM for the selection, the timelapse recording if there is one (TLPS, TLST), END.
    static constexpr std::uint32_t kVersion = 10;
    // A canvas with frames, sound clips or a video is written as this version, which adds to the manifest an "animation" object
    // (the frames with their own layers, the shared layers, the sound clips and the video's path), after the stack's layers the
    // LAYR and LMSK chunks of the other frames' layers, frame by frame, and after SELM one SOND chunk per sound clip. Both
    // versions are read. Not 11: earlier builds wrote their canvases with frames as 11, in a layout this version doesn't read,
    // so those stay refused as another version's files.
    // Version 21 has tracks: the "animation" object lists them, each with its frames, and the pixel chunks of the frames' own
    // layers follow track by track. Version 22 adds sound and video tracks (a track's "kind", a clip's "track") and locks.
    // Versions 20 (one row of frames, before tracks) and 21 (clips in rows of a sound lane) are still read.
    static constexpr std::uint32_t kAnimationVersion = 22;
    static constexpr std::uint32_t kFramesTracksVersion = 21;
    static constexpr std::uint32_t kOneTrackVersion = 20;

    QString id() const override { return QStringLiteral("doodle"); }
    QString name() const override { return QCoreApplication::translate("DoodleFormat", "Doodlebold canvas"); }
    QStringList extensions() const override { return {QStringLiteral("doodle")}; }
    bool canRead() const override { return true; }
    bool canWrite() const override { return true; }
    bool isLossless() const override { return true; }
    Features features() const override { return {.layers = true, .alpha = true}; }
    bool recognizes(QIODevice& device) const override;

    FileResult<LoadedDocument> read(QIODevice& device) const override;
    FileResult<> write(const DocumentContent& content, QIODevice& device, const ExportOptions& options) const override;
};

// The chunks of .doodle files, checksummed: type, 64-bit length, payload, CRC-32. They follow a 12-byte header (magic, version).
namespace doodle_file {

struct Chunk {
    std::array<char, 4> type{};
    QByteArray payload;
    bool is(const char t[4]) const { return std::memcmp(type.data(), t, 4) == 0; }
};

bool writeChunk(QIODevice& device, const char type[4], const QByteArray& payload);
FileResult<Chunk> readChunk(QIODevice& device);

}

}
