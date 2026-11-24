#pragma once

#include <QPoint>
#include <QPointF>

#include <cmath>
#include <optional>

namespace doodlebold {

// Not saved; StrokeController forgets it when it paints into another document. Alt+click sets the source. The first stroke
// fixes the offset (start minus source); with Aligned later strokes keep it.
class CloneStamp {
public:
    bool hasSource() const { return m_source.has_value(); }
    std::optional<QPoint> source() const { return m_source; }
    std::optional<QPoint> offset() const { return m_offset; }

    void setSource(const QPointF& docPos);
    void reset();

    std::optional<QPoint> beginStroke(const QPointF& start, bool aligned);

    // At the pointer minus the offset when painting, or when Aligned with a fixed offset; else at the source pixel.
    std::optional<QPointF> crosshair(const QPointF& pointer, bool aligned, bool painting) const;

private:
    static QPoint pixelOf(const QPointF& p) { return QPoint(int(std::floor(p.x())), int(std::floor(p.y()))); }

    std::optional<QPoint> m_source;
    std::optional<QPoint> m_offset;
};

}
