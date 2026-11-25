#include "ui/MainWindow.h"

#include "core/brush/BrushResources.h"
#include "core/document/Document.h"
#include "core/input/TabletStats.h"
#include "core/io/FormatRegistry.h"
#include "core/options/AppOptions.h"
#include "core/tools/ToolBox.h"
#include "core/tools/ToolBoxStore.h"
#include "ui/AboutDialog.h"
#include "ui/CrashReportDialog.h"
#include "ui/CanvasView.h"
#include "ui/ColumnDivider.h"
#include "ui/DocumentTabs.h"
#include "ui/animation/AnimationController.h"
#include "ui/Toast.h"
#include "ui/UsageMeters.h"
#include "ui/ViewSelector.h"
#include "ui/FileController.h"
#include "ui/GlassPopups.h"
#include "ui/KeysPage.h"
#include "core/paint/StrokeMemory.h"
#include "ui/MemoryNotice.h"
#include "ui/OptionsDialog.h"
#include "ui/QuickBar.h"
#include "ui/PanelCard.h"
#include "ui/RecoveryController.h"
#include "ui/RecoveryDialog.h"
#include "ui/SelectionGrowDialog.h"
#include "ui/TabletDiagnosticsDialog.h"
#include "ui/TabletInput.h"
#include "ui/Theme.h"
#include "ui/UpdateNotice.h"
#include "core/color/ColorAssets.h"
#include "core/color/ColorAssetsStore.h"
#include "core/color/ColorState.h"
#include "ui/panels/BrushPreviewStrip.h"
#include "ui/panels/ColorPanel.h"
#include "ui/panels/FixedToolPanels.h"
#include "ui/panels/LayerPanel.h"
#include "ui/panels/NavigatorPanel.h"
#include "ui/panels/ToolPanel.h"
#include "ui/panels/ToolParameters.h"
#include "ui/timelapse/TimelapseController.h"
#include "ui/timelapse/TimelapseReplayView.h"

#include <QApplication>
#include <QClipboard>
#include <QCloseEvent>
#include <QDebug>
#include <QDockWidget>
#include <QGraphicsOpacityEffect>
#include <QFileInfo>
#include <QOpenGLWidget>
#include <QMenuBar>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QGuiApplication>
#include <QLabel>
#include <QMimeData>
#include <QPointer>
#include <QSignalBlocker>
#include <QStatusBar>
#include <QStyle>
#include <QTimer>
#include <QTabBar>
#include <QTabletEvent>
#include <QToolBar>
#include <QUrl>

#include <algorithm>
#include <array>

namespace doodlebold {

namespace {

constexpr int kMenuItemPadding = Theme::kSpace2;  // a menu-bar item's side padding, as the style sheet has it

// The panels while a timelapse replays: dimmed to say they wait, as a disabled control's icon is (component metric).
constexpr double kReplayDimOpacity = 0.45;

// Our own copy on the system clipboard, beside the picture other programs read (its value is MainWindow::m_clipTag).
constexpr const char* kOwnCopyMime = "application/x-doodlebold-copy";
}

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent), m_stats(new TabletStats(this)), m_tools(new ToolBox(this)), m_tabs(new DocumentTabs(m_stats, this)) {
    setWindowTitle(QStringLiteral("Doodlebold"));
    setWindowIcon(QIcon(QStringLiteral(":/icons/doodlebold.png")));
    // A window that gets its first QOpenGLWidget while shown is destroyed and made again (on Windows the user sees it close
    // and reopen): one from the start makes the window OpenGL before it is first shown.
    (new QOpenGLWidget(this))->hide();
    auto* central = new QWidget(this);
    auto* centralLayout = new QVBoxLayout(central);
    centralLayout->setContentsMargins(0, 0, 0, 0);
    centralLayout->setSpacing(0);
    centralLayout->addWidget(m_tabs, 1);
    setCentralWidget(central);
    m_toasts = new ToastStack(this, m_tabs);
    setAcceptDrops(true);
    m_files = new FileController(m_tabs, FormatRegistry::instance(), this, this);
    connect(m_files, &FileController::documentCreated, this, &MainWindow::addDocument);
    connect(m_files, &FileController::message, this, [this](const QString& text) { statusBar()->showMessage(text, 4000); });
    m_timelapse = new TimelapseController(this, this);
    connect(m_timelapse, &TimelapseController::message, this, [this](const QString& text) { statusBar()->showMessage(text, 4000); });
    connect(m_timelapse, &TimelapseController::refused, this, [this](const QString& why) { showError(why); });
    connect(m_tabs, &DocumentTabs::recordingCommand, this, [this](Document* doc, RecordingPill::Command command) {
        switch (command) {
        case RecordingPill::Command::Start: m_timelapse->startRecording(doc); break;
        case RecordingPill::Command::Pause: m_timelapse->setPaused(doc, true); break;
        case RecordingPill::Command::Resume: m_timelapse->setPaused(doc, false); break;
        case RecordingPill::Command::Replay: startReplay(doc); break;
        case RecordingPill::Command::Export: m_timelapse->exportTimelapse(doc); break;
        case RecordingPill::Command::Restart: m_timelapse->restartRecording(doc); break;
        case RecordingPill::Command::Delete: m_timelapse->deleteRecording(doc); break;
        }
    });
    m_tabs->setSaveHandler([this](Document* doc) { return m_files->save(doc); });
    m_toolStore = new ToolBoxStore(m_tools, ToolBoxStore::defaultPath(), this);
    if (const auto loaded = m_toolStore->load();
        !loaded && loaded.error() != ToolBoxFileError::NoFile && loaded.error() != ToolBoxFileError::OtherVersion)
        qWarning().noquote() << describe(loaded.error()) << m_toolStore->path();
    m_options = new AppOptions(this);
    connect(m_options, &AppOptions::changed, this, &MainWindow::applyOptions);
    m_recovery = new RecoveryController(this);
    connect(m_recovery, &RecoveryController::documentRestored, this, &MainWindow::addDocument);
    buildMemoryNotice();
    m_updateNotice = new UpdateNotice(this);
    buildPanels();
    buildTimeline();
    m_tabs->setOuterGap(outerGap());
    m_quickBar = new QuickBar(m_tools, this);
    addToolBar(m_quickBar);
    m_panels.push_back({QStringLiteral("quickBar"), m_quickBar});
    m_panels.push_back({QStringLiteral("statusBar"), statusBar()});
    m_panels.push_back({QStringLiteral("viewSelector"), m_tabs->viewSelector()});
    buildMenus();
    bindKeys();
    bindColorPanel();
    populateQuickBar();
    placeQuickBar();

    m_usage = new UsageMeters(this);
    statusBar()->addPermanentWidget(m_usage);
    connect(m_tabs, &DocumentTabs::currentDocumentChanged, this, &MainWindow::onCurrentDocumentChanged);
    connect(m_tabs, &DocumentTabs::documentTitleChanged, this, &MainWindow::updateTitle);

    qApp->installEventFilter(this);  // TabletEnterProximity / LeaveProximity are delivered to the application
    resize(1400, 900);
    restoreLayout();
    applyPanelVisibility();
    applyOptions();
    onCurrentDocumentChanged();
}

MainWindow::~MainWindow() {
    // The documents are deleted with the tabs, after this class's members: their signals must not reach it by then.
    for (const QMetaObject::Connection& c : m_documentConnections)
        disconnect(c);
}

void MainWindow::buildPanels() {
    m_navigator = new NavigatorPanel(this);
    m_brushResources = new BrushResourceLibrary(BrushResourceLibrary::defaultFolder(), this);
    RebuiltStroke::setResources(m_brushResources);
    // A picture renamed in Options > Resources: the tools that use it follow the new name.
    connect(m_brushResources, &BrushResourceLibrary::renamed, this, [this](ResourceKind kind, const QString& from, const QString& to) {
        if (kind != ResourceKind::PaperTexture)
            m_tools->renameResource(isForm(kind), from, to);
    });
    m_layers = new LayerPanel(this);
    m_toolPanel = new ToolPanel(m_tools, m_brushResources, this);
    const auto dock = [this](QWidget* panel, const QString& title, const QString& name, const QString& id) {
        auto* d = new QDockWidget(title, this);
        d->setObjectName(name);
        d->setWidget(new PanelCard(title, panel));
        const auto titleBar = [d](bool floating) { d->setTitleBarWidget(floating ? nullptr : new QWidget(d)); };
        titleBar(false);
        connect(d, &QDockWidget::topLevelChanged, d, titleBar);
        d->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
        addDockWidget(Qt::LeftDockWidgetArea, d);
        m_panels.push_back({id, d});
        return d;
    };
    m_navigatorDock = dock(m_navigator, tr("Navigator"), QStringLiteral("navigatorDock"), QStringLiteral("navigatorPanel"));
    m_colorAssets = new ColorAssets(this);
    m_colorStore = new ColorAssetsStore(m_colorAssets, ColorAssetsStore::defaultPath(), this);
    if (!m_colorStore->load())
        qWarning().noquote() << "Color panel data unreadable:" << m_colorStore->path();
    m_colorPanel = new ColorPanel(m_tools, m_colorAssets, this);
    m_toolPanel->setColorState(m_colorPanel->state());
    m_colorDock = dock(m_colorPanel, tr("Color"), QStringLiteral("colorDock"), QStringLiteral("colorPanel"));
    m_toolsDock = dock(m_toolPanel, tr("Tools"), QStringLiteral("toolsDock"), QStringLiteral("toolPanel"));
    m_layersDock = dock(m_layers, tr("Layers"), QStringLiteral("layersDock"), QStringLiteral("layerPanel"));
    m_panels.push_back({QStringLiteral("brushPreview"), m_toolPanel->preview()});
    for (int i = 0; i < kColorViewCount; ++i) {
        const auto v = ColorView(i);
        m_panels.push_back({ColorPanel::viewId(v), m_colorPanel->view(v), ColorPanel::shownAtStart(v)});
    }
    m_columnDivider = new ColumnDivider(this);
    connect(m_columnDivider, &ColumnDivider::dragStarted, this, &MainWindow::startColumnDrag);
    connect(m_columnDivider, &ColumnDivider::dragged, this, &MainWindow::dragColumnDivider);
    connect(m_columnDivider, &ColumnDivider::dragFinished, this, [this] { m_colorLayouts.clear(); });
    arrangeDocks();
    const auto showRefusal = [this](const QString& why) { showError(why); };
    connect(m_layers, &LayerPanel::editRefused, this, showRefusal);
    connect(m_toolPanel, &ToolPanel::refused, this, showRefusal);

    FixedToolPanels* fixed = m_toolPanel->fixedPanels();
    connect(fixed, &FixedToolPanels::transformRequested, this,
            [this](TransformMode mode) { withCanvas([mode](CanvasView* c) { c->beginTransform(mode); }); });
    connect(fixed, &FixedToolPanels::transformApplied, this, [this] { withCanvas([](CanvasView* c) { c->commitTransform(); }); });
    connect(fixed, &FixedToolPanels::transformCancelled, this, [this] { withCanvas([](CanvasView* c) { c->cancelTransform(); }); });
    connect(fixed, &FixedToolPanels::reverseRequested, this,
            [this](bool horizontal) { applyTurn(horizontal ? transform::Turn::FlipHorizontal : transform::Turn::FlipVertical); });
    connect(fixed, &FixedToolPanels::rotate90Requested, this,
            [this](bool clockwise) { applyTurn(clockwise ? transform::Turn::Clockwise : transform::Turn::CounterClockwise); });
}

void MainWindow::arrangeDocks() {
    const auto checked = [this](const char* id) {
        const QAction* a = command(QString::fromLatin1(id));
        return a && a->isChecked();
    };
    const auto side = [](bool right) { return right ? Qt::RightDockWidgetArea : Qt::LeftDockWidgetArea; };
    const bool navigatorRight = checked("window.navigatorLayerRight");
    const bool colorRight = checked("window.colorToolRight");

    const std::vector<QDockWidget*> docks{m_navigatorDock, m_colorDock, m_toolsDock, m_layersDock};
    std::vector<bool> wasShown;
    wasShown.reserve(docks.size());
    for (QDockWidget* d : docks)
        wasShown.push_back(!d->isHidden());
    // Moving a column keeps the sizes the user gave the panels; the first arrangement is the default.
    const std::vector<QSize> sizes = m_docksArranged ? dockSizes() : std::vector<QSize>{};
    for (QDockWidget* d : docks)
        removeDockWidget(d);
    // The two columns first, then each column split top / bottom; splitting the navigator for the layers first would
    // leave the layer panel spanning both columns.
    addDockWidget(side(navigatorRight), m_navigatorDock);
    if (navigatorRight == colorRight) {
        splitDockWidget(m_navigatorDock, m_colorDock, Qt::Horizontal);
    } else {
        addDockWidget(side(colorRight), m_colorDock);
    }
    splitDockWidget(m_navigatorDock, m_layersDock, Qt::Vertical);
    splitDockWidget(m_colorDock, m_toolsDock, Qt::Vertical);
    for (std::size_t i = 0; i < docks.size(); ++i)
        docks[i]->setVisible(wasShown[i]);
    // Doodlebold's 9 pt font needs room: the navigator column's zoom row is 236 px
    // at the least (a smaller request is made up from the other column), the tool column fits the tool names and value
    // boxes. Again once the window has its size: before that, Qt shares out the columns' widths afresh. The cards add
    // their gap and inset (16 px) to that.
    const auto restore = [this, sizes] {
        if (sizes.empty()) {
            resizeDocks({m_navigatorDock, m_colorDock}, {252, 260}, Qt::Horizontal);
            const QSize chrome = static_cast<PanelCard*>(m_colorDock->widget())->chrome();
            setColorDockHeight(m_colorPanel->preferredHeight() + chrome.height());
            return;
        }
        setDockSizes(sizes);
    };
    restore();
    QTimer::singleShot(0, this, [this, restore, arrangement = ++m_arrangement] {
        if (arrangement == m_arrangement)
            restore();
    });
    m_docksArranged = true;
    m_colorLayouts.clear();
}

std::vector<QSize> MainWindow::dockSizes() const {
    return {m_navigatorDock->size(), m_colorDock->size(), m_toolsDock->size(), m_layersDock->size()};
}

void MainWindow::setDockSizes(const std::vector<QSize>& sizes) {
    if (sizes.size() != 4)
        return;
    const QList<QDockWidget*> docks{m_navigatorDock, m_colorDock, m_toolsDock, m_layersDock};
    QList<int> heights;
    for (const QSize& s : sizes)
        heights << s.height();
    resizeDocks({m_navigatorDock, m_colorDock}, {sizes[0].width(), sizes[1].width()}, Qt::Horizontal);
    resizeDocks(docks, heights, Qt::Vertical);
}

std::pair<QDockWidget*, QDockWidget*> MainWindow::sideBySideColumns() const {
    const auto docked = [](QDockWidget* d) { return d->isVisible() && !d->isFloating(); };
    QDockWidget* a = docked(m_navigatorDock) ? m_navigatorDock : docked(m_layersDock) ? m_layersDock : nullptr;
    QDockWidget* b = docked(m_colorDock) ? m_colorDock : docked(m_toolsDock) ? m_toolsDock : nullptr;
    if (!a || !b || dockWidgetArea(a) != dockWidgetArea(b))
        return {};
    return a->x() < b->x() ? std::pair{a, b} : std::pair{b, a};
}

void MainWindow::placeColumnDivider() {
    const auto [left, right] = sideBySideColumns();
    m_columnDivider->setVisible(left != nullptr);
    if (!left)
        return;
    const int gap = Theme::metric(Theme::Metric::CardGap);
    const QRect area = centralWidget()->geometry();
    const int from = left->x() + left->width() - gap;
    m_columnDivider->setGeometry(from, area.top(), right->x() + gap - from, area.height());
    m_columnDivider->raise();
}

void MainWindow::startColumnDrag() {
    const auto [left, right] = sideBySideColumns();
    if (!left) {
        m_columnDrag = {};
        return;
    }
    const bool leftAtEdge = dockWidgetArea(left) == Qt::LeftDockWidgetArea;
    QDockWidget* edge = leftAtEdge ? left : right;
    QDockWidget* inner = leftAtEdge ? right : left;
    int least = 0;
    for (QDockWidget* d : {m_navigatorDock, m_layersDock, m_colorDock, m_toolsDock})
        if (d->x() == edge->x() && d->isVisible() && !d->isFloating())
            least = std::max(least, d->minimumSizeHint().width());
    const int room = std::max(0, centralWidget()->width() - centralWidget()->minimumSizeHint().width());
    m_columnDrag = {edge, inner, edge->width(), inner->width(), least, std::max(least, edge->width() + room), leftAtEdge ? 1 : -1};
}

void MainWindow::dragColumnDivider(int dx) {
    const ColumnDrag& drag = m_columnDrag;
    if (!drag.edge)
        return;
    resizeDocks({drag.edge, drag.inner}, {std::clamp(drag.edgeWidth + drag.direction * dx, drag.least, drag.most), drag.innerWidth},
                Qt::Horizontal);
}

void MainWindow::applyTurn(transform::Turn turn) {
    withCanvas([this, turn](CanvasView* c) {
        c->commitTransform();
        report(c->document()->moveLayerExactly(c->document()->currentLayerId(), turn));
    });
}

void MainWindow::showRefusal(EditError error) {
    showError(describe(error));
}

void MainWindow::showError(const QString& text) {
    m_toasts->showMessage(text);
}

void MainWindow::withCanvas(const std::function<void(CanvasView*)>& f) {
    if (CanvasView* c = m_tabs->currentCanvas())
        f(c);
}

void MainWindow::withLayer(const std::function<void(Document*, LayerId)>& f) {
    if (Document* d = m_tabs->currentDocument())
        f(d, d->currentLayerId());
}

void MainWindow::resizeEvent(QResizeEvent* e) {
    QMainWindow::resizeEvent(e);
    m_colorLayouts.clear();
    // After the resize: moving a tool bar in or out of the toolbar area while Qt lays the window out for a Wayland
    // configure can leave its layout with a dangling item.
    QMetaObject::invokeMethod(this, &MainWindow::placeQuickBar, Qt::QueuedConnection);
}

void MainWindow::placeQuickBar() {
    if (!m_quickBar)
        return;
    const QFontMetrics metrics(menuBar()->font());
    int menus = 0;
    for (const QAction* a : menuBar()->actions())
        menus += metrics.horizontalAdvance(QString(a->text()).remove(QLatin1Char('&'))) + 2 * kMenuItemPadding;
    const bool fits = menus + m_quickBar->sizeHint().width() + 4 * kMenuItemPadding <= width();
    const bool beside = menuBar()->cornerWidget(Qt::TopRightCorner) == m_quickBar;
    if (fits == beside)
        return;
    const bool shown = !m_quickBar->isHidden();
    if (fits) {
        removeToolBar(m_quickBar);
        menuBar()->setCornerWidget(m_quickBar, Qt::TopRightCorner);
    } else {
        menuBar()->setCornerWidget(nullptr, Qt::TopRightCorner);
        addToolBar(m_quickBar);
    }
    m_quickBar->setVisible(shown);
}

void MainWindow::populateQuickBar() {
    const auto action = [this](const char* id) { return command(QString::fromLatin1(id)); };
    QuickBar::Commands c;
    c.undo = action("edit.undo");
    c.redo = action("edit.redo");
    c.deselect = action("selection.deselect");
    c.invertSelection = action("selection.invert");
    c.showSelection = action("selection.show");
    c.zoomOut = action("view.zoomOut");
    c.zoomIn = action("view.zoomIn");
    c.resetView = action("view.resetPosition");
    c.rotateCcw = action("view.rotateCCW");
    c.rotateCw = action("view.rotateCW");
    c.resetAngle = action("view.resetAngle");
    c.flip = action("view.flip");
    m_quickBar->populate(c);
}

void MainWindow::copyOrCut(bool cut, bool bounds) {
    withCanvas([this, cut, bounds](CanvasView* c) {
        c->commitTransform();
        Document* d = c->document();
        const LayerId id = d->currentLayerId();
        const auto copied = cut ? d->cut(id) : bounds ? d->copyBounds(id) : d->copy(id);
        if (!copied) {
            showError(copied.error() == EditError::NoChange ? tr("There is nothing to copy.") : describe(copied.error()));
            return;
        }
        m_clip = copied.value();
        // Other programs get the picture over white without alpha (a program that ignores alpha would show the
        // transparent parts black). Our own paste finds the tag and takes the copy with its transparency and position.
        static int serial = 0;
        m_clipTag = QByteArray::number(QCoreApplication::applicationPid()) + ':' + QByteArray::number(++serial);
        auto* mime = new QMimeData;
        mime->setImageData(m_clip->onWhite);
        mime->setData(QString::fromLatin1(kOwnCopyMime), m_clipTag);
        QGuiApplication::clipboard()->setMimeData(mime);
    });
}

void MainWindow::canvasFromClipboard() {
    const QMimeData* mime = QGuiApplication::clipboard()->mimeData();
    QImage picture = mime && mime->hasImage() ? qvariant_cast<QImage>(mime->imageData()) : QImage();
    if (m_clip && (picture.isNull() || (mime && mime->data(QString::fromLatin1(kOwnCopyMime)) == m_clipTag)))
        picture = m_clip->image;
    if (picture.isNull()) {
        showError(tr("There is no picture on the clipboard."));
        return;
    }
    m_files->newDocumentFromPicture(picture);
}

void MainWindow::paste(bool keepSelection) {
    withCanvas([this, keepSelection](CanvasView* c) {
        c->commitTransform();
        Document* d = c->document();
        std::optional<ClipboardImage> picture;
        const QMimeData* mime = QGuiApplication::clipboard()->mimeData();
        const QImage outside = mime && mime->hasImage() ? qvariant_cast<QImage>(mime->imageData()) : QImage();
        // NOLINTNEXTLINE(bugprone-branch-clone): the last branch gives the same, but only after another program's picture
        if (m_clip && mime && mime->data(QString::fromLatin1(kOwnCopyMime)) == m_clipTag) {
            picture = m_clip;
        } else if (!outside.isNull()) {
            picture = ClipboardImage::centred(outside, d->size());
        } else if (m_clip) {  // after another program's picture, which comes first
            picture = m_clip;
        }
        if (!picture) {
            showError(tr("There is nothing to paste."));
            return;
        }
        report(d->paste(*picture, keepSelection));
    });
}

void MainWindow::showGrowDialog(bool dilate) {
    Document* doc = m_tabs->currentDocument();
    if (!doc)
        return;
    if (!doc->selection()) {
        showError(tr("There is no selection."));
        return;
    }
    auto* dlg = new SelectionGrowDialog(dilate, this);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    connect(dlg, &QDialog::accepted, this, [this, dlg, doc = QPointer<Document>(doc), dilate] {
        if (doc)
            report(dilate ? doc->dilateSelection(dlg->growWidth()) : doc->erodeSelection(dlg->growWidth()));
    });
    dlg->open();  // not exec(): the dialog does not block the event loop
}

void MainWindow::newDocument() {
    addDocument(new Document(FileController::kDefaultCanvas, Qt::white), false);
}

void MainWindow::openFiles(const QStringList& paths) {
    for (const QString& path : paths)
        m_files->openFile(path);
}

void MainWindow::addDocument(Document* doc, bool restoreView) {
    if (m_options->values().recordNewCanvases)
        doc->startRecording();
    m_recovery->watch(doc);
    setUpView(m_tabs->addDocument(doc), restoreView);
}

void MainWindow::setUpView(CanvasView* view, bool restoreView) {
    view->setToolBox(m_tools);
    view->setSelectedLayers([this] { return m_layers->selectedLayers(); });
    applyCanvasOptions(view);
    view->setSelectionVisible(command(QStringLiteral("selection.show"))->isChecked());
    view->setFocus(Qt::OtherFocusReason);
    connect(view, &CanvasView::message, this, [this](const QString& text) { showError(text); });
    connect(view, &CanvasView::transformChanged, this, [this, view] {
        if (view == m_tabs->currentCanvas())
            m_toolPanel->fixedPanels()->setTransformActive(view->hasTransform());
    });
    connect(view, &CanvasView::viewChanged, this, &MainWindow::syncViewChecks);
    connect(view, &CanvasView::paintRefused, this, [this](const QString& why) { showError(why); });
    connect(view, &CanvasView::strokePainted, m_colorAssets, &ColorAssets::addToHistory);
    if (!restoreView)
        QMetaObject::invokeMethod(view, &CanvasView::resetView, Qt::QueuedConnection);
    updateTitle();
}

void MainWindow::updateTitle() {
    const Document* d = m_tabs->currentDocument();
    m_title = d ? QStringLiteral("%1 — Doodlebold").arg(DocumentTabs::titleTextOf(d)) : QStringLiteral("Doodlebold");
    setWindowTitle(m_title);
}

void MainWindow::dragEnterEvent(QDragEnterEvent* e) {
    // (urls() returns a new list each time: begin() and end() must come from one list.)
    const QList<QUrl> urls = e->mimeData()->urls();
    if (std::ranges::any_of(urls, [](const QUrl& u) { return u.isLocalFile(); }))
        e->acceptProposedAction();
}

void MainWindow::dropEvent(QDropEvent* e) {
    QStringList paths;
    for (const QUrl& u : e->mimeData()->urls())
        if (u.isLocalFile())
            paths << u.toLocalFile();
    if (paths.isEmpty())
        return;
    e->acceptProposedAction();
    // Opening can show dialogs: do it after the drop has finished, so the file manager is not kept waiting.
    QMetaObject::invokeMethod(this, [this, paths] { openFiles(paths); }, Qt::QueuedConnection);
}

void MainWindow::applyCanvasOptions(CanvasView* view) {
    const AppOptions::Values& v = m_options->values();
    view->setWheelZoom(v.swapWheelZoom, v.wheelZoomAtCentre);
    view->setQuickShapes(v.quickShapes, v.quickShapeHoldMs);
    view->setPressureRange({v.minimumPressure / 100.0, v.fullPressure / 100.0});
    view->setHeldKeys(v.held, v.altPicksColour, v.ctrlMovesLayers);
    view->setZoomedOut(v.zoomedOut);
    view->setBrushOutline(v.brushOutline);
    view->setStrokeTiming(v.strokeTiming);
}

void MainWindow::applyOptions() {
    const AppOptions::Values& v = m_options->values();
    BrushOptions brush = m_tools->options();
    brush.minimumPressure = v.minimumPressure / 100.0;
    m_tools->setOptions(brush);
    GlassPopups::setFrosted(v.frostedMenus);
    Document::setUndoSteps(v.undoSteps);
    stroke_memory::setPolicy({AppOptions::memoryBytes(v), v.keepBrushesInMemory});
    m_recovery->apply(v);
    m_toolPanel->parameters()->refreshSizeLimit();
    for (int i = 0; i < m_tabs->count(); ++i)
        applyCanvasOptions(m_tabs->canvasAt(i));
}

// The once-ever notice of a stroke too large for the memory, after that stroke, and the note when brushes aren't kept within
// it.
void MainWindow::buildMemoryNotice() {
    m_memoryNotice = new MemoryNotice(m_tools, this);
    connect(m_memoryNotice, &MemoryNotice::noteWanted, this, &MainWindow::showError);
    connect(m_memoryNotice, &MemoryNotice::noticeWanted, this, [this](const MemoryNotice::Event& event) {
        QMessageBox* box = MemoryNotice::makeBox(event, this);
        box->setAttribute(Qt::WA_DeleteOnClose);
        QAbstractButton* options = box->findChild<QPushButton*>(QStringLiteral("memoryNoticeOptions"));
        connect(box, &QMessageBox::buttonClicked, this, [this, options](QAbstractButton* clicked) {
            if (clicked == options)
                showOptions(true);
        });
        box->open();
    });
}

// Edit > Options…: OK applies and saves, Cancel leaves everything. Not modal: the dialog is a child that deletes
// itself.
void MainWindow::showOptions(bool atMemory) {
    auto* dlg = new OptionsDialog(m_options->values(), this);
    if (atMemory)
        dlg->showMemorySection();
    dlg->addResourcesPage(m_brushResources, m_tools);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->keysPage()->setTable(keyTable());
    if (CanvasView* canvas = m_tabs->currentCanvas())
        dlg->showGraphicsChoice(canvas->fastZoomedOut());
    connect(dlg, &QDialog::accepted, this, [this, dlg] {
        m_options->setValues(dlg->values());
        applyKeyTable(dlg->keysPage()->table());
    });
    dlg->open();
}

void MainWindow::checkForUpdatesAtStart() {
    if (m_options->values().checkForUpdatesAtStart)
        m_updateNotice->checkAtStart();
}

void MainWindow::showTabletDiagnostics() {
    auto* dlg = new TabletDiagnosticsDialog(m_stats, this);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->show();
}

CrashReportDialog* MainWindow::showCrashReport(const QString& report, bool afterCrash) {
    auto* dlg = new CrashReportDialog(report, afterCrash, this);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->open();  // window-modal, as About is
    return dlg;
}

void MainWindow::offerRecovery() {
    const std::vector<LeftoverCanvas> canvases = m_recovery->leftovers();
    if (canvases.empty())
        return;
    auto* dlg = new RecoveryDialog(m_recovery, canvases, this);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->open();
}

void MainWindow::showStartDialogs(const QString& crashReport) {
    if (crashReport.isEmpty())
        offerRecovery();
    else
        connect(showCrashReport(crashReport, true), &QDialog::finished, this, &MainWindow::offerRecovery);
}

void MainWindow::showAbout() {
    auto* dlg = new AboutDialog(this);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    // Window-modal, as every other dialog opens: on Wayland a window can't place itself, and the desktop centres a modal dialog
    // on its window but puts others where it likes (feedback: "The about doodlebold dialogue does not open up at the center of
    // the window").
    dlg->open();
}

bool MainWindow::startReplay(Document* doc) {
    if (!doc || isReplaying() || !doc->isRecording())
        return false;
    const std::shared_ptr<const Timelapse> recording = doc->timelapse();  // waits for the worker that is still compressing
    if (!recording)
        return false;
    TimelapseReplayView* view = m_tabs->showReplay(doc, recording);
    if (!view)
        return false;
    m_replayDoc = doc;
    connect(view, &TimelapseReplayView::doneRequested, this, &MainWindow::endReplay);
    connect(view, &TimelapseReplayView::exportRequested, this, [this, doc] { m_timelapse->exportTimelapse(doc); });
    setReplayState(true);
    return true;
}

void MainWindow::endReplay() {
    if (!m_replayDoc)
        return;
    m_tabs->endReplay(m_replayDoc);
    m_replayDoc.clear();
    setReplayState(false);
}

void MainWindow::setReplayState(bool replaying) {
    for (QDockWidget* d : {m_navigatorDock, m_colorDock, m_toolsDock, m_layersDock}) {
        d->setEnabled(!replaying);
        if (!replaying) {
            d->widget()->setGraphicsEffect(nullptr);
        } else {
            auto* dim = new QGraphicsOpacityEffect(d->widget());
            dim->setOpacity(kReplayDimOpacity);
            d->widget()->setGraphicsEffect(dim);
        }
    }
    m_quickBar->setEnabled(!replaying);
    updateCommandStates();
    if (replaying)
        statusBar()->showMessage(tr("Timelapse Replay: Done or Esc returns to the canvas"), 4000);
}

void MainWindow::onCurrentDocumentChanged() {
    if (isReplaying() && m_tabs->currentDocument() != m_replayDoc.data())
        endReplay();
    Document* d = m_tabs->currentDocument();
    updateCommandStates();
    m_layers->setDocument(d);
    m_animation->setDocument(d);
    updateFrameStatus();
    m_navigator->setCanvas(m_tabs->currentCanvas());
    if (CanvasView* c = m_tabs->currentCanvas())
        m_toolPanel->fixedPanels()->setTransformActive(c->hasTransform());
    m_quickBar->setCanvas(m_tabs->currentCanvas());
    m_toolPanel->parameters()->setCanvasSize(d ? d->size() : QSize());
    watchDocument(d);
    updateTitle();
    syncViewChecks();
}

void MainWindow::syncViewChecks() {
    if (Document* d = m_tabs->currentDocument())
        if (QAction* flip = command(QStringLiteral("view.flip")))
            flip->setChecked(d->view().flipH);
}

void MainWindow::changeEvent(QEvent* e) {
    // A maximized canvas window appends " - [its title]" to this one (Qt's MDI); the title stays as updateTitle() made it.
    if (e->type() == QEvent::WindowTitleChange && !m_title.isEmpty() && windowTitle() != m_title)
        setWindowTitle(m_title);
    QMainWindow::changeEvent(e);
    if (e->type() == QEvent::WindowStateChange)
        if (QAction* full = command(QStringLiteral("window.fullScreen")))
            full->setChecked(isFullScreen());
}

// A running stroke has its own copy of its settings and colour, the navigator only moves the view, and opening a menu changes nothing.
bool MainWindow::worksWhileAStrokePaints(const QWidget* widget) const {
    if (qobject_cast<const QMenuBar*>(widget) || qobject_cast<const QMenu*>(widget))
        return true;
    const std::array<const QDockWidget*, 3> docks = {m_toolsDock, m_colorDock, m_navigatorDock};
    return std::ranges::any_of(docks, [&](const QDockWidget* dock) { return dock && (dock == widget || dock->isAncestorOf(widget)); });
}

// A plain key picks a tool or a colour. Keys held with Ctrl or Alt, and these, can change or read the picture.
static bool keyMayNeedThePicture(const QKeyEvent& key) {
    if (key.modifiers() & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier))
        return key.key() != Qt::Key_Control && key.key() != Qt::Key_Alt && key.key() != Qt::Key_Meta;
    return key.key() == Qt::Key_Delete || key.key() == Qt::Key_Backspace || key.key() == Qt::Key_Return || key.key() == Qt::Key_Enter;
}

// A stroke can still be painting after the pen lifts. What might need the finished picture lets it finish first: a shortcut, picking
// a menu item, a click outside the canvas, or closing. Moving, panning and painting further don't, and nor do the panels above.
void MainWindow::settleStrokesBefore(QObject* watched, QEvent* e) {
    bool settle = false;  // This hears from widgets being destroyed, so it mustn't walk the window's widgets.
    if (e->type() == QEvent::ShortcutOverride) {
        settle = keyMayNeedThePicture(*static_cast<QKeyEvent*>(e));
    } else if (e->type() == QEvent::MouseButtonPress) {
        auto* widget = qobject_cast<QWidget*>(watched);
        settle = widget && !qobject_cast<CanvasView*>(watched) && !worksWhileAStrokePaints(widget);
    } else if (e->type() == QEvent::MouseButtonRelease) {
        settle = qobject_cast<QMenu*>(watched) != nullptr;
    } else if (e->type() == QEvent::Close) {
        settle = watched->isWidgetType();
    }
    if (settle)
        CanvasView::settleAllStrokes();
}

bool MainWindow::eventFilter(QObject* watched, QEvent* e) {
    settleStrokesBefore(watched, e);
    if (watched == this && e->type() == QEvent::HoverMove && QApplication::mouseButtons() == Qt::NoButton &&
        isStuckPanelGap(static_cast<QHoverEvent*>(e)->position().toPoint())) {
        QHoverEvent leave(QEvent::HoverLeave, QPointF(), QPointF(), QPointF());
        QMainWindow::event(&leave);  // takes back a resize cursor the last gap left
        return true;
    }
    if (watched == this && e->type() == QEvent::MouseButtonPress && isStuckPanelGap(static_cast<QMouseEvent*>(e)->position().toPoint()))
        return true;
    if (watched == this && e->type() == QEvent::MouseButtonRelease)
        m_colorLayouts.clear();  // a divider between the panels may have been dragged: the colour dock's height is the user's
    if (e->type() == QEvent::TabletEnterProximity || e->type() == QEvent::TabletLeaveProximity) {
        auto* te = static_cast<QTabletEvent*>(e);
        m_stats->setProximity(e->type() == QEvent::TabletEnterProximity, TabletInput::describe(te->pointingDevice()));
    } else if ((e->type() == QEvent::TabletMove || e->type() == QEvent::TabletPress) && !m_stats->inProximity()) {
        // A pen already in range when the program started sends no proximity event.
        m_stats->setProximity(true, TabletInput::describe(static_cast<QTabletEvent*>(e)->pointingDevice()));
    } else if (e->type() == QEvent::Move || e->type() == QEvent::Resize || e->type() == QEvent::Show || e->type() == QEvent::Hide) {
        for (QDockWidget* d : {m_navigatorDock, m_colorDock, m_toolsDock, m_layersDock})
            if (d == watched) {
                scheduleCardEdges();
                break;
            }
        if (watched == this || watched == centralWidget())
            scheduleCardEdges();
    } else if (e->type() == QEvent::ZOrderChange) {
        // Qt raises its dock separators each time it lays the window out.
        if (m_columnDivider && watched != m_columnDivider && watched->parent() == this)
            m_columnDivider->raise();
    } else if (e->type() == QEvent::Close) {
        for (const Panel& p : m_panels)
            if (p.widget == watched)
                if (QAction* a = command(QStringLiteral("window.show.") + p.id))
                    a->setChecked(false);
    }
    return QMainWindow::eventFilter(watched, e);
}

bool MainWindow::isStuckPanelGap(const QPoint& pos) const {
    if (childAt(pos))
        return false;
    bool above = false, below = false;
    int shrinkAbove = 0, growAbove = 0, shrinkBelow = 0, growBelow = 0;
    for (const QDockWidget* d : {m_navigatorDock, m_colorDock, m_toolsDock, m_layersDock}) {
        if (!d || !d->isVisible() || d->isFloating())
            continue;
        const QRect r = d->geometry();
        if (pos.x() < r.left() || pos.x() > r.right())
            continue;
        const int shrink = r.height() - d->layout()->minimumSize().height();
        const int grow = d->layout()->maximumSize().height() - r.height();
        if (r.bottom() < pos.y()) {
            above = true;
            shrinkAbove += shrink;
            growAbove += grow;
        } else {
            below = true;
            shrinkBelow += shrink;
            growBelow += grow;
        }
    }
    return above && below && std::min(shrinkAbove, growBelow) <= 0 && std::min(growAbove, shrinkBelow) <= 0;
}

int MainWindow::outerGap() const {
    return 2 * Theme::metric(Theme::Metric::CardGap) + style()->pixelMetric(QStyle::PM_DockWidgetSeparatorExtent, nullptr, this);
}

void MainWindow::scheduleCardEdges() {
    if (m_cardEdgesPending)
        return;
    m_cardEdgesPending = true;
    QTimer::singleShot(0, this, &MainWindow::syncCardEdges);
}

void MainWindow::syncCardEdges() {
    m_cardEdgesPending = false;
    if (!centralWidget())
        return;
    const int extra = outerGap() - Theme::metric(Theme::Metric::CardGap);
    const QRect area = centralWidget()->geometry();
    for (QDockWidget* d : {m_navigatorDock, m_colorDock, m_toolsDock, m_layersDock}) {
        auto* card = d ? qobject_cast<PanelCard*>(d->widget()) : nullptr;
        if (!card)
            continue;
        const bool docked = !d->isFloating() && d->isVisible();
        const QRect r = d->geometry();
        card->setEdgeGap(docked && r.top() <= area.top() + 1 ? extra : 0, docked && r.bottom() >= area.bottom() - 1 ? extra : 0);
    }
    placeColumnDivider();
}

namespace {
// QMainWindow::saveState() version: a different layout of panels starts afresh.
constexpr int kLayoutVersion = 1;

bool partOfLayout(const QString& id) {
    return id.startsWith(QLatin1String("window.show.")) || id == QLatin1String("window.colorToolRight") ||
           id == QLatin1String("window.navigatorLayerRight");
}
}

void MainWindow::saveLayout() {
    QSettings s = AppOptions::settings();
    s.beginGroup(QStringLiteral("layout"));
    s.setValue(QStringLiteral("geometry"), saveGeometry());
    // Without the quick bar: placeQuickBar moves it between the toolbar area and the menu bar, and a restored layout that
    // puts it back in the toolbar area while the menu bar holds it leaves Qt's toolbar layout with a dangling item.
    const bool inToolBarArea = m_quickBar && toolBarArea(m_quickBar) != Qt::NoToolBarArea;
    const bool shown = m_quickBar && !m_quickBar->isHidden();
    if (inToolBarArea)
        removeToolBar(m_quickBar);
    s.setValue(QStringLiteral("state"), saveState(kLayoutVersion));
    if (inToolBarArea) {
        addToolBar(m_quickBar);
        m_quickBar->setVisible(shown);
    }
    for (auto it = m_actions.cbegin(); it != m_actions.cend(); ++it)
        if (it.value()->isCheckable() && partOfLayout(it.key()))
            s.setValue(QStringLiteral("check/") + it.key(), it.value()->isChecked());
    s.endGroup();
}

void MainWindow::restoreLayout() {
    QSettings s = AppOptions::settings();
    s.beginGroup(QStringLiteral("layout"));
    for (auto it = m_actions.cbegin(); it != m_actions.cend(); ++it) {
        const QString key = QStringLiteral("check/") + it.key();
        if (it.value()->isCheckable() && partOfLayout(it.key()) && s.contains(key)) {
            const bool on = s.value(key).toBool();
            if (it.value()->isChecked() != on)
                it.value()->trigger();
        }
    }
    if (const QByteArray g = s.value(QStringLiteral("geometry")).toByteArray(); !g.isEmpty())
        restoreGeometry(g);
    if (const QByteArray st = s.value(QStringLiteral("state")).toByteArray(); !st.isEmpty()) {
        // The quick bar stays out of it (see saveLayout), wherever placeQuickBar has put it.
        const bool inToolBarArea = m_quickBar && toolBarArea(m_quickBar) != Qt::NoToolBarArea;
        const bool shown = m_quickBar && !m_quickBar->isHidden();
        if (inToolBarArea)
            removeToolBar(m_quickBar);
        if (restoreState(st, kLayoutVersion))
            ++m_arrangement;  // the sizes are the user's: arrangeDocks's second pass must not replace them
        if (inToolBarArea) {
            addToolBar(m_quickBar);
            m_quickBar->setVisible(shown);
        }
    }
    m_colorLayouts.clear();
    s.endGroup();
}

void MainWindow::closeEvent(QCloseEvent* e) {
    if (m_tabs->requestCloseAll()) {
        saveLayout();
        m_toolStore->flush();
        m_colorStore->flush();
        e->accept();
    } else {
        e->ignore();
    }
}

}
