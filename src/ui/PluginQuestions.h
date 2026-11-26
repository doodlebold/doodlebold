#pragma once

#include <QString>

class QWidget;

// The questions about format plugins at start: a plugin runs inside Doodlebold with the user's rights, so none is loaded before they
// say yes to it, and one the last crash was in waits for their say before it runs again.
namespace doodlebold::plugin_questions {

enum class NewPluginAnswer {
    Load,
    NotNow,  // asked again at the next start
    Never,
};

// A plugin file not seen before, or changed since the user's yes.
NewPluginAnswer askAboutNew(QWidget* parent, const QString& path);

// The plugin has sent nothing for a while (io::PluginHost::hangSeconds): true to keep waiting, false to stop it. Nothing is stopped
// without asking, as a large file can take long.
bool keepWaitingFor(QWidget* parent, const QString& plugin);

// Doodlebold crashed in this plugin when it last ran: true to keep it off, false to load it again.
bool keepOffAfterCrash(QWidget* parent, const QString& path);

}
