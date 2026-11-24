#pragma once

#include "core/text/TextContent.h"

#include "core/layers/BlendMode.h"
#include "core/layers/LayerId.h"
#include "core/layers/LayerPaper.h"
#include "core/raster/TiledImage.h"
#include "core/selection/SelectionMask.h"

#include <QSize>
#include <QString>

#include <algorithm>
#include <cstdint>
#include <functional>
#include <optional>
#include <vector>

namespace doodlebold {

enum class LayerKind : std::uint8_t {
    Raster,
    Folder,
    Text
};

struct LayerProps {
    LayerId id = kNoLayer;
    LayerKind kind = LayerKind::Raster;
    QString name;
    BlendMode mode = BlendMode::Normal;
    int opacity = 100;
    bool visible = true;
    bool clipping = false;
    bool preserveOpacity = false;
    bool locked = false;
    bool expanded = true;
    bool maskEnabled = true;       // the layer mask is applied (the toggle beside the mask thumbnail); no mask: unused
    bool maskLinked = true;        // the mask moves with the layer (the link button below the toggle); no mask: unused
    LayerPaper paper;

    float opacityFraction() const { return static_cast<float>(std::clamp(opacity, 0, 100)) / 100.0f; }

    bool operator==(const LayerProps&) const = default;
};

// Value type: copying a node copies its tile pointers (copy-on-write), so a copy of the tree is a cheap undo snapshot. Children
// are ordered bottom → top.
struct LayerNode {
    LayerProps props;
    TiledImage pixels;
    std::vector<LayerNode> children;
    // The layer mask: a coverage per pixel (0 hides, kOne shows). It multiplies the layer's opacity.
    std::optional<SelectionMask> mask;
    std::optional<TextContent> text;

    const SelectionMask* activeMask() const { return mask && props.maskEnabled ? &*mask : nullptr; }

    bool isFolder() const { return props.kind == LayerKind::Folder; }
    bool isText() const { return props.kind == LayerKind::Text; }
};

// Multiplies an active mask into the layer's pixels and removes it (exact for modes that mix linearly, approximate for
// Fill-semantics ones). Used where an operation moves pixels and the mask cannot move along (Move, Transform).
void bakeMask(LayerNode& node);

// Layer tree of one document. The root is an implicit folder (id kNoLayer) that is never drawn as a layer. Pointers and
// references into the tree are invalidated by any structural change.
class LayerStack {
public:
    // Folders inside folders go this deep, in the program and in the .doodle and PSD files it opens.
    static constexpr int kMaxFolderLevels = 10;

    LayerStack() = default;
    explicit LayerStack(const QSize& size);

    QSize size() const { return m_size; }
    void setSize(const QSize& size) { m_size = size; }
    const LayerNode& root() const { return m_root; }
    LayerNode& root() { return m_root; }
    bool isEmpty() const { return m_root.children.empty(); }

    LayerNode* find(LayerId id);
    const LayerNode* find(LayerId id) const;
    LayerNode* parentOf(LayerId id);
    const LayerNode* parentOf(LayerId id) const;
    int indexOf(LayerId id) const;
    int depthOf(LayerId id) const;
    bool isAncestor(LayerId ancestor, LayerId id) const;
    // Whether `levels` more levels of folders fit inside `parent` (kNoLayer: the top level).
    bool hasRoomForFolders(LayerId parent, int levels = 1) const { return depthOf(parent) + 1 + levels <= kMaxFolderLevels; }
    // How many levels of folders `node` is: 0 for a layer, 1 for a folder of layers, and so on.
    static int folderLevels(const LayerNode& node);

    LayerId allocateId() { return LayerId(m_nextId++); }
    // The id the next allocateId() hands out. A file reader restores it together with the ids it reads, so layers made
    std::uint32_t nextIdValue() const { return m_nextId; }
    void setNextIdValue(std::uint32_t next) { m_nextId = next; }
    LayerNode makeRaster(const QString& name);
    LayerNode makeFolder(const QString& name);
    LayerNode makeText(const QString& name, TextContent content);  // pixels not drawn yet: see layer_ops::addTextLayer

    LayerId insert(LayerNode node, LayerId parent, int index);
    LayerNode take(LayerId id);
    // Moves `id` into folder `parent` at `index` (index in the parent after removal). False if the move is invalid (unknown
    // layer, parent is not a folder, or parent is inside `id`).
    bool move(LayerId id, LayerId parent, int index);

    std::vector<const LayerNode*> displayOrder(bool includeCollapsedChildren = true) const;
    LayerId below(LayerId id) const;
    LayerId above(LayerId id) const;
    void forEach(const std::function<void(const LayerNode&)>& f) const;
    int layerCount() const;
    QString nextLayerName() const;
    QString nextFolderName() const;
    QString nextTextName() const;

private:
    QSize m_size;
    LayerNode m_root;
    std::uint32_t m_nextId = 1;
};

}
