#include "core/io/FileIo.h"

#include <QFile>
#include <QSaveFile>

namespace doodlebold::io {

namespace {

// The format that reads `file`: the one its extension names, else the first that recognises its content.
const DocumentFormat* readerFor(const QString& path, QFile& file, const FormatRegistry& registry) {
    if (const DocumentFormat* named = registry.forPath(path, false))
        return named;
    for (const DocumentFormat* candidate : registry.readable()) {
        file.seek(0);
        if (candidate->recognizes(file))
            return candidate;
    }
    return nullptr;
}

FileResult<LoadedDocument> load(const QString& path, const FormatRegistry& registry, bool scaledToFit) {
    using R = FileResult<LoadedDocument>;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return R::failure({FileError::Kind::CannotOpen, file.errorString()});
    const DocumentFormat* format = readerFor(path, file, registry);
    if (!format)
        return R::failure({FileError::Kind::UnknownFormat, {}});
    file.seek(0);
    return scaledToFit ? format->readScaledToFit(file) : format->read(file);
}

}

FileResult<LoadedDocument> loadFile(const QString& path, const FormatRegistry& registry) {
    return load(path, registry, false);
}

FileResult<LoadedDocument> loadFileScaledToFit(const QString& path, const FormatRegistry& registry) {
    return load(path, registry, true);
}

FileResult<> saveFile(const DocumentContent& content, const QString& path, const DocumentFormat& format, const ExportOptions& options) {
    if (!format.canWrite())
        return FileResult<>::failure({FileError::Kind::UnknownFormat, format.name()});
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return FileResult<>::failure({FileError::Kind::CannotWrite, file.errorString()});
    auto written = format.write(content, file, options);
    if (!written) {
        file.cancelWriting();
        return written;
    }
    if (!file.commit())
        return FileResult<>::failure({FileError::Kind::CannotWrite, file.errorString()});
    return {};
}

}
