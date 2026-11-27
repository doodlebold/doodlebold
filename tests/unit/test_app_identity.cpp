#include "core/brush/BrushResources.h"
#include "core/options/AppOptions.h"
#include "core/tools/ToolBoxStore.h"
#include "core/util/AppIdentity.h"

#include <QCoreApplication>
#include <QDir>
#include <QStandardPaths>

#include <gtest/gtest.h>

using namespace doodlebold;

namespace {

// The identity of the installed program, whose settings and files this build must never read or write.
const QString kOtherInstall = QStringLiteral("/Doodlebold/Doodlebold");

}

TEST(AppIdentity, IsNotTheInstalledProgramsAndEverySettingUsesIt) {
    EXPECT_STREQ(app_identity::kOrganization, "DoodleboldPaint");
    EXPECT_STREQ(app_identity::kApplication, "Doodlebold");
    EXPECT_STRNE(app_identity::kOrganization, "Doodlebold");
    EXPECT_EQ(app_identity::settings().organizationName(), QStringLiteral("DoodleboldPaint"));
    EXPECT_EQ(AppOptions::settings().organizationName(), QStringLiteral("DoodleboldPaint"));
#ifdef Q_OS_WIN
    // Windows keeps the settings in the registry, which the file name names.
    const QString settingsAt = QStringLiteral("/Software/DoodleboldPaint/Doodlebold");
#else
    const QString settingsAt = QStringLiteral("/DoodleboldPaint/Doodlebold.conf");
#endif
    EXPECT_TRUE(QDir::fromNativeSeparators(AppOptions::settings().fileName()).endsWith(settingsAt))
        << AppOptions::settings().fileName().toStdString();
}

TEST(AppIdentity, ThePathsOfTheFilesFollowIt) {
    const QString organization = QCoreApplication::organizationName(), application = QCoreApplication::applicationName();
    app_identity::apply();
    EXPECT_TRUE(ToolBoxStore::defaultPath().contains(QStringLiteral("DoodleboldPaint"))) << ToolBoxStore::defaultPath().toStdString();
    for (const QString& path : {ToolBoxStore::defaultPath(), BrushResourceLibrary::defaultFolder(),
                                QStandardPaths::writableLocation(QStandardPaths::AppDataLocation),
                                QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation),
                                QStandardPaths::writableLocation(QStandardPaths::CacheLocation)})
        EXPECT_FALSE(QDir::fromNativeSeparators(path).contains(kOtherInstall)) << path.toStdString();
    QCoreApplication::setOrganizationName(organization);
    QCoreApplication::setApplicationName(application);
}
