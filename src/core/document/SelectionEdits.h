#pragma once

#include "core/document/DocumentState.h"
#include "core/document/EditError.h"
#include "core/selection/SelectionOps.h"

// Selection edits as pure functions on a DocumentState, like LayerOps: a result that selects nothing becomes "no selection",
// and an edit that would leave the selection as it is fails with EditError::NoChange.
namespace doodlebold::selection_edits {

EditResult<> selectAll(DocumentState& s);
EditResult<> deselect(DocumentState& s);
// The canvas minus the selection; refused (NoChange) when nothing is selected.
EditResult<> invert(DocumentState& s);
EditResult<> set(DocumentState& s, const SelectionMask& shape, selection::CombineMode mode);
EditResult<> dilate(DocumentState& s, int n);
EditResult<> erode(DocumentState& s, int n);
EditResult<> fromLayer(DocumentState& s, LayerId id, selection::CombineMode mode);
EditResult<> fromMask(DocumentState& s, LayerId id, selection::CombineMode mode);

}
