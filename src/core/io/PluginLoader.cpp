#include "core/io/PluginLoader.h"

#include "core/io/PluginFormat.h"

#include <QCoreApplication>
#include <QDir>
#include <QLibrary>
#include <QRegularExpression>
#include <QStandardPaths>

#include <memory>

namespace doodlebold::io {

namespace {

// The interface versions this program loads: plugins built for any other are skipped.
constexpr std::uint32_t kOldestApiVersion = 1;
constexpr std::uint32_t kMaxFormatsPerPlugin = 64;

QString tr(const char* text) {
    return QCoreApplication::translate("PluginLoader", text);
}

QString fileOf(const QString& path) {
    return QDir::toNativeSeparators(path);
}

// The registered format that already reads or writes `extension` and keeps it.
const DocumentFormat* ownerOf(const FormatRegistry& registry, const QString& extension) {
    for (const auto& f : registry.all())
        if (!f->yieldsToPlugins() && f->extensions().contains(extension))
            return f.get();
    return nullptr;
}

QString checkPlugin(const doodlebold_plugin* plugin) {
    if (!plugin)
        return tr("it declined to work with this version of Doodlebold.");
    if (plugin->struct_size < sizeof(doodlebold_plugin))
        return tr("its description is too short.");
    if (plugin->api_version < kOldestApiVersion || plugin->api_version > DOODLEBOLD_PLUGIN_API_VERSION)
        return tr("it was built for plugin interface version %1, and this version of Doodlebold loads version %2.")
            .arg(plugin->api_version)
            .arg(DOODLEBOLD_PLUGIN_API_VERSION);
    if (plugin->format_count == 0 || plugin->format_count > kMaxFormatsPerPlugin || !plugin->formats)
        return tr("it gives no formats.");
    return {};
}

QString checkFormat(const doodlebold_format& format, const FormatRegistry& registry) {
    if (format.struct_size < sizeof(doodlebold_format))
        return tr("its description is too short.");
    if (!format.id || !*format.id || !format.name || !*format.name || !format.extensions)
        return tr("it has no id, name or extensions.");
    if (!format.read && !format.write)
        return tr("it can neither open nor save.");
    if (registry.byId(QString::fromUtf8(format.id)))
        return tr("a format with the id \"%1\" is already loaded.").arg(QString::fromUtf8(format.id));
    return {};
}

// The format's extensions that are free to take; a note for each one that isn't.
QStringList freeExtensions(const doodlebold_format& format, const FormatRegistry& registry, const QString& label, QStringList& notes) {
    static const QRegularExpression valid(QStringLiteral("^[a-z0-9_-]{1,16}$"));
    QStringList out;
    for (const QString& raw : QString::fromUtf8(format.extensions).split(QLatin1Char(';'), Qt::SkipEmptyParts)) {
        const QString ext = raw.trimmed().toLower();
        if (!valid.match(ext).hasMatch() || out.contains(ext))
            continue;
        if (const DocumentFormat* owner = ownerOf(registry, ext)) {
            notes << tr("%1: .%2 files stay with %3.").arg(label, ext, owner->name());
            continue;
        }
        out << ext;
    }
    return out;
}

// Adds what the plugin's formats can; returns how many were added.
int addFormats(const std::shared_ptr<QLibrary>& library, const doodlebold_plugin& plugin, FormatRegistry& registry,
               PluginLoadReport& report) {
    const QString pluginLabel = QString::fromUtf8(plugin.name && *plugin.name ? plugin.name : "?");
    int added = 0;
    for (std::uint32_t i = 0; i < plugin.format_count; ++i) {
        const doodlebold_format& format = plugin.formats[i];
        const QString label = tr("Plugin \"%1\", format %2").arg(pluginLabel).arg(i + 1);
        if (const QString why = checkFormat(format, registry); !why.isEmpty()) {
            report.notes << tr("%1 was skipped: %2").arg(label, why);
            continue;
        }
        QStringList extensions = freeExtensions(format, registry, label, report.notes);
        if (extensions.isEmpty()) {
            report.notes << tr("%1 was skipped: it has no extension left to take.").arg(label);
            continue;
        }
        registry.add(std::make_unique<PluginFormat>(library, format, std::move(extensions)));
        ++added;
    }
    if (added > 0)
        report.loaded << pluginLabel;
    return added;
}

void loadPlugin(const QString& path, FormatRegistry& registry, PluginLoadReport& report) {
    auto library = std::make_shared<QLibrary>(path);
    if (!library->load()) {
        report.notes << tr("\"%1\" was skipped: it can't be loaded (%2).").arg(fileOf(path), library->errorString());
        return;
    }
    const auto entry = reinterpret_cast<doodlebold_plugin_entry_fn>(library->resolve(DOODLEBOLD_PLUGIN_ENTRY));
    const doodlebold_plugin* plugin = entry ? entry(DOODLEBOLD_PLUGIN_API_VERSION) : nullptr;
    const QString why = entry ? checkPlugin(plugin) : tr("it has no %1 function.").arg(QLatin1String(DOODLEBOLD_PLUGIN_ENTRY));
    if (!why.isEmpty())
        report.notes << tr("\"%1\" was skipped: %2").arg(fileOf(path), why);
    if (!why.isEmpty() || addFormats(library, *plugin, registry, report) == 0)
        library->unload();
}

}

QStringList pluginFolders() {
    const QString data = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QStringList out;
    if (!data.isEmpty())
        out << QDir(data).filePath(QStringLiteral("plugins"));
    out << QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("plugins"));
    return out;
}

PluginLoadReport loadFormatPlugins(FormatRegistry& registry, const QStringList& folders) {
    PluginLoadReport report;
    for (const QString& folder : folders) {
        const QDir dir(folder);
        for (const QFileInfo& file : dir.entryInfoList(QDir::Files, QDir::Name))
            if (QLibrary::isLibrary(file.fileName()))
                loadPlugin(file.absoluteFilePath(), registry, report);
    }
    return report;
}

}
