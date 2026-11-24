#pragma once

#include "core/io/DocumentFormat.h"

#include <doodlebold/format_plugin.h>

#include <memory>

class QLibrary;

namespace doodlebold {

// A file format from a plugin (plugins/include/doodlebold/format_plugin.h): the plugin's C functions, behind the same interface
// as the built-in formats. It keeps the plugin's library loaded while it lives.
class PluginFormat : public DocumentFormat {
public:
    // `extensions`: the plugin's, less any a built-in format already has (PluginLoader decides).
    PluginFormat(std::shared_ptr<QLibrary> library, const doodlebold_format& format, QStringList extensions);

    QString id() const override { return m_id; }
    QString name() const override { return m_name; }
    QStringList extensions() const override { return m_extensions; }
    bool canRead() const override { return m_format.read != nullptr; }
    bool canWrite() const override { return m_format.write != nullptr; }
    bool isLossless() const override { return (m_format.flags & DOODLEBOLD_FORMAT_LOSSLESS) != 0; }
    Features features() const override;
    bool recognizes(QIODevice& device) const override;

    FileResult<LoadedDocument> read(QIODevice& device) const override;
    FileResult<> write(const DocumentContent& content, QIODevice& device, const ExportOptions& options) const override;

private:
    std::shared_ptr<QLibrary> m_library;
    doodlebold_format m_format;
    QString m_id;
    QString m_name;
    QStringList m_extensions;
};

}
