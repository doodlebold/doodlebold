#pragma once

#include "core/io/RecoveryStore.h"
#include "core/options/AppOptions.h"

#include <QHash>
#include <QObject>
#include <QThreadPool>
#include <QTimer>

#include <memory>
#include <vector>

namespace doodlebold {

class Document;

// Options › Recovery at work. Every few minutes each canvas with unsaved changes since its last copy gets a recovery copy,
// written from a snapshot (DocumentContent) on a thread of its own so painting goes on meanwhile. Saving or closing a canvas,
// or a normal end of the program, deletes its copies; the next start offers the ones a crash left (leftovers()).
class RecoveryController : public QObject {
    Q_OBJECT
public:
    explicit RecoveryController(QObject* parent = nullptr);
    // A normal end: the run's copies are deleted once the copies being written are done.
    ~RecoveryController() override;

    void apply(const AppOptions::Values& values);
    void watch(Document* doc);
    // Takes a copy of every canvas that wants one; the timer calls it.
    void saveCopies();
    // Until every copy, and every deletion, asked for so far is done.
    void waitForWrites();
    QString runFolder() const;

    std::vector<LeftoverCanvas> leftovers() const;
    // Opens a leftover canvas, from its newest copy that can be read, as a canvas with unsaved changes and no file, so saving it
    // asks where and the original is never overwritten unasked. False if none of its copies could be read.
    bool restore(const LeftoverCanvas& canvas);
    void discard(const LeftoverCanvas& canvas);

signals:
    void documentRestored(doodlebold::Document* doc, bool restoreView);

private:
    struct Canvas {
        QString folder;  // in the run's folder
        bool changed = true;  // since its last copy
        bool waitingForStroke = false;  // its copy was due while a stroke painted
        QString originalPath;  // a restored canvas's file, which it no longer has as its own
    };

    void saveCopy(const Document* doc);
    void removeCopies(const QString& folder);
    void forget(const Document* doc);
    void startRun();
    void endRun();

    std::shared_ptr<RecoveryStore> m_store;  // shared with the copies being written, which may outlast a change of folder
    QString m_root;
    int m_copiesKept = AppOptions::kDefaultRecoveryCopiesKept;
    QTimer m_timer;
    // One thread, so copies, deletions and the run's end happen in the order they were asked for.
    QThreadPool m_writer;
    QHash<const Document*, Canvas> m_canvases;
    int m_folders = 0;
};

}
