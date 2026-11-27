#include "core/io/PluginLoader.h"

#include "core/io/PluginFormat.h"
#include "core/io/PluginHost.h"
#include "core/util/AppIdentity.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLibrary>
#include <QRegularExpression>
#include <QSet>
#include <QSettings>
#include <QStandardPaths>

#include <memory>

namespace doodlebold::io {

namespace {

// Where the user's say about each plugin is kept, under its hash.
const QString kChoicesGroup = QStringLiteral("plugins/");
const QString kAllowed = QStringLiteral("allowed");
const QString kRefused = QStringLiteral("refused");

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

QString checkPlugin(const PluginHost::Description& plugin) {
    if (plugin.apiVersion < kOldestApiVersion || plugin.apiVersion > DOODLEBOLD_PLUGIN_API_VERSION)
        return tr("it was built for plugin interface version %1, and this version of Doodlebold loads version %2.")
            .arg(plugin.apiVersion)
            .arg(DOODLEBOLD_PLUGIN_API_VERSION);
    if (plugin.formats.empty() || plugin.formats.size() > kMaxFormatsPerPlugin)
        return tr("it gives no formats.");
    return {};
}

QString checkFormat(const PluginHost::Format& format, const FormatRegistry& registry) {
    if (format.structSize < sizeof(doodlebold_format))
        return tr("its description is too short.");
    if (format.id.isEmpty() || format.name.isEmpty())
        return tr("it has no id, name or extensions.");
    if (!format.reads && !format.writes)
        return tr("it can neither open nor save.");
    if (registry.byId(format.id))
        return tr("a format with the id \"%1\" is already loaded.").arg(format.id);
    return {};
}

// The format's extensions that are free to take; a note for each one that isn't.
QStringList freeExtensions(const PluginHost::Format& format, const FormatRegistry& registry, const QString& label, QStringList& notes) {
    static const QRegularExpression valid(QStringLiteral("^[a-z0-9_-]{1,16}$"));
    QStringList out;
    for (const QString& raw : format.extensions.split(QLatin1Char(';'), Qt::SkipEmptyParts)) {
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
int addFormats(const std::shared_ptr<PluginHost>& host, const PluginHost::Description& plugin, FormatRegistry& registry,
               PluginLoadReport& report) {
    const QString pluginLabel = plugin.name.isEmpty() ? QStringLiteral("?") : plugin.name;
    int added = 0;
    for (std::uint32_t i = 0; i < plugin.formats.size(); ++i) {
        const PluginHost::Format& format = plugin.formats[i];
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
        registry.add(std::make_unique<PluginFormat>(host, i, format, std::move(extensions)));
        ++added;
    }
    if (added > 0)
        report.loaded << pluginLabel;
    return added;
}

// The plugin runs in a helper of its own (PluginHost), from a copy checked against the hash the user allowed: its code never runs
// in Doodlebold.
void loadPlugin(const QString& path, const QString& hash, FormatRegistry& registry, PluginLoadReport& report) {
    QString why;
    const QString copy = PluginHost::copiedForHelper(path, hash, &why);
    if (copy.isEmpty()) {
        report.notes << tr("\"%1\" wasn't loaded: %2.").arg(fileOf(path), why);
        return;
    }
    auto host = std::make_shared<PluginHost>(copy, QFileInfo(path).fileName());
    const auto description = host->describe();
    if (!description) {
        report.notes << tr("\"%1\" was skipped: %2.").arg(fileOf(path), description.error());
        return;
    }
    if (const QString refused = checkPlugin(description.value()); !refused.isEmpty()) {
        report.notes << tr("\"%1\" was skipped: %2").arg(fileOf(path), refused);
        return;
    }
    addFormats(host, description.value(), registry, report);
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

QString pluginHash(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!hash.addData(&file))
        return {};
    return QString::fromLatin1(hash.result().toHex());
}

PluginChoice savedPluginChoice(const QString& hash) {
    const QString choice = app_identity::settings().value(kChoicesGroup + hash + QStringLiteral("/choice")).toString();
    return choice == kAllowed ? PluginChoice::Allowed : choice == kRefused ? PluginChoice::Refused : PluginChoice::Unasked;
}

void savePluginChoice(const QString& hash, const QString& path, PluginChoice choice) {
    QSettings s = app_identity::settings();
    if (choice == PluginChoice::Unasked) {
        s.remove(kChoicesGroup + hash);
        return;
    }
    s.setValue(kChoicesGroup + hash + QStringLiteral("/choice"), choice == PluginChoice::Allowed ? kAllowed : kRefused);
    s.setValue(kChoicesGroup + hash + QStringLiteral("/path"), path);  // shown in Options; the hash decides
}

QStringList pluginsNamedIn(const QString& crashReport, const QStringList& paths) {
    // A frame is "name.dll+0x…" on Windows, and "/path/libname.so(+0x…)" (glibc's backtrace) elsewhere.
    static const QRegularExpression frame(QStringLiteral(R"((?:^|[\s/\\])([^\s/\\()+]+)(?:\+0x|\(\+0x|\())"),
                                          QRegularExpression::MultilineOption);
    QSet<QString> modules;
    for (auto it = frame.globalMatch(crashReport); it.hasNext();)
        modules.insert(it.next().captured(1).toLower());
    QStringList named;
    for (const QString& path : paths)
        if (modules.contains(QFileInfo(path).fileName().toLower()))
            named << path;
    return named;
}

QStringList pluginFiles(const QStringList& folders) {
    QStringList files;
    for (const QString& folder : folders)
        for (const QFileInfo& file : QDir(folder).entryInfoList(QDir::Files, QDir::Name))
            if (QLibrary::isLibrary(file.fileName()))
                files << file.absoluteFilePath();
    return files;
}

PluginLoadReport loadFormatPlugins(FormatRegistry& registry, const QStringList& folders, const PluginChoiceOf& choiceOf,
                                   const QStringList& heldBack) {
    PluginLoadReport report;
    for (const QString& path : pluginFiles(folders)) {
        FoundPlugin found{path, pluginHash(path)};
        if (found.hash.isEmpty()) {
            report.notes << tr("\"%1\" was skipped: it can't be read.").arg(fileOf(found.path));
            continue;
        }
        found.choice = choiceOf(found.hash);
        found.heldBack = found.choice == PluginChoice::Allowed && heldBack.contains(found.path);
        if (found.choice == PluginChoice::Allowed && !found.heldBack)
            loadPlugin(found.path, found.hash, registry, report);
        report.found.push_back(std::move(found));
    }
    return report;
}

void loadAllowedPlugin(const QString& path, FormatRegistry& registry, PluginLoadReport& report) {
    loadPlugin(path, pluginHash(path), registry, report);
}

}
