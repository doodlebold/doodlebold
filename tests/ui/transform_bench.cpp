// Frame-time benchmark of a Transform handle drag on a large layer (not a test; run by hand).
//   QT_QPA_PLATFORM=xcb DISPLAY=:97 ./doodlebold_transform_bench [gpu|cpu] [width height steps [out.png]]
// Drives the canvas with the same mouse events as a user: press on the bottom-right corner handle, `steps` moves,
// release. Prints the time per step (event handling + one repaint() + glFinish) for the first step (one-off setup),
// then median / mean / max of the rest, and the release. out.png is a grab of the canvas at the last drag step.
#include "core/document/Document.h"
#include "core/document/TransformSession.h"
#include "core/input/TabletStats.h"
#include "core/tools/ToolBox.h"
#include "core/view/ViewNavigator.h"
#include "ui/CanvasView.h"

#include <QApplication>
#include <QElapsedTimer>
#include <QImage>
#include <QMouseEvent>
#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QPainter>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <numeric>
#include <vector>

using namespace doodlebold;

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    const bool gpu = argc < 2 || std::strcmp(argv[1], "cpu") != 0;
    const int w = argc > 4 ? std::atoi(argv[2]) : 3000, h = argc > 4 ? std::atoi(argv[3]) : 2000;
    const int steps = argc > 4 ? std::atoi(argv[4]) : 60;
    const QString out = argc > 5 ? QString::fromLocal8Bit(argv[5]) : QString();

    TabletStats stats;
    Document doc(QSize(w, h), Qt::white);
    ToolBox tools;
    CanvasView canvas(&doc, &stats);
    canvas.setToolBox(&tools);
    canvas.resize(1200, 800);
    canvas.show();
    QCoreApplication::processEvents();
    canvas.resetView();

    QImage art(w, h, QImage::Format_RGBA8888_Premultiplied);
    art.fill(Qt::transparent);
    {
        QPainter p(&art);
        for (int y = 0; y < h; y += 100)
            for (int x = 0; x < w; x += 100)
                p.fillRect(x, y, 100, 100, ((x / 100 + y / 100) & 1) ? QColor(220, 60, 60) : QColor(60, 90, 220));
        p.setPen(QPen(Qt::black, 6));
        p.drawLine(0, 0, w, h);
        p.drawLine(0, h, w, 0);
    }
    doc.paintTarget(doc.currentLayerId()).value()->writeImage(art, QPoint(0, 0));
    doc.notifyPixelsChanged(doc.currentLayerId(), QRect(0, 0, w, h));

    // BENCH_LAYERS=1: a layer above the transformed one (a green stripe) and 60 % opacity on it, to check the stacking.
    // BENCH_PERSPECTIVE=n: the Perspective slider.
    if (std::getenv("BENCH_LAYERS")) {
        const LayerId base = doc.currentLayerId();
        (void)doc.addLayer();
        doc.paintTarget(doc.currentLayerId()).value()->fill(QRect(0, h / 2 - 60, w, 120), QColor(30, 200, 60));
        doc.notifyPixelsChanged(doc.currentLayerId(), QRect(0, h / 2 - 60, w, 120));
        doc.setCurrentLayer(base);
        LayerProps props = doc.currentLayer()->props;
        props.opacity = 60;
        (void)doc.setLayerProps(base, props);
    }
    if (const char* p = std::getenv("BENCH_PERSPECTIVE")) {
        FixedToolOptions o = tools.fixedOptions();
        o.marquee.perspective = std::atoi(p);
        tools.setFixedOptions(o);
    }
    if (!canvas.beginTransform(TransformMode::Freeform)) {
        std::fprintf(stderr, "cannot begin transform\n");
        return 1;
    }
    TransformSession* s = canvas.transformSession();
    s->setGpuPreview(gpu);
    QCoreApplication::processEvents();
    canvas.repaint();

    const QTransform toWidget = ViewNavigator(doc.view(), QSizeF(canvas.size()), QSizeF(doc.size())).docToWidget();
    const auto send = [&](QEvent::Type type, const QPointF& at0, bool down) {
        const QPointF at = toWidget.map(at0);
        QMouseEvent e(type, at, at, Qt::LeftButton, down ? Qt::LeftButton : Qt::NoButton, {});
        QCoreApplication::sendEvent(&canvas, &e);
    };
    // BENCH_REPAINT=1 measures a window repaint() (adds the X11 flush of the backing store, vsync-bound on some drivers);
    // by default one frame is rendered and read back with grabFramebuffer(): paintGL + GPU work, no window system.
    const bool viaRepaint = std::getenv("BENCH_REPAINT") != nullptr;
    const auto frame = [&] {
        if (viaRepaint)
            canvas.repaint();
        else
            (void)canvas.grabFramebuffer();
        canvas.makeCurrent();
        canvas.context()->functions()->glFinish();
        canvas.doneCurrent();
    };

    QElapsedTimer clock;
    std::vector<double> ms;
    // Baseline: the same frame with nothing changing (display texture, handles, window compositing).
    std::vector<double> idle;
    for (int i = 0; i < 20; ++i) {
        clock.restart();
        frame();
        idle.push_back(double(clock.nsecsElapsed()) / 1e6);
    }
    std::ranges::sort(idle);
    canvas.makeCurrent();
    std::printf("GL: %s | idle frame median %.1f ms\n",
                reinterpret_cast<const char*>(canvas.context()->functions()->glGetString(GL_RENDERER)), idle[idle.size() / 2]);
    canvas.doneCurrent();
    const QPointF corner(w, h);
    send(QEvent::MouseButtonPress, corner, true);
    for (int i = 1; i <= steps; ++i) {
        const double t = double(i) / steps;
        const QPointF to = corner + QPointF(-0.35 * w * t, -0.30 * h * t + 40.0 * std::sin(i * 0.3));
        clock.restart();
        send(QEvent::MouseMove, to, true);
        frame();
        ms.push_back(double(clock.nsecsElapsed()) / 1e6);
    }
    if (!out.isEmpty())
        canvas.grabFramebuffer().save(out);
    clock.restart();
    send(QEvent::MouseButtonRelease, corner, false);
    frame();
    const double release = double(clock.nsecsElapsed()) / 1e6;

    std::vector<double> rest(ms.begin() + 1, ms.end());
    std::ranges::sort(rest);
    const double mean = std::accumulate(rest.begin(), rest.end(), 0.0) / double(rest.size());
    std::printf("%s %dx%d: first step %.1f ms, then median %.1f mean %.1f p95 %.1f max %.1f ms over %zu steps; release %.1f ms; "
                "cpu previews %llu gpu steps %llu\n",
                gpu ? "gpu" : "cpu", w, h, ms.front(), rest[rest.size() / 2], mean, rest[rest.size() * 95 / 100], rest.back(), rest.size(),
                release, (unsigned long long)s->cpuPreviewCount(), (unsigned long long)s->gpuPreviewSteps());
    return 0;
}
