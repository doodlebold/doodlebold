#include "core/io/RecoveryStore.h"

#include "core/io/DoodleFormat.h"
#include "core/io/FileIo.h"

#include <QCoreApplication>
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLockFile>
#include <QSaveFile>

#include <algorithm>
#include <atomic>

namespace doodlebold {

namespace {

const QString kRunPattern = QStringLiteral("run-*");
const QString kLockName = QStringLiteral("run.lock");
const QString kCopySuffix = QStringLiteral(".doodle");
const QString kManifestSuffix = QStringLiteral(".json");
// A copy's file name: when it was taken, in UTC to the millisecond, so the names sort as the copies were taken.
const QString kStampFormat = QStringLiteral("yyyyMMdd-HHmmss-zzz");
const QString kNameKey = QStringLiteral("name");
const QString kOriginalPathKey = QStringLiteral("originalPath");
const QString kSavedAtKey = QStringLiteral("savedAt");

QString manifestOf(const QString& copy) {
    return copy.chopped(kCopySuffix.size()) + kManifestSuffix;
}

// The copies' files in a canvas's folder, the newest first.
QFileInfoList copyFiles(const QString& canvasFolder) {
    return QDir(canvasFolder).entryInfoList({QLatin1Char('*') + kCopySuffix}, QDir::Files, QDir::Name | QDir::Reversed);
}

void keepNewest(const QString& canvasFolder, int keep) {
    const QFileInfoList copies = copyFiles(canvasFolder);
    for (qsizetype i = keep; i < copies.size(); ++i) {
        QFile::remove(manifestOf(copies[i].absoluteFilePath()));
        QFile::remove(copies[i].absoluteFilePath());
    }
}

// A run's lock is taken only by a process that is gone when nothing holds it. Never stale by age: a run lasts for hours.
bool inUse(const QString& run) {
    QLockFile lock(run + QLatin1Char('/') + kLockName);
    lock.setStaleLockTime(0);
    if (lock.tryLock(0)) {
        lock.unlock();
        return false;
    }
    return lock.error() == QLockFile::LockFailedError;
}

// A run that ended without closing holds nothing more once its canvases are restored or discarded.
void removeRunIfEmpty(const QString& run) {
    if (QDir(run).entryList(QDir::Dirs | QDir::NoDotAndDotDot).isEmpty())
        QDir(run).removeRecursively();
}

QDateTime newestCopy(const LeftoverCanvas& canvas) {
    return canvas.copies.front().manifest.savedAt;
}

}

RecoveryStore::RecoveryStore(const QString& root) : m_root(root) {
    // The start time as well, as a process number comes round again, so a new run never takes up an old run's folder; and a
    // count, so two stores made in the same millisecond never share one.
    static std::atomic<int> made{0};
    const QString run = root + QStringLiteral("/run-%1-%2-%3")
                                   .arg(QCoreApplication::applicationPid())
                                   .arg(QDateTime::currentMSecsSinceEpoch())
                                   .arg(++made);
    if (!QDir().mkpath(run))
        return;
    auto lock = std::make_unique<QLockFile>(run + QLatin1Char('/') + kLockName);
    if (!lock->tryLock(0))
        return;
    m_run = run;
    m_lock = std::move(lock);
}

RecoveryStore::~RecoveryStore() = default;

bool RecoveryStore::isOpen() const {
    return m_lock && m_lock->isLocked();
}

FileResult<> RecoveryStore::writeCopy(const QString& canvas, const DocumentContent& content, const RecoveryManifest& manifest,
                                      int keep) const {
    const QString folder = m_run + QLatin1Char('/') + canvas;
    if (!isOpen() || !QDir().mkpath(folder))
        return FileResult<>::failure({FileError::Kind::CannotWrite, folder});
    const QString copy = folder + QLatin1Char('/') + manifest.savedAt.toUTC().toString(kStampFormat) + kCopySuffix;
    if (FileResult<> written = io::saveFile(content, copy, DoodleFormat()); !written)
        return written;
    // After the copy, so a manifest always has its copy: a copy without one is never offered.
    if (!writeManifest(manifestOf(copy), manifest))
        return FileResult<>::failure({FileError::Kind::CannotWrite, manifestOf(copy)});
    keepNewest(folder, keep);
    return {};
}

void RecoveryStore::removeCanvas(const QString& canvas) const {
    if (isOpen())
        QDir(m_run + QLatin1Char('/') + canvas).removeRecursively();
}

void RecoveryStore::end() {
    if (!isOpen())
        return;
    m_lock.reset();  // first: Windows deletes no file that is open
    QDir(m_run).removeRecursively();
}

bool RecoveryStore::adopt(const LeftoverCanvas& leftover, const QString& canvas) const {
    if (!isOpen() || !QDir().rename(leftover.folder, m_run + QLatin1Char('/') + canvas))
        return false;
    removeRunIfEmpty(QFileInfo(leftover.folder).absolutePath());
    return true;
}

std::vector<LeftoverCanvas> RecoveryStore::leftovers(const QString& root) {
    std::vector<LeftoverCanvas> found;
    for (const QFileInfo& run : QDir(root).entryInfoList({kRunPattern}, QDir::Dirs | QDir::NoDotAndDotDot)) {
        if (inUse(run.absoluteFilePath()))
            continue;
        for (const QFileInfo& canvas : QDir(run.absoluteFilePath()).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot)) {
            std::vector<RecoveryCopy> copies = copiesIn(canvas.absoluteFilePath());
            if (copies.empty())
                QDir(canvas.absoluteFilePath()).removeRecursively();
            else
                found.push_back({canvas.absoluteFilePath(), std::move(copies)});
        }
        removeRunIfEmpty(run.absoluteFilePath());
    }
    std::ranges::sort(found, [](const LeftoverCanvas& a, const LeftoverCanvas& b) { return newestCopy(a) > newestCopy(b); });
    return found;
}

void RecoveryStore::discard(const LeftoverCanvas& leftover) {
    QDir(leftover.folder).removeRecursively();
    removeRunIfEmpty(QFileInfo(leftover.folder).absolutePath());
}

bool RecoveryStore::writeManifest(const QString& path, const RecoveryManifest& manifest) {
    const QJsonObject json{{kNameKey, manifest.name},
                           {kOriginalPathKey, manifest.originalPath},
                           {kSavedAtKey, manifest.savedAt.toUTC().toString(Qt::ISODateWithMs)}};
    QSaveFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(QJsonDocument(json).toJson()) >= 0 && file.commit();
}

std::optional<RecoveryManifest> RecoveryStore::readManifest(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return std::nullopt;
    const QJsonDocument json = QJsonDocument::fromJson(file.readAll());
    if (!json.isObject())
        return std::nullopt;
    const QJsonObject o = json.object();
    RecoveryManifest manifest{o.value(kNameKey).toString(), o.value(kOriginalPathKey).toString(),
                              QDateTime::fromString(o.value(kSavedAtKey).toString(), Qt::ISODateWithMs)};
    if (manifest.name.isEmpty() || !manifest.savedAt.isValid())
        return std::nullopt;
    return manifest;
}

std::vector<RecoveryCopy> RecoveryStore::copiesIn(const QString& canvasFolder) {
    std::vector<RecoveryCopy> copies;
    for (const QFileInfo& copy : copyFiles(canvasFolder))
        if (std::optional<RecoveryManifest> manifest = readManifest(manifestOf(copy.absoluteFilePath())))
            copies.push_back({copy.absoluteFilePath(), std::move(*manifest)});
    return copies;
}

}
