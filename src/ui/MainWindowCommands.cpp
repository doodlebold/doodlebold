#include "ui/MainWindow.h"

#include "core/document/Document.h"
#include "core/io/FormatRegistry.h"
#include "core/options/AppOptions.h"
#include "core/tools/ToolBox.h"
#include "core/util/ProjectLinks.h"
#include "core/view/ViewText.h"
#include "platform/CrashReport.h"
#include "ui/CanvasView.h"
#include "ui/DocumentTabs.h"
#include "ui/FileController.h"
#include "ui/MenuSpec.h"
#include "ui/EffectController.h"
#include "core/effects/EffectSettings.h"
#include "ui/timelapse/TimelapseController.h"
#include "core/color/ColorAssets.h"
#include "ui/PanelCard.h"
#include "ui/panels/LayerPanel.h"
#include "ui/panels/ToolPanel.h"
#include "ui/panels/ToolParameters.h"
#include "ui/MemoryNotice.h"
#include "ui/panels/ColorPanel.h"

#include <QAction>
#include <QActionGroup>
#include <QClipboard>
#include <QColorDialog>
#include <QDir>
#include <QDockWidget>
#include <QFileInfo>
#include <QGuiApplication>
#include <QLayout>
#include <QMenu>
#include <QMenuBar>
#include <QMimeData>
#include <QSignalBlocker>
#include <QStatusBar>
#include <QTimer>
#include <QUndoStack>

#include <array>
#include <utility>

namespace doodlebold {

namespace {

constexpr struct {
    const char* id;
    BackgroundMode mode;
} kBackgrounds[] = {
    {"canvas.background.transparencyColor", BackgroundMode::TransparencyColor},
    {"canvas.background.transparencyBlack", BackgroundMode::TransparencyBlack},
    {"canvas.background.transparencyWhite", BackgroundMode::TransparencyWhite},
    {"canvas.background.transparencyDarkChecker", BackgroundMode::TransparencyDarkChecker},
    {"canvas.background.transparencyBrightChecker", BackgroundMode::TransparencyBrightChecker},
    {"canvas.background.color", BackgroundMode::Color},
    {"canvas.background.black", BackgroundMode::Black},
    {"canvas.background.white", BackgroundMode::White},
};

constexpr std::array<const char*, 6> kExportExtensions = {"psd", "psb", "bmp", "jpg", "png", "tga"};

const DocumentFormat* exportFormat(const QString& extension) {
    for (const DocumentFormat* f : FormatRegistry::instance().writable())
        if (f->id() != QLatin1String("doodle") && f->extensions().contains(extension))
            return f;
    return nullptr;
}

// Most paths of the recent list fit a menu; a longer one keeps its end ("...pencil-ref.png").
QString recentLabel(int number, const QString& path) {
    constexpr int kMaxChars = 60;
    QString shown = QDir::toNativeSeparators(path);
    if (shown.size() > kMaxChars)
        shown = QStringLiteral("...") + shown.right(kMaxChars - 3);
    shown.replace(QLatin1Char('&'), QStringLiteral("&&"));
    return QStringLiteral("&%1 %2").arg(number % 10).arg(shown);
}

}

void MainWindow::bind(const QString& id, std::function<void()> run, Enable enable) {
    Binding b;
    b.run = std::move(run);
    b.enable = enable;
    m_bindings.insert(id, std::move(b));
}

void MainWindow::bindCheck(const QString& id, bool checked, std::function<void(bool)> toggled, Enable enable) {
    Binding b;
    b.toggled = std::move(toggled);
    b.checkedAtStart = checked;
    b.enable = enable;
    m_bindings.insert(id, std::move(b));
}

QAction* MainWindow::actionFor(std::string_view id) {
    const QString key = QString::fromUtf8(id.data(), static_cast<qsizetype>(id.size()));
    if (QAction* existing = m_actions.value(key))
        return existing;  // Select All sits in two menus and the quick bar shares Undo: one action
    const auto it = m_bindings.constFind(key);
    if (it == m_bindings.constEnd())
        return nullptr;
    auto* a = new QAction(this);
    a->setObjectName(key);
    if (it->toggled) {
        a->setCheckable(true);
        a->setChecked(it->checkedAtStart);
        connect(a, &QAction::toggled, this, it->toggled);
    } else {
        connect(a, &QAction::triggered, this, [run = it->run] { run(); });
    }
    m_actions.insert(key, a);
    return a;
}

bool MainWindow::populate(const menu::Entry& entry, QMenu* menu) {
    if (entry.id == "file.recent") {
        connect(menu, &QMenu::aboutToShow, this, [this, menu] { rebuildRecentMenu(menu); });
        connect(m_files, &FileController::recentFilesChanged, menu, [this, menu] { rebuildRecentMenu(menu); });
        rebuildRecentMenu(menu);
        return true;
    }
    if (entry.id == "canvas.background")
        connect(menu, &QMenu::aboutToShow, this, &MainWindow::updateBackgroundMenu);
    return false;
}

void MainWindow::buildMenus() {
    registerFileCommands();
    registerEditCommands();
    registerCanvasCommands();
    registerAnimationCommands();
    registerLayerCommands();
    registerEffectCommands();
    registerSelectionCommands();
    registerViewCommands();
    registerWindowCommands();
    registerOtherCommands();
    registerKeyCommands();

    menu::Binder binder;
    binder.action = [this](const menu::Entry& e) { return actionFor(e.id); };
    binder.populate = [this](const menu::Entry& e, QMenu* m) { return populate(e, m); };
    binder.keyAction = [this](const menu::KeyCommand& k) { return actionFor(k.id); };
    menu::build(menuBar(), this, binder);

    for (QAction* top : menuBar()->actions())
        if (top->objectName() == QLatin1String("View"))
            connect(top->menu(), &QMenu::aboutToShow, this, [this, menu = top->menu()] { rebuildViewList(menu); });
    connect(QGuiApplication::clipboard(), &QClipboard::dataChanged, this, [this] {
        noteClipboard();
        updateCommandStates();
    });
    noteClipboard();
    updateCommandStates();
}

bool MainWindow::isEnabled(Enable e) const {
    Document* d = m_tabs->currentDocument();
    switch (e) {
    case Enable::Always: return true;
    case Enable::Document: return d != nullptr;
    case Enable::Selection: return d && d->selection().has_value();
    case Enable::CanUndo: return d && d->undoStack()->canUndo();
    case Enable::CanRedo: return d && d->undoStack()->canRedo();
    case Enable::Paste: return d && (m_clip.has_value() || m_clipboardHasImage);
    case Enable::Clipboard: return m_clip.has_value() || m_clipboardHasImage;
    case Enable::SeveralLayers: return d && d->layers().layerCount() > 1;
    case Enable::Recording: return d && d->isRecording();
    case Enable::SeveralFrames: return d && d->animation().isAnimation();
    case Enable::Video: return d && d->animation().video.has_value();
    case Enable::Deletable: return d && (d->layers().layerCount() > 1 || d->maskTarget());  // the mask goes, not the layer
    }
    return false;
}

void MainWindow::noteClipboard() {
    const QMimeData* outside = QGuiApplication::clipboard()->mimeData();
    m_clipboardHasImage = outside && outside->hasImage();
}

void MainWindow::updateCommandStates() {
    // While a timelapse replays only what does not edit stays usable: the commands that always are, and Export Timelapse ….
    for (auto it = m_actions.cbegin(); it != m_actions.cend(); ++it) {
        const Enable enable = m_bindings.value(it.key()).enable;
        const bool replayAllows = !isReplaying() || enable == Enable::Always || it.key() == QLatin1String("file.exportTimelapse");
        it.value()->setEnabled(replayAllows && isEnabled(enable));
    }
    if (QAction* record = m_actions.value(QStringLiteral("file.recordTimelapse"))) {
        const Document* d = m_tabs->currentDocument();
        const QSignalBlocker blocker(record);
        record->setChecked(d && d->isRecording() && !d->isRecordingPaused());
    }
}

void MainWindow::watchDocument(Document* doc) {
    for (const QMetaObject::Connection& c : m_documentConnections)
        disconnect(c);
    m_documentConnections.clear();
    m_memoryNotice->watch(doc);
    if (doc) {
        const auto update = [this] { updateCommandStates(); };
        m_documentConnections = {
            connect(doc, &Document::selectionChanged, this, update),
            connect(doc, &Document::layersChanged, this, update),
            connect(doc, &Document::maskTargetChanged, this, update),
            connect(doc, &Document::recordingChanged, this, update),
            connect(doc, &Document::animationChanged, this, update),
            connect(doc, &Document::animationChanged, this, &MainWindow::updateFrameStatus),
            connect(doc, &Document::currentFrameChanged, this, &MainWindow::updateFrameStatus),
            connect(doc, &Document::canvasResized, this, [this](const QSize& size) { m_toolPanel->parameters()->setCanvasSize(size); }),
            connect(doc->undoStack(), &QUndoStack::canUndoChanged, this,
                    [this, update](bool can) {
                        if (std::exchange(m_canUndo, can) != can)
                            update();
                    }),
            connect(doc->undoStack(), &QUndoStack::canRedoChanged, this, [this, update](bool can) {
                if (std::exchange(m_canRedo, can) != can)
                    update();
            })};
        m_canUndo = doc->undoStack()->canUndo();
        m_canRedo = doc->undoStack()->canRedo();
    }
    updateCommandStates();
}

void MainWindow::registerFileCommands() {
    bind("file.new", [this] { m_files->newDocument(); }, Enable::Always);
    bind("file.open", [this] { m_files->open(); }, Enable::Always);
    bind("file.save", [this] { withLayer([this](Document* d, LayerId) { m_files->save(d); }); });
    bind("file.saveAs", [this] { withLayer([this](Document* d, LayerId) { m_files->saveAs(d); }); });
    for (const char* extension : kExportExtensions)
        if (const DocumentFormat* format = exportFormat(QString::fromLatin1(extension)))
            bind(QStringLiteral("file.export.") + QString::fromLatin1(extension),
                 [this, format] { withLayer([this, format](Document* d, LayerId) { m_files->exportAs(d, *format); }); });
    bind("file.canvasFromClipboard", [this] { canvasFromClipboard(); }, Enable::Clipboard);
    bind("file.close", [this] { m_tabs->requestCloseTab(m_tabs->currentIndex()); });
    bind("file.quit", [this] { close(); }, Enable::Always);
    bind("file.timelapseReplay", [this] { startReplay(m_tabs->currentDocument()); }, Enable::Recording);
    bind("file.exportTimelapse", [this] { m_timelapse->exportTimelapse(m_tabs->currentDocument()); }, Enable::Recording);
    // Checked: the canvas records. Checking it on a canvas without a recording starts one (an old file, or a canvas made while
    // "Record new canvases" was off); unchecking pauses.
    bindCheck(
        "file.recordTimelapse", false,
        [this](bool on) {
            Document* d = m_tabs->currentDocument();
            if (!d)
                return;
            if (on && !d->isRecording()) {
                m_timelapse->startRecording(d);
            } else {
                m_timelapse->setPaused(d, !on);
            }
        },
        Enable::Document);
    bind("file.restartRecording", [this] { m_timelapse->restartRecording(m_tabs->currentDocument()); }, Enable::Recording);
    bind("file.deleteRecording", [this] { m_timelapse->deleteRecording(m_tabs->currentDocument()); }, Enable::Recording);
}

void MainWindow::rebuildRecentMenu(QMenu* menu) {
    menu->clear();
    int shown = 0;
    for (const QString& path : m_files->recentFiles()) {
        if (!QFileInfo::exists(path))
            continue;
        QAction* a = menu->addAction(recentLabel(++shown, path));
        a->setToolTip(path);
        a->setStatusTip(path);
        connect(a, &QAction::triggered, this, [this, path] { m_files->openFile(path); });
    }
    menu->menuAction()->setEnabled(shown > 0);
}

void MainWindow::registerEditCommands() {
    // Undo would replace the layer tree under a running stroke, so it waits until the pen is lifted.
    bind("edit.undo", [this] { withLayer([this](Document* d, LayerId) { report(d->undo()); }); }, Enable::CanUndo);
    bind("edit.redo", [this] { withLayer([this](Document* d, LayerId) { report(d->redo()); }); }, Enable::CanRedo);
    bind("edit.cut", [this] { copyOrCut(true); }, Enable::Selection);
    bind("edit.copy", [this] { copyOrCut(false); }, Enable::Selection);
    bind("edit.copyBounds", [this] { copyOrCut(false, true); }, Enable::Selection);
    bind("edit.paste", [this] { paste(false); }, Enable::Paste);
    bind("edit.pasteKeep", [this] { paste(true); }, Enable::Paste);
}

void MainWindow::registerCanvasCommands() {
    bind("canvas.background.specify", [this] { specifyBackgroundColor(); });
    bind("canvas.flipH", [this] { withLayer([this](Document* d, LayerId) { report(d->reverseCanvas(true)); }); });
    bind("canvas.flipV", [this] { withLayer([this](Document* d, LayerId) { report(d->reverseCanvas(false)); }); });
    bind("canvas.trim", [this] { withLayer([this](Document* d, LayerId) { report(d->trimCanvas()); }); }, Enable::Selection);
    bind("canvas.rotateCCW", [this] { withLayer([this](Document* d, LayerId) { report(d->rotateCanvas(false)); }); }, Enable::Document);
    bind("canvas.rotateCW", [this] { withLayer([this](Document* d, LayerId) { report(d->rotateCanvas(true)); }); }, Enable::Document);
    for (const auto& entry : kBackgrounds)
        bind(entry.id, [this, mode = entry.mode] { setCanvasBackground(mode); });
}

void MainWindow::updateBackgroundMenu() {
    const Document* d = m_tabs->currentDocument();
    for (const auto& entry : kBackgrounds)
        if (QAction* a = command(QString::fromLatin1(entry.id)))
            a->setChecked(d && d->background().mode == entry.mode);
}

void MainWindow::setCanvasBackground(BackgroundMode mode) {
    withLayer([this, mode](Document* d, LayerId) {
        CanvasBackground b = d->background();
        b.mode = mode;
        report(d->setBackground(b));
    });
}

// "Background Color…": sets the colour of the Color
// modes; if the canvas is not in one of them it switches to Color so that the choice shows.
void MainWindow::specifyBackgroundColor() {
    withLayer([this](Document* d, LayerId) {
        const QColor picked = QColorDialog::getColor(d->background().color, this, tr("Canvas Background Color"));
        if (!picked.isValid())
            return;
        CanvasBackground b = d->background();
        b.color = QColor(picked.red(), picked.green(), picked.blue());
        if (b.mode != BackgroundMode::Color && b.mode != BackgroundMode::TransparencyColor)
            b.mode = BackgroundMode::Color;
        report(d->setBackground(b));
    });
}

void MainWindow::registerLayerCommands() {
    bind("layer.newNormal", [this] { withLayer([this](Document* d, LayerId) { report(d->addLayer()); }); });
    bind("layer.newFolder", [this] { withLayer([this](Document* d, LayerId) { report(d->addFolder()); }); });
    bind("layer.copy", [this] { withLayer([this](Document* d, LayerId id) { report(d->duplicateLayer(id)); }); });
    bind(
        "layer.delete",
        [this] {
            withLayer([this](Document* d, LayerId id) {
                if (const auto ids = m_layers->selectedLayers(); ids.size() > 1)
                    report(d->deleteLayers(ids));
                else
                    report(d->maskTarget() ? d->deleteLayerMask(id) : d->deleteLayer(id));
            });
        },
        Enable::Deletable);
    bind("layer.erase", [this] {
        // Frames chosen in the timeline since the last click elsewhere are what Delete clears, wherever the keyboard went.
        if (deleteInTimeline())
            return;
        withLayer([this](Document* d, LayerId id) {
            if (const auto ids = m_layers->selectedLayers(); ids.size() > 1)
                report(d->clearLayers(ids));
            else
                report(d->clearLayer(id));
        });
    });
    // Rasterize: a text layer becomes an ordinary layer with the same pixels.
    bind("layer.rasterize", [this] { withLayer([this](Document* d, LayerId id) { report(d->rasterizeLayer(id)); }); });
    bind(
        "layer.merge",
        [this] {
            withLayer([this](Document* d, LayerId id) {
                if (const auto ids = m_layers->selectedLayers(); ids.size() > 1)
                    report(d->mergeLayers(ids));
                else
                    report(d->mergeDown(id));
            });
        },
        Enable::SeveralLayers);
    bind(
        "layer.transferDown", [this] { withLayer([this](Document* d, LayerId id) { report(d->transferDown(id)); }); },
        Enable::SeveralLayers);
    bind("layer.mergeVisible", [this] { withLayer([this](Document* d, LayerId) { report(d->mergeVisible()); }); });
    bind("layer.flatten", [this] { withLayer([this](Document* d, LayerId) { report(d->flattenImage()); }); });
    bind("layer.fill", [this] {
        withCanvas([this](CanvasView* c) { report(c->document()->fillLayer(c->document()->currentLayerId(), m_tools->color())); });
    });
    bind("layer.transform", [this] {
        m_tools->setFixedTool(FixedTool::Marquee);
        withCanvas([](CanvasView* c) { c->beginTransform(); });
    });
    bind("layer.flipH", [this] { applyTurn(transform::Turn::FlipHorizontal); });
    bind("layer.flipV", [this] { applyTurn(transform::Turn::FlipVertical); });
    bind("layer.rotateCCW", [this] { applyTurn(transform::Turn::CounterClockwise); });
    bind("layer.rotateCW", [this] { applyTurn(transform::Turn::Clockwise); });
}

// Effects: each entry changes the current layer, or only the selection when there is one.
void MainWindow::registerEffectCommands() {
    m_effects = new EffectController(
        this,
        [this] {
            const CanvasView* c = m_tabs->currentCanvas();
            return c ? c->visibleDocumentArea().boundingRect().toAlignedRect() : QRect();
        },
        this);
    connect(m_effects, &EffectController::refused, this, [this](EditError e) { showRefusal(e); });
    connect(m_effects, &EffectController::message, this, &MainWindow::showError);
    for (const EffectSpec& spec : allEffects())
        bind(QString::fromLatin1(spec.id),
             [this, kind = spec.kind] { withLayer([this, kind](Document* d, LayerId) { m_effects->run(kind, d); }); });
}

void MainWindow::registerSelectionCommands() {
    bind("selection.all", [this] { withLayer([this](Document* d, LayerId) { report(d->selectAll()); }); });
    bind("selection.deselect", [this] { withLayer([this](Document* d, LayerId) { report(d->deselect()); }); }, Enable::Selection);
    bind(
        "selection.invert",
        [this] {
            withLayer([this](Document* d, LayerId) {
                if (!d->selection())
                    showError(tr("There is no selection to invert."));
                else
                    report(d->invertSelection());
            });
        },
        Enable::Selection);
    bind("selection.dilate", [this] { showGrowDialog(true); }, Enable::Selection);
    bind("selection.erode", [this] { showGrowDialog(false); }, Enable::Selection);
    bind("selection.dilate1", [this] { withLayer([this](Document* d, LayerId) { report(d->dilateSelection(1)); }); }, Enable::Selection);
    bind("selection.erode1", [this] { withLayer([this](Document* d, LayerId) { report(d->erodeSelection(1)); }); }, Enable::Selection);
    bindCheck("selection.show", true, [this](bool shown) {
        for (int i = 0; i < m_tabs->count(); ++i)
            if (CanvasView* c = m_tabs->canvasAt(i))
                c->setSelectionVisible(shown);
    });
}

void MainWindow::registerViewCommands() {
    // Arrow-key scrolling lives in CanvasView, so lists and spin boxes keep their arrows. Zoom In/Out go about the view
    // centre; the keys of the same names "at Cursor Position" (registerKeyCommands) about the pointer.
    bind("view.close", [this] { m_tabs->requestCloseTab(m_tabs->currentIndex()); });
    bind("view.closeAll", [this] { m_tabs->requestCloseAll(); });
    bind("view.zoomIn", [this] { withCanvas([](CanvasView* c) { c->zoomStep(1, c->viewCentre()); }); });
    bind("view.zoomOut", [this] { withCanvas([](CanvasView* c) { c->zoomStep(-1, c->viewCentre()); }); });
    bind("view.rotateCCW", [this] { withCanvas([](CanvasView* c) { c->rotateBy(-CanvasView::kRotateStepDeg); }); });
    bind("view.rotateCW", [this] { withCanvas([](CanvasView* c) { c->rotateBy(CanvasView::kRotateStepDeg); }); });
    bind("view.scale100", [this] { withCanvas([](CanvasView* c) { c->actualPixels(); }); });
    bind("view.fit", [this] { withCanvas([](CanvasView* c) { c->fitToWindow(); }); });
    bind("view.resetPosition", [this] { withCanvas([this](CanvasView* c) { c->resetPosition(m_options->values().fitOnResetPosition); }); });
    bind("view.resetAngle", [this] { withCanvas([](CanvasView* c) { c->resetAngle(); }); });
    bind("view.flip", [this] { withCanvas([](CanvasView* c) { c->toggleFlip(); }); });
}

void MainWindow::rebuildViewList(QMenu* viewMenu) {
    qDeleteAll(m_viewListActions);
    m_viewListActions.clear();
    if (m_tabs->count() > 0) {
        QAction* separator = viewMenu->addSeparator();  // deleted with the entries below: it belongs to the list
        m_viewListActions.push_back(separator);
    }
    for (int i = 0; i < m_tabs->count(); ++i) {
        const CanvasView* c = m_tabs->canvasAt(i);
        const QString name = QString(c->document()->displayName()).replace(QLatin1Char('&'), QStringLiteral("&&"));
        auto* a = new QAction(QStringLiteral("%1 %2 (%3)").arg(i + 1).arg(name, view_text::zoomText(c->zoom())), viewMenu);
        a->setCheckable(true);
        a->setChecked(i == m_tabs->currentIndex());
        connect(a, &QAction::triggered, this, [this, i] { m_tabs->setCurrentIndex(i); });
        viewMenu->addAction(a);
        m_viewListActions.push_back(a);
    }
}

void MainWindow::registerWindowCommands() {
    for (const Panel& p : m_panels)
        bindCheck(QStringLiteral("window.show.") + p.id, p.shownAtStart, [this](bool) { applyPanelVisibility(); });
    const std::array<std::pair<const char*, color::SliderMode>, 2> modes = {
        {{"window.hsv.hsv", color::SliderMode::Hsv}, {"window.hsv.hsl", color::SliderMode::Hsl}}};
    for (const auto& [id, mode] : modes)
        bind(id, [this, mode] { m_colorAssets->setSliderMode(mode); }, Enable::Always);
    const std::array<std::pair<const char*, SwatchSize>, 3> sizes = {{{"window.swatches.small", SwatchSize::Small},
                                                                      {"window.swatches.middle", SwatchSize::Middle},
                                                                      {"window.swatches.large", SwatchSize::Large}}};
    for (const auto& [id, size] : sizes)
        bind(id, [this, size] { m_colorAssets->setSwatchSize(size); }, Enable::Always);
    const std::array<std::pair<const char*, QDockWidget*>, 4> separate = {{{"window.separate.toolPanel", m_toolsDock},
                                                                           {"window.separate.colorPanel", m_colorDock},
                                                                           {"window.separate.layerPanel", m_layersDock},
                                                                           {"window.separate.navigatorPanel", m_navigatorDock}}};
    for (const auto& [id, dock] : separate) {
        bindCheck(id, false, [dock](bool on) { dock->setFloating(on); });
        connect(dock, &QDockWidget::topLevelChanged, this, [this, id](bool floating) {
            if (QAction* a = command(QString::fromLatin1(id)))
                a->setChecked(floating);
        });
    }
    bindCheck("window.colorToolRight", false, [this](bool) { arrangeDocks(); });
    bindCheck("window.navigatorLayerRight", false, [this](bool) { arrangeDocks(); });
    bindCheck("window.fullScreen", false, [this](bool on) { on ? showFullScreen() : showNormal(); });
    bindCheck("window.showAll", true, [this](bool) { applyPanelVisibility(); });
}

// The colour dock is never taller than its views (the card's largest height is the panel's plus its chrome) and takes
// their height when they change; a set of views seen before gets back the sizes it had (m_colorLayouts). arrangeDocks
// gives it its first height. The dock is wide enough for the circle (it only grows: the column stays as wide as the
// user made it).
void MainWindow::fitColorDock() {
    if (!m_colorDock || !m_colorDock->isVisible())
        return;
    // The card's new largest height, up to the window's layout (a queued layout pass takes a few turns of the event
    // loop).
    const auto relayout = [this] {
        for (QLayout* l : {m_colorDock->widget()->layout(), m_colorDock->layout(), layout()})
            l->activate();
    };
    if (m_colorDock->isFloating()) {
        relayout();
        m_colorDock->resize(m_colorDock->width(), m_colorDock->sizeHint().height());
        return;
    }
    const QSize chrome = static_cast<PanelCard*>(m_colorDock->widget())->chrome();
    const int views = colorViewsShown();
    const std::vector<QSize> before = m_colorLayouts.value(views);
    m_colorViews = views;
    // Again once the layout has taken the panel's new limits (until then a shrinking panel's old least height holds).
    const auto apply = [this, before, chrome, relayout] {
        relayout();
        if (!before.empty())
            setDockSizes(before);
        else
            setColorDockHeight(m_colorPanel->preferredHeight() + chrome.height());
    };
    apply();
    QTimer::singleShot(0, this, apply);
    if (m_colorPanel->width() < m_colorPanel->preferredWidth())
        resizeDocks({m_colorDock}, {m_colorPanel->preferredWidth() + chrome.width()}, Qt::Horizontal);
}

// Given one dock of a column, Qt's resizeDocks moves it only part of the way there (about half), so a dock that grows by a
// view fell a few pixels short and showed a scroll bar. Given both docks of the column it sets them: the tool panel below
// takes the rest, down to its least height.
void MainWindow::setColorDockHeight(int height) {
    const bool toolsBelow = m_toolsDock->isVisible() && !m_toolsDock->isFloating() &&
                            dockWidgetArea(m_toolsDock) == dockWidgetArea(m_colorDock) && m_toolsDock->x() == m_colorDock->x();
    if (!toolsBelow) {
        resizeDocks({m_colorDock}, {height}, Qt::Vertical);
        return;
    }
    const int column = m_colorDock->height() + m_toolsDock->height();
    resizeDocks({m_colorDock, m_toolsDock}, {height, std::max(0, column - height)}, Qt::Vertical);
}

// The colour views shown (bit i: ColorView i), and which panels are shown and docked (from bit 8): a saved layout is
// only given back to the same panels, so it never shows or docks a panel the user changed since.
int MainWindow::colorViewsShown() const {
    int views = 0;
    for (int i = 0; i < kColorViewCount; ++i)
        if (m_colorPanel->view(ColorView(i))->isVisibleTo(m_colorPanel))
            views |= 1 << i;
    int bit = 8;
    for (const QDockWidget* d : {m_navigatorDock, m_colorDock, m_toolsDock, m_layersDock}) {
        views |= (d->isHidden() ? 1 : 0) << bit++;
        views |= (d->isFloating() ? 1 : 0) << bit++;
    }
    return views;
}

void MainWindow::bindColorPanel() {
    connect(m_colorPanel, &ColorPanel::sizeWanted, this, &MainWindow::fitColorDock, Qt::QueuedConnection);
    connect(m_colorDock, &QDockWidget::topLevelChanged, this, &MainWindow::fitColorDock, Qt::QueuedConnection);
    // Only when the set of views changes: a second fit of the same set (a view that settles a moment later) would keep its
    // half-fitted sizes and give them back.
    connect(m_colorPanel, &ColorPanel::heightChanging, this, [this] {
        if (!m_colorDock || m_colorDock->isFloating() || !m_colorDock->isVisible())
            return;
        if (m_colorViews >= 0 && m_colorViews != colorViewsShown() && !m_colorLayouts.contains(m_colorViews))
            m_colorLayouts.insert(m_colorViews, dockSizes());
    });
    for (int i = 0; i < kColorViewCount; ++i)
        if (QAction* a = command(QStringLiteral("window.show.") + ColorPanel::viewId(ColorView(i))))
            m_colorPanel->bindToggle(ColorView(i), a);
    const auto syncRadios = [this] {
        const char* mode = m_colorAssets->sliderMode() == color::SliderMode::Hsl ? "window.hsv.hsl" : "window.hsv.hsv";
        const char* size = m_colorAssets->swatchSize() == SwatchSize::Small    ? "window.swatches.small"
                           : m_colorAssets->swatchSize() == SwatchSize::Middle ? "window.swatches.middle"
                                                                               : "window.swatches.large";
        for (const char* id : {mode, size})
            if (QAction* a = command(QString::fromLatin1(id)))
                a->setChecked(true);
    };
    connect(m_colorAssets, &ColorAssets::optionsChanged, this, syncRadios);
    syncRadios();
}

void MainWindow::applyPanelVisibility() {
    const QAction* all = command(QStringLiteral("window.showAll"));
    const bool allShown = !all || all->isChecked();
    for (const Panel& p : m_panels) {
        const QAction* shown = command(QStringLiteral("window.show.") + p.id);
        p.widget->setVisible(allShown && (!shown || shown->isChecked()));
    }
    QTimer::singleShot(0, this, [this] {
        if (m_colorPanel)
            m_colorPanel->fitHeight();
    });
}

void MainWindow::registerOtherCommands() {
    bind("other.options", [this] { showOptions(); }, Enable::Always);
    bind("other.tabletDiagnostics", [this] { showTabletDiagnostics(); }, Enable::Always);
    bind("other.aboutDoodlebold", [this] { showAbout(); }, Enable::Always);
    bind("other.reportBug", [] { links::open(links::bugReportUrl(links::systemFacts())); }, Enable::Always);
    bind("other.crashReport", [this] { showCrashReport(crash_report::lastReport(), false); }, Enable::Always);
}

void MainWindow::registerKeyCommands() {
    const auto toolKey = [this](const char* id, ToolType type) {
        bind(
            id,
            [this, type] {
                if (!m_tools->selectToolOfType(type))
                    showError(tr("There is no %1 tool in the tool tray.").arg(toolTypeName(type)));
            },
            Enable::Always);
    };
    toolKey("tool.pencil", ToolType::Pencil);
    toolKey("tool.brush", ToolType::Brush);
    // C is shared: it cycles through the Water Color and Pixel Art Pen tools.
    bind(
        "tool.waterColor",
        [this] {
            if (!m_tools->selectToolOfTypes({ToolType::WaterColor, ToolType::PixelArtPen}))
                showError(tr("There is no %1 tool in the tool tray.").arg(toolTypeName(ToolType::WaterColor)));
        },
        Enable::Always);
    toolKey("tool.eraser", ToolType::Eraser);
    // A tray without Selection Pen / Eraser (a tool set saved before they existed) gets one.
    bind("tool.selectionPen", [this] { m_tools->selectOrCreateToolOfType(ToolType::SelectionPen); }, Enable::Always);
    bind("tool.selectionEraser", [this] { m_tools->selectOrCreateToolOfType(ToolType::SelectionEraser); }, Enable::Always);
    bind("tool.cloneStamp", [this] { m_tools->selectOrCreateToolOfType(ToolType::Clone); }, Enable::Always);
    for (const auto& [id, type] :
         {std::pair{"tool.marker", ToolType::Marker}, std::pair{"tool.pixelArtPen", ToolType::PixelArtPen},
          std::pair{"tool.blur", ToolType::Blur}, std::pair{"tool.smudge", ToolType::Smudge}, std::pair{"tool.bucket", ToolType::Bucket}})
        bind(id, [this, type] { m_tools->selectOrCreateToolOfType(type); }, Enable::Always);
    for (const auto& [id, fixed] : {std::pair{"tool.text", FixedTool::Text}, std::pair{"tool.move", FixedTool::Move},
                                    std::pair{"tool.zoom", FixedTool::Zoom}, std::pair{"tool.rotate", FixedTool::Rotate},
                                    std::pair{"tool.hand", FixedTool::Hand}, std::pair{"tool.eyedropper", FixedTool::Eyedropper}})
        bind(id, [this, fixed] { m_tools->setFixedTool(fixed); }, Enable::Always);
    bind("tool.marquee", [this] { m_tools->setFixedTool(FixedTool::Marquee); }, Enable::Always);
    bind("tool.lasso", [this] { m_tools->setFixedTool(FixedTool::Lasso); }, Enable::Always);
    bind("tool.wand", [this] { m_tools->setFixedTool(FixedTool::MagicWand); }, Enable::Always);
    bind("color.exchange", [this] { m_tools->exchangeColors(); }, Enable::Always);
    bind("color.transparent", [this] { m_tools->setTransparent(!m_tools->transparent()); }, Enable::Always);
    bind("brush.sizePrevious", [this] { m_tools->stepSize(-1); }, Enable::Always);
    bind("brush.sizeNext", [this] { m_tools->stepSize(+1); }, Enable::Always);
    for (int percent = 10; percent <= 100; percent += 10)
        bind(
            QStringLiteral("brush.density%1").arg(percent),
            [this, percent] {
                BrushSettings s = m_tools->settings();
                s.density = percent;
                m_tools->setSettings(s);
            },
            Enable::Always);

    bind("view.zoomInAtCursor", [this] { withCanvas([](CanvasView* c) { c->zoomStep(1, c->zoomAnchor()); }); });
    bind("view.zoomOutAtCursor", [this] { withCanvas([](CanvasView* c) { c->zoomStep(-1, c->zoomAnchor()); }); });
    bind("view.next", [this] { m_tabs->selectNextTab(); });
    bind("view.previous", [this] { m_tabs->selectPreviousTab(); });

    bind("layer.forward", [this] { withLayer([this](Document* d, LayerId id) { report(d->raiseLayer(id)); }); });
    bind("layer.backward", [this] { withLayer([this](Document* d, LayerId id) { report(d->lowerLayer(id)); }); });
    bind("layer.toFront", [this] { withLayer([this](Document* d, LayerId id) { report(d->layerToFront(id)); }); });
    bind("layer.toBack", [this] { withLayer([this](Document* d, LayerId id) { report(d->layerToBack(id)); }); });
    bind("layer.selectForward", [this] { withLayer([](Document* d, LayerId id) { d->setCurrentLayer(d->layers().above(id)); }); });
    bind("layer.selectBackward", [this] { withLayer([](Document* d, LayerId id) { d->setCurrentLayer(d->layers().below(id)); }); });
    bind("layer.selectMask", [this] { withLayer([](Document* d, LayerId) { d->setMaskTarget(!d->maskTarget()); }); });
}

}
