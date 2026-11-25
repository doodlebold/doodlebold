#pragma once

#include <QByteArray>
#include <QDateTime>
#include <QString>
#include <QUrl>

#include <compare>
#include <optional>

// Whether a newer Doodlebold has been released, read from GitHub's latest release. It only tells: nothing is downloaded.
namespace doodlebold::update_check {

struct Version {
    int major = 0;
    int minor = 0;
    int patch = 0;
    auto operator<=>(const Version&) const = default;
};

// "X.Y.Z" or "vX.Y.Z", as releases are tagged, or "X.Y.Z-dev", as builds of unreleased work are named; nullopt for anything else.
std::optional<Version> parseVersion(const QString& text);

// Whether `release` is newer than `running`, this build's version. Malformed versions are never newer.
bool isNewer(const QString& release, const QString& running);

struct Release {
    QString version;  // as tagged, without the tag's "v"
    QUrl page;        // the release's page on GitHub
};

// The release a reply of GitHub's latest-release endpoint names; nullopt when it names none that can be read.
std::optional<Release> parseLatestRelease(const QByteArray& json);

// GitHub's latest-release endpoint for links::repositoryUrl().
QUrl latestReleaseUrl();

// The User-Agent GitHub requires: the program and its version, and nothing that tells who runs it.
QByteArray userAgent(const QString& version);

// Whether the check at start is due, given when it last ran (an invalid time if never).
bool isDue(const QDateTime& last, const QDateTime& now);

}
