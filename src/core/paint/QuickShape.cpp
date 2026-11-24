#include "core/paint/QuickShape.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>

namespace doodlebold {

namespace {

constexpr double kPi = std::numbers::pi;
constexpr double kDegree = kPi / 180.0;

constexpr int kResampled = 180;
constexpr double kMinimumDiagonal = 12.0;
constexpr double kClosedGap = 0.22;       // of the diagonal
constexpr double kEndPart = 0.18;         // of the stroke: where its two ends may meet
constexpr double kMinimumFill = 0.15;     // the line's area in its bounding box's
constexpr double kTurningSlack = 100.0;   // degrees off one full turn
constexpr double kCornerWindow = 0.035;   // of the length
constexpr double kCornerTurn = 40.0;      // degrees
constexpr double kLineMean = 0.028;       // of the diagonal
constexpr double kLineWorst = 0.09;
constexpr double kOvalMean = 0.035;
constexpr double kOvalWorst = 0.09;
constexpr double kRightAngleSlack = 20.0;  // degrees
constexpr double kSameSides = 0.12;        // the difference that still makes a Square or a Circle
constexpr double kUprightSlack = 8.0;      // degrees off upright that become upright
constexpr int kMostCorners = 12;
constexpr double kPolygonSampleSpacing = 4.0;
constexpr double kOvalSampleSpacing = 2.0;
constexpr int kMaximumSamples = 4000;

using Points = std::vector<QPointF>;

double length(const QPointF& v) {
    return std::hypot(v.x(), v.y());
}

double cross(const QPointF& a, const QPointF& b) {
    return a.x() * b.y() - a.y() * b.x();
}

double dot(const QPointF& a, const QPointF& b) {
    return a.x() * b.x() + a.y() * b.y();
}

QPointF turned(const QPointF& v, double angle) {
    const double c = std::cos(angle), s = std::sin(angle);
    return {v.x() * c - v.y() * s, v.x() * s + v.y() * c};
}

Points resampled(const Points& line, int count) {
    double total = 0.0;
    for (std::size_t i = 1; i < line.size(); ++i)
        total += length(line[i] - line[i - 1]);
    Points out;
    out.reserve(std::size_t(count));
    if (total <= 0.0)
        return out;
    const double step = total / (count - 1);
    double covered = 0.0;
    std::size_t i = 1;
    for (int n = 0; n < count; ++n) {
        const double wanted = n * step;
        while (i + 1 < line.size() && covered + length(line[i] - line[i - 1]) < wanted) {
            covered += length(line[i] - line[i - 1]);
            ++i;
        }
        const double segment = length(line[i] - line[i - 1]);
        const double t = segment > 0.0 ? std::clamp((wanted - covered) / segment, 0.0, 1.0) : 0.0;
        out.push_back(line[i - 1] + (line[i] - line[i - 1]) * t);
    }
    return out;
}

struct Bounds {
    double left = std::numeric_limits<double>::max(), right = std::numeric_limits<double>::lowest();
    double top = std::numeric_limits<double>::max(), bottom = std::numeric_limits<double>::lowest();
    void add(const QPointF& p) {
        left = std::min(left, p.x());
        right = std::max(right, p.x());
        top = std::min(top, p.y());
        bottom = std::max(bottom, p.y());
    }
    double width() const { return right - left; }
    double height() const { return bottom - top; }
    QPointF centre() const { return {(left + right) / 2.0, (top + bottom) / 2.0}; }
};

template <typename Range> Bounds boundsOf(const Range& points) {
    Bounds b;
    for (const QPointF& p : points)
        b.add(p);
    return b;
}

double signedArea(const Points& loop) {
    double area = 0.0;
    for (std::size_t i = 0; i < loop.size(); ++i)
        area += cross(loop[i], loop[(i + 1) % loop.size()]);
    return area / 2.0;
}

// The stroke from where its two ends come closest, with the end repeating the start; none if they stay apart.
std::optional<Points> closedLoop(const Points& line, double diagonal) {
    const int n = int(line.size());
    const int part = int(n * kEndPart);
    double best = std::numeric_limits<double>::max();
    int from = 0, to = n - 1;
    for (int i = 0; i < part; ++i)
        for (int j = n - part; j < n; ++j)
            if (const double d = length(line[std::size_t(j)] - line[std::size_t(i)]); d < best) {
                best = d;
                from = i;
                to = j;
            }
    if (best > kClosedGap * diagonal)
        return std::nullopt;
    Points loop(line.begin() + from, line.begin() + to + 1);
    if (best > 0.0)
        loop.push_back(loop.front());
    return loop;
}

// Signed degrees the closed line (no repeated end) turns through in all, over a smoothing span.
double totalTurning(const Points& loop) {
    const int n = int(loop.size());
    const int span = std::max(2, n / 24);
    double total = 0.0;
    QPointF before = loop[std::size_t(span % n)] - loop[0];
    for (int i = 1; i <= n; ++i) {
        const QPointF after = loop[std::size_t((i + span) % n)] - loop[std::size_t(i % n)];
        total += std::atan2(cross(before, after), dot(before, after));
        before = after;
    }
    return total / kDegree;
}

double distanceToSegment(const QPointF& p, const QPointF& a, const QPointF& b) {
    const QPointF ab = b - a;
    const double len2 = dot(ab, ab);
    const double t = len2 > 0.0 ? std::clamp(dot(p - a, ab) / len2, 0.0, 1.0) : 0.0;
    return length(p - (a + ab * t));
}

struct Fit {
    double mean = std::numeric_limits<double>::max();
    double worst = std::numeric_limits<double>::max();
};

struct Ellipse {
    QPointF centre;
    double a = 0.0, b = 0.0, angle = 0.0;
    Fit fit;
};

// Principal axes of the line, then the semi-axes that fit it best in the least-squares sense.
Ellipse fitEllipse(const Points& loop, double diagonal) {
    QPointF mean;
    for (const QPointF& p : loop)
        mean += p;
    mean /= double(loop.size());
    double xx = 0.0, xy = 0.0, yy = 0.0;
    for (const QPointF& p : loop) {
        const QPointF d = p - mean;
        xx += d.x() * d.x();
        xy += d.x() * d.y();
        yy += d.y() * d.y();
    }
    Ellipse e;
    e.angle = 0.5 * std::atan2(2.0 * xy, xx - yy);
    Bounds local;
    Points flat;
    flat.reserve(loop.size());
    for (const QPointF& p : loop) {
        flat.push_back(turned(p - mean, -e.angle));
        local.add(flat.back());
    }
    const QPointF middle = local.centre();
    double u4 = 0.0, v4 = 0.0, u2v2 = 0.0, u2 = 0.0, v2 = 0.0;
    for (QPointF& p : flat) {
        p -= middle;
        const double uu = p.x() * p.x(), vv = p.y() * p.y();
        u4 += uu * uu;
        v4 += vv * vv;
        u2v2 += uu * vv;
        u2 += uu;
        v2 += vv;
    }
    const double det = u4 * v4 - u2v2 * u2v2;
    const double A = det != 0.0 ? (u2 * v4 - v2 * u2v2) / det : 0.0;
    const double B = det != 0.0 ? (v2 * u4 - u2 * u2v2) / det : 0.0;
    e.a = A > 0.0 ? 1.0 / std::sqrt(A) : local.width() / 2.0;
    e.b = B > 0.0 ? 1.0 / std::sqrt(B) : local.height() / 2.0;
    e.centre = mean + turned(middle, e.angle);
    if (e.a <= 0.0 || e.b <= 0.0)
        return e;
    double sum = 0.0, worst = 0.0;
    for (const QPointF& p : flat) {
        const double r = std::hypot(p.x() / e.a, p.y() / e.b);
        const double d = r > 0.0 ? std::abs(r - 1.0) * length(p) / r : std::min(e.a, e.b);
        sum += d;
        worst = std::max(worst, d);
    }
    e.fit = {sum / double(flat.size()) / diagonal, worst / diagonal};
    return e;
}

// Corners: where the line turns sharply over a short span.
std::vector<int> findCorners(const Points& loop) {
    const int n = int(loop.size());
    const int window = std::max(2, int(std::lround(n * kCornerWindow)));
    std::vector<double> turn(std::size_t(n), 0.0);
    for (int i = 0; i < n; ++i) {
        const QPointF before = loop[std::size_t(i)] - loop[std::size_t((i - window + n) % n)];
        const QPointF after = loop[std::size_t((i + window) % n)] - loop[std::size_t(i)];
        turn[std::size_t(i)] = std::abs(std::atan2(cross(before, after), dot(before, after))) / kDegree;
    }
    std::vector<int> corners;
    for (int i = 0; i < n; ++i) {
        if (turn[std::size_t(i)] < kCornerTurn)
            continue;
        bool peak = true;
        for (int d = 1; d <= window && peak; ++d) {
            peak = turn[std::size_t(i)] >= turn[std::size_t((i + d) % n)] && turn[std::size_t(i)] > turn[std::size_t((i - d + n) % n)];
        }
        if (peak)
            corners.push_back(i);
    }
    return corners;
}

struct Line {
    QPointF point, direction;
};

Line fitLine(const Points& loop, int from, int to) {
    const int n = int(loop.size());
    const int count = (to - from + n) % n;
    const int skip = std::max(1, count / 8);
    QPointF mean;
    int used = 0;
    for (int k = skip; k <= count - skip; ++k) {
        mean += loop[std::size_t((from + k) % n)];
        ++used;
    }
    if (used < 2)
        return {loop[std::size_t(from)], loop[std::size_t(to)] - loop[std::size_t(from)]};
    mean /= double(used);
    double xx = 0.0, xy = 0.0, yy = 0.0;
    for (int k = skip; k <= count - skip; ++k) {
        const QPointF d = loop[std::size_t((from + k) % n)] - mean;
        xx += d.x() * d.x();
        xy += d.x() * d.y();
        yy += d.y() * d.y();
    }
    const double angle = 0.5 * std::atan2(2.0 * xy, xx - yy);
    return {mean, QPointF(std::cos(angle), std::sin(angle))};
}

// The corners with the sides straightened: each corner where the lines of its two sides meet.
Points straightened(const Points& loop, const std::vector<int>& corners, double diagonal) {
    const int count = int(corners.size());
    std::vector<Line> sides;
    sides.reserve(std::size_t(count));
    for (int i = 0; i < count; ++i)
        sides.push_back(fitLine(loop, corners[std::size_t(i)], corners[std::size_t((i + 1) % count)]));
    Points out;
    out.reserve(std::size_t(count));
    for (int i = 0; i < count; ++i) {
        const Line& a = sides[std::size_t((i - 1 + count) % count)];
        const Line& b = sides[std::size_t(i)];
        const QPointF raw = loop[std::size_t(corners[std::size_t(i)])];
        const double denominator = cross(a.direction, b.direction);
        QPointF at = raw;
        if (std::abs(denominator) > 0.2) {
            const QPointF met = a.point + a.direction * (cross(b.point - a.point, b.direction) / denominator);
            if (length(met - raw) < 0.15 * diagonal)
                at = met;
        }
        out.push_back(at);
    }
    return out;
}

Fit fitPolygon(const Points& loop, const Points& corners, double diagonal) {
    double sum = 0.0, worst = 0.0;
    for (const QPointF& p : loop) {
        double best = std::numeric_limits<double>::max();
        for (std::size_t i = 0; i < corners.size(); ++i)
            best = std::min(best, distanceToSegment(p, corners[i], corners[(i + 1) % corners.size()]));
        sum += best;
        worst = std::max(worst, best);
    }
    return {sum / double(loop.size()) / diagonal, worst / diagonal};
}

// Closest to upright: between -45° and 45°; within kUprightSlack of it, upright.
double uprightAngle(double angle) {
    double a = std::remainder(angle, kPi / 2.0);
    if (std::abs(a) < kUprightSlack * kDegree)
        a = 0.0;
    return a;
}

std::optional<QuickShape> asRectangle(const Points& corners) {
    double directionX = 0.0, directionY = 0.0;
    for (std::size_t i = 0; i < 4; ++i) {
        const QPointF side = corners[(i + 1) % 4] - corners[i];
        const QPointF next = corners[(i + 2) % 4] - corners[(i + 1) % 4];
        const double angle = std::abs(std::atan2(cross(side, next), dot(side, next))) / kDegree;
        if (std::abs(angle - 90.0) > kRightAngleSlack)
            return std::nullopt;
        // Four times the side's angle: sides a right angle apart then add up.
        const double a = 4.0 * std::atan2(side.y(), side.x());
        directionX += std::cos(a) * length(side);
        directionY += std::sin(a) * length(side);
    }
    QuickShape shape;
    shape.angle = uprightAngle(std::atan2(directionY, directionX) / 4.0);
    Bounds flat;
    for (const QPointF& p : corners)
        flat.add(turned(p, -shape.angle));
    shape.centre = turned(flat.centre(), shape.angle);
    shape.width = flat.width();
    shape.height = flat.height();
    if (std::abs(shape.width - shape.height) <= kSameSides * std::max(shape.width, shape.height)) {
        shape.kind = QuickShapeKind::Square;
        shape.width = shape.height = (shape.width + shape.height) / 2.0;
    } else {
        shape.kind = QuickShapeKind::Rectangle;
    }
    return shape;
}

QuickShape asPolygon(const Points& corners, QuickShapeKind kind) {
    const Bounds b = boundsOf(corners);
    QuickShape shape;
    shape.kind = kind;
    shape.centre = b.centre();
    shape.width = b.width();
    shape.height = b.height();
    shape.corners.reserve(corners.size());
    for (const QPointF& p : corners)
        shape.corners.push_back(shape.toUnit(p));
    return shape;
}

QuickShape asOval(const Ellipse& e) {
    QuickShape shape;
    shape.centre = e.centre;
    double width = 2.0 * e.a, height = 2.0 * e.b;
    double angle = uprightAngle(e.angle);
    // uprightAngle may have turned the box by a right angle: its width then lies along the other axis.
    if (std::abs(std::cos(e.angle - angle)) < 0.5)
        std::swap(width, height);
    shape.angle = angle;
    if (std::abs(width - height) <= kSameSides * std::max(width, height)) {
        shape.kind = QuickShapeKind::Circle;
        shape.width = shape.height = (width + height) / 2.0;
        shape.angle = 0.0;
    } else {
        shape.kind = QuickShapeKind::Oval;
        shape.width = width;
        shape.height = height;
    }
    return shape;
}

Points regularCorners(int count) {
    Points out;
    out.reserve(std::size_t(count));
    for (int i = 0; i < count; ++i) {
        const double a = -kPi / 2.0 + i * 2.0 * kPi / count;
        out.emplace_back(std::cos(a) / 2.0, std::sin(a) / 2.0);
    }
    return out;
}

Points boxCorners() {
    return {{-0.5, -0.5}, {0.5, -0.5}, {0.5, 0.5}, {-0.5, 0.5}};
}

Points shapeCorners(const QuickShape& shape) {
    return shape.hasOwnCorners() ? shape.corners : boxCorners();
}

}

QPointF QuickShape::toDocument(const QPointF& unit) const {
    return centre + turned(QPointF(unit.x() * width, unit.y() * height), angle);
}

QPointF QuickShape::toUnit(const QPointF& doc) const {
    const QPointF flat = turned(doc - centre, -angle);
    return {width > 0.0 ? flat.x() / width : 0.0, height > 0.0 ? flat.y() / height : 0.0};
}

std::optional<QuickShape> recognizeShape(const std::vector<QPointF>& stroke) {
    if (stroke.size() < 8)
        return std::nullopt;
    const Points line = resampled(stroke, kResampled);
    if (line.empty())
        return std::nullopt;
    const Bounds whole = boundsOf(line);
    const double diagonal = std::hypot(whole.width(), whole.height());
    if (diagonal < kMinimumDiagonal)
        return std::nullopt;
    const std::optional<Points> closed = closedLoop(line, diagonal);
    if (!closed)
        return std::nullopt;
    Points loop = resampled(*closed, kResampled);
    if (loop.size() < 16)
        return std::nullopt;
    loop.pop_back();  // the end repeats the start
    const Bounds bounds = boundsOf(loop);
    const double area = signedArea(loop);
    if (std::abs(area) < kMinimumFill * bounds.width() * bounds.height())
        return std::nullopt;
    if (std::abs(std::abs(totalTurning(loop)) - 360.0) > kTurningSlack)
        return std::nullopt;

    const Ellipse ellipse = fitEllipse(loop, diagonal);
    const std::vector<int> found = findCorners(loop);
    Points corners;
    Fit polygon;
    if (found.size() >= 3 && found.size() <= kMostCorners) {
        corners = straightened(loop, found, diagonal);
        polygon = fitPolygon(loop, corners, diagonal);
    }
    const bool lineFits = polygon.mean < kLineMean && polygon.worst < kLineWorst;
    const bool ovalFits = ellipse.fit.mean < kOvalMean && ellipse.fit.worst < kOvalWorst;

    std::optional<QuickShape> shape;
    if (lineFits && (!ovalFits || polygon.mean <= ellipse.fit.mean)) {
        if (corners.size() == 3)
            shape = asPolygon(corners, QuickShapeKind::Triangle);
        else if (corners.size() == 4)
            shape = asRectangle(corners);
        if (!shape)
            shape = asPolygon(corners, QuickShapeKind::Polygon);
    } else if (ovalFits) {
        shape = asOval(ellipse);
    }
    if (shape) {
        // In screen terms a positive area runs clockwise, which is the way a shape's corners are kept.
        shape->reversed = area < 0.0;
        if (shape->reversed)
            std::reverse(shape->corners.begin(), shape->corners.end());
        shape->start = shape->toUnit(stroke.front());
    }
    return shape;
}

std::vector<QPointF> shapeOutline(const QuickShape& shape) {
    Points out;
    if (shape.kind == QuickShapeKind::Circle || shape.kind == QuickShapeKind::Oval) {
        const double a = shape.width / 2.0, b = shape.height / 2.0;
        const double around = kPi * (3.0 * (a + b) - std::sqrt((3.0 * a + b) * (a + 3.0 * b)));
        const int count = std::clamp(int(around / kOvalSampleSpacing), 16, kMaximumSamples);
        const double from = std::atan2(shape.start.y(), shape.start.x());
        for (int i = 0; i <= count; ++i) {
            const double t = from + (shape.reversed ? -1.0 : 1.0) * i * 2.0 * kPi / count;
            out.push_back(shape.toDocument({std::cos(t) / 2.0, std::sin(t) / 2.0}));
        }
        return out;
    }
    Points corners;
    for (const QPointF& unit : shapeCorners(shape))
        corners.push_back(shape.toDocument(unit));
    const QPointF from = shape.toDocument(shape.start);
    std::size_t first = 0;
    double best = std::numeric_limits<double>::max();
    for (std::size_t i = 0; i < corners.size(); ++i)
        if (const double d = length(corners[i] - from); d < best) {
            best = d;
            first = i;
        }
    std::rotate(corners.begin(), corners.begin() + std::ptrdiff_t(first), corners.end());
    if (shape.reversed)
        std::reverse(corners.begin() + 1, corners.end());
    double around = 0.0;
    for (std::size_t i = 0; i < corners.size(); ++i)
        around += length(corners[(i + 1) % corners.size()] - corners[i]);
    const double spacing = std::max(kPolygonSampleSpacing, around / kMaximumSamples);
    for (std::size_t i = 0; i < corners.size(); ++i) {
        const QPointF a = corners[i], b = corners[(i + 1) % corners.size()];
        const int steps = std::max(1, int(length(b - a) / spacing));
        for (int s = 0; s < steps; ++s)
            out.push_back(a + (b - a) * (double(s) / steps));
    }
    out.push_back(corners.front());
    return out;
}

std::vector<InputSample> shapeSamples(const QuickShape& shape, const std::vector<InputSample>& stroke) {
    std::vector<InputSample> out;
    if (stroke.empty())
        return out;
    const Points line = shapeOutline(shape);
    std::vector<double> along(line.size(), 0.0);
    for (std::size_t i = 1; i < line.size(); ++i)
        along[i] = along[i - 1] + length(line[i] - line[i - 1]);
    std::vector<double> drawn(stroke.size(), 0.0);
    for (std::size_t i = 1; i < stroke.size(); ++i)
        drawn[i] = drawn[i - 1] + length(stroke[i].pos - stroke[i - 1].pos);
    const double lineTotal = along.back() > 0.0 ? along.back() : 1.0;
    out.reserve(line.size());
    std::size_t at = 0;
    for (std::size_t i = 0; i < line.size(); ++i) {
        const double wanted = along[i] / lineTotal * drawn.back();
        while (at + 1 < stroke.size() && drawn[at + 1] < wanted)
            ++at;
        const std::size_t next = std::min(at + 1, stroke.size() - 1);
        const double span = drawn[next] - drawn[at];
        const double t = span > 0.0 ? std::clamp((wanted - drawn[at]) / span, 0.0, 1.0) : 0.0;
        InputSample s = stroke[at];
        s.pos = line[i];
        s.pressure = stroke[at].pressure + (stroke[next].pressure - stroke[at].pressure) * t;
        s.tiltX = stroke[at].tiltX + (stroke[next].tiltX - stroke[at].tiltX) * t;
        s.tiltY = stroke[at].tiltY + (stroke[next].tiltY - stroke[at].tiltY) * t;
        s.tangentialPressure = stroke[at].tangentialPressure + (stroke[next].tangentialPressure - stroke[at].tangentialPressure) * t;
        out.push_back(s);
    }
    return out;
}

QuickShape convertShape(const QuickShape& shape, QuickShapeKind kind, int polygonCorners) {
    QuickShape out = shape;
    out.kind = kind;
    out.corners.clear();
    switch (kind) {
    case QuickShapeKind::Square:
    case QuickShapeKind::Circle: out.width = out.height = (shape.width + shape.height) / 2.0; break;
    case QuickShapeKind::Triangle: out.corners = {{0.0, -0.5}, {0.5, 0.5}, {-0.5, 0.5}}; break;
    case QuickShapeKind::Polygon: out.corners = regularCorners(polygonCorners); break;
    default: break;
    }
    return out;
}

void QuickShapeEdit::setKind(QuickShapeKind kind) {
    if (kind != m_shape.kind)
        m_shape = convertShape(m_shape, kind, std::clamp(cornerCount(), kMinimumCorners, kMaximumCorners));
}

int QuickShapeEdit::cornerCount() const {
    return m_shape.kind == QuickShapeKind::Polygon ? int(m_shape.corners.size()) : kMinimumCorners;
}

void QuickShapeEdit::setCornerCount(int corners) {
    if (m_shape.kind == QuickShapeKind::Polygon && corners >= kMinimumCorners && corners <= kMaximumCorners)
        m_shape.corners = regularCorners(corners);
}

QPointF QuickShapeEdit::boxHandle(Grip grip, int index) const {
    const Points box = boxCorners();
    if (grip == Grip::BoxCorner)
        return m_shape.toDocument(box[std::size_t(index)]);
    return m_shape.toDocument((box[std::size_t(index)] + box[std::size_t((index + 1) % 4)]) / 2.0);
}

QuickShapeEdit::Hit QuickShapeEdit::hitTest(const QPointF& doc, double docPerPixel) const {
    const double radius = kHandleRadius * docPerPixel;
    if (m_shape.hasOwnCorners())
        for (std::size_t i = 0; i < m_shape.corners.size(); ++i)
            if (length(m_shape.toDocument(m_shape.corners[i]) - doc) <= radius)
                return {Grip::ShapeCorner, int(i)};
    for (int i = 0; i < 4; ++i)
        if (length(boxHandle(Grip::BoxCorner, i) - doc) <= radius)
            return {Grip::BoxCorner, i};
    for (int i = 0; i < 4; ++i)
        if (length(boxHandle(Grip::BoxEdge, i) - doc) <= radius)
            return {Grip::BoxEdge, i};
    const QPointF flat = turned(doc - m_shape.centre, -m_shape.angle);
    const double dx = std::abs(flat.x()) - m_shape.width / 2.0, dy = std::abs(flat.y()) - m_shape.height / 2.0;
    if (dx <= 0.0 && dy <= 0.0)
        return {Grip::Inside, 0};
    if (std::hypot(std::max(dx, 0.0), std::max(dy, 0.0)) <= kTurnRing * docPerPixel)
        return {Grip::Turn, 0};
    return {};
}

void QuickShapeEdit::beginDrag(const Hit& hit, const QPointF& doc) {
    m_hit = hit;
    m_from = doc;
    m_start = m_shape;
}

void QuickShapeEdit::dragTo(const QPointF& doc, bool keepRatio) {
    switch (m_hit.grip) {
    case Grip::None: return;
    case Grip::Inside: m_shape.centre = m_start.centre + doc - m_from; return;
    case Grip::Turn: {
        const QPointF a = m_from - m_start.centre, b = doc - m_start.centre;
        m_shape.angle = m_start.angle + std::atan2(cross(a, b), dot(a, b));
        return;
    }
    case Grip::ShapeCorner:
        m_shape = m_start;
        m_shape.corners[std::size_t(m_hit.index)] = m_shape.toUnit(doc);
        fitBoxToCorners();
        return;
    case Grip::BoxCorner:
    case Grip::BoxEdge: break;
    }
    // Which box sides move: x and y of the handle, in box units doubled (-1, 0 or 1).
    const Points box = boxCorners();
    QPointF sides = box[std::size_t(m_hit.index)];
    if (m_hit.grip == Grip::BoxEdge)
        sides = (box[std::size_t(m_hit.index)] + box[std::size_t((m_hit.index + 1) % 4)]) / 2.0;
    sides *= 2.0;
    const QPointF anchor = m_start.toDocument(-sides / 2.0);
    const QPointF flat = turned(doc - anchor, -m_start.angle);
    double width = sides.x() != 0.0 ? std::max(kMinimumSize, flat.x() * sides.x()) : m_start.width;
    double height = sides.y() != 0.0 ? std::max(kMinimumSize, flat.y() * sides.y()) : m_start.height;
    if (m_start.keepsRatio() || keepRatio) {
        const double byWidth = sides.x() != 0.0 ? width / m_start.width : 0.0;
        const double byHeight = sides.y() != 0.0 ? height / m_start.height : 0.0;
        const double scale = std::max(byWidth, byHeight);
        width = m_start.width * scale;
        height = m_start.height * scale;
    }
    m_shape.width = width;
    m_shape.height = height;
    m_shape.centre = anchor + turned(QPointF(sides.x() * width / 2.0, sides.y() * height / 2.0), m_start.angle);
}

void QuickShapeEdit::fitBoxToCorners() {
    Points flat;
    Bounds b;
    for (const QPointF& unit : m_shape.corners) {
        flat.emplace_back(unit.x() * m_shape.width, unit.y() * m_shape.height);
        b.add(flat.back());
    }
    const QPointF middle = b.centre();
    const QPointF centre = m_shape.centre + turned(middle, m_shape.angle);
    const double width = std::max(kMinimumSize, b.width()), height = std::max(kMinimumSize, b.height());
    for (std::size_t i = 0; i < flat.size(); ++i)
        m_shape.corners[i] = {(flat[i].x() - middle.x()) / width, (flat[i].y() - middle.y()) / height};
    m_shape.centre = centre;
    m_shape.width = width;
    m_shape.height = height;
}

void QuickShapeEdit::beginHold(const QPointF& doc) {
    m_from = doc;
    m_start = m_shape;
}

void QuickShapeEdit::holdTo(const QPointF& doc) {
    const QPointF a = m_from - m_start.centre, b = doc - m_start.centre;
    if (length(a) <= 0.0 || length(b) <= 0.0)
        return;
    const double scale = length(b) / length(a);
    const double shortest = kMinimumSize / std::max(1.0, std::min(m_start.width, m_start.height));
    const double used = std::max(scale, shortest);
    m_shape.width = m_start.width * used;
    m_shape.height = m_start.height * used;
    if (m_shape.kind != QuickShapeKind::Circle)
        m_shape.angle = m_start.angle + std::atan2(cross(a, b), dot(a, b));
}

}
