#pragma once

#include "core/input/InputSample.h"

#include <deque>
#include <optional>
#include <vector>

namespace doodlebold {

// Turns pen samples into the samples a painting stroke follows, one for each pen event, never resampled to time. It drops a stale
// first point when the pen lands, averages position and pressure over the last few events, and starts a pen's pressure from 0 so
// the start tapers.
class StrokeSmoother {
public:
    // How the stroke's ends ease in and out.
    struct Ends {
        // The pen's lift is pressure 0 where it last was, and the catch-up averages it in, so the line narrows to a point there.
        bool liftAtZero = false;
    };

    explicit StrokeSmoother(int level = 0);
    StrokeSmoother(int level, Ends ends);

    // What to paint for this pen sample, which may be nothing yet.
    std::vector<InputSample> feed(const InputSample& sample);
    // The rest of the stroke when the pen lifts: the line catches up to where the pen last was.
    std::vector<InputSample> finish();

private:
    struct Point {
        double x = 0.0;
        double y = 0.0;
        double pressure = 0.0;
    };

    void start(const InputSample& sample);
    double startEase() const;
    InputSample smoothed(const InputSample& sample);
    Point averaged() const;
    bool caughtUp() const;
    std::vector<InputSample> restOfThePath();

    int m_events = 1;  // how many events the first stage averages position and pressure over
    double m_feedback = 1.0;  // how far the second stage moves to the first's average at each event. At 1 there's no second stage
    Ends m_ends;
    std::deque<QPointF> m_places;
    std::deque<double> m_pressures;
    Point m_out;
    bool m_sawFirst = false;
    bool m_started = false;
    std::optional<InputSample> m_landing;
    InputSample m_last;
    int m_eventsSoFar = 0;  // since the stroke started
};

}
