#include "core/util/BuildInfo.h"

#include <QRegularExpression>

#include <gtest/gtest.h>

using namespace doodlebold;

TEST(BuildInfo, VersionTextNamesTheCommitAfterTheVersion) {
    EXPECT_EQ(build_info::versionText(QStringLiteral("1.2.0"), QStringLiteral("abc1234")), QStringLiteral("1.2.0 (abc1234)"));
    EXPECT_EQ(build_info::versionText(QStringLiteral("1.2.0-dev"), QStringLiteral("abc1234")),
              QStringLiteral("1.2.0-dev (abc1234)"));
}

// A build from a source archive has neither git nor GITHUB_SHA, and "1.2.0 ()" would read as a broken build.
TEST(BuildInfo, VersionTextWithoutACommitIsTheVersionAlone) {
    EXPECT_EQ(build_info::versionText(QStringLiteral("1.2.0"), QString()), QStringLiteral("1.2.0"));
}

TEST(BuildInfo, ThisBuildsVersionIsAReleaseOrADevVersion) {
    const QRegularExpression form(QStringLiteral("^\\d+\\.\\d+\\.\\d+(-dev)?$"));
    EXPECT_TRUE(form.match(build_info::version()).hasMatch()) << build_info::version().toStdString();
}

TEST(BuildInfo, ThisBuildsCommitIsAShortHashOrNothing) {
    const QRegularExpression form(QStringLiteral("^([0-9a-f]{7,40})?$"));
    EXPECT_TRUE(form.match(build_info::commit()).hasMatch()) << build_info::commit().toStdString();
    EXPECT_EQ(build_info::versionText(), build_info::versionText(build_info::version(), build_info::commit()));
}
