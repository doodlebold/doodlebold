#pragma once

#include "core/animation/Animation.h"
#include "core/layers/CanvasBackground.h"
#include "core/layers/LayerStack.h"
#include "core/selection/SelectionMask.h"

#include <optional>

namespace doodlebold {

// Everything a structural undo step restores: the layer tree, current layer, selection and canvas background (all tile-shared,
// so copies are cheap). Layer and selection operations (LayerOps.h, SelectionEdits.h) are functions from one DocumentState to
// the next.
struct DocumentState {
    LayerStack layers;
    LayerId current = kNoLayer;
    // The selection; no value = nothing selected, so the whole canvas can be edited. Never an empty mask.
    std::optional<SelectionMask> selection;
    CanvasBackground background;
    // The current layer's mask, not its pixels, is what painting, Erase, Fill and Delete act on (the red frame round the mask
    // thumbnail). Only true while the current layer has a mask; not stored in files.
    bool maskTarget = false;
    // The frames, shared layers, sound clips and video of an animation; none for an ordinary picture (AnimationOps.h).
    Animation animation;
};

}
