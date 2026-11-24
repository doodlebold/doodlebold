// How the largest canvas, 16384 × 16384, costs in time and memory, against a smaller one: a new canvas, two more layers,
// a 4000 px Marker stroke across it, the whole picture and the thumbnails made after it, undo, saving and opening it again. Each
// step prints how long it took, and the memory the program holds after it and at most so far (VmRSS, VmHWM). Not a test: run by
// hand, alone, in a memory-capped scope.
//
//   doodlebold_big_canvas_bench <side px> <stroke.jsonl> [--cores <n>] [--tool <name>] [--size <px>] [--stroke-only] [--no-limit]
//                              [--usable <MiB>] [--background] [--live] [--stroke <n>] [--scale <factor>] [--min-size <percent>]
//                              [--layer <png>] [--stabilizer <level>] [--all-pressures] [--mouse] [--dump <file>] [--frames <dir>] [--before fill-clear|painted-undone]
//
// --tool and --size paint another tool (its key, "watercolor") or size in place of the 4000 px Marker;
// --stroke-only stops after the stroke, for its memory alone; --no-limit paints it whatever memory it takes, to measure that;
// --usable takes that as the memory a stroke may hold in place of the machine's; --background paints it on the stroke's own
// thread, as the canvas does. --live also plays it at the speed it was drawn, taking frames as the canvas does, and prints one line
// of what the painter feels: how old the ink was when shown, the longest gap between updates that showed new ink, how long painting
// went on after the lift, the CPU it took and the memory it held. It only runs the stroke. --stroke picks the recording's nth stroke,
// --scale multiplies the pen's places by that factor in place of stretching them to the side given (1: as drawn), --min-size sets
// Min Size, --layer tiles that picture over the layer first, --before does what a painter might first (see below), --stabilizer sets the level (else the app's starting one) and
// --all-pressures turns on pressure for size, density and blending, as a pen painter paints. --mouse plays the pen's samples as the mouse's.
// --dump writes the layer's tiles to a file, each tile's place then its 16-bit pixels, to compare one run with another.
// --frames saves each update of the picture, scaled to 1000 px across, as numbered PNGs in the folder, to look at what a painter sees
// (it slows the replay a little, so it's for the look, not for timing).
//
// The stroke file's first stroke is stretched from the 3000 × 3000 canvas it was drawn on to the side given, as
// tests/fixtures/reference/strokes/recorded-c5-3k.jsonl is.

#include "core/stroke/StrokeWorker.h"
#include "core/brush/BrushSettings.h"
#include "core/document/Document.h"
#include "core/io/DoodleFormat.h"
#include "core/io/FileIo.h"
#include "core/paint/StrokeController.h"
#include "core/paint/StrokeMemory.h"
#include "core/stroke/StrokeRecording.h"
#include "core/util/CpuCores.h"

#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QTemporaryDir>

#include <sys/resource.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <functional>
#include <memory>
#include <optional>
#include <thread>
#include <vector>

using namespace doodlebold;

namespace {

constexpr double kRecordedSide = 3000.0;
constexpr int kLayerThumbnail = 40;  // LayerTreeModel::kThumbSize
constexpr int kFrameThumbnail = 64;

// A line of /proc/self/status, in MiB.
double statusMiB(const char* key) {
    QFile status(QStringLiteral("/proc/self/status"));
    if (!status.open(QIODevice::ReadOnly))
        return -1.0;
    for (const QByteArray& line : status.readAll().split('\n'))
        if (line.startsWith(key))
            return line.mid(qstrlen(key)).trimmed().split(' ').value(0).toDouble() / 1024.0;
    return -1.0;
}

// Says so if a step's edit or file operation was refused, as its time would mean nothing.
void check(bool ok, const char* what) {
    if (!ok)
        std::printf("  %s failed\n", what);
}

void step(const char* name, const std::function<void()>& work) {
    const auto start = std::chrono::steady_clock::now();
    work();
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    std::printf("%-34s %9.1f ms %8.0f MiB now %8.0f MiB at most\n", name, ms, statusMiB("VmRSS:"), statusMiB("VmHWM:"));
    std::fflush(stdout);
}

RecordedStroke scaled(const RecordedStroke& stroke, double factor) {
    RecordedStroke out = stroke;
    for (InputSample& s : out.samples)
        s.pos *= factor;
    return out;
}

struct Options {
    int cores = 0;
    ToolType tool = ToolType::Marker;
    double sizePx = 4000.0;
    bool strokeOnly = false;
    bool noLimit = false;
    std::int64_t usableMiB = 0;
    bool background = false;
    bool live = false;
    int strokeIndex = 0;
    double scale = 0.0;  // 0: stretched to the canvas's side
    int minSize = -1;
    QString layer;
    QString dump;
    QString before;
    QString frames;
    int stabilizer = StabilizerLevel::kGlobalStart;
    bool allPressures = false;
    bool mouse = false;
    int edge = -1;  // the Shape button, 0 to 4; below 0 the tool's own
    int sharpness = -1;  // %; below 0 the tool's own
    int dilution = -1, persistence = -1, blending = -1;  // %; below 0 the tool's own (task 57's checks play Water Color at others)
    QColor colour = QColor(32, 80, 200);
    bool preserveOpacity = false;
    int viewLevel = 0;  // --view-level, the stroke shown as for a view zoomed out by 2^this
};

Options optionsFrom(int argc, char** argv) {
    Options options;
    for (int i = 3; i < argc; ++i) {
        const QByteArray arg(argv[i]);
        if (arg == "--stroke-only") {
            options.strokeOnly = true;
        } else if (arg == "--background") {
            options.background = true;
        } else if (arg == "--no-limit") {
            options.noLimit = true;
        } else if (arg == "--live") {
            options.live = true;
        } else if (arg == "--all-pressures") {
            options.allPressures = true;
        } else if (arg == "--mouse") {
            options.mouse = true;
        } else if (i + 1 == argc) {
            break;
        } else if (arg == "--cores") {
            options.cores = std::atoi(argv[++i]);
        } else if (arg == "--size") {
            options.sizePx = std::atof(argv[++i]);
        } else if (arg == "--usable") {
            options.usableMiB = std::atoll(argv[++i]);
        } else if (arg == "--tool") {
            options.tool = toolTypeFromKey(QString::fromLocal8Bit(argv[++i])).value_or(options.tool);
        } else if (arg == "--stroke") {
            options.strokeIndex = std::atoi(argv[++i]);
        } else if (arg == "--scale") {
            options.scale = std::atof(argv[++i]);
        } else if (arg == "--min-size") {
            options.minSize = std::atoi(argv[++i]);
        } else if (arg == "--layer") {
            options.layer = QString::fromLocal8Bit(argv[++i]);
        } else if (arg == "--dump") {
            options.dump = QString::fromLocal8Bit(argv[++i]);
        } else if (arg ==
                   "--backlog") {  // never or always group what's waiting, as the reference set does, so two builds lay the same dabs
            const QString backlog = QString::fromLocal8Bit(argv[++i]);
            StrokeWorker::setBacklogForTests(backlog == QLatin1String("always") ? StrokeWorker::Backlog::Always
                                                                                : StrokeWorker::Backlog::Never);
        } else if (arg == "--before") {
            options.before = QString::fromLocal8Bit(argv[++i]);
        } else if (arg == "--frames") {
            options.frames = QString::fromLocal8Bit(argv[++i]);
        } else if (arg == "--stabilizer") {
            options.stabilizer = std::atoi(argv[++i]);
        } else if (arg == "--edge") {
            options.edge = std::atoi(argv[++i]);
        } else if (arg == "--sharpness") {
            options.sharpness = std::atoi(argv[++i]);
        } else if (arg == "--dilution") {
            options.dilution = std::atoi(argv[++i]);
        } else if (arg == "--persistence") {
            options.persistence = std::atoi(argv[++i]);
        } else if (arg == "--blending") {
            options.blending = std::atoi(argv[++i]);
        } else if (arg == "--colour") {
            options.colour = QColor(QString::fromLocal8Bit(argv[++i]));
        } else if (arg == "--preserve-opacity") {
            options.preserveOpacity = true;
        } else if (arg == "--view-level") {
            options.viewLevel = std::atoi(argv[++i]);
        }
    }
    return options;
}

BrushSettings settingsFor(const Options& options) {
    BrushSettings settings = defaultSettings(options.tool);
    settings.size = static_cast<int>(options.sizePx * 10.0);
    if (options.minSize >= 0)
        settings.minSize = options.minSize;
    if (options.allPressures)
        settings.pressureSize = settings.pressureDensity = settings.pressureBlending = true;
    if (options.edge >= 0)
        settings.edge = options.edge;
    if (options.sharpness >= 0)
        settings.sharpness = options.sharpness;
    if (options.dilution >= 0)
        settings.dilution = options.dilution;
    if (options.persistence >= 0)
        settings.persistence = options.persistence;
    if (options.blending >= 0)
        settings.blending = options.blending;
    return settings.clamped();
}

std::int64_t nowUs() {
    return std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

double cpuSeconds(int who) {
    rusage used{};
    getrusage(who, &used);
    return double(used.ru_utime.tv_sec + used.ru_stime.tv_sec) + double(used.ru_utime.tv_usec + used.ru_stime.tv_usec) / 1e6;
}

// --live: as recorded_strokes_bench plays a stroke, on any canvas, with what the view bench can't say without a screen.
void paintLive(Document& doc, const RecordedStroke& stroke, const Options& options) {
    constexpr std::int64_t kFrameUs = 7000;
    double limitedTo = options.sizePx;
    bool stopped = false;
    QObject::connect(&doc, &Document::strokeLimited, [&](int, int used) { limitedTo = used / 10.0; });
    QObject::connect(&doc, &Document::strokeStopped, [&](int) { stopped = true; });
    StrokeController controller(&doc);
    std::vector<std::int64_t> inkUs, shownAtUs;
    std::int64_t lastStamp = 0, lastFrameUs = 0;
    const auto showFrame = [&] {
        if (!controller.hasUnshownPaint())
            return;
        controller.showPaint();
        lastFrameUs = nowUs();
        shownAtUs.push_back(lastFrameUs);  // every frame that shows new paint is an update
        if (!options.frames.isEmpty())
            doc.projection()
                .scaled(1000, 1000, Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
                .save(QStringLiteral("%1/frame-%2.png").arg(options.frames).arg(shownAtUs.size(), 4, 10, QLatin1Char('0')));
        if (const std::int64_t stamp = controller.shownStampUs(); stamp > 0 && stamp != lastStamp) {
            inkUs.push_back(lastFrameUs - stamp);
            lastStamp = stamp;
        }
    };
    const auto frameIfDue = [&] {
        if (nowUs() - lastFrameUs >= kFrameUs)
            showFrame();
    };
    if (options.preserveOpacity) {
        LayerProps props = doc.layer(doc.currentLayerId())->props;
        props.preserveOpacity = true;
        (void)doc.setLayerProps(doc.currentLayerId(), props);
    }
    controller.paintInBackground([] {}, showFrame);
    const double cpuStart = cpuSeconds(RUSAGE_SELF), mainStart = cpuSeconds(RUSAGE_THREAD);
    const std::int64_t startUs = nowUs();
    const std::uint64_t firstMs = stroke.samples.front().timestampMs;
    // The Clone Stamp's source, as set for the reference: an Alt tap this far left of where the stroke starts.
    constexpr double kCloneSourceLeft = 600.0;  // px
    if (options.tool == ToolType::Clone)
        controller.cloneStamp().setSource(stroke.samples.front().pos - QPointF(kCloneSourceLeft, 0.0));
    if (!controller.begin(stroke.samples.front(), options.colour, settingsFor(options), options.stabilizer).ok())
        return;
    controller.setViewLevel(options.viewLevel);
    for (std::size_t i = 1; i < stroke.samples.size(); ++i) {
        const std::int64_t dueUs = startUs + std::int64_t(stroke.samples[i].timestampMs - firstMs) * 1000;
        while (nowUs() < dueUs) {
            frameIfDue();
            std::this_thread::sleep_for(std::chrono::microseconds(300));
        }
        controller.add(stroke.samples[i]);
    }
    const std::int64_t liftUs = nowUs();
    const std::size_t updatesWithPenDown = shownAtUs.size();
    controller.finishInBackground();
    while (controller.isFinishing() && !controller.isPainted()) {
        frameIfDue();
        std::this_thread::sleep_for(std::chrono::microseconds(300));
    }
    showFrame();
    const std::int64_t paintedUs = nowUs();
    controller.finish();
    const double finishMs = double(nowUs() - paintedUs) / 1000.0;
    const double cpu = cpuSeconds(RUSAGE_SELF) - cpuStart, mainCpu = cpuSeconds(RUSAGE_THREAD) - mainStart;
    std::vector<std::int64_t> gapsUs;
    for (std::size_t i = 1; i < shownAtUs.size(); ++i)
        gapsUs.push_back(shownAtUs[i] - shownAtUs[i - 1]);
    if (qEnvironmentVariableIsSet("BENCH_GAPS")) {  // every update's time from the first event and the gap before it, ms; + after the lift
        for (std::size_t i = 0; i < shownAtUs.size(); ++i)
            std::printf("update %zu at %.1f gap %.1f%s\n", i, double(shownAtUs[i] - startUs) / 1000.0,
                        i ? double(shownAtUs[i] - shownAtUs[i - 1]) / 1000.0 : 0.0, shownAtUs[i] > liftUs ? " +" : "");
    }
    std::ranges::sort(gapsUs);
    const std::int64_t gapUs = gapsUs.empty() ? 0 : gapsUs.back();
    const auto gapAt = [&](double share) {
        return gapsUs.empty() ? 0 : gapsUs[static_cast<std::size_t>(share * double(gapsUs.size() - 1))];
    };
    std::ranges::sort(inkUs);
    const auto ms = [](std::int64_t us) { return double(us) / 1000.0; };
    std::printf("live tool=%s px=%.0f used_px=%.0f side=%d cores=%d events=%zu drawn_ms=%.0f first_ink_ms=%.1f ink_med_ms=%.1f "
                "ink_worst_ms=%.1f gap_med_ms=%.1f gap_p90_ms=%.1f gap_worst_ms=%.1f "
                "after_lift_ms=%.1f finish_ms=%.1f updates=%zu updates_pen_down=%zu total_ms=%.0f cpu_s=%.2f main_cpu_s=%.2f peak_mib=%.0f "
                "stopped=%d\n",
                qPrintable(toolTypeName(options.tool).replace(u' ', u'_')), options.sizePx, limitedTo, doc.size().width(),
                cpu_cores::used(), stroke.samples.size(), double(stroke.samples.back().timestampMs - firstMs),
                shownAtUs.empty() ? -1.0 : ms(shownAtUs.front() - startUs), inkUs.empty() ? -1.0 : ms(inkUs[inkUs.size() / 2]),
                inkUs.empty() ? -1.0 : ms(inkUs.back()), ms(gapAt(0.5)), ms(gapAt(0.9)), ms(gapUs), ms(paintedUs - liftUs), finishMs,
                shownAtUs.size(), updatesWithPenDown, ms(paintedUs - startUs), cpu, mainCpu, statusMiB("VmHWM:"), stopped ? 1 : 0);
}

// The picture repeated over the layer, for a stroke to have paint under it on a canvas of any size.
void tileLayer(Document& doc, const QString& path) {
    const QImage picture(path);
    if (picture.isNull())
        return;
    TiledImage* layer = doc.paintTarget(doc.currentLayerId()).value();
    for (int y = 0; y < doc.size().height(); y += picture.height())
        for (int x = 0; x < doc.size().width(); x += picture.width())
            layer->writeImage(picture, {x, y});
}

void paint(Document& doc, const RecordedStroke& stroke, const Options& options) {
    const BrushSettings settings = settingsFor(options);
    QObject::connect(&doc, &Document::strokeLimited, [](int chosen, int used) {
        std::printf("  the brush kept to %.1f px instead of %.1f px\n", used / 10.0, chosen / 10.0);
    });
    QObject::connect(&doc, &Document::strokeStopped, [](int size) { std::printf("  stopped partway at %.1f px\n", size / 10.0); });
    StrokeController controller(&doc);
    const auto showFrame = [&controller] {
        if (controller.hasUnshownPaint())
            controller.showPaint();
    };
    if (options.background)
        controller.paintInBackground([] {}, showFrame);
    if (!controller.begin(stroke.samples.front(), options.colour, settings, options.stabilizer).ok())
        return;
    for (std::size_t i = 1; i + 1 < stroke.samples.size(); ++i) {
        controller.add(stroke.samples[i]);
        if (options.background)
            showFrame();
    }
    controller.finish();
}

}

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    if (argc < 3) {
        std::fprintf(stderr,
                     "usage: %s <side px> <stroke.jsonl> [--cores <n>] [--tool <name>] [--size <px>] [--stroke-only] [--no-limit] "
                     "[--usable <MiB>] [--background] [--live] [--stroke <n>] [--scale <factor>] [--min-size <percent>] [--layer <png>] "
                     "[--stabilizer <level>] [--all-pressures] [--dilution <%%>] [--persistence <%%>] [--blending <%%>] [--dump <file>]\n",
                     argv[0]);
        return 2;
    }
    const Options options = optionsFrom(argc, argv);
    if (options.cores > 0)
        cpu_cores::setLimit(options.cores);
    if (options.noLimit)
        stroke_memory::usableOverride() = -1;
    else if (options.usableMiB > 0)
        stroke_memory::usableOverride() = options.usableMiB << 20;
    const int side = std::atoi(argv[1]);
    const std::vector<RecordedStroke> strokes = readRecording(QString::fromLocal8Bit(argv[2]));
    if (std::size_t(options.strokeIndex) >= strokes.size())
        return 2;
    RecordedStroke stroke = scaled(strokes[std::size_t(options.strokeIndex)], options.scale > 0.0 ? options.scale : side / kRecordedSide);
    if (options.mouse) {
        for (InputSample& sample : stroke.samples)
            sample.kind = PointerKind::Mouse;
    }
    if (options.live) {
        Document live(QSize(side, side), Qt::white);
        if (!options.layer.isEmpty())
            tileLayer(live, options.layer);
        // --before: what a painter does first, as the app does it: the Bucket over the whole layer then Delete, or the stroke painted and undone.
        if (options.before == QLatin1String("fill-clear")) {
            check(live.fillLayer(live.currentLayerId(), Qt::white).ok(), "fill");
            const std::size_t filled = live.layer(live.currentLayerId())->pixels.tileCount();
            check(live.clearLayer(live.currentLayerId()).ok(), "clear");
            std::printf("before: filled %zu tiles, %zu left after the clear, %.0f MiB held\n", filled,
                        live.layer(live.currentLayerId())->pixels.tileCount(), statusMiB("VmRSS:"));
        } else if (options.before == QLatin1String("painted-undone")) {
            paintLive(live, stroke, options);
            check(live.undo().ok(), "undo");
            std::printf("before: painted and undone, %zu tiles left, %.0f MiB held\n",
                        live.layer(live.currentLayerId())->pixels.tileCount(), statusMiB("VmRSS:"));
        }
        rusage faultsBefore{};
        getrusage(RUSAGE_SELF, &faultsBefore);
        paintLive(live, stroke, options);
        rusage faultsAfter{};
        getrusage(RUSAGE_SELF, &faultsAfter);
        std::printf("faults %ld\n", faultsAfter.ru_minflt - faultsBefore.ru_minflt);
        if (!options.dump.isEmpty()) {
            QFile file(options.dump);
            if (!file.open(QIODevice::WriteOnly))
                return 3;
            for (const auto& [coord, tile] : live.layer(live.currentLayerId())->pixels.tiles()) {
                const std::array<std::int32_t, 2> place = {coord.x, coord.y};
                file.write(reinterpret_cast<const char*>(place.data()), sizeof place);
                file.write(reinterpret_cast<const char*>(tile->px.data()), sizeof tile->px);
            }
        }
        return 0;
    }
    QTemporaryDir folder;
    const QString path = folder.filePath(QStringLiteral("big.doodle"));
    std::printf("%d × %d, the %s at %.0f px, %d cores\n", side, side, qPrintable(toolTypeName(options.tool)), options.sizePx,
                cpu_cores::used());
    step("start", [] {});
    std::unique_ptr<Document> doc;
    step("new canvas", [&] { doc = std::make_unique<Document>(QSize(side, side), Qt::white); });
    step("the whole picture, first time", [&] { doc->projection(); });
    step("two more layers", [&] { check(doc->addLayer().ok() && doc->addLayer().ok(), "adding a layer"); });
    step("the stroke", [&] { paint(*doc, stroke, options); });
    const std::size_t tiles = doc->layer(doc->currentLayerId())->pixels.tileCount();
    std::printf("%-34s %9zu tiles, %.0f Mpx\n", "its layer", tiles, double(tiles) * Tile::kSize * Tile::kSize / 1e6);
    if (options.strokeOnly)
        return 0;
    step("the whole picture after it", [&] { doc->projection(); });
    step("its layer's thumbnail", [&] { doc->layer(doc->currentLayerId())->pixels.thumbnail(kLayerThumbnail); });
    step("the frame's thumbnail", [&] { doc->cellThumbnail(0, 0, kFrameThumbnail); });
    step("undo the stroke", [&] { check(doc->undo().ok(), "undo"); });
    step("redo it", [&] { check(doc->redo().ok(), "redo"); });
    step("save", [&] { check(io::saveFile(doc->content(), path, DoodleFormat()).ok(), "saving"); });
    std::printf("%-34s %9.0f MiB\n", "the file", double(QFileInfo(path).size()) / (1024.0 * 1024.0));
    step("close", [&] { doc.reset(); });
    step("open", [&] {
        auto loaded = io::loadFile(path);
        check(loaded.ok(), "opening");
        if (loaded.ok())
            doc = std::make_unique<Document>(loaded.value().content);  // its tiles are shared, not copied
    });
    step("the whole picture, opened", [&] {
        if (doc)
            doc->projection();
    });
    return 0;
}
