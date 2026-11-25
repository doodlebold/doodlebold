#pragma once

#include "core/util/Result.h"

#include <QString>

namespace doodlebold {

// Why a document edit was refused: an expected situation (a shortcut on a layer where it does not apply), so a value the UI can
// show, not an exception.
enum class EditError {
    NoSuchLayer,
    NotARasterLayer,  // the operation needs pixels, the target is a folder
    LayerLocked,
    LayerHidden,
    LastLayer,        // a document keeps at least one layer
    NothingBelow,     // Merge Down at the bottom of a folder
    InvalidMove,      // e.g. a folder into its own descendant
    FoldersTooDeep,   // a folder beyond LayerStack::kMaxFolderLevels
    NoChange,         // the edit would not change anything (no undo step is recorded)
    StrokeInProgress, // the layer tree cannot change while a brush stroke is running
    InvalidTransform, // a transform that collapses the layer to nothing
    SizeMismatch,     // a selection or image built for another canvas size
    NotForThisLayer,  // a pixel edit on a text layer ("This operation does not apply to this layer.")
    NoLayerMask,      // a mask operation on a layer without a mask
    HasLayerMask,     // New Layer Mask on a layer that has one
    MaskDisabled,     // painting a layer mask that is switched off
    NoCloneSource,    // the Clone Stamp has no source point yet (Alt+click sets it)
    CloneOnMask,      // the Clone Stamp does not paint layer masks
    LastFrame,        // an animation keeps at least one frame
    NoSuchFrame,      // a frame index outside the animation
    LastTrack,        // an animation keeps at least one track
    LastShownTrack,   // at least one track stays shown
    NoSuchTrack,      // a track index outside the animation
    TrackLocked,      // changing or painting a locked track
    UndoLetGo,        // Undo reached a step whose pixels were freed to keep within memory
};

QString describe(EditError e);

template <typename T = void> using EditResult = Result<T, EditError>;

}
