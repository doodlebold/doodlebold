#pragma once

#include <QString>

// So About and bug reports name the exact build.
namespace doodlebold::build_info {

QString version();
QString commit();

QString versionText(const QString& version, const QString& commit);
QString versionText();

}
