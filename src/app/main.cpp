#include "core/io/PluginLoader.h"
#include "core/options/AppOptions.h"
#include "core/util/AppIdentity.h"
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
    QApplication::setApplicationVersion(QStringLiteral(DOODLEBOLD_VERSION));
    QApplication::setDesktopFileName(QStringLiteral("doodlebold"));

    doodlebold::platform::routeAltSpaceToWidgets();
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
    window.openFiles(QApplication::arguments().mid(1));
    return QApplication::exec();
}
