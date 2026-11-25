#pragma once

#include "core/document/Clipboard.h"
#include "core/document/EditError.h"
#include "core/layers/CanvasBackground.h"
#include "core/layers/LayerId.h"
#include "core/options/KeyTable.h"
#include "core/transform/Resample.h"

#include <QHash>
#include <QMainWindow>
#include <QPointer>

#include <functional>
#include <optional>
#include <utility>
#include <vector>

class QAction;
class QDockWidget;
class QDragEnterEvent;
class QDropEvent;
class QLabel;
class QMenu;

namespace doodlebold {

class BrushResourceLibrary;
class ColorAssets;
class ColumnDivider;
class ColorAssetsStore;
class ColorPanel;

namespace menu {
struct Entry;
struct KeyCommand;
}

class CanvasView;
class AppOptions;
class Document;
class DocumentTabs;
class UsageMeters;
class FileController;
class LayerPanel;
class NavigatorPanel;
class QuickBar;
class CrashReportDialog;
class RecoveryController;
class ToastStack;
class MemoryNotice;
class UpdateNotice;
class TabletStats;
class ToolBox;
class ToolBoxStore;
class ToolPanel;
class TimelapseController;
class EffectController;
class AnimationController;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

    DocumentTabs* tabs() const { return m_tabs; }
    ToolBox* tools() const { return m_tools; }

    FileController* files() const { return m_files; }
    QuickBar* quickBar() const { return m_quickBar; }
    AppOptions* options() const { return m_options; }
    EffectController* effects() const { return m_effects; }
    AnimationController* animation() const { return m_animation; }
    RecoveryController* recovery() const { return m_recovery; }

    // The action of a menu entry or key command by its id in MenuSpec ("file.new", "tool.pencil"); null if there is no
    // such command.
    QAction* command(const QString& id) const { return m_actions.value(id); }
    int outerGap() const;

    bool startReplay(Document* doc);
    void endReplay();
    bool isReplaying() const { return !m_replayDoc.isNull(); }

    void setCanvasBackground(BackgroundMode mode);

    // Once the window shows: unless Options › View turned it off, the once-a-day check that speaks only of a newer release.
    void checkForUpdatesAtStart();

public slots:
    void newDocument();
    void openFiles(const QStringList& paths);
    void showTabletDiagnostics();
    void showAbout();
    CrashReportDialog* showCrashReport(const QString& report, bool afterCrash);
    // The canvases a run that didn't close left recovery copies of, if there are any.
    void offerRecovery();
    // At start: the last run's crash report if it crashed, then the canvases to recover, one after the other so that neither
    // dialog covers the other.
    void showStartDialogs(const QString& crashReport);

protected:
    void closeEvent(QCloseEvent* e) override;
    void dragEnterEvent(QDragEnterEvent* e) override;
    void dropEvent(QDropEvent* e) override;
    void changeEvent(QEvent* e) override;
    void resizeEvent(QResizeEvent* e) override;
    bool eventFilter(QObject* watched, QEvent* e) override;

private:
    enum class Enable {
        Always,
        Document,
        Selection,
        CanUndo,
        CanRedo,
        Paste,
        Clipboard,
        SeveralLayers,
        Deletable,
        Recording,
        SeveralFrames,
        Video
    };
    struct Binding {
        std::function<void()> run;
        std::function<void(bool)> toggled;
        Enable enable = Enable::Document;
        bool checkedAtStart = false;
    };

    void buildMenus();
    void registerFileCommands();
    void registerEditCommands();
    void registerCanvasCommands();
    void registerLayerCommands();
    void registerEffectCommands();
    void registerSelectionCommands();
    void registerViewCommands();
    void registerWindowCommands();
    void registerOtherCommands();
    void registerKeyCommands();
    // Options › Keys (MainWindowKeys.cpp): the user's keys for commands and tray tools.
    void bindKeys();
    KeyTable keyTable() const;
    void applyKeyTable(const KeyTable& table);
    void rebuildTrayKeys();
    // The timeline and the animation commands (MainWindowAnimation.cpp).
    void buildTimeline();
    void registerAnimationCommands();
    void setTimelineShown(bool shown);
    // Delete clears the timeline's selection instead of the layer while the timeline holds it (FrameStrip::selectionActive).
    bool deleteInTimeline();
    void updateFrameStatus();
    void bind(const QString& id, std::function<void()> run, Enable enable = Enable::Document);
    void bindCheck(const QString& id, bool checked, std::function<void(bool)> toggled, Enable enable = Enable::Always);
    QAction* actionFor(std::string_view id);
    bool populate(const menu::Entry& entry, QMenu* menu);
    bool isEnabled(Enable e) const;
    void updateCommandStates();
    // Whether the system clipboard holds a picture, asked when it says it changed rather than whenever a command's state is
    // checked: asking costs a few ms a time, and a stroke's undo step checks them all.
    void noteClipboard();
    void setReplayState(bool replaying);
    void watchDocument(Document* doc);

    void rebuildRecentMenu(QMenu* menu);
    void rebuildViewList(QMenu* viewMenu);
    void updateBackgroundMenu();
    void specifyBackgroundColor();
    void applyOptions();
    // Edit › Options…; `atMemory` opens it on Options › Performance › Memory.
    void showOptions(bool atMemory = false);
    void buildMemoryNotice();
    void applyPanelVisibility();
    void bindColorPanel();
    void fitColorDock();
    void setColorDockHeight(int height);
    void arrangeDocks();
    // The cards at the top and the bottom of a column get an extra edge gap, so that the panel area's gap to the menu
    // bar and the status bar equals the gap between two cards (outerGap()); the canvas area gets the same through its
    // margins.
    void scheduleCardEdges();
    void syncCardEdges();
    // Two panel columns side by side leave a gap that sets the width of the column at the window's edge: the other one
    // keeps its width and the canvas gives or takes the room. Qt's own separator there only trades width between the two
    // columns, which are usually both at their least, so it would not move.
    struct ColumnDrag {
        QDockWidget* edge = nullptr;
        QDockWidget* inner = nullptr;
        int edgeWidth = 0, innerWidth = 0, least = 0, most = 0;
        int direction = 1;  // what moving right does to the edge column's width
    };
    std::pair<QDockWidget*, QDockWidget*> sideBySideColumns() const;
    void placeColumnDivider();
    void startColumnDrag();
    void dragColumnDivider(int dx);
    // Whether a point is on the gap between two panels of a column that neither panel's height lets move, such as under the
    // Color card with only its circle. Qt gives every gap a resize cursor and a drag; these get neither.
    bool isStuckPanelGap(const QPoint& pos) const;
    void addDocument(Document* doc, bool restoreView);
    void setUpView(CanvasView* view, bool restoreView);
    void updateTitle();
    void buildPanels();
    void copyOrCut(bool cut, bool bounds = false);
    void canvasFromClipboard();
    void paste(bool keepSelection);
    void applyTurn(transform::Turn turn);
    void showGrowDialog(bool dilate);
    void populateQuickBar();
    void placeQuickBar();
    // Shows why an edit was refused. "Nothing to change" is not worth a message.
    template <typename T> void report(const EditResult<T>& result) {
        if (!result && result.error() != EditError::NoChange)
            showRefusal(result.error());
    }
    void showRefusal(EditError error);
    void applyCanvasOptions(CanvasView* view);
    void settleStrokesBefore(QObject* watched, QEvent* e);
    bool worksWhileAStrokePaints(const QWidget* widget) const;
    void showError(const QString& text);
    void withCanvas(const std::function<void(CanvasView*)>& f);
    void withLayer(const std::function<void(Document*, LayerId)>& f);
    void onCurrentDocumentChanged();
    void syncViewChecks();
    void saveLayout();
    void restoreLayout();

    TabletStats* m_stats;
    ToolBox* m_tools;
    DocumentTabs* m_tabs;
    ToastStack* m_toasts = nullptr;
    FileController* m_files = nullptr;
    AppOptions* m_options = nullptr;
    RecoveryController* m_recovery = nullptr;
    MemoryNotice* m_memoryNotice = nullptr;
    UpdateNotice* m_updateNotice = nullptr;
    QHash<QString, Binding> m_bindings;
    QHash<QString, QAction*> m_actions;
    std::vector<QAction*> m_trayKeyActions;  // one for each tray tool with a key
    std::vector<QMetaObject::Connection> m_documentConnections;
    std::vector<QAction*> m_viewListActions;
    struct Panel {
        QString id;
        QWidget* widget;
        bool shownAtStart = true;
    };
    std::vector<Panel> m_panels;
    bool m_cardEdgesPending = false;
    QDockWidget* m_navigatorDock = nullptr;
    QDockWidget* m_colorDock = nullptr;
    QHash<int, std::vector<QSize>> m_colorLayouts;
    std::vector<QSize> dockSizes() const;
    void setDockSizes(const std::vector<QSize>& sizes);
    int m_colorViews = -1;
    int colorViewsShown() const;
    bool m_docksArranged = false;
    int m_arrangement = 0;  // an arrangement's second pass is dropped once a later one or a restored layout has replaced it
    ColumnDivider* m_columnDivider = nullptr;
    ColumnDrag m_columnDrag;
    QDockWidget* m_toolsDock = nullptr;
    QDockWidget* m_layersDock = nullptr;
    LayerPanel* m_layers = nullptr;
    NavigatorPanel* m_navigator = nullptr;
    ToolPanel* m_toolPanel = nullptr;
    ToolBoxStore* m_toolStore = nullptr;
    BrushResourceLibrary* m_brushResources = nullptr;
    TimelapseController* m_timelapse = nullptr;
    EffectController* m_effects = nullptr;
    AnimationController* m_animation = nullptr;
    QLabel* m_frameStatus = nullptr;
    QPointer<Document> m_replayDoc;
    ColorAssets* m_colorAssets = nullptr;
    ColorAssetsStore* m_colorStore = nullptr;
    ColorPanel* m_colorPanel = nullptr;
    QuickBar* m_quickBar = nullptr;
    std::optional<ClipboardImage> m_clip;
    bool m_clipboardHasImage = false;
    // What the current canvas's undo stack last said, so its signals, sent at every undo step, update the commands only on a change.
    bool m_canUndo = false;
    bool m_canRedo = false;
    QByteArray m_clipTag;  // marks our copy on the system clipboard (this process, this copy)
    QString m_title;
    UsageMeters* m_usage = nullptr;
};

}
