#include "core/brush/BrushSettings.h"
#include "core/document/Document.h"
#include "core/paint/StrokeController.h"
#include "core/paint/StrokeSmoother.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <memory>
#include <vector>

using namespace doodlebold;

namespace {

InputSample at(double x, double y, double pressure, std::uint64_t ms) {
    InputSample s;
    s.pos = QPointF(x, y);
    s.pressure = pressure;
    s.timestampMs = ms;
    return s;
}

std::vector<InputSample> feedAll(StrokeSmoother& smoother, const std::vector<InputSample>& samples) {
    std::vector<InputSample> out;
    for (const InputSample& sample : samples)
        for (const InputSample& painted : smoother.feed(sample))
            out.push_back(painted);
    return out;
}

// A line along y = 0 at 1000 px a second, shaking 5 px up and down at every event.
std::vector<InputSample> shakyLine(int events) {
    std::vector<InputSample> samples;
    samples.reserve(events);
    for (int i = 0; i < events; ++i)
        samples.push_back(at(i * 8.0, i % 2 ? 5.0 : -5.0, 0.5, std::uint64_t(i) * 8));
    return samples;
}

double shakeLeft(const std::vector<InputSample>& painted) {
    double most = 0.0;
    for (std::size_t i = painted.size() / 2; i < painted.size(); ++i)
        most = std::max(most, std::abs(painted[i].pos.y()));
    return most;
}

// How far behind the pen a steady stroke's line is after a while, at one pen event every `step` px.
double trailOnAStraightRun(int level, double step = 8.0) {
    StrokeSmoother smoother(level);
    double x = 0.0;
    QPointF painted;
    for (int i = 0; i < 400; ++i) {
        x = i * step;
        for (const InputSample& out : smoother.feed(at(x, 0.0, 0.5, std::uint64_t(i) * 8)))
            painted = out.pos;
    }
    return x - painted.x();
}

InputSample withKind(InputSample sample, PointerKind kind) {
    sample.kind = kind;
    return sample;
}

}

TEST(StrokeSmoother, LevelZeroPaintsThePensPlacesExactly) {
    StrokeSmoother smoother(0);
    const std::vector<InputSample> samples = shakyLine(20);
    const std::vector<InputSample> painted = feedAll(smoother, samples);
    ASSERT_EQ(painted.size(), samples.size());
    for (std::size_t i = 0; i < samples.size(); ++i)
        EXPECT_EQ(painted[i].pos, samples[i].pos);
    EXPECT_DOUBLE_EQ(painted.back().pressure, 0.5);
    EXPECT_TRUE(smoother.finish().empty());
}

TEST(StrokeSmoother, AStaleFirstPointIsDropped) {
    StrokeSmoother smoother(0);
    EXPECT_TRUE(smoother.feed(at(100.0, 100.0, 0.0, 0)).empty()) << "pressure 0: the position is from before the pen touched";
    const std::vector<InputSample> painted = smoother.feed(at(109.0, 95.0, 0.5, 8));
    ASSERT_EQ(painted.size(), 1u);
    EXPECT_EQ(painted[0].pos, QPointF(109.0, 95.0));
}

TEST(StrokeSmoother, AFirstPointWithRealPressureIsKept) {
    StrokeSmoother smoother(0);
    const std::vector<InputSample> painted = smoother.feed(at(100.0, 100.0, 1.0, 0));
    ASSERT_EQ(painted.size(), 1u);
    EXPECT_EQ(painted[0].pos, QPointF(100.0, 100.0));
}

TEST(StrokeSmoother, OnlyTheFirstPointCanBeDropped) {
    StrokeSmoother smoother(0);
    smoother.feed(at(0.0, 0.0, 0.0, 0));
    smoother.feed(at(1.0, 0.0, 0.01, 8));
    EXPECT_EQ(smoother.feed(at(2.0, 0.0, 0.01, 16)).size(), 1u) << "a very light line still draws";
}

TEST(StrokeSmoother, APenThatNeverMovesOnStillPaintsItsPoint) {
    StrokeSmoother smoother(3);
    EXPECT_TRUE(smoother.feed(at(50.0, 60.0, 0.01, 0)).empty());
    const std::vector<InputSample> rest = smoother.finish();
    ASSERT_EQ(rest.size(), 1u);
    EXPECT_EQ(rest[0].pos, QPointF(50.0, 60.0));
    EXPECT_TRUE(smoother.finish().empty());
}

TEST(StrokeSmoother, HigherLevelsTakeOutMoreShake) {
    const auto shakeAt = [](int level) {
        StrokeSmoother smoother(level);
        return shakeLeft(feedAll(smoother, shakyLine(800)));
    };
    EXPECT_DOUBLE_EQ(shakeAt(0), 5.0);
    EXPECT_LT(shakeAt(2), 5.0);
    EXPECT_LT(shakeAt(4), shakeAt(2) + 1e-9);
    EXPECT_LT(shakeAt(20), 0.1);
}

// A running average of n events trails a steady pen by (n - 1) / 2 events, and level L averages L events.
TEST(StrokeSmoother, TheLineTrailsByHalfTheEventsItAverages) {
    EXPECT_NEAR(trailOnAStraightRun(0), 0.0, 1e-9);
    EXPECT_NEAR(trailOnAStraightRun(1), 0.0, 1e-9) << "one event is the pen itself";
    EXPECT_NEAR(trailOnAStraightRun(3), 8.0, 1e-9);
    EXPECT_NEAR(trailOnAStraightRun(15), 56.0, 1e-9);
}

TEST(StrokeSmoother, EveryLevelFromTwoTrailsMoreThanTheOneBefore) {
    for (int level = 2; level <= StabilizerLevel::kMax; ++level)
        EXPECT_GT(trailOnAStraightRun(level), trailOnAStraightRun(level - 1)) << level;
}

// Levels 16 to 20, the reference's S-1 to S-5, add a second stage that moves a share f of the way each event, so on a steady run the line
// trails level 15's by (1 - f) / f events more. The shares are fitted to Doodlebold's sweep, the recorded C above all: 16
// trails one event more. The reference's S-6 and S-7 are left out.
TEST(StrokeSmoother, TheStrongLevelsTrailByTheirFittedShares) {
    const double step = 8.0;
    const double fifteen = trailOnAStraightRun(15, step);
    const double shares[] = {0.5, 0.2343, 0.1326, 0.0849, 0.0578};
    for (int k = 1; k <= 5; ++k) {
        const double f = shares[k - 1];
        EXPECT_NEAR(trailOnAStraightRun(15 + k, step) - fifteen, (1.0 - f) / f * step, 0.05) << "level " << 15 + k;
    }
    EXPECT_EQ(StabilizerLevel::kMax, 20);
    EXPECT_GT(trailOnAStraightRun(StabilizerLevel::kMax), fifteen * 3.0);
}

// Smoothing counts events, not time, so a faster pen trails by less time.
TEST(StrokeSmoother, TheTrailIsInEventsNotTime) {
    EXPECT_NEAR(trailOnAStraightRun(5, 4.0) * 2.0, trailOnAStraightRun(5, 8.0), 1e-9);
}

TEST(StrokeSmoother, TimestampsChangeNothing) {
    StrokeSmoother a(5);
    StrokeSmoother b(5);
    a.feed(at(0.0, 0.0, 0.5, 0));
    b.feed(at(0.0, 0.0, 0.5, 0));
    EXPECT_EQ(a.feed(at(10.0, 0.0, 0.5, 0)).back().pos, b.feed(at(10.0, 0.0, 0.5, 300)).back().pos);
}

TEST(StrokeSmoother, AtMaxTheLineStillEndsAtThePen) {
    StrokeSmoother smoother(StabilizerLevel::kMax);
    std::vector<InputSample> samples;
    samples.reserve(60);
    for (int i = 0; i < 60; ++i)
        samples.push_back(at(i * 8.0, 0.0, 0.5, std::uint64_t(i) * 8));
    feedAll(smoother, samples);
    const std::vector<InputSample> rest = smoother.finish();
    ASSERT_GE(rest.size(), 2u);
    EXPECT_EQ(rest.back().pos, samples.back().pos);
    EXPECT_LT(std::abs(rest[rest.size() - 2].pos.x() - samples.back().pos.x()), 1.0) << "with no big jump at the end";
}

namespace {

// Over a level's first n events and one more a pen's pressure is also eased in, times (k / (n + 1))^0.8 ((k / n)^0.6 before).
double eased(int k, int n) {
    return std::min(std::pow(static_cast<double>(k) / (n + 1), 0.8), 1.0);
}

}

TEST(StrokeSmoother, APensPressureRisesFromZeroOverTheFirstEvents) {
    StrokeSmoother smoother(3);
    std::vector<double> pressures;
    pressures.reserve(6);
    for (int i = 0; i < 6; ++i)
        pressures.push_back(smoother.feed(at(i * 8.0, 0.0, 0.8, std::uint64_t(i) * 8)).back().pressure);
    EXPECT_NEAR(pressures[0], 0.8 / 3.0 * eased(1, 3), 1e-9);
    EXPECT_NEAR(pressures[1], 1.6 / 3.0 * eased(2, 3), 1e-9);
    EXPECT_NEAR(pressures[2], 0.8 * eased(3, 3), 1e-9);
    EXPECT_NEAR(pressures[3], 0.8, 1e-9);
    EXPECT_NEAR(pressures[5], 0.8, 1e-9);
}

// At levels 0 and 1 the quick line reaches 90 % of its width 33 px in, as the pen's own pressure does.
TEST(StrokeSmoother, AtLevelsZeroAndOnePressureFollowsThePen) {
    for (const int level : {0, 1}) {
        StrokeSmoother smoother(level);
        EXPECT_NEAR(smoother.feed(at(0.0, 0.0, 0.8, 0)).back().pressure, 0.8, 1e-9) << level;
        EXPECT_NEAR(smoother.feed(at(8.0, 0.0, 0.4, 8)).back().pressure, 0.4, 1e-9) << level;
    }
}

// Pressure falls over the level's events, as fast as it rises, for every tool. The Marker's had fallen over one more, which kept its
// ends at Stabilizer 12 too strong.
TEST(StrokeSmoother, PressureFallsAsFastAsItRises) {
    StrokeSmoother::Ends ends;
    ends.liftAtZero = true;
    StrokeSmoother smoother(3, ends);
    double pressure = 0.0;
    for (int i = 0; i < 5; ++i)
        pressure = smoother.feed(at(i * 8.0, 0.0, 0.8, std::uint64_t(i) * 8)).back().pressure;
    EXPECT_NEAR(pressure, 0.8, 1e-9);
    pressure = smoother.feed(at(40.0, 0.0, 0.2, 40)).back().pressure;
    EXPECT_NEAR(pressure, (0.8 * 2.0 + 0.2) / 3.0, 1e-9) << "three events as it falls, as it rises";
}

TEST(StrokeSmoother, HigherLevelsTaperTheStartOverMoreEvents) {
    StrokeSmoother smoother(10);
    double pressure = 0.0;
    for (int i = 0; i < 6; ++i)
        pressure = smoother.feed(at(i * 8.0, 0.0, 1.0, std::uint64_t(i) * 8)).back().pressure;
    EXPECT_NEAR(pressure, 6.0 / 10.0 * eased(6, 10), 1e-9);
}

TEST(StrokeSmoother, AMouseStartsAtFullPressure) {
    StrokeSmoother smoother(5);
    const InputSample first = smoother.feed(withKind(at(0.0, 0.0, 1.0, 0), PointerKind::Mouse)).back();
    EXPECT_DOUBLE_EQ(first.pressure, 1.0);
}

// The C at level 11 bent where the places, grown from the landing alone, stopped growing. Full of the landing from the
// start, the line leaves it smoothly.
TEST(StrokeSmoother, TheLineLeavesWhereThePenLandedSmoothly) {
    StrokeSmoother smoother(10);
    const InputSample first = smoother.feed(at(100.0, 50.0, 0.5, 0)).back();
    EXPECT_EQ(first.pos, QPointF(100.0, 50.0));
    const InputSample second = smoother.feed(at(111.0, 50.0, 0.5, 8)).back();
    EXPECT_NEAR(second.pos.x(), (100.0 * 9.0 + 111.0) / 10.0, 1e-9) << "a level's worth of the landing, less one";
}

TEST(StrokeSmoother, PressureIsSmoothedToo) {
    StrokeSmoother smoother(5);
    for (int i = 0; i < 10; ++i)
        smoother.feed(at(i * 8.0, 0.0, 0.2, std::uint64_t(i) * 8));
    const std::vector<InputSample> painted = smoother.feed(at(80.0, 0.0, 1.0, 80));
    ASSERT_EQ(painted.size(), 1u);
    EXPECT_GT(painted[0].pressure, 0.2);
    EXPECT_LT(painted[0].pressure, 0.5);
}

TEST(StrokeSmoother, OnLiftTheLineCatchesUpToThePen) {
    StrokeSmoother smoother(10);
    std::vector<InputSample> samples;
    samples.reserve(100);
    for (int i = 0; i < 100; ++i)
        samples.push_back(at(i * 8.0, 0.0, 0.5, std::uint64_t(i) * 8));
    const std::vector<InputSample> painted = feedAll(smoother, samples);
    const std::vector<InputSample> rest = smoother.finish();
    ASSERT_FALSE(rest.empty());
    EXPECT_EQ(rest.back().pos, samples.back().pos);
    double x = painted.back().pos.x();
    for (const InputSample& sample : rest) {
        EXPECT_GE(sample.pos.x(), x) << "it only moves forward";
        x = sample.pos.x();
    }
    EXPECT_TRUE(smoother.finish().empty()) << "only once";
}

// The Pencil's lift: pressure 0 where the pen lifted, averaged in as the line catches up, so it narrows to a point there.
TEST(StrokeSmoother, AnEndThatLiftsAtZeroNarrowsToAPointWhereThePenLifted) {
    StrokeSmoother::Ends lift;
    lift.liftAtZero = true;
    StrokeSmoother smoother(3, lift);
    std::vector<InputSample> samples;
    samples.reserve(10);
    for (int i = 0; i < 10; ++i)
        samples.push_back(at(i * 18.0, 0.0, 0.8, std::uint64_t(i) * 8));
    feedAll(smoother, samples);
    const std::vector<InputSample> rest = smoother.finish();
    ASSERT_GE(rest.size(), 4u);
    EXPECT_EQ(rest.back().pos, samples.back().pos);
    EXPECT_DOUBLE_EQ(rest.back().pressure, 0.0);
    for (std::size_t i = 1; i < rest.size(); ++i)
        EXPECT_LE(rest[i].pressure, rest[i - 1].pressure) << i;
    EXPECT_GT(rest.front().pressure, 0.4) << "it narrows over the run, not at once";
}

TEST(StrokeSmoother, SameInputGivesTheSamePath) {
    const auto path = [] {
        StrokeSmoother smoother(4);
        std::vector<QPointF> points;
        for (const InputSample& sample : feedAll(smoother, shakyLine(50)))
            points.push_back(sample.pos);
        return points;
    };
    EXPECT_EQ(path(), path());
}

class StrokeControllerSmoothing : public testing::Test {
protected:
    void SetUp() override {
        doc = std::make_unique<Document>(QSize(300, 300), Qt::white);
        ASSERT_TRUE(doc->addLayer());
        settings = defaultSettings(ToolType::Pencil);
        settings.size = 100;
        settings.pressureSize = false;
    }
    TiledImage* layer() { return doc->paintTarget(doc->currentLayerId()).value(); }
    int alphaAt(int x, int y) { return layer()->pixel(x, y)[3]; }

    std::unique_ptr<Document> doc;
    BrushSettings settings;
};

TEST_F(StrokeControllerSmoothing, AStaleLandingPointLeavesNoHook) {
    StrokeController stroke(doc.get());
    ASSERT_TRUE(stroke.begin(at(100.0, 100.0, 0.0, 0), Qt::black, settings));
    stroke.add(at(150.0, 150.0, 0.5, 8));
    stroke.add(at(160.0, 150.0, 0.5, 16));
    stroke.finish();
    EXPECT_EQ(alphaAt(100, 100), 0) << "where the pen was before it touched";
    EXPECT_EQ(alphaAt(125, 125), 0) << "the way from there";
    EXPECT_NEAR(alphaAt(155, 150), kChannelOne, 100);
}

TEST_F(StrokeControllerSmoothing, AQuickTapWithOnlyOnePointStillMarks) {
    StrokeController stroke(doc.get());
    settings.minSize = 100;
    ASSERT_TRUE(stroke.begin(at(100.5, 100.5, 0.01, 0), Qt::black, settings));
    stroke.finish();
    EXPECT_GT(alphaAt(100, 100), 0);
}

TEST_F(StrokeControllerSmoothing, TheStabilizedLineReachesThePenOnLift) {
    StrokeController stroke(doc.get());
    ASSERT_TRUE(stroke.begin(at(20.5, 150.5, 1.0, 0), Qt::black, settings, 10));
    for (int i = 1; i <= 25; ++i)
        stroke.add(at(20.5 + i * 8.0, 150.5, 1.0, std::uint64_t(i) * 8));
    EXPECT_EQ(alphaAt(219, 150), 0) << "still trailing the pen";
    stroke.finish();
    EXPECT_GT(alphaAt(215, 150), 0) << "it narrows as it gets there, but it gets there";
    EXPECT_NEAR(alphaAt(100, 150), kChannelOne, 100);
}

TEST_F(StrokeControllerSmoothing, ACancelledStrokeLeavesNothing) {
    StrokeController stroke(doc.get());
    ASSERT_TRUE(stroke.begin(at(100.0, 100.0, 0.0, 0), Qt::black, settings));
    stroke.cancel();
    EXPECT_FALSE(stroke.isActive());
    EXPECT_EQ(alphaAt(100, 100), 0);
}
