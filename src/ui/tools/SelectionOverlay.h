#pragma once

#include "core/selection/SelectionMask.h"

#include <QImage>
#include <QPainterPath>
#include <QRect>
#include <QRegion>
#include <QTransform>

class QPainter;

namespace doodlebold {

// A white line with black dashes of 4 px on / 4 px off, moved along by `phase` pixels.
void paintAnts(QPainter& painter, const QPainterPath& path, int phase);
inline constexpr int kAntPeriod = 8;

// Drawn at screen resolution through the view transform, so it follows zoom, rotation and flip without being
// recomputed.
class SelectionOverlay {
public:
    void invalidate() { m_dirty = true; }
    void paint(QPainter& painter, const QTransform& docToWidget, int phase, const SelectionMask* mask);
    bool hasOutline() const { return !m_outline.isEmpty(); }

private:
    QPainterPath m_outline;
    bool m_dirty = true;
};

// Shown instead of the ants while the Selection Pen or Eraser is active. The canvas keeps it in a texture of document
// size and uploads only what changed: rebuilding and uploading the whole tint for every pen event made it lag.
class SelectionTint {
public:
    void reset(const QRect& document);
    void invalidate() { m_all = true; }
    void invalidate(const QRect& docRect);
    QRegion takeDirty(const SelectionMask* mask);
    // The tint of `rect`, premultiplied RGBA8888 as a GL_RGBA upload reads it.
    static QImage pixels(const SelectionMask* mask, const QRect& rect);
    // The same in the given tint, for threads other than the window's, which can't ask the theme.
    static QImage pixels(const SelectionMask* mask, const QRect& rect, const QColor& tint);

private:
    QRegion m_dirty;
    QRect m_extent;
    bool m_all = true;
};

}
