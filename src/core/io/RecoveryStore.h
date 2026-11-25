#pragma once

#include "core/document/DocumentContent.h"
#include "core/io/FileError.h"

#include <QDateTime>
#include <QString>

#include <memory>
#include <optional>
#include <vector>

class QLockFile;

namespace doodlebold {

// What a recovery copy's manifest, the JSON file beside it, says of the canvas the copy was taken from.
struct RecoveryManifest {
    QString name;          // the canvas's name, as its window showed it
    QString originalPath;  // the file it was opened from or saved to; empty for a canvas never saved
    QDateTime savedAt;     // when the copy was taken
    bool operator==(const RecoveryManifest&) const = default;
};

struct RecoveryCopy {
    QString file;  // the .doodle file
    RecoveryManifest manifest;
};

// A canvas that a run which ended without closing left behind: its folder and its copies, the newest first.
struct LeftoverCanvas {
    QString folder;
    std::vector<RecoveryCopy> copies;
};

// The recovery copies of one run of Doodlebold: root/run-<process>-<start>-<n>/<canvas>/<time>.doodle, each with <time>.json. The
// run's folder holds a lock while the run lives, so another copy of the program running at the same time leaves it alone, and a
// run that ended without end() (a crash, a kill) shows by a lock whose process is gone. The instance's file work may run on any
// one thread at a time.
class RecoveryStore {
public:
    explicit RecoveryStore(const QString& root);
    // Lets the lock go and keeps the files, as a crash would: only end() deletes them.
    ~RecoveryStore();
    RecoveryStore(const RecoveryStore&) = delete;
    RecoveryStore& operator=(const RecoveryStore&) = delete;

    // False if the run's folder couldn't be made or locked, or the run has ended: then nothing is written.
    bool isOpen() const;
    QString root() const { return m_root; }
    QString runFolder() const { return m_run; }

    // Writes a copy of the canvas whose folder is `canvas`, and deletes all but its `keep` newest copies.
    FileResult<> writeCopy(const QString& canvas, const DocumentContent& content, const RecoveryManifest& manifest, int keep) const;
    void removeCanvas(const QString& canvas) const;
    // A normal close: the run's copies and folder are deleted.
    void end();
    // Moves a leftover canvas's copies into this run as `canvas`, so they stay recoverable until this run writes its own.
    bool adopt(const LeftoverCanvas& leftover, const QString& canvas) const;

    // The canvases of the runs under `root` that ended without end(), the most recently copied first. Their runs' folders that
    // hold no copy are deleted.
    static std::vector<LeftoverCanvas> leftovers(const QString& root);
    // Deletes a leftover canvas's copies, and its run's folder once that holds no other canvas.
    static void discard(const LeftoverCanvas& leftover);

    static bool writeManifest(const QString& path, const RecoveryManifest& manifest);
    static std::optional<RecoveryManifest> readManifest(const QString& path);
    // The copies in a canvas's folder that have a manifest, the newest first.
    static std::vector<RecoveryCopy> copiesIn(const QString& canvasFolder);

private:
    QString m_root;
    QString m_run;
    std::unique_ptr<QLockFile> m_lock;
};

}
