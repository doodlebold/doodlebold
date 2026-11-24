#include "core/util/ProjectLinks.h"

#include <QCoreApplication>
#include <QSysInfo>
#include <QUrlQuery>

#include <gtest/gtest.h>

using namespace doodlebold;

namespace {

links::SystemFacts facts() {
    return {QStringLiteral("1.2.3"), QStringLiteral("Fedora Linux 44, COSMIC, wayland, Qt 6.9.1"), QStringLiteral("Wacom Intuos S Pen")};
}

}

TEST(ProjectLinks, BugReportOpensANewIssueOnTheBugTemplate) {
    const QUrl url = links::bugReportUrl(facts());
    EXPECT_EQ(url.scheme(), QStringLiteral("https"));
    EXPECT_EQ(url.host(), QStringLiteral("github.com"));
    EXPECT_EQ(url.path(), QStringLiteral("/doodlebold/DoodleBold/issues/new"));
    EXPECT_EQ(QUrlQuery(url).queryItemValue(QStringLiteral("template")), QStringLiteral("bug_report.yml"));
    EXPECT_TRUE(url.toString().startsWith(links::issuesUrl().toString() + QStringLiteral("/new?")));
}

TEST(ProjectLinks, BugReportFillsTheTemplatesFieldsByTheirIds) {
    const QUrlQuery query(links::bugReportUrl(facts()));
    EXPECT_EQ(query.queryItemValue(QStringLiteral("version")), QStringLiteral("1.2.3"));
    EXPECT_EQ(query.queryItemValue(QStringLiteral("system"), QUrl::FullyDecoded),
              QStringLiteral("Fedora Linux 44, COSMIC, wayland, Qt 6.9.1"));
    EXPECT_EQ(query.queryItemValue(QStringLiteral("tablet"), QUrl::FullyDecoded), QStringLiteral("Wacom Intuos S Pen"));
}

TEST(ProjectLinks, BugReportIsPercentEncodedAndShort) {
    links::SystemFacts f = facts();
    f.system = QStringLiteral("A & B=C #1 ü");
    const QString url = links::bugReportUrl(f).toEncoded();
    EXPECT_TRUE(url.contains(QStringLiteral("system=A%20%26%20B%3DC%20%231%20%C3%BC"))) << url.toStdString();
    EXPECT_FALSE(url.contains(QLatin1Char(' ')));
    EXPECT_LT(url.size(), 400);
}

TEST(ProjectLinks, BugReportLeavesOutWhatIsUnknown) {
    const QString url = links::bugReportUrl({QStringLiteral("1.0.0"), QString(), QString()}).toString();
    EXPECT_TRUE(url.contains(QStringLiteral("version=1.0.0")));
    EXPECT_FALSE(url.contains(QStringLiteral("system=")));
    EXPECT_FALSE(url.contains(QStringLiteral("tablet=")));
}

TEST(ProjectLinks, SystemFactsNameTheVersionTheSystemAndQt) {
    const links::SystemFacts f = links::systemFacts();
    EXPECT_EQ(f.version, QCoreApplication::applicationVersion());
    EXPECT_TRUE(f.system.contains(QStringLiteral("Qt ") + QString::fromLatin1(qVersion())));
    EXPECT_TRUE(f.system.contains(QSysInfo::prettyProductName()));
}

TEST(ProjectLinks, TheOpenerIsReplaceable) {
    QList<QUrl> opened;
    links::setOpener([&opened](const QUrl& url) {
        opened << url;
        return true;
    });
    EXPECT_TRUE(links::open(links::repositoryUrl()));
    links::setOpener({});
    EXPECT_EQ(opened, QList<QUrl>{links::repositoryUrl()});
}
