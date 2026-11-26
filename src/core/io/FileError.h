#pragma once

#include "core/util/Result.h"

#include <QSize>
#include <QString>

#include <utility>

namespace doodlebold {

// Files are outside the program's control, so every failure, a damaged file included, is a value the UI can show, never a crash
// or an exception.
struct FileError {
    enum class Kind {
        CannotOpen,
        CannotWrite,
        UnknownFormat,       // no registered format reads this file / writes this extension
        NotADoodleFile,     // wrong magic: the file is of another kind
        UnsupportedVersion,  // written in another version of the format: only the current one is read
        Truncated,
        Corrupt,             // a checksum, size, count or reference is wrong
        TooLarge,
        Unsupported,         // a valid file using a feature this reader does not have (e.g. CMYK in a PSD)
        PluginFailed,        // the format's plugin stopped, broke its protocol or was stopped by the user (io::PluginHost)
    };

    FileError() = default;
    FileError(Kind k, QString d) : kind(k), detail(std::move(d)) {}

    Kind kind = Kind::Corrupt;
    QString detail;  // what exactly was wrong, e.g. "layer 3: tile list"; may be empty
    // TooLarge only, for a canvas beyond kMaxCanvasSide: its size, and whether it is a flat picture that
    // DocumentFormat::readScaledToFit can open scaled down.
    QSize size;
    bool scalable = false;

    static FileError canvasTooLarge(QSize size, bool scalable);

    QString describe() const;
};

template <typename T = void> using FileResult = Result<T, FileError>;

}
