#include "core/stroke/StrokePath.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <utility>

namespace doodlebold {

namespace {

// px: no chord's centripetal knot (its length's square root) is spaced by less than this one's root. The stamps' walk already merges
// samples under 1e-9 px, so their curve never meets a chord of 0; up to 0.009 px no brush-pixel case changes.
constexpr double kSamePlace = 1e-6;
// px: how finely a segment is walked: a piece this long strays from the curve by at most h²/8ρ, 0.031 px on a turn of 1 px radius.
// The mocks score the same from 0.125 to 1 px.
constexpr double kFlattenStep = 0.5;
// and a segment is cut into at most this many, so a far-off sample can't take millions of points (513 points, 8 KB). No reference
// chord reaches it; on the recorded fast strokes no cap moves them by more than 8 in 65535 but at one spot.
constexpr int kMostFlattenSteps = 512;

double lengthOf(QPointF v) {
    return std::hypot(v.x(), v.y());
}

QPointF mix(QPointF a, QPointF b, double t) {
    return a + (b - a) * t;
}

QPointF between(QPointF a, QPointF b, double ta, double tb, double t) {
    return mix(a, b, (t - ta) / (tb - ta));
}

// Barry and Goldman's evaluation. The centripetal guarantee belongs to this form, not to the tangent form with the same knots.
QPointF onSpline(const std::array<QPointF, 4>& p, const std::array<double, 4>& k, double t) {
    const QPointF a1 = between(p[0], p[1], k[0], k[1], t);
    const QPointF a2 = between(p[1], p[2], k[1], k[2], t);
    const QPointF a3 = between(p[2], p[3], k[2], k[3], t);
    const QPointF b1 = between(a1, a2, k[0], k[2], t);
    const QPointF b2 = between(a2, a3, k[1], k[3], t);
    return between(b1, b2, k[1], k[2], t);
}

std::array<double, 4> centripetalKnots(const std::array<QPointF, 4>& p) {
    std::array<double, 4> knots{0.0, 0.0, 0.0, 0.0};
    for (int i = 1; i < 4; ++i)
        knots[i] = knots[i - 1] + std::sqrt(std::max(lengthOf(p[i] - p[i - 1]), kSamePlace));
    return knots;
}

std::vector<QPointF> flatten(const std::array<QPointF, 4>& p) {
    const std::array<double, 4> knots = centripetalKnots(p);
    const double chord = lengthOf(p[2] - p[1]);
    const int steps = std::clamp(static_cast<int>(std::ceil(chord / kFlattenStep)), 1, kMostFlattenSteps);
    std::vector<QPointF> line;
    line.reserve(steps + 1);
    for (int i = 0; i <= steps; ++i)
        line.push_back(onSpline(p, knots, knots[1] + (knots[2] - knots[1]) * i / steps));
    line.back() = p[2];
    return line;
}

}

std::vector<QPointF> curveBetween(const std::array<QPointF, 4>& p) {
    return flatten(p);
}

}
