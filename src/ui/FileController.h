#pragma once

#include "core/document/NewCanvas.h"
#include "core/io/DocumentFormat.h"

#include <QHash>
#include <QObject>
#include <QPointer>
#include <QSize>
#include <QStringList>

#include <functional>
#include <optional>

class QWidget;

namespace doodlebold {

class Document;
class DocumentTabs;
class FormatRegistry;

struct NewCanvasRequest {
    QString fileName;
    NewCanvasValues values;
    CanvasPresetList presets;
    // For a canvas that takes a while, makes it while the dialog shows it's working; see NewCanvasDialog::createWhileShowing.
    std::function<void(const NewCanvasAnswer&, std::function<void()>)> createWhileShowing = {};
};

// Every question to the user goes through a protected virtual so that tests can answer without showing anything. The
// file work runs on the calling thread under a wait cursor; it is a function of a DocumentContent snapshot, so
// a worker thread can take it over later.
class FileController : public QObject {
    Q_OBJECT
public:
    static constexpr int kMaxRecent = 10;
    static constexpr QSize kDefaultCanvas{2000, 1500};

    FileController(DocumentTabs* tabs, const FormatRegistry& registry, QWidget* dialogParent, QObject* parent = nullptr);

    void newDocument();
    // A picture beyond kMaxCanvasSide is scaled down to fit, if the user wants.
    void newDocumentFromPicture(QImage picture);
    void open();
    bool openFile(const QString& path);
    bool save(Document* doc);
    bool saveAs(Document* doc);
    bool exportAs(Document* doc, const DocumentFormat& format);

    static CanvasPresetList savedPresets();
    static void savePresets(const CanvasPresetList& presets);

    QStringList recentFiles() const;
    void clearRecent();

    // The file a Save As ends up with: the extension in `path` names the format; without a known extension the dialog's
    // chosen format supplies one. Null format if neither says. Pure, for tests.
    static std::pair<QString, const DocumentFormat*> resolveSaveTarget(const FormatRegistry& registry, const QString& path,
                                                                       const DocumentFormat* chosen);

signals:
    void documentCreated(doodlebold::Document* doc, bool restoreView);
    void recentFilesChanged();
    void message(const QString& text);

protected:
    virtual std::optional<NewCanvasAnswer> askNewCanvas(const NewCanvasRequest& request);
    void createShowingProgress(const NewCanvasAnswer& answer, std::function<void()> done);
    virtual QStringList askOpenPaths();
    virtual QString askSavePath(const Document* doc, const DocumentFormat** chosen);
    virtual QString askExportPath(const QString& suggested, const DocumentFormat& format);
    virtual std::optional<ExportOptions> askExportOptions(const DocumentFormat& format, const DocumentContent& content,
                                                          const ExportOptions& initial);
    virtual void showError(const QString& title, const QString& text);
    virtual void showNotes(const QString& title, const QStringList& notes);
    // "`what` is W × H pixels, and canvases are at most …: scale it down?" True to scale it down.
    virtual bool askScaleDown(const QString& title, const QString& what, QSize size);

private:
    void rememberOptions(Document* doc, const ExportOptions& options);
    bool write(Document* doc, const QString& path, const DocumentFormat& format, const ExportOptions& options, bool saving);
    void addRecent(const QString& path);
    QString lastDirectory() const;
    void setLastDirectory(const QString& path);

    DocumentTabs* m_tabs;
    const FormatRegistry& m_registry;
    QWidget* m_parent;
    QHash<const Document*, ExportOptions> m_options;
    int m_newCanvasCounter = 1;
};

}
