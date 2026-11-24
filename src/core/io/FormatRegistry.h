#pragma once

#include "core/io/DocumentFormat.h"

#include <memory>
#include <vector>

namespace doodlebold {

// How to add a file format
// A file importer or exporter is one class plus one registration line, with no other changes to the core:
//   1. Subclass DocumentFormat (DocumentFormat.h). Give it an id(), a name() for the file dialogs and its extensions(). To import,
//      return true from canRead() and implement read(); to export, return true from canWrite() and implement write().
//      Report every failure through FileResult, so that a damaged file is a message and never a crash. Optionally implement
//      recognizes(), so that a file with a missing or wrong extension still finds the format.
//   2. Add one line to registerBuiltInFormats() in FormatRegistry.cpp:  registry.add(std::make_unique<MyFormat>());
// The Open and Save dialogs, dropped files and the command line all go through the registry, so they pick the format up from there.
// Registration is at compile time; formats are not loaded dynamically. Tests can fill a FormatRegistry of their own.

// Every file format the program knows.
class FormatRegistry {
public:
    FormatRegistry() = default;
    static FormatRegistry& instance();

    // Takes the format. A format registered later wins when two claim the same extension.
    void add(std::unique_ptr<DocumentFormat> format);

    const std::vector<std::unique_ptr<DocumentFormat>>& all() const { return m_formats; }
    const DocumentFormat* byId(const QString& id) const;
    const DocumentFormat* forPath(const QString& path, bool forWriting) const;
    std::vector<const DocumentFormat*> readable() const;
    std::vector<const DocumentFormat*> writable() const;

    // File-dialog filters. The open filter starts with "All supported files".
    QString openFilter() const;
    static QString filterFor(const DocumentFormat& format);  // "PNG image (*.png)"
    QStringList readableExtensions() const;

private:
    std::vector<std::unique_ptr<DocumentFormat>> m_formats;
};

// Registers Doodlebold's own formats (.doodle, PSD, pictures) in `registry`.
void registerBuiltInFormats(FormatRegistry& registry);

}
