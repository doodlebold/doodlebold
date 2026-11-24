#pragma once

#include "core/input/InputSample.h"

#include <QPointF>

#include <optional>
#include <utility>
#include <vector>

namespace doodlebold {

enum class QuickShapeKind {
    Triangle,
    Square,
    Rectangle,
    Circle,
    Oval,
    Polygon
};

// A clean shape in its own box. Triangle and Polygon keep their corners in box units (-0.5 … 0.5); the others fill the box.
struct QuickShape {
    QuickShapeKind kind = QuickShapeKind::Oval;
    QPointF centre;
    double width = 0.0;
    double height = 0.0;
    double angle = 0.0;  // radians
    std::vector<QPointF> corners;
    // The line starts where the stroke began and runs the same way round, so the stroke's pressure stays where it was.
    QPointF start{0.0, -0.5};  // box units
    bool reversed = false;

    QPointF toDocument(const QPointF& unit) const;
    QPointF toUnit(const QPointF& doc) const;
    bool hasOwnCorners() const { return kind == QuickShapeKind::Triangle || kind == QuickShapeKind::Polygon; }
    bool keepsRatio() const { return kind == QuickShapeKind::Square || kind == QuickShapeKind::Circle; }
};

// The shape a closed stroke looks like; none for anything else.
std::optional<QuickShape> recognizeShape(const std::vector<QPointF>& stroke);

// The closed line of a shape in document coordinates, from its start round to its start.
std::vector<QPointF> shapeOutline(const QuickShape& shape);

// The shape's line as pen samples, each with the pressure and tilt of the stroke at the same part of its length.
std::vector<InputSample> shapeSamples(const QuickShape& shape, const std::vector<InputSample>& stroke);

// The same box as another kind. A regular polygon has `polygonCorners` corners.
QuickShape convertShape(const QuickShape& shape, QuickShapeKind kind, int polygonCorners);

// Moves a shape by its handles. Dragging inside moves it, a box handle resizes it about the opposite one (Square and Circle, or
// with keepRatio, keep their ratio), a corner dot moves that corner alone, and dragging the ring outside the box turns it.
class QuickShapeEdit {
public:
    enum class Grip {
        None,
        Inside,
        Turn,
        BoxCorner,
        BoxEdge,
        ShapeCorner
    };
    struct Hit {
        Grip grip = Grip::None;
        int index = 0;  // box corners from the top left and box edges from the top, clockwise; or the shape's corner
        bool operator==(const Hit&) const = default;
    };

    static constexpr double kHandleRadius = 9.0;  // screen pixels
    static constexpr double kTurnRing = 32.0;     // screen pixels: past this a press belongs to the next stroke
    static constexpr double kMinimumSize = 2.0;
    static constexpr int kMinimumCorners = 5;     // of a regular polygon
    static constexpr int kMaximumCorners = 12;

    explicit QuickShapeEdit(QuickShape shape) : m_shape(std::move(shape)) {}

    const QuickShape& shape() const { return m_shape; }
    void setKind(QuickShapeKind kind);
    int cornerCount() const;
    void setCornerCount(int corners);

    Hit hitTest(const QPointF& doc, double docPerPixel) const;
    QPointF boxHandle(Grip grip, int index) const;

    void beginDrag(const Hit& hit, const QPointF& doc);
    void dragTo(const QPointF& doc, bool keepRatio);
    void endDrag() { m_hit = {}; }
    bool isDragging() const { return m_hit.grip != Grip::None; }

    // The pen still down after the snap: moving it resizes and turns the shape about its centre.
    void beginHold(const QPointF& doc);
    void holdTo(const QPointF& doc);

private:
    void fitBoxToCorners();

    QuickShape m_shape;
    QuickShape m_start;
    Hit m_hit;
    QPointF m_from;
};

}
