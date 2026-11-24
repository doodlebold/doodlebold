// A stroke too large for the memory (the decisions of 2026-11-08): the notice shows once ever, after the stroke; the
// limit is silent after it; Options › Performance › Memory sets the amount and whether brushes are kept within it; the Size control
// shows the limit. Also renders the notice, the Size control and the Memory section for review (DOODLEBOLD_SCREENSHOT_DIR).
#include "ScreenshotSupport.h"

#include "core/document/Document.h"
#include "core/options/AppOptions.h"
#include "core/paint/StrokeMemory.h"
#include "core/tools/ToolBox.h"
#include "core/tools/ToolBoxStore.h"
#include "ui/MainWindow.h"
#include "ui/MemoryNotice.h"
#include "ui/OptionsDialog.h"
#include "ui/ValueBar.h"
#include "ui/panels/ToolPanel.h"
#include "ui/panels/ToolParameters.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFile>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QSettings>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QTemporaryDir>

#include <gtest/gtest.h>

#include <cmath>
#include <memory>

using namespace doodlebold;

namespace {

constexpr std::int64_t kGiB = std::int64_t(1) << 30;

class MemoryNoticeTest : public testing::Test {
protected:
    void SetUp() override {
        QStandardPaths::setTestModeEnabled(true);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDir.path());
        test::applyTheme();
        MemoryNotice::setShown(false);
        notice.watch(&doc);
        QObject::connect(&notice, &MemoryNotice::noticeWanted, &notice, [this](const MemoryNotice::Event& e) { notices.push_back(e); });
        QObject::connect(&notice, &MemoryNotice::noteWanted, &notice, [this](const QString& text) { notes << text; });
    }
    void TearDown() override {
        stroke_memory::setPolicy({});
        stroke_memory::usableOverride() = 0;
    }

    // A stroke as the document hears of one: it paints, it's limited or stops, and it ends.
    void stroke(bool stopped) {
        doc.setPainting(true);
        if (stopped)
            emit doc.strokeStopped(40000);
        else
            emit doc.strokeLimited(40000, 21040);
        doc.setPainting(false);
        QCoreApplication::processEvents();
    }

    QTemporaryDir settingsDir;
    ToolBox tools;
    Document doc{QSize(400, 300), Qt::white};
    MemoryNotice notice{&tools};
    std::vector<MemoryNotice::Event> notices;
    QStringList notes;
};

MemoryNotice::Event blurEvent(bool stopped) {
    MemoryNotice::Event e;
    e.stopped = stopped;
    e.chosen = 40000;
    e.used = stopped ? 40000 : 21040;
    e.tool = ToolType::Blur;
    e.largestThatFits = 21040;
    return e;
}

}

TEST_F(MemoryNoticeTest, TheNoticeShowsOnceAfterTheStrokeAndNeverAgain) {
    doc.setPainting(true);
    emit doc.strokeLimited(40000, 21040);
    QCoreApplication::processEvents();
    EXPECT_TRUE(notices.empty()) << "never mid-stroke";
    emit doc.strokeStopped(21040);
    doc.setPainting(false);
    QCoreApplication::processEvents();
    ASSERT_EQ(notices.size(), 1u);
    EXPECT_FALSE(notices.front().stopped) << "whichever came first";
    EXPECT_EQ(notices.front().chosen, 40000);
    EXPECT_EQ(notices.front().used, 21040);
    EXPECT_TRUE(MemoryNotice::wasShown()) << "kept";

    stroke(false);
    stroke(true);
    MemoryNotice again(&tools);  // as after the program starts again
    again.watch(&doc);
    int later = 0;
    QObject::connect(&again, &MemoryNotice::noticeWanted, &again, [&later] { ++later; });
    stroke(true);
    EXPECT_EQ(notices.size(), 1u);
    EXPECT_EQ(later, 0);
    EXPECT_TRUE(notes.isEmpty()) << "after the notice, the limit is silent";
}

TEST_F(MemoryNoticeTest, AStrokeThatStoppedFirstIsWhatTheNoticeTells) {
    stroke(true);
    ASSERT_EQ(notices.size(), 1u);
    EXPECT_TRUE(notices.front().stopped);
}

TEST_F(MemoryNoticeTest, WithBrushesNotKeptWithinAStoppedStrokeGetsANoteEachTimeAndNoNotice) {
    stroke_memory::setPolicy({0, false});
    stroke(true);
    stroke(true);
    EXPECT_TRUE(notices.empty());
    EXPECT_EQ(notes.size(), 2);
    EXPECT_EQ(notes.front(), MemoryNotice::stoppedNote());
    EXPECT_FALSE(MemoryNotice::wasShown());
}

TEST_F(MemoryNoticeTest, TheNoticeSaysWhatHappenedAndWhereToTurnItOff) {
    const QString stopped = MemoryNotice::body(blurEvent(true));
    EXPECT_TRUE(stopped.startsWith(QStringLiteral("This stroke stopped partway: at 4000 px it needs more memory than there is. "
                                                  "What you painted is kept.")));
    EXPECT_TRUE(stopped.contains(QStringLiteral("On this canvas the Blur goes up to 2104 px.")));
    EXPECT_TRUE(stopped.contains(QStringLiteral("Options › Performance › Memory")));
    const QString limited = MemoryNotice::body(blurEvent(false));
    EXPECT_TRUE(limited.startsWith(QStringLiteral("The brush was kept to 2104 px instead of 4000 px")));
    EXPECT_EQ(limited.section(QStringLiteral("\n\n"), 1), stopped.section(QStringLiteral("\n\n"), 1)) << "the rest is the same";
    EXPECT_EQ(MemoryNotice::title(), QStringLiteral("Brush too large for this computer's memory"));

    for (const bool wasStopped : {true, false}) {
        std::unique_ptr<QMessageBox> box(MemoryNotice::makeBox(blurEvent(wasStopped), nullptr));
        auto* options = box->findChild<QPushButton*>(QStringLiteral("memoryNoticeOptions"));
        ASSERT_TRUE(options);
        EXPECT_EQ(options->text(), QStringLiteral("Open Performance options"));
        ASSERT_TRUE(box->defaultButton());
        EXPECT_EQ(box->defaultButton(), box->button(QMessageBox::Ok));
        box->show();
        QCoreApplication::processEvents();
        EXPECT_TRUE(test::saveScreenshot(box->grab(),
                                         wasStopped ? QStringLiteral("memory-notice-stopped") : QStringLiteral("memory-notice-limited")));
    }
}

TEST_F(MemoryNoticeTest, TheSizeControlShowsTheLimitOnlyAboveIt) {
    ToolPanel panel(&tools);
    panel.resize(320, 760);
    panel.show();
    ASSERT_TRUE(tools.selectToolOfType(ToolType::Blur));
    BrushSettings s = tools.settings();
    s.size = 40000;
    tools.setSettings(s);
    stroke_memory::usableOverride() = stroke_memory::heldBytes() + 2 * kGiB;
    panel.parameters()->setCanvasSize(QSize(16384, 16384));
    auto* bar = panel.findChild<ValueBar*>(QStringLiteral("sizeBar"));
    ASSERT_TRUE(bar);
    EXPECT_TRUE(bar->showsLimit());
    QCoreApplication::processEvents();
    // As on an 8 GB machine with a 16384 px canvas open, for the picture.
    stroke_memory::usableOverride() = stroke_memory::heldBytes() + std::int64_t(5.5 * double(kGiB));
    panel.parameters()->refreshSizeLimit();
    EXPECT_TRUE(bar->showsLimit());
    EXPECT_TRUE(test::saveScreenshot(bar->parentWidget()->grab(), QStringLiteral("memory-size-control")));
    stroke_memory::usableOverride() = stroke_memory::heldBytes() + 2 * kGiB;

    s.size = 500;
    tools.setSettings(s);
    EXPECT_FALSE(bar->showsLimit()) << "a size that fits shows alone";
    s.size = 40000;
    tools.setSettings(s);
    EXPECT_TRUE(bar->showsLimit());
    stroke_memory::setPolicy({0, false});
    panel.parameters()->refreshSizeLimit();
    EXPECT_FALSE(bar->showsLimit()) << "brushes not kept within memory";
    stroke_memory::setPolicy({});
    panel.parameters()->refreshSizeLimit();
    EXPECT_TRUE(bar->showsLimit());
    stroke_memory::usableOverride() = stroke_memory::heldBytes() + 4 * kGiB;
    panel.parameters()->setCanvasSize(QSize(400, 300));
    EXPECT_FALSE(bar->showsLimit()) << "on a small canvas it fits";
}

class MemoryOptionsTest : public testing::Test {
protected:
    void SetUp() override {
        QStandardPaths::setTestModeEnabled(true);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDir.path());
        test::applyTheme();
        QFile::remove(ToolBoxStore::defaultPath());
    }
    void TearDown() override {
        QFile::remove(ToolBoxStore::defaultPath());
        stroke_memory::setPolicy({});
    }
    QTemporaryDir settingsDir;
};

TEST_F(MemoryOptionsTest, TheMemorySectionComesLastOnThePerformancePage) {
    OptionsDialog dlg(AppOptions::Values{});
    dlg.showMemorySection();
    dlg.resize(960, 640);
    dlg.show();
    QCoreApplication::processEvents();
    auto* pages = dlg.findChild<QListWidget*>(QStringLiteral("optionsPages"));
    EXPECT_EQ(pages->currentItem()->text(), QStringLiteral("Performance"));
    auto* combo = dlg.findChild<QComboBox*>(QStringLiteral("memoryCombo"));
    auto* spin = dlg.findChild<QDoubleSpinBox*>(QStringLiteral("memorySpin"));
    auto* keep = dlg.findChild<QCheckBox*>(QStringLiteral("keepBrushesInMemoryCheck"));
    auto* inUse = dlg.findChild<QLabel*>(QStringLiteral("memoryInUseLabel"));
    ASSERT_TRUE(combo && spin && keep && inUse);
    EXPECT_EQ(combo->itemText(0), QStringLiteral("Automatic"));
    EXPECT_EQ(combo->itemText(1), QStringLiteral("Custom"));
    EXPECT_EQ(combo->currentIndex(), 0);
    EXPECT_FALSE(spin->isEnabled()) << "only for Custom";
    EXPECT_DOUBLE_EQ(spin->minimum(), 1.0);
    EXPECT_DOUBLE_EQ(spin->singleStep(), 0.5);
    EXPECT_TRUE(keep->isChecked()) << "on by default";
    EXPECT_TRUE(inUse->text().startsWith(QStringLiteral("In use now: ")));
    EXPECT_TRUE(test::saveScreenshot(dlg.grab(), QStringLiteral("memory-options")));

    EXPECT_NEAR(spin->value(), AppOptions::automaticMemoryGB(), 0.05) << "Automatic's own amount, as the help says";
    combo->setCurrentIndex(1);
    EXPECT_TRUE(spin->isEnabled());
    EXPECT_DOUBLE_EQ(spin->value(), std::max(1.0, std::floor(AppOptions::automaticMemoryGB() / 0.5) * 0.5)) << "rounded down";
    spin->setValue(2.5);
    combo->setCurrentIndex(0);
    EXPECT_NEAR(spin->value(), AppOptions::automaticMemoryGB(), 0.05);
    EXPECT_FALSE(dlg.values().customMemory);
    combo->setCurrentIndex(1);
    EXPECT_DOUBLE_EQ(spin->value(), 2.5) << "what was set for Custom comes back";
    keep->setChecked(false);
    const AppOptions::Values v = dlg.values();
    EXPECT_TRUE(v.customMemory);
    EXPECT_DOUBLE_EQ(v.customMemoryGB, 2.5);
    EXPECT_FALSE(v.keepBrushesInMemory);
}

TEST_F(MemoryOptionsTest, TheMemoryOptionsAreSavedAndReachPainting) {
    MainWindow window;
    AppOptions::Values v = window.options()->values();
    EXPECT_FALSE(v.customMemory);
    EXPECT_TRUE(v.keepBrushesInMemory);
    EXPECT_EQ(stroke_memory::policy().customBytes, 0) << "Automatic";
    v.customMemory = true;
    v.customMemoryGB = 2.0;
    v.keepBrushesInMemory = false;
    window.options()->setValues(v);
    EXPECT_EQ(stroke_memory::policy().customBytes, 2 * kGiB) << "Custom in place of Automatic";
    EXPECT_FALSE(stroke_memory::policy().keepBrushesWithin);
    EXPECT_LE(stroke_memory::allowedBytes(), 2 * kGiB);

    const AppOptions reread;
    EXPECT_TRUE(reread.values().customMemory);
    EXPECT_DOUBLE_EQ(reread.values().customMemoryGB, 2.0);
    EXPECT_FALSE(reread.values().keepBrushesInMemory);
    AppOptions::Values huge = v;
    huge.customMemoryGB = 1e6;
    EXPECT_LE(AppOptions::clamped(huge).customMemoryGB, std::max(1.0, AppOptions::machineMemoryGB())) << "up to the machine's";
    huge.customMemoryGB = 0.2;
    EXPECT_DOUBLE_EQ(AppOptions::clamped(huge).customMemoryGB, 1.0) << "from 1 GB";
    window.options()->setValues(AppOptions::Values{});
    EXPECT_EQ(stroke_memory::policy().customBytes, 0);
    EXPECT_TRUE(stroke_memory::policy().keepBrushesWithin);
}
