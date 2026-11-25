#include "core/util/UpdateCheck.h"

#include "core/util/ProjectLinks.h"

#include <gtest/gtest.h>

using namespace doodlebold;

namespace {

bool newer(const char* release, const char* running) {
    return update_check::isNewer(QString::fromLatin1(release), QString::fromLatin1(running));
}

QByteArray reply(const char* tag, const char* page) {
    return QStringLiteral(R"({"tag_name": "%1", "html_url": "%2", "draft": false, "prerelease": false})")
        .arg(QString::fromLatin1(tag), QString::fromLatin1(page))
        .toUtf8();
}

}

TEST(UpdateCheck, ReadsReleaseTagsAndThisBuildsVersions) {
    EXPECT_EQ(update_check::parseVersion(QStringLiteral("1.2.3")), (update_check::Version{1, 2, 3}));
    EXPECT_EQ(update_check::parseVersion(QStringLiteral("v1.2.3")), (update_check::Version{1, 2, 3}));
    EXPECT_EQ(update_check::parseVersion(QStringLiteral("0.10.0-dev")), (update_check::Version{0, 10, 0}));
}

TEST(UpdateCheck, AnythingElseIsMalformed) {
    for (const char* text : {"", "v", "1.2", "1.2.3.4", "1.2.x", "1.2.3-beta", "1.2.3-dev2", "V1.2.3", " 1.2.3", "1.2.3 ", "-1.2.3",
                             "99999999999.0.0", "vv1.2.3"})
        EXPECT_FALSE(update_check::parseVersion(QString::fromLatin1(text))) << text;
}

TEST(UpdateCheck, ALaterReleaseIsNewer) {
    EXPECT_TRUE(newer("0.1.1", "0.1.0"));
    EXPECT_TRUE(newer("0.2.0", "0.1.9"));
    EXPECT_TRUE(newer("1.0.0", "0.99.99"));
    EXPECT_FALSE(newer("0.1.0", "0.1.0"));
    EXPECT_FALSE(newer("0.1.0", "0.2.0"));
}

// Part by part as numbers, never as text, where "0.10.0" would sort before "0.9.9".
TEST(UpdateCheck, PartsCompareAsNumbers) {
    EXPECT_TRUE(newer("0.10.0", "0.9.9"));
    EXPECT_FALSE(newer("0.9.9", "0.10.0"));
    EXPECT_TRUE(newer("0.1.10", "0.1.9"));
}

TEST(UpdateCheck, TheTagsVIsIgnored) {
    EXPECT_TRUE(newer("v0.2.0", "0.1.0"));
    EXPECT_FALSE(newer("v0.1.0", "0.1.0"));
}

// A -dev build is work after the release it names: that release is not newer, the next one is.
TEST(UpdateCheck, ADevBuildIsAfterTheReleaseItFollows) {
    EXPECT_FALSE(newer("v0.1.0", "0.1.0-dev"));
    EXPECT_FALSE(newer("0.0.9", "0.1.0-dev"));
    EXPECT_TRUE(newer("v0.1.1", "0.1.0-dev"));
}

TEST(UpdateCheck, AMalformedVersionIsNeverNewer) {
    EXPECT_FALSE(newer("latest", "0.1.0"));
    EXPECT_FALSE(newer("v0.2.0-beta", "0.1.0"));
    EXPECT_FALSE(newer("0.2.0", "unknown"));
    EXPECT_FALSE(newer("", ""));
}

TEST(UpdateCheck, ReadsTheTagAndPageOfTheLatestRelease) {
    const auto release =
        update_check::parseLatestRelease(reply("v0.2.0", "https://github.com/doodlebold/DoodleBold/releases/tag/v0.2.0"));
    ASSERT_TRUE(release);
    EXPECT_EQ(release->version, QStringLiteral("0.2.0"));
    EXPECT_EQ(release->page, QUrl(QStringLiteral("https://github.com/doodlebold/DoodleBold/releases/tag/v0.2.0")));
    const auto untagged = update_check::parseLatestRelease(reply("0.3.1", "https://github.com/doodlebold/DoodleBold/releases/tag/0.3.1"));
    ASSERT_TRUE(untagged);
    EXPECT_EQ(untagged->version, QStringLiteral("0.3.1"));
}

TEST(UpdateCheck, AReplyWithoutAReadableReleaseNamesNone) {
    EXPECT_FALSE(update_check::parseLatestRelease(QByteArray()));
    EXPECT_FALSE(update_check::parseLatestRelease(QByteArrayLiteral("not json")));
    EXPECT_FALSE(update_check::parseLatestRelease(QByteArrayLiteral("[]")));
    // What GitHub answers a repository without releases.
    EXPECT_FALSE(update_check::parseLatestRelease(QByteArrayLiteral(R"({"message": "Not Found", "status": "404"})")));
    EXPECT_FALSE(update_check::parseLatestRelease(QByteArrayLiteral(R"({"tag_name": 2, "html_url": "https://github.com/"})")));
    EXPECT_FALSE(update_check::parseLatestRelease(reply("nightly", "https://github.com/doodlebold/DoodleBold/releases/tag/nightly")));
    EXPECT_FALSE(update_check::parseLatestRelease(reply("v0.2.0", "")));
    EXPECT_FALSE(update_check::parseLatestRelease(reply("v0.2.0", "http://github.com/doodlebold/DoodleBold/releases/tag/v0.2.0")));
}

TEST(UpdateCheck, AsksGitHubForTheRepositorysLatestRelease) {
    const QUrl url = update_check::latestReleaseUrl();
    EXPECT_EQ(url.scheme(), QStringLiteral("https"));
    EXPECT_EQ(url.host(), QStringLiteral("api.github.com"));
    EXPECT_EQ(url.path(), QStringLiteral("/repos") + links::repositoryUrl().path() + QStringLiteral("/releases/latest"));
    EXPECT_EQ(url.path().compare(QStringLiteral("/repos/doodlebold/doodlebold/releases/latest"), Qt::CaseInsensitive), 0);
    EXPECT_FALSE(url.hasQuery());
}

TEST(UpdateCheck, TheUserAgentNamesOnlyTheProgramAndItsVersion) {
    EXPECT_EQ(update_check::userAgent(QStringLiteral("0.1.0-dev")), QByteArrayLiteral("Doodlebold/0.1.0-dev"));
}

TEST(UpdateCheck, TheCheckIsDueOnceADay) {
    const QDateTime now = QDateTime::fromString(QStringLiteral("2026-11-25T12:00:00Z"), Qt::ISODate);
    EXPECT_TRUE(update_check::isDue(QDateTime(), now)) << "never checked";
    EXPECT_FALSE(update_check::isDue(now, now));
    EXPECT_FALSE(update_check::isDue(now.addSecs(-23 * 60 * 60), now));
    EXPECT_TRUE(update_check::isDue(now.addDays(-1), now));
    EXPECT_TRUE(update_check::isDue(now.addDays(-30), now));
    EXPECT_TRUE(update_check::isDue(now.addDays(2), now)) << "a clock set back";
}
