// Frame-time benchmark of a Selection Pen stroke over a canvas that is all selected (not a test; run by hand).
//   QT_QPA_PLATFORM=xcb DISPLAY=:97 ./doodlebold_selection_pen_bench [width height steps [out.png]]
// Drives the canvas with the same mouse events as a user: press, `steps` moves, release, each followed by one rendered
// frame (grabFramebuffer + glFinish),
// and prints the median / mean / max time per move and the release. out.png: afterwards a Selection Eraser stroke across
// the canvas and a grab of the result (the tint with the erased band), to check what is drawn.
#include "core/document/Document.h"
#include "core/input/TabletStats.h"
#include "core/tools/ToolBox.h"
#include "core/view/ViewNavigator.h"
#include "ui/CanvasView.h"

#include <QApplication>
#include <QElapsedTimer>
#include <QMouseEvent>
#include <QOpenGLContext>
#include <QOpenGLFunctions>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <numeric>
#include <vector>

using namespace doodlebold;

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    const int w = argc > 3 ? std::atoi(argv[1]) : 3000, h = argc > 3 ? std::atoi(argv[2]) : 2000;
    const int steps = argc > 3 ? std::atoi(argv[3]) : 120;
    const QString out = argc > 4 ? QString::fromLocal8Bit(argv[4]) : QString();

    TabletStats stats;
    Document doc(QSize(w, h), Qt::white);
    ToolBox tools;
    tools.selectOrCreateToolOfType(ToolType::SelectionPen);
    CanvasView canvas(&doc, &stats);
    canvas.setToolBox(&tools);
    canvas.resize(1200, 800);
    canvas.show();
    QCoreApplication::processEvents();
    canvas.resetView();
    if (!doc.selectAll()) {
        std::fprintf(stderr, "cannot select all\n");
        return 1;
    }
    QCoreApplication::processEvents();

    const auto frame = [&] {
        (void)canvas.grabFramebuffer();
        canvas.makeCurrent();
        canvas.context()->functions()->glFinish();
        canvas.doneCurrent();
    };
    const QTransform toWidget = ViewNavigator(doc.view(), QSizeF(canvas.size()), QSizeF(doc.size())).docToWidget();
    const auto send = [&](QEvent::Type type, const QPointF& doc0, bool down) {
        const QPointF at = toWidget.map(doc0);
        QMouseEvent e(type, at, at, Qt::LeftButton, down ? Qt::LeftButton : Qt::NoButton, {});
        QCoreApplication::sendEvent(&canvas, &e);
    };

    QElapsedTimer clock;
    std::vector<double> idle;
    for (int i = 0; i < 20; ++i) {
        clock.restart();
        frame();
        idle.push_back(double(clock.nsecsElapsed()) / 1e6);
    }
    std::ranges::sort(idle);
    canvas.makeCurrent();
    std::printf("GL: %s | canvas %d x %d | idle frame median %.2f ms\n",
                reinterpret_cast<const char*>(canvas.context()->functions()->glGetString(GL_RENDERER)), w, h, idle[idle.size() / 2]);
    canvas.doneCurrent();

    std::vector<double> ms;
    clock.restart();
    send(QEvent::MouseButtonPress, QPointF(w * 0.2, h * 0.5), true);
    frame();
    const double first = double(clock.nsecsElapsed()) / 1e6;
    for (int i = 1; i <= steps; ++i) {
        const double t = double(i) / steps;
        clock.restart();
        send(QEvent::MouseMove, QPointF(w * (0.2 + 0.6 * t), h * (0.5 + 0.1 * std::sin(t * 6.28))), true);
        frame();
        ms.push_back(double(clock.nsecsElapsed()) / 1e6);
    }
    clock.restart();
    send(QEvent::MouseButtonRelease, QPointF(w * 0.8, h * 0.5), false);
    frame();
    const double release = double(clock.nsecsElapsed()) / 1e6;
    std::vector<double> sorted = ms;
    std::ranges::sort(sorted);
    std::printf("press %.2f ms | move median %.2f mean %.2f max %.2f ms (%d moves) | release %.2f ms\n", first, sorted[sorted.size() / 2],
                std::accumulate(ms.begin(), ms.end(), 0.0) / double(ms.size()), sorted.back(), steps, release);
    if (!out.isEmpty()) {
        tools.selectOrCreateToolOfType(ToolType::SelectionEraser);
        BrushSettings s = tools.settings();
        s.size = 2000;
        tools.setSettings(s);
        send(QEvent::MouseButtonPress, QPointF(w * 0.1, h * 0.3), true);
        for (int i = 1; i <= 40; ++i)
            send(QEvent::MouseMove, QPointF(w * (0.1 + 0.8 * i / 40.0), h * 0.3), true);
        (void)canvas.grabFramebuffer().save(out + QStringLiteral(".during.png"));
        send(QEvent::MouseButtonRelease, QPointF(w * 0.9, h * 0.3), false);
        QCoreApplication::processEvents();
        (void)canvas.grabFramebuffer().save(out);
    }
    return 0;
}
