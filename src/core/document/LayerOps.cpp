#include "core/document/LayerOps.h"

#include "core/layers/Compositor.h"
#include "core/selection/SelectionOps.h"
#include "core/transform/Resample.h"
#include "core/text/TextLayout.h"

#include <QCoreApplication>
#include <QGuiApplication>

#include <algorithm>
#include <optional>

namespace doodlebold::layer_ops {

namespace {

using Failure = EditResult<>;

QString tr(const char* text) {
    return QCoreApplication::translate("LayerOps", text);
}

std::pair<LayerId, int> slotAboveCurrent(const DocumentState& s) {
    const LayerNode* parent = s.layers.parentOf(s.current);
    if (!parent)
        return {kNoLayer, -1};
    return {parent->props.id, s.layers.indexOf(s.current) + 1};
}

void reassignIds(LayerStack& stack, LayerNode& n) {
    n.props.id = stack.allocateId();
    for (LayerNode& c : n.children)
        reassignIds(stack, c);
}

EditResult<LayerNode*> editableRaster(DocumentState& s, LayerId id) {
    LayerNode* n = s.layers.find(id);
    if (!n)
        return EditResult<LayerNode*>::failure(EditError::NoSuchLayer);
    if (n->isFolder())
        return EditResult<LayerNode*>::failure(EditError::NotARasterLayer);
    if (n->isText())
        return EditResult<LayerNode*>::failure(EditError::NotForThisLayer);  // pixel edits on text layers are refused
    if (n->props.locked)
        return EditResult<LayerNode*>::failure(EditError::LayerLocked);
    return n;
}

}

EditResult<LayerId> addLayer(DocumentState& s) {
    const auto [parent, index] = slotAboveCurrent(s);
    s.current = s.layers.insert(s.layers.makeRaster(s.layers.nextLayerName()), parent, index);
    return s.current;
}

EditResult<LayerId> addFolder(DocumentState& s) {
    const auto [parent, index] = slotAboveCurrent(s);
    if (!s.layers.hasRoomForFolders(parent))
        return EditResult<LayerId>::failure(EditError::FoldersTooDeep);
    s.current = s.layers.insert(s.layers.makeFolder(s.layers.nextFolderName()), parent, index);
    return s.current;
}

EditResult<LayerId> duplicate(DocumentState& s, LayerId id) {
    const LayerNode* src = s.layers.find(id);
    if (!src)
        return EditResult<LayerId>::failure(EditError::NoSuchLayer);
    LayerNode copy = *src;  // shares tiles until either side is painted
    reassignIds(s.layers, copy);
    copy.props.name = src->props.name + tr(" copy");
    const LayerId parent = s.layers.parentOf(id)->props.id;
    s.current = s.layers.insert(std::move(copy), parent, s.layers.indexOf(id) + 1);
    return s.current;
}

EditResult<> remove(DocumentState& s, LayerId id) {
    if (!s.layers.find(id))
        return Failure::failure(EditError::NoSuchLayer);
    // Prefer the layer below as the next current one, unless that is inside the folder being removed.
    LayerId next = s.layers.below(id);
    if (!next || s.layers.isAncestor(id, next))
        next = s.layers.above(id);
    s.layers.take(id);
    // A folder is not a layer to paint on: at least one real layer stays.
    bool layerLeft = false;
    s.layers.forEach([&layerLeft](const LayerNode& n) { layerLeft = layerLeft || !n.isFolder(); });
    if (!layerLeft)
        return Failure::failure(EditError::LastLayer);
    s.current = next;
    return {};
}

namespace {

EditResult<LayerNode*> mergeIntoLower(DocumentState& s, LayerId id) {
    using R = EditResult<LayerNode*>;
    LayerNode* upper = s.layers.find(id);
    LayerNode* parent = s.layers.parentOf(id);
    if (!upper || !parent)
        return R::failure(EditError::NoSuchLayer);
    const int index = s.layers.indexOf(id);
    if (index <= 0)
        return R::failure(EditError::NothingBelow);
    LayerNode& lower = parent->children[static_cast<std::size_t>(index - 1)];
    if (lower.isFolder())
        return R::failure(EditError::NotARasterLayer);
    if (lower.isText())
        return R::failure(EditError::NotForThisLayer);  // merging would paint over its text
    if (lower.props.locked)
        return R::failure(EditError::LayerLocked);
    // The lower layer's own mode and opacity remain properties; its pixels enter the merge as Normal 100 %. A clipped upper
    // layer stays clipped to it, which is exactly what merging into the clipping base means.
    LayerNode base = lower;
    base.props.mode = BlendMode::Normal;
    base.props.opacity = 100;
    base.props.visible = true;
    base.props.clipping = false;
    const LayerNode top = *upper;
    lower.pixels = Compositor::flatten({&base, &top}, s.layers.size());
    // The merged layer has no paper (its pixels equal
    // the canvas as it showed before the merge).
    lower.props.paper = LayerPaper{};
    // The flatten drew the lower layer through its own mask, so the result carries it already (a switched-off mask was not
    // drawn and stays).
    if (lower.activeMask())
        lower.mask.reset();
    return &lower;
}

bool removeVisible(LayerNode& folder) {
    bool removed = false;
    std::vector<LayerNode> kept;
    for (LayerNode& c : folder.children) {
        if (!c.props.visible) {
            kept.push_back(std::move(c));
            continue;
        }
        if (c.isFolder()) {
            removed = removeVisible(c) || removed;
            if (!c.children.empty()) {
                kept.push_back(std::move(c));
                continue;
            }
        }
        removed = true;
    }
    folder.children = std::move(kept);
    return removed;
}

TiledImage visibleComposite(const DocumentState& s) {
    std::vector<const LayerNode*> top;
    for (const LayerNode& n : s.layers.root().children)
        top.push_back(&n);
    return Compositor::flatten(top, s.layers.size());
}

}

namespace {

// Bottom to top; what is inside a selected folder is left out, it goes with the folder.
void collectTopMost(const LayerNode& folder, const std::vector<LayerId>& ids, std::vector<LayerId>& out) {
    for (const LayerNode& c : folder.children) {
        if (std::ranges::find(ids, c.props.id) != ids.end())
            out.push_back(c.props.id);
        else
            collectTopMost(c, ids, out);
    }
}

}

EditResult<> removeLayers(DocumentState& s, const std::vector<LayerId>& ids) {
    std::vector<LayerId> parts;
    collectTopMost(s.layers.root(), ids, parts);
    if (parts.empty())
        return Failure::failure(EditError::NoSuchLayer);
    for (const LayerId id : parts)
        if (const auto removed = remove(s, id); !removed)
            return removed;
    s.maskTarget = false;
    return {};
}

EditResult<> moveLayers(DocumentState& s, const std::vector<LayerId>& ids, LayerId parent, int index) {
    std::vector<LayerId> parts;
    collectTopMost(s.layers.root(), ids, parts);
    if (parts.empty())
        return Failure::failure(EditError::NoSuchLayer);
    const LayerNode* target = parent == kNoLayer ? &s.layers.root() : s.layers.find(parent);
    if (!target || !target->isFolder())
        return Failure::failure(EditError::InvalidMove);
    for (const LayerId id : parts)
        if (id == parent || (parent != kNoLayer && s.layers.isAncestor(id, parent)))
            return Failure::failure(EditError::InvalidMove);
    for (const LayerId id : parts)
        if (!s.layers.hasRoomForFolders(parent, LayerStack::folderLevels(*s.layers.find(id))))
            return Failure::failure(EditError::FoldersTooDeep);
    const std::vector<const LayerNode*> before = s.layers.displayOrder();
    std::vector<LayerId> orderBefore;
    orderBefore.reserve(before.size());
    for (const LayerNode* node : before)
        orderBefore.push_back(node->props.id);

    std::vector<LayerNode> moved;
    moved.reserve(parts.size());
    for (const LayerId id : parts)
        moved.push_back(s.layers.take(id));
    for (std::size_t i = 0; i < moved.size(); ++i)
        s.layers.insert(std::move(moved[i]), parent, index + static_cast<int>(i));

    std::vector<LayerId> orderAfter;
    for (const LayerNode* node : s.layers.displayOrder())
        orderAfter.push_back(node->props.id);
    if (orderAfter == orderBefore)
        return Failure::failure(EditError::NoChange);
    return {};
}

EditResult<> clearLayers(DocumentState& s, const std::vector<LayerId>& ids) {
    bool cleared = false;
    std::optional<EditError> refusal;
    for (const LayerId id : ids) {
        const auto result = clear(s, id);
        cleared = cleared || result.ok();
        if (!result && (!refusal || *refusal == EditError::NoChange))
            refusal = result.error();
    }
    if (!cleared)
        return Failure::failure(refusal.value_or(EditError::NoChange));
    return {};
}

EditResult<LayerId> mergeLayers(DocumentState& s, const std::vector<LayerId>& ids) {
    using R = EditResult<LayerId>;
    std::vector<LayerId> parts;
    collectTopMost(s.layers.root(), ids, parts);
    if (parts.size() < 2)
        return R::failure(EditError::NoChange);
    std::vector<LayerNode> copies;
    for (const LayerId id : parts) {
        const LayerNode* node = s.layers.find(id);
        if (node->props.locked)
            return R::failure(EditError::LayerLocked);
        copies.push_back(*node);
        copies.back().props.visible = true;
    }
    // As in mergeIntoLower: the lowest one's own mode and opacity stay properties of the result.
    LayerNode& base = copies.front();
    base.props.mode = BlendMode::Normal;
    base.props.opacity = 100;
    base.props.clipping = false;
    std::vector<const LayerNode*> order;
    order.reserve(copies.size());
    for (const LayerNode& copy : copies)
        order.push_back(&copy);
    TiledImage merged = Compositor::flatten(order, s.layers.size());

    for (std::size_t i = 1; i < parts.size(); ++i)
        s.layers.take(parts[i]);
    LayerNode* result = s.layers.find(parts.front());
    result->props.kind = LayerKind::Raster;
    if (result->props.mode == BlendMode::PassThrough)
        result->props.mode = BlendMode::Normal;
    result->props.paper = LayerPaper{};
    result->pixels = std::move(merged);
    result->children.clear();
    result->text.reset();
    if (result->activeMask())
        result->mask.reset();
    s.current = parts.front();
    s.maskTarget = false;
    return parts.front();
}

EditResult<> mergeDown(DocumentState& s, LayerId id) {
    const auto lower = mergeIntoLower(s, id);
    if (!lower)
        return Failure::failure(lower.error());
    s.current = lower.value()->props.id;
    s.layers.take(id);
    return {};
}

EditResult<> transferDown(DocumentState& s, LayerId id) {
    const LayerNode* upper = s.layers.find(id);
    if (!upper)
        return Failure::failure(EditError::NoSuchLayer);
    if (upper->isFolder())
        return Failure::failure(EditError::NotARasterLayer);
    if (upper->isText())
        return Failure::failure(EditError::NotForThisLayer);
    if (upper->props.locked)
        return Failure::failure(EditError::LayerLocked);
    if (upper->pixels.isEmpty())
        return Failure::failure(EditError::NoChange);
    const auto lower = mergeIntoLower(s, id);
    if (!lower)
        return Failure::failure(lower.error());
    s.layers.find(id)->pixels.clear();  // its mask (if any) stays with it
    s.current = id;
    return {};
}

EditResult<LayerId> mergeVisible(DocumentState& s) {
    TiledImage merged = visibleComposite(s);
    LayerNode node = s.layers.makeRaster(s.layers.nextLayerName());
    if (!removeVisible(s.layers.root()))
        return EditResult<LayerId>::failure(EditError::NoChange);
    node.pixels = std::move(merged);
    s.current = s.layers.insert(std::move(node), kNoLayer, -1);
    s.maskTarget = false;
    return s.current;
}

EditResult<LayerId> flatten(DocumentState& s) {
    TiledImage merged = visibleComposite(s);
    LayerNode node = s.layers.makeRaster(s.layers.nextLayerName());
    node.pixels = std::move(merged);
    s.layers.root().children.clear();
    s.current = s.layers.insert(std::move(node), kNoLayer, -1);
    s.maskTarget = false;
    return s.current;
}

namespace {

// Where the box `r` (inside `area`) lands when `area` is turned by `turn` and shifted by `offset`, as moveExactly moves its
// pixels: a quarter turn clockwise takes (x, y) to (left + bottom − y, top + x − left) of the turned area.
QRect movedBox(const QRect& r, const QRect& area, transform::Turn turn, const QPoint& offset) {
    const QRect whole = transform::movedArea(area, turn, offset);
    switch (turn) {
    case transform::Turn::Clockwise:
        return QRect(whole.left() + area.bottom() - r.bottom(), whole.top() + r.left() - area.left(), r.height(), r.width());
    case transform::Turn::CounterClockwise:
        return QRect(whole.left() + r.top() - area.top(), whole.top() + area.right() - r.right(), r.height(), r.width());
    default: return r.translated(offset);  // Trim: a plain move
    }
}

// Every layer, mask and the selection moved exactly (`area` turned by `turn`, shifted by `offset`) onto a canvas of `size`.
// Text layers keep their text upright: it moves with its drawn box.
void remapCanvas(DocumentState& s, const QRect& area, transform::Turn turn, const QPoint& offset, const QSize& size) {
    const auto walk = [&](auto&& self, LayerNode& folder) -> void {
        for (LayerNode& n : folder.children) {
            if (n.mask)
                n.mask = transform::moveExactly(*n.mask, area, turn, offset, size);
            if (n.isFolder()) {
                self(self, n);
            } else if (n.isText()) {
                const QRect ink = n.pixels.contentBounds(true);
                if (!ink.isEmpty())
                    n.text->origin += QPointF(movedBox(ink, area, turn, offset).topLeft() - ink.topLeft());
                n.pixels = TiledImage(size);
                renderText(n);
            } else {
                n.pixels = transform::moveExactly(n.pixels, area, turn, offset, size);
            }
        }
    };
    s.layers.setSize(size);
    walk(walk, s.layers.root());
    if (s.selection) {
        SelectionMask moved = transform::moveExactly(*s.selection, area, turn, offset, size);
        if (moved.isEmpty())
            s.selection.reset();
        else
            s.selection = std::move(moved);
    }
}

}

EditResult<> trimCanvas(DocumentState& s) {
    if (!s.selection)
        return Failure::failure(EditError::NoChange);
    const QRect bounds = s.selection->bounds(true);
    const QRect canvas(QPoint(0, 0), s.layers.size());
    if (bounds.isEmpty() || bounds == canvas)
        return Failure::failure(EditError::NoChange);
    remapCanvas(s, bounds, transform::Turn::None, -bounds.topLeft(), bounds.size());
    return {};
}

EditResult<> rotateCanvas(DocumentState& s, bool clockwise) {
    const QRect canvas(QPoint(0, 0), s.layers.size());
    const transform::Turn turn = clockwise ? transform::Turn::Clockwise : transform::Turn::CounterClockwise;
    // A quarter turn about the centre, then moved so that the turned canvas starts at (0, 0).
    const QRect turned = transform::movedArea(canvas, turn);
    remapCanvas(s, canvas, turn, -turned.topLeft(), turned.size());
    return {};
}

EditResult<> reverseCanvas(DocumentState& s, bool horizontal) {
    const QRect canvas(QPoint(0, 0), s.layers.size());
    const transform::Turn turn = horizontal ? transform::Turn::FlipHorizontal : transform::Turn::FlipVertical;
    const auto walk = [&](auto&& self, LayerNode& folder) -> void {
        for (LayerNode& n : folder.children) {
            if (n.mask)
                n.mask = transform::moveExactly(*n.mask, canvas, turn);
            if (n.isFolder()) {
                self(self, n);
            } else if (n.isText()) {
                // The text stays readable; it moves to the mirrored place.
                const QRect ink = n.pixels.contentBounds(true);
                if (!ink.isEmpty()) {
                    // The drawn text's box lands where its mirror image would: left edge at width − 1 − right.
                    const QPoint shift = horizontal ? QPoint(canvas.width() - 1 - ink.right() - ink.left(), 0)
                                                    : QPoint(0, canvas.height() - 1 - ink.bottom() - ink.top());
                    n.text->origin += QPointF(shift);
                    renderText(n);
                }
            } else {
                n.pixels = transform::moveExactly(n.pixels, canvas, turn);
            }
        }
    };
    walk(walk, s.layers.root());
    if (s.selection)
        s.selection = transform::moveExactly(*s.selection, canvas, turn);
    return {};
}

EditResult<> move(DocumentState& s, LayerId id, LayerId parent, int index) {
    const LayerNode* oldParent = s.layers.parentOf(id);
    if (!oldParent)
        return Failure::failure(EditError::NoSuchLayer);
    if (oldParent->props.id == parent && s.layers.indexOf(id) == index)
        return Failure::failure(EditError::NoChange);
    if (id != parent && !s.layers.isAncestor(id, parent) &&
        !s.layers.hasRoomForFolders(parent, LayerStack::folderLevels(*s.layers.find(id))))
        return Failure::failure(EditError::FoldersTooDeep);
    if (!s.layers.move(id, parent, index))
        return Failure::failure(EditError::InvalidMove);
    return {};
}

EditResult<> raise(DocumentState& s, LayerId id) {
    const LayerNode* p = s.layers.parentOf(id);
    if (!p)
        return Failure::failure(EditError::NoSuchLayer);
    const int i = s.layers.indexOf(id);
    if (i + 1 >= static_cast<int>(p->children.size()))
        return Failure::failure(EditError::NoChange);
    return move(s, id, p->props.id, i + 1);
}

EditResult<> lower(DocumentState& s, LayerId id) {
    const LayerNode* p = s.layers.parentOf(id);
    if (!p)
        return Failure::failure(EditError::NoSuchLayer);
    const int i = s.layers.indexOf(id);
    if (i <= 0)
        return Failure::failure(EditError::NoChange);
    return move(s, id, p->props.id, i - 1);
}

EditResult<> toFront(DocumentState& s, LayerId id) {
    const LayerNode* p = s.layers.parentOf(id);
    if (!p)
        return Failure::failure(EditError::NoSuchLayer);
    return move(s, id, p->props.id, static_cast<int>(p->children.size()) - 1);
}

EditResult<> toBack(DocumentState& s, LayerId id) {
    const LayerNode* p = s.layers.parentOf(id);
    if (!p)
        return Failure::failure(EditError::NoSuchLayer);
    return move(s, id, p->props.id, 0);
}

EditResult<> setProps(DocumentState& s, LayerId id, const LayerProps& props) {
    LayerNode* n = s.layers.find(id);
    if (!n)
        return Failure::failure(EditError::NoSuchLayer);
    LayerProps p = props;
    p.id = id;
    p.kind = n->props.kind;
    p.opacity = std::clamp(p.opacity, 0, 100);
    if (!n->isFolder() && p.mode == BlendMode::PassThrough)
        p.mode = BlendMode::Normal;
    // The paper's numbers stay on the slider's grid; folders have no paper (the controls are greyed).
    p.paper.intensity = LayerPaper::clampIntensity(p.paper.intensity);
    p.paper.scale = LayerPaper::clampScale(p.paper.scale);
    if (n->isFolder())
        p.paper = LayerPaper{};
    if (p == n->props)
        return Failure::failure(EditError::NoChange);
    n->props = p;
    return {};
}

EditResult<> clear(DocumentState& s, LayerId id) {
    const auto target = editableRaster(s, id);
    if (!target)
        return Failure::failure(target.error());
    TiledImage& pixels = target.value()->pixels;
    if (!s.selection) {
        if (pixels.isEmpty())
            return Failure::failure(EditError::NoChange);
        pixels.clear();
        return {};
    }
    bool touched = false;
    for (const auto& [coord, selTile] : s.selection->tiles()) {
        if (!pixels.tile(coord.x, coord.y))
            continue;
        touched = true;
        if (SelectionMask::isFull(selTile.get())) {
            pixels.setTile(coord.x, coord.y, nullptr);
            continue;
        }
        Tile* t = pixels.writableTile(coord.x, coord.y);
        for (std::size_t i = 0; i < std::size_t(SelectionTile::kValues); ++i)
            for (std::size_t c = 0; c < 4; ++c)
                t->px[i * 4 + c] = static_cast<Channel>(lerpChannel(t->px[i * 4 + c], 0, selTile->v[i]));
    }
    if (!touched)
        return Failure::failure(EditError::NoChange);
    pixels.squeeze();
    return {};
}

EditResult<> fill(DocumentState& s, LayerId id, const QColor& color) {
    const auto target = editableRaster(s, id);
    if (!target)
        return Failure::failure(target.error());
    LayerNode& n = *target.value();
    const int rgb[3] = {fromUnorm8(color.red()), fromUnorm8(color.green()), fromUnorm8(color.blue())};
    // Preserve Opacity: keep each pixel's coverage, replace its colour. A selection weights the change per pixel; no selection
    // is the same as coverage 1 everywhere.
    if (!s.selection) {
        if (!n.props.preserveOpacity) {
            n.pixels.fill(color);
            return {};
        }
        const TileMap tiles = n.pixels.tiles();  // copy: writableTile() modifies the map
        for (const auto& [coord, tile] : tiles) {
            if (!tile)
                continue;
            Tile* t = n.pixels.writableTile(coord.x, coord.y);
            for (std::size_t i = 0; i < static_cast<std::size_t>(Tile::kValues); i += 4) {
                const int a = t->px[i + 3];
                for (std::size_t c = 0; c < 3; ++c)
                    t->px[i + c] = static_cast<Channel>(mulChannel(rgb[c], a));
            }
        }
        return {};
    }
    const std::array<Channel, 4> target4 = TiledImage::premultiplied(color);
    for (const auto& [coord, selTile] : s.selection->tiles()) {
        if (n.props.preserveOpacity && !n.pixels.tile(coord.x, coord.y))
            continue;
        Tile* t = n.pixels.writableTile(coord.x, coord.y);
        for (std::size_t i = 0; i < std::size_t(SelectionTile::kValues); ++i) {
            Channel* p = &t->px[i * 4];
            const int w = selTile->v[i];
            if (n.props.preserveOpacity) {
                for (std::size_t c = 0; c < 3; ++c)
                    p[c] = static_cast<Channel>(lerpChannel(p[c], mulChannel(rgb[c], p[3]), w));
            } else {
                for (std::size_t c = 0; c < 4; ++c)
                    p[c] = static_cast<Channel>(lerpChannel(p[c], target4[c], w));
            }
        }
    }
    n.pixels.squeeze();
    return {};
}

void renderText(LayerNode& node) {
    // Fonts need a GUI application; without one (command-line tools, core tests) the pixels stay as they are.
    if (!node.text || !qobject_cast<QGuiApplication*>(QCoreApplication::instance()))
        return;
    node.pixels = TiledImage(node.pixels.size());
    TextLayout(*node.text).render(node.pixels);
}

namespace {

EditResult<LayerNode*> maskedLayer(DocumentState& s, LayerId id) {
    LayerNode* n = s.layers.find(id);
    if (!n)
        return EditResult<LayerNode*>::failure(EditError::NoSuchLayer);
    if (!n->mask)
        return EditResult<LayerNode*>::failure(EditError::NoLayerMask);
    if (n->props.locked)
        return EditResult<LayerNode*>::failure(EditError::LayerLocked);
    return n;
}

EditResult<> setMaskValue(DocumentState& s, LayerId id, int value) {
    const auto target = maskedLayer(s, id);
    if (!target)
        return Failure::failure(target.error());
    SelectionMask& mask = *target.value()->mask;
    const SelectionMask before = mask;
    if (!s.selection) {
        for (int ty = 0; ty < mask.tilesY(); ++ty)
            for (int tx = 0; tx < mask.tilesX(); ++tx)
                mask.setUniform(tx, ty, value);
    } else {
        std::array<std::uint16_t, SelectionTile::kSize> sel{}, row{};
        for (const auto& [coord, selTile] : s.selection->tiles()) {
            const QRect r = SelectionMask::tileRect(coord.x, coord.y).intersected(mask.rect());
            for (int y = r.top(); y <= r.bottom(); ++y) {
                s.selection->row(y, r.left(), r.width(), sel.data());
                mask.row(y, r.left(), r.width(), row.data());
                for (int i = 0; i < r.width(); ++i)
                    row[std::size_t(i)] = std::uint16_t(lerpChannel(row[std::size_t(i)], value, sel[std::size_t(i)]));
                mask.writeRow(y, r.left(), r.width(), row.data());
            }
        }
        mask.squeeze();
    }
    if (mask == before)
        return Failure::failure(EditError::NoChange);
    return {};
}

}

int maskValueOf(const QColor& color) {
    return int(std::lround(0.299 * color.red() + 0.587 * color.green() + 0.114 * color.blue()));
}

EditResult<> addMask(DocumentState& s, LayerId id) {
    LayerNode* n = s.layers.find(id);
    if (!n)
        return Failure::failure(EditError::NoSuchLayer);
    if (n->mask)
        return Failure::failure(EditError::HasLayerMask);
    n->mask = selection::all(s.layers.size());
    n->props.maskEnabled = true;
    n->props.maskLinked = true;
    s.current = id;
    s.maskTarget = true;
    return {};
}

EditResult<> applyMask(DocumentState& s, LayerId id) {
    const auto target = maskedLayer(s, id);
    if (!target)
        return Failure::failure(target.error());
    LayerNode& n = *target.value();
    if (n.isFolder())
        return Failure::failure(EditError::NotARasterLayer);
    if (n.isText())
        return Failure::failure(EditError::NotForThisLayer);
    bakeMask(n);  // only a switched-on mask changes the pixels (what the canvas showed stays)
    n.mask.reset();
    if (s.current == id)
        s.maskTarget = false;
    return {};
}

EditResult<> removeMask(DocumentState& s, LayerId id) {
    LayerNode* n = s.layers.find(id);
    if (!n)
        return Failure::failure(EditError::NoSuchLayer);
    if (!n->mask)
        return Failure::failure(EditError::NoLayerMask);
    n->mask.reset();
    if (s.current == id)
        s.maskTarget = false;
    return {};
}

EditResult<> clearMask(DocumentState& s, LayerId id) {
    return setMaskValue(s, id, 0);
}

EditResult<> fillMask(DocumentState& s, LayerId id, const QColor& color) {
    return setMaskValue(s, id, fromUnorm8(maskValueOf(color)));
}

EditResult<LayerId> addTextLayer(DocumentState& s, const TextContent& content) {
    const auto [parent, index] = slotAboveCurrent(s);
    LayerNode node = s.layers.makeText(s.layers.nextTextName(), content);
    renderText(node);
    s.current = s.layers.insert(std::move(node), parent, index);
    return s.current;
}

EditResult<> setText(DocumentState& s, LayerId id, const TextContent& content) {
    LayerNode* n = s.layers.find(id);
    if (!n)
        return Failure::failure(EditError::NoSuchLayer);
    if (!n->isText())
        return Failure::failure(EditError::NotForThisLayer);
    if (n->props.locked)
        return Failure::failure(EditError::LayerLocked);
    if (n->text == content)
        return Failure::failure(EditError::NoChange);
    n->text = content;
    renderText(*n);
    return {};
}

EditResult<> rasterize(DocumentState& s, LayerId id) {
    LayerNode* n = s.layers.find(id);
    if (!n)
        return Failure::failure(EditError::NoSuchLayer);
    if (!n->isText())
        return Failure::failure(EditError::NoChange);
    n->props.kind = LayerKind::Raster;
    n->text.reset();
    return {};
}

}
