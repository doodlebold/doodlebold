#pragma once

#include "core/util/Result.h"

#include <doodlebold/format_plugin.h>

#include <QByteArray>
#include <QString>

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <vector>

namespace doodlebold::io {

// One format plugin's own process, doodlebold-plugin-host (src/plugin_host, plugin-process-design): the plugin's code never runs in
// Doodlebold. It starts on first use, ends by itself after a minute with nothing to do, and starts again when needed. Calls are made
// one at a time, on the caller's thread, which waits for the helper: a helper that crashes or is stopped fails that call alone.
//
// Windows starts it in an AppContainer (no network, none of the user's files) and refuses plugins where one can't be made; Linux
// starts it without new rights, network or more processes, and holds it to the system calls its loop needs (seccomp).
class PluginHost {
public:
    struct Format {
        std::uint32_t structSize = 0;
        QString id, name, extensions;
        std::uint32_t flags = 0;
        bool recognizes = false, reads = false, writes = false;
    };
    struct Description {
        QString name;
        std::uint32_t apiVersion = 0;
        std::vector<Format> formats;
    };

    // What read() and write() return when the helper ended mid-call, broke the protocol or was stopped by the user; the reason goes
    // to the call's set_error.
    static constexpr int kStopped = -1;

    // `plugin`: the approved copy the helper loads (copiedForHelper), `label` the name shown in questions and errors.
    PluginHost(QString plugin, QString label);
    ~PluginHost();
    PluginHost(const PluginHost&) = delete;
    PluginHost& operator=(const PluginHost&) = delete;

    Result<Description, QString> describe();
    bool recognizes(std::uint32_t format, const QByteArray& head);
    // The plugin's read() and write(), its calls back to `host` made here, as if it ran in this process.
    int read(std::uint32_t format, const doodlebold_read_host* host);
    int write(std::uint32_t format, const doodlebold_write_host* host);

    // A copy of the approved plugin file where the helper may load it, named by its hash and checked against it, so the file loaded
    // is the one the user allowed. Empty, with `why`, when it can't be made.
    static QString copiedForHelper(const QString& plugin, const QString& hash, QString* why);

    // Asked when a helper has sent nothing for hangSeconds(): true to keep waiting, false to stop it. Called on the waiting thread.
    static void setHangHandler(std::function<bool(const QString& plugin)> keepWaiting);
    // How long a helper may send nothing before the user is asked, in seconds: 15, the owner's choice; tests shorten it.
    static std::atomic<int>& hangSeconds();
    // doodlebold-plugin-host next to the program; tests point it at their build's.
    static QString helperPath();
    static void setHelperPath(const QString& path);

private:
    class Process;
    enum class Wait { Got, Ended, Stopped };

    bool ensureRunning(QString* why);
    bool send(std::uint32_t type, const QByteArray& body);
    Wait receive(std::uint32_t& type, QByteArray& body);
    // Why the helper is gone once a message to it couldn't be sent. One that failed as it started has said why before it ended,
    // and that is still waiting to be read.
    QString whyItEnded();
    int relayRead(const doodlebold_read_host* host);
    int relayWrite(const doodlebold_write_host* host);
    int stopped(void* context, void (*setError)(void*, const char*), const QString& why);

    QString m_plugin, m_label;
    std::mutex m_mutex;
    std::unique_ptr<Process> m_process;
    QString m_lastEnd;  // why the helper last ended, for the error
};

}
