#include "ScreenshotSupport.h"

#include "core/document/Document.h"
#include "core/io/RecoveryStore.h"
#include "core/options/AppOptions.h"
#include "core/tools/ToolBoxStore.h"
#include "ui/CrashReportDialog.h"
#include "ui/CanvasView.h"
#include "ui/DocumentTabs.h"
#include "ui/MainWindow.h"
#include "ui/OptionsDialog.h"
#include "ui/RecoveryController.h"
#include "ui/RecoveryDialog.h"

#include <QCheckBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QTemporaryDir>

#include <gtest/gtest.h>

#include <memory>

using namespace doodlebold;

namespace {

template <typename T> T* find(QObject* parent, const char* name) {
    T* w = parent->findChild<T*>(QString::fromLatin1(name));
    EXPECT_NE(w, nullptr) << name;
    return w;
}

// A canvas a crash left in `root`: a run that ended without end(), with one copy of it.
void leaveACrashedCanvas(const QString& root, const RecoveryManifest& manifest) {
    RecoveryStore crashed(root);
    const Document canvas(QSize(64, 48), QColor(Qt::white));
    ASSERT_TRUE(crashed.writeCopy(QStringLiteral("canvas-1"), canvas.content(), manifest, AppOptions::kDefaultRecoveryCopiesKept));
}

RecoveryManifest portrait() {
    return {QStringLiteral("Portrait.doodle"), QStringLiteral("C:/Art/Portrait.doodle"), QDateTime::currentDateTimeUtc()};
}

QList<QWidget*> rowsOf(const RecoveryDialog* dialog) {
    QList<QWidget*> shown;
    for (QWidget* row : dialog->findChildren<QWidget*>(QStringLiteral("recoveryRow")))
        if (!row->isHidden())
            shown << row;
    return shown;
}

class RecoveryTest : public testing::Test {
protected:
    void SetUp() override {
        QStandardPaths::setTestModeEnabled(true);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDir.path());
        test::applyTheme();
        QFile::remove(ToolBoxStore::defaultPath());
        AppOptions::Values v = AppOptions().values();
        v.recoveryFolder = root.path();
        AppOptions().setValues(v);
    }
    void TearDown() override {
        window.reset();
        QFile::remove(ToolBoxStore::defaultPath());
    }

    void openWindow() {
        window = std::make_unique<MainWindow>();
        window->show();
        QCoreApplication::processEvents();
    }
    Document* newCanvas() {
        window->newDocument();
        return window->tabs()->currentDocument();
    }
    RecoveryController* recovery() const { return window->recovery(); }
    QString canvasFolder() const { return recovery()->runFolder() + QStringLiteral("/canvas-1"); }
    std::vector<RecoveryCopy> copies() const { return RecoveryStore::copiesIn(canvasFolder()); }
    RecoveryDialog* offer() {
        window->offerRecovery();
        return window->findChild<RecoveryDialog*>();
    }

    QTemporaryDir settingsDir;
    QTemporaryDir root;
    std::unique_ptr<MainWindow> window;
};

}

TEST_F(RecoveryTest, TheRecoveryPageComesLastWithItsDefaults) {
    OptionsDialog dlg(AppOptions::Values{});
    auto* pages = find<QListWidget>(&dlg, "optionsPages");
    ASSERT_TRUE(pages);
    EXPECT_EQ(pages->item(pages->count() - 1)->text(), QStringLiteral("Recovery"));
    pages->setCurrentRow(pages->count() - 1);
    QWidget* page = dlg.findChild<QStackedWidget*>()->currentWidget();

    auto* save = find<QCheckBox>(page, "saveRecoveryCopiesCheck");
    auto* minutes = find<QSpinBox>(page, "recoveryMinutesSpin");
    auto* kept = find<QSpinBox>(page, "recoveryCopiesKeptSpin");
    auto* folder = find<QLineEdit>(page, "recoveryFolderEdit");
    auto* browse = find<QPushButton>(page, "recoveryFolderBrowse");
    ASSERT_TRUE(save && minutes && kept && folder && browse);
    EXPECT_TRUE(save->isChecked());
    EXPECT_EQ(minutes->value(), 5);
    EXPECT_EQ(minutes->minimum(), 1);
    EXPECT_EQ(minutes->maximum(), 60);
    EXPECT_EQ(kept->value(), 3);
    EXPECT_EQ(kept->minimum(), 1);
    EXPECT_EQ(kept->maximum(), 20);
    EXPECT_EQ(QDir::fromNativeSeparators(folder->text()), AppOptions::defaultRecoveryFolder());
    EXPECT_TRUE(AppOptions::defaultRecoveryFolder().endsWith(QStringLiteral("/recovery")));
    EXPECT_EQ(browse->text(), QStringLiteral("Browse…"));

    save->setChecked(false);
    for (QWidget* control : std::initializer_list<QWidget*>{minutes, kept, folder, browse})
        EXPECT_FALSE(control->isEnabled()) << control->objectName().toStdString();
    save->setChecked(true);
    EXPECT_TRUE(folder->isEnabled());
    dlg.resize(960, 640);
    EXPECT_TRUE(test::saveScreenshot(dlg.grab(), QStringLiteral("options_recovery")));
}

TEST_F(RecoveryTest, TheRecoveryOptionsFollowTheControlsAndAreKept) {
    OptionsDialog dlg(AppOptions::Values{});
    find<QCheckBox>(&dlg, "saveRecoveryCopiesCheck")->setChecked(false);
    find<QSpinBox>(&dlg, "recoveryMinutesSpin")->setValue(12);
    find<QSpinBox>(&dlg, "recoveryCopiesKeptSpin")->setValue(7);
    find<QLineEdit>(&dlg, "recoveryFolderEdit")->setText(QDir::toNativeSeparators(root.filePath(QStringLiteral("mine"))));
    const AppOptions::Values v = dlg.values();
    EXPECT_FALSE(v.saveRecoveryCopies);
    EXPECT_EQ(v.recoveryMinutes, 12);
    EXPECT_EQ(v.recoveryCopiesKept, 7);
    EXPECT_EQ(v.recoveryFolder, root.filePath(QStringLiteral("mine")));

    AppOptions().setValues(v);
    const AppOptions reread;
    EXPECT_FALSE(reread.values().saveRecoveryCopies);
    EXPECT_EQ(reread.values().recoveryMinutes, 12);
    EXPECT_EQ(reread.values().recoveryCopiesKept, 7);
    EXPECT_EQ(reread.values().recoveryFolder, root.filePath(QStringLiteral("mine")));

    find<QLineEdit>(&dlg, "recoveryFolderEdit")->clear();
    AppOptions::Values cleared = dlg.values();
    cleared.recoveryMinutes = 500;
    cleared.recoveryCopiesKept = 0;
    AppOptions().setValues(cleared);
    EXPECT_EQ(AppOptions().values().recoveryFolder, AppOptions::defaultRecoveryFolder()) << "left empty: the default";
    EXPECT_EQ(AppOptions().values().recoveryMinutes, AppOptions::kMostRecoveryMinutes);
    EXPECT_EQ(AppOptions().values().recoveryCopiesKept, AppOptions::kLeastRecoveryCopiesKept);
    EXPECT_FALSE(AppOptions::settings().contains(QStringLiteral("options/recoveryFolder"))) << "the default isn't written down";
}

TEST_F(RecoveryTest, ACanvasWithUnsavedChangesGetsACopyOnceForEachChange) {
    openWindow();
    Document* doc = newCanvas();
    ASSERT_TRUE(doc);
    recovery()->saveCopies();
    recovery()->waitForWrites();
    EXPECT_TRUE(copies().empty()) << "nothing unsaved";

    ASSERT_TRUE(doc->addLayer());
    recovery()->saveCopies();
    recovery()->waitForWrites();
    ASSERT_EQ(copies().size(), 1u);
    EXPECT_EQ(copies()[0].manifest.name, doc->displayName());
    EXPECT_TRUE(copies()[0].manifest.originalPath.isEmpty());
    EXPECT_TRUE(recovery()->runFolder().startsWith(root.path() + QStringLiteral("/run-")));

    recovery()->saveCopies();
    recovery()->waitForWrites();
    EXPECT_EQ(copies().size(), 1u) << "nothing changed since";
}

TEST_F(RecoveryTest, ACanvasBeingPaintedIsCopiedWhenItsStrokeEnds) {
    openWindow();
    Document* doc = newCanvas();
    ASSERT_TRUE(doc->addLayer());
    doc->setPainting(true);
    recovery()->saveCopies();
    recovery()->waitForWrites();
    EXPECT_TRUE(copies().empty());
    doc->setPainting(false);
    recovery()->waitForWrites();
    EXPECT_EQ(copies().size(), 1u);
}

TEST_F(RecoveryTest, SavingClosingAndANormalEndDeleteTheCopies) {
    openWindow();
    Document* doc = newCanvas();
    ASSERT_TRUE(doc->addLayer());
    recovery()->saveCopies();
    recovery()->waitForWrites();
    ASSERT_EQ(copies().size(), 1u);
    doc->undoStack()->setClean();  // as a save leaves it
    recovery()->waitForWrites();
    EXPECT_FALSE(QDir(canvasFolder()).exists());

    ASSERT_TRUE(doc->addLayer());
    recovery()->saveCopies();
    recovery()->waitForWrites();
    ASSERT_EQ(copies().size(), 1u);
    doc->undoStack()->setClean();
    ASSERT_TRUE(window->tabs()->requestCloseTab(0));
    recovery()->waitForWrites();
    EXPECT_FALSE(QDir(canvasFolder()).exists());

    ASSERT_TRUE(newCanvas()->addLayer());
    recovery()->saveCopies();
    recovery()->waitForWrites();
    const QString run = recovery()->runFolder();
    EXPECT_FALSE(QDir(run).entryList(QDir::Dirs | QDir::NoDotAndDotDot).isEmpty());
    window.reset();
    EXPECT_FALSE(QDir(run).exists());
    EXPECT_TRUE(RecoveryStore::leftovers(root.path()).empty());
}

TEST_F(RecoveryTest, TurningCopiesOffEndsTheRun) {
    openWindow();
    const QString run = recovery()->runFolder();
    ASSERT_TRUE(QDir(run).exists());
    AppOptions::Values v = window->options()->values();
    v.saveRecoveryCopies = false;
    window->options()->setValues(v);
    recovery()->waitForWrites();
    EXPECT_FALSE(QDir(run).exists());
    EXPECT_TRUE(recovery()->runFolder().isEmpty());
}

TEST_F(RecoveryTest, NothingToRecoverShowsNoDialog) {
    openWindow();
    EXPECT_EQ(offer(), nullptr);
}

TEST_F(RecoveryTest, RestoreOpensTheCanvasAsAnUnsavedOneUnderANewName) {
    leaveACrashedCanvas(root.path(), portrait());
    openWindow();
    RecoveryDialog* dialog = offer();
    ASSERT_TRUE(dialog);
    EXPECT_TRUE(dialog->isVisible());
    EXPECT_EQ(dialog->windowModality(), Qt::WindowModal);
    const QList<QWidget*> rows = rowsOf(dialog);
    ASSERT_EQ(rows.size(), 1);
    EXPECT_EQ(find<QLabel>(rows[0], "recoveryName")->text(), QStringLiteral("Portrait.doodle"));
    const QString details = find<QLabel>(rows[0], "recoveryDetails")->text();
    EXPECT_TRUE(details.contains(QDir::toNativeSeparators(QStringLiteral("C:/Art/Portrait.doodle")))) << details.toStdString();
    dialog->resize(560, 260);
    EXPECT_TRUE(test::saveScreenshot(dialog->grab(), QStringLiteral("recovery_dialog")));

    const int before = window->tabs()->count();
    find<QPushButton>(rows[0], "recoveryRestore")->click();
    ASSERT_EQ(window->tabs()->count(), before + 1);
    Document* restored = window->tabs()->currentDocument();
    EXPECT_TRUE(restored->isModified());
    EXPECT_TRUE(restored->filePath().isEmpty()) << "Save asks where: the original is never overwritten unasked";
    EXPECT_EQ(restored->displayName(), QStringLiteral("Portrait (recovered)"));
    EXPECT_EQ(restored->size(), QSize(64, 48));
    EXPECT_FALSE(dialog->isVisible()) << "closed with its last canvas";

    EXPECT_TRUE(RecoveryStore::leftovers(root.path()).empty()) << "its copy is the running run's now";
    ASSERT_EQ(copies().size(), 1u);
    EXPECT_EQ(copies()[0].manifest.originalPath, QStringLiteral("C:/Art/Portrait.doodle"));
}

TEST_F(RecoveryTest, DiscardDeletesTheCopiesAndOpensNothing) {
    leaveACrashedCanvas(root.path(), portrait());
    openWindow();
    RecoveryDialog* dialog = offer();
    ASSERT_TRUE(dialog);
    const int before = window->tabs()->count();
    find<QPushButton>(rowsOf(dialog)[0], "recoveryDiscard")->click();
    EXPECT_EQ(window->tabs()->count(), before);
    EXPECT_TRUE(RecoveryStore::leftovers(root.path()).empty());
    EXPECT_EQ(QDir(root.path()).entryList(QDir::Dirs | QDir::NoDotAndDotDot), QStringList{QFileInfo(recovery()->runFolder()).fileName()})
        << "only the running run is left";
}

TEST_F(RecoveryTest, ACanvasWhoseCopiesCantBeReadStaysToBeDiscarded) {
    leaveACrashedCanvas(root.path(), portrait());
    const std::vector<LeftoverCanvas> left = RecoveryStore::leftovers(root.path());
    ASSERT_EQ(left.size(), 1u);
    QFile damaged(left[0].copies[0].file);
    ASSERT_TRUE(damaged.open(QIODevice::WriteOnly | QIODevice::Truncate));
    damaged.write("not a canvas");
    damaged.close();
    openWindow();
    RecoveryDialog* dialog = offer();
    ASSERT_TRUE(dialog);
    QWidget* row = rowsOf(dialog).value(0);
    ASSERT_TRUE(row);
    find<QPushButton>(row, "recoveryRestore")->click();
    EXPECT_EQ(window->tabs()->count(), 0);
    EXPECT_FALSE(find<QPushButton>(row, "recoveryRestore")->isEnabled());
    EXPECT_EQ(rowsOf(dialog).size(), 1) << "still there to be discarded";
    EXPECT_TRUE(dialog->isVisible());
    find<QPushButton>(row, "recoveryDiscard")->click();
    EXPECT_TRUE(RecoveryStore::leftovers(root.path()).empty());
}

TEST_F(RecoveryTest, RestoreAllAndDiscardAllSettleEveryCanvas) {
    leaveACrashedCanvas(root.path(), portrait());
    leaveACrashedCanvas(root.path(), {QStringLiteral("NewCanvas2"), QString(), QDateTime::currentDateTimeUtc()});
    openWindow();
    RecoveryDialog* dialog = offer();
    ASSERT_TRUE(dialog);
    ASSERT_EQ(rowsOf(dialog).size(), 2);
    EXPECT_TRUE(find<QPushButton>(dialog, "recoveryRestoreAll")->isDefault());
    const int before = window->tabs()->count();
    find<QPushButton>(dialog, "recoveryRestoreAll")->click();
    EXPECT_EQ(window->tabs()->count(), before + 2);
    EXPECT_TRUE(RecoveryStore::leftovers(root.path()).empty());
    QStringList names;
    for (int i = 0; i < window->tabs()->count(); ++i)
        names << window->tabs()->canvasAt(i)->document()->displayName();
    EXPECT_TRUE(names.contains(QStringLiteral("NewCanvas2 (recovered)")));

    window.reset();
    leaveACrashedCanvas(root.path(), portrait());
    leaveACrashedCanvas(root.path(), portrait());
    openWindow();
    dialog = offer();
    ASSERT_TRUE(dialog);
    find<QPushButton>(dialog, "recoveryDiscardAll")->click();
    EXPECT_EQ(window->tabs()->count(), 0);
    EXPECT_TRUE(RecoveryStore::leftovers(root.path()).empty());
}

TEST_F(RecoveryTest, TheCrashReportComesFirstAndTheRecoveryAfterIt) {
    leaveACrashedCanvas(root.path(), portrait());
    openWindow();
    window->showStartDialogs(QStringLiteral("Doodlebold crash report\n"));
    auto* crash = window->findChild<CrashReportDialog*>();
    ASSERT_TRUE(crash);
    EXPECT_EQ(window->findChild<RecoveryDialog*>(), nullptr) << "not both at once";
    crash->reject();
    auto* dialog = window->findChild<RecoveryDialog*>();
    ASSERT_TRUE(dialog);
    EXPECT_TRUE(dialog->isVisible());
}
