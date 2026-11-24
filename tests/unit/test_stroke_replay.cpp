#include "core/stroke/PaintTimer.h"
#include "core/stroke/StrokeBuffer.h"
#include "core/stroke/StrokeMerge.h"
#include "core/stroke/StrokeRecording.h"
#include "core/stroke/StrokeReplay.h"

#include <gtest/gtest.h>

#include <cmath>

using namespace doodlebold;

namespace {

// A stand-in tip for these tests: a disc of density at each sample, as wide as the pressure says.
class DiscEvaluator : public StrokeEvaluator {
public:
    explicit DiscEvaluator(float density) : m_density(density) {}
    void begin(const InputSample& first, StrokeBuffer& buffer) override { lay(first, buffer); }
    void add(const InputSample& sample, StrokeBuffer& buffer) override { lay(sample, buffer); }
    void end(StrokeBuffer&) override {}

private:
    void lay(const InputSample& sample, StrokeBuffer& buffer) const {
        const int radius = 2 + static_cast<int>(sample.pressure * 12.0);
        const int cx = static_cast<int>(std::floor(sample.pos.x()));
        const int cy = static_cast<int>(std::floor(sample.pos.y()));
        for (int y = -radius; y <= radius; ++y) {
            for (int x = -radius; x <= radius; ++x) {
                if (x * x + y * y <= radius * radius)
                    buffer.add(cx + x, cy + y, m_density);
            }
        }
    }
    float m_density;
};

const StrokePaint kRed{{0.8f, 0.1f, 0.2f}, 1.0f};

std::vector<RecordedStroke> penSample() {
    return readRecording(QStringLiteral(DOODLEBOLD_FIXTURE_DIR "/pen-sample.jsonl"));
}

TiledImage blankCanvas() {
    return TiledImage(QSize(2600, 1800));
}

std::vector<RecordedStroke> transformed(const std::vector<RecordedStroke>& strokes, RecordedStroke (*change)(const RecordedStroke&)) {
    std::vector<RecordedStroke> result;
    result.reserve(strokes.size());
    for (const RecordedStroke& stroke : strokes)
        result.push_back(change(stroke));
    return result;
}

std::vector<RecordedStroke> atRate(const std::vector<RecordedStroke>& strokes, double hz) {
    std::vector<RecordedStroke> result;
    result.reserve(strokes.size());
    for (const RecordedStroke& stroke : strokes)
        result.push_back(resampledTo(stroke, hz));
    return result;
}

int overfullPixels(const TiledImage& image) {
    int count = 0;
    for (const auto& [coord, tile] : image.tiles()) {
        for (int i = 0; i < Tile::kValues; i += 4) {
            const Channel* p = tile->px.data() + i;
            if (p[0] > p[3] || p[1] > p[3] || p[2] > p[3])
                ++count;
        }
    }
    return count;
}

int colourWithoutAlpha(const TiledImage& image) {
    int count = 0;
    for (const auto& [coord, tile] : image.tiles()) {
        for (int i = 0; i < Tile::kValues; i += 4) {
            const Channel* p = tile->px.data() + i;
            if (p[3] == 0 && (p[0] != 0 || p[1] != 0 || p[2] != 0))
                ++count;
        }
    }
    return count;
}

StrokeBuffer rampBuffer() {
    StrokeBuffer buffer;
    for (int x = 0; x < 200; ++x)
        buffer.add(x, 3, 0.002f * static_cast<float>(x * x) / 40.0f);
    return buffer;
}

}

TEST(StrokeBuffer, DensityBuildsUpByAdding) {
    StrokeBuffer buffer;
    buffer.add(5, 6, 0.25f);
    buffer.add(5, 6, 0.5f);
    EXPECT_FLOAT_EQ(buffer.density(5, 6), 0.75f);
    EXPECT_FLOAT_EQ(buffer.density(6, 6), 0.0f);
}

TEST(StrokeBuffer, DensityIsNotClamped) {
    StrokeBuffer buffer;
    for (int i = 0; i < 100; ++i)
        buffer.add(0, 0, 1.0f);
    EXPECT_FLOAT_EQ(buffer.density(0, 0), 100.0f);
}

TEST(StrokeBuffer, WorksAcrossTilesAndOffTheCanvas) {
    StrokeBuffer buffer;
    buffer.add(-1, -1, 1.0f);
    buffer.add(0, 0, 2.0f);
    buffer.add(64, 64, 3.0f);
    EXPECT_FLOAT_EQ(buffer.density(-1, -1), 1.0f);
    EXPECT_FLOAT_EQ(buffer.density(0, 0), 2.0f);
    EXPECT_FLOAT_EQ(buffer.density(64, 64), 3.0f);
    EXPECT_EQ(buffer.tiles().size(), 3u);
}

TEST(StrokeBuffer, NoPaintMakesNoTile) {
    StrokeBuffer buffer;
    buffer.add(5, 6, 0.0f);
    EXPECT_TRUE(buffer.tiles().empty());
}

TEST(StrokeMerge, CoverageFollowsTheExponential) {
    EXPECT_FLOAT_EQ(coverageOf(0.0f, 1.0f, 1.0f), 0.0f);
    EXPECT_NEAR(coverageOf(1.0f, 1.0f, 1.0f), 1.0 - std::exp(-1.0), 1e-6);
    EXPECT_NEAR(coverageOf(100.0f, 1.0f, 1.0f), 1.0, 1e-6);
}

TEST(StrokeMerge, OpacityCapsCoverage) {
    EXPECT_NEAR(coverageOf(100.0f, 1.0f, 0.4f), 0.4, 1e-6);
    EXPECT_NEAR(coverageOf(1.0f, 1.0f, 0.5f), 0.5 * (1.0 - std::exp(-1.0)), 1e-6);
}

TEST(StrokeMerge, VeryLowDensityStillCounts) {
    EXPECT_GT(coverageOf(1e-6f, 1.0f, 1.0f), 0.0f);
    EXPECT_NEAR(coverageOf(1e-6f, 1.0f, 1.0f), 1e-6, 1e-9);
}

TEST(StrokeMerge, SplittingDensityComposesExactly) {
    const double whole = coverageOf(1.5f, 1.0f, 1.0f);
    const double a = coverageOf(0.5f, 1.0f, 1.0f);
    const double b = coverageOf(1.0f, 1.0f, 1.0f);
    EXPECT_NEAR(1.0 - (1.0 - a) * (1.0 - b), whole, 1e-6);
}

TEST(StrokeMerge, ShapeLimitsCoverageHoweverDense) {
    EXPECT_NEAR(coverageOf(0.25f * 8.0f, 0.25f, 1.0f), 0.25 * (1.0 - std::exp(-8.0)), 1e-6);
    EXPECT_NEAR(coverageOf(0.1f * 8.0f, 0.1f, 1.0f), 0.1 * (1.0 - std::exp(-8.0)), 1e-6);
}

TEST(StrokeMerge, PaintWithNoShapeLeavesThePixelAlone) {
    StrokeBuffer buffer;
    buffer.tile(TileCoord{0, 0}).value[0] = 2.0f;
    StrokePaint paint;
    TiledImage image(QSize(64, 64));
    image.fill(QColor(200, 150, 100));
    const auto before = image.pixel(0, 0);
    mergeStroke(buffer, paint, image);
    EXPECT_EQ(image.pixel(0, 0), before);
}

TEST(StrokeMerge, FullDensityShowsThePaintColour) {
    StrokeBuffer buffer;
    buffer.add(2, 2, 40.0f);
    TiledImage image(QSize(64, 64));
    mergeStroke(buffer, kRed, image);
    const auto pixel = image.pixel(2, 2);
    EXPECT_EQ(pixel[3], kChannelOne);
    EXPECT_NEAR(pixel[0], 0.8 * kChannelOne, 2.0);
    EXPECT_NEAR(pixel[1], 0.1 * kChannelOne, 2.0);
}

TEST(StrokeMerge, ColourNeverExceedsAlpha) {
    TiledImage image(QSize(256, 64));
    image.fill(QRect(0, 0, 100, 64), QColor(10, 200, 30, 128));
    const StrokePaint bright{{1.0f, 1.0f, 1.0f}, 1.0f};
    mergeStroke(rampBuffer(), bright, image);
    EXPECT_EQ(overfullPixels(image), 0);
}

TEST(StrokeMerge, NoAlphaMeansNoColour) {
    TiledImage image(QSize(256, 64));
    mergeStroke(rampBuffer(), kRed, image);
    EXPECT_EQ(colourWithoutAlpha(image), 0);
}

TEST(StrokeMerge, ColourStaysTheSameAsCoverageFades) {
    const StrokeBuffer buffer = rampBuffer();
    TiledImage overBlack(QSize(256, 64));
    overBlack.fill(Qt::black);
    TiledImage overWhite(QSize(256, 64));
    overWhite.fill(Qt::white);
    mergeStroke(buffer, kRed, overBlack);
    mergeStroke(buffer, kRed, overWhite);
    for (int x = 20; x < 200; ++x) {
        const double alpha = 1.0 - static_cast<double>(overWhite.pixel(x, 3)[1] - overBlack.pixel(x, 3)[1]) / kChannelOne;
        if (alpha < 0.01)
            continue;
        EXPECT_NEAR(overBlack.pixel(x, 3)[0] / (alpha * kChannelOne), 0.8, 0.01) << "at x " << x;
    }
}

TEST(StrokeMerge, AZeroStrengthStrokeChangesNothing) {
    TiledImage image(QSize(128, 128));
    image.fill(QColor(10, 200, 30, 128));
    const TilePtr before = image.tileShared(0, 0);
    StrokeBuffer buffer;
    buffer.add(3, 3, 0.0f);
    mergeStroke(buffer, kRed, image);
    EXPECT_EQ(image.tileShared(0, 0), before);
}

TEST(StrokeMerge, OnlyTouchedTilesChange) {
    TiledImage image(QSize(128, 128));
    image.fill(Qt::white);
    const TilePtr untouched = image.tileShared(1, 1);
    StrokeBuffer buffer;
    buffer.add(3, 3, 1.0f);
    mergeStroke(buffer, kRed, image);
    EXPECT_EQ(image.tileShared(1, 1), untouched);
}

TEST(StrokeMerge, LiveFrameLeavesTheCanvasAlone) {
    TiledImage image(QSize(64, 64));
    StrokeBuffer buffer;
    buffer.add(3, 3, 1.0f);
    const TiledImage frame = liveFrame(image, buffer, kRed);
    EXPECT_EQ(image.pixel(3, 3)[3], 0);
    EXPECT_GT(frame.pixel(3, 3)[3], 0);
}

TEST(StrokeRecording, ReadsStrokesFromPressToRelease) {
    const auto strokes = parseRecording(R"({"e":"press","t":10,"x":1,"y":2,"p":0,"kind":1}
{"e":"move","t":18,"x":3,"y":4,"p":0.5,"kind":1}
{"e":"release","t":26,"x":5,"y":6,"p":0,"kind":1}
)");
    ASSERT_EQ(strokes.size(), 1u);
    ASSERT_EQ(strokes[0].samples.size(), 3u);
    EXPECT_EQ(strokes[0].samples[1].timestampMs, 18u);
    EXPECT_DOUBLE_EQ(strokes[0].samples[1].pos.y(), 4.0);
    EXPECT_DOUBLE_EQ(strokes[0].samples[1].pressure, 0.5);
}

TEST(StrokeRecording, ReadsTiltWhenTheLogHasIt) {
    const auto strokes = parseRecording(R"({"e":"press","t":1,"x":1,"y":2,"p":0,"tx":10,"ty":-5,"r":30,"kind":1}
{"e":"release","t":2,"x":1,"y":2,"p":0,"kind":1}
)");
    ASSERT_EQ(strokes.size(), 1u);
    EXPECT_DOUBLE_EQ(strokes[0].samples[0].tiltX, 10.0);
    EXPECT_DOUBLE_EQ(strokes[0].samples[0].tiltY, -5.0);
    EXPECT_DOUBLE_EQ(strokes[0].samples[0].rotation, 30.0);
    EXPECT_DOUBLE_EQ(strokes[0].samples[1].tiltX, 0.0);
}

TEST(StrokeRecording, LeavesOutHoverMouseAndUnfinishedStrokes) {
    const auto strokes = parseRecording(R"({"e":"move","t":1,"x":1,"y":2,"p":0,"kind":1}
{"e":"press","t":2,"x":1,"y":2,"p":1,"kind":0}
{"e":"release","t":3,"x":1,"y":2,"p":0,"kind":0}
{"e":"press","t":4,"x":1,"y":2,"p":0,"kind":1}
{"e":"move","t":5,"x":1,"y":2,"p":0.5,"kind":1}
)");
    EXPECT_TRUE(strokes.empty());
}

TEST(StrokeRecording, ResamplingKeepsBothEnds) {
    const RecordedStroke stroke = penSample().front();
    const RecordedStroke result = resampledTo(stroke, 60.0);
    EXPECT_EQ(result.samples.front().timestampMs, stroke.samples.front().timestampMs);
    EXPECT_EQ(result.samples.back().timestampMs, stroke.samples.back().timestampMs);
    EXPECT_EQ(result.samples.back().pos, stroke.samples.back().pos);
}

TEST(StrokeRecording, ResamplingGivesTheAskedForRate) {
    RecordedStroke stroke;
    InputSample a;
    a.timestampMs = 0;
    InputSample b;
    b.timestampMs = 1000;
    b.pos = QPointF(100, 0);
    stroke.samples = {a, b};
    const RecordedStroke result = resampledTo(stroke, 100.0);
    EXPECT_EQ(result.samples.size(), 101u);
    EXPECT_NEAR(result.samples[50].pos.x(), 50.0, 1e-6);
}

TEST(StrokeRecording, ResamplingCopesWithSamplesAtTheSameTime) {
    RecordedStroke stroke;
    InputSample a;
    a.timestampMs = 5;
    stroke.samples = {a, a, a};
    EXPECT_EQ(resampledTo(stroke, 200.0).samples.size(), 2u);
}

TEST(StrokeRecording, CanDropTheFinalZero) {
    RecordedStroke stroke;
    InputSample held;
    held.pressure = 0.4;
    InputSample lifted;
    lifted.pressure = 0.0;
    stroke.samples = {held, held, lifted};
    EXPECT_EQ(withoutFinalZero(stroke).samples.size(), 2u);
}

TEST(StrokeRecording, TheFixtureHasRealStrokes) {
    EXPECT_GE(penSample().size(), 5u);
}

TEST(StrokeReplay, TheSameStrokesGiveTheSamePixels) {
    DiscEvaluator evaluator(0.3f);
    const auto strokes = penSample();
    const auto first = replayStrokes(strokes, evaluator, kRed, blankCanvas());
    const auto second = replayStrokes(strokes, evaluator, kRed, blankCanvas());
    EXPECT_FALSE(first.merged.isEmpty());
    EXPECT_EQ(pixelHash(first.merged), pixelHash(second.merged));
}

TEST(StrokeReplay, MergedEqualsTheLastLiveFrame) {
    DiscEvaluator evaluator(0.3f);
    const auto result = replayStrokes(penSample(), evaluator, kRed, blankCanvas());
    EXPECT_EQ(pixelHash(result.merged), pixelHash(result.lastLive));
}

TEST(StrokeReplay, RunsAtOtherEventRates) {
    DiscEvaluator evaluator(0.3f);
    for (const double hz : {60.0, 100.0, 200.0, 300.0}) {
        const auto strokes = atRate(penSample(), hz);
        const auto first = replayStrokes(strokes, evaluator, kRed, blankCanvas());
        const auto second = replayStrokes(strokes, evaluator, kRed, blankCanvas());
        EXPECT_EQ(pixelHash(first.merged), pixelHash(second.merged)) << hz << " Hz";
        EXPECT_EQ(pixelHash(first.merged), pixelHash(first.lastLive)) << hz << " Hz";
        EXPECT_EQ(overfullPixels(first.merged), 0) << hz << " Hz";
    }
}

TEST(StrokeReplay, RunsWithoutAFinalZero) {
    DiscEvaluator evaluator(0.3f);
    const auto strokes = transformed(penSample(), withoutFinalZero);
    const auto result = replayStrokes(strokes, evaluator, kRed, blankCanvas());
    EXPECT_FALSE(result.merged.isEmpty());
    EXPECT_EQ(pixelHash(result.merged), pixelHash(result.lastLive));
}

TEST(StrokeReplay, DifferentPaintGivesDifferentPixels) {
    DiscEvaluator evaluator(0.3f);
    const StrokePaint blue{{0.1f, 0.2f, 0.8f}, 1.0f};
    const auto strokes = penSample();
    EXPECT_NE(pixelHash(replayStrokes(strokes, evaluator, kRed, blankCanvas()).merged),
              pixelHash(replayStrokes(strokes, evaluator, blue, blankCanvas()).merged));
}

TEST(StrokeReplay, AnEmptyTileHashesLikeNoTile) {
    TiledImage image(QSize(128, 128));
    const std::uint64_t empty = pixelHash(image);
    image.writableTile(0, 0);
    EXPECT_EQ(pixelHash(image), empty);
}

TEST(PaintTimer, AFrameCountsFromTheOldestEventItShows) {
    PaintTimer timer;
    timer.eventReceived(1000);
    timer.pixelsChanged();
    timer.eventReceived(5000);
    timer.pixelsChanged();
    timer.frameFinished(6000);
    const auto summary = timer.takeSummary();
    EXPECT_EQ(summary.events, 2u);
    EXPECT_EQ(summary.frames, 1u);
    EXPECT_EQ(summary.worstUs, 5000);
}

TEST(PaintTimer, AnEventThatChangedNothingDoesNotWait) {
    PaintTimer timer;
    timer.eventReceived(0);
    timer.eventReceived(5000);
    timer.pixelsChanged();
    timer.frameFinished(6000);
    EXPECT_EQ(timer.takeSummary().worstUs, 1000) << "the pen held still until 5 ms";
}

TEST(PaintTimer, AnEventWithNoChangeAndNoFrameCountsOnlyAsAnEvent) {
    PaintTimer timer;
    timer.eventReceived(0);
    timer.frameFinished(1000);
    const auto summary = timer.takeSummary();
    EXPECT_EQ(summary.events, 1u);
    EXPECT_EQ(summary.frames, 0u);
}

TEST(PaintTimer, ItTimesDrawingAFrameOnItsOwn) {
    PaintTimer timer;
    timer.frameStarted(100);
    timer.frameFinished(400);
    timer.frameStarted(1000);
    timer.frameFinished(1500);
    const auto summary = timer.takeSummary();
    EXPECT_EQ(summary.drawWorstUs, 500);
    EXPECT_EQ(summary.drawMedianUs, 300);
}

TEST(PaintTimer, ItTimesHandlingEventsAndSwappingFrames) {
    PaintTimer timer;
    timer.eventHandled(300);
    timer.eventHandled(700);
    timer.frameStarted(0);
    timer.frameFinished(100);
    timer.frameSwapped(1100);
    const auto summary = timer.takeSummary();
    EXPECT_EQ(summary.handleWorstUs, 700);
    EXPECT_EQ(summary.swapWorstUs, 1000);
}

TEST(PaintTimer, ABacklogIsHowLateAnEventIsAgainstTheStrokesQuickest) {
    PaintTimer timer;
    timer.eventStamped(1000, 5000000);  // the two clocks start apart, which doesn't matter
    timer.eventStamped(1008, 5008000);
    timer.eventStamped(1016, 5046000);
    EXPECT_EQ(timer.takeSummary().backlogWorstUs, 30000);
    EXPECT_EQ(timer.takeSummary().backlogWorstUs, 0) << "each stroke starts afresh";
}

TEST(PaintTimer, ItTracksHowOldTheInkIs) {
    PaintTimer timer;
    timer.inkShown(2000);
    timer.inkShown(9000);
    const auto summary = timer.takeSummary();
    EXPECT_EQ(summary.inkWorstUs, 9000);
    EXPECT_EQ(summary.inkMedianUs, 2000);
}

TEST(PaintTimer, ItFindsTheLongestStallOfTheMainThread) {
    PaintTimer timer;
    timer.mainThreadTick(1000);
    timer.mainThreadTick(6000);
    timer.mainThreadTick(90000);
    timer.mainThreadTick(95000);
    EXPECT_EQ(timer.takeSummary().mainStallWorstUs, 84000);
}

TEST(PaintTimer, ItKeepsTheLongestStallAfterALiftForTheNextStroke) {
    PaintTimer timer;
    timer.mainThreadTick(0);
    timer.mainThreadTick(50000);  // before the lift: not after it
    timer.lifted(50000);
    timer.mainThreadTick(55000);
    timer.mainThreadTick(75000);
    EXPECT_EQ(timer.takeSummary().lastLiftStallWorstUs, 0) << "the stroke that lifted doesn't hold its own";
    timer.mainThreadTick(80000);
    timer.mainThreadTick(1'040'000);  // still within the second
    timer.mainThreadTick(2'000'000);  // after it
    timer.eventReceived(2'100'000);   // the next stroke
    timer.mainThreadTick(2'110'000);
    timer.lifted(2'500'000);
    EXPECT_EQ(timer.takeSummary().lastLiftStallWorstUs, 960000);
}

TEST(PaintTimer, AFrameWithNoNewEventDoesNotCount) {
    PaintTimer timer;
    timer.frameFinished(100);
    EXPECT_EQ(timer.takeSummary().frames, 0u);
}

TEST(PaintTimer, ReportsPercentiles) {
    PaintTimer timer;
    std::int64_t now = 0;
    for (int wait = 1; wait <= 1000; ++wait) {
        timer.eventReceived(now);
        timer.pixelsChanged();
        now += wait;
        timer.frameFinished(now);
    }
    const auto summary = timer.takeSummary();
    EXPECT_EQ(summary.medianUs, 500);
    EXPECT_EQ(summary.p99Us, 990);
    EXPECT_EQ(summary.worstUs, 1000);
}

TEST(PaintTimer, TakingTheSummaryStartsAfresh) {
    PaintTimer timer;
    timer.eventReceived(0);
    timer.pixelsChanged();
    timer.frameFinished(10);
    timer.takeSummary();
    const auto summary = timer.takeSummary();
    EXPECT_EQ(summary.events, 0u);
    EXPECT_EQ(summary.frames, 0u);
}
