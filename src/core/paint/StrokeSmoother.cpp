#include "core/paint/StrokeSmoother.h"

#include "core/brush/BrushSettings.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <utility>

// docs/brush-engine/stroke-smoother.md says what the named values here are for and how they were chosen.
namespace doodlebold {

namespace {

constexpr double kLandingPressure = 0.05;
constexpr int kFirstStrongLevel = StabilizerLevel::kLastAveraged + 1;  // 16, the reference's S-1
constexpr double kCaughtUpPx = 0.25;
constexpr int kMostCatchUpSamples = 1024;
// The strong levels' second stage (16 to 20, the reference's S-1 to S-5): the share of the way it moves each event.
constexpr std::array<double, StabilizerLevel::kMax - StabilizerLevel::kLastAveraged> kStrongShares{0.5, 0.2343, 0.1326, 0.0849, 0.0578};
// Over a stroke's first events the pressure eases in a little more slowly than the average alone gives it, so the line
// starts at a fine point and widens over two or three events.
constexpr double kStartEase = 0.8;
constexpr int kStartEaseExtra = 1;  // events

// A level averages that many events, places and pressure alike, so levels 0 and 1 follow the pen, and the strong levels average as
// many as level 15. Fitted at level 3, the global level, to the reference's Pencil, for every tool, and at the others to
// its stabilizer readings.
int eventsOfLevel(int level) {
    return std::clamp(level, 1, StabilizerLevel::kLastAveraged);
}

// At the strong levels a second stage follows the first, fed back on itself, moving less of the way at each level.
double feedbackOfLevel(int level) {
    if (level < kFirstStrongLevel)
        return 1.0;
    return kStrongShares[static_cast<std::size_t>(std::min(level, static_cast<int>(StabilizerLevel::kMax)) - kFirstStrongLevel)];
}

// A mouse's button and a finger have no pressure to rise from 0, so they paint at full pressure from the start.
bool atFullPressure(PointerKind kind) {
    return kind == PointerKind::Mouse || kind == PointerKind::Touch;
}

bool sensesPressureFrom(const InputSample& sample) {
    return !atFullPressure(sample.kind);
}

double towards(double from, double to, double amount) {
    return from + (to - from) * amount;
}

}

StrokeSmoother::StrokeSmoother(int level) : StrokeSmoother(level, {}) {}

StrokeSmoother::StrokeSmoother(int level, Ends ends) : m_events(eventsOfLevel(level)), m_feedback(feedbackOfLevel(level)), m_ends(ends) {}

std::vector<InputSample> StrokeSmoother::feed(const InputSample& sample) {
    const bool first = !std::exchange(m_sawFirst, true);
    if (first && sample.pressure < kLandingPressure) {
        m_landing = sample;
        return {};
    }
    if (first || m_landing) {
        m_landing.reset();  // a held first point is stale: the stroke starts here instead
        start(sample);
        return {smoothed(sample)};
    }
    if (!m_started)
        return {};
    m_last = sample;
    return {smoothed(sample)};
}

std::vector<InputSample> StrokeSmoother::finish() {
    if (m_landing) {
        const InputSample only = *m_landing;  // the pen never moved on: this is all there was
        m_landing.reset();
        return {only};
    }
    if (!std::exchange(m_started, false))
        return {};
    return restOfThePath();
}

// What's still behind the pen when it lifts: the averages fed the pen's last event until they reach it, so the ink goes all the
// way to where the pen lifted.
std::vector<InputSample> StrokeSmoother::restOfThePath() {
    std::vector<InputSample> rest;
    if (m_ends.liftAtZero && sensesPressureFrom(m_last))
        m_last.pressure = 0.0;
    if (caughtUp())
        return rest;
    for (int i = 0; i < kMostCatchUpSamples && !caughtUp(); ++i)
        rest.push_back(smoothed(m_last));
    rest.push_back(m_last);
    return rest;
}

// The places start full of the first, as many as the level averages, so the line leaves where the pen landed smoothly and curves
// into the rest of the stroke (they grew from the first alone before, which bent the line where they stopped growing). A
// pen's pressure starts full of 0, so it rises over the first few events and the start tapers. A mouse has no pressure to rise, so
// it starts at full.
void StrokeSmoother::start(const InputSample& sample) {
    m_started = true;
    m_last = sample;
    const double pressure = atFullPressure(sample.kind) ? 1.0 : 0.0;
    m_eventsSoFar = 0;
    m_places.assign(static_cast<std::size_t>(m_events), sample.pos);
    m_pressures.assign(static_cast<std::size_t>(m_events), pressure);
    m_out = {sample.pos.x(), sample.pos.y(), pressure};
}

StrokeSmoother::Point StrokeSmoother::averaged() const {
    Point total;
    for (const QPointF& place : m_places) {
        total.x += place.x();
        total.y += place.y();
    }
    for (const double pressure : m_pressures)
        total.pressure += pressure;
    const double places = static_cast<double>(m_places.size());
    return {total.x / places, total.y / places, total.pressure / static_cast<double>(m_pressures.size())};
}

// A plain average of the last few events, then at the strong levels a second stage that follows it, fed back on itself.
InputSample StrokeSmoother::smoothed(const InputSample& sample) {
    m_places.push_back(sample.pos);
    if (m_places.size() > static_cast<std::size_t>(m_events))
        m_places.pop_front();
    m_pressures.push_back(atFullPressure(sample.kind) ? 1.0 : sample.pressure);
    m_pressures.pop_front();
    const Point average = averaged();
    m_out = {towards(m_out.x, average.x, m_feedback), towards(m_out.y, average.y, m_feedback),
             towards(m_out.pressure, average.pressure, m_feedback)};
    InputSample out = sample;
    out.pos = QPointF(m_out.x, m_out.y);
    out.pressure = m_out.pressure;
    ++m_eventsSoFar;
    if (sensesPressureFrom(sample))
        out.pressure *= startEase();
    return out;
}

// What the pressure is times over the first events: (events so far / (the level's events + kStartEaseExtra))^kStartEase, then 1.
double StrokeSmoother::startEase() const {
    if (m_events <= 1)
        return 1.0;
    const int over = m_events + kStartEaseExtra;
    if (m_eventsSoFar >= over)
        return 1.0;
    return std::pow(static_cast<double>(m_eventsSoFar) / over, kStartEase);
}

bool StrokeSmoother::caughtUp() const {
    const bool placesThere = std::all_of(m_places.begin(), m_places.end(), [this](QPointF place) { return place == m_last.pos; });
    const bool pressureThere =
        !m_ends.liftAtZero || std::all_of(m_pressures.begin(), m_pressures.end(), [this](double p) { return p == m_last.pressure; });
    return placesThere && pressureThere && std::hypot(m_out.x - m_last.pos.x(), m_out.y - m_last.pos.y()) < kCaughtUpPx;
}

}
