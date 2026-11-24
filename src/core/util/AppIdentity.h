#pragma once

#include <QCoreApplication>
#include <QSettings>
#include <QString>

namespace doodlebold::app_identity {

// Where this build keeps its settings and files: QSettings and every QStandardPaths location are found through these two names,
// so changing them moves everything. They are kept apart from any other install of the program on the same computer.
inline constexpr const char* kOrganization = "DoodleboldPaint";
inline constexpr const char* kApplication = "Doodlebold";

// For QStandardPaths: called once, first thing in main().
inline void apply() {
    QCoreApplication::setOrganizationName(QString::fromLatin1(kOrganization));
    QCoreApplication::setApplicationName(QString::fromLatin1(kApplication));
}

inline QSettings settings() {
    return QSettings(QSettings::defaultFormat(), QSettings::UserScope, QString::fromLatin1(kOrganization),
                     QString::fromLatin1(kApplication));
}

}
