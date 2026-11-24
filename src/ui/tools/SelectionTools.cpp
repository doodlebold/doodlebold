#include "ui/tools/SelectionTools.h"

#include "core/document/Document.h"
#include "core/document/TransformSession.h"
#include "core/selection/MagicWand.h"
#include "core/tools/ToolBox.h"
#include "ui/tools/SelectionOverlay.h"

#include <QCoreApplication>
#include <QKeyEvent>
#include <QPainter>

#include <algorithm>
#include <cmath>

namespace doodlebold {

namespace {

QString tr(const char* text) {
    return QCoreApplication::translate("SelectionTools", text);
}

QPointF pixelCentre(const QPoint& p) {
    return QPointF(p.x() + 0.5, p.y() + 0.5);
}

void report(ToolHost& host, const EditResult<>& r) {
    if (!r && r.error() != EditError::NoChange)
        host.toolMessage(describe(r.error()));
}

bool isMoveGesture(Qt::KeyboardModifiers mods) {
    return (mods & Qt::ControlModifier) && (mods & Qt::ShiftModifier) && !(mods & Qt::AltModifier);
}

}

QPoint ToolPointer::pixel() const {
    return QPoint(int(std::floor(doc.x())), int(std::floor(doc.y())));
}

selection::CombineMode combineModeFor(Qt::KeyboardModifiers mods) {
    const bool shift = mods & Qt::ShiftModifier, alt = mods & Qt::AltModifier;
    if (shift && alt)
        return selection::CombineMode::Intersect;
    if (shift)
        return selection::CombineMode::Add;
    if (alt)
        return selection::CombineMode::Subtract;
    return selection::CombineMode::Replace;
}

void MarqueeTool::press(const ToolPointer& p) {
    m_mods = p.modifiers;
    Document* doc = host().toolDocument();
    const MarqueeOptions& options = host().toolBox()->fixedOptions().marquee;
    const bool inside = doc->selection() && doc->selection()->at(p.pixel().x(), p.pixel().y()) > 0;
    const bool plain = !(p.modifiers & (Qt::ShiftModifier | Qt::AltModifier | Qt::ControlModifier));
    if (isMoveGesture(p.modifiers) || (options.cutAndMove && plain && inside)) {
        if (host().startMoveSession()) {
            host().session()->beginDrag(TransformSession::Handle::Inside, p.doc);
            m_state = State::Moving;
        }
        return;
    }
    m_state = State::Selecting;
    m_anchor = m_current = p.pixel();
    m_dragged = false;
    host().toolChanged();
}

void MarqueeTool::move(const ToolPointer& p) {
    if (m_state == State::Selecting) {
        m_current = p.pixel();
        m_dragged = m_dragged || m_current != m_anchor;
    } else if (m_state == State::Moving) {
        host().session()->dragTo(p.doc, p.modifiers);
    }
    host().toolChanged();
}

void MarqueeTool::release(const ToolPointer& p) {
    const State state = m_state;
    m_state = State::Idle;
    if (state == State::Moving) {
        host().session()->endDrag();
        host().finishSession(true);
        return;
    }
    if (state != State::Selecting)
        return;
    m_current = p.pixel();
    Document* doc = host().toolDocument();
    const selection::CombineMode mode = combineModeFor(m_mods);
    if (!m_dragged && m_current == m_anchor) {
        if (mode == selection::CombineMode::Replace)
            report(host(), doc->deselect());
    } else {
        report(host(), doc->setSelection(selection::marquee(doc->size(), m_anchor, m_current), mode));
    }
    host().toolChanged();
}

bool MarqueeTool::keyPress(QKeyEvent* e) {
    if (e->key() == Qt::Key_Escape && isActive()) {
        cancel();
        return true;
    }
    return false;
}

void MarqueeTool::cancel() {
    if (m_state == State::Moving) {
        host().session()->endDrag();
        host().finishSession(false);
    }
    m_state = State::Idle;
    host().toolChanged();
}

QRect MarqueeTool::dragRect() const {
    return QRect(QPoint(std::min(m_anchor.x(), m_current.x()), std::min(m_anchor.y(), m_current.y())),
                 QPoint(std::max(m_anchor.x(), m_current.x()), std::max(m_anchor.y(), m_current.y())));
}

void MarqueeTool::paintOverlay(QPainter& painter, const QTransform& docToWidget) {
    if (m_state != State::Selecting)
        return;
    const QRect r = dragRect();
    QPainterPath path;
    path.addPolygon(docToWidget.map(QPolygonF(QRectF(r.left(), r.top(), r.width(), r.height()))));
    path.closeSubpath();
    paintAnts(painter, path, 0);
}

Qt::CursorShape MarqueeTool::cursor(const QPointF& doc) const {
    Document* d = host().toolDocument();
    const MarqueeOptions& options = host().toolBox()->fixedOptions().marquee;
    if (options.cutAndMove && d->selection() && d->selection()->at(int(std::floor(doc.x())), int(std::floor(doc.y()))) > 0)
        return Qt::SizeAllCursor;
    return Qt::CrossCursor;
}

void LassoTool::addPoint(const QPointF& docPos, PointerKind kind) {
    const bool pen = kind == PointerKind::Pen || kind == PointerKind::Eraser;
    const QPointF c = pen ? docPos : pixelCentre(QPoint(int(std::floor(docPos.x())), int(std::floor(docPos.y()))));
    if (m_path.isEmpty() || QLineF(m_path.last(), c).length() >= (pen ? 0.25 : 1.0))
        m_path << c;
}

void LassoTool::press(const ToolPointer& p) {
    if (!m_started) {
        m_started = true;
        m_mods = p.modifiers;
        m_path.clear();
    }
    m_down = true;
    m_dragged = false;
    m_kind = p.sample.kind;
    m_cursor = pixelCentre(p.pixel());
    if (m_path.isEmpty())
        addPoint(p.doc, m_kind);
    m_freehand = host().toolBox()->fixedOptions().lasso.mode != LassoMode::Polygonal;
    host().toolChanged();
}

void LassoTool::move(const ToolPointer& p) {
    m_cursor = pixelCentre(p.pixel());
    if (m_started && m_down && m_freehand) {
        if (!m_dragged && QLineF(p.doc, m_path.last()).length() >= 0.5)
            m_dragged = true;
        addPoint(p.doc, p.sample.kind);
    }
    host().toolChanged();
}

void LassoTool::release(const ToolPointer& p) {
    if (!m_started || !m_down)
        return;
    m_down = false;
    if (m_freehand) {
        addPoint(p.doc, p.sample.kind);
        m_freehand = false;
    }
    const LassoMode mode = host().toolBox()->fixedOptions().lasso.mode;
    if (mode == LassoMode::Freeform) {
        close();
        return;
    }
    // Polygonal: the vertex goes where the button was released. Polygonal + Freeform: after a freehand stretch the
    // path already ends there; a plain click adds a vertex.
    addPoint(p.doc, p.sample.kind);
    host().toolChanged();
}

void LassoTool::doubleClick(const ToolPointer&) {
    if (m_started)
        close();
}

bool LassoTool::keyPress(QKeyEvent* e) {
    if (!m_started)
        return false;
    if (e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter) {
        close();
        return true;
    }
    if (e->key() == Qt::Key_Escape) {
        cancel();
        return true;
    }
    return false;
}

void LassoTool::cancel() {
    m_started = m_down = m_freehand = false;
    m_path.clear();
    host().toolChanged();
}

void LassoTool::close() {
    const QPolygonF path = m_path;
    const Qt::KeyboardModifiers mods = m_mods;
    const bool wasFreehandClick = !m_dragged && path.size() < 3;
    m_started = m_down = false;
    m_path.clear();
    Document* doc = host().toolDocument();
    const LassoOptions& options = host().toolBox()->fixedOptions().lasso;
    const selection::CombineMode mode = combineModeFor(mods);
    if (path.size() >= 3) {
        report(host(), doc->setSelection(selection::polygon(doc->size(), path), mode));
    } else if (wasFreehandClick && options.mode == LassoMode::Freeform && mode == selection::CombineMode::Replace) {
        report(host(), doc->deselect());
    }
    host().toolChanged();
}

void LassoTool::paintOverlay(QPainter& painter, const QTransform& docToWidget) {
    if (!m_started || m_path.size() < 1)
        return;
    QPolygonF shown = m_path;
    const LassoMode mode = host().toolBox()->fixedOptions().lasso.mode;
    if (mode != LassoMode::Freeform || !m_down)
        shown << m_cursor;
    QPainterPath path;
    path.addPolygon(docToWidget.map(shown));
    paintAnts(painter, path, 0);
}

void WandTool::press(const ToolPointer& p) {
    Document* doc = host().toolDocument();
    const WandToolOptions& o = host().toolBox()->fixedOptions().wand;
    const QPoint seed = p.pixel();
    if (!doc->rect().contains(seed))
        return;
    selection::WandOptions wand;
    wand.mode = o.mode;
    wand.colourThreshold = o.colourThreshold;
    wand.transparencyRange = o.transparencyRange;
    wand.gapTolerance = o.gapTolerance;
    wand.antialias = o.antialias;
    SelectionMask mask;
    if (o.source == WandSource::MergedImage) {
        mask = selection::magicWand(doc->mergedImage(), seed, wand);
    } else {
        const LayerNode* layer = doc->currentLayer();
        if (!layer || layer->isFolder()) {
            host().toolMessage(tr("The Magic Wand needs a layer with pixels."));
            return;
        }
        mask = selection::magicWand(layer->pixels, seed, wand);
    }
    report(host(), doc->setSelection(std::move(mask), combineModeFor(p.modifiers)));
    host().toolChanged();
}

}
