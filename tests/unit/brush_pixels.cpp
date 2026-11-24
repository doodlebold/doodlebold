// The brush pixels (the golden results, tools/README.md): a few hundred strokes painted through the app's own stroke path,
// each kept as a checksum of the layer's 16-bit pixels, and some as PNGs to look at. A change that only makes painting faster, or
// its maths easier to read, must leave every checksum as it was.
//
//   doodlebold_brush_pixels --check <dir> [--checksums <file>] [--only <text>] [--out <dir>] [--cores <n>]
//   doodlebold_brush_pixels --compare <dump dir> [--within] [--pictures <dir>] [--only <text>]   (another build or platform)
//   doodlebold_brush_pixels --write <dir> [--checksums <file> [--only <text>]] [--cores <n>]
//   doodlebold_brush_pixels --list [--only <text>]
//   doodlebold_brush_pixels --time [--only <text>] [--cores <n>]
//   doodlebold_brush_pixels --check <dir> --dump <dir2>             (another platform: Linux's renders to compare with)
//   ... --case <name>                                                 (one case by its whole name, in place of --only)
//
// <dir> holds checksums.txt, one "<case> <sha-256>" a line, and png/<case>.png for the cases picked to be looked at.
// checksums.txt is Linux x86-64's, with glibc, the reference. Another platform whose maths library rounds differently keeps its own
// file there (--checksums checksums-windows-msvc.txt), checked to the bit like the reference's. A platform's file is written
// (--write --checksums, which writes no pictures) only once its renders pass --compare --within against the reference's dumps of
// the same commit: no channel further than kPlatformMostLevels from Linux's, in no more than kPlatformMostShare of a case's pixels.
// --check re-renders the cases whose names contain the --only text (all of them without it), saying how long each took, and
// exits with 1 if one differs. With --out it writes each one that differs there, and beside it, where <dir> has its PNG, a
// picture of what changed: the pixels that moved in magenta over the stored picture, faded. --write renders every case and
// writes <dir> again. --time prints how long each case's strokes take to paint, leaving out making its canvas, and a "work:" line with their CPU
// cycles and instructions. --cores paints
// with that many threads, as a machine with that many cores does (taskset alone leaves them at this machine's count); the
// pixels don't depend on it. --dump keeps each render's tiles in <dir2>, for --compare to go through another build's render pixel
// by pixel, and --pictures writes the two side by side with their difference.
//
// The cases, named <group>/<case>:
// - grid: every painting tool at 10, 50, 200, 600, 1500 and 4000 px along the recorded C stroke on a 3000 × 3000 canvas, at
//   Stabilizer 4 over the red | blue field (and at 0 and 12, but for 4000 px), at 4 on an empty layer, and at 4 over the field
//   with the Paper Grain texture.
// - forms: the Brush with a blot and a bristle form, at 50 and 600 px.
// - stabilizer: every level, 0 to 20, along the four C strokes with the Pencil at 6 px (the sweep's), the Brush
//   at 200 px over the field and the Marker at 117.4 px.
// - background: some of the grid painted as the canvas paints, on the stroke's own thread, showing frames on the way, never
//   grouping what's waiting into fewer dabs, which in the app goes by the clock.
// - backlog: the Brush, Water Color and Marker at 1500 and 4000 px painted the same way, but with all that's waiting grouped, as a stroke that
//   has fallen behind is.
// - recorded: the eight recorded strokes of tests/fixtures/recorded-3k-strokes.jsonl on one canvas with their tools, as
//   doodlebold_recorded_strokes_bench paints them, as fast as they go and in the background.
// - settings: grid cases with one setting changed (Opacity, pressure, Preserve Opacity, the transparent colour, a brush
//   shape and its jitters, Randomize), and the selection pens.

#ifdef __linux__
#include <linux/perf_event.h>
#include <sys/syscall.h>
#include <unistd.h>
#endif

#include "core/brush/BrushResources.h"
#include "core/brush/BrushSettings.h"
#include "core/document/Document.h"
#include "core/paint/SelectionStrokeController.h"
#include "core/paint/StrokeController.h"
#include "core/paint/StrokeMemory.h"
#include "core/raster/Channel.h"
#include "core/stroke/StrokeRecording.h"
#include "core/stroke/StrokeWorker.h"
#include "core/tools/ToolBox.h"
#include "core/tools/ToolBoxStore.h"
#include "core/util/CpuCores.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QFile>
#include <QImage>
#include <QPainter>
#include <QStringList>
#include <QTemporaryDir>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <unordered_map>
#include <vector>

using namespace doodlebold;

namespace {

// The fixtures folder: where the build found it, unless DOODLEBOLD_FIXTURE_DIR says otherwise (a checkout mounted elsewhere).
const QString kFixtures = qEnvironmentVariable("DOODLEBOLD_FIXTURE_DIR", QStringLiteral(DOODLEBOLD_FIXTURE_DIR));
const QString kStrokes = kFixtures + QStringLiteral("/brush-pixels/strokes/");
const QString kRecordedLog = kFixtures + QStringLiteral("/recorded-3k-strokes.jsonl");
const QString kRecordedTools = kFixtures + QStringLiteral("/recorded-3k-tools.json");
constexpr QSize kSmallCanvas(768, 512);  // the reference cases', for a stroke file without a starting layer
constexpr QSize kBigCanvas(3000, 3000);  // the canvas the strokes were recorded on
const QColor kBlue(32, 80, 200);
const QColor kTeal(0, 176, 176);
const QColor kRecordedColour(40, 60, 140);  // doodlebold_recorded_strokes_bench's
constexpr int kRecordedStabilizer = 12;

constexpr std::array kPaintingTools = {ToolType::Pencil, ToolType::Brush,  ToolType::WaterColor,  ToolType::Marker, ToolType::Eraser,
                                       ToolType::Blur,   ToolType::Smudge, ToolType::PixelArtPen, ToolType::Clone};
constexpr std::array kSizesPx = {10, 50, 200, 600, 1500, 4000};

struct Case {
    QString name;
    QString strokes;  // a stroke file
    ToolType tool = ToolType::Pencil;
    double sizePx = 0.0;  // 0: the tool's starting size
    int minSize = -1;  // Min Size, %; -1: the tool's starting value
    int stabilizer = 0;
    QSize canvas = kSmallCanvas;
    QString layer;  // a starting picture, the canvas's size; none: an empty layer
    QString texture = BrushResourceLibrary::kNoTexture;
    QString form = BrushResourceLibrary::kSimpleCircle;
    QColor colour = kBlue;
    bool inBackground = false;
    bool backlog = false;  // in the background, with what's waiting always grouped into fewer dabs; otherwise never
    bool recorded = false;  // the eight recorded strokes with their own tools, sizes and colour
    bool png = false;
    // What the case sets on the tool's settings, last; what it paints on, the layer or the selection (with the Selection Pen or
    // Eraser); the layer's Preserve Opacity; and a rectangle selected as it paints (for the selection pens, the selection they
    // start from).
    std::function<void(BrushSettings&)> adjust;
    enum class Target {
        Layer,
        SelectionPen,
        SelectionEraser
    };
    Target target = Target::Layer;
    bool preserveOpacity = false;
    bool inSelection = false;
};

QString sizeName(double px) {
    return QString::number(px, 'g', 6) + QStringLiteral("px");
}

QString toolName(ToolType tool) {
    return toolTypeKey(tool);
}

Case gridCase(ToolType tool, int px, int level, const QString& layer, const QString& texture = BrushResourceLibrary::kNoTexture) {
    Case c;
    c.strokes = kStrokes + QStringLiteral("recorded-c5-3k.jsonl");
    c.canvas = kBigCanvas;
    c.tool = tool;
    c.sizePx = px;
    c.stabilizer = level;
    c.layer = layer.isEmpty() ? QString() : kStrokes + layer;
    c.texture = texture;
    const QString surface = layer.isEmpty() ? QStringLiteral("empty") : QStringLiteral("painted");
    const QString textured =
        texture == BrushResourceLibrary::kNoTexture ? QString() : QStringLiteral("-") + texture.toLower().replace(' ', '-');
    c.name = QStringLiteral("grid/%1-%2-s%3-%4%5").arg(toolName(tool), sizeName(px)).arg(level).arg(surface, textured);
    return c;
}

// The tools that mix with what's under them, whose pictures at 600 px are kept as well as at 50.
bool mixes(ToolType tool) {
    return tool == ToolType::Brush || tool == ToolType::WaterColor || tool == ToolType::Marker || tool == ToolType::Blur ||
           tool == ToolType::Smudge;
}

void addGrid(std::vector<Case>& cases) {
    for (const ToolType tool : kPaintingTools)
        for (const int px : kSizesPx) {
            for (const int level : {0, 4, 12}) {
                if (level != 4 && px == kSizesPx.back())
                    continue;  // the widest is slow, and its path at other levels is covered at the other sizes
                Case c = gridCase(tool, px, level, QStringLiteral("field-3k.png"));
                c.png = level == 4 && (px == 50 || (px == 600 && mixes(tool)));
                cases.push_back(c);
            }
            cases.push_back(gridCase(tool, px, 4, QString()));
            cases.push_back(gridCase(tool, px, 4, QStringLiteral("field-3k.png"), QStringLiteral("Paper Grain")));
        }
}

void addForms(std::vector<Case>& cases) {
    for (const QString& form : {QStringLiteral("Blots"), QStringLiteral("Flat Bristle")})
        for (const int px : {50, 600}) {
            Case c = gridCase(ToolType::Brush, px, 4, QStringLiteral("field-3k.png"));
            c.form = form;
            c.name = QStringLiteral("forms/brush-%1-%2").arg(sizeName(px), form.toLower().replace(' ', '-'));
            cases.push_back(c);
        }
}

Case stabilizerCase(const QString& strokes, ToolType tool, double px, int level) {
    Case c;
    c.strokes = kStrokes + strokes + QStringLiteral(".jsonl");
    c.tool = tool;
    c.sizePx = px;
    c.stabilizer = level;
    c.name = QStringLiteral("stabilizer/%1-%2-%3-s%4").arg(strokes, toolName(tool), sizeName(px)).arg(level);
    c.png = strokes == QLatin1String("recorded-c5") && (level == 0 || level == 10 || level == 20);
    return c;
}

void addStabilizer(std::vector<Case>& cases) {
    for (int level = 0; level <= StabilizerLevel::kMax; ++level) {
        for (const QString& strokes :
             {QStringLiteral("recorded-c1"), QStringLiteral("recorded-c3"), QStringLiteral("recorded-c4"), QStringLiteral("recorded-c5")})
            cases.push_back(stabilizerCase(strokes, ToolType::Pencil, 6.0, level));
        Case brush = stabilizerCase(QStringLiteral("recorded-c5"), ToolType::Brush, 200.0, level);
        brush.layer = kStrokes + QStringLiteral("field.png");
        cases.push_back(brush);
        Case marker = stabilizerCase(QStringLiteral("recorded-c5-marker"), ToolType::Marker, 117.4, level);
        marker.minSize = 100;
        marker.colour = kTeal;
        marker.png = level == 0 || level == 10 || level == 20;
        cases.push_back(marker);
    }
}

void addBackground(std::vector<Case>& cases) {
    for (const ToolType tool :
         {ToolType::Pencil, ToolType::Brush, ToolType::WaterColor, ToolType::Marker, ToolType::Blur, ToolType::Smudge})
        for (const int px : {200, 1500}) {
            Case c = gridCase(tool, px, 4, QStringLiteral("field-3k.png"));
            c.inBackground = true;
            c.name.replace(QStringLiteral("grid/"), QStringLiteral("background/"));
            cases.push_back(c);
        }
}

void addBacklog(std::vector<Case>& cases) {
    // The Marker too, which lays a dab an event however many wait (stage 3): its backlog paints as it does one event at a time.
    for (const ToolType tool : {ToolType::Brush, ToolType::WaterColor, ToolType::Marker})
        for (const int px : {1500, 4000})
            for (const QString& layer : {QString(), QStringLiteral("field-3k.png")}) {
                Case c = gridCase(tool, px, 4, layer);
                c.inBackground = true;
                c.backlog = true;
                c.name.replace(QStringLiteral("grid/"), QStringLiteral("backlog/"));
                cases.push_back(c);
            }
}

void addRecorded(std::vector<Case>& cases) {
    for (const bool inBackground : {false, true}) {
        Case c;
        c.strokes = kRecordedLog;
        c.canvas = kBigCanvas;
        c.recorded = true;
        c.stabilizer = kRecordedStabilizer;
        c.colour = kRecordedColour;
        c.inBackground = inBackground;
        c.png = !inBackground;
        c.name = inBackground ? QStringLiteral("recorded/eight-strokes-background") : QStringLiteral("recorded/eight-strokes");
        cases.push_back(c);
    }
}

BrushSettings settingsOf(const Case& c);

// Settings the grid leaves at the tool's start, each changed on its own from a grid case (its twin: the same name with
// "-s4-painted" for the setting): Opacity at half, pressure turned on and off for Size and Density (only where that isn't the
// tool's start already), Preserve Opacity and the transparent colour; a brush shape with and without its jitters, and the bristles' Randomize, which draw on the stroke's own
// random numbers; and the Selection Pen and Eraser, whose checksum is of the selection they leave.
void addSettings(std::vector<Case>& cases) {
    const auto variant = [&](Case twin, const QString& setting, std::function<void(BrushSettings&)> adjust) {
        Case c = std::move(twin);
        const BrushSettings before = settingsOf(c);
        c.adjust = std::move(adjust);
        if (settingsOf(c) == before)
            return;  // the tool starts this way, so its twin covers it
        // Without "-s4-painted", which would put the 1500 px cases in the subset before a commit.
        c.name = QStringLiteral("settings/") + c.name.section(QLatin1Char('/'), 1).remove(QStringLiteral("-s4-painted")) +
                 QLatin1Char('-') + setting;
        cases.push_back(c);
    };
    for (const ToolType tool : kPaintingTools)
        for (const int px : {50, 1500}) {
            const Case twin = gridCase(tool, px, 4, QStringLiteral("field-3k.png"));
            // Opacity paints the same as the twin for the Brush, Water Color, Blur and Smudge, and for the Marker at 50 px (2026-11-23).
            const bool opacityShows = !mixes(tool) || (tool == ToolType::Marker && px == 1500);
            if (opacityShows)
                variant(twin, QStringLiteral("opacity128"), [](BrushSettings& s) { s.opacity = 128; });
            if (tool == ToolType::PixelArtPen)
                continue;  // it doesn't read pressure
            variant(twin, QStringLiteral("pressure-on"), [](BrushSettings& s) { s.pressureSize = s.pressureDensity = true; });
            variant(twin, QStringLiteral("pressure-off"), [](BrushSettings& s) { s.pressureSize = s.pressureDensity = false; });
        }
    // Preserve Opacity, and the transparent colour for the tools that lay one: the mixing tools paint those on grids of their own.
    for (const ToolType tool : kPaintingTools)
        for (const int px : {50, 1500}) {
            // Over the opaque field the Marker, Blur and Smudge paint the same with Preserve Opacity as without it (2026-11-23).
            if (tool != ToolType::Marker && tool != ToolType::Blur && tool != ToolType::Smudge) {
                Case keep = gridCase(tool, px, 4, QStringLiteral("field-3k.png"));
                keep.preserveOpacity = true;
                keep.name = QStringLiteral("settings/%1-%2-preserve-opacity").arg(toolName(tool), sizeName(px));
                cases.push_back(keep);
            }
            if (tool == ToolType::Eraser || tool == ToolType::Blur || tool == ToolType::Smudge || tool == ToolType::Clone)
                continue;  // they lay no colour of their own
            Case clear = gridCase(tool, px, 4, QStringLiteral("field-3k.png"));
            clear.colour.setAlpha(0);
            clear.name = QStringLiteral("settings/%1-%2-transparent").arg(toolName(tool), sizeName(px));
            cases.push_back(clear);
        }
    for (const ToolType tool : {ToolType::Pencil, ToolType::Brush, ToolType::Marker})
        for (const int px : {50, 600}) {
            Case shape = gridCase(tool, px, 4, QStringLiteral("field-3k.png"));
            shape.form = QStringLiteral("Splatter");
            shape.name = QStringLiteral("settings/%1-%2-splatter").arg(toolName(tool), sizeName(px));
            cases.push_back(shape);
            variant(shape, QStringLiteral("jitters50"),
                    [](BrushSettings& s) { s.shapeAngleJitter = s.shapeSizeJitter = s.shapeScattering = 50; });
        }
    // The Marker with a blot map, which it lays stamp by stamp rather than by events (the forms group has only the Brush's).
    for (const int px : {50, 600}) {
        Case blots = gridCase(ToolType::Marker, px, 4, QStringLiteral("field-3k.png"));
        blots.form = QStringLiteral("Blots");
        blots.name = QStringLiteral("settings/marker-%1-blots").arg(sizeName(px));
        cases.push_back(blots);
    }
    for (const int px : {50, 600}) {
        Case bristle = gridCase(ToolType::Brush, px, 4, QStringLiteral("field-3k.png"));
        bristle.form = QStringLiteral("Flat Bristle");
        bristle.name = QStringLiteral("forms/brush-%1-flat-bristle").arg(sizeName(px));
        variant(bristle, QStringLiteral("randomize20"), [](BrushSettings& s) { s.bristleRandomize = kBristleRandomizeMost; });
    }
    for (const Case::Target pen : {Case::Target::SelectionPen, Case::Target::SelectionEraser})
        for (const int px : {50, 600}) {
            const ToolType tool = pen == Case::Target::SelectionPen ? ToolType::SelectionPen : ToolType::SelectionEraser;
            Case c = gridCase(tool, px, 4, QString());
            c.target = pen;
            c.inSelection = pen == Case::Target::SelectionEraser;  // the Eraser starts from the middle half selected
            c.name = QStringLiteral("settings/%1-%2-s4").arg(toolName(tool), sizeName(px));
            cases.push_back(c);
        }
}

std::vector<Case> allCases() {
    std::vector<Case> cases;
    addGrid(cases);
    addForms(cases);
    addStabilizer(cases);
    addBackground(cases);
    addRecorded(cases);
    addBacklog(cases);
    addSettings(cases);
    return cases;
}

// The strokes' tools and sizes, from their timing log, as doodlebold_recorded_strokes_bench's kStrokes has them.
struct LoggedStroke {
    ToolType tool;
    double sizePx;
};
constexpr std::array<LoggedStroke, 8> kLoggedStrokes = {{{ToolType::Pencil, 104.2},
                                                         {ToolType::Brush, 176.7},
                                                         {ToolType::Brush, 176.7},
                                                         {ToolType::Brush, 721.9},
                                                         {ToolType::Brush, 721.9},
                                                         {ToolType::Brush, 721.9},
                                                         {ToolType::Marker, 161.8},
                                                         {ToolType::WaterColor, 554.5}}};

const ToolBox& recordedTools() {
    static const std::unique_ptr<ToolBox> tools = [] {
        auto box = std::make_unique<ToolBox>();
        QTemporaryDir copies;  // the store renames a file it refuses, so it reads a copy
        const QString copy = copies.filePath(QStringLiteral("tools.json"));
        QFile::copy(kRecordedTools, copy);
        ToolBoxStore store(box.get(), copy);
        if (!store.load().ok())
            std::fprintf(stderr, "%s: not a tool set this build reads\n", qPrintable(kRecordedTools));
        return box;
    }();
    return *tools;
}

// The settings of the slot of `tool` nearest `size` (tenths of a pixel), at that size.
BrushSettings recordedSettings(const LoggedStroke& logged) {
    const int size = static_cast<int>(std::lround(logged.sizePx * 10.0));
    BrushSettings best = defaultSettings(logged.tool);
    int nearest = -1;
    const ToolBox& tools = recordedTools();
    for (int g = 0; g < tools.groupCount(); ++g)
        for (const std::optional<ToolSlot>& slot : tools.group(g).cells)
            if (slot && slot->type() == logged.tool && (nearest < 0 || std::abs(slot->settings.size - size) < nearest)) {
                nearest = std::abs(slot->settings.size - size);
                best = slot->settings;
            }
    best.size = size;
    return best;
}

BrushSettings settingsOf(const Case& c) {
    BrushSettings s = defaultSettings(c.tool);
    if (c.tool == ToolType::Clone) {
        // The Clone Stamp's cases keep the tip they were written with, from before it started with the Pencil's.
        constexpr int kWrittenEdge = 0, kWrittenSharpness = 100;  // the 1st Shape at Sharpness 100 %
        s.edge = kWrittenEdge;
        s.sharpness = kWrittenSharpness;
    }
    if (c.sizePx > 0.0)
        s.size = static_cast<int>(std::lround(c.sizePx * 10.0));
    if (c.minSize >= 0)
        s.minSize = c.minSize;
    s.stabilizer = StabilizerLevel{c.stabilizer};
    s.texture = c.texture;
    s.form = c.form;
    if (c.adjust)
        c.adjust(s);
    return s.clamped();
}

const BrushResourceLibrary& builtInPictures() {
    static QTemporaryDir folder;  // none of the user's own pictures, only those that come with Doodlebold
    static const BrushResourceLibrary library(folder.path());
    return library;
}

// Read once: decoding a 3000 px picture takes longer than painting a small stroke.
const QImage& pictureOf(const QString& path) {
    static std::map<QString, QImage> read;
    auto found = read.find(path);
    if (found == read.end())
        found = read.emplace(path, QImage(path)).first;
    return found->second;
}

// The middle half of the canvas, selected: where the Selection Eraser's cases start.
SelectionMask middleSelected(QSize canvas) {
    SelectionMask mask(canvas);
    const QRect middle(canvas.width() / 4, canvas.height() / 4, canvas.width() / 2, canvas.height() / 2);
    const std::vector<std::uint16_t> row(std::size_t(middle.width()), std::uint16_t(SelectionMask::kOne));
    for (int y = middle.top(); y <= middle.bottom(); ++y)
        mask.writeRow(y, middle.left(), middle.width(), row.data());
    mask.squeeze();
    return mask;
}

// A case's layer as it starts: Preserve Opacity, a selection.
void prepare(Document& doc, const Case& c) {
    const LayerId id = doc.currentLayerId();
    if (c.preserveOpacity) {
        LayerProps props = doc.layer(id)->props;
        props.preserveOpacity = true;
        (void)doc.setLayerProps(id, props);
    }
    if (c.inSelection)
        (void)doc.setSelection(middleSelected(doc.size()));
}

std::unique_ptr<Document> freshCanvas(const Case& c) {
    auto doc = std::make_unique<Document>(c.canvas, Qt::white);
    TiledImage* layer = doc->paintTarget(doc->currentLayerId()).value();
    if (!c.layer.isEmpty())
        layer->writeImage(pictureOf(c.layer), {0, 0});
    prepare(*doc, c);
    return doc;
}

// The Clone Stamp copies from a quarter of the way across the canvas, half way down, as an Alt+click there.
void setCloneSource(StrokeController& stroke, const Document& doc) {
    stroke.cloneStamp().setSource(QPointF(doc.size().width() * 0.25, doc.size().height() * 0.5));
}

// Shows what the stroke has painted when there's news, as the canvas does between pen events.
void showFrame(StrokeController& stroke) {
    if (stroke.hasUnshownPaint())
        stroke.showPaint();
}

// As CanvasView hands a pen stroke over: the press begins it, each move adds to it, and the release only ends it.
bool paintStroke(Document& doc, const RecordedStroke& recorded, const BrushSettings& settings, const Case& c) {
    StrokeController stroke(&doc);
    StrokeWorker::setBacklogForTests(c.backlog ? StrokeWorker::Backlog::Always : StrokeWorker::Backlog::Never);
    if (c.inBackground)
        stroke.paintInBackground([] {}, [&] { showFrame(stroke); });
    if (settings.tool == ToolType::Clone)
        setCloneSource(stroke, doc);
    const std::vector<InputSample>& samples = recorded.samples;
    if (!stroke.begin(samples.front(), c.colour, settings, c.stabilizer).ok())
        return false;
    for (std::size_t i = 1; i + 1 < samples.size(); ++i) {
        stroke.add(samples[i]);
        if (c.inBackground)
            showFrame(stroke);
    }
    stroke.finish();
    return true;
}

// A selection pen's stroke as SelectionPenTool hands it over: the press begins it, each move adds to it, the release finishes it,
// with no stabilizer. In the background, what it has painted is taken in after each move, as the tool does on the painting thread's news.
bool paintSelectionStroke(Document& doc, const RecordedStroke& recorded, const BrushSettings& settings, const Case& c) {
    SelectionStrokeController stroke(&doc);
    if (c.inBackground)
        stroke.paintInBackground([] {});
    const SelectionBrush brush = c.target == Case::Target::SelectionPen ? SelectionBrush::Pen : SelectionBrush::Eraser;
    const std::vector<InputSample>& samples = recorded.samples;
    if (!stroke.begin(samples.front(), brush, settings))
        return false;
    for (std::size_t i = 1; i + 1 < samples.size(); ++i) {
        stroke.add(samples[i]);
        if (c.inBackground)
            stroke.takeChanges();
    }
    stroke.finish();
    return true;
}

const std::vector<RecordedStroke>& strokesOf(const QString& path) {
    static std::map<QString, std::vector<RecordedStroke>> read;
    auto found = read.find(path);
    if (found == read.end())
        found = read.emplace(path, readRecording(path)).first;
    return found->second;
}

// The CPU cycles and instructions of user code, in this thread and every thread it starts, which a busy machine barely changes.
// Both read 0 off Linux, or where the system refuses the counters (kernel.perf_event_paranoid above 2).
class WorkCounters {
public:
    std::uint64_t cycles() const { return read(m_cycles); }
    std::uint64_t instructions() const { return read(m_instructions); }

private:
#ifdef __linux__
    static int open(std::uint64_t counter) {
        perf_event_attr attr{};
        attr.size = sizeof attr;
        attr.type = PERF_TYPE_HARDWARE;
        attr.config = counter;
        attr.exclude_kernel = 1;
        attr.exclude_hv = 1;
        attr.inherit = 1;
        return static_cast<int>(syscall(SYS_perf_event_open, &attr, 0, -1, -1, 0));
    }
    static std::uint64_t read(int counter) {
        std::uint64_t value = 0;
        return counter >= 0 && ::read(counter, &value, sizeof value) == sizeof value ? value : 0;
    }
    int m_cycles = open(PERF_COUNT_HW_CPU_CYCLES);
    int m_instructions = open(PERF_COUNT_HW_INSTRUCTIONS);
#else
    static std::uint64_t read(int) { return 0; }
    int m_cycles = -1;
    int m_instructions = -1;
#endif
};

// What a case's strokes took, from the first pen event to the last finished, leaving out making the canvas.
struct PaintCost {
    double ms = 0.0;
    std::uint64_t cycles = 0;
    std::uint64_t instructions = 0;
};

double msSince(std::chrono::steady_clock::time_point start) {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
}

// The case painted on a fresh canvas. `cost`, if given, is what its strokes took, as `counters` count it.
std::unique_ptr<Document> render(const Case& c, PaintCost* cost = nullptr, const WorkCounters* counters = nullptr) {
    std::unique_ptr<Document> doc = freshCanvas(c);
    const std::uint64_t cyclesBefore = counters ? counters->cycles() : 0;
    const std::uint64_t instructionsBefore = counters ? counters->instructions() : 0;
    const auto start = std::chrono::steady_clock::now();
    const std::vector<RecordedStroke>& strokes = strokesOf(c.strokes);
    if (strokes.empty()) {
        std::fprintf(stderr, "%s: no pen strokes in %s\n", qPrintable(c.name), qPrintable(c.strokes));
        return nullptr;
    }
    for (std::size_t i = 0; i < strokes.size(); ++i) {
        const BrushSettings settings =
            c.recorded ? recordedSettings(kLoggedStrokes[std::min(i, kLoggedStrokes.size() - 1)]).clamped() : settingsOf(c);
        const bool selecting = c.target == Case::Target::SelectionPen || c.target == Case::Target::SelectionEraser;
        if (!(selecting ? paintSelectionStroke(*doc, strokes[i], settings, c) : paintStroke(*doc, strokes[i], settings, c))) {
            std::fprintf(stderr, "%s: the stroke was refused\n", qPrintable(c.name));
            return nullptr;
        }
    }
    if (cost) {
        cost->ms = msSince(start);
        cost->cycles = counters ? counters->cycles() - cyclesBefore : 0;
        cost->instructions = counters ? counters->instructions() - instructionsBefore : 0;
    }
    return doc;
}

const TiledImage& layerOf(const Document& doc) {
    return doc.layer(doc.currentLayerId())->pixels;
}

// A selection or a mask as a picture, each value in all four channels, as SelectionStrokeController's proxy holds it.
TiledImage pictureOfMask(const SelectionMask& mask) {
    TiledImage image(mask.size());
    for (const auto& [coord, tile] : mask.tiles()) {
        Tile* out = image.writableTile(coord.x, coord.y);
        for (std::size_t i = 0; i < std::size_t(SelectionTile::kValues); ++i)
            for (std::size_t channel = 0; channel < 4; ++channel)
                out->px[i * 4 + channel] = tile->v[i];
    }
    return image;
}

// What a case painted: the layer, or for the selection pens the selection, as a picture.
TiledImage resultOf(const Case& c, const Document& doc) {
    switch (c.target) {
    case Case::Target::SelectionPen:
    case Case::Target::SelectionEraser: return doc.selection() ? pictureOfMask(*doc.selection()) : TiledImage(doc.size());
    case Case::Target::Layer: break;
    }
    return layerOf(doc);
}

// For the pictures only: a selection's picture is white, which over white shows nothing, so it's drawn black, its coverage as alpha.
TiledImage shownInPictures(const Case& c, const TiledImage& image) {
    if (c.target != Case::Target::SelectionPen && c.target != Case::Target::SelectionEraser)
        return image;
    TiledImage black(image.size());
    for (const auto& [coord, tile] : image.tiles()) {
        Tile* out = black.writableTile(coord.x, coord.y);
        for (std::size_t i = 0; i < Tile::kValues; i += 4)
            out->px[i + 3] = tile->px[i + 3];
    }
    return black;
}

// What is kept of a render.

// The tiles of the layer that hold paint, on the canvas or off it, in order of place.
std::vector<TileCoord> paintedTiles(const TiledImage& image) {
    std::vector<TileCoord> coords;
    for (const auto& [coord, tile] : image.tiles())
        if (std::any_of(tile->px.begin(), tile->px.end(), [](Channel v) { return v != 0; }))
            coords.push_back(coord);
    std::ranges::sort(coords, [](TileCoord a, TileCoord b) { return a.y != b.y ? a.y < b.y : a.x < b.x; });
    return coords;
}

// A SHA-256 of every tile of the layer that holds paint, at the full 16 bits.
QByteArray checksum(const TiledImage& image) {
    QCryptographicHash hash(QCryptographicHash::Sha256);
    for (const TileCoord coord : paintedTiles(image)) {
        const std::array<std::int32_t, 2> place = {coord.x, coord.y};
        hash.addData(QByteArrayView(reinterpret_cast<const char*>(place.data()), sizeof place));
        const Tile& tile = *image.tiles().at(coord);
        hash.addData(QByteArrayView(reinterpret_cast<const char*>(tile.px.data()), sizeof tile.px));
    }
    return hash.result().toHex();
}

// The layer over white, at 1:1, as doodlebold_replay_case writes it. Layer pixels are premultiplied.
QImage flattenedOverWhite(const TiledImage& layer, QSize size) {
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

// The stored picture faded, with the pixels that differ from `now` in magenta.
QImage differences(const QImage& stored, const QImage& now) {
    QImage out = stored.convertToFormat(QImage::Format_RGBA8888);
    const QImage current = now.convertToFormat(QImage::Format_RGBA8888);
    for (int y = 0; y < out.height() && y < current.height(); ++y)
        for (int x = 0; x < out.width() && x < current.width(); ++x) {
            const QRgb was = out.pixel(x, y);
            const bool moved = was != current.pixel(x, y);
            const int grey = 192 + qGray(was) / 4;
            out.setPixel(x, y, moved ? qRgb(255, 0, 255) : qRgb(grey, grey, grey));
        }
    return out;
}

QString pngPath(const QString& dir, const Case& c) {
    return dir + QStringLiteral("/png/") + c.name + QStringLiteral(".png");
}

bool savePng(const QImage& image, const QString& path) {
    QDir().mkpath(QFileInfo(path).path());
    return image.convertToFormat(QImage::Format_RGB888).save(path, "PNG", 0);  // opaque, at the strongest compression
}

std::map<QString, QByteArray> readChecksums(const QString& dir, const QString& name) {
    std::map<QString, QByteArray> sums;
    QFile file(dir + QLatin1Char('/') + name);
    if (!file.open(QIODevice::ReadOnly))
        return sums;
    for (const QByteArray& line : file.readAll().split('\n'))
        if (const QList<QByteArray> parts = line.split(' '); parts.size() == 2)
            sums[QString::fromUtf8(parts[0])] = parts[1];
    return sums;
}

// A render against another build's, pixel by pixel (another platform's against Linux's).

// A render's tiles as --dump keeps them: each painted tile's place, then its 16-bit pixels.
using DumpedTiles = std::unordered_map<TileCoord, std::vector<Channel>, TileCoordHash>;

QString dumpPath(const QString& dir, const Case& c) {
    return dir + QLatin1Char('/') + c.name + QStringLiteral(".tiles");
}

bool dumpLayer(const TiledImage& image, const QString& path) {
    QDir().mkpath(QFileInfo(path).path());
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    for (const TileCoord coord : paintedTiles(image)) {
        const std::array<std::int32_t, 2> place = {coord.x, coord.y};
        file.write(reinterpret_cast<const char*>(place.data()), sizeof place);
        const Tile& tile = *image.tiles().at(coord);
        file.write(reinterpret_cast<const char*>(tile.px.data()), sizeof tile.px);
    }
    return true;
}

std::optional<DumpedTiles> readDump(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return std::nullopt;
    DumpedTiles tiles;
    std::array<std::int32_t, 2> place{};
    while (file.read(reinterpret_cast<char*>(place.data()), sizeof place) == sizeof place) {
        std::vector<Channel>& px = tiles[TileCoord{place[0], place[1]}];
        px.resize(Tile::kValues);
        if (file.read(reinterpret_cast<char*>(px.data()), Tile::kValues * sizeof(Channel)) != Tile::kValues * qint64(sizeof(Channel)))
            return std::nullopt;
    }
    return tiles;
}

// How far a pixel moved, in 16-bit levels, its worst channel: 0, 1, 2-3, 4-15, 16-63, 64-128 and 129-256 are all under one 8-bit
// level (257 of these), then 1-2, 2-4, 4-8 and 8 or more levels of 255.
constexpr std::array<int, 10> kBinTops = {0, 1, 3, 15, 63, 128, 256, 513, 1027, 2055};
constexpr int kDarkBelow = kChannelOne / 8;  // a channel under this counts as dark

int binOf(int moved) {
    for (std::size_t i = 0; i < kBinTops.size(); ++i)
        if (moved <= kBinTops[i])
            return static_cast<int>(i);
    return static_cast<int>(kBinTops.size());
}

struct Comparison {
    std::int64_t pixels = 0;
    std::int64_t differ16 = 0;  // pixels with a channel that's another 16-bit value
    std::int64_t differ8 = 0;  // pixels with a channel that's another 8-bit value
    int worst = 0;  // the most any channel moved, 16-bit levels
    QPoint worstAt;
    int worstDark = 0;  // the same over channels that are dark in the first render
    std::int64_t darkDiffer8 = 0;  // and how many of those are another 8-bit value
    std::array<std::int64_t, kBinTops.size() + 1> bins{};
};

void comparePixel(Comparison& c, const Channel* before, const Channel* now, QPoint place) {
    int worst = 0, dark = 0;
    bool eightBit = false, darkEightBit = false;
    for (int channel = 0; channel < 4; ++channel) {
        const int moved = std::abs(int(before[channel]) - int(now[channel]));
        const bool differs = toUnorm8(int(before[channel])) != toUnorm8(int(now[channel]));
        const bool isDark = channel < 3 && before[channel] < kDarkBelow;
        worst = std::max(worst, moved);
        eightBit = eightBit || differs;
        if (isDark) {
            dark = std::max(dark, moved);
            darkEightBit = darkEightBit || differs;
        }
    }
    ++c.pixels;
    ++c.bins[static_cast<std::size_t>(binOf(worst))];
    c.differ16 += worst != 0;
    c.differ8 += eightBit;
    c.darkDiffer8 += darkEightBit;
    c.worstDark = std::max(c.worstDark, dark);
    if (worst > c.worst) {
        c.worst = worst;
        c.worstAt = place;
    }
}

void compareTiles(Comparison& c, TileCoord coord, const Channel* before, const Channel* now) {
    static const std::array<Channel, Tile::kValues> kEmpty{};
    const Channel* a = before ? before : kEmpty.data();
    const Channel* b = now ? now : kEmpty.data();
    for (int i = 0; i < Tile::kSize * Tile::kSize; ++i)
        comparePixel(c, a + i * 4, b + i * 4, QPoint(coord.x * Tile::kSize + i % Tile::kSize, coord.y * Tile::kSize + i / Tile::kSize));
}

Comparison compareWith(const DumpedTiles& before, const TiledImage& now) {
    Comparison c;
    for (const auto& [coord, px] : before) {
        const auto tile = now.tiles().find(coord);
        compareTiles(c, coord, px.data(), tile == now.tiles().end() ? nullptr : tile->second->px.data());
    }
    for (const auto& [coord, tile] : now.tiles())
        if (!before.contains(coord))
            compareTiles(c, coord, nullptr, tile->px.data());
    return c;
}

constexpr int kPanel = 512;  // px: the side of each panel of a side-by-side picture

// Each pixel's largest move, as grey: `levelsToWhite` 8-bit levels or more are white.
QImage amplifiedDifference(const TiledImage& before, const TiledImage& now, const QRect& area, double levelsToWhite) {
    QImage out(area.size(), QImage::Format_RGB888);
    for (int y = 0; y < area.height(); ++y) {
        std::uint8_t* line = out.scanLine(y);
        for (int x = 0; x < area.width(); ++x) {
            const std::array<Channel, 4> a = before.pixel(area.left() + x, area.top() + y), b = now.pixel(area.left() + x, area.top() + y);
            int moved = 0;
            for (int c = 0; c < 4; ++c)
                moved = std::max(moved, std::abs(int(a[c]) - int(b[c])));
            const std::uint8_t grey = static_cast<std::uint8_t>(std::min(255.0, moved / 257.0 / levelsToWhite * 255.0));
            line[x * 3] = line[x * 3 + 1] = line[x * 3 + 2] = grey;
        }
    }
    return out;
}

void drawRow(QPainter& painter, int top, const std::array<QImage, 3>& panels) {
    for (int i = 0; i < 3; ++i)
        painter.drawImage(QRect(i * (kPanel + 8), top, kPanel, kPanel), panels[static_cast<std::size_t>(i)]);
}

// Before, after and their difference amplified, side by side: above, the whole canvas; below, 1:1 around the pixel that moved most.
bool writeSideBySide(const QString& dir, const Case& c, const TiledImage& before, const TiledImage& now, QSize canvas,
                     const Comparison& result) {
    const double levelsToWhite = std::max(result.worst / 257.0, 1.0);
    QRect crop(result.worstAt - QPoint(kPanel / 2, kPanel / 2), QSize(kPanel, kPanel));
    crop.moveLeft(std::clamp(crop.left(), 0, std::max(0, canvas.width() - kPanel)));
    crop.moveTop(std::clamp(crop.top(), 0, std::max(0, canvas.height() - kPanel)));
    crop = crop.intersected(QRect(QPoint(0, 0), canvas));
    const QImage beforeWhole = flattenedOverWhite(before, canvas), afterWhole = flattenedOverWhite(now, canvas);
    const QImage differenceWhole = amplifiedDifference(before, now, QRect(QPoint(0, 0), canvas), levelsToWhite);
    QImage sheet(3 * kPanel + 16, 2 * kPanel + 8, QImage::Format_RGB888);
    sheet.fill(Qt::black);
    QPainter painter(&sheet);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    drawRow(painter, 0, {beforeWhole, afterWhole, differenceWhole});
    drawRow(painter, kPanel + 8, {beforeWhole.copy(crop), afterWhole.copy(crop), amplifiedDifference(before, now, crop, levelsToWhite)});
    painter.end();
    QString name = c.name;
    name.replace(QLatin1Char('/'), QLatin1Char('-'));
    return savePng(sheet, dir + QLatin1Char('/') + name + QStringLiteral(".png"));
}

TiledImage layerFrom(const DumpedTiles& tiles, QSize size) {
    TiledImage image(size);
    for (const auto& [coord, px] : tiles) {
        auto tile = std::make_shared<Tile>();
        std::ranges::copy(px, tile->px.begin());
        image.setTile(coord.x, coord.y, std::move(tile));
    }
    return image;
}

// One line a case, the columns a script can read: pixels gone through, those that differ at 16 and at 8 bits, the worst move in 8-bit
// levels and where, the same for dark channels, and the histogram of moves (kBinTops).
void printComparison(const Case& c, const Comparison& r) {
    std::printf("%s pixels=%lld differ16=%lld differ8=%lld worst8=%.3f at=%d,%d worstdark8=%.3f darkdiffer8=%lld bins=", qPrintable(c.name),
                static_cast<long long>(r.pixels), static_cast<long long>(r.differ16), static_cast<long long>(r.differ8), r.worst / 257.0,
                r.worstAt.x(), r.worstAt.y(), r.worstDark / 257.0, static_cast<long long>(r.darkDiffer8));
    for (std::size_t i = 0; i < r.bins.size(); ++i)
        std::printf("%s%lld", i ? "," : "", static_cast<long long>(r.bins[i]));
    std::printf("\n");
    std::fflush(stdout);
}

struct Options {
    QString mode;
    QString dir;
    QString sums = QStringLiteral("checksums.txt");  // --checksums: the platform's file in <dir>
    bool within = false;  // --compare --within: fail past the platform tolerance
    QString only;
    QString exact;  // --case: one case by its whole name, so it paints alone in its process
    QString out;
    QString dump;  // --check also keeps each render's tiles here
    QString pictures;  // --compare writes side-by-side pictures here
    int cores = 0;
};

std::vector<Case> chosenCases(const Options& o) {
    std::vector<Case> chosen;
    for (const Case& c : allCases())
        if (o.exact.isEmpty() ? c.name.contains(o.only) : c.name == o.exact)
            chosen.push_back(c);
    return chosen;
}

int list(const Options& o) {
    for (const Case& c : chosenCases(o))
        std::printf("%s%s\n", qPrintable(c.name), c.png ? "  (png)" : "");
    return 0;
}

int time(const Options& o) {
    const WorkCounters counters;
    double totalMs = 0.0;
    for (const Case& c : chosenCases(o)) {
        PaintCost cost;
        if (!render(c, &cost, &counters))
            return 2;
        totalMs += cost.ms;
        std::printf("%9.1f ms  %s\n", cost.ms, qPrintable(c.name));
        std::printf("work: %llu %llu %s\n", static_cast<unsigned long long>(cost.cycles),
                    static_cast<unsigned long long>(cost.instructions), qPrintable(c.name));
        std::fflush(stdout);
    }
    std::printf("%9.1f ms  in all\n", totalMs);
    return 0;
}

int write(const Options& o) {
    // Only the reference writes the pictures, and always every case. Another platform's file is its checksums alone, and can be written
    // part by part (--only), each part after it passed --compare --within: the cases not painted keep what the file had for them.
    const bool reference = o.sums == QLatin1String("checksums.txt");
    if (reference && (!o.only.isEmpty() || !o.exact.isEmpty())) {
        std::fprintf(stderr, "the reference's checksums are written whole: --write takes no --only or --case\n");
        return 2;
    }
    std::map<QString, QByteArray> sums = reference ? std::map<QString, QByteArray>{} : readChecksums(o.dir, o.sums);
    if (reference)
        QDir(o.dir + QStringLiteral("/png")).removeRecursively();
    const auto start = std::chrono::steady_clock::now();
    const std::vector<Case> chosen = chosenCases(o);
    for (const Case& c : chosen) {
        const std::unique_ptr<Document> doc = render(c);
        if (!doc)
            return 2;
        sums[c.name] = checksum(resultOf(c, *doc));
        if (reference && c.png && !savePng(flattenedOverWhite(shownInPictures(c, resultOf(c, *doc)), doc->size()), pngPath(o.dir, c)))
            return 2;
    }
    QFile file(o.dir + QLatin1Char('/') + o.sums);
    QDir().mkpath(o.dir);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return 2;
    for (const Case& c : allCases())  // in the set's own order, as the reference's file is
        if (const auto kept = sums.find(c.name); kept != sums.end())
            file.write(c.name.toUtf8() + ' ' + kept->second + '\n');
    std::printf("wrote %zu cases in %.1f s\n", chosen.size(), msSince(start) / 1000.0);
    return 0;
}

void writeDifference(const Options& o, const Case& c, const Document& doc) {
    const QImage now = flattenedOverWhite(layerOf(doc), doc.size());
    savePng(now, pngPath(o.out, c));
    const QImage stored(pngPath(o.dir, c));
    if (!stored.isNull())
        savePng(differences(stored, now), o.out + QStringLiteral("/png/") + c.name + QStringLiteral("-diff.png"));
}

int check(const Options& o) {
    const std::map<QString, QByteArray> stored = readChecksums(o.dir, o.sums);
    const std::vector<Case> cases = chosenCases(o);
    if (stored.empty() || cases.empty()) {
        std::fprintf(stderr, "nothing to check in %s/%s\n", qPrintable(o.dir), qPrintable(o.sums));
        return 2;
    }
    const auto start = std::chrono::steady_clock::now();
    int differ = 0;
    for (const Case& c : cases) {
        PaintCost cost;
        const std::unique_ptr<Document> doc = render(c, &cost);
        const auto kept = stored.find(c.name);
        const bool same = doc && kept != stored.end() && checksum(resultOf(c, *doc)) == kept->second;
        // A case added to the set has its checksum said, to be added to checksums.txt as it is, without writing the rest again.
        const QByteArray unkept = kept == stored.end() && doc ? " (no checksum kept: " + checksum(resultOf(c, *doc)) + ')' : QByteArray();
        std::printf("%9.1f ms  %s  %s%s\n", cost.ms, same ? "same   " : "DIFFERS", qPrintable(c.name), unkept.constData());
        std::fflush(stdout);
        if (doc && !o.dump.isEmpty() && !dumpLayer(resultOf(c, *doc), dumpPath(o.dump, c)))
            return 2;
        if (same || kept == stored.end())
            continue;
        ++differ;
        if (doc && !o.out.isEmpty())
            writeDifference(o, c, *doc);
    }
    std::printf("%zu cases in %.1f s, %d differ\n", cases.size(), msSince(start) / 1000.0, differ);
    return differ == 0 ? 0 : 1;
}

// How far another platform's render may be from Linux's before its own checksums are written (--compare --within). Measured on MinGW-w64,
// whose maths library rounds expm1, log1p, pow and others differently from glibc: 97 of the 408 cases moved, at worst by 3 levels of
// 65535, in at most 0.093 % of a case's pixels. 4 levels is a 64th of one 8-bit step, never seen on a screen or in an 8-bit file; the
// share is that worst case's, rounded up. MSVC and macOS weren't measured when these were set.
constexpr int kPlatformMostLevels = 4;
constexpr double kPlatformMostShare = 0.001;

bool withinPlatformTolerance(const Comparison& r) {
    const double share = r.pixels > 0 ? static_cast<double>(r.differ16) / static_cast<double>(r.pixels) : 0.0;
    return r.worst <= kPlatformMostLevels && share <= kPlatformMostShare;
}

int compare(const Options& o) {
    int beyond = 0;
    for (const Case& c : chosenCases(o)) {
        const std::optional<DumpedTiles> before = readDump(dumpPath(o.dir, c));
        const std::unique_ptr<Document> doc = before ? render(c) : nullptr;
        if (!doc) {
            std::fprintf(stderr, "%s: no render to compare with\n", qPrintable(c.name));
            return 2;
        }
        const TiledImage now = resultOf(c, *doc);
        const TiledImage was = layerFrom(*before, doc->size());
        const Comparison result = compareWith(*before, now);
        printComparison(c, result);
        if (o.within && !withinPlatformTolerance(result)) {
            std::printf("%s: past the platform tolerance\n", qPrintable(c.name));
            ++beyond;
        }
        if (!o.pictures.isEmpty()) {
            QDir().mkpath(o.pictures);
            if (!writeSideBySide(o.pictures, c, shownInPictures(c, was), shownInPictures(c, now), doc->size(), result))
                return 2;
        }
    }
    return beyond == 0 ? 0 : 1;
}

std::optional<Options> optionsFrom(const QStringList& args) {
    Options o;
    for (int i = 1; i < args.size(); ++i) {
        const QString& a = args[i];
        const bool hasValue = i + 1 < args.size();
        if (a == QLatin1String("--list") || a == QLatin1String("--time"))
            o.mode = a.mid(2);
        else if ((a == QLatin1String("--check") || a == QLatin1String("--write")) && hasValue)
            o.mode = a.mid(2), o.dir = args[++i];
        else if (a == QLatin1String("--compare") && hasValue)
            o.mode = QStringLiteral("compare"), o.dir = args[++i];
        else if (a == QLatin1String("--dump") && hasValue)
            o.dump = args[++i];
        else if (a == QLatin1String("--pictures") && hasValue)
            o.pictures = args[++i];
        else if (a == QLatin1String("--only") && hasValue)
            o.only = args[++i];
        else if (a == QLatin1String("--case") && hasValue)
            o.exact = args[++i];
        else if (a == QLatin1String("--out") && hasValue)
            o.out = args[++i];
        else if (a == QLatin1String("--checksums") && hasValue)
            o.sums = args[++i];
        else if (a == QLatin1String("--within"))
            o.within = true;
        else if (a == QLatin1String("--cores") && hasValue)
            o.cores = args[++i].toInt();
        else
            return std::nullopt;
    }
    return o.mode.isEmpty() ? std::nullopt : std::optional(o);
}

}

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    const std::optional<Options> o = optionsFrom(QCoreApplication::arguments());
    if (!o) {
        std::fprintf(
            stderr,
            "usage: %s --check <dir> [--checksums <file>] [--only <text>] [--out <dir>] [--cores <n>] | --write <dir> [--cores <n>] | "
            "--list [--only <text>] | --time [--only <text>] [--cores <n>] | "
            "--check <dir> --dump <dir2> | --compare <dir2> [--within] [--pictures <dir3>] [--only <text>] [--cores <n>]\n",
            argv[0]);
        return 2;
    }
    if (o->cores > 0)
        cpu_cores::setLimit(o->cores);
    // Every case paints in full: what's in memory from the cases before it must never change a result.
    stroke_memory::usableOverride() = -1;
    RebuiltStroke::setResources(&builtInPictures());
    if (o->mode == QLatin1String("list"))
        return list(*o);
    if (o->mode == QLatin1String("time"))
        return time(*o);
    if (o->mode == QLatin1String("write"))
        return write(*o);
    if (o->mode == QLatin1String("compare"))
        return compare(*o);
    return check(*o);
}
