#include "ScreenshotSupport.h"
#include "core/options/AppOptions.h"
#include "core/tools/ToolBoxStore.h"
#include "core/util/ProjectLinks.h"
#include "ui/AboutDialog.h"
#include "ui/MainWindow.h"
#include "ui/Theme.h"

#include <QAction>
#include <QCoreApplication>
#include <QFile>
#include <QIcon>
#include <QLabel>
#include <QLayout>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QUrlQuery>

#include <gtest/gtest.h>

using namespace doodlebold;

namespace {

class AboutTest : public testing::Test {
protected:
    void SetUp() override {
        QStandardPaths::setTestModeEnabled(true);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDir.path());
        test::applyTheme();
        QCoreApplication::setApplicationVersion(QStringLiteral("1.2.3"));
        links::setOpener([this](const QUrl& url) {
            opened << url;
            return true;
        });
    }
    void TearDown() override {
        links::setOpener({});
        QFile::remove(ToolBoxStore::defaultPath());
    }

    QStringList texts(const QDialog& dialog) const {
        QStringList out;
        for (const QLabel* label : dialog.findChildren<QLabel*>())
            out << label->text();
        return out;
    }

    QTemporaryDir settingsDir;
    QList<QUrl> opened;
};

}

TEST_F(AboutTest, DialogShowsVersionAuthorLicenceAndQt) {
    AboutDialog dialog;
    const QStringList text = texts(dialog);
    EXPECT_TRUE(text.contains(QStringLiteral("Doodlebold")));
    EXPECT_TRUE(text.contains(QStringLiteral("Version 1.2.3")));
    EXPECT_TRUE(text.contains(QStringLiteral("doodlebold")));
    EXPECT_TRUE(text.contains(QStringLiteral("MIT")));
    EXPECT_TRUE(text.contains(QStringLiteral("Built with Qt %1 (LGPLv3)").arg(QString::fromLatin1(qVersion()))));
    EXPECT_TRUE(text.contains(QStringLiteral("A free, open-source digital-painting program.")));
}

TEST_F(AboutTest, ThirdPartyLicencesButtonShowsTheNotices) {
    AboutDialog dialog;
    auto* button = dialog.findChild<QPushButton*>(QStringLiteral("aboutLicences"));
    ASSERT_TRUE(button);
    EXPECT_EQ(button->text(), QStringLiteral("Third-party licences"));
    EXPECT_FALSE(dialog.findChild<QDialog*>(QStringLiteral("noticesDialog")));

    button->click();
    const auto* notices = dialog.findChild<QDialog*>(QStringLiteral("noticesDialog"));
    ASSERT_TRUE(notices);
    EXPECT_TRUE(notices->isVisible());
    const auto* view = notices->findChild<QPlainTextEdit*>(QStringLiteral("noticesText"));
    ASSERT_TRUE(view);
    EXPECT_TRUE(view->isReadOnly());
    EXPECT_TRUE(view->toPlainText().contains(QStringLiteral("Doodlebold third-party notices")));
    EXPECT_TRUE(view->toPlainText().contains(QStringLiteral("Independent JPEG Group")));
}

TEST_F(AboutTest, NoticesAreEmbeddedInTheProgram) {
    QFile file(QStringLiteral(":/THIRD-PARTY-NOTICES.txt"));
    ASSERT_TRUE(file.open(QIODevice::ReadOnly));
    EXPECT_TRUE(file.readAll().contains("LGPL"));
}

TEST_F(AboutTest, DialogShowsTheProgramIcon) {
    AboutDialog dialog;
    const auto* icon = dialog.findChild<QLabel*>(QStringLiteral("aboutIcon"));
    ASSERT_TRUE(icon);
    const QImage shown = icon->pixmap().toImage();
    ASSERT_FALSE(shown.isNull());
    EXPECT_EQ(shown, QIcon(QStringLiteral(":/icons/doodlebold.png")).pixmap(icon->pixmap().deviceIndependentSize().toSize()).toImage());
    bool drawn = false;
    for (int y = 0; y < shown.height() && !drawn; ++y)
        for (int x = 0; x < shown.width() && !drawn; ++x)
            drawn = qAlpha(shown.pixel(x, y)) > 0;
    EXPECT_TRUE(drawn) << "the icon is not empty";
}

// Feedback: "Lets make the doodlebold icon 45% bigger in the about dialogue, and center it vertically in the same column it is."
TEST_F(AboutTest, TheIconIsLargerAndCentredBesideTheText) {
    AboutDialog dialog;
    dialog.show();
    QCoreApplication::processEvents();
    const auto* icon = dialog.findChild<QLabel*>(QStringLiteral("aboutIcon"));
    const auto* version = dialog.findChild<QLabel*>(QStringLiteral("aboutVersion"));
    ASSERT_TRUE(icon && version);
    EXPECT_EQ(icon->pixmap().deviceIndependentSize().toSize(), QSize(93, 93));
    EXPECT_TRUE(icon->alignment() & Qt::AlignVCenter);
    EXPECT_GE(icon->height(), 93);
    EXPECT_LE(icon->geometry().top(), version->geometry().top()) << "the column runs from the top of the text";
}

TEST_F(AboutTest, TheProgramIconIsTheDragonAtFullSize) {
    const QImage icon(QStringLiteral(":/icons/doodlebold.png"));
    ASSERT_FALSE(icon.isNull());
    EXPECT_EQ(icon.size(), QSize(512, 512));
    EXPECT_TRUE(icon.hasAlphaChannel());
    EXPECT_EQ(qAlpha(icon.pixel(0, 0)), 0) << "the corners are see-through";
    EXPECT_FALSE(QFile::exists(QStringLiteral(":/icons/doodlebold.svg")));
}

// Its links pointed at Doodlebold's repository, since renamed; until this version has its own they're left out.
TEST_F(AboutTest, ThereAreNoRepositoryLinks) {
    AboutDialog dialog;
    EXPECT_EQ(dialog.findChild<QLabel*>(QStringLiteral("aboutRepository")), nullptr);
    EXPECT_EQ(dialog.findChild<QLabel*>(QStringLiteral("aboutIssues")), nullptr);
    for (const QLabel* label : dialog.findChildren<QLabel*>())
        EXPECT_FALSE(label->text().contains(QStringLiteral("github.com"))) << label->text().toStdString();
}

TEST_F(AboutTest, DialogFollowsTheDialogStandard) {
    AboutDialog dialog;
    const int pad = Theme::metric(Theme::Metric::DialogPadding);
    ASSERT_TRUE(dialog.layout());
    EXPECT_EQ(dialog.layout()->contentsMargins(), QMargins(pad, pad, pad, pad));
    EXPECT_EQ(dialog.layout()->sizeConstraint(), QLayout::SetFixedSize);
    dialog.show();
    QApplication::processEvents();
    const QSize natural = dialog.size();
    dialog.resize(natural + QSize(200, 200));
    QApplication::processEvents();
    EXPECT_EQ(dialog.size(), natural);
}

TEST_F(AboutTest, ScreenshotOfTheDialog) {
    AboutDialog dialog;
    dialog.show();
    QApplication::processEvents();
    EXPECT_TRUE(test::saveScreenshot(dialog.grab(), QStringLiteral("about-dialog")));
}

TEST_F(AboutTest, MenuEntriesOpenTheDialogAndTheBugReport) {
    MainWindow window;
    QAction* about = window.command(QStringLiteral("other.aboutDoodlebold"));
    QAction* report = window.command(QStringLiteral("other.reportBug"));
    ASSERT_TRUE(about && report);

    about->trigger();
    const auto* dialog = window.findChild<AboutDialog*>();
    ASSERT_TRUE(dialog);
    EXPECT_TRUE(dialog->isVisible());
    // Window-modal, as the other dialogs: on Wayland the desktop centres a modal dialog on its window, and places others where it
    // likes. Where it lands can't be read here: the offscreen screen doesn't place windows as a desktop does.
    EXPECT_EQ(dialog->windowModality(), Qt::WindowModal);
    EXPECT_TRUE(opened.isEmpty());

    report->trigger();
    ASSERT_EQ(opened.size(), 1);
    const QUrlQuery query(opened.first());
    EXPECT_EQ(opened.first().path(), QStringLiteral("/doodlebold/DoodleBold/issues/new"));
    EXPECT_EQ(query.queryItemValue(QStringLiteral("template")), QStringLiteral("bug_report.yml"));
    EXPECT_EQ(query.queryItemValue(QStringLiteral("version")), QStringLiteral("1.2.3"));
    EXPECT_TRUE(query.hasQueryItem(QStringLiteral("system")));
}
