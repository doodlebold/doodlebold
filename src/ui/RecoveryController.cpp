#include "ui/RecoveryController.h"

#include "core/document/Document.h"
#include "core/io/FileIo.h"

#include <QDebug>
#include <QFileInfo>
#include <QUndoStack>

namespace doodlebold {

namespace {

constexpr int kMsPerMinute = 60 * 1000;

}

RecoveryController::RecoveryController(QObject* parent) : QObject(parent) {
    m_writer.setMaxThreadCount(1);
    connect(&m_timer, &QTimer::timeout, this, &RecoveryController::saveCopies);
}

RecoveryController::~RecoveryController() {
    endRun();
    m_writer.waitForDone();
}

void RecoveryController::apply(const AppOptions::Values& values) {
    m_root = values.recoveryFolder;
    m_copiesKept = values.recoveryCopiesKept;
    if (!values.saveRecoveryCopies) {
        endRun();
        m_timer.stop();
        return;
    }
    if (!m_store || m_store->root() != m_root) {
        endRun();
        startRun();
    }
    // Restarted only when the interval changes: any other option leaves the countdown as it was.
    const int interval = values.recoveryMinutes * kMsPerMinute;
    if (!m_timer.isActive() || m_timer.interval() != interval)
        m_timer.start(interval);
}

void RecoveryController::startRun() {
    auto store = std::make_shared<RecoveryStore>(m_root);
    if (!store->isOpen()) {
        qWarning().noquote() << "No recovery copies: their folder can't be made or locked:" << m_root;
        return;
    }
    m_store = std::move(store);
    for (Canvas& canvas : m_canvases)
        canvas.changed = true;  // the new run has no copy of anything yet
}

void RecoveryController::endRun() {
    if (m_store)
        m_writer.start([store = std::move(m_store)] { store->end(); });
}

void RecoveryController::watch(Document* doc) {
    if (m_canvases.contains(doc))
        return;
    Canvas canvas;
    canvas.folder = QStringLiteral("canvas-%1").arg(++m_folders);
    m_canvases.insert(doc, canvas);
    connect(doc->undoStack(), &QUndoStack::indexChanged, this, [this, doc] {
        if (const auto it = m_canvases.find(doc); it != m_canvases.end())
            it->changed = true;
    });
    // Saved, or undone back to what its file holds: the file is what a crash would leave, so its copies would only be older.
    connect(doc, &Document::modifiedChanged, this, [this, doc](bool modified) {
        if (const auto it = m_canvases.find(doc); !modified && it != m_canvases.end())
            removeCopies(it->folder);
    });
    connect(doc, &Document::paintingChanged, this, [this, doc](bool painting) {
        if (const auto it = m_canvases.find(doc); !painting && it != m_canvases.end() && it->waitingForStroke)
            saveCopy(doc);
    });
    connect(doc, &QObject::destroyed, this, [this, doc] { forget(doc); });
}

void RecoveryController::saveCopies() {
    for (const Document* doc : m_canvases.keys())
        saveCopy(doc);
}

void RecoveryController::saveCopy(const Document* doc) {
    const auto it = m_canvases.find(doc);
    if (!m_store || it == m_canvases.end() || !doc->isModified() || !it->changed)
        return;
    // A stroke paints into the layers' tiles on threads of their own: the snapshot waits for its end.
    it->waitingForStroke = doc->isPainting();
    if (it->waitingForStroke)
        return;
    it->changed = false;
    const RecoveryManifest manifest{doc->displayName(), doc->filePath().isEmpty() ? it->originalPath : doc->filePath(),
                                    QDateTime::currentDateTimeUtc()};
    m_writer.start([store = m_store, folder = it->folder, content = doc->content(), manifest, kept = m_copiesKept] {
        if (const FileResult<> written = store->writeCopy(folder, content, manifest, kept); !written)
            qWarning().noquote() << "A recovery copy could not be written:" << written.error().describe();
    });
}

void RecoveryController::removeCopies(const QString& folder) {
    if (m_store)
        m_writer.start([store = m_store, folder] { store->removeCanvas(folder); });
}

void RecoveryController::forget(const Document* doc) {
    if (const auto it = m_canvases.find(doc); it != m_canvases.end()) {
        removeCopies(it->folder);
        m_canvases.erase(it);
    }
}

void RecoveryController::waitForWrites() {
    m_writer.waitForDone();
}

QString RecoveryController::runFolder() const {
    return m_store ? m_store->runFolder() : QString();
}

std::vector<LeftoverCanvas> RecoveryController::leftovers() const {
    return RecoveryStore::leftovers(m_root);
}

bool RecoveryController::restore(const LeftoverCanvas& canvas) {
    for (const RecoveryCopy& copy : canvas.copies) {
        const FileResult<LoadedDocument> loaded = io::loadFile(copy.file);
        if (!loaded)
            continue;
        DocumentContent content = loaded.value().content;
        const bool restoreView = content.view.has_value();
        auto* doc = new Document(std::move(content));
        // Named for its file where it had one, so a second recovery doesn't add a second mark.
        const QString name =
            copy.manifest.originalPath.isEmpty() ? copy.manifest.name : QFileInfo(copy.manifest.originalPath).completeBaseName();
        const QString mark = tr(" (recovered)");
        doc->setSourceName(name.endsWith(mark) ? name : name + mark);
        doc->undoStack()->resetClean();  // its changes are in no file of its own until it's saved
        watch(doc);
        Canvas& restored = m_canvases[doc];
        restored.originalPath = copy.manifest.originalPath;
        // Its copies become this run's, so a crash before its first copy still leaves them. With copies off they go.
        if (m_store)
            restored.changed = !m_store->adopt(canvas, restored.folder);
        else
            RecoveryStore::discard(canvas);
        emit documentRestored(doc, restoreView);
        return true;
    }
    return false;
}

void RecoveryController::discard(const LeftoverCanvas& canvas) {
    RecoveryStore::discard(canvas);
}

}
