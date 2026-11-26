#pragma once

// What keeps a plugin to its own business in doodlebold-plugin-host (plugin-process-design), and its loading between the two
// halves. Each returns nullptr when done, or why it couldn't be: the helper then refuses to run the plugin, as a plugin that
// couldn't be held to its restrictions isn't run at all.
#include <doodlebold/format_plugin.h>

namespace doodlebold::plugin_host {

// The helper's arguments: the plugin's file (a copy only Doodlebold writes, checked by its hash), then the most memory it may use,
// in bytes (0: no limit of Doodlebold's own).
inline constexpr int kPluginArgument = 1;
inline constexpr int kMemoryArgument = 2;

// Windows: the helper must be in an AppContainer (Doodlebold starts it in one), and it puts itself in a job: one process, at most
// the memory given, no clipboard or other windows. Linux: no new privileges, no network (where the system lets a user make its
// own namespaces), the memory given, no core file and no more processes.
const char* restrictBeforeLoading(int argc, char** argv);

// Loads the plugin and takes its description; the plugin's own start-up code runs here, within the restrictions so far.
const char* loadPlugin(int argc, char** argv, const doodlebold_plugin*& plugin);

// Linux: from here on only the calls the helper needs to read and write its standard input and output and to manage memory
// (seccomp); any other ends it. Windows: nothing more, the AppContainer and the job hold already.
const char* restrictAfterLoading();

}
