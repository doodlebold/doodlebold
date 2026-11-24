#pragma once

#include "core/document/DocumentState.h"
#include "core/document/EditError.h"
#include "core/transform/Resample.h"

// Moving a layer's pixels as pure functions on a DocumentState. Without a selection the whole layer moves. With one, only the
// selected part moves: it is lifted out (each pixel by its coverage, transform/Lift.h), the hole stays transparent,
// the part is moved and laid over what remained, and the selection goes through the same transform. Pixels moved off the canvas
// are lost. Common refusals: a folder (NotARasterLayer), a locked layer (LayerLocked), nothing to move (NoChange).
namespace doodlebold::transform_edits {

// The transform box: the selection's bounds, else the bounds of the layer's content. Empty when there is nothing to
// transform.
QRect transformBounds(const DocumentState& s, LayerId id);

// Transform Layer (Ctrl+T): the box is mapped onto destination. Start from Quad::of(transformBounds(...)), the identity
// (NoChange); a degenerate quad is InvalidTransform.
EditResult<> transformLayer(DocumentState& s, LayerId id, const transform::Quad& destination, transform::Interpolation interpolation,
                            double perspective = 0.0);

EditResult<> moveLayerExactly(DocumentState& s, LayerId id, transform::Turn turn, const QPoint& offset = {});
// The layers to move for these selected ones: a folder stands for the layers inside it. Each counts once, and the empty ones too.
EditResult<std::vector<LayerId>> layersToMove(const LayerStack& layers, const std::vector<LayerId>& selected);
// Several whole layers moved together, each with its linked mask; a selection neither limits them nor moves.
EditResult<> moveLayersExactly(DocumentState& s, const std::vector<LayerId>& selected, const QPoint& offset);

}
