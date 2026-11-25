#include "core/util/UpdateCheck.h"

#include "core/util/ProjectLinks.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>

namespace doodlebold::update_check {

namespace {

// s: a day. Releases come weeks apart, so hearing of one a day late loses nothing, and starting Doodlebold many times a day asks
// GitHub once.
constexpr qint64 kCheckEverySeconds = 24 * 60 * 60;

// A part too long for an int is malformed, not wrapped round into a smaller version.
std::optional<int> partOf(const QString& digits) {
    bool ok = false;
    const int value = digits.toInt(&ok);
    return ok ? std::optional(value) : std::nullopt;
}

}

std::optional<Version> parseVersion(const QString& text) {
    static const QRegularExpression form(QStringLiteral("^v?(\\d+)\\.(\\d+)\\.(\\d+)(-dev)?$"));
    const QRegularExpressionMatch match = form.match(text);
    if (!match.hasMatch())
        return std::nullopt;
    const std::optional<int> major = partOf(match.captured(1));
    const std::optional<int> minor = partOf(match.captured(2));
    const std::optional<int> patch = partOf(match.captured(3));
    if (!major || !minor || !patch)
        return std::nullopt;
    return Version{*major, *minor, *patch};
}

// The numbers alone decide: X.Y.Z-dev is work after X.Y.Z, so the release X.Y.Z is not newer than it, and X.Y.(Z+1) is.
bool isNewer(const QString& release, const QString& running) {
    const std::optional<Version> released = parseVersion(release);
    const std::optional<Version> runs = parseVersion(running);
    return released && runs && *released > *runs;
}

std::optional<Release> parseLatestRelease(const QByteArray& json) {
    const QJsonObject release = QJsonDocument::fromJson(json).object();
    QString tag = release.value(QLatin1String("tag_name")).toString();
    const QUrl page(release.value(QLatin1String("html_url")).toString());
    if (!parseVersion(tag) || page.scheme() != QLatin1String("https"))
        return std::nullopt;
    if (tag.startsWith(QLatin1Char('v')))
        tag.remove(0, 1);
    return Release{tag, page};
}

QUrl latestReleaseUrl() {
    return QUrl(QStringLiteral("https://api.github.com/repos") + links::repositoryUrl().path() + QStringLiteral("/releases/latest"));
}

QByteArray userAgent(const QString& version) {
    return QByteArrayLiteral("Doodlebold/") + version.toUtf8();
}

// A clock set back leaves `last` in the future: due, or it wouldn't check again until the clock caught up.
bool isDue(const QDateTime& last, const QDateTime& now) {
    return !last.isValid() || last > now || last.secsTo(now) >= kCheckEverySeconds;
}

}
