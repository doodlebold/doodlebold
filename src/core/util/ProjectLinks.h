#pragma once

#include <QString>
#include <QUrl>

#include <functional>

namespace doodlebold::links {

QUrl repositoryUrl();
QUrl issuesUrl();

struct SystemFacts {
    QString version;
    QString system;
    QString tablet;
};
SystemFacts systemFacts();

QUrl bugReportUrl(const SystemFacts& facts);

using Opener = std::function<bool(const QUrl&)>;
void setOpener(Opener opener);
bool open(const QUrl& url);

}
