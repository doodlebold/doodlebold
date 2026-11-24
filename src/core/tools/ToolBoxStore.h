#pragma once

#include "core/tools/ToolBox.h"
#include "core/tools/ToolBoxJson.h"

#include <QObject>
#include <QString>
#include <QTimer>

namespace doodlebold {

// Keeps a ToolBox in tools.json: load once at start-up, save a moment after the last change (a slider drag is one save) and on
// flush().
class ToolBoxStore : public QObject {
    Q_OBJECT
public:
    static constexpr int kSaveDelayMs = 500;

    ToolBoxStore(ToolBox* tools, QString path, QObject* parent = nullptr);

    static QString defaultPath();

    const QString& path() const { return m_path; }

    // On any error the tool box is left as it is (the default tool set on a first start); NoFile is the normal first-start
    // case. A file that is not the current format is renamed aside (tools.json.refused), so the next save does not overwrite it.
    Result<void, ToolBoxFileError> load();
    Result<void, ToolBoxFileError> save() const;
    void flush();
    bool savePending() const { return m_timer.isActive(); }

private:
    void saveAndReport();  // a failed background save is logged; the next change tries again
    void keepRefusedFile() const;

    ToolBox* m_tools;
    QString m_path;
    QTimer m_timer;
};

}
