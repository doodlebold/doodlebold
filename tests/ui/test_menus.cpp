#include "core/document/Document.h"
#include "core/selection/SelectionOps.h"
#include "core/tools/ToolBox.h"
#include "core/tools/ToolBoxStore.h"
#include "ui/CanvasView.h"
#include "ui/DocumentTabs.h"
#include "ui/UsageMeters.h"
#include "ui/ViewSelector.h"
#include "core/options/AppOptions.h"
#include "ui/MainWindow.h"
#include "ui/panels/ColorPanel.h"
#include "ScreenshotSupport.h"

#include <QAction>
#include <QActionGroup>
#include <QDockWidget>
#include <QFile>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QRegularExpression>
#include <QSettings>
#include <QStandardPaths>
#include <QStatusBar>
#include <QTabBar>
#include <QTemporaryDir>
#include <QToolBar>
#include <QToolButton>

#include <gtest/gtest.h>

#include <functional>
#include <memory>
#include <string_view>

using namespace doodlebold;

namespace {

QString labelOf(const QAction* a) {
    return a->text().section(QLatin1Char('\t'), 0, 0);
}

// The key sequences of a shortcut text ("F11, Shift+Tab"); fails the test on a key Qt cannot read, which would otherwise
// compare equal to every other unreadable key.
QList<QKeySequence> keysOf(const std::string& shortcuts) {
    QList<QKeySequence> out;
    for (QString one : QString::fromStdString(shortcuts).split(QStringLiteral(", "), Qt::SkipEmptyParts)) {
        one.replace(QStringLiteral("PageUp"), QStringLiteral("PgUp")).replace(QStringLiteral("PageDown"), QStringLiteral("PgDown"));
        out << QKeySequence::fromString(one, QKeySequence::PortableText);
        EXPECT_FALSE(out.last().isEmpty()) << "unreadable key: " << one.toStdString();
    }
    return out;
}

class MenuTest : public testing::Test {
protected:
    void SetUp() override {
        QStandardPaths::setTestModeEnabled(true);  // never touch the real tools.json
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDir.path());
        test::applyTheme();
        QFile::remove(ToolBoxStore::defaultPath());
        window = std::make_unique<MainWindow>();
    }
    void TearDown() override { QFile::remove(ToolBoxStore::defaultPath()); }

    // A canvas, as File → New would make it; before show(): the offscreen platform cannot paint a canvas added to a visible window.
    void openCanvas() {
        window->newDocument();
        window->show();
        QCoreApplication::processEvents();
    }

    QMenu* menu(const QString& title) const {
        for (QAction* top : window->menuBar()->actions())
            if (top->text() == title)
                return top->menu();
        return nullptr;
    }
    QStringList titles() const {
        QStringList out;
        for (const QAction* top : window->menuBar()->actions())
            out << top->text();
        return out;
    }
    static QStringList labels(const QMenu* m) {
        QStringList out;
        for (const QAction* a : m->actions())
            out << (a->isSeparator() ? QStringLiteral("---") : labelOf(a));
        return out;
    }
    QAction* command(const char* id) const {
        QAction* a = window->command(QString::fromLatin1(id));
        EXPECT_NE(a, nullptr) << id;
        return a;
    }
    Document* doc() const { return window->tabs()->currentDocument(); }
    CanvasView* canvas() const { return window->tabs()->currentCanvas(); }

    QTemporaryDir settingsDir;
    std::unique_ptr<MainWindow> window;
};

}

TEST_F(MenuTest, TheMenuBarHoldsTheseMenus) {
    EXPECT_EQ(titles(), (QStringList{"File", "Edit", "Canvas", "Layer", "Effects", "Selection", "View", "Window", "Help"}));
}

TEST_F(MenuTest, TheMenusHoldExactlyWhatDoodleboldImplements) {
    EXPECT_EQ(labels(menu("File")), (QStringList{"New…", "New from Clipboard", "New from Video…", "---", "Open…", "Open Recent", "---",
                                                 "Save", "Save As…", "Export", "Timelapse", "---", "Close", "Quit"}));
    EXPECT_EQ(labels(menu("Edit")), (QStringList{"Undo", "Redo", "---", "Cut", "Copy", "Paste", "---", "Copy Selection Rectangle",
                                                 "Paste and Keep Selection", "---", "Options…"}));
    EXPECT_EQ(labels(menu("Canvas")),
              (QStringList{"Background", "---", "Trim to Selection", "---", "Flip Horizontal", "Flip Vertical", "Rotate 90° Left",
                           "Rotate 90° Right", "---", "Show Timeline", "New Frame", "Duplicate Frame", "Delete Frame", "---", "Add Video…",
                           "Remove Video", "---", "Keep Only This Frame…"}));
    EXPECT_EQ(labels(menu("Layer")),
              (QStringList{"New Layer", "New Folder", "Duplicate Layer", "Delete Layer", "---", "Fill", "Clear", "Rasterize", "---",
                           "Move Painting to Layer Below", "Merge Layers", "Merge Visible Layers", "Flatten Image", "---", "Transform",
                           "Flip Horizontal", "Flip Vertical", "Rotate 90° Left", "Rotate 90° Right"}));
    EXPECT_EQ(labels(menu("Effects")),
              (QStringList{"Brightness/Contrast…", "Hue/Saturation/Lightness…", "Levels…", "Color Balance…", "Invert", "Desaturate",
                           "Posterize…", "---", "Gaussian Blur…", "Motion Blur…", "Radial Blur…", "---", "Sharpen/Unsharp Mask…",
                           "Add Noise…", "Chromatic Aberration…"}));
    EXPECT_EQ(labels(menu("Selection")), (QStringList{"Select All", "Deselect", "Invert", "---", "Grow…", "Shrink…", "Grow by 1 Pixel",
                                                      "Shrink by 1 Pixel", "---", "Show Selection"}));
    EXPECT_EQ(labels(menu("View")),
              (QStringList{"Zoom In", "Zoom Out", "Actual Size", "Fit to Window", "---", "Rotate Left", "Rotate Right", "Reset Rotation",
                           "Flip View", "---", "Reset View", "---", "Close View", "Close All Views"}));
    EXPECT_EQ(labels(menu("Help")), (QStringList{"Tablet Diagnostics…", "Report a Bug…", "Crash Report…", "---", "Check for Updates…",
                                                 "About Doodlebold…"}));
    const QAction* timelapse = window->findChild<QAction*>(QStringLiteral("file.timelapse"));
    ASSERT_TRUE(timelapse && timelapse->menu());
    EXPECT_EQ(labels(timelapse->menu()), (QStringList{"Record Timelapse", "Replay", "---", "Restart Recording…", "Delete Recording…"}));
}

TEST_F(MenuTest, FileExportHoldsEveryKindOfExport) {
    const QAction* exportEntry = window->findChild<QAction*>(QStringLiteral("file.export"));
    ASSERT_TRUE(exportEntry && exportEntry->menu());
    EXPECT_EQ(labels(exportEntry->menu()), (QStringList{"PNG (.png)…", "JPEG (.jpg)…", "Photoshop (.psd)…", "Large Photoshop (.psb)…",
                                                        "Bitmap (.bmp)…", "Targa (.tga)…", "Video (.mp4)…", "---", "Timelapse…"}));
}

TEST_F(MenuTest, EveryLabelIsPlainText) {
    const std::function<void(const QMenu*)> check = [&](const QMenu* m) {
        for (const QAction* a : m->actions()) {
            const QString label = labelOf(a);
            EXPECT_FALSE(label.contains(QLatin1String("..."))) << label.toStdString();
            EXPECT_FALSE(label.contains(QLatin1String(" …"))) << label.toStdString();
            EXPECT_FALSE(label.contains(QRegularExpression(QStringLiteral(R"(\([A-Z0-9]\)$)")))) << label.toStdString();
            if (a->menu())
                check(a->menu());
        }
    };
    for (const QAction* top : window->menuBar()->actions())
        check(top->menu());
}

TEST_F(MenuTest, EntriesKeepTheirKeys) {
    const std::pair<const char*, const char*> keys[] = {
        {"file.new", "Ctrl+N"},
        {"file.open", "Ctrl+O"},
        {"file.canvasFromClipboard", "Ctrl+B"},
        {"file.save", "Ctrl+S"},
        {"file.saveAs", "Shift+Ctrl+S"},
        {"edit.undo", "Ctrl+Z"},
        {"edit.redo", "Ctrl+Y"},
        {"edit.cut", "Ctrl+X"},
        {"edit.copy", "Ctrl+C"},
        {"edit.paste", "Ctrl+V"},
        {"selection.all", "Ctrl+A"},
        {"selection.deselect", "Ctrl+D"},
        {"selection.show", "Ctrl+H"},
        {"layer.newNormal", "Shift+Ctrl+N"},
        {"layer.erase", "Delete"},
        {"layer.transferDown", "F"},
        {"layer.merge", "Ctrl+E"},
        {"layer.fill", "Alt+Delete"},
        {"layer.transform", "Ctrl+T"},
        {"effects.hueSaturation", "Ctrl+U"},
        {"view.close", "Ctrl+W"},
        {"view.rotateCCW", "Shift+PageUp"},
        {"view.rotateCW", "Shift+PageDown"},
        {"view.scale100", "Ctrl+Alt+0"},
        {"view.fit", "Ctrl+0"},
        {"view.resetPosition", "Home"},
        {"view.resetAngle", "Shift+Home"},
        {"view.flip", "H"},
        {"window.fullScreen", "F11, Shift+Tab"},
        {"window.showAll", "Tab"},
    };
    for (const auto& [id, key] : keys)
        EXPECT_EQ(command(id)->shortcuts(), keysOf(key)) << id;
}

TEST_F(MenuTest, NoImportEntries) {
    for (const QAction* a : window->findChildren<QAction*>())
        EXPECT_FALSE(a->text().contains(QLatin1String("Import"))) << a->text().toStdString();
    EXPECT_EQ(window->command(QStringLiteral("file.import")), nullptr);
    const QAction* recent = window->findChild<QAction*>(QStringLiteral("file.recent"));
    ASSERT_TRUE(recent && recent->menu());
    emit recent->menu()->aboutToShow();
    for (const QAction* a : recent->menu()->actions())
        EXPECT_FALSE(a->text().contains(QLatin1String("Clear")));
}

TEST_F(MenuTest, KeyCommandsHaveTheirDefaultKeys) {
    const std::pair<const char*, const char*> keys[] = {
        {"tool.pencil", "N"},
        {"tool.brush", "V"},
        {"tool.eraser", "E"},
        {"tool.waterColor", "C"},
        {"tool.selectionPen", "A"},
        {"tool.selectionEraser", "S"},
        {"tool.marquee", "M"},
        {"tool.lasso", "L"},
        {"tool.wand", "W"},
        {"brush.sizePrevious", "["},
        {"brush.sizeNext", "]"},
        {"brush.density10", "1"},
        {"brush.density50", "5"},
        {"brush.density90", "9"},
        {"brush.density100", "0"},
        {"view.next", "Ctrl+Tab"},
        {"view.previous", "Shift+Ctrl+Tab"},
        {"layer.forward", "Ctrl+["},
        {"layer.backward", "Ctrl+]"},
        {"layer.toFront", "Shift+Ctrl+["},
        {"layer.toBack", "Shift+Ctrl+]"},
        {"layer.selectForward", "Alt+["},
        {"layer.selectBackward", "Alt+]"},
        {"color.exchange", "X"},
        {"color.transparent", "-"},
    };
    for (const auto& [id, key] : keys)
        EXPECT_EQ(command(id)->shortcuts(), keysOf(key)) << id;
    EXPECT_EQ(command("view.zoomInAtCursor")->shortcuts(), keysOf("PageUp, Ctrl+="));
    EXPECT_EQ(command("view.zoomOutAtCursor")->shortcuts(), keysOf("PageDown, Ctrl+-"));
    EXPECT_FALSE(labels(menu(QStringLiteral("View"))).contains(QStringLiteral("Zoom In at Cursor Position")));
    EXPECT_EQ(command("view.zoomIn")->shortcuts(), QList<QKeySequence>{});
    EXPECT_EQ(command("view.zoomOut")->shortcuts(), QList<QKeySequence>{});
}

TEST_F(MenuTest, NoTwoCommandsShareAKey) {
    QMap<QKeySequence, QString> seen;
    for (const QAction* a : window->findChildren<QAction*>())
        for (const QKeySequence& k : a->shortcuts()) {
            EXPECT_FALSE(seen.contains(k)) << k.toString().toStdString() << ": " << a->text().toStdString() << " and "
                                           << seen.value(k).toStdString();
            seen.insert(k, a->text());
        }
}

TEST_F(MenuTest, WithoutACanvasOnlyNewOpenRecentQuitAndTheWindowAndOtherMenusWork) {
    for (const char* id : {"file.save", "file.saveAs", "file.close", "edit.undo", "edit.redo", "edit.cut", "edit.copy", "edit.paste",
                           "selection.all", "selection.deselect", "layer.newNormal", "layer.erase", "layer.fill", "view.zoomIn", "view.fit",
                           "view.resetPosition", "view.flip", "view.close", "canvas.background.white"})
        EXPECT_FALSE(command(id)->isEnabled()) << id;
    for (const char* id : {"file.new", "file.open", "file.quit", "window.fullScreen", "window.showAll", "other.tabletDiagnostics"})
        EXPECT_TRUE(command(id)->isEnabled()) << id;
    EXPECT_FALSE(window->findChild<QAction*>(QStringLiteral("file.recent"))->isEnabled());
}

TEST_F(MenuTest, EntriesAreGreyedWhenTheyCannotApply) {
    openCanvas();
    EXPECT_FALSE(command("edit.undo")->isEnabled());
    EXPECT_FALSE(command("edit.redo")->isEnabled());
    EXPECT_FALSE(command("edit.cut")->isEnabled());
    EXPECT_FALSE(command("edit.copy")->isEnabled());
    EXPECT_FALSE(command("selection.deselect")->isEnabled());
    EXPECT_FALSE(command("selection.invert")->isEnabled());
    EXPECT_FALSE(command("layer.delete")->isEnabled());
    EXPECT_FALSE(command("layer.merge")->isEnabled());
    EXPECT_TRUE(command("selection.all")->isEnabled());
    EXPECT_TRUE(command("selection.show")->isEnabled());
    EXPECT_TRUE(command("layer.erase")->isEnabled());
    EXPECT_TRUE(command("file.save")->isEnabled());

    ASSERT_TRUE(doc()->addLayer());
    EXPECT_TRUE(command("edit.undo")->isEnabled());
    EXPECT_FALSE(command("edit.redo")->isEnabled());
    EXPECT_TRUE(command("layer.delete")->isEnabled());
    EXPECT_TRUE(command("layer.merge")->isEnabled());
    ASSERT_TRUE(doc()->undo());
    EXPECT_TRUE(command("edit.redo")->isEnabled());
    EXPECT_FALSE(command("edit.undo")->isEnabled());
    EXPECT_FALSE(command("layer.delete")->isEnabled());

    command("selection.all")->trigger();
    EXPECT_TRUE(command("edit.copy")->isEnabled());
    EXPECT_TRUE(command("selection.deselect")->isEnabled());
    EXPECT_TRUE(command("selection.invert")->isEnabled());
    EXPECT_TRUE(command("selection.dilate1")->isEnabled());
}

TEST_F(MenuTest, EnabledStatesFollowTheCurrentTab) {
    openCanvas();
    command("selection.all")->trigger();
    window->newDocument();
    EXPECT_FALSE(command("selection.deselect")->isEnabled());
    window->tabs()->setCurrentIndex(0);
    EXPECT_TRUE(command("selection.deselect")->isEnabled());
}

TEST_F(MenuTest, ViewResetsUndoZoomFlipRotationAndScroll) {
    openCanvas();
    CanvasView* c = canvas();
    const auto messy = [&] {
        c->setZoom(2.5, QPointF(100, 100));
        if (!c->isFlipped())
            c->toggleFlip();
        c->setRotation(45.0);
        c->scrollBy(QPointF(30, 20));
    };

    messy();
    command("view.resetPosition")->trigger();
    EXPECT_DOUBLE_EQ(c->zoom(), 1.0);
    EXPECT_DOUBLE_EQ(c->rotation(), 0.0);
    EXPECT_FALSE(c->isFlipped());
    EXPECT_EQ(doc()->view().pan, QPointF());

    messy();
    command("view.resetAngle")->trigger();
    EXPECT_DOUBLE_EQ(c->rotation(), 0.0);
    EXPECT_DOUBLE_EQ(c->zoom(), 2.5);
    EXPECT_TRUE(c->isFlipped());

    messy();
    command("view.scale100")->trigger();
    EXPECT_DOUBLE_EQ(c->zoom(), 1.0);
    EXPECT_TRUE(c->isFlipped());
    EXPECT_NE(c->rotation(), 0.0);

    messy();
    command("view.fit")->trigger();
    EXPECT_LT(c->zoom(), 1.0);
    EXPECT_DOUBLE_EQ(c->rotation(), 0.0);
    EXPECT_FALSE(c->isFlipped());
}

TEST_F(MenuTest, ViewZoomRotateAndFlipEntries) {
    openCanvas();
    CanvasView* c = canvas();
    c->setZoom(1.0, c->viewCentre());
    command("view.zoomIn")->trigger();
    EXPECT_DOUBLE_EQ(c->zoom(), 1.5);  // the reference's levels
    command("view.zoomOut")->trigger();
    command("view.zoomOut")->trigger();
    EXPECT_DOUBLE_EQ(c->zoom(), 0.75);
    command("view.rotateCW")->trigger();
    EXPECT_DOUBLE_EQ(c->rotation(), 15.0);
    command("view.rotateCCW")->trigger();
    command("view.rotateCCW")->trigger();
    EXPECT_DOUBLE_EQ(c->rotation(), -15.0);
    QAction* flip = command("view.flip");
    EXPECT_FALSE(flip->isChecked());
    flip->trigger();
    EXPECT_TRUE(c->isFlipped());
    EXPECT_TRUE(flip->isChecked());
    c->toggleFlip();
    EXPECT_FALSE(flip->isChecked());

    c->setZoom(1.0, c->viewCentre());
    command("view.zoomInAtCursor")->trigger();
    EXPECT_DOUBLE_EQ(c->zoom(), 1.5);
    command("view.zoomOutAtCursor")->trigger();
    EXPECT_DOUBLE_EQ(c->zoom(), 1.0);
}

TEST_F(MenuTest, ViewListHasOneCheckedEntryPerOpenCanvas) {
    openCanvas();
    window->newDocument();
    ASSERT_EQ(window->tabs()->count(), 2);
    QMenu* view = menu(QStringLiteral("View"));
    emit view->aboutToShow();
    QStringList list;
    QAction* current = nullptr;
    for (QAction* a : view->actions())
        if (a->isCheckable() && a->objectName().isEmpty()) {
            list << a->text();
            if (a->isChecked())
                current = a;
        }
    ASSERT_EQ(list.size(), 2);
    EXPECT_TRUE(list[0].startsWith(QStringLiteral("1 ")));
    EXPECT_TRUE(list[1].startsWith(QStringLiteral("2 ")));
    EXPECT_TRUE(list[0].contains(QLatin1Char('%')));
    ASSERT_NE(current, nullptr);
    EXPECT_EQ(current->text(), list[1]);
    EXPECT_EQ(view->actions().back()->text(), list[1]) << "the list ends the menu";
    EXPECT_TRUE(view->actions().at(view->actions().size() - 3)->isSeparator()) << "set apart from the entries above";
    for (QAction* a : view->actions())
        if (a->text() == list[0])
            a->trigger();
    EXPECT_EQ(window->tabs()->currentIndex(), 0);
    emit view->aboutToShow();
    int entries = 0, separators = 0;
    for (const QAction* a : view->actions()) {
        entries += a->isCheckable() && a->objectName().isEmpty();
        separators += a->isSeparator();
    }
    EXPECT_EQ(entries, 2);
    EXPECT_EQ(separators, 4) << "opening the menu again adds nothing";
}

TEST_F(MenuTest, CloseViewAndCloseAllViewsCloseTabs) {
    openCanvas();
    window->newDocument();
    window->newDocument();
    ASSERT_EQ(window->tabs()->count(), 3);
    command("view.close")->trigger();
    EXPECT_EQ(window->tabs()->count(), 2);
    command("file.close")->trigger();
    EXPECT_EQ(window->tabs()->count(), 1);
    window->newDocument();
    command("view.closeAll")->trigger();
    EXPECT_EQ(window->tabs()->count(), 0);
    EXPECT_FALSE(command("view.close")->isEnabled());
}

TEST_F(MenuTest, WindowMenuListsThePanelsDoodleboldHas) {
    const QAction* show = window->findChild<QAction*>(QStringLiteral("window.showPanel"));
    ASSERT_TRUE(show && show->menu());
    EXPECT_EQ(labels(show->menu()),
              (QStringList{"Tool Panel", "Color Panel", "Layer Panel", "Navigator Panel", "---", "Quick Bar", "Status Bar", "View Selector",
                           "---", "Brush Preview", "Color Circle", "HSV/HSL Sliders", "RGB Sliders", "Swatches"}));
    const QAction* separate = window->findChild<QAction*>(QStringLiteral("window.separatePanel"));
    ASSERT_TRUE(separate && separate->menu());
    EXPECT_EQ(labels(separate->menu()), (QStringList{"Tool Panel", "Color Panel", "Layer Panel", "Navigator Panel"}));
    EXPECT_EQ(labels(menu(QStringLiteral("Window"))), (QStringList{"Show Panels", "Separate Panels", "Color and Tool Panels on the Right",
                                                                   "Navigator and Layer Panels on the Right", "---", "Color Sliders",
                                                                   "Swatch Size", "---", "Full Screen", "Show All Panels"}));
    EXPECT_TRUE(command("window.show.colorCircle")->isChecked());
    for (const char* id : {"window.show.rgbSlider", "window.show.hsvSlider", "window.show.swatches"})
        EXPECT_FALSE(command(id)->isChecked()) << id;
    EXPECT_TRUE(command("window.hsv.hsv")->isChecked());
    EXPECT_TRUE(command("window.swatches.large")->isChecked());
    EXPECT_EQ(command("window.fullScreen")->shortcuts(), keysOf("F11, Shift+Tab"));
    EXPECT_EQ(command("window.showAll")->shortcuts(), keysOf("Tab"));
}

TEST_F(MenuTest, StatusBarShowsMemoryAndDriveUsage) {
    auto* meters = window->statusBar()->findChild<UsageMeters*>(QStringLiteral("usageMeters"));
    ASSERT_TRUE(meters);
    // The one label is the animation's frame ("Frame 3 of 8"), empty for a picture.
    const auto texts = window->statusBar()->findChildren<QLabel*>();
    ASSERT_EQ(texts.size(), 1);
    EXPECT_EQ(texts.front()->objectName(), QStringLiteral("frameStatus"));
    EXPECT_TRUE(texts.front()->text().isEmpty());
#ifdef Q_OS_LINUX
    EXPECT_GE(meters->memoryPercent(), 1);
    EXPECT_GE(meters->residentPercent(), meters->memoryPercent());
    EXPECT_TRUE(meters->memoryText().endsWith(QStringLiteral("%)")));
    EXPECT_GE(meters->drivePercent(), 0);
    EXPECT_LE(meters->drivePercent(), 100);
    EXPECT_EQ(meters->driveText(), QString::number(meters->drivePercent()) + QLatin1Char('%'));
#endif
}

TEST_F(MenuTest, PanelLayoutIsKeptBetweenStarts) {
    window->show();
    QCoreApplication::processEvents();
    command("window.show.navigatorPanel")->trigger();
    command("window.colorToolRight")->trigger();
    ASSERT_TRUE(window->close());
    window = std::make_unique<MainWindow>();
    window->show();
    QCoreApplication::processEvents();
    EXPECT_FALSE(command("window.show.navigatorPanel")->isChecked());
    EXPECT_FALSE(window->findChild<QDockWidget*>(QStringLiteral("navigatorDock"))->isVisible());
    EXPECT_TRUE(command("window.colorToolRight")->isChecked());
    EXPECT_EQ(window->dockWidgetArea(window->findChild<QDockWidget*>(QStringLiteral("colorDock"))), Qt::RightDockWidgetArea);
    EXPECT_FALSE(command("window.fullScreen")->isChecked());
}

// The saved layout leaves the quick bar out, wherever it sits: a layout restored with it in the toolbar area while the menu
// bar held it crashed Qt's toolbar layout on Wayland.
TEST_F(MenuTest, TheSavedLayoutLeavesTheQuickBarOut) {
    window->show();
    auto* quick = window->findChild<QToolBar*>(QStringLiteral("quickBar"));
    ASSERT_TRUE(quick);
    QByteArray name;  // as QDataStream writes the tool bar's object name: UTF-16, big-endian
    for (const char c : std::string_view("quickBar"))
        name.append('\0').append(c);
    for (const int width : {2000, 700}) {
        window->resize(width, 900);
        QCoreApplication::processEvents();
        ASSERT_TRUE(window->close());
        const QByteArray state = AppOptions::settings().value(QStringLiteral("layout/state")).toByteArray();
        EXPECT_FALSE(state.isEmpty());
        EXPECT_FALSE(state.contains(name)) << "width " << width;
        window = std::make_unique<MainWindow>();
        window->show();
        QCoreApplication::processEvents();
        quick = window->findChild<QToolBar*>(QStringLiteral("quickBar"));
        EXPECT_TRUE(quick->isVisible()) << "width " << width;
    }
}

TEST_F(MenuTest, TheQuickBarSitsBesideTheMenusWhereItFits) {
    window->show();
    auto* quick = window->findChild<QToolBar*>(QStringLiteral("quickBar"));
    ASSERT_TRUE(quick);
    window->resize(2000, 900);
    QCoreApplication::processEvents();
    EXPECT_EQ(window->menuBar()->cornerWidget(Qt::TopRightCorner), quick);
    EXPECT_TRUE(quick->isVisible());
    window->resize(700, 900);
    QCoreApplication::processEvents();
    EXPECT_EQ(window->menuBar()->cornerWidget(Qt::TopRightCorner), nullptr);
    EXPECT_EQ(window->toolBarArea(quick), Qt::TopToolBarArea);
    EXPECT_TRUE(quick->isVisible());
    command("window.show.quickBar")->trigger();
    window->resize(2000, 900);
    QCoreApplication::processEvents();
    EXPECT_EQ(window->menuBar()->cornerWidget(Qt::TopRightCorner), quick);
    EXPECT_FALSE(quick->isVisible()) << "the user hid it";
}

TEST_F(MenuTest, ShowPanelEntriesHideAndShowThePanels) {
    window->show();
    QCoreApplication::processEvents();
    auto* tools = window->findChild<QDockWidget*>(QStringLiteral("toolsDock"));
    auto* colors = window->findChild<QDockWidget*>(QStringLiteral("colorDock"));
    auto* quick = window->findChild<QToolBar*>(QStringLiteral("quickBar"));
    ASSERT_TRUE(tools && colors && quick);
    EXPECT_TRUE(tools->isVisible());
    command("window.show.toolPanel")->trigger();
    EXPECT_FALSE(tools->isVisible());
    EXPECT_FALSE(command("window.show.toolPanel")->isChecked());
    command("window.show.toolPanel")->trigger();
    EXPECT_TRUE(tools->isVisible());

    command("window.show.quickBar")->trigger();
    EXPECT_FALSE(quick->isVisible());
    command("window.show.statusBar")->trigger();
    EXPECT_FALSE(window->statusBar()->isVisible());
    command("window.show.viewSelector")->trigger();
    EXPECT_FALSE(window->tabs()->viewSelector()->isVisible());

    command("window.showAll")->trigger();
    EXPECT_FALSE(tools->isVisible());
    EXPECT_FALSE(colors->isVisible());
    EXPECT_TRUE(command("window.show.colorPanel")->isChecked());
    command("window.showAll")->trigger();
    EXPECT_TRUE(tools->isVisible());
    EXPECT_TRUE(colors->isVisible());
    EXPECT_FALSE(quick->isVisible());
}

TEST_F(MenuTest, ClosingAPanelWithItsOwnButtonUnchecksItsEntry) {
    window->show();
    QCoreApplication::processEvents();
    auto* layers = window->findChild<QDockWidget*>(QStringLiteral("layersDock"));
    ASSERT_TRUE(layers);
    layers->close();
    EXPECT_FALSE(command("window.show.layerPanel")->isChecked());
    command("window.show.layerPanel")->trigger();
    EXPECT_TRUE(layers->isVisible());
}

TEST_F(MenuTest, SeparateAndRightSideEntriesMoveThePanels) {
    window->show();
    QCoreApplication::processEvents();
    auto* navigator = window->findChild<QDockWidget*>(QStringLiteral("navigatorDock"));
    auto* tools = window->findChild<QDockWidget*>(QStringLiteral("toolsDock"));
    ASSERT_TRUE(navigator && tools);
    EXPECT_FALSE(navigator->isFloating());
    command("window.separate.navigatorPanel")->trigger();
    EXPECT_TRUE(navigator->isFloating());
    command("window.separate.navigatorPanel")->trigger();
    EXPECT_FALSE(navigator->isFloating());
    navigator->setFloating(true);
    EXPECT_TRUE(command("window.separate.navigatorPanel")->isChecked());
    navigator->setFloating(false);

    EXPECT_EQ(window->dockWidgetArea(tools), Qt::LeftDockWidgetArea);
    EXPECT_EQ(window->dockWidgetArea(navigator), Qt::LeftDockWidgetArea);
    command("window.colorToolRight")->trigger();
    EXPECT_EQ(window->dockWidgetArea(tools), Qt::RightDockWidgetArea);
    EXPECT_EQ(window->dockWidgetArea(window->findChild<QDockWidget*>(QStringLiteral("colorDock"))), Qt::RightDockWidgetArea);
    EXPECT_EQ(window->dockWidgetArea(navigator), Qt::LeftDockWidgetArea);
    command("window.navigatorLayerRight")->trigger();
    EXPECT_EQ(window->dockWidgetArea(navigator), Qt::RightDockWidgetArea);
    EXPECT_EQ(window->dockWidgetArea(window->findChild<QDockWidget*>(QStringLiteral("layersDock"))), Qt::RightDockWidgetArea);
    command("window.colorToolRight")->trigger();
    command("window.navigatorLayerRight")->trigger();
    EXPECT_EQ(window->dockWidgetArea(tools), Qt::LeftDockWidgetArea);
    EXPECT_TRUE(tools->isVisible());
}

TEST_F(MenuTest, FullScreenEntryFollowsTheWindowState) {
    window->show();
    QCoreApplication::processEvents();
    QAction* full = command("window.fullScreen");
    EXPECT_FALSE(full->isChecked());
    full->trigger();
    EXPECT_TRUE(window->isFullScreen());
    window->showNormal();
    QCoreApplication::processEvents();
    EXPECT_FALSE(full->isChecked());
}

TEST_F(MenuTest, TheColorBoxShowsTheBrushColour) {
    window->show();
    auto* swatch = window->findChild<QToolButton*>(QStringLiteral("colorSwatch"));
    ASSERT_TRUE(swatch);
    window->tools()->setColor(QColor(10, 200, 30));
    EXPECT_NE(swatch->styleSheet().indexOf(QStringLiteral("#0ac81e")), -1);
}

TEST_F(MenuTest, ScreenshotsOfTheWindowAndItsMenus) {
    openCanvas();
    window->resize(1400, 900);
    command("selection.all")->trigger();
    QCoreApplication::processEvents();
    EXPECT_TRUE(test::saveScreenshot(window->grab(), QStringLiteral("main_window_menu_bar_and_quick_bar")));
    for (QAction* top : window->menuBar()->actions()) {
        QMenu* m = top->menu();
        emit m->aboutToShow();
        m->adjustSize();
        EXPECT_TRUE(test::saveScreenshot(m->grab(), QStringLiteral("menu_") + top->text()));
    }
}

TEST_F(MenuTest, RotateAndTrimCanvasResizeTheOpenCanvas) {
    openCanvas();
    Document* d = window->tabs()->currentDocument();
    ASSERT_TRUE(d);
    const QSize size = d->size();
    auto* rotate = window->findChild<QAction*>(QStringLiteral("canvas.rotateCW"));
    auto* trim = window->findChild<QAction*>(QStringLiteral("canvas.trim"));
    ASSERT_TRUE(rotate && trim);
    EXPECT_FALSE(trim->isEnabled()) << "needs a selection";
    rotate->trigger();
    QCoreApplication::processEvents();
    EXPECT_EQ(d->size(), size.transposed());
    ASSERT_TRUE(d->setSelection(selection::rectangle(d->size(), QRectF(10, 10, 100, 80), false)));
    QCoreApplication::processEvents();
    EXPECT_TRUE(trim->isEnabled());
    trim->trigger();
    QCoreApplication::processEvents();
    EXPECT_EQ(d->size(), QSize(100, 80));
    EXPECT_EQ(d->view().pan, QPointF()) << "the trimmed canvas is centred";
    EXPECT_TRUE(d->undo());
    QCoreApplication::processEvents();
    EXPECT_EQ(d->size(), size.transposed());
}
