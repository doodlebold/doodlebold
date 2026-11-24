#pragma once

#include "ui/Theme.h"

#include <QApplication>
#include <QDir>
#include <QPixmap>
#include <QString>

namespace doodlebold::test {

// Screenshots of widgets for visual review. Tests always render them (so painting code runs), but only write PNGs
// when DOODLEBOLD_SCREENSHOT_DIR names a directory, e.g.
//   DOODLEBOLD_SCREENSHOT_DIR=/tmp/shots ctest --test-dir build -R doodlebold_ui_tests
// Returns false only if a requested file could not be written.
inline bool saveScreenshot(const QPixmap& shot, const QString& name) {
    const QString dir = qEnvironmentVariable("DOODLEBOLD_SCREENSHOT_DIR");
    if (dir.isEmpty())
        return true;
    QDir().mkpath(dir);
    return shot.save(QDir(dir).filePath(name + QStringLiteral(".png")));
}

inline void applyTheme() {
    // Doodlebold's own theme, so screenshots do not depend on the desktop's; DOODLEBOLD_SCREENSHOT_THEME=auto shows the desktop's
    // (the COSMIC theme where there is one), for looking at it by hand.
    const bool desktop = qEnvironmentVariable("DOODLEBOLD_SCREENSHOT_THEME") == QLatin1String("auto");
    Theme::apply(*qobject_cast<QApplication*>(QCoreApplication::instance()), desktop ? Theme::Source::Auto : Theme::Source::BuiltIn);
}

}
