// How long a new canvas takes, from pressing OK in the New Canvas dialog to its first picture on screen, in the app's own window. Not a
// test: run by hand, on a display of its own, with HOME and the XDG folders pointing somewhere new, so it touches nothing of the painter's:
//   QT_QPA_PLATFORM=xcb HOME=<empty dir> xvfb-run -a doodlebold_new_canvas_bench <side px> [<folder for pictures of the dialog>]
// It prints the time, and the longest time the event loop went without running (how long the window was frozen).
#include "core/document/Document.h"
#include "ui/CanvasView.h"
#include "ui/DocumentTabs.h"
#include "ui/MainWindow.h"
#include "ui/NewCanvasDialog.h"
#include "platform/Platform.h"

#include <QAction>
#include <QApplication>
#include <QDialogButtonBox>
#include <QElapsedTimer>
#include <QLineEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QStandardPaths>
#include <QTimer>

#include <cstdio>
#include <cstdlib>

using namespace doodlebold;

int main(int argc, char** argv) {
    QCoreApplication::setAttribute(Qt::AA_CompressTabletEvents, false);
    QSurfaceFormat::setDefaultFormat(platform::defaultSurfaceFormat());
    QApplication app(argc, argv);
    QStandardPaths::setTestModeEnabled(true);
    if (argc < 2) {
        std::fprintf(stderr, "usage: %s <side px> [<folder for pictures>]\n", argv[0]);
        return 2;
    }
    const QString side = QString::fromLocal8Bit(argv[1]);
    const QString shots = argc > 2 ? QString::fromLocal8Bit(argv[2]) : QString();
    MainWindow window;
    window.resize(1280, 800);
    window.show();
    QElapsedTimer settling;
    settling.start();
    while (settling.elapsed() < 800)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);

    QAction* newCanvas = nullptr;
    for (QAction* action : window.findChildren<QAction*>())
        if (action->objectName() == QLatin1String("file.new"))
            newCanvas = action;
    if (!newCanvas) {
        std::fprintf(stderr, "no New action\n");
        return 2;
    }
    const int before = window.tabs()->count();
    QElapsedTimer sinceOk;
    bool pressed = false, shown = false;
    qint64 firstPicture = -1, longestStall = 0, sawIndicatorAt = -1;
    int picture = 0;
    QElapsedTimer tick;
    tick.start();
    QTimer poll;
    poll.setTimerType(Qt::PreciseTimer);
    QObject::connect(&poll, &QTimer::timeout, [&] {
        const qint64 gap = tick.restart();
        if (pressed && firstPicture < 0)
            longestStall = std::max(longestStall, gap);
        auto* dialog = qobject_cast<NewCanvasDialog*>(QApplication::activeModalWidget());
        if (dialog && !pressed) {
            auto* width = dialog->findChild<QLineEdit*>(QStringLiteral("widthEdit"));
            auto* height = dialog->findChild<QLineEdit*>(QStringLiteral("heightEdit"));
            const auto type = [&](QLineEdit* edit) {
                edit->setText(side);
                emit edit->textEdited(side);
            };
            type(width);
            type(height);
            QCoreApplication::processEvents();
            pressed = true;
            sinceOk.start();
            dialog->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();
            return;
        }
        if (dialog && pressed && firstPicture < 0) {
            if (const auto* bar = dialog->findChild<QProgressBar*>(QStringLiteral("newCanvasWorking"));
                bar && bar->isVisible() && sawIndicatorAt < 0)
                sawIndicatorAt = sinceOk.elapsed();
            if (!shots.isEmpty() && picture < 6 && sinceOk.elapsed() > picture * 400)
                dialog->grab().save(QStringLiteral("%1/dialog-%2.png").arg(shots).arg(picture++));
        }
        if (pressed && !shown && window.tabs()->count() > before) {
            shown = true;
            QObject::connect(window.tabs()->currentCanvas(), &QOpenGLWidget::frameSwapped, &app, [&] {
                if (firstPicture < 0)
                    firstPicture = sinceOk.elapsed();
            });
        }
    });
    poll.start(5);
    QTimer::singleShot(0, newCanvas, &QAction::trigger);
    QElapsedTimer limit;
    limit.start();
    while (firstPicture < 0 && limit.elapsed() < 120000)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    poll.stop();
    if (!shots.isEmpty())
        window.grab().save(shots + QStringLiteral("/window-after.png"));
    if (const Document* doc = window.tabs()->currentDocument())
        std::printf("canvas %d x %d\n", doc->size().width(), doc->size().height());
    std::printf("side %s: OK to first picture %lld ms; longest freeze %lld ms; indicator seen at %lld ms\n", qPrintable(side),
                static_cast<long long>(firstPicture), static_cast<long long>(longestStall), static_cast<long long>(sawIndicatorAt));
    return firstPicture < 0;
}
