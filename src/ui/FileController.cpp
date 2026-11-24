#include "ui/FileController.h"

#include "core/util/AppIdentity.h"

#include "core/document/Clipboard.h"
#include "core/document/Document.h"
#include "core/io/FileIo.h"
#include "core/io/FormatRegistry.h"
#include "core/io/ImageConvert.h"
#include "ui/CanvasView.h"
#include "ui/DocumentTabs.h"
#include "ui/JpegQualityDialog.h"
#include "ui/NewCanvasDialog.h"

#include <QFileDialog>
#include <QFileInfo>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QThread>
#include <QTimer>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>

namespace doodlebold {

namespace {

class WaitCursor {
public:
    WaitCursor() { QGuiApplication::setOverrideCursor(Qt::WaitCursor); }
    ~WaitCursor() { QGuiApplication::restoreOverrideCursor(); }
    WaitCursor(const WaitCursor&) = delete;
    WaitCursor& operator=(const WaitCursor&) = delete;
};

const QString kNewCanvasKey = QStringLiteral("files/newCanvas");
constexpr int kLongestFirstFrameWaitMs = 15000;
const QString kPresetsKey = QStringLiteral("files/canvasPresets");

QJsonDocument readJson(const QSettings& s, const QString& key) {
    return QJsonDocument::fromJson(s.value(key).toString().toUtf8());
}

QString canonical(const QString& path) {
    const QString c = QFileInfo(path).canonicalFilePath();
    return c.isEmpty() ? QFileInfo(path).absoluteFilePath() : c;
}

}

FileController::FileController(DocumentTabs* tabs, const FormatRegistry& registry, QWidget* dialogParent, QObject* parent)
    : QObject(parent), m_tabs(tabs), m_registry(registry), m_parent(dialogParent) {}

CanvasPresetList FileController::savedPresets() {
    const QSettings s = app_identity::settings();
    const QJsonDocument stored = readJson(s, kPresetsKey);
    if (stored.isArray())
        if (auto list = presetsFromJson(stored.array()))
            return *list;
    return defaultCanvasPresets();
}

void FileController::savePresets(const CanvasPresetList& presets) {
    QSettings s = app_identity::settings();
    s.setValue(kPresetsKey, QString::fromUtf8(QJsonDocument(presetsToJson(presets)).toJson(QJsonDocument::Compact)));
}

std::optional<NewCanvasAnswer> FileController::askNewCanvas(const NewCanvasRequest& request) {
    NewCanvasDialog dlg(request.fileName, request.values, request.presets, m_parent);
    // The preset list is kept as soon as it is edited, even if the canvas is not created.
    connect(&dlg, &NewCanvasDialog::presetsChanged, &dlg, [](const CanvasPresetList& presets) { savePresets(presets); });
    dlg.createWhileShowing = request.createWhileShowing;
    if (dlg.exec() != QDialog::Accepted)
        return std::nullopt;
    return dlg.answer();
}

QStringList FileController::askOpenPaths() {
    return QFileDialog::getOpenFileNames(m_parent, tr("Open"), lastDirectory(), m_registry.openFilter());
}

QString FileController::askSavePath(const Document* doc, const DocumentFormat** chosen) {
    const auto formats = m_registry.writable();
    QStringList filters;
    for (const DocumentFormat* f : formats)
        filters << FormatRegistry::filterFor(*f);
    const QString current = doc->filePath();
    const DocumentFormat* start = current.isEmpty() ? nullptr : m_registry.forPath(current, true);
    if (!start && !formats.empty())
        start = formats.front();
    const QString dir = current.isEmpty() ? lastDirectory() : QFileInfo(current).absolutePath();
    const QString baseName = current.isEmpty() ? QFileInfo(doc->displayName()).completeBaseName() : QFileInfo(current).completeBaseName();

    QFileDialog dlg(m_parent, tr("Save As"), dir);
    dlg.setAcceptMode(QFileDialog::AcceptSave);
    dlg.setFileMode(QFileDialog::AnyFile);
    dlg.setNameFilters(filters);
    if (start)
        dlg.selectNameFilter(FormatRegistry::filterFor(*start));
    dlg.selectFile(baseName + QLatin1Char('.') + (start ? start->extensions().first() : QStringLiteral("doodle")));
    if (dlg.exec() != QDialog::Accepted || dlg.selectedFiles().isEmpty())
        return {};
    const int index = static_cast<int>(filters.indexOf(dlg.selectedNameFilter()));
    *chosen = index >= 0 ? formats[static_cast<std::size_t>(index)] : start;
    return dlg.selectedFiles().first();
}

QString FileController::askExportPath(const QString& suggested, const DocumentFormat& format) {
    return QFileDialog::getSaveFileName(m_parent, tr("Export %1").arg(format.name()), suggested, FormatRegistry::filterFor(format));
}

std::optional<ExportOptions> FileController::askExportOptions(const DocumentFormat&, const DocumentContent& content,
                                                              const ExportOptions& initial) {
    JpegQualityDialog dlg(io::flattenedPicture(content, false), initial, content.resolution, m_parent);
    if (dlg.exec() != QDialog::Accepted)
        return std::nullopt;
    return dlg.options();  // only OK hands the settings back: Cancel remembers nothing
}

void FileController::showError(const QString& title, const QString& text) {
    QMessageBox::warning(m_parent, title, text);
}

void FileController::showNotes(const QString& title, const QStringList& notes) {
    QMessageBox::information(m_parent, title, notes.join(QStringLiteral("\n\n")));
}

void FileController::newDocument() {
    QSettings s = app_identity::settings();
    NewCanvasValues values;
    if (const QJsonDocument stored = readJson(s, kNewCanvasKey); stored.isObject())
        values = newCanvasValuesFromJson(stored.object());
    bool created = false;
    const auto answer = askNewCanvas({QStringLiteral("NewCanvas%1").arg(m_newCanvasCounter), values, savedPresets(),
                                      [this, &created](const NewCanvasAnswer& given, std::function<void()> done) {
                                          created = true;
                                          createShowingProgress(given, std::move(done));
                                      }});
    if (!answer)
        return;
    ++m_newCanvasCounter;
    s.setValue(kNewCanvasKey, QString::fromUtf8(QJsonDocument(newCanvasValuesToJson(answer->values)).toJson(QJsonDocument::Compact)));
    if (created)
        return;
    auto* doc = new Document(answer->values.pixels, answer->values.background, answer->values.resolution);
    doc->setSourceName(answer->fileName);
    emit documentCreated(doc, false);
}

// The picture of a new canvas is composed on a thread of its own while the dialog is up; what has to be on the window's thread (the
// graphics textures, its first frame) follows, and `done` is called when that frame is on screen.
void FileController::createShowingProgress(const NewCanvasAnswer& answer, std::function<void()> done) {
    auto* doc = new Document(answer.values.pixels, answer.values.background, answer.values.resolution);
    doc->setSourceName(answer.fileName);
    QThread* composing = QThread::create([doc] { doc->displayImage(); });
    connect(composing, &QThread::finished, this, [this, composing, doc, done = std::move(done)] {
        composing->deleteLater();
        emit documentCreated(doc, false);
        CanvasView* view = m_tabs->currentCanvas();
        auto shown = std::make_shared<QMetaObject::Connection>();
        const auto finish = std::make_shared<std::function<void()>>([shown, done] {
            if (!*shown)
                return;
            QObject::disconnect(*shown);
            *shown = {};
            done();
        });
        *shown = connect(view, &QOpenGLWidget::frameSwapped, this, *finish);
        // A view that can't draw (no OpenGL) never swaps a frame: it isn't waited for.
        QTimer::singleShot(kLongestFirstFrameWaitMs, this, *finish);
    });
    composing->start();
}

bool FileController::askScaleDown(const QString& title, const QString& what, QSize size) {
    const QSize fitted = fittedToCanvasLimit(size);
    QMessageBox box(QMessageBox::Question, title,
                    tr("%1 is %2 × %3 pixels, and canvases are at most %4 × %4.\n\nScale it down to %5 × %6?")
                        .arg(what)
                        .arg(size.width())
                        .arg(size.height())
                        .arg(kMaxCanvasSide)
                        .arg(fitted.width())
                        .arg(fitted.height()),
                    QMessageBox::Cancel, m_parent);
    QPushButton* scale = box.addButton(tr("Scale Down"), QMessageBox::AcceptRole);
    box.setDefaultButton(scale);
    box.exec();
    return box.clickedButton() == scale;
}

void FileController::newDocumentFromPicture(QImage picture) {
    if (picture.isNull())
        return;
    if (beyondCanvasLimit(picture.size())) {
        if (!askScaleDown(tr("New from Clipboard"), tr("The picture on the clipboard"), picture.size()))
            return;
        picture = picture.scaled(fittedToCanvasLimit(picture.size()), Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    }
    DocumentContent content;
    content.state.layers = LayerStack(picture.size());
    LayerNode layer = content.state.layers.makeRaster(content.state.layers.nextLayerName());
    ClipboardImage clip;
    clip.image = picture.convertToFormat(QImage::Format_RGBA8888);
    layer.pixels = clipboard::toPixels(clip, picture.size());
    content.state.current = content.state.layers.insert(std::move(layer), kNoLayer, -1);
    auto* doc = new Document(std::move(content));
    doc->setSourceName(QStringLiteral("NewCanvas%1").arg(m_newCanvasCounter++));
    emit documentCreated(doc, false);
}

void FileController::open() {
    for (const QString& path : askOpenPaths())
        openFile(path);
}

bool FileController::openFile(const QString& path) {
    const QString where = canonical(path);
    for (int i = 0; i < m_tabs->count(); ++i)
        if (const CanvasView* v = m_tabs->canvasAt(i);
            v && v->document() && !v->document()->filePath().isEmpty() && canonical(v->document()->filePath()) == where) {
            m_tabs->setCurrentIndex(i);
            return true;
        }

    FileResult<LoadedDocument> loaded = FileResult<LoadedDocument>::failure({});
    {
        WaitCursor wait;
        loaded = io::loadFile(path);
    }
    // A flat picture beyond the canvas limit opens scaled down, if the user wants.
    const bool scaled = !loaded && loaded.error().kind == FileError::Kind::TooLarge && loaded.error().scalable;
    if (scaled) {
        if (!askScaleDown(tr("Open"), tr("“%1”").arg(QFileInfo(path).fileName()), loaded.error().size))
            return false;
        WaitCursor wait;
        loaded = io::loadFileScaledToFit(path);
    }
    if (!loaded) {
        showError(tr("Open"), tr("“%1” could not be opened.\n\n%2").arg(QFileInfo(path).fileName(), loaded.error().describe()));
        return false;
    }
    LoadedDocument file = loaded.value();
    const bool restoreView = file.content.view.has_value();
    auto* doc = new Document(std::move(file.content));
    const DocumentFormat* format = m_registry.forPath(path, false);
    if (format && format->isLossless() && !scaled)
        doc->markSaved(where, false);
    else
        doc->setSourceName(QFileInfo(path).fileName());
    setLastDirectory(path);
    addRecent(where);
    emit documentCreated(doc, restoreView);
    if (!file.notes.isEmpty())
        showNotes(tr("Opened “%1”").arg(QFileInfo(path).fileName()), file.notes);
    return true;
}

std::pair<QString, const DocumentFormat*> FileController::resolveSaveTarget(const FormatRegistry& registry, const QString& path,
                                                                            const DocumentFormat* chosen) {
    if (const DocumentFormat* byName = registry.forPath(path, true))
        return {path, byName};
    if (!chosen)
        return {path, nullptr};
    return {path + QLatin1Char('.') + chosen->extensions().first(), chosen};
}

void FileController::rememberOptions(Document* doc, const ExportOptions& options) {
    m_options.insert(doc, options);
    connect(
        doc, &QObject::destroyed, this, [this](QObject* d) { m_options.remove(static_cast<const Document*>(d)); }, Qt::UniqueConnection);
}

bool FileController::write(Document* doc, const QString& path, const DocumentFormat& format, const ExportOptions& options, bool saving) {
    const DocumentContent content = doc->content();
    FileResult<> result;
    {
        WaitCursor wait;
        result = io::saveFile(content, path, format, options);
    }
    if (!result) {
        showError(saving ? tr("Save") : tr("Export"),
                  tr("“%1” could not be written.\n\n%2").arg(QFileInfo(path).fileName(), result.error().describe()));
        return false;
    }
    setLastDirectory(path);
    return true;
}

bool FileController::save(Document* doc) {
    if (!doc)
        return false;
    const DocumentFormat* format = doc->filePath().isEmpty() ? nullptr : m_registry.forPath(doc->filePath(), true);
    if (!format)
        return saveAs(doc);
    if (!write(doc, doc->filePath(), *format, m_options.value(doc), true))
        return false;
    doc->markSaved(doc->filePath(), !format->isLossless());
    emit message(tr("Saved “%1”").arg(doc->displayName()));
    return true;
}

bool FileController::saveAs(Document* doc) {
    if (!doc)
        return false;
    const DocumentFormat* chosen = nullptr;
    const QString asked = askSavePath(doc, &chosen);
    if (asked.isEmpty())
        return false;
    const auto [path, format] = resolveSaveTarget(m_registry, asked, chosen);
    if (!format) {
        showError(tr("Save As"), tr("“%1” has an extension no format is known for.").arg(QFileInfo(path).fileName()));
        return false;
    }
    ExportOptions options = m_options.value(doc);
    if (JpegQualityDialog::hasOptions(format->features()) && !format->isLossless()) {
        const auto answer = askExportOptions(*format, doc->content(), options);
        if (!answer)
            return false;
        options = *answer;
    }
    if (!write(doc, path, *format, options, true))
        return false;
    rememberOptions(doc, options);
    doc->markSaved(canonical(path), !format->isLossless());
    addRecent(canonical(path));
    emit message(tr("Saved “%1”").arg(doc->displayName()));
    return true;
}

bool FileController::exportAs(Document* doc, const DocumentFormat& format) {
    if (!doc)
        return false;
    const QString base = QFileInfo(doc->displayName()).completeBaseName();
    const QString dir = doc->filePath().isEmpty() ? lastDirectory() : QFileInfo(doc->filePath()).absolutePath();
    const QString asked = askExportPath(dir + QLatin1Char('/') + base + QLatin1Char('.') + format.extensions().first(), format);
    if (asked.isEmpty())
        return false;
    QString path = asked;
    if (!format.extensions().contains(QFileInfo(path).suffix().toLower()))
        path += QLatin1Char('.') + format.extensions().first();
    ExportOptions options = m_options.value(doc);
    if (JpegQualityDialog::hasOptions(format.features())) {
        const auto answer = askExportOptions(format, doc->content(), options);
        if (!answer)
            return false;
        options = *answer;
    }
    if (!write(doc, path, format, options, false))
        return false;
    if (JpegQualityDialog::hasOptions(format.features()))
        rememberOptions(doc, options);
    emit message(tr("Exported “%1”").arg(QFileInfo(path).fileName()));
    return true;
}

QStringList FileController::recentFiles() const {
    return app_identity::settings().value(QStringLiteral("files/recent")).toStringList();
}

void FileController::addRecent(const QString& path) {
    QSettings s = app_identity::settings();
    QStringList recent = s.value(QStringLiteral("files/recent")).toStringList();
    recent.removeAll(path);
    recent.prepend(path);
    while (recent.size() > kMaxRecent)
        recent.removeLast();
    s.setValue(QStringLiteral("files/recent"), recent);
    emit recentFilesChanged();
}

void FileController::clearRecent() {
    app_identity::settings().remove(QStringLiteral("files/recent"));
    emit recentFilesChanged();
}

QString FileController::lastDirectory() const {
    const QString dir = app_identity::settings().value(QStringLiteral("files/lastDirectory")).toString();
    return QFileInfo(dir).isDir() ? dir : QString();
}

void FileController::setLastDirectory(const QString& path) {
    app_identity::settings().setValue(QStringLiteral("files/lastDirectory"), QFileInfo(path).absolutePath());
}

}
