#include "core/document/Document.h"
#include "core/paint/QuickShape.h"
#include "core/paint/StrokeController.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <numbers>

using namespace doodlebold;
using Kind = QuickShapeKind;

namespace {

constexpr double kPi = std::numbers::pi;
const QSize kCanvas(320, 320);

// The hand: a slow, small wander across the line.
QPointF trembled(const QPointF& p, double t, double amount) {
    return p + QPointF(amount * (std::sin(t * 23.0) + 0.5 * std::sin(t * 61.0 + 1.0)),
                       amount * (std::sin(t * 29.0 + 2.0) + 0.5 * std::sin(t * 53.0)));
}

std::vector<QPointF> ovalStroke(QPointF centre, double a, double b, double angleDeg = 0.0, double amount = 0.8, double sweep = 2.0 * kPi,
                                double direction = 1.0, double startAngle = -1.2) {
    const double angle = angleDeg * kPi / 180.0;
    std::vector<QPointF> out;
    constexpr int kCount = 140;
    for (int i = 0; i <= kCount; ++i) {
        const double t = double(i) / kCount;
        const double u = startAngle + direction * t * sweep;
        const QPointF flat(a * std::cos(u), b * std::sin(u));
        const QPointF p(flat.x() * std::cos(angle) - flat.y() * std::sin(angle), flat.x() * std::sin(angle) + flat.y() * std::cos(angle));
        out.push_back(trembled(centre + p, t, amount));
    }
    return out;
}

std::vector<QPointF> cornerStroke(const std::vector<QPointF>& corners, double amount = 0.7, double gap = 0.0) {
    std::vector<QPointF> out;
    constexpr int kPerSide = 30;
    const std::size_t n = corners.size();
    double t = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
        const QPointF a = corners[i], b = corners[(i + 1) % n];
        for (int s = 0; s < kPerSide; ++s, t += 1.0 / double(kPerSide * n))
            out.push_back(trembled(a + (b - a) * (double(s) / kPerSide), t, amount));
    }
    if (gap == 0.0)
        out.push_back(corners[0]);
    else
        out.resize(out.size() - std::size_t(gap * double(out.size())));
    return out;
}

std::vector<QPointF> regularCorners(int count, QPointF centre, double radius, double turnDeg = 0.0) {
    std::vector<QPointF> out;
    for (int i = 0; i < count; ++i) {
        const double a = (turnDeg - 90.0) * kPi / 180.0 + i * 2.0 * kPi / count;
        out.push_back(centre + QPointF(radius * std::cos(a), radius * std::sin(a)));
    }
    return out;
}

std::vector<QPointF> turnedBox(QPointF centre, double w, double h, double angleDeg) {
    const double a = angleDeg * kPi / 180.0;
    std::vector<QPointF> out;
    for (const auto& [x, y] : {std::pair{-w / 2, -h / 2}, {w / 2, -h / 2}, {w / 2, h / 2}, {-w / 2, h / 2}})
        out.push_back(centre + QPointF(x * std::cos(a) - y * std::sin(a), x * std::sin(a) + y * std::cos(a)));
    return out;
}

Kind kindOf(const std::vector<QPointF>& stroke) {
    const auto shape = recognizeShape(stroke);
    EXPECT_TRUE(shape.has_value());
    return shape ? shape->kind : Kind::Oval;
}

}

TEST(QuickShapeRecognition, ARoundStrokeIsACircle) {
    const auto shape = recognizeShape(ovalStroke({160, 160}, 80, 78));
    ASSERT_TRUE(shape);
    EXPECT_EQ(shape->kind, Kind::Circle);
    EXPECT_NEAR(shape->width, 158, 6);
    EXPECT_EQ(shape->width, shape->height);
    EXPECT_NEAR(shape->centre.x(), 160, 3);
    EXPECT_NEAR(shape->centre.y(), 160, 3);
}

TEST(QuickShapeRecognition, ALongRoundStrokeIsAnOvalUprightWhenNearlyUpright) {
    const auto shape = recognizeShape(ovalStroke({160, 160}, 110, 60, 5));
    ASSERT_TRUE(shape);
    EXPECT_EQ(shape->kind, Kind::Oval);
    EXPECT_NEAR(shape->width, 220, 8);
    EXPECT_NEAR(shape->height, 120, 8);
    EXPECT_EQ(shape->angle, 0.0);
}

TEST(QuickShapeRecognition, ATallOvalKeepsItsSidesWhenMadeUpright) {
    const auto shape = recognizeShape(ovalStroke({160, 160}, 60, 110));
    ASSERT_TRUE(shape);
    EXPECT_EQ(shape->kind, Kind::Oval);
    EXPECT_NEAR(shape->width, 120, 8);
    EXPECT_NEAR(shape->height, 220, 8);
}

TEST(QuickShapeRecognition, ATurnedOvalKeepsItsTurn) {
    const auto shape = recognizeShape(ovalStroke({160, 160}, 110, 60, 30));
    ASSERT_TRUE(shape);
    EXPECT_EQ(shape->kind, Kind::Oval);
    EXPECT_NEAR(shape->angle * 180.0 / kPi, 30, 6);
}

TEST(QuickShapeRecognition, ThreeCornersAreATriangleAtThoseCorners) {
    const std::vector<QPointF> corners{{160, 40}, {280, 260}, {50, 240}};
    const auto shape = recognizeShape(cornerStroke(corners));
    ASSERT_TRUE(shape);
    EXPECT_EQ(shape->kind, Kind::Triangle);
    ASSERT_EQ(shape->corners.size(), 3u);
    for (const QPointF& wanted : corners) {
        double best = 1e9;
        for (const QPointF& unit : shape->corners)
            best = std::min(best, QLineF(shape->toDocument(unit), wanted).length());
        EXPECT_LT(best, 6.0);
    }
}

TEST(QuickShapeRecognition, EqualSidesAreASquare) {
    EXPECT_EQ(kindOf(cornerStroke(turnedBox({160, 160}, 180, 174, 0))), Kind::Square);
}

TEST(QuickShapeRecognition, UnequalSidesAreARectangleAtTheirTurn) {
    const auto shape = recognizeShape(cornerStroke(turnedBox({160, 160}, 220, 110, 25)));
    ASSERT_TRUE(shape);
    EXPECT_EQ(shape->kind, Kind::Rectangle);
    EXPECT_NEAR(std::abs(shape->angle) * 180.0 / kPi, 25, 6);
    EXPECT_NEAR(std::max(shape->width, shape->height), 220, 8);
    EXPECT_NEAR(std::min(shape->width, shape->height), 110, 8);
}

TEST(QuickShapeRecognition, FiveToEightCornersAreAPolygonWithThoseCorners) {
    for (const int count : {5, 6, 7, 8}) {
        const auto shape = recognizeShape(cornerStroke(regularCorners(count, {160, 160}, 120), 0.5));
        ASSERT_TRUE(shape) << count;
        EXPECT_EQ(shape->kind, Kind::Polygon) << count;
        EXPECT_EQ(shape->corners.size(), std::size_t(count));
    }
}

TEST(QuickShapeRecognition, FourCornersWithoutRightAnglesAreAPolygon) {
    const auto shape = recognizeShape(cornerStroke({{110, 60}, {210, 60}, {290, 250}, {30, 250}}));
    ASSERT_TRUE(shape);
    EXPECT_EQ(shape->kind, Kind::Polygon);
    EXPECT_EQ(shape->corners.size(), 4u);
}

TEST(QuickShapeRecognition, AnOvershootOrASmallGapStillCloses) {
    EXPECT_EQ(kindOf(ovalStroke({160, 160}, 90, 88, 0, 0.8, 2.0 * kPi * 1.08)), Kind::Circle);
    EXPECT_EQ(kindOf(ovalStroke({160, 160}, 90, 88, 0, 0.8, 2.0 * kPi * 0.94)), Kind::Circle);
    EXPECT_EQ(kindOf(cornerStroke(turnedBox({160, 160}, 220, 110, 0), 0.7, 0.03)), Kind::Rectangle);
}

TEST(QuickShapeRecognition, AnyStartAndEitherDirectionGiveTheSameKind) {
    for (const double start : {0.0, 1.0, 2.5, 4.0})
        for (const double direction : {1.0, -1.0})
            EXPECT_EQ(kindOf(ovalStroke({160, 160}, 110, 60, 0, 0.8, 2.0 * kPi, direction, start)), Kind::Oval);
    std::vector<QPointF> corners = turnedBox({160, 160}, 220, 110, 0);
    std::ranges::reverse(corners);
    EXPECT_EQ(kindOf(cornerStroke(corners)), Kind::Rectangle);
}

TEST(QuickShapeRecognition, ShapesOfAnySizeAreSeen) {
    EXPECT_EQ(kindOf(ovalStroke({30, 30}, 14, 14, 0, 0.15)), Kind::Circle);
    EXPECT_EQ(kindOf(ovalStroke({1500, 1500}, 1400, 900, 0, 6.0)), Kind::Oval);
    EXPECT_EQ(kindOf(cornerStroke(turnedBox({900, 900}, 1600, 800, 0), 4.0)), Kind::Rectangle);
}

TEST(QuickShapeRecognition, WhatIsNoShapeStaysUnrecognized) {
    std::vector<QPointF> line, scribble, backAndForth, wave, spiral;
    for (int i = 0; i <= 100; ++i) {
        const double t = i / 100.0;
        line.emplace_back(20 + t * 280, 40 + t * 200);
        scribble.emplace_back(40 + t * 240 + 22 * std::sin(t * 31), 165 + 60 * std::sin(t * 13) + 34 * std::sin(t * 29));
        backAndForth.emplace_back(160 + 120 * std::sin(t * 2.0 * kPi), 160 + 0.5 * std::sin(t * 40));
        wave.emplace_back(20 + t * 280, 160 + 80 * std::sin(t * 4.0 * kPi));
        spiral.emplace_back(160 + (40 + t * 100) * std::cos(t * 6.0 * kPi), 160 + (40 + t * 100) * std::sin(t * 6.0 * kPi));
    }
    EXPECT_FALSE(recognizeShape(line));
    EXPECT_FALSE(recognizeShape(scribble));
    EXPECT_FALSE(recognizeShape(backAndForth));
    EXPECT_FALSE(recognizeShape(wave));
    EXPECT_FALSE(recognizeShape(spiral));
    EXPECT_FALSE(recognizeShape(ovalStroke({160, 160}, 100, 80, 0, 0.8, kPi)));  // half an oval
    EXPECT_FALSE(recognizeShape({{10, 10}, {11, 10}}));
    EXPECT_FALSE(recognizeShape(ovalStroke({30, 30}, 3, 3, 0, 0.05)));  // a dot's worth of movement
}

TEST(QuickShapeOutline, IsClosedAndStartsWhereTheStrokeDid) {
    const std::vector<QPointF> stroke = ovalStroke({160, 160}, 110, 60);
    const auto oval = recognizeShape(stroke);
    ASSERT_TRUE(oval);
    const std::vector<QPointF> line = shapeOutline(*oval);
    ASSERT_GT(line.size(), 16u);
    EXPECT_LT(QLineF(line.front(), line.back()).length(), 0.01);
    EXPECT_LT(QLineF(line.front(), stroke.front()).length(), 25.0);

    const std::vector<QPointF> corners = turnedBox({160, 160}, 220, 110, 0);
    const auto box = recognizeShape(cornerStroke({corners[2], corners[3], corners[0], corners[1]}));
    ASSERT_TRUE(box);
    const std::vector<QPointF> boxLine = shapeOutline(*box);
    EXPECT_LT(QLineF(boxLine.front(), corners[2]).length(), 8.0);
    EXPECT_LT(QLineF(boxLine.front(), boxLine.back()).length(), 0.01);
}

TEST(QuickShapeOutline, RunsTheWayTheStrokeRan) {
    for (const double direction : {1.0, -1.0}) {
        const std::vector<QPointF> stroke = ovalStroke({160, 160}, 110, 60, 0, 0.8, 2.0 * kPi, direction);
        const auto shape = recognizeShape(stroke);
        ASSERT_TRUE(shape);
        const std::vector<QPointF> line = shapeOutline(*shape);
        const QPointF a = line[0] - QPointF(160, 160), b = line[line.size() / 8] - QPointF(160, 160);
        const double drawnWay = a.x() * b.y() - a.y() * b.x();
        EXPECT_EQ(drawnWay > 0.0, direction > 0.0);
    }
    std::vector<QPointF> corners = regularCorners(3, {160, 160}, 120);
    std::ranges::reverse(corners);
    const auto triangle = recognizeShape(cornerStroke(corners));
    ASSERT_TRUE(triangle);
    const std::vector<QPointF> line = shapeOutline(*triangle);
    EXPECT_LT(QLineF(line.front(), corners[0]).length(), 10.0);
    EXPECT_LT(QLineF(line[line.size() / 3], corners[1]).length(), 12.0);
}

TEST(QuickShapeSamples, CarryThePressureOfTheSamePartOfTheStroke) {
    std::vector<InputSample> stroke;
    const std::vector<QPointF> line = ovalStroke({160, 160}, 110, 60);
    for (std::size_t i = 0; i < line.size(); ++i) {
        InputSample s;
        s.pos = line[i];
        s.pressure = 0.2 + 0.7 * double(i) / double(line.size() - 1);
        s.kind = PointerKind::Pen;
        s.tiltX = 30.0;
        stroke.push_back(s);
    }
    std::vector<QPointF> points;
    points.reserve(stroke.size());
    for (const InputSample& s : stroke)
        points.push_back(s.pos);
    const auto shape = recognizeShape(points);
    ASSERT_TRUE(shape);
    const std::vector<InputSample> samples = shapeSamples(*shape, stroke);
    ASSERT_GT(samples.size(), 16u);
    EXPECT_NEAR(samples.front().pressure, 0.2, 0.02);
    EXPECT_NEAR(samples.back().pressure, 0.9, 0.02);
    EXPECT_NEAR(samples[samples.size() / 2].pressure, 0.55, 0.05);
    EXPECT_EQ(samples.front().kind, PointerKind::Pen);
    EXPECT_NEAR(samples[10].tiltX, 30.0, 0.01);
    for (std::size_t i = 1; i < samples.size(); ++i)
        EXPECT_GE(samples[i].pressure, samples[i - 1].pressure - 1e-9);
}

TEST(QuickShapeSamples, AHeldStrokeWithoutMovementStillGivesAFullLine) {
    std::vector<InputSample> stroke(5);
    for (InputSample& s : stroke)
        s.pos = QPointF(10, 10);
    QuickShape shape;
    shape.centre = {100, 100};
    shape.width = shape.height = 60;
    EXPECT_GT(shapeSamples(shape, stroke).size(), 16u);
}

namespace {

QuickShape box(Kind kind, double w = 100, double h = 60) {
    QuickShape shape;
    shape.centre = {160, 160};
    shape.width = w;
    shape.height = h;
    return convertShape(shape, kind, 5);
}

constexpr double kOnePixel = 1.0;

}

TEST(QuickShapeEdit, ChangingKindKeepsTheBox) {
    QuickShapeEdit edit(box(Kind::Rectangle));
    edit.setKind(Kind::Oval);
    EXPECT_EQ(edit.shape().width, 100);
    EXPECT_EQ(edit.shape().height, 60);
    edit.setKind(Kind::Triangle);
    EXPECT_EQ(edit.shape().corners.size(), 3u);
    edit.setKind(Kind::Polygon);
    EXPECT_EQ(edit.cornerCount(), 5);
    edit.setCornerCount(8);
    EXPECT_EQ(edit.cornerCount(), 8);
    edit.setCornerCount(QuickShapeEdit::kMaximumCorners + 1);
    EXPECT_EQ(edit.cornerCount(), 8);
    edit.setCornerCount(QuickShapeEdit::kMinimumCorners - 1);
    EXPECT_EQ(edit.cornerCount(), 8);
    edit.setKind(Kind::Circle);
    EXPECT_EQ(edit.shape().width, edit.shape().height);
    edit.setKind(Kind::Square);
    EXPECT_EQ(edit.shape().width, edit.shape().height);
}

TEST(QuickShapeEdit, HitTestFindsHandlesInsideAndTheTurnRing) {
    using Grip = QuickShapeEdit::Grip;
    const QuickShapeEdit edit(box(Kind::Rectangle));
    EXPECT_EQ(edit.hitTest({110, 130}, kOnePixel).grip, Grip::BoxCorner);
    EXPECT_EQ(edit.hitTest({110, 130}, kOnePixel).index, 0);
    EXPECT_EQ(edit.hitTest({210, 190}, kOnePixel).index, 2);
    EXPECT_EQ(edit.hitTest({210, 160}, kOnePixel).grip, Grip::BoxEdge);
    EXPECT_EQ(edit.hitTest({210, 160}, kOnePixel).index, 1);
    EXPECT_EQ(edit.hitTest({160, 160}, kOnePixel).grip, Grip::Inside);
    EXPECT_EQ(edit.hitTest({230, 150}, kOnePixel).grip, Grip::Turn);
    EXPECT_EQ(edit.hitTest({260, 150}, kOnePixel).grip, Grip::None);
    // The handles and the ring are screen pixels: at 400 % the same place is further away.
    EXPECT_EQ(edit.hitTest({230, 150}, 0.25).grip, Grip::None);
}

TEST(QuickShapeEdit, DraggingInsideMovesAndTheOutsideTurns) {
    QuickShapeEdit edit(box(Kind::Rectangle));
    edit.beginDrag({QuickShapeEdit::Grip::Inside, 0}, {160, 160});
    edit.dragTo({180, 150}, false);
    EXPECT_EQ(edit.shape().centre, QPointF(180, 150));
    edit.endDrag();
    EXPECT_FALSE(edit.isDragging());

    QuickShapeEdit turn(box(Kind::Rectangle));
    turn.beginDrag({QuickShapeEdit::Grip::Turn, 0}, {230, 160});
    turn.dragTo({160, 230}, false);
    EXPECT_NEAR(turn.shape().angle, kPi / 2.0, 1e-9);
    EXPECT_EQ(turn.shape().width, 100);
}

TEST(QuickShapeEdit, ACornerResizesAboutTheOppositeOne) {
    QuickShapeEdit edit(box(Kind::Rectangle));
    const QPointF anchor = edit.boxHandle(QuickShapeEdit::Grip::BoxCorner, 0);
    edit.beginDrag({QuickShapeEdit::Grip::BoxCorner, 2}, {210, 190});
    edit.dragTo({260, 200}, false);
    EXPECT_NEAR(edit.shape().width, 150, 1e-9);
    EXPECT_NEAR(edit.shape().height, 70, 1e-9);
    EXPECT_LT(QLineF(edit.boxHandle(QuickShapeEdit::Grip::BoxCorner, 0), anchor).length(), 1e-9);
}

TEST(QuickShapeEdit, AnEdgeStretchesOneWayAndATurnedBoxResizesInItsOwnFrame) {
    QuickShapeEdit edit(box(Kind::Rectangle));
    edit.beginDrag({QuickShapeEdit::Grip::BoxEdge, 1}, {210, 160});
    edit.dragTo({240, 170}, false);
    EXPECT_NEAR(edit.shape().width, 130, 1e-9);
    EXPECT_NEAR(edit.shape().height, 60, 1e-9);

    QuickShape turnedShape = box(Kind::Rectangle);
    turnedShape.angle = kPi / 2.0;
    QuickShapeEdit turned(turnedShape);
    const QPointF anchor = turned.boxHandle(QuickShapeEdit::Grip::BoxCorner, 0);
    const QPointF handle = turned.boxHandle(QuickShapeEdit::Grip::BoxCorner, 2);
    turned.beginDrag({QuickShapeEdit::Grip::BoxCorner, 2}, handle);
    turned.dragTo(handle + QPointF(0, 40), false);
    EXPECT_NEAR(turned.shape().width, 140, 1e-9);
    EXPECT_NEAR(turned.shape().height, 60, 1e-9);
    EXPECT_LT(QLineF(turned.boxHandle(QuickShapeEdit::Grip::BoxCorner, 0), anchor).length(), 1e-9);
}

TEST(QuickShapeEdit, SquareAndCircleKeepTheirRatioAndShiftKeepsAnyRatio) {
    for (const Kind kind : {Kind::Square, Kind::Circle}) {
        QuickShapeEdit edit(box(kind, 80, 80));
        edit.beginDrag({QuickShapeEdit::Grip::BoxCorner, 2}, edit.boxHandle(QuickShapeEdit::Grip::BoxCorner, 2));
        edit.dragTo({260, 210}, false);
        EXPECT_NEAR(edit.shape().width, edit.shape().height, 1e-9);
        edit.endDrag();
        edit.beginDrag({QuickShapeEdit::Grip::BoxEdge, 1}, edit.boxHandle(QuickShapeEdit::Grip::BoxEdge, 1));
        edit.dragTo(edit.boxHandle(QuickShapeEdit::Grip::BoxEdge, 1) + QPointF(20, 0), false);
        EXPECT_NEAR(edit.shape().width, edit.shape().height, 1e-9);
    }
    QuickShapeEdit edit(box(Kind::Rectangle));
    edit.beginDrag({QuickShapeEdit::Grip::BoxCorner, 2}, {210, 190});
    edit.dragTo({310, 190}, true);
    EXPECT_NEAR(edit.shape().width / edit.shape().height, 100.0 / 60.0, 1e-9);
}

TEST(QuickShapeEdit, ACornerDotMovesOnlyThatCorner) {
    QuickShapeEdit edit(box(Kind::Triangle));
    const std::vector<QPointF> units = edit.shape().corners;
    const QPointF other1 = edit.shape().toDocument(units[1]), other2 = edit.shape().toDocument(units[2]);
    const QPointF top = edit.shape().toDocument(units[0]);
    EXPECT_EQ(edit.hitTest(top, kOnePixel).grip, QuickShapeEdit::Grip::ShapeCorner);
    edit.beginDrag(edit.hitTest(top, kOnePixel), top);
    edit.dragTo(top + QPointF(90, -40), false);
    const QuickShape& s = edit.shape();
    EXPECT_LT(QLineF(s.toDocument(s.corners[0]), top + QPointF(90, -40)).length(), 1e-6);
    EXPECT_LT(QLineF(s.toDocument(s.corners[1]), other1).length(), 1e-6);
    EXPECT_LT(QLineF(s.toDocument(s.corners[2]), other2).length(), 1e-6);
}

TEST(QuickShapeEdit, HoldingAndMovingResizesAndTurnsAboutTheCentre) {
    QuickShapeEdit edit(box(Kind::Rectangle));
    edit.beginHold({210, 160});
    edit.holdTo({160, 260});
    EXPECT_NEAR(edit.shape().width, 200, 1e-9);
    EXPECT_NEAR(edit.shape().height, 120, 1e-9);
    EXPECT_NEAR(edit.shape().angle, kPi / 2.0, 1e-9);
    EXPECT_EQ(edit.shape().centre, QPointF(160, 160));
    edit.holdTo({160, 160});  // on the centre: nothing to measure by
    EXPECT_NEAR(edit.shape().width, 200, 1e-9);

    QuickShapeEdit circle(box(Kind::Circle, 80, 80));
    circle.beginHold({200, 160});
    circle.holdTo({160, 240});
    EXPECT_EQ(circle.shape().angle, 0.0);
    EXPECT_NEAR(circle.shape().width, 160, 1e-9);
}

// Repainting through StrokeController.

namespace {

InputSample sampleAt(const QPointF& at, double pressure = 1.0, PointerKind kind = PointerKind::Mouse) {
    InputSample s;
    s.pos = at;
    s.pressure = pressure;
    s.kind = kind;
    return s;
}

const QColor kInk(200, 30, 60);

BrushSettings brushOf(ToolType tool) {
    BrushSettings s = defaultSettings(tool);
    s.size = 120;
    return s;
}

std::vector<InputSample> samplesOf(const std::vector<QPointF>& points) {
    std::vector<InputSample> out;
    out.reserve(points.size());
    for (const QPointF& p : points)
        out.push_back(sampleAt(p));
    return out;
}

void drawnStroke(StrokeController& stroke, const std::vector<InputSample>& samples, const BrushSettings& settings) {
    ASSERT_TRUE(stroke.begin(samples.front(), kInk, settings));
    for (std::size_t i = 1; i < samples.size(); ++i)
        stroke.add(samples[i]);
}

QImage picture(const Document& doc) {
    return doc.currentLayer()->pixels.toImage(doc.rect());
}

const std::vector<QPointF> kCircle = ovalStroke({160, 160}, 90, 88);

}

class QuickShapeRepaint : public ::testing::TestWithParam<ToolType> {};

TEST_P(QuickShapeRepaint, TheShapeIsExactlyAStrokeOfTheSameBrushAlongItsLine) {
    const std::vector<InputSample> drawn = samplesOf(kCircle);
    const auto shape = recognizeShape(kCircle);
    ASSERT_TRUE(shape);
    const std::vector<InputSample> path = shapeSamples(*shape, drawn);

    Document direct(kCanvas, Qt::white);
    {
        BrushSettings straight = brushOf(GetParam());
        straight.stabilizer = StabilizerLevel{0};
        StrokeController stroke(&direct);
        ASSERT_TRUE(stroke.begin(path.front(), kInk, straight));
        for (std::size_t i = 1; i < path.size(); ++i)
            stroke.add(path[i]);
        stroke.finish();
    }

    Document snapped(kCanvas, Qt::white);
    StrokeController stroke(&snapped);
    drawnStroke(stroke, drawn, brushOf(GetParam()));
    stroke.repaintAlong(path);
    stroke.repaintAlong(path);  // a repaint leaves nothing of the last one behind
    stroke.finish();
    EXPECT_EQ(picture(snapped), picture(direct));
    EXPECT_EQ(snapped.undoStack()->count(), 1);
}

TEST_P(QuickShapeRepaint, CancellingGivesBackTheStrokeAsDrawnInOneStep) {
    const std::vector<InputSample> drawn = samplesOf(kCircle);
    Document plain(kCanvas, Qt::white);
    {
        StrokeController stroke(&plain);
        drawnStroke(stroke, drawn, brushOf(GetParam()));
        stroke.finish();
    }
    Document snapped(kCanvas, Qt::white);
    const QImage empty = picture(snapped);
    StrokeController stroke(&snapped);
    drawnStroke(stroke, drawn, brushOf(GetParam()));
    QuickShape big = *recognizeShape(kCircle);
    big.width = big.height = 280;  // reaches tiles the stroke did not
    stroke.repaintAlong(shapeSamples(big, drawn));
    EXPECT_NE(picture(snapped), picture(plain));
    stroke.finishAsDrawn();
    EXPECT_FALSE(stroke.isActive());
    EXPECT_FALSE(snapped.isPainting());
    EXPECT_EQ(picture(snapped), picture(plain));
    EXPECT_EQ(snapped.undoStack()->count(), 1);
    snapped.undoStack()->undo();
    EXPECT_EQ(picture(snapped), empty);
}

INSTANTIATE_TEST_SUITE_P(LineTools, QuickShapeRepaint,
                         ::testing::Values(ToolType::Pencil, ToolType::Brush, ToolType::WaterColor, ToolType::Marker,
                                           ToolType::PixelArtPen),
                         [](const auto& test) { return toolTypeKey(test.param).toStdString(); });

TEST(QuickShapeStroke, TheWholeResultIsOneUndoStep) {
    const std::vector<InputSample> drawn = samplesOf(kCircle);
    Document doc(kCanvas, Qt::white);
    const QImage empty = picture(doc);
    StrokeController stroke(&doc);
    drawnStroke(stroke, drawn, brushOf(ToolType::Pencil));
    QuickShape shape = *recognizeShape(kCircle);
    stroke.repaintAlong(shapeSamples(shape, drawn));
    shape.width = shape.height = 300;
    stroke.repaintAlong(shapeSamples(shape, drawn));
    shape.width = shape.height = 100;
    stroke.repaintAlong(shapeSamples(shape, drawn));
    EXPECT_TRUE(doc.isPainting());
    stroke.finish();
    const QImage done = picture(doc);
    EXPECT_NE(done, empty);
    EXPECT_EQ(doc.undoStack()->count(), 1);
    doc.undoStack()->undo();
    EXPECT_EQ(picture(doc), empty);  // also on the tiles only the 300 px repaint reached
    doc.undoStack()->redo();
    EXPECT_EQ(picture(doc), done);
}

TEST(QuickShapeStroke, AnAbandonedRepaintedStrokeLeavesNothing) {
    Document doc(kCanvas, Qt::white);
    const QImage empty = picture(doc);
    const std::vector<InputSample> drawn = samplesOf(kCircle);
    StrokeController stroke(&doc);
    drawnStroke(stroke, drawn, brushOf(ToolType::Pencil));
    stroke.repaintAlong(shapeSamples(*recognizeShape(kCircle), drawn));
    stroke.cancel();
    EXPECT_EQ(picture(doc), empty);
    EXPECT_EQ(doc.undoStack()->count(), 0);
}

TEST(QuickShapeStroke, RepaintsOverExistingPaintDoNotPileUp) {
    const std::vector<InputSample> drawn = samplesOf(kCircle);
    const std::vector<InputSample> path = shapeSamples(*recognizeShape(kCircle), drawn);
    BrushSettings straight = brushOf(ToolType::Brush);
    straight.stabilizer = StabilizerLevel{0};

    const auto paintBase = [](Document& d) {
        d.paintTarget(d.currentLayerId()).value()->fill(QRect(60, 60, 200, 200), QColor(30, 120, 200));
    };
    Document direct(kCanvas, Qt::white);
    paintBase(direct);
    {
        StrokeController stroke(&direct);
        ASSERT_TRUE(stroke.begin(path.front(), kInk, straight));
        for (std::size_t i = 1; i < path.size(); ++i)
            stroke.add(path[i]);
        stroke.finish();
    }
    Document snapped(kCanvas, Qt::white);
    paintBase(snapped);
    StrokeController stroke(&snapped);
    drawnStroke(stroke, drawn, brushOf(ToolType::Brush));
    for (int i = 0; i < 3; ++i)
        stroke.repaintAlong(path);
    stroke.finish();
    EXPECT_EQ(picture(snapped), picture(direct));
}

TEST(QuickShapeStroke, TheEraserAndAMaskRepaintToo) {
    const std::vector<InputSample> drawn = samplesOf(kCircle);
    const std::vector<InputSample> path = shapeSamples(*recognizeShape(kCircle), drawn);

    Document erased(kCanvas, Qt::white);
    erased.paintTarget(erased.currentLayerId()).value()->fill(erased.rect(), QColor(10, 10, 10));
    const QImage full = picture(erased);
    StrokeController eraser(&erased);
    drawnStroke(eraser, drawn, brushOf(ToolType::Eraser));
    eraser.repaintAlong(path);
    eraser.finish();
    EXPECT_NE(picture(erased), full);
    erased.undoStack()->undo();
    EXPECT_EQ(picture(erased), full);

    Document masked(kCanvas, Qt::white);
    const LayerId id = masked.currentLayerId();
    ASSERT_TRUE(masked.addLayerMask(id));
    masked.setMaskTarget(true);
    const SelectionMask before = *masked.layer(id)->mask;
    StrokeController stroke(&masked);
    ASSERT_TRUE(stroke.begin(drawn.front(), QColor(0, 0, 0), brushOf(ToolType::Pencil)));
    for (std::size_t i = 1; i < drawn.size(); ++i)
        stroke.add(drawn[i]);
    QuickShape big = *recognizeShape(kCircle);
    big.width = big.height = 280;
    stroke.repaintAlong(shapeSamples(big, drawn));
    stroke.repaintAlong(path);
    stroke.finish();
    const SelectionMask after = *masked.layer(id)->mask;
    EXPECT_FALSE(after == before);
    // Nothing of the larger repaint stays on the mask.
    EXPECT_EQ(after.at(160 + 138, 160), before.at(160 + 138, 160));
    masked.undoStack()->undo();
    EXPECT_TRUE(*masked.layer(id)->mask == before);
}

TEST(QuickShapeStroke, FinishingAsDrawnOnAMaskPutsBackTheStroke) {
    const std::vector<InputSample> drawn = samplesOf(kCircle);
    const auto paint = [&](Document& doc, bool repaint) {
        const LayerId id = doc.currentLayerId();
        ASSERT_TRUE(doc.addLayerMask(id));
        doc.setMaskTarget(true);
        StrokeController stroke(&doc);
        ASSERT_TRUE(stroke.begin(drawn.front(), QColor(0, 0, 0), brushOf(ToolType::Pencil)));
        for (std::size_t i = 1; i < drawn.size(); ++i)
            stroke.add(drawn[i]);
        if (repaint) {
            QuickShape big = *recognizeShape(kCircle);
            big.width = big.height = 280;
            stroke.repaintAlong(shapeSamples(big, drawn));
            stroke.finishAsDrawn();
        } else {
            stroke.finish();
        }
    };
    Document plain(kCanvas, Qt::white), snapped(kCanvas, Qt::white);
    paint(plain, false);
    paint(snapped, true);
    EXPECT_TRUE(*snapped.layer(snapped.currentLayerId())->mask == *plain.layer(plain.currentLayerId())->mask);
}
