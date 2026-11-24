#include "core/util/ProjectLinks.h"

#include <QCoreApplication>
#include <QDesktopServices>
#include <QGuiApplication>
#include <QInputDevice>
#include <QSysInfo>

namespace doodlebold::links {

namespace {

Opener& opener() {
    static Opener o;
    return o;
}

QString tabletNames() {
    QStringList names;
    for (const QInputDevice* device : QInputDevice::devices())
        if (device->type() == QInputDevice::DeviceType::Stylus && !names.contains(device->name()))
            names << device->name();
    return names.join(QStringLiteral(", "));
}

}

QUrl repositoryUrl() {
    return QUrl(QStringLiteral("https://github.com/doodlebold/DoodleBold"));
}

QUrl issuesUrl() {
    return QUrl(repositoryUrl().toString() + QStringLiteral("/issues"));
}

SystemFacts systemFacts() {
    QStringList system{QSysInfo::prettyProductName()};
    const QString desktop = qEnvironmentVariable("XDG_CURRENT_DESKTOP");
    if (!desktop.isEmpty())
        system << desktop;
    system << QGuiApplication::platformName() << QStringLiteral("Qt ") + QString::fromLatin1(qVersion());
    return {QCoreApplication::applicationVersion(), system.join(QStringLiteral(", ")), tabletNames()};
}

QUrl bugReportUrl(const SystemFacts& facts) {
    QString url = repositoryUrl().toString() + QStringLiteral("/issues/new?template=bug_report.yml");
    const auto field = [&url](const char* id, const QString& value) {
        if (!value.isEmpty())
            url += QLatin1Char('&') + QLatin1String(id) + QLatin1Char('=') + QString::fromLatin1(QUrl::toPercentEncoding(value));
    };
    field("version", facts.version);
    field("system", facts.system);
    field("tablet", facts.tablet);
    return QUrl::fromEncoded(url.toLatin1());
}

void setOpener(Opener o) {
    opener() = std::move(o);
}

bool open(const QUrl& url) {
    return opener() ? opener()(url) : QDesktopServices::openUrl(url);
}

}
