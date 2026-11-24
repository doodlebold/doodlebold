#include "core/document/TransformEdits.h"

#include "core/document/LayerOps.h"

#include <QLineF>

#include "core/transform/Lift.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <optional>

namespace doodlebold::transform_edits {

namespace {

using Failure = EditResult<>;

std::optional<EditError> refusal(const DocumentState& s, LayerId id) {
    const LayerNode* n = s.layers.find(id);
    if (!n)
        return EditError::NoSuchLayer;
    if (n->isFolder())
        return EditError::NotARasterLayer;
    if (n->props.locked)
        return EditError::LayerLocked;
    return std::nullopt;
}

// A linked layer mask moves with the whole layer; when only a selected part moves, an active linked mask is first
// multiplied into the pixels. An unlinked mask stays.
EditResult<> apply(DocumentState& s, LayerId id, const std::function<TiledImage(const TiledImage&)>& movePixels,
                   const std::function<SelectionMask(const SelectionMask&)>& moveMask) {
    LayerNode* n = s.layers.find(id);
    if (!s.selection) {
        n->pixels = movePixels(n->pixels);
        if (n->mask && n->props.maskLinked)
            n->mask = moveMask(*n->mask);
        return {};
    }
    if (n->props.maskLinked)
        bakeMask(*n);
    transform::Lifted lifted = transform::liftSelection(n->pixels, *s.selection);
    n->pixels = transform::composeOver(lifted.rest, movePixels(lifted.part));
    SelectionMask moved = moveMask(*s.selection);
    if (moved.isEmpty())
        s.selection.reset();  // moved off the canvas
    else
        s.selection = std::move(moved);
    return {};
}

double area(const transform::Quad& q) {
    double twice = 0.0;
    for (std::size_t i = 0; i < 4; ++i) {
        const QPointF a = q.corner[i], b = q.corner[(i + 1) % 4];
        twice += a.x() * b.y() - b.x() * a.y();
    }
    return std::abs(twice) / 2.0;
}

}

QRect transformBounds(const DocumentState& s, LayerId id) {
    const LayerNode* n = s.layers.find(id);
    if (!n || n->isFolder())
        return {};
    return s.selection && !n->isText() ? s.selection->bounds(true) : n->pixels.contentBounds(true);
}

// A text layer only moves (the Move tool moves it, Ctrl+T refuses it): its text's origin shifts and it is drawn again; the
// selection stays.
EditResult<> moveText(DocumentState& s, LayerId id, const QPointF& delta) {
    LayerNode* n = s.layers.find(id);
    if (delta.isNull())
        return Failure::failure(EditError::NoChange);
    TextContent moved = *n->text;
    moved.origin += delta;
    return layer_ops::setText(s, id, moved);
}

EditResult<> transformLayer(DocumentState& s, LayerId id, const transform::Quad& destination, transform::Interpolation interpolation,
                            double perspective) {
    if (const auto why = refusal(s, id))
        return Failure::failure(*why);
    const QRect box = transformBounds(s, id);
    if (box.isEmpty())
        return Failure::failure(EditError::NoChange);
    if (destination == transform::Quad::of(box))
        return Failure::failure(EditError::NoChange);
    if (s.layers.find(id)->isText()) {
        const transform::Quad from = transform::Quad::of(box);
        const QPointF delta = destination.corner[0] - from.corner[0];
        for (std::size_t i = 1; i < 4; ++i)
            if (QLineF(destination.corner[i] - from.corner[i], delta).length() > 1e-6)
                return Failure::failure(EditError::NotForThisLayer);  // anything but a move
        return moveText(s, id, delta);
    }
    if (area(destination) < 0.5)
        return Failure::failure(EditError::InvalidTransform);
    return apply(
        s, id, [&](const TiledImage& p) { return transform::resample(p, box, destination, interpolation, perspective); },
        [&](const SelectionMask& m) { return transform::resample(m, box, destination, interpolation, perspective); });
}

namespace {

void collectLeaves(const LayerNode& node, std::vector<LayerId>& out) {
    if (!node.isFolder()) {
        if (std::ranges::find(out, node.props.id) == out.end())
            out.push_back(node.props.id);
        return;
    }
    for (const LayerNode& child : node.children)
        collectLeaves(child, out);
}

}

EditResult<std::vector<LayerId>> layersToMove(const LayerStack& layers, const std::vector<LayerId>& selected) {
    using R = EditResult<std::vector<LayerId>>;
    std::vector<LayerId> leaves;
    for (const LayerId id : selected) {
        const LayerNode* node = layers.find(id);
        if (!node)
            return R::failure(EditError::NoSuchLayer);
        collectLeaves(*node, leaves);
    }
    for (const LayerId id : leaves)
        if (layers.find(id)->props.locked)
            return R::failure(EditError::LayerLocked);
    return leaves;
}

EditResult<> moveLayersExactly(DocumentState& s, const std::vector<LayerId>& selected, const QPoint& offset) {
    if (offset.isNull())
        return Failure::failure(EditError::NoChange);
    const auto leaves = layersToMove(s.layers, selected);
    if (!leaves)
        return Failure::failure(leaves.error());
    bool moved = false;
    for (const LayerId id : leaves.value()) {
        LayerNode* n = s.layers.find(id);
        const QRect box = n->pixels.contentBounds(true);
        if (box.isEmpty())
            continue;
        moved = true;
        if (n->isText()) {
            if (const auto done = moveText(s, id, QPointF(offset)); !done)
                return done;
            continue;
        }
        n->pixels = transform::moveExactly(n->pixels, box, transform::Turn::None, offset);
        if (n->mask && n->props.maskLinked)
            n->mask = transform::moveExactly(*n->mask, n->mask->rect(), transform::Turn::None, offset);
    }
    return moved ? EditResult<>{} : Failure::failure(EditError::NoChange);
}

EditResult<> moveLayerExactly(DocumentState& s, LayerId id, transform::Turn turn, const QPoint& offset) {
    if (const auto why = refusal(s, id))
        return Failure::failure(*why);
    const QRect box = transformBounds(s, id);
    if (box.isEmpty())
        return Failure::failure(EditError::NoChange);
    if (turn == transform::Turn::None && offset.isNull())
        return Failure::failure(EditError::NoChange);
    if (s.layers.find(id)->isText())
        return turn == transform::Turn::None ? moveText(s, id, QPointF(offset)) : Failure::failure(EditError::NotForThisLayer);
    return apply(
        s, id, [&](const TiledImage& p) { return transform::moveExactly(p, box, turn, offset); },
        [&](const SelectionMask& m) { return transform::moveExactly(m, box, turn, offset); });
}

}
