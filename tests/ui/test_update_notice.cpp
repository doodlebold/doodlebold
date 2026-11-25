// The update check: Help › Check for Updates… always answers, the check at start only tells of a newer release, at most once a
// day and only while Options › View allows it. GitHub's replies are faked: the tests never go on the network. Also renders the
// notice for review (DOODLEBOLD_SCREENSHOT_DIR).
#include "ScreenshotSupport.h"

#include "core/options/AppOptions.h"
#include "core/tools/ToolBoxStore.h"
#include "core/util/BuildInfo.h"
#include "core/util/ProjectLinks.h"
#include "core/util/UpdateCheck.h"
#include "ui/MainWindow.h"
#include "ui/UpdateNotice.h"

#include <QAction>
#include <QApplication>
#include <QFile>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryDir>

#include <gtest/gtest.h>

#include <cstdlib>
#include <memory>
#include <optional>

using namespace doodlebold;

namespace {

const QUrl kNewerPage(QStringLiteral("https://github.com/doodlebold/DoodleBold/releases/tag/v999.0.0"));

QByteArray releaseReply(const QString& tag, const QUrl& page) {
    return QStringLiteral(R"({"tag_name": "%1", "html_url": "%2"})").arg(tag, page.toString()).toUtf8();
}

// This build's own release: its version without the -dev of a build of later work.
QString thisRelease() {
    return build_info::version().remove(QStringLiteral("-dev"));
}

class UpdateNoticeTest : public testing::Test {
protected:
    void SetUp() override {
        QStandardPaths::setTestModeEnabled(true);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDir.path());
        test::applyTheme();
        QFile::remove(ToolBoxStore::defaultPath());
        UpdateNotice::setLastCheck(QDateTime());
        UpdateNotice::setFetcher([this](const QUrl& url, const UpdateNotice::Done& done) {
            asked << url;
            done(answer);
        });
        links::setOpener([this](const QUrl& url) {
            opened << url;
            return true;
        });
        window = std::make_unique<MainWindow>();
        window->show();
        QCoreApplication::processEvents();
    }
    void TearDown() override {
        window.reset();
        UpdateNotice::setFetcher({});
        links::setOpener({});
        QFile::remove(ToolBoxStore::defaultPath());
    }

    void answerWith(const QString& tag) { answer = releaseReply(tag, kNewerPage); }
    void checkNow() {
        window->command(QStringLiteral("other.checkForUpdates"))->trigger();
        QCoreApplication::processEvents();
    }
    void checkAtStart() {
        window->checkForUpdatesAtStart();
        QCoreApplication::processEvents();
    }
    QMessageBox* notice() const {
        const QList<QMessageBox*> boxes = window->findChildren<QMessageBox*>(QStringLiteral("updateNotice"));
        EXPECT_LE(boxes.size(), 1);
        return boxes.isEmpty() ? nullptr : boxes.first();
    }
    QPushButton* openButton() const { return notice()->findChild<QPushButton*>(QStringLiteral("updateNoticeOpen")); }

    QTemporaryDir settingsDir;
    std::unique_ptr<MainWindow> window;
    std::optional<QByteArray> answer;
    QList<QUrl> asked;
    QList<QUrl> opened;
};

}

TEST_F(UpdateNoticeTest, TheMenuEntryTellsOfANewerReleaseAndOpensItsPage) {
    answerWith(QStringLiteral("v999.0.0"));
    checkNow();
    EXPECT_EQ(asked, (QList<QUrl>{update_check::latestReleaseUrl()}));
    QMessageBox* box = notice();
    ASSERT_TRUE(box);
    EXPECT_EQ(box->text(), QStringLiteral("Doodlebold 999.0.0 is available"));
    EXPECT_TRUE(box->informativeText().contains(build_info::version()));
    EXPECT_EQ(box->windowModality(), Qt::WindowModal);
    ASSERT_TRUE(openButton());
    EXPECT_EQ(box->defaultButton(), openButton());
    EXPECT_TRUE(test::saveScreenshot(box->grab(), QStringLiteral("update-notice")));
    openButton()->click();
    EXPECT_EQ(opened, (QList<QUrl>{kNewerPage}));
}

TEST_F(UpdateNoticeTest, TheMenuEntrySaysWhenThisIsTheLatestRelease) {
    answerWith(QStringLiteral("v") + thisRelease());
    checkNow();
    QMessageBox* box = notice();
    ASSERT_TRUE(box);
    EXPECT_EQ(box->text(), QStringLiteral("Doodlebold is up to date"));
    EXPECT_TRUE(box->informativeText().contains(thisRelease()));
    EXPECT_FALSE(openButton());
}

TEST_F(UpdateNoticeTest, TheMenuEntrySaysWhenItCouldNotCheck) {
    checkNow();
    ASSERT_TRUE(notice());
    EXPECT_EQ(notice()->text(), QStringLiteral("Couldn't check for updates"));
    EXPECT_FALSE(openButton());
    notice()->close();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);

    answer = QByteArrayLiteral(R"({"message": "Not Found"})");
    checkNow();
    ASSERT_TRUE(notice());
    EXPECT_EQ(notice()->text(), QStringLiteral("Couldn't check for updates"));
    EXPECT_TRUE(opened.isEmpty());
}

TEST_F(UpdateNoticeTest, TheCheckAtStartTellsOfANewerReleaseWithoutBlockingTheWindow) {
    answerWith(QStringLiteral("v999.0.0"));
    checkAtStart();
    QMessageBox* box = notice();
    ASSERT_TRUE(box);
    EXPECT_EQ(box->text(), QStringLiteral("Doodlebold 999.0.0 is available"));
    EXPECT_EQ(box->windowModality(), Qt::NonModal);
    EXPECT_EQ(QApplication::activeModalWidget(), nullptr);
    openButton()->click();
    EXPECT_EQ(opened, (QList<QUrl>{kNewerPage}));
}

TEST_F(UpdateNoticeTest, TheCheckAtStartIsSilentUnlessANewerReleaseExists) {
    answerWith(QStringLiteral("v") + thisRelease());
    checkAtStart();
    EXPECT_EQ(asked.size(), 1);
    EXPECT_FALSE(notice()) << "up to date";

    UpdateNotice::setLastCheck(QDateTime());
    answer.reset();
    checkAtStart();
    EXPECT_EQ(asked.size(), 2);
    EXPECT_FALSE(notice()) << "GitHub unreachable";

    UpdateNotice::setLastCheck(QDateTime());
    answer = QByteArrayLiteral("<html>a captive portal</html>");
    checkAtStart();
    EXPECT_EQ(asked.size(), 3);
    EXPECT_FALSE(notice()) << "a reply that is no release";
}

TEST_F(UpdateNoticeTest, TheCheckAtStartAsksAtMostOnceADay) {
    EXPECT_FALSE(UpdateNotice::lastCheck().isValid());
    checkAtStart();
    EXPECT_EQ(asked.size(), 1);
    const QDateTime now = QDateTime::currentDateTimeUtc();
    EXPECT_LE(std::abs(UpdateNotice::lastCheck().secsTo(now)), 60) << "kept, though GitHub couldn't be reached";
    checkAtStart();
    EXPECT_EQ(asked.size(), 1) << "again the same day";
    UpdateNotice::setLastCheck(now.addSecs(-23 * 60 * 60));
    checkAtStart();
    EXPECT_EQ(asked.size(), 1);
    UpdateNotice::setLastCheck(now.addDays(-1).addSecs(-60));
    checkAtStart();
    EXPECT_EQ(asked.size(), 2) << "a day later";
    checkNow();
    checkNow();
    EXPECT_EQ(asked.size(), 4) << "the menu entry asks every time";
}

TEST_F(UpdateNoticeTest, TheCheckAtStartIsOnByDefaultAndTheOptionTurnsItOff) {
    EXPECT_TRUE(window->options()->values().checkForUpdatesAtStart);
    AppOptions::Values v = window->options()->values();
    v.checkForUpdatesAtStart = false;
    window->options()->setValues(v);
    checkAtStart();
    EXPECT_TRUE(asked.isEmpty());
    EXPECT_FALSE(UpdateNotice::lastCheck().isValid());
    EXPECT_FALSE(AppOptions().values().checkForUpdatesAtStart) << "kept in the settings";

    v.checkForUpdatesAtStart = true;
    window->options()->setValues(v);
    checkAtStart();
    EXPECT_EQ(asked.size(), 1);
}

// The check runs only when main() asks for it: a window made, as every UI test makes one, asks nothing.
TEST_F(UpdateNoticeTest, AWindowAloneAsksNothing) {
    EXPECT_TRUE(asked.isEmpty());
}
