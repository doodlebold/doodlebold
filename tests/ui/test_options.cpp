#include "ScreenshotSupport.h"

#include "core/document/Document.h"
#include "core/options/AppOptions.h"
#include "core/tools/ToolBox.h"
#include "core/tools/ToolBoxStore.h"
#include "core/util/CpuCores.h"
#include "ui/CanvasView.h"
#include "ui/DocumentTabs.h"
#include "ui/MainWindow.h"
#include "ui/GlassPopups.h"
#include "ui/OptionsDialog.h"
#include "ui/panels/LayerPanel.h"

#include <QAction>
#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFile>
#include <QLabel>
#include <QListWidget>
#include <QRadioButton>
#include "core/util/AppIdentity.h"
#include <QSettings>
#include <QSpinBox>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QWheelEvent>

#include <algorithm>

#include <gtest/gtest.h>

#include <memory>

using namespace doodlebold;

namespace {

template <typename T> T* find(QObject* parent, const char* name) {
    T* w = parent->findChild<T*>(QString::fromLatin1(name));
    EXPECT_NE(w, nullptr) << name;
    return w;
}

AppOptions::Values everythingChanged() {
    AppOptions::Values v;
    v.minimumPressure = 17;
    v.fullPressure = 64;
    v.frostedMenus = false;
    v.zoomedOut = AppOptions::ZoomedOut::Fast;
    v.undoSteps = 250;
    v.brushOutline = false;
    v.strokeTiming = true;
    v.fitOnResetPosition = true;
    v.swapWheelZoom = true;
    v.wheelZoomAtCentre = true;
    v.recordNewCanvases = false;
    v.quickShapes = false;
    v.quickShapeHoldMs = 1700;
    v.altPicksColour = false;
    v.ctrlMovesLayers = false;
    return v;
}

class OptionsTest : public testing::Test {
protected:
    void SetUp() override {
        QStandardPaths::setTestModeEnabled(true);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDir.path());
        test::applyTheme();
        QFile::remove(ToolBoxStore::defaultPath());
        window = std::make_unique<MainWindow>();
        window->newDocument();  // before show(): the offscreen platform cannot paint a canvas added to a visible window
        window->show();
        QCoreApplication::processEvents();
    }
    void TearDown() override { QFile::remove(ToolBoxStore::defaultPath()); }

    OptionsDialog* openOptions() {
        window->command(QStringLiteral("other.options"))->trigger();
        return window->findChild<OptionsDialog*>();
    }
    CanvasView* canvas() const { return window->tabs()->currentCanvas(); }

    QTemporaryDir settingsDir;
    std::unique_ptr<MainWindow> window;
};

}

TEST_F(OptionsTest, DialogHasPagesAndButtonsForWhatDoodleboldHonours) {
    OptionsDialog dlg(AppOptions::Values{});
    EXPECT_EQ(dlg.windowTitle(), QStringLiteral("Options"));
    auto* pages = find<QListWidget>(&dlg, "optionsPages");
    ASSERT_TRUE(pages);
    QStringList names;
    for (int i = 0; i < pages->count(); ++i)
        names << pages->item(i)->text();
    EXPECT_EQ(names, (QStringList{"Pen Tablet", "Performance", "View", "Drawing", "Keys", "Timelapse"}));
    dlg.addResourcesPage(nullptr);
    EXPECT_EQ(pages->item(5)->text(), QStringLiteral("Resources")) << "before Timelapse";
    EXPECT_EQ(pages->count(), 7);
    EXPECT_EQ(pages->currentRow(), 0);

    auto* buttons = dlg.findChild<QDialogButtonBox*>();
    ASSERT_TRUE(buttons);
    EXPECT_TRUE(buttons->button(QDialogButtonBox::Ok));
    EXPECT_TRUE(buttons->button(QDialogButtonBox::Cancel));

    for (const QRadioButton* r : dlg.findChildren<QRadioButton*>())
        EXPECT_FALSE(r->text().contains(QLatin1String("API"))) << r->text().toStdString();
    EXPECT_EQ(find<QSpinBox>(&dlg, "minimumPressureSpin")->value(), 0);
}

// It had opened at its smallest, about 830 by 400, and couldn't be resized.
TEST_F(OptionsTest, TheDialogOpensRoomyAndResizes) {
    OptionsDialog dlg(AppOptions::Values{});
    dlg.show();
    EXPECT_GE(dlg.width(), 900);
    EXPECT_GE(dlg.height(), 600);
    EXPECT_NE(dlg.minimumSize(), dlg.maximumSize());
    dlg.resize(1100, 760);
    EXPECT_EQ(dlg.size(), QSize(1100, 760));
}

TEST_F(OptionsTest, RecordNewCanvasesIsOffByDefaultAndLivesInTheTimelapsePage) {
    OptionsDialog dlg(AppOptions::Values{});
    auto* box = find<QCheckBox>(&dlg, "recordNewCanvasesCheck");
    ASSERT_TRUE(box);
    EXPECT_EQ(box->text(), QStringLiteral("Record new canvases"));
    EXPECT_FALSE(box->isChecked());
    EXPECT_FALSE(dlg.values().recordNewCanvases);
    box->setChecked(true);
    EXPECT_TRUE(dlg.values().recordNewCanvases);
}

TEST_F(OptionsTest, TheTimelapsePageHoldsOnlyRecordNewCanvases) {
    OptionsDialog dlg(AppOptions::Values{});
    auto* pages = find<QListWidget>(&dlg, "optionsPages");
    ASSERT_TRUE(pages);
    const QList<QListWidgetItem*> timelapse = pages->findItems(QStringLiteral("Timelapse"), Qt::MatchExactly);
    ASSERT_EQ(timelapse.size(), 1);
    EXPECT_EQ(pages->row(timelapse[0]), pages->count() - 1) << "the last page";
    pages->setCurrentItem(timelapse[0]);
    auto* stack = dlg.findChild<QStackedWidget*>();
    ASSERT_TRUE(stack);
    QWidget* page = stack->currentWidget();
    ASSERT_TRUE(page);
    const QList<QCheckBox*> boxes = page->findChildren<QCheckBox*>();
    ASSERT_EQ(boxes.size(), 1);
    EXPECT_EQ(boxes[0]->objectName(), QStringLiteral("recordNewCanvasesCheck"));
    EXPECT_TRUE(page->findChildren<QSpinBox*>().isEmpty());
    EXPECT_TRUE(page->findChildren<QComboBox*>().isEmpty());
    QStringList texts;
    for (const QLabel* l : page->findChildren<QLabel*>())
        texts << l->text();
    const QString all = texts.join(QLatin1Char('\n'));
    EXPECT_TRUE(all.contains(QStringLiteral("Every new canvas, and every picture opened without a recording")));
    EXPECT_FALSE(all.contains(QStringLiteral("Export Timelapse starts with")));
    EXPECT_FALSE(all.contains(QStringLiteral("Frame rate")));
    for (const char* name : {"timelapseLengthCombo", "timelapseSecondsSpin", "timelapseFpsSpin", "timelapseHoldSpin"})
        EXPECT_EQ(dlg.findChild<QWidget*>(QString::fromLatin1(name)), nullptr) << name;
    dlg.resize(960, 640);
    EXPECT_TRUE(test::saveScreenshot(dlg.grab(), QStringLiteral("options_timelapse")));
}

TEST_F(OptionsTest, OnlyRecordNewCanvasesIsSavedOfTheTimelapseOptions) {
    AppOptions::Values v;
    v.recordNewCanvases = true;
    AppOptions options;
    options.setValues(v);
    QSettings saved = AppOptions::settings();
    for (const char* key : {"options/timelapseLength", "options/timelapseSeconds", "options/timelapseFps", "options/timelapseHoldSeconds"})
        EXPECT_FALSE(saved.contains(QString::fromLatin1(key))) << key;
    EXPECT_TRUE(saved.contains(QStringLiteral("options/recordNewCanvases")));
    EXPECT_TRUE(AppOptions().values().recordNewCanvases) << "Record new canvases is kept";
}

TEST_F(OptionsTest, DialogTextsOfTheKeptOptionsAreTheExpectedOnes) {
    OptionsDialog dlg(AppOptions::Values{});
    QStringList texts;
    for (const QLabel* l : dlg.findChildren<QLabel*>())
        texts << l->text();
    for (const QCheckBox* c : dlg.findChildren<QCheckBox*>())
        texts << c->text();
    for (const QRadioButton* r : dlg.findChildren<QRadioButton*>())
        texts << r->text();
    const QString all = texts.join(QLatin1Char('\n'));
    for (const char* text :
         {"Pen Pressure", "Ignore pressure below:", "Reverse the mouse wheel's zoom direction",
          "Mouse wheel always zooms at the center of the view", "Fit the canvas to the window when resetting the view", "Frosted menus",
          "Display when zoomed out:", "Undo history:", "Brush outline under the pen", "Show stroke timing"})
        EXPECT_TRUE(all.contains(QString::fromLatin1(text))) << text;
    for (const char* typo : {"Methoc", "Doudge", "cloclwise", "potision"})
        EXPECT_FALSE(all.contains(QString::fromLatin1(typo))) << typo;
}

TEST_F(OptionsTest, ValuesFollowTheControls) {
    OptionsDialog dlg(everythingChanged());
    const AppOptions::Values v = dlg.values();
    EXPECT_EQ(v, AppOptions::clamped(everythingChanged()));
    EXPECT_EQ(v.minimumPressure, 17);
    EXPECT_EQ(v.fullPressure, 64);
    EXPECT_FALSE(v.frostedMenus);
    EXPECT_EQ(v.zoomedOut, AppOptions::ZoomedOut::Fast);
    EXPECT_EQ(v.undoSteps, 250);
    EXPECT_FALSE(v.brushOutline);
    EXPECT_TRUE(v.strokeTiming);
    EXPECT_TRUE(v.fitOnResetPosition);
    EXPECT_TRUE(v.swapWheelZoom);
    EXPECT_TRUE(v.wheelZoomAtCentre);
    EXPECT_FALSE(v.recordNewCanvases);
    EXPECT_FALSE(v.quickShapes);
    EXPECT_EQ(v.quickShapeHoldMs, 1700);
    EXPECT_FALSE(v.altPicksColour);
    EXPECT_FALSE(v.ctrlMovesLayers);

    find<QComboBox>(&dlg, "zoomedOutCombo")->setCurrentIndex(0);
    EXPECT_EQ(dlg.values().zoomedOut, AppOptions::ZoomedOut::Smooth);
    find<QSpinBox>(&dlg, "undoStepsSpin")->setValue(5);
    EXPECT_EQ(dlg.values().undoSteps, AppOptions::kLeastUndoSteps);

    find<QSpinBox>(&dlg, "minimumPressureSpin")->setValue(100);
    EXPECT_EQ(dlg.values().minimumPressure, 100);
    EXPECT_EQ(find<QSpinBox>(&dlg, "minimumPressureSpin")->maximum(), 100);
}

TEST_F(OptionsTest, OkAppliesAndSavesAndCancelDoesNot) {
    OptionsDialog* dlg = openOptions();
    ASSERT_TRUE(dlg);
    find<QSpinBox>(dlg, "minimumPressureSpin")->setValue(40);
    dlg->reject();
    QCoreApplication::processEvents();
    EXPECT_EQ(window->options()->values().minimumPressure, 0);
    EXPECT_EQ(window->tools()->options().minimumPressure, 0.0);

    dlg = openOptions();
    ASSERT_TRUE(dlg);
    EXPECT_EQ(find<QSpinBox>(dlg, "minimumPressureSpin")->value(), 0);
    find<QSpinBox>(dlg, "minimumPressureSpin")->setValue(40);
    find<QCheckBox>(dlg, "swapWheelCheck")->setChecked(true);
    dlg->accept();
    QCoreApplication::processEvents();
    EXPECT_EQ(window->options()->values().minimumPressure, 40);
    EXPECT_TRUE(window->options()->values().swapWheelZoom);
    EXPECT_DOUBLE_EQ(window->tools()->options().minimumPressure, 0.40);

    const AppOptions reread;
    EXPECT_EQ(reread.values().minimumPressure, 40);
    EXPECT_TRUE(reread.values().swapWheelZoom);
    window.reset();
    window = std::make_unique<MainWindow>();
    EXPECT_EQ(window->options()->values().minimumPressure, 40);
    EXPECT_DOUBLE_EQ(window->tools()->options().minimumPressure, 0.40);
}

TEST_F(OptionsTest, OptionsAreKeptInQSettingsAndClamped) {
    AppOptions options;
    EXPECT_EQ(options.values(), AppOptions::clamped(AppOptions::Values{}));
    EXPECT_EQ(options.values().minimumPressure, 0);
    EXPECT_TRUE(options.values().frostedMenus);
    EXPECT_EQ(options.values().zoomedOut, AppOptions::ZoomedOut::ByGraphics) << "until the user picks";
    EXPECT_EQ(options.values().undoSteps, 100) << "as fixed before";
    EXPECT_TRUE(options.values().brushOutline);
    EXPECT_FALSE(options.values().strokeTiming);
    EXPECT_FALSE(options.values().fitOnResetPosition);
    EXPECT_FALSE(options.values().swapWheelZoom);
    EXPECT_FALSE(options.values().wheelZoomAtCentre);
    EXPECT_FALSE(options.values().recordNewCanvases) << "timelapse recording is off unless the user turns it on";

    int emitted = 0;
    QObject::connect(&options, &AppOptions::changed, [&] { ++emitted; });
    AppOptions::Values v = everythingChanged();
    v.minimumPressure = 250;
    v.undoSteps = 99999;
    options.setValues(v);
    EXPECT_EQ(emitted, 1);
    EXPECT_EQ(options.values().minimumPressure, 100);
    EXPECT_EQ(options.values().undoSteps, AppOptions::kMostUndoSteps);
    options.setValues(options.values());
    EXPECT_EQ(emitted, 1);

    const AppOptions reread;
    EXPECT_EQ(reread.values(), options.values());
    EXPECT_TRUE(app_identity::settings().contains(QStringLiteral("options/minimumPressure")));
}

TEST_F(OptionsTest, MinimumPressureReachesTheBrushOptions) {
    AppOptions::Values v = window->options()->values();
    v.minimumPressure = 30;
    window->options()->setValues(v);
    EXPECT_DOUBLE_EQ(window->tools()->options().minimumPressure, 0.30);
    v.minimumPressure = 0;
    window->options()->setValues(v);
    EXPECT_DOUBLE_EQ(window->tools()->options().minimumPressure, 0.0);
}

TEST_F(OptionsTest, WheelZoomFollowsTheViewOptions) {
    CanvasView* c = canvas();
    c->setZoom(1.0, c->viewCentre());
    const QPointF away = c->viewCentre() + QPointF(200, 120);
    const auto wheel = [&](int delta) {
        QWheelEvent e(away, c->mapToGlobal(away), QPoint(), QPoint(0, delta), Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
        QCoreApplication::sendEvent(c, &e);
    };

    wheel(120);
    EXPECT_GT(c->zoom(), 1.0);
    EXPECT_NE(window->tabs()->currentDocument()->view().pan, QPointF());
    c->setZoom(1.0, c->viewCentre());
    c->resetPosition(false);

    AppOptions::Values v = window->options()->values();
    v.swapWheelZoom = true;
    window->options()->setValues(v);
    wheel(120);
    EXPECT_LT(c->zoom(), 1.0);

    c->resetPosition(false);
    v.swapWheelZoom = false;
    v.wheelZoomAtCentre = true;
    window->options()->setValues(v);
    wheel(120);
    EXPECT_GT(c->zoom(), 1.0);
    EXPECT_LT(std::abs(window->tabs()->currentDocument()->view().pan.x()), 1e-6);
    EXPECT_LT(std::abs(window->tabs()->currentDocument()->view().pan.y()), 1e-6);

    window->newDocument();
    CanvasView* second = canvas();
    ASSERT_NE(second, c);
    second->setZoom(1.0, second->viewCentre());
    QWheelEvent e(away, second->mapToGlobal(away), QPoint(), QPoint(0, 120), Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
    QCoreApplication::sendEvent(second, &e);
    EXPECT_LT(std::abs(window->tabs()->currentDocument()->view().pan.x()), 1e-6);
}

TEST_F(OptionsTest, ResetPositionOfViewFitsTheWindowOnlyWhenAsked) {
    CanvasView* c = canvas();
    c->setZoom(3.0, c->viewCentre());
    window->command(QStringLiteral("view.resetPosition"))->trigger();
    EXPECT_DOUBLE_EQ(c->zoom(), 1.0);

    AppOptions::Values v = window->options()->values();
    v.fitOnResetPosition = true;
    window->options()->setValues(v);
    c->setZoom(3.0, c->viewCentre());
    c->setRotation(45.0);
    window->command(QStringLiteral("view.resetPosition"))->trigger();
    EXPECT_LT(c->zoom(), 1.0);
    EXPECT_DOUBLE_EQ(c->rotation(), 0.0);
}

TEST_F(OptionsTest, TheEightSpecialBlendModesAreAlwaysInTheModeComboOnce) {
    auto* combo = window->findChild<QComboBox*>(QStringLiteral("blendModeCombo"));
    ASSERT_TRUE(combo);
    EXPECT_GE(combo->findData(static_cast<int>(BlendMode::Deepen)), 0);
    for (const BlendMode mode : {BlendMode::LinearBurn, BlendMode::LinearDodge, BlendMode::LinearLight, BlendMode::ColorBurn,
                                 BlendMode::ColorDodge, BlendMode::VividLight, BlendMode::HardMixShape, BlendMode::DifferenceShape}) {
        int listed = 0;
        for (int i = 0; i < combo->count(); ++i)
            listed += combo->itemData(i).toInt() == static_cast<int>(mode) ? 1 : 0;
        EXPECT_EQ(listed, 1) << int(mode);
    }
}

TEST_F(OptionsTest, TheOptionsDialogHasNoSettingForTheSpecialBlendModes) {
    OptionsDialog dlg(AppOptions::Values{}, window.get());
    EXPECT_EQ(dlg.findChild<QCheckBox*>(QStringLiteral("specialBlendModesCheck")), nullptr);
}

// The words: "Lets remove the cores option, and add your settings instead". Parallel work uses every core.
TEST_F(OptionsTest, TheCoresOptionIsGoneWithItsSavedValue) {
    {
        QSettings s = AppOptions::settings();
        s.setValue(QStringLiteral("options/limitCpuCores"), true);
        s.setValue(QStringLiteral("options/cpuCores"), 1);
    }
    const AppOptions options;
    const QSettings saved = AppOptions::settings();
    EXPECT_FALSE(saved.contains(QStringLiteral("options/limitCpuCores")));
    EXPECT_FALSE(saved.contains(QStringLiteral("options/cpuCores")));
    EXPECT_EQ(cpu_cores::used(), cpu_cores::available());
    OptionsDialog dlg(AppOptions::Values{});
    for (const char* name : {"allCoresRadio", "fewerCoresRadio", "coresSpin"})
        EXPECT_EQ(dlg.findChild<QWidget*>(QString::fromLatin1(name)), nullptr) << name;
    for (const QLabel* l : dlg.findChildren<QLabel*>())
        EXPECT_FALSE(l->text().contains(QLatin1String("cores"), Qt::CaseInsensitive)) << l->text().toStdString();
}

TEST_F(OptionsTest, ThePerformancePageHoldsTheNewSettingsAndTheyReachTheCanvasAtOnce) {
    OptionsDialog* dlg = openOptions();
    ASSERT_TRUE(dlg);
    auto* pages = find<QListWidget>(dlg, "optionsPages");
    pages->setCurrentRow(1);
    EXPECT_EQ(pages->currentItem()->text(), QStringLiteral("Performance"));
    QWidget* page = dlg->findChild<QStackedWidget*>()->currentWidget();
    for (const char* name : {"frostedMenusCheck", "zoomedOutCombo", "undoStepsSpin", "brushOutlineCheck", "strokeTimingCheck"})
        EXPECT_NE(page->findChild<QWidget*>(QString::fromLatin1(name)), nullptr) << name;
    EXPECT_EQ(find<QComboBox>(dlg, "zoomedOutCombo")->itemText(0), QStringLiteral("Smooth"));
    EXPECT_EQ(find<QComboBox>(dlg, "zoomedOutCombo")->itemText(1), QStringLiteral("Fast"));
    // The sections top to bottom: Display, then Memory, then Undo and Stroke Timing.
    QStringList titles;
    for (QLabel* title : page->findChildren<QLabel*>())
        if (title->property("role").toString() == QLatin1String("sectionTitle"))
            titles << title->text();
    std::ranges::sort(titles, [page](const QString& a, const QString& b) {
        const auto top = [page](const QString& t) {
            for (QLabel* l : page->findChildren<QLabel*>())
                if (l->text() == t)
                    return l->mapTo(page, QPoint(0, 0)).y();
            return 0;
        };
        return top(a) < top(b);
    });
    EXPECT_EQ(titles, (QStringList{"Display", "Memory", "Undo", "Stroke Timing"}));
    dlg->resize(960, 640);
    EXPECT_TRUE(test::saveScreenshot(dlg->grab(), QStringLiteral("options_performance")));

    ASSERT_TRUE(canvas());
    const bool graphicsChoseFast = CanvasView::startsFast(CanvasView::softwareGraphics().value_or(false), canvas()->document()->size());
    EXPECT_EQ(canvas()->fastZoomedOut(), graphicsChoseFast) << "by the graphics";
    EXPECT_TRUE(canvas()->brushOutline());
    EXPECT_FALSE(canvas()->strokeTiming());
    find<QCheckBox>(dlg, "frostedMenusCheck")->setChecked(false);
    find<QComboBox>(dlg, "zoomedOutCombo")->setCurrentIndex(1);
    find<QSpinBox>(dlg, "undoStepsSpin")->setValue(30);
    find<QCheckBox>(dlg, "brushOutlineCheck")->setChecked(false);
    find<QCheckBox>(dlg, "strokeTimingCheck")->setChecked(true);
    dlg->accept();
    QCoreApplication::processEvents();
    EXPECT_TRUE(canvas()->fastZoomedOut());
    EXPECT_FALSE(canvas()->brushOutline());
    EXPECT_TRUE(canvas()->strokeTiming());
    EXPECT_FALSE(GlassPopups::frosted());
    EXPECT_EQ(Document::undoSteps(), 30);
    window->newDocument();
    EXPECT_EQ(window->tabs()->currentCanvas()->document()->undoStack()->undoLimit(), 30) << "a new canvas keeps 30 steps";

    const AppOptions reread;
    EXPECT_FALSE(reread.values().frostedMenus);
    // Where the graphics chose Fast already, it is left to them (ZoomedOutIsLeftToTheGraphicsUntilTheUserPicks).
    EXPECT_EQ(reread.values().zoomedOut, graphicsChoseFast ? AppOptions::ZoomedOut::ByGraphics : AppOptions::ZoomedOut::Fast);
    EXPECT_EQ(reread.values().undoSteps, 30);
    EXPECT_FALSE(reread.values().brushOutline);
    EXPECT_TRUE(reread.values().strokeTiming);
    window->options()->setValues(AppOptions::Values{});
    EXPECT_TRUE(GlassPopups::frosted());
    EXPECT_EQ(Document::undoSteps(), Document::kUndoLimit);
}

// The decision: with software graphics only, a large canvas starts Fast zoomed out and a smaller one Smooth; a
// graphics card always starts Smooth; a pick of the user's stays.
TEST_F(OptionsTest, ZoomedOutIsLeftToTheGraphicsUntilTheUserPicks) {
    for (const char* renderer : {"llvmpipe (LLVM 22.1.8, 256 bits)", "softpipe", "zink Vulkan 1.3(llvmpipe (LLVM 17.0.6, 256 bits))",
                                 "Google SwiftShader", "Apple Software Renderer", "Microsoft Basic Render Driver", "GDI Generic"})
        EXPECT_TRUE(CanvasView::isSoftwareRenderer(QString::fromLatin1(renderer))) << renderer;
    for (const char* renderer : {"NVIDIA GeForce RTX 4070/PCIe/SSE2", "AMD Radeon RX 7600 (radeonsi, navi33, LLVM 17.0.6, DRM 3.57)",
                                 "Mesa Intel(R) UHD Graphics 620 (KBL GT2)", "Apple M2"})
        EXPECT_FALSE(CanvasView::isSoftwareRenderer(QString::fromLatin1(renderer))) << renderer;

    // By the canvas's pixels, so a long thin canvas counts as its area does.
    const qint64 from = CanvasView::kSoftwareFastFromPixels;
    EXPECT_FALSE(CanvasView::startsFast(true, QSize(3000, 3000)));
    EXPECT_TRUE(CanvasView::startsFast(true, QSize(16384, 16384)));
    EXPECT_TRUE(CanvasView::startsFast(true, QSize(int(from / 1000), 1000)));
    EXPECT_FALSE(CanvasView::startsFast(true, QSize(int(from / 1000) - 1, 1000)));
    EXPECT_FALSE(CanvasView::startsFast(false, QSize(16384, 16384))) << "a graphics card";

    ASSERT_TRUE(canvas());
    if (!CanvasView::softwareGraphics())
        GTEST_SKIP() << "no OpenGL on this platform";
    const bool chose = CanvasView::startsFast(*CanvasView::softwareGraphics(), canvas()->document()->size());
    EXPECT_EQ(window->options()->values().zoomedOut, AppOptions::ZoomedOut::ByGraphics);
    EXPECT_EQ(canvas()->fastZoomedOut(), chose);

    // Shown as what the graphics chose for this canvas, and left to them unless changed.
    OptionsDialog* dlg = openOptions();
    ASSERT_TRUE(dlg);
    EXPECT_EQ(find<QComboBox>(dlg, "zoomedOutCombo")->currentIndex(), chose ? 1 : 0);
    find<QSpinBox>(dlg, "undoStepsSpin")->setValue(40);
    dlg->accept();
    QCoreApplication::processEvents();
    EXPECT_EQ(window->options()->values().zoomedOut, AppOptions::ZoomedOut::ByGraphics);
    EXPECT_FALSE(AppOptions::settings().contains(QStringLiteral("options/fastZoomedOut"))) << "nothing saved for it";

    // Picking the other one keeps it, on any graphics.
    dlg = openOptions();
    ASSERT_TRUE(dlg);
    find<QComboBox>(dlg, "zoomedOutCombo")->setCurrentIndex(chose ? 0 : 1);
    dlg->accept();
    QCoreApplication::processEvents();
    const AppOptions::ZoomedOut picked = chose ? AppOptions::ZoomedOut::Smooth : AppOptions::ZoomedOut::Fast;
    EXPECT_EQ(window->options()->values().zoomedOut, picked);
    EXPECT_EQ(canvas()->fastZoomedOut(), !chose);
    EXPECT_EQ(AppOptions().values().zoomedOut, picked);
    // And picking the graphics' own choice afterwards is a pick too.
    dlg = openOptions();
    ASSERT_TRUE(dlg);
    find<QComboBox>(dlg, "zoomedOutCombo")->setCurrentIndex(chose ? 1 : 0);
    dlg->accept();
    QCoreApplication::processEvents();
    EXPECT_EQ(AppOptions().values().zoomedOut, chose ? AppOptions::ZoomedOut::Fast : AppOptions::ZoomedOut::Smooth);
    window->options()->setValues(AppOptions::Values{});
    EXPECT_EQ(AppOptions().values().zoomedOut, AppOptions::ZoomedOut::ByGraphics);
    EXPECT_EQ(canvas()->fastZoomedOut(), chose) << "back to the graphics' choice";
}

TEST_F(OptionsTest, QuickShapesAndHeldKeysAreOnByDefaultAndStayAsSet) {
    AppOptions options;
    EXPECT_TRUE(options.values().quickShapes);
    EXPECT_EQ(options.values().quickShapeHoldMs, 1000);
    EXPECT_TRUE(options.values().altPicksColour);
    EXPECT_TRUE(options.values().ctrlMovesLayers);

    AppOptions::Values v = options.values();
    v.quickShapes = false;
    v.quickShapeHoldMs = 100000;
    v.altPicksColour = false;
    options.setValues(v);
    AppOptions again;
    EXPECT_FALSE(again.values().quickShapes);
    EXPECT_EQ(again.values().quickShapeHoldMs, 3000) << "the longest hold";
    EXPECT_FALSE(again.values().altPicksColour);
    EXPECT_TRUE(again.values().ctrlMovesLayers);
}

TEST_F(OptionsTest, TheHoldTimeIsOffWithQuickShapesAndTheOptionsReachTheCanvas) {
    OptionsDialog dlg(AppOptions::Values{}, window.get());
    auto* quick = dlg.findChild<QCheckBox*>(QStringLiteral("quickShapesCheck"));
    auto* hold = dlg.findChild<QDoubleSpinBox*>(QStringLiteral("quickShapeHoldSpin"));
    ASSERT_TRUE(quick && hold);
    EXPECT_DOUBLE_EQ(hold->value(), 1.0);
    EXPECT_TRUE(hold->isEnabled());
    quick->setChecked(false);
    EXPECT_FALSE(hold->isEnabled());
    EXPECT_FALSE(dlg.values().quickShapes);

    AppOptions::Values v = window->options()->values();
    v.ctrlMovesLayers = false;
    window->options()->setValues(v);
    CanvasView* canvas = window->tabs()->currentCanvas();
    // Against the cursor shown just before, not a fixed shape, which was unsteady on the Linux runner.
    const Qt::CursorShape before = canvas->cursor().shape();
    QKeyEvent ctrl(QEvent::KeyPress, Qt::Key_Control, Qt::NoModifier);
    QCoreApplication::sendEvent(canvas, &ctrl);
    EXPECT_EQ(canvas->cursor().shape(), before) << "Ctrl is off";
    QKeyEvent up(QEvent::KeyRelease, Qt::Key_Control, Qt::ControlModifier);
    QCoreApplication::sendEvent(canvas, &up);
}
