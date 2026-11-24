#include "core/document/TransformSession.h"

#include "core/document/TransformEdits.h"

#include <QPolygonF>
#include <QTransform>

#include <algorithm>
#include <cmath>
#include <numbers>

namespace doodlebold {

namespace {

using transform::Quad;

constexpr double kSnapDegrees = 15.0;  // Shift while rotating
constexpr double kMinScale = 1e-3;

QPointF meanOf(const Quad& q) {
    QPointF c;
    for (const QPointF& p : q.corner)
        c += p;
    return c / 4.0;
}

double dot(const QPointF& a, const QPointF& b) {
    return a.x() * b.x() + a.y() * b.y();
}

bool nearlyEqual(const Quad& a, const Quad& b) {
    for (std::size_t i = 0; i < 4; ++i)
        if (std::abs(a.corner[i].x() - b.corner[i].x()) > 1e-6 || std::abs(a.corner[i].y() - b.corner[i].y()) > 1e-6)
            return false;
    return true;
}

Quad translated(const Quad& q, const QPointF& d) {
    Quad out = q;
    for (QPointF& p : out.corner)
        p += d;
    return out;
}

double clampScale(double s) {
    return std::abs(s) < kMinScale ? std::copysign(kMinScale, s == 0.0 ? 1.0 : s) : s;
}

// Resize: scales the quad in its own frame (origin = corner 0, axes = the edges to corners 1 and 3) about the corner or
Quad resized(const Quad& q0, TransformSession::Handle h, const QPointF& p, bool keepRatio) {
    using H = TransformSession::Handle;
    const QPointF o = q0.corner[0];
    const QPointF ux = q0.corner[1] - o, uy = q0.corner[3] - o;
    const double det = ux.x() * uy.y() - ux.y() * uy.x();
    if (std::abs(det) < 1e-9)
        return q0;
    const auto toLocal = [&](const QPointF& d) {
        return QPointF((d.x() * uy.y() - d.y() * uy.x()) / det, (ux.x() * d.y() - ux.y() * d.x()) / det);
    };
    const auto toDoc = [&](const QPointF& st) { return o + st.x() * ux + st.y() * uy; };
    const QPointF lp = toLocal(p - o);
    static const QPointF nominal[4] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};

    double ax = 0, ay = 0, sx = 1, sy = 1;
    switch (h) {
    case H::Corner0:
    case H::Corner1:
    case H::Corner2:
    case H::Corner3: {
        const int i = int(h) - int(H::Corner0);
        const QPointF c = nominal[i], a = nominal[(i + 2) % 4];
        ax = a.x(), ay = a.y();
        sx = (lp.x() - ax) / (c.x() - ax);
        sy = (lp.y() - ay) / (c.y() - ay);
        if (keepRatio)
            sx = sy = std::abs(sx) >= std::abs(sy) ? sx : sy;
        break;
    }
    case H::Edge0: ay = 1, sy = (lp.y() - 1.0) / -1.0; break;  // top edge: the bottom stays
    case H::Edge1: ax = 0, sx = lp.x(); break;                 // right edge: the left stays
    case H::Edge2: ay = 0, sy = lp.y(); break;                 // bottom edge: the top stays
    case H::Edge3: ax = 1, sx = (lp.x() - 1.0) / -1.0; break;  // left edge: the right stays
    default: return q0;
    }
    sx = clampScale(sx), sy = clampScale(sy);
    Quad out;
    for (std::size_t i = 0; i < 4; ++i) {
        const QPointF l = toLocal(q0.corner[i] - o);
        out.corner[i] = toDoc(QPointF(ax + (l.x() - ax) * sx, ay + (l.y() - ay) * sy));
    }
    return out;
}

QRect boundsOf(const Quad& q) {
    double x0 = q.corner[0].x(), x1 = x0, y0 = q.corner[0].y(), y1 = y0;
    for (const QPointF& p : q.corner) {
        x0 = std::min(x0, p.x()), x1 = std::max(x1, p.x());
        y0 = std::min(y0, p.y()), y1 = std::max(y1, p.y());
    }
    return QRect(QPoint(int(std::floor(x0)) - 1, int(std::floor(y0)) - 1), QPoint(int(std::ceil(x1)) + 1, int(std::ceil(y1)) + 1));
}

}

TransformSession::TransformSession(Document& doc, LayerId id, Kind kind, TransformMode mode, const MarqueeOptions& options)
    : m_doc(doc), m_layer(id), m_kind(kind), m_mode(mode), m_options(options), m_projection(doc.size(), doc.background()) {}

EditResult<std::shared_ptr<TransformSession>> TransformSession::beginTransform(Document& doc, LayerId id, TransformMode mode,
                                                                               const MarqueeOptions& options) {
    using R = EditResult<std::shared_ptr<TransformSession>>;
    const LayerNode* n = doc.layer(id);
    if (!n)
        return R::failure(EditError::NoSuchLayer);
    if (n->isFolder())
        return R::failure(EditError::NotARasterLayer);
    if (n->isText())
        return R::failure(EditError::NotForThisLayer);  // Ctrl+T on a text layer is refused; the Move tool moves it
    if (n->props.locked)
        return R::failure(EditError::LayerLocked);
    if (doc.transformBounds(id).isEmpty())
        return R::failure(EditError::NoChange);
    std::shared_ptr<TransformSession> s(new TransformSession(doc, id, Kind::Transform, mode, options));
    s->begin();
    return R(std::move(s));
}

EditResult<std::shared_ptr<TransformSession>> TransformSession::beginMove(Document& doc, LayerId id, const MarqueeOptions& options) {
    using R = EditResult<std::shared_ptr<TransformSession>>;
    const LayerNode* n = doc.layer(id);
    if (!n)
        return R::failure(EditError::NoSuchLayer);
    if (n->isFolder())
        return R::failure(EditError::NotARasterLayer);
    if (n->props.locked)
        return R::failure(EditError::LayerLocked);
    if (doc.transformBounds(id).isEmpty())
        return R::failure(EditError::NoChange);
    std::shared_ptr<TransformSession> s(new TransformSession(doc, id, Kind::Move, options.transformMode, options));
    s->begin();
    return R(std::move(s));
}

EditResult<std::shared_ptr<TransformSession>> TransformSession::beginMoveLayers(Document& doc, const std::vector<LayerId>& selected,
                                                                                const MarqueeOptions& options) {
    using R = EditResult<std::shared_ptr<TransformSession>>;
    const auto leaves = transform_edits::layersToMove(doc.layers(), selected);
    if (!leaves)
        return R::failure(leaves.error());
    std::shared_ptr<TransformSession> s(new TransformSession(doc, kNoLayer, Kind::Move, options.transformMode, options));
    s->m_selected = selected;
    s->m_layers = doc.layers();
    s->m_selection = doc.selection();
    for (const LayerId id : leaves.value()) {
        const LayerNode* n = doc.layer(id);
        const QRect box = n->pixels.contentBounds(true);
        if (box.isEmpty())
            continue;
        s->m_moved.push_back({id, n->pixels, n->props.maskLinked ? n->mask : std::nullopt, box});
        s->m_box |= box;
    }
    if (s->m_moved.empty())
        return R::failure(EditError::NoChange);
    s->m_quad = Quad::of(QRectF(s->m_box));
    s->m_projection.assign(doc.projection());
    s->m_lastArea = s->m_box;
    return R(std::move(s));
}

void TransformSession::begin() {
    m_perspective = std::clamp(m_options.perspective / 100.0, 0.0, 1.0);
    m_box = m_doc.transformBounds(m_layer);
    m_quad = Quad::of(QRectF(m_box));
    m_layers = m_doc.layers();
    LayerNode* node = m_layers.find(m_layer);
    m_originalSelection = node->isText() ? std::nullopt : m_doc.selection();  // a text layer moves whole
    if (m_originalSelection && node->props.maskLinked)
        bakeMask(*node);  // as the commit does (TransformEdits::apply): only a part moves
    m_original = node->pixels;
    m_originalMask = node->props.maskLinked ? node->mask : std::nullopt;  // an unlinked mask stays where it is
    m_selection = m_originalSelection;
    if (m_originalSelection) {
        transform::Lifted lifted = transform::liftSelection(m_original, *m_originalSelection);
        m_part = std::move(lifted.part);
        m_rest = std::move(lifted.rest);
    } else {
        m_part = m_original;
        m_rest = TiledImage(m_doc.size());
    }
    m_projection.assign(m_doc.projection());
    m_lastArea = m_box;
}

void TransformSession::setOptions(const MarqueeOptions& options) {
    m_options = options;
    m_perspective = std::clamp(options.perspective / 100.0, 0.0, 1.0);
    changed();
}

void TransformSession::setPerspective(double p) {
    p = std::clamp(p, 0.0, 1.0);
    if (p == m_perspective)
        return;
    m_perspective = p;
    changed();
}

bool TransformSession::isIdentity() const {
    if (m_kind == Kind::Move)
        return m_offset.isNull();
    return nearlyEqual(m_quad, Quad::of(QRectF(m_box)));  // perspective only acts on a box that is no longer a rectangle
}

void TransformSession::reset() {
    m_quad = Quad::of(QRectF(m_box));
    m_offset = {};
    m_dragging = false;
    changed();
}

QPointF TransformSession::centre() const {
    return meanOf(m_quad);
}

QRect TransformSession::coveredArea() const {
    if (m_kind == Kind::Move)
        return m_box | m_box.translated(m_offset);
    return m_box | boundsOf(m_quad);
}

QPointF TransformSession::handlePosition(Handle h) const {
    const int c = int(h) - int(Handle::Corner0);
    if (c >= 0 && c < 4)
        return m_quad.corner[std::size_t(c)];
    const int e = int(h) - int(Handle::Edge0);
    if (e >= 0 && e < 4)
        return (m_quad.corner[std::size_t(e)] + m_quad.corner[std::size_t((e + 1) % 4)]) / 2.0;
    return centre();
}

TransformSession::Handle TransformSession::hitTest(const QPointF& doc, double docPerPixel) const {
    if (m_kind == Kind::Move)
        return Handle::Inside;
    const double r = kHandleRadius * docPerPixel;
    const auto near = [&](const QPointF& p) { return std::hypot(p.x() - doc.x(), p.y() - doc.y()) <= r; };
    for (int i = 0; i < 4; ++i)
        if (near(m_quad.corner[std::size_t(i)]))
            return Handle(int(Handle::Corner0) + i);
    for (int i = 0; i < 4; ++i)
        if (near(handlePosition(Handle(int(Handle::Edge0) + i))))
            return Handle(int(Handle::Edge0) + i);
    const QPolygonF poly({m_quad.corner[0], m_quad.corner[1], m_quad.corner[2], m_quad.corner[3]});
    return poly.containsPoint(doc, Qt::OddEvenFill) ? Handle::Inside : Handle::Outside;
}

void TransformSession::beginDrag(Handle h, const QPointF& start) {
    m_dragging = true;
    m_handle = h;
    m_dragStart = start;
    m_dragQuad = m_quad;
    m_dragOffset = m_offset;
}

void TransformSession::dragTo(const QPointF& pos, Qt::KeyboardModifiers mods) {
    if (!m_dragging)
        return;
    const QPointF delta = pos - m_dragStart;
    if (m_kind == Kind::Move) {
        m_offset = m_dragOffset + QPoint(int(std::lround(delta.x())), int(std::lround(delta.y())));
        changed();
        return;
    }
    const bool shift = mods & Qt::ShiftModifier;
    const Handle h = m_handle;
    Quad q = m_dragQuad;
    if (h == Handle::None) {
        return;
    }
    if (h == Handle::Inside) {
        q = translated(m_dragQuad, delta);
    } else if (m_mode == TransformMode::Rotate) {
        const QPointF c = meanOf(m_dragQuad);
        const QPointF a = m_dragStart - c, b = pos - c;
        if (std::hypot(a.x(), a.y()) > 1e-6 && std::hypot(b.x(), b.y()) > 1e-6) {
            double deg = (std::atan2(b.y(), b.x()) - std::atan2(a.y(), a.x())) * 180.0 / std::numbers::pi;
            if (shift)
                deg = std::round(deg / kSnapDegrees) * kSnapDegrees;
            q = m_dragQuad.transformed(QTransform().translate(c.x(), c.y()).rotate(deg).translate(-c.x(), -c.y()));
        }
    } else if (m_mode == TransformMode::Resize) {
        q = resized(m_dragQuad, h, pos, shift);
    } else {  // Freeform, Deform
        const int c = int(h) - int(Handle::Corner0);
        const int e = int(h) - int(Handle::Edge0);
        if (c >= 0 && c < 4) {
            q.corner[std::size_t(c)] += delta;
        } else if (e >= 0 && e < 4) {
            const std::size_t i = std::size_t(e), j = std::size_t((e + 1) % 4);
            QPointF move = delta;
            if (m_mode == TransformMode::Freeform) {  // stretch: only the part of the drag across the edge
                const QPointF edge = m_dragQuad.corner[j] - m_dragQuad.corner[i];
                QPointF n(edge.y(), -edge.x());
                const double len = std::hypot(n.x(), n.y());
                if (len < 1e-9)
                    return;
                n /= len;
                const QPointF mid = (m_dragQuad.corner[i] + m_dragQuad.corner[j]) / 2.0;
                if (dot(n, mid - meanOf(m_dragQuad)) < 0)
                    n = -1.0 * n;
                move = dot(delta, n) * n;
            }
            q.corner[i] += move;
            q.corner[j] += move;
        }
    }
    m_quad = q;
    changed();
}

void TransformSession::endDrag() {
    if (!m_dragging)
        return;
    m_dragging = false;
    m_handle = Handle::None;
    if (!m_gpuEnabled || m_gpuStale)
        refreshCpu(false);  // back to the Preview quality
}

void TransformSession::setGpuPreview(bool on) {
    on = on && m_moved.empty();
    if (on == m_gpuEnabled)
        return;
    m_gpuEnabled = on;
    if (!on && m_gpuStale)
        refreshCpu(false);
}

const FloatingPreview& TransformSession::floatingPreview() {
    if (!m_floating) {
        // The GPU draws the lifted pixels through the quad; a mask moves with them, so it is multiplied in for the preview.
        const LayerNode* node = m_layers.find(m_layer);
        LayerNode masked;
        masked.pixels = m_part;
        masked.mask = m_originalMask;
        masked.props.maskEnabled = node && node->props.maskEnabled;
        bakeMask(masked);
        m_floating = buildFloatingPreview(m_layers, m_layer, m_rest, masked.pixels, m_box, m_doc.background());
    }
    return *m_floating;
}

transform::Quad TransformSession::floatingQuad() const {
    if (m_kind == Kind::Move)
        return Quad::of(QRectF(m_box.translated(m_offset)));
    return m_quad;
}

void TransformSession::settlePreview() {
    if (m_gpuStale)
        refreshCpu(false);
}

void TransformSession::changed() {
    // Dragging with the GPU preview on: only the quad moved, the owner draws it. (Back at the untransformed shape the
    if (m_gpuEnabled && m_dragging && !isIdentity()) {
        m_gpuStale = true;
        ++m_gpuSteps;
        return;
    }
    refreshCpu(m_dragging);
}

void TransformSession::refreshCpu(bool dragQuality) {
    m_gpuStale = false;
    ++m_cpuPreviews;
    for (const MovedLayer& moved : m_moved) {
        LayerNode* layer = m_layers.find(moved.id);
        layer->pixels = transform::moveExactly(moved.pixels, moved.box, transform::Turn::None, m_offset);
        if (moved.mask)
            layer->mask = transform::moveExactly(*moved.mask, moved.mask->rect(), transform::Turn::None, m_offset);
    }
    LayerNode* node = m_layers.find(m_layer);
    if (!m_moved.empty()) {
        // Done above.
    } else if (isIdentity()) {
        node->pixels = m_original;
        if (m_originalMask)
            node->mask = m_originalMask;
        m_selection = m_originalSelection;
    } else if (m_kind == Kind::Move) {
        node->pixels = transform::composeOver(m_rest, transform::moveExactly(m_part, m_box, transform::Turn::None, m_offset));
        if (m_originalMask)
            node->mask = transform::moveExactly(*m_originalMask, m_box, transform::Turn::None, m_offset);
        if (m_originalSelection) {
            SelectionMask moved = transform::moveExactly(*m_originalSelection, m_box, transform::Turn::None, m_offset);
            m_selection = moved.isEmpty() ? std::nullopt : std::optional<SelectionMask>(std::move(moved));
        }
    } else {
        const transform::Interpolation q =
            transform::interactiveQuality(m_options.final, m_options.preview, m_options.dragging, dragQuality);
        node->pixels = transform::composeOver(m_rest, transform::resample(m_part, m_box, m_quad, q, m_perspective));
        if (m_originalMask)
            node->mask = transform::resample(*m_originalMask, m_box, m_quad, q, m_perspective);
        if (m_originalSelection) {
            SelectionMask moved = transform::resample(*m_originalSelection, m_box, m_quad, q, m_perspective);
            m_selection = moved.isEmpty() ? std::nullopt : std::optional<SelectionMask>(std::move(moved));
        }
    }
    const QRect area = (m_lastArea | coveredArea()).intersected(m_doc.rect());
    m_lastArea = coveredArea();
    m_projection.invalidate(area);
    m_dirty |= area;
}

const QImage& TransformSession::image() {
    return m_projection.image(m_layers);
}

QRect TransformSession::takeDirty() {
    const QRect r = m_dirty;
    m_dirty = QRect();
    return r;
}

EditResult<> TransformSession::commit() {
    if (isIdentity())
        return EditResult<>::failure(EditError::NoChange);
    if (!m_moved.empty())
        return m_doc.moveLayersExactly(m_selected, m_offset);
    if (m_kind == Kind::Move)
        return m_doc.moveLayerExactly(m_layer, transform::Turn::None, m_offset);
    return m_doc.transformLayer(m_layer, m_quad, m_options.final, m_perspective);
}

}
