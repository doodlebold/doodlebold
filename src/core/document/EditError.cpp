#include "core/document/EditError.h"

#include "core/layers/LayerStack.h"

#include <QCoreApplication>

namespace doodlebold {

QString describe(EditError e) {
    switch (e) {
    case EditError::NoSuchLayer: return QCoreApplication::translate("EditError", "There is no such layer.");
    case EditError::NotForThisLayer: return QCoreApplication::translate("EditError", "This operation does not apply to this layer.");
    case EditError::NotARasterLayer: return QCoreApplication::translate("EditError", "This works on layers, not folders.");
    case EditError::LayerLocked: return QCoreApplication::translate("EditError", "The layer is locked.");
    case EditError::LayerHidden: return QCoreApplication::translate("EditError", "The layer is hidden.");
    case EditError::LastLayer: return QCoreApplication::translate("EditError", "A canvas keeps at least one layer.");
    case EditError::NothingBelow: return QCoreApplication::translate("EditError", "There is no layer below to merge into.");
    case EditError::InvalidMove: return QCoreApplication::translate("EditError", "The layer cannot be moved there.");
    case EditError::FoldersTooDeep:
        return QCoreApplication::translate("EditError", "Folders can only be nested %1 levels deep.").arg(LayerStack::kMaxFolderLevels);
    case EditError::NoChange: return QCoreApplication::translate("EditError", "Nothing to change.");
    case EditError::StrokeInProgress: return QCoreApplication::translate("EditError", "Finish the current stroke first.");
    case EditError::InvalidTransform: return QCoreApplication::translate("EditError", "That transformation is not possible.");
    case EditError::SizeMismatch: return QCoreApplication::translate("EditError", "That does not fit the canvas size.");
    case EditError::NoLayerMask: return QCoreApplication::translate("EditError", "The layer has no layer mask.");
    case EditError::HasLayerMask: return QCoreApplication::translate("EditError", "The layer has a layer mask already.");
    case EditError::MaskDisabled: return QCoreApplication::translate("EditError", "The layer mask is switched off.");
    case EditError::NoCloneSource: return QCoreApplication::translate("EditError", "Alt+click to set the clone source.");
    case EditError::CloneOnMask:
        return QCoreApplication::translate("EditError", "The Clone Stamp does not paint layer masks. Select the layer instead.");
    case EditError::LastFrame: return QCoreApplication::translate("EditError", "An animation keeps at least one frame.");
    case EditError::NoSuchFrame: return QCoreApplication::translate("EditError", "There is no such frame.");
    case EditError::LastTrack: return QCoreApplication::translate("EditError", "An animation keeps at least one track.");
    case EditError::LastShownTrack: return QCoreApplication::translate("EditError", "At least one track stays shown.");
    case EditError::NoSuchTrack: return QCoreApplication::translate("EditError", "There is no such track.");
    case EditError::TrackLocked: return QCoreApplication::translate("EditError", "The track is locked.");
    }
    return {};
}

}
