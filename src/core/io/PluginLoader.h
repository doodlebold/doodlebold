#pragma once

#include "core/io/FormatRegistry.h"

#include <QStringList>

#include <functional>
#include <vector>

namespace doodlebold::io {

// What the user said about a plugin file, by its contents (SHA-256): a plugin runs inside Doodlebold with the user's rights, so
// none is loaded before they say yes to it, and a changed file is asked about again.
enum class PluginChoice {
    Unasked,
    Allowed,
    Refused,
};

struct FoundPlugin {
    QString path;
    QString hash;  // SHA-256 of the file, in hex
    PluginChoice choice = PluginChoice::Unasked;
    bool heldBack = false;  // allowed, but not loaded this time: the last crash was in it (pluginsNamedIn)
};

struct PluginLoadReport {
    QStringList loaded;              // the names of the plugins that added a format
    QStringList notes;               // one per plugin or format skipped, for the user
    std::vector<FoundPlugin> found;  // every plugin file seen, loaded or not
};

using PluginChoiceOf = std::function<PluginChoice(const QString& hash)>;

// Where plugins are looked for at start: "plugins" in the program's data folder, then "plugins" next to the program.
QStringList pluginFolders();

// The library files in `folders`, in the order they would load.
QStringList pluginFiles(const QStringList& folders = pluginFolders());

// SHA-256 of a file, in hex; empty if it can't be read.
QString pluginHash(const QString& path);

// What the user said, kept in the settings by hash.
PluginChoice savedPluginChoice(const QString& hash);
void savePluginChoice(const QString& hash, const QString& path, PluginChoice choice);

// The plugin files `crashReport` names as modules on its crashing stack, of `paths`: Doodlebold's crash report gives each frame
// as module+offset (CrashReport.cpp), so a crash inside a plugin names its file.
QStringList pluginsNamedIn(const QString& crashReport, const QStringList& paths);

// Loads the format plugins (plugins/include/doodlebold/format_plugin.h) in `folders` that `choiceOf` says were allowed, in order and
// by file name, and adds their formats to `registry` after the built-in ones. A plugin is hashed before it is loaded, and runs in a
// process of its own (PluginHost), from a copy checked against that hash: its code never runs in Doodlebold. Those unasked or
// refused are listed in `found` and not loaded, as are allowed ones `heldBack` names (file paths).
// A file that isn't a plugin, a plugin built for another interface version and a format that doesn't fit are skipped with a note.
// A plugin can't take an extension a built-in format has, apart from those of the read-only "Other pictures" of Qt's image plugins,
// nor one an earlier plugin took.
PluginLoadReport loadFormatPlugins(FormatRegistry& registry, const QStringList& folders = pluginFolders(),
                                   const PluginChoiceOf& choiceOf = savedPluginChoice, const QStringList& heldBack = {});

// Loads one plugin the user has just allowed, as loadFormatPlugins would have.
void loadAllowedPlugin(const QString& path, FormatRegistry& registry, PluginLoadReport& report);

}
