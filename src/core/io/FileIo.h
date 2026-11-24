#pragma once

#include "core/io/DocumentFormat.h"
#include "core/io/FormatRegistry.h"

namespace doodlebold::io {

// Reads a file with the format its extension names; a file with a missing or misleading extension is recognised by its content.
// The work is a function of the file alone, so it can run on any thread.
FileResult<LoadedDocument> loadFile(const QString& path, const FormatRegistry& registry = FormatRegistry::instance());
// The same, for a flat picture that loadFile found beyond kMaxCanvasSide (FileError::scalable): it is scaled down, in
// proportion, to fit.
FileResult<LoadedDocument> loadFileScaledToFit(const QString& path, const FormatRegistry& registry = FormatRegistry::instance());

// Writes `content` to `path` with `format`. The file is written next to its destination and moved into place only when
// everything succeeded (QSaveFile), so a failed or interrupted save never damages an existing file.
FileResult<> saveFile(const DocumentContent& content, const QString& path, const DocumentFormat& format, const ExportOptions& options = {});

}
