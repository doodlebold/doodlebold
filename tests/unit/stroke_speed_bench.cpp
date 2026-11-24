// Times a whole stroke through the engine, laying paint and merging it, and says whether it keeps up with the pen. It stops
// short of the screen, which the app's own timer covers: DOODLEBOLD_TIMING_LOG. Exits with 1 if a tip it holds to the budget falls
// behind, which is how the tests use it, with --small-tips. Water Color and the Marker at 200 px, which mix and melt what's under
// every dab, are held to keeping up with the pen, since Water Color was slow there.
//
//   doodlebold_stroke_speed_bench <input log> [--small-tips]

#include "core/brush/BrushSettings.h"
#include "core/stroke/EventBacklog.h"
#include "core/stroke/RebuiltStroke.h"
#include "core/stroke/StrokeRecording.h"

#include <QString>

#include <algorithm>
#include <utility>
#include <chrono>
#include <cstdio>
#include <vector>

using namespace doodlebold;

namespace {

constexpr double kBudgetMs = 3.0;     // for raster and composite per event, from the design report
constexpr double kHeldToBudget = 25.0;  // tips up to this radius have to meet the budget at every rate, px
constexpr double kLargeMixingRadius = 100.0;  // px: Size 200, where Water Color was slow

struct Run {
    std::vector<double> takesMs;
    EventBacklog backlog;
    bool fellBehind = false;
};

double percentile(std::vector<double> values, double share) {
    std::ranges::sort(values);
    return values[static_cast<std::size_t>(share * static_cast<double>(values.size() - 1))];
}

BrushSettings mixingOf(ToolType tool, double radius) {
    BrushSettings settings = defaultSettings(tool);
    settings.size = static_cast<int>(radius * 20.0);
    return settings;
}

BrushSettings brushOf(double radius, int sharpness) {
    BrushSettings settings = defaultSettings(ToolType::Brush);
    settings.size = static_cast<int>(radius * 20.0);
    settings.sharpness = sharpness;
    settings.minSize = 0;
    return settings;
}

void paintStroke(const RecordedStroke& stroke, const BrushSettings& settings, double hz, Run& run) {
    using Clock = std::chrono::steady_clock;
    TiledImage canvas(QSize(6000, 6000));
    canvas.fill(Qt::white);  // so the mixing tools have paint to pick up and melt
    RebuiltStroke painter;
    painter.begin(&canvas, Qt::black, settings, stroke.samples.front());
    const auto oneEventUs = static_cast<std::int64_t>(1e6 / hz);
    EventBacklog backlog;
    for (std::size_t i = 1; i < stroke.samples.size(); ++i) {
        const auto start = Clock::now();
        painter.add(stroke.samples[i]);
        const std::chrono::duration<double, std::milli> took = Clock::now() - start;
        run.takesMs.push_back(took.count());
        backlog.eventHandled(static_cast<std::int64_t>(i) * oneEventUs, static_cast<std::int64_t>(took.count() * 1000.0));
        run.fellBehind = run.fellBehind || backlog.fellBehind(oneEventUs);
    }
    painter.end();
}

Run timed(const std::vector<RecordedStroke>& strokes, const BrushSettings& settings, double hz) {
    Run run;
    for (const RecordedStroke& stroke : strokes)
        paintStroke(resampledTo(stroke, hz), settings, hz, run);
    return run;
}

bool printRun(const char* tool, double radius, int sharpness, double hz, const Run& run) {
    const double median = percentile(run.takesMs, 0.5);
    const double p99 = percentile(run.takesMs, 0.99);
    const double worst = percentile(run.takesMs, 1.0);
    const bool held = radius <= kHeldToBudget;
    const bool failed = (held && p99 > kBudgetMs) || ((held || radius == kLargeMixingRadius) && run.fellBehind);
    std::printf("%-11s %6.0f %6d %5.0f %9.3f %9.3f %9.3f %7s %s\n", tool, radius, sharpness, hz, median, p99, worst,
                run.fellBehind ? "behind" : "keeps up", failed ? "FAILED" : "");
    return !failed;
}

}

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: %s <input log> [--small-tips]\n", argv[0]);
        return 2;
    }
    const bool smallTipsOnly = argc > 2 && QString::fromLocal8Bit(argv[2]) == QStringLiteral("--small-tips");
    const std::vector<RecordedStroke> strokes = readRecording(QString::fromLocal8Bit(argv[1]));
    std::printf("%-11s %6s %6s %5s %9s %9s %9s\n", "tool", "radius", "sharp", "hz", "median ms", "99% ms", "worst ms");
    bool passed = true;
    for (const double radius : {5.0, 25.0, 100.0, 350.0}) {
        if (smallTipsOnly && radius > kHeldToBudget)
            continue;
        for (const int sharpness : {100, 0}) {
            for (const double hz : {60.0, 133.0, 300.0})
                passed = printRun("Brush", radius, sharpness, hz, timed(strokes, brushOf(radius, sharpness), hz)) && passed;
        }
    }
    for (const auto& [tool, name] : {std::pair{ToolType::WaterColor, "Water Color"}, std::pair{ToolType::Marker, "Marker"}}) {
        const BrushSettings settings = mixingOf(tool, kLargeMixingRadius);
        for (const double hz : {60.0, 133.0, 300.0})
            passed = printRun(name, kLargeMixingRadius, settings.sharpness, hz, timed(strokes, settings, hz)) && passed;
    }
    return passed ? 0 : 1;
}
