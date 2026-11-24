#include "core/paint/CloneStamp.h"

namespace doodlebold {

void CloneStamp::setSource(const QPointF& docPos) {
    m_source = pixelOf(docPos);
    m_offset.reset();
}

void CloneStamp::reset() {
    m_source.reset();
    m_offset.reset();
}

std::optional<QPoint> CloneStamp::beginStroke(const QPointF& start, bool aligned) {
    if (!m_source)
        return std::nullopt;
    if (!aligned || !m_offset)
        m_offset = pixelOf(start) - *m_source;
    return m_offset;
}

std::optional<QPointF> CloneStamp::crosshair(const QPointF& pointer, bool aligned, bool painting) const {
    if (!m_source)
        return std::nullopt;
    if (m_offset && (painting || aligned))
        return pointer - QPointF(*m_offset);
    return QPointF(*m_source) + QPointF(0.5, 0.5);
}

}
