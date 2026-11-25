#include "platform/CrashReport.h"
#include "ui/CrashReportDialog.h"
#include "ui/MainWindow.h"

#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QFileInfo>
#include <QPlainTextEdit>
#include <QProcess>
#include <QPushButton>
#include <QStandardPaths>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

#include <gtest/gtest.h>

using namespace doodlebold;

namespace {

constexpr char kChildVariable[] = "DOODLEBOLD_CRASH_REPORT_CHILD";
constexpr char kBadPointer[] = "bad-pointer";
constexpr char kHeapCorruption[] = "heap-corruption";
constexpr int kChildMs = 60000;  // the child starts the whole test program, which takes a second or two

QString crashAChild(const char* how) {
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    environment.insert(QString::fromLatin1(kChildVariable), QString::fromLatin1(how));
    // Not sharded as this run is, or the child's one test would be in another shard and it would run none.
    environment.remove(QStringLiteral("GTEST_TOTAL_SHARDS"));
    environment.remove(QStringLiteral("GTEST_SHARD_INDEX"));
    QProcess child;
    child.setProcessEnvironment(environment);
    // gtest's own catching off, or the crash would be a failed test and never reach the handler.
    child.start(QCoreApplication::applicationFilePath(),
                {QStringLiteral("--gtest_filter=CrashReport.CrashingChild"), QStringLiteral("--gtest_catch_exceptions=0")});
    EXPECT_TRUE(child.waitForFinished(kChildMs));
    EXPECT_EQ(child.exitStatus(), QProcess::CrashExit) << child.readAll().toStdString();
    return crash_report::takeNewReport();
}

class CrashReport : public ::testing::Test {
protected:
    void SetUp() override {
        QStandardPaths::setTestModeEnabled(true);  // the reports go to Qt's test folder, not the machine's own
        crash_report::takeNewReport();  // none left from an earlier run
    }
    void TearDown() override { QStandardPaths::setTestModeEnabled(false); }
};

}

// Not a test of its own: the process crashAChild starts runs it to crash.
TEST_F(CrashReport, CrashingChild) {
    const QByteArray how = qgetenv(kChildVariable);
    if (how.isEmpty())
        GTEST_SKIP() << "run only as the crashing process";
#ifdef Q_OS_WIN
    SetErrorMode(SetErrorMode(0) | SEM_NOGPFAULTERRORBOX);  // no "stopped working" window to wait for
#endif
    crash_report::install();
#ifdef Q_OS_WIN
    // STATUS_HEAP_CORRUPTION, raised as the heap's own check raises it.
    constexpr DWORD kHeapCorruptionCode = 0xC0000374;
    if (how == kHeapCorruption)
        RaiseException(kHeapCorruptionCode, EXCEPTION_NONCONTINUABLE, 0, nullptr);
#endif
    crash_report::crashForTest();
}

TEST_F(CrashReport, ABadPointerLeavesAReportForTheNextStart) {
    const QString report = crashAChild(kBadPointer);
    EXPECT_TRUE(report.startsWith(QStringLiteral("Doodlebold crash report\nVersion: "))) << report.toStdString();
    EXPECT_TRUE(report.contains(QStringLiteral("\nSystem: ")));
#ifdef Q_OS_WIN
    EXPECT_TRUE(report.contains(QStringLiteral("Crash: access violation 0xC0000005, writing 0x0\n"))) << report.toStdString();
    EXPECT_TRUE(report.contains(QStringLiteral("(the main thread)")));
    // Its first frame is where it crashed, in the test program, as module+offset.
    const QString program = QFileInfo(QCoreApplication::applicationFilePath()).fileName();
    EXPECT_TRUE(report.contains(QStringLiteral("Stack:\n  #0 ") + program + QStringLiteral("+0x"))) << report.toStdString();
#else
    EXPECT_TRUE(report.contains(QStringLiteral("Crash: segmentation fault (SIGSEGV), at 0x0\n"))) << report.toStdString();
    EXPECT_TRUE(report.contains(QStringLiteral("Stack:\n")));
#endif
    EXPECT_EQ(crash_report::lastReport(), report) << "kept for Other › Crash Report";
    EXPECT_TRUE(crash_report::takeNewReport().isEmpty()) << "shown once";
}

#ifdef Q_OS_WIN
TEST_F(CrashReport, HeapCorruptionIsReportedThoughItSkipsTheUnhandledFilter) {
    const QString report = crashAChild(kHeapCorruption);
    EXPECT_TRUE(report.contains(QStringLiteral("Crash: heap corruption 0xC0000374\n"))) << report.toStdString();
    EXPECT_TRUE(report.contains(QStringLiteral("Stack:\n  #0 "))) << report.toStdString();
}
#endif

TEST(CrashReportDialog, CopyPutsTheWholeReportOnTheClipboard) {
    const QString report = QStringLiteral("Doodlebold crash report\nCrash: access violation\n");
    CrashReportDialog dialog(report, true);
    EXPECT_EQ(dialog.windowTitle(), QStringLiteral("Doodlebold closed unexpectedly"));
    auto* text = dialog.findChild<QPlainTextEdit*>(QStringLiteral("crashReportText"));
    ASSERT_TRUE(text);
    EXPECT_EQ(text->toPlainText(), report);
    auto* copy = dialog.findChild<QPushButton*>(QStringLiteral("crashReportCopy"));
    ASSERT_TRUE(copy);
    EXPECT_TRUE(copy->isDefault());
    QGuiApplication::clipboard()->clear();
    copy->click();
    EXPECT_EQ(QGuiApplication::clipboard()->text(), report);
}

TEST(CrashReportDialog, WithNoReportThereIsNothingToCopy) {
    CrashReportDialog dialog(QString(), false);
    dialog.show();
    EXPECT_EQ(dialog.windowTitle(), QStringLiteral("Crash Report"));
    EXPECT_FALSE(dialog.findChild<QPlainTextEdit*>(QStringLiteral("crashReportText"))->isVisible());
    EXPECT_FALSE(dialog.findChild<QPushButton*>(QStringLiteral("crashReportCopy"))->isEnabled());
}

TEST(CrashReportDialog, OtherMenuOpensTheLastReport) {
    MainWindow window;
    QAction* entry = window.command(QStringLiteral("other.crashReport"));
    ASSERT_TRUE(entry);
    entry->trigger();
    const auto* dialog = window.findChild<CrashReportDialog*>();
    ASSERT_TRUE(dialog);
    EXPECT_TRUE(dialog->isVisible());
    EXPECT_EQ(dialog->windowModality(), Qt::WindowModal);
}
