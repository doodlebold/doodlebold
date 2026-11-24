#pragma once

#include "core/brush/BrushSettings.h"
#include "core/selection/MagicWand.h"
#include "ui/tools/CanvasTool.h"

namespace doodlebold {

// The Magic Wand's search with the Bucket's choices. In Enclosed Transparent Area, Color Threshold is how solid a
// pixel may be and still count as transparent.
selection::WandOptions bucketSearch(const BucketSettings& b);

// The Bucket: a click fills the area around it with the main colour, as one undo step. Inside a selection the fill
// stays inside it, unless Ignore Existing Selection is on.
class BucketTool : public CanvasTool {
public:
    using CanvasTool::CanvasTool;
    void press(const ToolPointer& p) override;
    void move(const ToolPointer&) override {}
    void release(const ToolPointer&) override {}
    bool isActive() const override { return false; }
};

}
