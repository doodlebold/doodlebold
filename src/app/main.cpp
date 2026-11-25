#include "core/io/PluginLoader.h"
#include "core/options/AppOptions.h"
#include "core/util/AppIdentity.h"
#include "core/util/BuildInfo.h"
#include "platform/CrashReport.h"
#include "platform/Platform.h"
#include "ui/GlassPopups.h"
#include "ui/MainWindow.h"
#include "ui/Theme.h"

#include <QApplication>
#include <QMessageBox>
#include <QOpenGLContext>
#include <QTimer>

int main(int argc, char* argv[]) {
    // Every tablet event must reach the stroke.
    QCoreApplication::setAttribute(Qt::AA_CompressTabletEvents, false);

    QSurfaceFormat::setDefaultFormat(doodlebold::platform::defaultSurfaceFormat());

    QApplication app(argc, argv);
    doodlebold::app_identity::apply();
    QApplication::setApplicationDisplayName(QStringLiteral("Doodlebold"));
    // The commit too, so About, bug reports and tablet reports name the exact build.
    QApplication::setApplicationVersion(doodlebold::build_info::versionText());
    QApplication::setDesktopFileName(QStringLiteral("doodlebold"));
    doodlebold::crash_report::install();  // once the version and the app's data folder are known

    doodlebold::platform::routeAltSpaceToWidgets();
    doodlebold::platform::turnOffPenGestures();
    // Options > Performance > Frosted menus, known before the first menu is made; MainWindow follows its changes.
    doodlebold::GlassPopups::setFrosted(doodlebold::AppOptions().values().frostedMenus);
    doodlebold::Theme::apply(app);

    if (!doodlebold::platform::processorIsSupported()) {
        QMessageBox::critical(nullptr, QApplication::applicationDisplayName(),
                              QCoreApplication::translate("main",
                                                          "Doodlebold needs a processor with AVX2, and this one does not have it.\n\n"
                                                          "Most PCs made since 2013 (Intel) or 2015 (AMD) have it."));
        return 1;
    }

    // A driver without OpenGL 3.3 still gives a context, of a lower version, in which no canvas can be drawn.
    if (QOpenGLContext gl; !gl.create() || !doodlebold::platform::canDrawTheCanvas(gl.format())) {
        QMessageBox::critical(
            nullptr, QApplication::applicationDisplayName(),
            QCoreApplication::translate("main", "Doodlebold needs OpenGL 3.3 to draw the canvas, and this computer does not offer it.\n\n"
                                                "Installing the latest driver for your graphics card usually fixes this. Virtual "
                                                "machines and Remote Desktop sessions often do not have it."));
        return 1;
    }

    // Format plugins join Open and Save before the window lists the formats.
    const auto plugins = doodlebold::io::loadFormatPlugins(doodlebold::FormatRegistry::instance());

    doodlebold::MainWindow window;
    window.show();
    if (!plugins.notes.isEmpty())
        QTimer::singleShot(0, &window, [&window, notes = plugins.notes] {
            QMessageBox::information(&window, QCoreApplication::translate("main", "Plugins"), notes.join(QStringLiteral("\n\n")));
        });
    if (const QString crashed = doodlebold::crash_report::takeNewReport(); !crashed.isEmpty())
        QTimer::singleShot(0, &window, [&window, crashed] { window.showCrashReport(crashed, true); });
    window.openFiles(QApplication::arguments().mid(1));
    window.checkForUpdatesAtStart();
    const int status = QApplication::exec();
    doodlebold::crash_report::uninstall();
    return status;
}
