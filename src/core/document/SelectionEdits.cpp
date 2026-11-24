#include "core/document/SelectionEdits.h"

#include "core/layers/Compositor.h"

namespace doodlebold::selection_edits {

namespace {

using Failure = EditResult<>;

EditResult<> replace(DocumentState& s, SelectionMask next) {
    std::optional<SelectionMask> value;
    if (!next.isEmpty())
        value = std::move(next);
    if (value == s.selection)
        return Failure::failure(EditError::NoChange);
    s.selection = std::move(value);
    return {};
}

}

EditResult<> selectAll(DocumentState& s) {
    return replace(s, selection::all(s.layers.size()));
}

EditResult<> deselect(DocumentState& s) {
    if (!s.selection)
        return Failure::failure(EditError::NoChange);
    s.selection.reset();
    return {};
}

EditResult<> invert(DocumentState& s) {
    if (!s.selection)
        return Failure::failure(EditError::NoChange);
    return replace(s, selection::invert(*s.selection));
}

EditResult<> set(DocumentState& s, const SelectionMask& shape, selection::CombineMode mode) {
    if (shape.size() != s.layers.size())
        return Failure::failure(EditError::SizeMismatch);
    return replace(s, selection::combine(s.selection, shape, mode));
}

EditResult<> dilate(DocumentState& s, int n) {
    if (!s.selection || n < 1)
        return Failure::failure(EditError::NoChange);
    return replace(s, selection::dilate(*s.selection, n));
}

EditResult<> erode(DocumentState& s, int n) {
    if (!s.selection || n < 1)
        return Failure::failure(EditError::NoChange);
    return replace(s, selection::erode(*s.selection, n));
}

EditResult<> fromLayer(DocumentState& s, LayerId id, selection::CombineMode mode) {
    const LayerNode* n = s.layers.find(id);
    if (!n)
        return Failure::failure(EditError::NoSuchLayer);
    if (!n->isFolder())
        return set(s, selection::fromLayerAlpha(n->pixels), mode);
    std::vector<const LayerNode*> kids;
    kids.reserve(n->children.size());
    for (const LayerNode& c : n->children)
        kids.push_back(&c);
    return set(s, selection::fromLayerAlpha(Compositor::flatten(kids, s.layers.size())), mode);
}

EditResult<> fromMask(DocumentState& s, LayerId id, selection::CombineMode mode) {
    const LayerNode* n = s.layers.find(id);
    if (!n)
        return Failure::failure(EditError::NoSuchLayer);
    if (!n->mask)
        return Failure::failure(EditError::NoLayerMask);
    return set(s, *n->mask, mode);
}

}
