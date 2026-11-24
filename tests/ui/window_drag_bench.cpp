// Drags a mouse across the canvas of the real main window, panels and all, and prints how long the main thread went without getting
// back to its event loop. Not a test: run by hand, on a display of its own, with HOME pointing somewhere new, so that it touches
// nothing of the painter's.
//   QT_QPA_PLATFORM=xcb HOME=<empty dir> xvfb-run -a doodlebold_window_drag_bench <size px> <seconds> <events per second>
#include "core/document/Document.h"
#include "core/tools/ToolBox.h"
#include "core/view/ViewNavigator.h"
#include "ui/CanvasView.h"
#include "ui/MainWindow.h"

#include <QApplication>
#include <QElapsedTimer>
#include <QMouseEvent>
#include <QStandardPaths>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

using namespace doodlebold;

namespace {

double percentile(std::vector<double> values, double share) {
    if (values.empty())
        return 0.0;
    std::ranges::sort(values);
    return values[static_cast<std::size_t>(share * static_cast<double>(values.size() - 1))];
}

void sendMouse(CanvasView& canvas, QEvent::Type type, const QPointF& docPos, Qt::MouseButton button, Qt::MouseButtons buttons) {
    Document& doc = *canvas.document();
    const QPointF at = ViewNavigator(doc.view(), QSizeF(canvas.size()), QSizeF(doc.size())).docToWidget().map(docPos);
    QMouseEvent event(type, at, canvas.mapToGlobal(at), button, buttons, Qt::NoModifier);
    QCoreApplication::sendEvent(&canvas, &event);
}

}

int main(int argc, char** argv) {
    if (argc != 4) {
        std::fprintf(stderr, "usage: %s <size px> <seconds> <events per second>\n", argv[0]);
        return 2;
    }
    QApplication app(argc, argv);
    QStandardPaths::setTestModeEnabled(true);
    MainWindow window;
    window.newDocument();
    window.resize(1500, 950);
    window.show();
    QCoreApplication::processEvents();
    auto* canvas = window.findChild<CanvasView*>();
    if (!canvas) {
        std::fprintf(stderr, "no canvas\n");
        return 1;
    }
    BrushSettings settings = window.tools()->settings();
    settings.size = static_cast<int>(std::atof(argv[1]) * 10.0);
    window.tools()->setSettings(settings);
    const QSize size = canvas->document()->size();
    std::printf("canvas %dx%d, zoom %.3f, tool %d, stabilizer %d\n", size.width(), size.height(), canvas->document()->view().zoom,
                static_cast<int>(settings.tool), window.tools()->stabilizerLevel());

    const auto placeAt = [&size](double t) {
        return QPointF(size.width() * (0.5 + 0.35 * std::sin(t * 2.3)), size.height() * (0.5 + 0.35 * std::sin(t * 3.1)));
    };
    const qint64 lengthMs = static_cast<qint64>(std::atof(argv[2]) * 1000.0);
    const double perSecond = std::atof(argv[3]);
    std::vector<double> gaps, handling;
    QElapsedTimer clock;
    clock.start();
    sendMouse(*canvas, QEvent::MouseButtonPress, placeAt(0.0), Qt::LeftButton, Qt::LeftButton);
    qint64 sent = 0, lastLoop = clock.nsecsElapsed();
    while (clock.elapsed() < lengthMs) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 1);
        const qint64 nowNs = clock.nsecsElapsed();
        gaps.push_back(static_cast<double>(nowNs - lastLoop) / 1e6);
        while (sent < static_cast<qint64>(static_cast<double>(clock.elapsed()) * perSecond / 1000.0)) {
            ++sent;
            QElapsedTimer one;
            one.start();
            sendMouse(*canvas, QEvent::MouseMove, placeAt(static_cast<double>(sent) / perSecond), Qt::NoButton, Qt::LeftButton);
            handling.push_back(static_cast<double>(one.nsecsElapsed()) / 1e6);
        }
        lastLoop = clock.nsecsElapsed();
    }
    QElapsedTimer lift;
    lift.start();
    sendMouse(*canvas, QEvent::MouseButtonRelease, placeAt(static_cast<double>(sent) / perSecond), Qt::LeftButton, Qt::NoButton);
    const double liftMs = static_cast<double>(lift.nsecsElapsed()) / 1e6;
    QElapsedTimer catchUp;
    catchUp.start();
    double worstCatchUpGap = 0.0;
    while (canvas->isPainting() && catchUp.elapsed() < 120000) {
        const qint64 before = catchUp.nsecsElapsed();
        QCoreApplication::processEvents(QEventLoop::AllEvents, 1);
        worstCatchUpGap = std::max(worstCatchUpGap, static_cast<double>(catchUp.nsecsElapsed() - before) / 1e6);
    }
    std::printf("%lld events. Event loop gap ms: median %.1f, 99%% %.1f, worst %.1f. Handling one event ms: median %.2f, worst %.1f.\n",
                sent, percentile(gaps, 0.5), percentile(gaps, 0.99), percentile(gaps, 1.0), percentile(handling, 0.5),
                percentile(handling, 1.0));
    std::printf("Lift took %.1f ms, then painting went on %.0f ms, with the event loop's worst gap %.1f ms.\n", liftMs,
                static_cast<double>(catchUp.elapsed()), worstCatchUpGap);
    return 0;
}
