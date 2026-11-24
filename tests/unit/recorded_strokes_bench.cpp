// Plays the recorded slow strokes in real time through the app's own stroke path, a
// StrokeController painting in the background, and says how old the newest pen event was each time a frame showed the stroke's
// paint, as DOODLEBOLD_TIMING_LOG's ink latency does, and how long the stroke went on painting after the lift. It ends with a
// checksum of the canvas, which a change that only makes painting faster must leave as it was. Not a test: run by hand.
//
//   doodlebold_recorded_strokes_bench <input log> [<tools.json> [<only stroke>]]
//   doodlebold_recorded_strokes_bench --check <input log> <tools.json>
//   doodlebold_recorded_strokes_bench --marker-sizes <input log> <tools.json> [<px>]
//   doodlebold_recorded_strokes_bench --sizes <input log> <tool> <px,px,...> [<layer.png>]
//
// --sizes is --marker-sizes for any painting tool (pencil, brush, water, marker, eraser, blur, smudge) at the sizes given, with its
// starting values but Min Size 100 %, as the on-screen runs of the view bench have it, on an empty
// layer or on the 3000 × 3000 picture given.
//
//   doodlebold_recorded_strokes_bench --render <input log> <tools.json> <px> <out.png> [<layer.png or -> <R,G,B>]
//
// --render paints the Marker's stroke (RENDER_TOOL another tool's, RENDER_MINSIZE at that Min Size) as --marker-sizes does, in blue (32, 80, 200) on an empty layer or in the colour given, over a
// starting layer if one is given, and writes the canvas over white as a PNG, as the reference's are rendered. A log of
// fewer than seven strokes is painted whole.
//
// --marker-sizes paints the longest stroke, the Marker's, with the Marker at 100 to 2000 px, each on a fresh canvas:
// how long it takes as fast as it goes, and its ink latency in real time. Given a size, only that one, only as
// fast as it goes.
//
// --check is how the tests use it: the Marker's and Water Color's strokes, and the Marker's at 1000 px, painted as fast as they go
// rather than in real time, each
// the best of kRuns, must take no more than their budget times as long as they took to draw, or it exits with 1. Before
// At first Water Color took 2 to 2.6 times as long and its ink fell a second behind, and the Marker about 1 times; now about 1.2 and
// 0.5. The best of a few runs is what a busy machine moves least.
//
// The input log is the DOODLEBOLD_INPUT_LOG, tests/fixtures/recorded-3k-strokes.jsonl; its strokes are painted in order, one after
// the other, on one 3000 × 3000 canvas, each with the tool and size the timing log gave it, at Stabilizer 12. Given the
// tools.json, tests/fixtures/recorded-3k-tools.json, each takes the settings of the slot of that tool nearest its size; otherwise
// the tool's starting values. A frame is shown whenever the stroke has news, at most every kFrameMs, as the canvas does.
// BENCH_CORES=<n> paints with n cores (as taskset does, since the app counts only the cores it may run on).

#include "core/brush/BrushSettings.h"
#include "core/document/Document.h"
#include "core/paint/StrokeController.h"
#include "core/raster/Channel.h"
#include "core/tools/ToolBox.h"
#include "core/tools/ToolBoxStore.h"
#include "core/util/CpuCores.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QFile>
#include <QImage>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iterator>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <memory>
#include <optional>
#include <thread>
#include <utility>
#include <vector>

using namespace doodlebold;

namespace {

constexpr int kStabilizer = 12;
constexpr std::int64_t kFrameUs = 7000;
constexpr int kRuns = 3;

struct LoggedStroke {
    ToolType tool;
    double sizePx;
    const char* name;
    double budget = 0.0;  // for --check: the most times as long as it took to draw that painting it may take
    int minSize = -1;  // Min Size, %, in place of the settings'
};

// The timing log, ~/Downloads/timing-3k.jsonl, in the order the strokes were painted.
const LoggedStroke kStrokes[] = {
    {ToolType::Pencil, 104.2, "Pencil 104 px"},      {ToolType::Brush, 176.7, "Brush 177 px"},
    {ToolType::Brush, 176.7, "Brush 177 px"},        {ToolType::Brush, 721.9, "Brush 722 px"},
    {ToolType::Brush, 721.9, "Brush 722 px"},        {ToolType::Brush, 721.9, "Brush 722 px"},
    {ToolType::Marker, 161.8, "Marker 162 px", 0.8}, {ToolType::WaterColor, 554.5, "Water Color 555 px", 1.8},
};

std::vector<std::vector<InputSample>> readStrokes(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    std::vector<std::vector<InputSample>> strokes;
    std::optional<std::vector<InputSample>> running;
    for (const QByteArray& text : file.readAll().split('\n')) {
        const QJsonObject line = QJsonDocument::fromJson(text).object();
        const QString event = line["e"].toString();
        InputSample s;
        s.pos = QPointF(line["x"].toDouble(), line["y"].toDouble());
        s.pressure = line["p"].toDouble();
        s.tiltX = line["tx"].toDouble();
        s.tiltY = line["ty"].toDouble();
        s.rotation = line["r"].toDouble();
        s.timestampMs = static_cast<std::uint64_t>(line["t"].toDouble());
        s.kind = static_cast<PointerKind>(line["kind"].toInt());
        if (event == QLatin1String("press")) {
            running.emplace(1, s);
        } else if (running && event == QLatin1String("move") && s.kind == running->front().kind) {
            running->push_back(s);
        } else if (running && event == QLatin1String("release")) {
            strokes.push_back(std::move(*running));
            running.reset();
        }
    }
    return strokes;
}

std::int64_t nowUs() {
    return std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

struct Shown {
    std::vector<std::int64_t> inkUs;
    std::int64_t lastStamp = 0;
    std::int64_t lastFrameUs = 0;

    void frame(StrokeController& stroke) {
        if (!stroke.hasUnshownPaint())
            return;
        stroke.showPaint();
        lastFrameUs = nowUs();
        if (const std::int64_t stamp = stroke.shownStampUs(); stamp > 0 && stamp != lastStamp) {
            inkUs.push_back(lastFrameUs - stamp);
            lastStamp = stamp;
        }
    }
    void frameIfDue(StrokeController& stroke) {
        if (nowUs() - lastFrameUs >= kFrameUs)
            frame(stroke);
    }
};

// The settings of the tray's slot of `tool` nearest `size`, or the tool's starting values if it has none.
BrushSettings settingsFor(const ToolBox* tools, ToolType tool, int size) {
    BrushSettings best = defaultSettings(tool);
    int nearest = -1;
    for (int g = 0; tools && g < tools->groupCount(); ++g)
        for (const std::optional<ToolSlot>& slot : tools->group(g).cells)
            if (slot && slot->type() == tool && (nearest < 0 || std::abs(slot->settings.size - size) < nearest)) {
                nearest = std::abs(slot->settings.size - size);
                best = slot->settings;
            }
    return best;
}

// --sizes: the layer a canvas starts with, if one is given.
QImage& startingLayer() {
    static QImage layer;
    return layer;
}

// A 3000 × 3000 canvas, white, with the starting layer on it if there is one.
std::unique_ptr<Document> freshCanvas() {
    auto doc = std::make_unique<Document>(QSize(3000, 3000), Qt::white);
    if (!startingLayer().isNull())
        doc->paintTarget(doc->currentLayerId()).value()->writeImage(startingLayer(), {0, 0});
    return doc;
}

// The slot of the tool nearest the size, or its starting values, at the size.
BrushSettings settingsOf(const LoggedStroke& logged, const ToolBox* tools) {
    BrushSettings settings = settingsFor(tools, logged.tool, static_cast<int>(std::lround(logged.sizePx * 10.0)));
    settings.size = static_cast<int>(std::lround(logged.sizePx * 10.0));
    if (logged.minSize >= 0)
        settings.minSize = logged.minSize;
    return settings;
}

void play(Document& doc, const std::vector<InputSample>& samples, const LoggedStroke& logged, const ToolBox* tools) {
    const BrushSettings settings = settingsOf(logged, tools);
    StrokeController stroke(&doc);
    Shown shown;
    stroke.paintInBackground([] {}, [&] { shown.frame(stroke); });
    const std::int64_t startUs = nowUs();
    const auto dueUs = [&](const InputSample& s) { return startUs + std::int64_t(s.timestampMs - samples.front().timestampMs) * 1000; };
    if (!stroke.begin(samples.front(), QColor(40, 60, 140), settings, kStabilizer).ok()) {
        std::printf("%s: refused\n", logged.name);
        return;
    }
    for (std::size_t i = 1; i < samples.size(); ++i) {
        while (nowUs() < dueUs(samples[i])) {
            shown.frameIfDue(stroke);
            std::this_thread::sleep_for(std::chrono::microseconds(300));
        }
        stroke.add(samples[i]);
    }
    const std::int64_t liftUs = nowUs();
    stroke.finish();
    const std::int64_t catchUpUs = nowUs() - liftUs;
    std::vector<std::int64_t> ink = shown.inkUs;
    std::ranges::sort(ink);
    const auto ms = [](std::int64_t us) { return double(us) / 1000.0; };
    std::printf("%-20s %6zu %9.1f %9.1f %10.1f\n", logged.name, samples.size(), ink.empty() ? 0.0 : ms(ink[ink.size() / 2]),
                ink.empty() ? 0.0 : ms(ink.back()), ms(catchUpUs));
}

// The stroke painted on this thread, as fast as it goes, from a fresh canvas each time: how long it took, at best, in ms.
double bestPaintMs(const std::vector<InputSample>& samples, const LoggedStroke& logged, const ToolBox* tools, int runs = kRuns) {
    const BrushSettings settings = settingsOf(logged, tools);
    double best = 1e18;
    for (int run = 0; run < runs; ++run) {
        const std::unique_ptr<Document> canvas = freshCanvas();
        Document& doc = *canvas;
        StrokeController stroke(&doc);
        const std::int64_t startUs = nowUs();
        if (!stroke.begin(samples.front(), QColor(40, 60, 140), settings, kStabilizer).ok())
            return -1.0;
        for (std::size_t i = 1; i < samples.size(); ++i)
            stroke.add(samples[i]);
        stroke.finish();
        best = std::min(best, double(nowUs() - startUs) / 1000.0);
    }
    return best;
}

int markerSizes(const std::vector<std::vector<InputSample>>& strokes, const ToolBox* tools, double onlyPx) {
    if (strokes.size() < 7)
        return 2;
    const std::vector<InputSample>& longest = strokes[6];
    if (onlyPx > 0.0) {
        std::printf("Marker %.0f px: %.0f ms as fast as it goes\n", onlyPx,
                    bestPaintMs(longest, {ToolType::Marker, onlyPx, "Marker"}, tools, 1));
        return 0;
    }
    const double drawnMs = double(longest.back().timestampMs - longest.front().timestampMs);
    std::printf("the Marker's stroke, drawn in %.0f ms\n", drawnMs);
    std::printf("%-20s %6s %9s %9s %10s %12s\n", "stroke", "events", "ink med", "ink worst", "catch-up ms", "fast paint ms");
    static QByteArray names[6];
    int n = 0;
    for (const double px : {100.0, 160.0, 250.0, 500.0, 1000.0, 2000.0}) {
        names[n] = QByteArray("Marker ") + QByteArray::number(px) + " px";
        const LoggedStroke logged{ToolType::Marker, px, names[n++].constData()};
        Document doc(QSize(3000, 3000), Qt::white);
        play(doc, longest, logged, tools);
        std::printf("%62.0f\n", bestPaintMs(longest, logged, tools, 1));
    }
    return 0;
}

// The layer over white, at 1:1, as doodlebold_replay_case writes it.
QImage overWhite(const TiledImage& layer, const QSize& size) {
    QImage out(size, QImage::Format_RGBA8888);
    for (int y = 0; y < size.height(); ++y) {
        std::uint8_t* line = out.scanLine(y);
        for (int x = 0; x < size.width(); ++x) {
            const std::array<Channel, 4> p = layer.pixel(x, y);
            const double a = p[3] * double(kChannelToFloat);
            for (int i = 0; i < 3; ++i)
                line[x * 4 + i] =
                    static_cast<std::uint8_t>(std::lround(std::clamp(p[i] * double(kChannelToFloat) + 1.0 - a, 0.0, 1.0) * 255.0));
            line[x * 4 + 3] = 255;
        }
    }
    return out;
}

int render(const std::vector<std::vector<InputSample>>& strokes, const ToolBox* tools, const QStringList& args) {
    if (strokes.empty() || args.size() < 2)
        return 2;
    const double px = args[0].toDouble();
    // RENDER_TOOL paints with that tool's starting values (its tools.json key, as "blur"), and RENDER_MINSIZE sets Min Size, %.
    const std::optional<ToolType> named = toolTypeFromKey(qEnvironmentVariable("RENDER_TOOL"));
    BrushSettings settings =
        settingsFor(named ? nullptr : tools, named.value_or(ToolType::Marker), static_cast<int>(std::lround(px * 10.0)));
    settings.size = static_cast<int>(std::lround(px * 10.0));
    if (qEnvironmentVariableIsSet("RENDER_MINSIZE"))
        settings.minSize = qEnvironmentVariableIntValue("RENDER_MINSIZE");
    Document doc(QSize(3000, 3000), Qt::white);
    QColor colour(32, 80, 200);
    if (args.size() >= 4) {
        if (args[2] != QLatin1String("-")) {
            const QImage start(args[2]);
            if (start.isNull())
                return 2;
            doc.paintTarget(doc.currentLayerId()).value()->writeImage(start, {0, 0});
        }
        const QStringList rgb = args[3].split(u',');
        colour = QColor(rgb.value(0).toInt(), rgb.value(1).toInt(), rgb.value(2).toInt());
    }
    const std::vector<std::vector<InputSample>> painted = strokes.size() >= 7 ? std::vector<std::vector<InputSample>>{strokes[6]} : strokes;
    for (const std::vector<InputSample>& samples : painted) {
        StrokeController stroke(&doc);
        if (!stroke.begin(samples.front(), colour, settings, kStabilizer).ok())
            return 1;
        for (std::size_t i = 1; i < samples.size(); ++i)
            stroke.add(samples[i]);
        stroke.finish();
    }
    return overWhite(doc.layer(doc.currentLayerId())->pixels, doc.size()).save(args[1]) ? 0 : 1;
}

// The Marker stroke again at 1000 px: before, 21 to 27 times as long as drawing it.
const LoggedStroke kWideMarker{ToolType::Marker, 1000.0, "Marker 1000 px", 1.8};

int toolSizes(const std::vector<std::vector<InputSample>>& strokes, ToolType tool, const QStringList& sizes) {
    if (strokes.size() < 7)
        return 2;
    const std::vector<InputSample>& longest = strokes[6];
    std::printf("stroke 6, drawn in %.0f ms\n", double(longest.back().timestampMs - longest.front().timestampMs));
    std::printf("%-20s %6s %9s %9s %10s %12s\n", "stroke", "events", "ink med", "ink worst", "catch-up ms", "fast paint ms");
    for (const QString& px : sizes) {
        const QByteArray name = toolTypeName(tool).toLatin1() + " " + px.toLatin1() + " px";
        const LoggedStroke logged{tool, px.toDouble(), name.constData(), 0.0, 100};
        play(*freshCanvas(), longest, logged, nullptr);
        std::printf("%62.0f\n", bestPaintMs(longest, logged, nullptr, 1));
    }
    return 0;
}

int check(const std::vector<std::vector<InputSample>>& strokes, const ToolBox* tools) {
    bool passed = true;
    for (const auto& [i, logged] :
         {std::pair{std::size_t(6), kStrokes[6]}, std::pair{std::size_t(7), kStrokes[7]}, std::pair{std::size_t(6), kWideMarker}}) {
        if (i >= strokes.size())
            return 2;
        const double drawnMs = double(strokes[i].back().timestampMs - strokes[i].front().timestampMs);
        const double paintMs = bestPaintMs(strokes[i], logged, tools);
        const bool ok = paintMs >= 0.0 && paintMs <= logged.budget * drawnMs;
        std::printf("%-20s drawn in %6.0f ms, painted in %6.0f ms at best: %.2f times %s\n", logged.name, drawnMs, paintMs,
                    paintMs / drawnMs, ok ? "" : "FAILED");
        passed = passed && ok;
    }
    return passed ? 0 : 1;
}

}

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    // BENCH_CORES=<n> paints with that many cores, as a machine with that many would.
    if (qEnvironmentVariableIsSet("BENCH_CORES"))
        cpu_cores::setLimit(qEnvironmentVariableIntValue("BENCH_CORES"));
    if (argc < 2) {
        std::fprintf(stderr, "usage: %s <input log> [<tools.json> [<only stroke>]]\n", argv[0]);
        return 2;
    }
    const bool checking = QString::fromLocal8Bit(argv[1]) == QLatin1String("--check");
    const bool sizing = QString::fromLocal8Bit(argv[1]) == QLatin1String("--marker-sizes");
    const bool rendering = QString::fromLocal8Bit(argv[1]) == QLatin1String("--render");
    if (QString::fromLocal8Bit(argv[1]) == QLatin1String("--sizes") && argc >= 5) {
        const std::vector<std::vector<InputSample>> strokes = readStrokes(QString::fromLocal8Bit(argv[2]));
        const QString name = QString::fromLocal8Bit(argv[3]);
        std::optional<ToolType> tool = toolTypeFromKey(name == QLatin1String("water") ? QStringLiteral("watercolor") : name);
        if (!tool) {
            std::fprintf(stderr, "no tool called %s\n", argv[3]);
            return 2;
        }
        if (argc >= 6)
            startingLayer() = QImage(QString::fromLocal8Bit(argv[5]));
        return toolSizes(strokes, *tool, QString::fromLocal8Bit(argv[4]).split(u','));
    }
    if (checking || sizing || rendering) {
        ++argv;
        --argc;
    }
    const std::vector<std::vector<InputSample>> strokes = readStrokes(QString::fromLocal8Bit(argv[1]));
    std::optional<ToolBox> tools;
    QTemporaryDir copies;  // the store renames a file it refuses, so it reads a copy
    if (argc > 2 && QString::fromLocal8Bit(argv[2]) != QLatin1String("-")) {
        tools.emplace();
        const QString copy = copies.filePath(QStringLiteral("tools.json"));
        QFile::copy(QString::fromLocal8Bit(argv[2]), copy);
        ToolBoxStore store(&*tools, copy);
        if (!store.load().ok()) {
            std::fprintf(stderr, "%s: not a tool set this build reads\n", argv[2]);
            return 2;
        }
    }
    if (checking)
        return check(strokes, tools ? &*tools : nullptr);
    if (rendering) {
        QStringList rest;
        for (int i = 3; i < argc; ++i)
            rest << QString::fromLocal8Bit(argv[i]);
        return render(strokes, tools ? &*tools : nullptr, rest);
    }
    if (sizing)
        return markerSizes(strokes, tools ? &*tools : nullptr, argc > 3 ? std::atof(argv[3]) : 0.0);
    const int only = argc > 3 ? std::atoi(argv[3]) : -1;
    Document doc(QSize(3000, 3000), Qt::white);
    std::printf("%-20s %6s %9s %9s %10s\n", "stroke", "events", "ink med", "ink worst", "catch-up ms");
    for (std::size_t i = 0; i < strokes.size() && i < std::size(kStrokes); ++i)
        if (only < 0 || only == int(i))
            play(doc, strokes[i], kStrokes[i], tools ? &*tools : nullptr);
    const QImage& picture = doc.projection();
    const QByteArray hash = QCryptographicHash::hash(
        QByteArrayView(reinterpret_cast<const char*>(picture.constBits()), picture.sizeInBytes()), QCryptographicHash::Sha256);
    std::printf("canvas %s\n", hash.toHex().left(16).constData());
    return 0;
}
