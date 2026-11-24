#pragma once

#include "core/document/DocumentContent.h"
#include "core/io/FileError.h"

#include <QIODevice>
#include <QSize>
#include <QString>
#include <QStringList>

#include <algorithm>
#include <cmath>

namespace doodlebold {

// The largest canvas side Doodlebold opens or creates, the baseline. A canvas also costs a full-size display
// image (4 bytes per pixel).
inline constexpr int kMaxCanvasSide = 16384;

inline bool beyondCanvasLimit(QSize size) {
    return size.width() > kMaxCanvasSide || size.height() > kMaxCanvasSide;
}

// `size` scaled down, in proportion, until neither side is beyond kMaxCanvasSide; a size within it is kept.
inline QSize fittedToCanvasLimit(QSize size) {
    if (!beyondCanvasLimit(size))
        return size;
    const double scale = double(kMaxCanvasSide) / std::max(size.width(), size.height());
    return {std::clamp(int(std::lround(size.width() * scale)), 1, kMaxCanvasSide),
            std::clamp(int(std::lround(size.height() * scale)), 1, kMaxCanvasSide)};
}

struct LoadedDocument {
    DocumentContent content;
    QStringList notes;
};

// Only JPEG asks anything (its "JPEG Quality" dialog); everything else follows the document: a transparent
// background exports with alpha where the format has one, any other exports opaque as the canvas shows it.
struct ExportOptions {
    int quality = 90;
    bool chromaHorizontal = false;
    bool chromaVertical = false;
    bool operator==(const ExportOptions&) const = default;
};

// Readers build a DocumentContent from a device; writers take a DocumentContent (a cheap immutable snapshot, tiles shared), so
// both can run on a worker thread.
class DocumentFormat {
public:
    struct Features {
        bool layers = false;
        bool alpha = false;
        bool quality = false;
    };

    virtual ~DocumentFormat() = default;

    virtual QString id() const = 0;
    virtual QString name() const = 0;
    virtual QStringList extensions() const = 0;      // lower case, without the dot; the first is the default
    virtual bool canRead() const { return false; }
    virtual bool canWrite() const { return false; }
    // True when a saved file keeps everything the document holds. Otherwise the title bar shows a "!" after a save.
    virtual bool isLossless() const { return false; }
    virtual Features features() const { return {}; }
    // A format whose extensions a format plugin may take (PluginLoader.h): the read-only pictures that this computer's Qt image
    // plugins happen to decode.
    virtual bool yieldsToPlugins() const { return false; }
    // Content sniffing, for files whose extension is missing or wrong. The device is positioned at its start and is left in any
    // position.
    virtual bool recognizes(QIODevice&) const { return false; }

    virtual FileResult<LoadedDocument> read(QIODevice& device) const;
    // A picture beyond kMaxCanvasSide, scaled down to fittedToCanvasLimit. Only flat pictures can: a format that can't says
    // so with Unsupported. A reader whose TooLarge error carries the picture's size (FileError::size) can.
    virtual FileResult<LoadedDocument> readScaledToFit(QIODevice& device) const;
    virtual FileResult<> write(const DocumentContent& content, QIODevice& device, const ExportOptions& options) const;
};

}
