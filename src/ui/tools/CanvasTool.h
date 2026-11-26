#pragma once

#include "core/input/InputSample.h"
#include "core/selection/SelectionMask.h"
#include "core/selection/SelectionOps.h"
#include "core/stroke/PressureCurve.h"

#include <QPointF>
#include <QRect>
#include <QString>
#include <QTransform>
#include <Qt>

#include <optional>

class QKeyEvent;
class QPainter;

namespace doodlebold {

class Document;
class ToolBox;
class TransformSession;

struct ToolPointer {
    QPointF doc;     // document position; pixel (x, y) spans [x, x + 1) × [y, y + 1)
    QPointF widget;
    InputSample sample;
    Qt::KeyboardModifiers modifiers;

    QPoint pixel() const;
};

class ToolHost {
public:
    virtual ~ToolHost() = default;
    virtual Document* toolDocument() = 0;
    virtual ToolBox* toolBox() = 0;
    virtual double docPerPixel() const = 0;
    virtual void toolChanged() = 0;
    virtual void toolMessage(const QString& text) = 0;
    // A Move session starts on the current layer (its selected part); false when that is refused (the reason has been
    // reported).
    virtual TransformSession* session() = 0;
    virtual bool startMoveSession() = 0;
    virtual void finishSession(bool commit) = 0;
    // The part of the pen's pressure strokes use (Options' "Ignore pressure below" and "Full pressure at"), for the selection pens'
    // strokes as for painting's.
    virtual PressureRange pressureRange() const { return {}; }
    // Whether this is the canvas the user works in: every open canvas's tools hear the one shared ToolBox.
    virtual bool isCurrentCanvas() const { return true; }
};

class CanvasTool {
public:
    explicit CanvasTool(ToolHost& host) : m_host(host) {}
    virtual ~CanvasTool() = default;
    CanvasTool(const CanvasTool&) = delete;
    CanvasTool& operator=(const CanvasTool&) = delete;

    virtual void press(const ToolPointer& p) = 0;
    virtual void move(const ToolPointer& p) = 0;
    virtual void release(const ToolPointer& p) = 0;
    virtual void doubleClick(const ToolPointer&) {}
    virtual bool keyPress(QKeyEvent*) { return false; }
    // The tool takes this key before the window's shortcuts and the canvas's own keys (the Text tool while typing).
    virtual bool wantsKey(const QKeyEvent*) const { return false; }
    virtual bool isActive() const = 0;
    virtual void cancel() {}
    virtual void paintOverlay(QPainter&, const QTransform& /*docToWidget*/) {}
    virtual const SelectionMask* liveSelection() const { return nullptr; }
    // nullopt: unknown, the view redraws all of it.
    virtual std::optional<QRect> takeLiveSelectionChange() { return std::nullopt; }
    virtual Qt::CursorShape cursor(const QPointF& /*doc*/) const { return Qt::CrossCursor; }

protected:
    ToolHost& host() const { return m_host; }

private:
    ToolHost& m_host;
};

// The modifier rules for the selection tools: none = replace, Shift = add, Alt = subtract, Alt+Shift = intersect.
selection::CombineMode combineModeFor(Qt::KeyboardModifiers mods);

}
