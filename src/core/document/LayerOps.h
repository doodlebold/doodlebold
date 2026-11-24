#pragma once

#include "core/document/DocumentState.h"
#include "core/document/EditError.h"

#include <QColor>

#include <vector>

// Layer operations as pure functions on a DocumentState: no signals, no undo, no rendering; Document applies them to a copy of
// its state and records one undo step. On failure the state may be partly modified and must be discarded.
namespace doodlebold::layer_ops {

EditResult<LayerId> addLayer(DocumentState& s);
EditResult<LayerId> addFolder(DocumentState& s);
EditResult<LayerId> duplicate(DocumentState& s, LayerId id);
EditResult<> remove(DocumentState& s, LayerId id);
// Composites `id` onto the sibling below it; the lower layer keeps its own properties and becomes current.
EditResult<> mergeDown(DocumentState& s, LayerId id);
// Move Painting to Layer Below: as Merge Down, but the upper layer stays, empty, with its properties, and current.
EditResult<> transferDown(DocumentState& s, LayerId id);
// The layer panel's several selected layers, one step. A selected folder counts with everything inside it. Delete: LastLayer
// if none would remain. Clear: what can be cleared is, the others stay; refused only if nothing could. Merge: into the lowest
// of them, which keeps its name, place, mode and opacity, like Merge Down repeated; hidden ones go in as if shown.
EditResult<> removeLayers(DocumentState& s, const std::vector<LayerId>& ids);
// Moves them into folder `parent`, keeping their order, with `index` their place among the children that stay.
EditResult<> moveLayers(DocumentState& s, const std::vector<LayerId>& ids, LayerId parent, int index);
EditResult<> clearLayers(DocumentState& s, const std::vector<LayerId>& ids);
EditResult<LayerId> mergeLayers(DocumentState& s, const std::vector<LayerId>& ids);
// Merge Visible Layers: every visible layer composited into a new layer ("LayerN", Normal 100 %) at the top; hidden layers
// stay, folders left empty go. Flatten Image: that new layer is all that remains. Both make it current.
EditResult<LayerId> mergeVisible(DocumentState& s);
EditResult<LayerId> flatten(DocumentState& s);
// Flip Canvas Horizontal / Vertical: every layer, mask and the selection mirrored across the canvas; text layers keep their
// text and move to the mirrored place.
EditResult<> reverseCanvas(DocumentState& s, bool horizontal);
// Trim Canvas by Selection: the canvas becomes the selection's bounding box; the selection stays. Needs one.
EditResult<> trimCanvas(DocumentState& s);
EditResult<> rotateCanvas(DocumentState& s, bool clockwise);
EditResult<> move(DocumentState& s, LayerId id, LayerId parent, int index);
EditResult<> raise(DocumentState& s, LayerId id);
EditResult<> lower(DocumentState& s, LayerId id);
EditResult<> toFront(DocumentState& s, LayerId id);
EditResult<> toBack(DocumentState& s, LayerId id);
// Replaces the editable properties (id and kind are kept; opacity is clamped to 0..100; Pass Through is folders
EditResult<> setProps(DocumentState& s, LayerId id, const LayerProps& props);
// Clear / Fill. Fill respects Preserve Opacity (recolours, keeps coverage). With a selection both act on the selected
// part only, each pixel moving toward transparent / the colour by its coverage.
EditResult<> clear(DocumentState& s, LayerId id);
EditResult<> fill(DocumentState& s, LayerId id, const QColor& color);

// A mask is coverage per pixel: 1.0 shows the layer, 0 hides it. New Layer Mask shows everything, is switched on and linked,
// and becomes the editing target; folders and text layers take masks too.
EditResult<> addMask(DocumentState& s, LayerId id);
// Apply Layer Mask: a switched-on mask is multiplied into the pixels, then the mask is removed (a switched-off one is removed
// without changing them). Raster layers only.
EditResult<> applyMask(DocumentState& s, LayerId id);
// Deletes the mask; the layer becomes the editing target.
EditResult<> removeMask(DocumentState& s, LayerId id);
// Erase / Fill on the mask: Erase hides (0), Fill sets the colour's luma (maskValueOf). With a selection, each pixel moves
// toward the value by its coverage.
EditResult<> clearMask(DocumentState& s, LayerId id);
EditResult<> fillMask(DocumentState& s, LayerId id, const QColor& color);
// The mask value painting a colour gives: its Rec. 601 luma, 0.299 R + 0.587 G + 0.114 B, rounded.
int maskValueOf(const QColor& color);

// The layer's pixels are drawn from its text after every change.
EditResult<LayerId> addTextLayer(DocumentState& s, const TextContent& content);
// Replaces a text layer's text; NoChange if it is the same.
EditResult<> setText(DocumentState& s, LayerId id, const TextContent& content);
EditResult<> rasterize(DocumentState& s, LayerId id);
// Draws a text layer's pixels from its text (after a file load, or when fonts may have changed).
void renderText(LayerNode& node);

}
