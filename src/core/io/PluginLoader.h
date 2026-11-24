#pragma once

#include "core/io/FormatRegistry.h"

#include <QStringList>

namespace doodlebold::io {

struct PluginLoadReport {
    QStringList loaded;  // the names of the plugins that added a format
    QStringList notes;   // one per plugin or format skipped, for the user
};

// Where plugins are looked for at start: "plugins" in the program's data folder, then "plugins" next to the program.
QStringList pluginFolders();

// Loads every format plugin (plugins/include/doodlebold/format_plugin.h) in `folders`, in order and by file name, and adds
// their formats to `registry` after the built-in ones. A file that isn't a plugin, a plugin built for another interface version
// and a format that doesn't fit are skipped with a note. A plugin can't take an extension a built-in format has, apart from those
// of the read-only "Other pictures" of Qt's image plugins, nor one an earlier plugin took.
PluginLoadReport loadFormatPlugins(FormatRegistry& registry, const QStringList& folders = pluginFolders());

}
