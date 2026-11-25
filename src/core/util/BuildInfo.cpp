#include "core/util/BuildInfo.h"

namespace doodlebold::build_info {

QString versionText(const QString& version, const QString& commit) {
    return commit.isEmpty() ? version : QStringLiteral("%1 (%2)").arg(version, commit);
}

QString versionText() {
    return versionText(version(), commit());
}

}
