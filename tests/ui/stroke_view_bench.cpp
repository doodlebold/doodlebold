// Plays a recording's strokes into the real canvas, in real time, as pen events, and prints what the app's own timer saw. Not a
// test: run by hand, with HOME and the XDG folders pointing somewhere new, so that it touches nothing of the painter's. On a display
// of its own:
//   QT_QPA_PLATFORM=xcb HOME=<empty dir> DOODLEBOLD_TIMING_LOG=<file> xvfb-run -a doodlebold_stroke_view_bench <log> <sizes px, a,b,c> <canvas w> <canvas h> <strokes>
// or in a window on the painter's own Wayland desktop, which it only shows a window on (the pen events are made up, not the tablet's):
//   QT_QPA_PLATFORM=wayland HOME=<empty dir> XDG_CONFIG_HOME=<empty dir>/config XDG_DATA_HOME=<empty dir>/data
//     XDG_CACHE_HOME=<empty dir>/cache DOODLEBOLD_TIMING_LOG=<file> doodlebold_stroke_view_bench ...
// <log> may be "hard", a pen stroke pressed hard and lifted fast (hardPress), or "long": one scribble of BENCH_SECONDS (6), a pen event every BENCH_EVENT_MS (8), over the canvas's middle, instead of
// a recording's strokes. BENCH_BUCKETS=1 prints, for every 2 s of each stroke, how long the pen events took to handle, how far the
// app's loop was behind the pen and how many frames it drew, so that a cost growing over a stroke shows.
// BENCH_STROKE=<n> plays only the log's nth pen stroke (from 0), <strokes> times, at the sizes in turn; BENCH_AS_RECORDED=1 paints it
// where it was drawn, where otherwise its first place goes to the canvas's middle (for a log drawn on a canvas of the same size).
// BENCH_LAYER=<png> paints on that picture, the canvas's size, as its layer over the white, where it starts empty otherwise.
// BENCH_TOOL (pencil, brush, marker, water, eraser, blur, smudge), BENCH_MINSIZE (Min Size, %), BENCH_FORM, BENCH_TEXTURE (a built-in picture's name),
// BENCH_FORM_STRENGTH and BENCH_TEXTURE_STRENGTH (their Strength, %; unset: the tool's starting values),
// BENCH_STABILIZER (a level) and BENCH_ZOOM (instead of fitting the canvas in the view) set what it paints with; BENCH_FAST=1 shows it zoomed out without mipmaps, 0 with (unset: as the app starts);
// BENCH_WINDOW_SIZE=<w>x<h> sizes the window, and BENCH_LIFT_WATCH=1 measures the event loop's stalls for a second after each lift and lists what it handled then that took BENCH_WATCH_MS (1) or more, BENCH_STROKE_WATCH=1 lists what it handled while the pen was down, BENCH_HOVER=1 hovers the pen meanwhile and BENCH_LEAVE_MS=<ms> takes it out of range that long after the lift, BENCH_TIMELINE=1 shows the timeline; BENCH_VIEW=<w>x<h> sizes the view (a small one makes a virtual display's copying of every frame cheap).
// BENCH_CORES=<n> paints with n cores (as taskset does, since the app counts only the cores it may run on), BENCH_GO_MS=<ms> sets
// how long the stroke's thread lays for between merges (StrokeWorker, 30).
// BENCH_SCALE=<factor> multiplies the pen's places by that factor and paints them where they then fall, for a stroke drawn on a smaller canvas to cross this one.
// BENCH_SHOTS=<dir> saves the view of what was painted at a few zooms and angles (saveShots), BENCH_MAX_TEXTURE=<px> cuts the canvas's
// textures as graphics with that limit would.
// BENCH_TITLE_SHOT=<png>, with BENCH_WINDOW, adds a new canvas of the size given at BENCH_DPI (350), in a window of its own beside the
// first, fitted, and saves a picture of the app's window with it, for its title.
// BENCH_ZOOM_SWEEP=1, once the strokes are painted, zooms from BENCH_SWEEP_FROM (0.046) up to 1 and back down, a wheel notch at a time
// about the view's middle, and prints each step's zoom and how long it took from the zoom's change to its frame shown and the GPU done
// (zoomSweep). BENCH_SWEEP_WAIT_MS=<ms> lets the app run that long first, as a painter might look before zooming,
// and BENCH_SWEEP_SHOTS=<dir> saves each step's view there as step-<n>.png, to compare two builds' pixels (it slows the steps). With DOODLEBOLD_TIMING_UPDATES=1 the canvas's own notes of each frame's parts come on stderr between them.
#include "core/brush/BrushResources.h"
#include "core/document/Document.h"
#include "core/input/TabletStats.h"
#include "core/stroke/StrokeRecording.h"
#include "core/stroke/StrokeWorker.h"

#include <QFile>
#include "core/tools/ToolBox.h"
#include "core/util/CpuCores.h"
#include "core/view/ViewNavigator.h"
#include "platform/Platform.h"
#include "ui/CanvasView.h"
#include "ui/DocumentTabs.h"
#include "ui/MainWindow.h"
#include "ui/panels/LayerTreeModel.h"

#include <QAction>
#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QImage>
#include <QMdiSubWindow>
#include <QMetaEnum>
#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QPointingDevice>
#include <QStandardPaths>
#include <QStringList>
#include <QTabletEvent>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <iterator>
#include <map>
#include <memory>
#include <utility>
#include <vector>

using namespace doodlebold;

namespace {

// BENCH_DEVICE=pen sends the events from a stylus, as a tablet does, and BENCH_DEVICE=mouse as mouse events. Otherwise they're tablet
// events from the core pointer, which the app takes for a mouse: full pressure, and none of a pen's start or lift.
enum class Device {
    CorePointer,
    Pen,
    Mouse
};

Device benchDevice() {
    const QString device = qEnvironmentVariable("BENCH_DEVICE");
    return device == QLatin1String("pen") ? Device::Pen : device == QLatin1String("mouse") ? Device::Mouse : Device::CorePointer;
}

const QPointingDevice* benchStylus() {
    static const QPointingDevice stylus(
        QStringLiteral("bench stylus"), 4242, QInputDevice::DeviceType::Stylus, QPointingDevice::PointerType::Pen,
        QInputDevice::Capability::Position | QInputDevice::Capability::Pressure | QInputDevice::Capability::Hover, 1, 3);
    return &stylus;
}

QEvent::Type mouseType(QEvent::Type tablet) {
    return tablet == QEvent::TabletPress     ? QEvent::MouseButtonPress
           : tablet == QEvent::TabletRelease ? QEvent::MouseButtonRelease
                                             : QEvent::MouseMove;
}

void sendPen(CanvasView& canvas, QEvent::Type type, const QPointF& docPos, double pressure, Qt::MouseButtons buttons,
             Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
    static const Device device = benchDevice();
    const QTransform toWidget =
        ViewNavigator(canvas.document()->view(), QSizeF(canvas.size()), QSizeF(canvas.document()->size())).docToWidget();
    const QPointF at = toWidget.map(docPos);
    if (device == Device::Mouse) {
        const QPointF global = canvas.mapToGlobal(at);
        QMouseEvent event(mouseType(type), at, at, global, type == QEvent::TabletMove ? Qt::NoButton : Qt::LeftButton, buttons, modifiers);
        QCoreApplication::sendEvent(&canvas, &event);
        return;
    }
    const QPointingDevice* from = device == Device::Pen ? benchStylus() : QPointingDevice::primaryPointingDevice();
    QTabletEvent event(type, from, at, at, pressure, 0.0, 0.0, 0.0, 0.0, 0.0, modifiers, Qt::LeftButton, buttons);
    QCoreApplication::sendEvent(&canvas, &event);
}

// BENCH_LEAVE_MS=<ms>: the pen leaves the tablet's range that long after it lifts, as a real one does when it's raised.
void sendLeaveProximity() {
    if (benchDevice() != Device::Pen)
        return;
    QTabletEvent event(QEvent::TabletLeaveProximity, benchStylus(), QPointF(), QPointF(), 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, Qt::NoModifier,
                       Qt::NoButton, Qt::NoButton);
    QCoreApplication::sendEvent(qApp, &event);
}

// Runs the app's event loop until it's time for this sample, so timers fire and frames get drawn as they would. It runs at least
// once, as an app that's behind gets back to its loop between events. BENCH_BATCHED=1 doesn't: pen events that have waited arrive
// in a batch, and the loop only draws after them.
void waitUntil(const QElapsedTimer& clock, qint64 ms) {
    static const bool batched = qEnvironmentVariableIsSet("BENCH_BATCHED");
    if (batched && clock.elapsed() >= ms)
        return;
    do {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 1);
    } while (clock.elapsed() < ms);
}

// Fits the recording's first place inside the canvas: the pen's coordinates are from another canvas. BENCH_AS_RECORDED keeps them.
QPointF placed(const QPointF& pos, const QPointF& origin, const QSize& canvas) {
    static const bool asRecorded = qEnvironmentVariableIsSet("BENCH_AS_RECORDED");
    static const double scale = qEnvironmentVariable("BENCH_SCALE").toDouble();
    if (scale > 0.0)
        return pos * scale;
    return asRecorded ? pos : QPointF(canvas.width() / 2.0, canvas.height() / 2.0) + (pos - origin);
}

// "hard": a stroke along the canvas's middle, an event every BENCH_EVENT_MS (8), pressed hard after its first events and lifted fast:
// in the recorded lifts from a hard press the pressure falls 0.88, 0.7, 0.34 and the pen's up, an event apart. BENCH_PRESSURE
// (1) is how hard, BENCH_SPEED (1.5 px a ms) how fast it goes, BENCH_SECONDS (1) how long.
RecordedStroke hardPress(const QSize& canvas, double seconds) {
    RecordedStroke stroke;
    const int eventMs = qEnvironmentVariableIsSet("BENCH_EVENT_MS") ? qEnvironmentVariableIntValue("BENCH_EVENT_MS") : 8;
    const double pressure = qEnvironmentVariableIsSet("BENCH_PRESSURE") ? qEnvironmentVariable("BENCH_PRESSURE").toDouble() : 1.0;
    const double speed = qEnvironmentVariableIsSet("BENCH_SPEED") ? qEnvironmentVariable("BENCH_SPEED").toDouble() : 1.5;
    const double rise[] = {0.2, 0.45, 0.7, 0.9};
    const double fall[] = {0.88, 0.7, 0.34};
    const int count = static_cast<int>(seconds * 1000.0 / eventMs);
    const QPointF from(canvas.width() * 0.2, canvas.height() * 0.5);
    for (int i = 0; i < count + 3; ++i) {
        InputSample s;
        const double t = i * eventMs;
        s.pos = from + QPointF(speed * t, 0.15 * canvas.height() * std::sin(t / 400.0));
        s.pressure = i < 4 ? rise[i] * pressure : i < count ? pressure : fall[i - count] * pressure;
        s.timestampMs = static_cast<std::uint64_t>(t);
        s.kind = PointerKind::Pen;
        stroke.samples.push_back(s);
    }
    return stroke;
}

// A long, quick scribble: loops along a line across the canvas's middle, with the pressure going up and down.
RecordedStroke scribble(const QSize& canvas, double seconds) {
    RecordedStroke stroke;
    const int eventMs = qEnvironmentVariableIsSet("BENCH_EVENT_MS") ? qEnvironmentVariableIntValue("BENCH_EVENT_MS") : 8;
    const int count = static_cast<int>(seconds * 1000.0 / eventMs);
    const double span = std::min(canvas.width(), canvas.height()) * 0.3;
    for (int i = 0; i < count; ++i) {
        const double t = i * eventMs / 1000.0;
        InputSample s;
        s.pos = QPointF(canvas.width() / 2.0 + span * std::sin(t * 0.7) + 60.0 * std::cos(t * 9.0),
                        canvas.height() / 2.0 + span * 0.6 * std::sin(t * 1.1) + 60.0 * std::sin(t * 9.0));
        s.pressure = 0.55 + 0.35 * std::sin(t * 3.0);
        s.timestampMs = static_cast<std::uint64_t>(i * eventMs);
        s.kind = PointerKind::Pen;
        stroke.samples.push_back(s);
    }
    return stroke;
}

// The pictures BENCH_FORM and BENCH_TEXTURE are named from: the window's, or BENCH_PICTURES's.
const BrushResourceLibrary* s_benchLibrary = nullptr;

BrushSettings benchSettings(ToolBox& tools) {
    static const std::pair<const char*, ToolType> kTools[] = {
        {"pencil", ToolType::Pencil},
        {"brush", ToolType::Brush},
        {"marker", ToolType::Marker},
        {"water", ToolType::WaterColor},
        {"eraser", ToolType::Eraser},
        {"blur", ToolType::Blur},
        {"smudge", ToolType::Smudge},
        {"clone", ToolType::Clone},
        {"pixelart", ToolType::PixelArtPen},
        {"select", ToolType::SelectionPen},
        {"deselect", ToolType::SelectionEraser},
    };
    const QString tool = qEnvironmentVariable("BENCH_TOOL");
    const auto* const named = std::ranges::find_if(kTools, [&tool](const auto& t) { return tool == QLatin1String(t.first); });
    if (named != std::end(kTools))
        tools.selectOrCreateToolOfType(named->second);
    else if (!tool.isEmpty())
        std::fprintf(stderr, "BENCH_TOOL: no tool called %s; painting with the current one\n", qPrintable(tool));
    BrushSettings settings = tools.settings();
    if (qEnvironmentVariableIsSet("BENCH_MINSIZE"))
        settings.minSize = qEnvironmentVariableIntValue("BENCH_MINSIZE");
    if (qEnvironmentVariableIsSet("BENCH_FORM")) {  // a brush shape brings its settings file's settings, as the panel does
        settings.form = qEnvironmentVariable("BENCH_FORM");
        if (const BrushResource* r = s_benchLibrary ? s_benchLibrary->form(settings.form) : nullptr; r && r->shapeSettings)
            applyShapeSettings(settings, *r->shapeSettings);
    }
    if (qEnvironmentVariableIsSet("BENCH_TEXTURE"))
        settings.texture = qEnvironmentVariable("BENCH_TEXTURE");
    if (qEnvironmentVariableIsSet("BENCH_FORM_STRENGTH"))
        settings.formStrength = qEnvironmentVariableIntValue("BENCH_FORM_STRENGTH");
    if (qEnvironmentVariableIsSet("BENCH_TEXTURE_STRENGTH"))
        settings.textureStrength = qEnvironmentVariableIntValue("BENCH_TEXTURE_STRENGTH");
    if (qEnvironmentVariableIsSet("BENCH_STABILIZER"))
        settings.stabilizer = StabilizerLevel{qEnvironmentVariableIntValue("BENCH_STABILIZER")};
    if (qEnvironmentVariableIsSet("BENCH_EDGE"))  // the Shape, from 0
        settings.edge = qEnvironmentVariableIntValue("BENCH_EDGE");
    if (qEnvironmentVariableIsSet("BENCH_SHARPNESS"))  // %
        settings.sharpness = qEnvironmentVariableIntValue("BENCH_SHARPNESS");
    if (qEnvironmentVariableIsSet("BENCH_ALL_PRESSURES"))
        settings.pressureSize = settings.pressureDensity = settings.pressureBlending = true;
    if (qEnvironmentVariableIsSet("BENCH_COLOUR"))
        tools.setColor(QColor(qEnvironmentVariable("BENCH_COLOUR")));
    return settings;
}

// What 2 s of a stroke took on the main thread.
struct Bucket {
    std::vector<double> sendUs;
    qint64 lagMs = 0;
    int frames = 0;
};

void printBuckets(const std::vector<Bucket>& buckets) {
    std::printf("%6s %7s %10s %10s %8s %7s\n", "from s", "events", "median us", "worst us", "lag ms", "frames");
    for (std::size_t b = 0; b < buckets.size(); ++b) {
        std::vector<double> sends = buckets[b].sendUs;
        if (sends.empty())
            continue;
        std::ranges::sort(sends);
        std::printf("%6zu %7zu %10.0f %10.0f %8lld %7d\n", b * 2, sends.size(), sends[sends.size() / 2], sends.back(),
                    static_cast<long long>(buckets[b].lagMs), buckets[b].frames);
    }
}

// With BENCH_LIFT_WATCH=1 the app times what it handles on the main thread from the lift for a second, event by event, so that
// what a freeze at the lift is made of shows: an event handled inside another (a widget's paint inside the window's update, what
// the pen's lift sends) is listed under it.
class WatchedApp : public QApplication {
public:
    using QApplication::QApplication;

    void watch(bool on) {
        m_watching = on;
        if (on) {
            m_handled.clear();
            m_clock.start();
        }
    }

    void printHandled(double atLeastMs) const {
        std::vector<Handled> slow;
        std::ranges::copy_if(m_handled, std::back_inserter(slow), [&](const Handled& h) { return h.tookMs >= atLeastMs; });
        std::ranges::sort(slow, [](const Handled& a, const Handled& b) { return a.fromMs < b.fromMs; });
        for (const Handled& h : slow)
            std::printf("  %7.1f ms after the lift, %5.1f ms:%*s %s\n", h.fromMs, h.tookMs, 2 * h.depth, "", qPrintable(h.what));
    }

    // BENCH_STROKE_WATCH=1: what the app handled while the pen was down, by receiver and event, with how often and how long in all,
    // and the widgets painted inside those.
    void tally(bool on) {
        m_tallying = on;
        if (on)
            m_tally.clear();
    }

    void printTally() const {
        std::vector<std::pair<QString, Total>> rows(m_tally.begin(), m_tally.end());
        std::ranges::sort(rows, [](const auto& a, const auto& b) { return a.second.ms > b.second.ms; });
        for (std::size_t i = 0; i < std::min<std::size_t>(rows.size(), 25); ++i)
            std::printf("  %7.1f ms in %5d: %s\n", rows[i].second.ms, rows[i].second.count, qPrintable(rows[i].first));
    }

    bool notify(QObject* receiver, QEvent* event) override {
        if (m_tallying && m_depth == 0) {
            const QString what = describe(receiver, event->type());
            QElapsedTimer took;
            took.start();
            ++m_depth;
            const bool handled = QApplication::notify(receiver, event);
            --m_depth;
            Total& total = m_tally[what];
            ++total.count;
            total.ms += double(took.nsecsElapsed()) / 1e6;
            return handled;
        }
        // The widgets painted inside the window's update, each on its own line.
        if (m_tallying && event->type() == QEvent::Paint) {
            const QString what = QStringLiteral("  painted: ") + describe(receiver, QEvent::Paint);
            QElapsedTimer took;
            took.start();
            ++m_depth;
            const bool handled = QApplication::notify(receiver, event);
            --m_depth;
            Total& total = m_tally[what];
            ++total.count;
            total.ms += double(took.nsecsElapsed()) / 1e6;
            return handled;
        }
        if (!m_watching)
            return QApplication::notify(receiver, event);
        const double from = double(m_clock.nsecsElapsed()) / 1e6;
        const int depth = m_depth++;
        const QEvent::Type type = event->type();  // the event may be gone once it's handled
        const bool handled = QApplication::notify(receiver, event);
        --m_depth;
        const double took = double(m_clock.nsecsElapsed()) / 1e6 - from;
        if (took >= 0.5)
            m_handled.push_back({from, took, depth, describe(receiver, type)});
        return handled;
    }

private:
    struct Handled {
        double fromMs, tookMs;
        int depth;
        QString what;
    };

    static QString describe(const QObject* receiver, QEvent::Type type) {
        QString what = QString::fromLatin1(receiver->metaObject()->className());
        if (!receiver->objectName().isEmpty())
            what += u" \"" + receiver->objectName() + u'"';
        if (const QObject* parent = receiver->parent())
            what += u" in " + QString::fromLatin1(parent->metaObject()->className());
        if (const auto* timer = qobject_cast<const QTimer*>(receiver))
            what += QStringLiteral(" of %1 ms").arg(timer->interval());  // which one it is, where it has no name
        return what + u", " + QString::fromLatin1(QMetaEnum::fromType<QEvent::Type>().valueToKey(type));
    }

    struct Total {
        int count = 0;
        double ms = 0.0;
    };

    bool m_watching = false;
    bool m_tallying = false;
    std::map<QString, Total> m_tally;
    int m_depth = 0;
    QElapsedTimer m_clock;
    std::vector<Handled> m_handled;
};

// BENCH_LIFT_WATCH=1: for a second after the lift, a timer due every millisecond notes the longest the event loop went without
// getting back to it, and when, so that a freeze at the lift shows; and what the app handled in that second that took 1 ms or more.
// With BENCH_HOVER=1 the pen stays in range after it lifts, as a real one does, and hovers, an event every 5 ms, drifting from where it lifted.
void watchAfterLift(const QElapsedTimer& lift, CanvasView& canvas, const QPointF& lifted) {
    QElapsedTimer tick;
    tick.start();
    qint64 longest = 0, at = 0, total = 0;
    QTimer timer;
    timer.setTimerType(Qt::PreciseTimer);
    QObject::connect(&timer, &QTimer::timeout, [&] {
        const qint64 gap = tick.restart();
        total += gap > 2 ? gap : 0;
        if (gap > longest) {
            longest = gap;
            at = lift.elapsed() - gap;
        }
    });
    timer.start(1);
    QTimer hover;
    hover.setTimerType(Qt::PreciseTimer);
    QObject::connect(&hover, &QTimer::timeout, [&] {
        const double t = double(lift.elapsed()) / 1000.0;
        sendPen(canvas, QEvent::TabletMove, lifted + QPointF(40.0 * t, 20.0 * t), 0.0, Qt::NoButton);
    });
    if (qEnvironmentVariableIsSet("BENCH_HOVER"))
        hover.start(5);
    bool leaves = qEnvironmentVariableIsSet("BENCH_LEAVE_MS");
    const qint64 leaveMs = qEnvironmentVariableIntValue("BENCH_LEAVE_MS");
    while (lift.elapsed() < 1000) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 1);
        if (leaves && lift.elapsed() >= leaveMs) {
            leaves = false;
            hover.stop();
            sendLeaveProximity();
        }
    }
    std::printf("after the lift: the event loop's longest stall %lld ms, from %lld ms after it; %lld ms in stalls over 2 ms\n",
                static_cast<long long>(longest), static_cast<long long>(at), static_cast<long long>(total));
    auto* app = static_cast<WatchedApp*>(QCoreApplication::instance());
    app->watch(false);
    app->printHandled(qEnvironmentVariableIsSet("BENCH_WATCH_MS") ? qEnvironmentVariable("BENCH_WATCH_MS").toDouble() : 1.0);
}

void playStroke(CanvasView& canvas, const RecordedStroke& stroke) {
    static const bool bucketed = qEnvironmentVariableIsSet("BENCH_BUCKETS");
    std::vector<Bucket> buckets;
    int frames = 0;
    // BENCH_VSYNC_MS: each swap waits for the next display refresh, as a compositor's does, instead of returning at once.
    static const int vsyncMs = qEnvironmentVariableIntValue("BENCH_VSYNC_MS");
    QElapsedTimer refresh;
    refresh.start();
    const QMetaObject::Connection counting = QObject::connect(&canvas, &QOpenGLWidget::frameSwapped, [&frames, &refresh] {
        ++frames;
        if (vsyncMs > 0)
            for (const qint64 next = (refresh.elapsed() / vsyncMs + 1) * vsyncMs; refresh.elapsed() < next;)
                QThread::usleep(200);
    });
    QElapsedTimer clock;
    clock.start();
    const InputSample& first = stroke.samples.front();
    const QPointF origin = first.pos;
    const QSize size = canvas.document()->size();
    QElapsedTimer landing;
    landing.start();
    static const bool tallying = qEnvironmentVariableIsSet("BENCH_STROKE_WATCH");
    auto* app = static_cast<WatchedApp*>(QCoreApplication::instance());
    app->tally(tallying);
    if (qEnvironmentVariable("BENCH_TOOL") == QLatin1String("clone")) {
        // The Clone Stamp's source, as set for the reference: an Alt tap this far left of where the stroke starts.
        constexpr double kCloneSourceLeft = 600.0;  // px
        const QPointF source = placed(first.pos, origin, size) - QPointF(kCloneSourceLeft, 0.0);
        sendPen(canvas, QEvent::TabletPress, source, 1.0, Qt::LeftButton, Qt::AltModifier);
        sendPen(canvas, QEvent::TabletRelease, source, 0.0, Qt::NoButton, Qt::AltModifier);
        QCoreApplication::processEvents();
        landing.restart();
    }
    sendPen(canvas, QEvent::TabletPress, placed(first.pos, origin, size), first.pressure, Qt::LeftButton);
    const qint64 landingMs = landing.elapsed();
    for (std::size_t i = 1; i < stroke.samples.size(); ++i) {
        const InputSample& sample = stroke.samples[i];
        const qint64 due = static_cast<qint64>(sample.timestampMs - first.timestampMs);
        waitUntil(clock, due);
        QElapsedTimer send;
        send.start();
        sendPen(canvas, QEvent::TabletMove, placed(sample.pos, origin, size), sample.pressure, Qt::LeftButton);
        const std::size_t b = static_cast<std::size_t>(due / 2000);
        if (buckets.size() <= b)
            buckets.resize(b + 1);
        buckets[b].sendUs.push_back(double(send.nsecsElapsed()) / 1000.0);
        buckets[b].lagMs = clock.elapsed() - due;
        buckets[b].frames += std::exchange(frames, 0);
    }
    QObject::disconnect(counting);
    if (tallying) {
        app->tally(false);
        std::printf("handled while the pen was down:\n");
        app->printTally();
    }
    if (bucketed)
        printBuckets(buckets);
    const qint64 last = static_cast<qint64>(stroke.samples.back().timestampMs - first.timestampMs);
    std::printf("behind the pen at the lift: %lld ms, of a stroke of %lld ms\n", static_cast<long long>(clock.elapsed() - last),
                static_cast<long long>(last));
    QCoreApplication::processEvents();
    QElapsedTimer lift;
    lift.start();
    static const bool watching = qEnvironmentVariableIsSet("BENCH_LIFT_WATCH");
    if (watching)
        static_cast<WatchedApp*>(QCoreApplication::instance())->watch(true);
    const QPointF lifted = placed(stroke.samples.back().pos, origin, size);
    sendPen(canvas, QEvent::TabletRelease, lifted, 0.0, Qt::NoButton);
    std::printf("landing took %lld ms, the lift %lld ms\n", static_cast<long long>(landingMs), static_cast<long long>(lift.elapsed()));
    if (watching)
        watchAfterLift(lift, canvas, lifted);
    // The app runs until the stroke has finished painting and is done, as it would for a painter who waits: returning sooner closes
    // the window, which finishes the stroke at once, the rest of its painting in one go.
    // A selection pen's stroke finishes on its own, the document painting until then.
    constexpr qint64 kLongestFinishMs = 20000;
    while ((canvas.strokeFinishing() || canvas.document()->isPainting()) && lift.elapsed() < kLongestFinishMs)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
    std::printf("done %lld ms after the lift\n", static_cast<long long>(lift.elapsed()));
    waitUntil(clock, clock.elapsed() + 100);
}

// BENCH_SHOTS=<dir>: once the strokes are painted, the view as it shows them at a few zooms, Smooth and Fast, rotated and not, as
// PNGs, to compare a change to the display with the pictures from before it. The zoomed ones are centred on `focus`.
void saveShots(CanvasView& canvas, const QString& dir, const QPointF& focus) {
    canvas.settleStroke();
    QDir().mkpath(dir);
    const bool fast = canvas.fastZoomedOut();
    const QPointF centre = canvas.rect().center();
    struct Shot {
        const char* name;
        double zoom;  // 0: fitted
        double degrees;
    };
    static const Shot kShots[] = {{"fit", 0, 0},         {"z0.4", 0.4, 0},      {"z0.7", 0.7, 0}, {"z1", 1, 0},      {"z2.5", 2.5, 0},
                                  {"z0.4-rot", 0.4, 30}, {"z2.5-rot", 2.5, 30}, {"z0.1", 0.1, 0}, {"z0.03", 0.03, 0}};
    for (const bool fastShot : {false, true}) {
        canvas.setFastZoomedOut(fastShot);
        for (const Shot& s : kShots) {
            canvas.resetView();
            if (s.zoom > 0) {
                canvas.setZoom(s.zoom, centre);
                canvas.centreOn(focus);
            }
            canvas.setRotation(s.degrees);
            QCoreApplication::processEvents();
            // BENCH_SHOTS_BOTH=1: each view twice, without and with the reduced texture, from the same layer.
            static const bool both = qEnvironmentVariableIsSet("BENCH_SHOTS_BOTH");
            for (const bool reduced : both ? std::vector<bool>{false, true} : std::vector<bool>{true}) {
                CanvasView::setReducedViewForTesting(reduced);
                canvas.update();
                QCoreApplication::processEvents();
                const QString name =
                    QStringLiteral("%1-%2%3.png")
                        .arg(QLatin1String(s.name), fastShot ? u"fast" : u"smooth", both ? (reduced ? u"-reduced" : u"-full") : u"");
                if (!canvas.grabFramebuffer().save(QDir(dir).filePath(name)))
                    std::fprintf(stderr, "BENCH_SHOTS: couldn't write %s\n", qPrintable(name));
            }
        }
    }
    canvas.setFastZoomedOut(fast);
    canvas.resetView();
}

}

// BENCH_ZOOM_SWEEP: each wheel notch's frame, timed from the zoom's change to the frame swapped and the GPU done, as the painter waits for it.
void zoomSweep(CanvasView& canvas) {
    const double from = qEnvironmentVariableIsSet("BENCH_SWEEP_FROM") ? qEnvironmentVariable("BENCH_SWEEP_FROM").toDouble() : 0.046;
    constexpr double kTo = 1.0;
    const QPointF middle(canvas.rect().center());
    QElapsedTimer waiting;
    waiting.start();
    while (waiting.elapsed() < qEnvironmentVariableIntValue("BENCH_SWEEP_WAIT_MS"))
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    std::vector<double> zooms;
    for (double z = from; z < kTo; z = ViewNavigator::nextWheelZoom(z, 1))
        zooms.push_back(z);
    zooms.push_back(kTo);
    for (std::size_t i = zooms.size() - 1; i-- > 0;)
        zooms.push_back(zooms[i]);
    bool swapped = false;
    const QMetaObject::Connection watch = QObject::connect(&canvas, &QOpenGLWidget::frameSwapped, [&swapped] { swapped = true; });
    constexpr qint64 kLongestFrameMs = 10000;
    const auto drawn = [&](double z) {
        swapped = false;
        canvas.setZoom(z, middle);
        QElapsedTimer waited;
        waited.start();
        while (!swapped && waited.elapsed() < kLongestFrameMs)
            QCoreApplication::processEvents(QEventLoop::AllEvents, 1);
        canvas.makeCurrent();
        canvas.context()->functions()->glFinish();
        canvas.doneCurrent();
    };
    drawn(zooms.front());
    double total = 0.0;
    int step = 0;
    for (const double z : zooms) {
        QElapsedTimer timed;
        timed.start();
        drawn(z);
        const double ms = static_cast<double>(timed.nsecsElapsed()) / 1e6;
        total += ms;
        std::fprintf(stderr, "sweep zoom %.4f took %.2f ms\n", z, ms);
        if (qEnvironmentVariableIsSet("BENCH_SWEEP_SHOTS"))
            canvas.grabFramebuffer().save(
                QStringLiteral("%1/step-%2.png").arg(qEnvironmentVariable("BENCH_SWEEP_SHOTS")).arg(step++, 2, 10, QLatin1Char('0')));
    }
    QObject::disconnect(watch);
    std::fprintf(stderr, "sweep %zu steps took %.1f ms\n", zooms.size(), total);
}

int main(int argc, char** argv) {
    if (argc != 6) {
        std::fprintf(stderr, "usage: %s <input log> <sizes px> <canvas w> <canvas h> <strokes>\n", argv[0]);
        return 2;
    }
    // As the app starts (src/app/main.cpp): every tablet event reaches the stroke, and the canvas gets the OpenGL it's made for,
    // where a real display could otherwise give an older one, in which the canvas can't draw as it does.
    QCoreApplication::setAttribute(Qt::AA_CompressTabletEvents, false);
    QSurfaceFormat::setDefaultFormat(platform::defaultSurfaceFormat());
    WatchedApp app(argc, argv);
    // BENCH_CORES=<n> paints with that many cores, as a machine with that many would.
    if (qEnvironmentVariableIsSet("BENCH_CORES"))
        cpu_cores::setLimit(qEnvironmentVariableIntValue("BENCH_CORES"));
    // BENCH_BACKLOG=never or always groups what's waiting as the reference set does, so a run lays the same dabs as the big canvas bench's
    // --backlog; BENCH_DUMP=<file> writes the layer's tiles after the strokes, in that bench's --dump form, to compare the two.
    if (qEnvironmentVariableIsSet("BENCH_BACKLOG"))
        StrokeWorker::setBacklogForTests(qEnvironmentVariable("BENCH_BACKLOG") == QLatin1String("always") ? StrokeWorker::Backlog::Always
                                                                                                          : StrokeWorker::Backlog::Never);
    if (qEnvironmentVariableIsSet("BENCH_GO_MS"))
        StrokeWorker::setTimePerGoForBenches(qEnvironmentVariableIntValue("BENCH_GO_MS") * 1000);
    // BENCH_MAX_TEXTURE=<px> shows the canvas as graphics whose largest texture is that size would, cut into several.
    if (qEnvironmentVariableIsSet("BENCH_MAX_TEXTURE"))
        CanvasView::setLargestTextureForTesting(qEnvironmentVariableIntValue("BENCH_MAX_TEXTURE"));
    QStandardPaths::setTestModeEnabled(true);  // and anything it saves goes to a folder of its own, under HOME
    const QSize canvasSize(std::atoi(argv[3]), std::atoi(argv[4]));
    const bool longStroke = QString::fromLocal8Bit(argv[1]) == QLatin1String("long");
    const bool hardStroke = QString::fromLocal8Bit(argv[1]) == QLatin1String("hard");
    const double seconds = qEnvironmentVariableIsSet("BENCH_SECONDS") ? qEnvironmentVariable("BENCH_SECONDS").toDouble()
                           : hardStroke                               ? 1.0
                                                                      : 6.0;
    const auto strokeCount = static_cast<std::size_t>(std::atoi(argv[5]));
    std::vector<RecordedStroke> strokes = longStroke   ? std::vector<RecordedStroke>(strokeCount, scribble(canvasSize, seconds))
                                          : hardStroke ? std::vector<RecordedStroke>(strokeCount, hardPress(canvasSize, seconds))
                                                       : readRecording(QString::fromLocal8Bit(argv[1]));
    if (qEnvironmentVariableIsSet("BENCH_STROKE")) {
        const auto only = static_cast<std::size_t>(qEnvironmentVariableIntValue("BENCH_STROKE"));
        if (only >= strokes.size()) {
            std::fprintf(stderr, "BENCH_STROKE: the log has %zu pen strokes\n", strokes.size());
            return 2;
        }
        strokes = std::vector<RecordedStroke>(static_cast<std::size_t>(std::max(1, std::atoi(argv[5]))), strokes[only]);
    }
    // BENCH_WINDOW=1 paints in the app's own window, with its panels watching the canvas.
    std::unique_ptr<MainWindow> window;
    std::unique_ptr<BrushResourceLibrary> library;
    std::unique_ptr<Document> ownDoc;
    std::unique_ptr<ToolBox> ownTools;
    std::unique_ptr<LayerTreeModel> layers;
    std::unique_ptr<CanvasView> ownCanvas;
    TabletStats stats;
    QTemporaryDir pictures;
    CanvasView* view = nullptr;
    ToolBox* toolBox = nullptr;
    if (qEnvironmentVariableIsSet("BENCH_WINDOW")) {
        window = std::make_unique<MainWindow>();
        const QStringList windowSize = qEnvironmentVariable("BENCH_WINDOW_SIZE", QStringLiteral("1600x1000")).split(u'x');
        window->resize(windowSize.value(0).toInt(), windowSize.value(1).toInt());
        window->show();
        // Opened as a file, so the window sets the canvas up as it does any: its tools, options and timelapse.
        QString start = qEnvironmentVariable("BENCH_LAYER");
        if (start.isEmpty()) {
            start = pictures.filePath(QStringLiteral("canvas.png"));
            QImage empty(canvasSize, QImage::Format_ARGB32);
            empty.fill(Qt::transparent);
            empty.save(start);
        }
        window->openFiles({start});
        QElapsedTimer opening;
        opening.start();
        while ((!window->tabs()->currentCanvas() || window->tabs()->currentDocument()->size() != canvasSize) && opening.elapsed() < 20000)
            QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        view = window->tabs()->currentCanvas();
        if (!view) {
            std::fprintf(stderr, "the canvas didn't open\n");
            return 1;
        }
        toolBox = window->tools();
        s_benchLibrary = window->findChild<BrushResourceLibrary*>();
        if (qEnvironmentVariableIsSet("BENCH_TIMELINE"))
            if (auto* timeline = window->findChild<QAction*>(QStringLiteral("canvas.showTimeline")); timeline && !timeline->isChecked())
                timeline->trigger();
    } else {
        // BENCH_PICTURES=<folder>: the user's own tip shapes and textures from there (forms/blots, textures, ..., as the app's data
        // folder holds them), as the window loads them from its data folder; otherwise none.
        library = std::make_unique<BrushResourceLibrary>(
            qEnvironmentVariableIsSet("BENCH_PICTURES") ? qEnvironmentVariable("BENCH_PICTURES") : pictures.path());
        RebuiltStroke::setResources(library.get());
        s_benchLibrary = library.get();
        ownDoc = std::make_unique<Document>(canvasSize, Qt::white);
        if (qEnvironmentVariableIsSet("BENCH_LAYER")) {
            const QImage start(qEnvironmentVariable("BENCH_LAYER"));
            if (start.size() != canvasSize) {
                std::fprintf(stderr, "BENCH_LAYER: %s isn't %dx%d\n", qPrintable(qEnvironmentVariable("BENCH_LAYER")), canvasSize.width(),
                             canvasSize.height());
                return 2;
            }
            ownDoc->paintTarget(ownDoc->currentLayerId()).value()->writeImage(start, {0, 0});
        }
        ownTools = std::make_unique<ToolBox>();
        layers = std::make_unique<LayerTreeModel>();
        layers->setDocument(ownDoc.get());
        ownCanvas = std::make_unique<CanvasView>(ownDoc.get(), &stats);
        ownCanvas->setToolBox(ownTools.get());
        const QStringList viewSize = qEnvironmentVariable("BENCH_VIEW", QStringLiteral("1200x800")).split(u'x');
        ownCanvas->resize(viewSize.value(0).toInt(), viewSize.value(1).toInt());
        ownCanvas->show();
        view = ownCanvas.get();
        toolBox = ownTools.get();
        // BENCH_FULL_PRESSURE=<%>: Options' "Full pressure at", which the window's canvas takes from the app's options and this one
        // otherwise leaves at 100.
        if (qEnvironmentVariableIsSet("BENCH_FULL_PRESSURE"))
            ownCanvas->setPressureRange({0.0, qEnvironmentVariableIntValue("BENCH_FULL_PRESSURE") / 100.0});
    }
    CanvasView& canvas = *view;
    ToolBox& tools = *toolBox;
    Document& doc = *canvas.document();
    // A compositor can take a moment to show the window, and the canvas only has its GL once it's shown.
    QElapsedTimer shown;
    shown.start();
    do {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    } while (!canvas.isValid() && shown.elapsed() < 5000);
    canvas.resetView();
    if (qEnvironmentVariableIsSet("BENCH_FAST"))
        canvas.setFastZoomedOut(qEnvironmentVariableIntValue("BENCH_FAST") != 0);
    if (qEnvironmentVariableIsSet("BENCH_ZOOM"))
        canvas.setZoom(qEnvironmentVariable("BENCH_ZOOM").toDouble(), QPointF(canvas.rect().center()));
    QCoreApplication::processEvents();
    std::printf("zoom %.3f, GL %s\n", doc.view().zoom, canvas.isValid() ? "ready" : "not ready");
    if (QOpenGLContext* gl = canvas.context()) {
        canvas.makeCurrent();
        const QSurfaceFormat format = gl->format();
        const auto* renderer = reinterpret_cast<const char*>(gl->functions()->glGetString(GL_RENDERER));
        std::printf("OpenGL %d.%d %s, %s, on %s\n", format.majorVersion(), format.minorVersion(),
                    format.profile() == QSurfaceFormat::CoreProfile            ? "core"
                    : format.profile() == QSurfaceFormat::CompatibilityProfile ? "compatibility"
                                                                               : "no profile",
                    renderer ? renderer : "an unknown renderer", qPrintable(QGuiApplication::platformName()));
        canvas.doneCurrent();
    }
    const QStringList sizes = QString::fromLocal8Bit(argv[2]).split(u',');
    int played = 0;
    // BENCH_SETTLE_MS=<ms>: the app runs that long before the first stroke, so a large canvas has been drawn and uploaded once, as a
    // painter sees it before they draw.
    QElapsedTimer settling;
    settling.start();
    while (settling.elapsed() < qEnvironmentVariableIntValue("BENCH_SETTLE_MS"))
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    QRectF painted;
    for (const RecordedStroke& stroke : strokes) {
        if (stroke.samples.size() < 20 || played >= std::atoi(argv[5]))
            continue;
        BrushSettings settings = benchSettings(tools);
        settings.size = static_cast<int>(sizes[played % sizes.size()].toDouble() * 10.0);
        tools.setSettings(settings);
        std::printf("%s at %.1f px, Min Size %d %%, Stabilizer %s\n", qPrintable(toolTypeName(tools.settings().tool)),
                    tools.settings().size / 10.0, tools.settings().minSize,
                    tools.settings().stabilizer ? qPrintable(tools.settings().stabilizer->label()) : "global");
        ++played;
        playStroke(canvas, stroke);
        for (const InputSample& s : stroke.samples)
            painted |= QRectF(placed(s.pos, stroke.samples.front().pos, doc.size()), QSizeF(1, 1));
    }
    if (qEnvironmentVariableIsSet("BENCH_SHOTS"))
        saveShots(canvas, qEnvironmentVariable("BENCH_SHOTS"), painted.center());
    if (qEnvironmentVariableIsSet("BENCH_ZOOM_SWEEP"))
        zoomSweep(canvas);
    if (window && qEnvironmentVariableIsSet("BENCH_TITLE_SHOT")) {
        constexpr double kDefaultDpi = 350.0;
        constexpr int kShotSettleMs = 1500;
        const QSize kSubWindow(1000, 700);
        const double dpi = qEnvironmentVariableIsSet("BENCH_DPI") ? qEnvironmentVariable("BENCH_DPI").toDouble() : kDefaultDpi;
        CanvasView* titleView = window->tabs()->addDocument(new Document(canvasSize, CanvasBackground::fromPaper(Qt::white), dpi));
        QMdiSubWindow* sub = window->tabs()->windowAt(window->tabs()->indexOf(titleView));
        sub->showNormal();
        sub->resize(kSubWindow);
        titleView->resetView();
        QElapsedTimer settled;
        settled.start();
        while (settled.elapsed() < kShotSettleMs)
            QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        window->grab().save(qEnvironmentVariable("BENCH_TITLE_SHOT"));
        std::printf("title: %s\n", qPrintable(sub->windowTitle()));
    }
    if (qEnvironmentVariableIsSet("BENCH_DUMP")) {
        QFile file(qEnvironmentVariable("BENCH_DUMP"));
        if (!file.open(QIODevice::WriteOnly))
            return 3;
        for (const auto& [coord, tile] : doc.layer(doc.currentLayerId())->pixels.tiles()) {
            const std::array<std::int32_t, 2> place = {coord.x, coord.y};
            file.write(reinterpret_cast<const char*>(place.data()), sizeof place);
            file.write(reinterpret_cast<const char*>(tile->px.data()), sizeof tile->px);
        }
    }
    if (library) {
        RebuiltStroke::setResources(nullptr);
    }
    return 0;
}
