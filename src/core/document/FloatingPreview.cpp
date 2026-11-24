#include "core/document/FloatingPreview.h"

#include "core/layers/Compositor.h"

#include <vector>

namespace doodlebold {

namespace {

// The nodes painted after `id` in paint order (bottom → top): first its later siblings, then those of its parent, and so
std::vector<const LayerNode*> laterNodes(const LayerStack& tree, LayerId id) {
    std::vector<const LayerNode*> out;
    for (LayerId cur = id; cur != kNoLayer;) {
        const LayerNode* parent = tree.parentOf(cur);
        if (!parent)
            break;
        for (std::size_t i = std::size_t(tree.indexOf(cur)) + 1; i < parent->children.size(); ++i)
            out.push_back(&parent->children[i]);
        cur = parent->props.id;
    }
    return out;
}

}

FloatingPreview buildFloatingPreview(const LayerStack& layers, LayerId id, const TiledImage& rest, const TiledImage& part, const QRect& box,
                                     const CanvasBackground& background) {
    FloatingPreview out;
    out.box = box;
    const LayerNode* layer = layers.find(id);
    if (!layer)
        return out;
    out.opacity = layer->props.visible ? static_cast<double>(layer->props.opacityFraction()) : 0.0;
    out.source = part.toImage(box);

    if (const std::vector<const LayerNode*> later = laterNodes(layers, id); !later.empty()) {
        const TiledImage flat = Compositor::flatten(later, layers.size());
        if (!flat.isEmpty())
            out.above = flat.toImage();
    }

    // The picture below: a copy of the tree with the layer reduced to `rest` and everything painted after it removed.
    LayerStack tree = layers;
    for (LayerId cur = id; cur != kNoLayer;) {
        LayerNode* parent = tree.parentOf(cur);
        if (!parent)
            break;
        parent->children.resize(std::size_t(tree.indexOf(cur)) + 1);
        cur = parent->props.id;
    }
    tree.find(id)->pixels = rest;
    out.below = QImage(tree.size(), QImage::Format_RGBA8888_Premultiplied);
    Compositor::renderDisplay(tree, QRect(QPoint(0, 0), tree.size()), background, &out.below);
    return out;
}

}
