#include "core/layers/LayerStack.h"

#include <QSet>

#include <algorithm>
#include <array>
#include <ranges>

namespace doodlebold {

void bakeMask(LayerNode& node) {
    const SelectionMask* mask = node.activeMask();
    if (!mask || node.isFolder())
        return;
    const TileMap tiles = node.pixels.tiles();  // copy: writableTile() changes the map
    std::array<std::uint16_t, Tile::kSize> row{};
    for (const auto& [coord, tile] : tiles) {
        if (!tile)
            continue;
        Tile* t = node.pixels.writableTile(coord.x, coord.y);
        for (int y = 0; y < Tile::kSize; ++y) {
            mask->row(coord.y * Tile::kSize + y, coord.x * Tile::kSize, Tile::kSize, row.data());
            for (int x = 0; x < Tile::kSize; ++x) {
                const unsigned m = row[static_cast<std::size_t>(x)];
                Channel* p = t->at(x, y);
                for (int c = 0; c < 4; ++c)
                    p[c] = static_cast<Channel>(mulChannel(p[c], int(m)));
            }
        }
    }
    node.pixels.squeeze();
    node.mask.reset();
}

namespace {

LayerNode* findIn(LayerNode& n, LayerId id, LayerNode** parent) {
    for (LayerNode& c : n.children) {
        if (c.props.id == id) {
            if (parent)
                *parent = &n;
            return &c;
        }
        if (c.isFolder())
            if (LayerNode* r = findIn(c, id, parent))
                return r;
    }
    return nullptr;
}

void collectDisplay(const LayerNode& folder, bool includeCollapsed, std::vector<const LayerNode*>& out) {
    for (const auto& it : std::views::reverse(folder.children)) {
        out.push_back(&it);
        if (it.isFolder() && (includeCollapsed || it.props.expanded))
            collectDisplay(it, includeCollapsed, out);
    }
}

void forEachIn(const LayerNode& n, const std::function<void(const LayerNode&)>& f) {
    for (const LayerNode& c : n.children) {
        f(c);
        if (c.isFolder())
            forEachIn(c, f);
    }
}

}

LayerStack::LayerStack(const QSize& size) : m_size(size) {
    m_root.props.kind = LayerKind::Folder;
    m_root.props.mode = BlendMode::PassThrough;
}

LayerNode* LayerStack::find(LayerId id) {
    if (id == kNoLayer)
        return nullptr;
    return findIn(m_root, id, nullptr);
}

const LayerNode* LayerStack::find(LayerId id) const {
    return const_cast<LayerStack*>(this)->find(id);
}

LayerNode* LayerStack::parentOf(LayerId id) {
    LayerNode* parent = nullptr;
    return findIn(m_root, id, &parent) ? parent : nullptr;
}

const LayerNode* LayerStack::parentOf(LayerId id) const {
    return const_cast<LayerStack*>(this)->parentOf(id);
}

int LayerStack::indexOf(LayerId id) const {
    const LayerNode* p = parentOf(id);
    if (!p)
        return -1;
    for (std::size_t i = 0; i < p->children.size(); ++i)
        if (p->children[i].props.id == id)
            return static_cast<int>(i);
    return -1;
}

int LayerStack::depthOf(LayerId id) const {
    int depth = -1;
    for (const LayerNode* p = parentOf(id); p; p = p == &m_root ? nullptr : parentOf(p->props.id))
        ++depth;
    return depth;
}

int LayerStack::folderLevels(const LayerNode& node) {
    if (!node.isFolder())
        return 0;
    int deepest = 0;
    for (const LayerNode& c : node.children)
        deepest = std::max(deepest, folderLevels(c));
    return deepest + 1;
}

bool LayerStack::isAncestor(LayerId ancestor, LayerId id) const {
    for (const LayerNode* p = parentOf(id); p && p != &m_root; p = parentOf(p->props.id))
        if (p->props.id == ancestor)
            return true;
    return false;
}

LayerNode LayerStack::makeRaster(const QString& name) {
    LayerNode n;
    n.props.id = allocateId();
    n.props.kind = LayerKind::Raster;
    n.props.name = name;
    n.pixels = TiledImage(m_size);
    return n;
}

LayerNode LayerStack::makeFolder(const QString& name) {
    LayerNode n;
    n.props.id = allocateId();
    n.props.kind = LayerKind::Folder;
    n.props.name = name;
    n.props.mode = BlendMode::Normal;
    return n;
}

LayerNode LayerStack::makeText(const QString& name, TextContent content) {
    LayerNode n;
    n.props.id = allocateId();
    n.props.kind = LayerKind::Text;
    n.props.name = name;
    n.pixels = TiledImage(m_size);
    n.text = std::move(content);
    return n;
}

LayerId LayerStack::insert(LayerNode node, LayerId parent, int index) {
    LayerNode* p = parent == kNoLayer ? &m_root : find(parent);
    if (!p || !p->isFolder())
        p = &m_root;
    if (node.props.id == kNoLayer)
        node.props.id = allocateId();
    m_nextId = std::max(m_nextId, node.props.id.value() + 1);
    const int n = static_cast<int>(p->children.size());
    const int at = (index < 0 || index > n) ? n : index;
    const LayerId id = node.props.id;
    p->children.insert(p->children.begin() + at, std::move(node));
    return id;
}

LayerNode LayerStack::take(LayerId id) {
    LayerNode* p = parentOf(id);
    if (!p)
        return {};
    auto it = std::ranges::find_if(p->children, [id](const LayerNode& c) { return c.props.id == id; });
    LayerNode out = std::move(*it);
    p->children.erase(it);
    return out;
}

bool LayerStack::move(LayerId id, LayerId parent, int index) {
    if (!find(id) || id == parent || (parent != kNoLayer && isAncestor(id, parent)))
        return false;
    const LayerNode* target = parent == kNoLayer ? &m_root : find(parent);
    if (!target || !target->isFolder())
        return false;
    insert(take(id), parent, index);
    return true;
}

std::vector<const LayerNode*> LayerStack::displayOrder(bool includeCollapsedChildren) const {
    std::vector<const LayerNode*> out;
    collectDisplay(m_root, includeCollapsedChildren, out);
    return out;
}

LayerId LayerStack::below(LayerId id) const {
    const auto order = displayOrder();
    for (std::size_t i = 0; i + 1 < order.size(); ++i)
        if (order[i]->props.id == id)
            return order[i + 1]->props.id;
    return kNoLayer;
}

LayerId LayerStack::above(LayerId id) const {
    const auto order = displayOrder();
    for (std::size_t i = 1; i < order.size(); ++i)
        if (order[i]->props.id == id)
            return order[i - 1]->props.id;
    return kNoLayer;
}

void LayerStack::forEach(const std::function<void(const LayerNode&)>& f) const {
    forEachIn(m_root, f);
}

int LayerStack::layerCount() const {
    int n = 0;
    forEach([&n](const LayerNode&) { ++n; });
    return n;
}

QString LayerStack::nextLayerName() const {
    QSet<QString> used;
    forEach([&used](const LayerNode& n) { used.insert(n.props.name); });
    for (int i = 1;; ++i) {
        const QString name = QStringLiteral("Layer%1").arg(i);
        if (!used.contains(name))
            return name;
    }
}

QString LayerStack::nextTextName() const {
    QSet<QString> used;
    forEach([&used](const LayerNode& n) { used.insert(n.props.name); });
    for (int i = 1;; ++i) {
        const QString name = QStringLiteral("Text%1").arg(i);
        if (!used.contains(name))
            return name;
    }
}

QString LayerStack::nextFolderName() const {
    QSet<QString> used;
    forEach([&used](const LayerNode& n) { used.insert(n.props.name); });
    for (int i = 1;; ++i) {
        const QString name = QStringLiteral("Folder%1").arg(i);
        if (!used.contains(name))
            return name;
    }
}

}
