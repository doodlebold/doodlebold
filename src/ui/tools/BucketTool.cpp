#include "ui/tools/BucketTool.h"

#include "core/document/Document.h"
#include "core/tools/ToolBox.h"

namespace doodlebold {

selection::WandOptions bucketSearch(const BucketSettings& b) {
    selection::WandOptions o;
    o.mode = b.mode;
    o.colourThreshold = b.colourThreshold;
    o.transparencyRange = b.colourThreshold;
    o.gapTolerance = b.gapTolerance;
    o.antialias = b.antialias;
    return o;
}

void BucketTool::press(const ToolPointer& p) {
    Document* doc = host().toolDocument();
    const QPoint seed = p.pixel();
    const LayerNode* layer = doc->currentLayer();
    if (!doc->rect().contains(seed) || !layer)
        return;
    const BucketSettings& b = host().toolBox()->settings().bucket;
    SelectionMask area = selection::magicWand(b.mergedSource ? doc->mergedImage() : layer->pixels, seed, bucketSearch(b));
    if (doc->selection() && !b.ignoreSelection)
        area = selection::combine(doc->selection(), area, selection::CombineMode::Intersect);
    const EditResult<> filled = doc->fillArea(doc->currentLayerId(), host().toolBox()->paintColor(), area);
    if (!filled && filled.error() != EditError::NoChange)
        host().toolMessage(describe(filled.error()));
    host().toolChanged();
}

}
