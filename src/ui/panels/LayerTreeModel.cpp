#include "ui/panels/LayerTreeModel.h"

#include "core/document/Document.h"

#include <QMimeData>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <ranges>
#include <utility>

namespace doodlebold {

namespace {

constexpr char kLayerMime[] = "application/x-doodlebold-layer-id";

// Rows of a folder run top → bottom while its children are stored bottom → top, so row r of n is child n-1-r.
int rowOfChild(int childIndex, int count) {
    return count - 1 - childIndex;
}

std::size_t toSize(int i) {
    return static_cast<std::size_t>(i);
}

void collectShape(const LayerNode& folder, LayerId folderId, std::vector<std::pair<LayerId, LayerId>>& out) {
    for (const auto& it : std::views::reverse(folder.children)) {
        out.emplace_back(it.props.id, folderId);
        if (it.isFolder())
            collectShape(it, it.props.id, out);
    }
}

QImage maskPicture(const SelectionMask& mask, int maxSide) {
    const QSize full = mask.size();
    const double scale = std::min(1.0, double(maxSide) / std::max(full.width(), full.height()));
    const QSize size(std::max(1, int(std::lround(full.width() * scale))), std::max(1, int(std::lround(full.height() * scale))));
    QImage out(size, QImage::Format_Grayscale8);
    const double sx = double(full.width()) / size.width(), sy = double(full.height()) / size.height();
    for (int y = 0; y < size.height(); ++y) {
        uchar* line = out.scanLine(y);
        for (int x = 0; x < size.width(); ++x) {
            int sum = 0;
            for (int j = 0; j < 4; ++j)
                for (int i = 0; i < 4; ++i)
                    sum += mask.at(int((x + (i + 0.5) / 4) * sx), int((y + (j + 0.5) / 4) * sy));
            line[x] = uchar((sum / 16 * 255 + SelectionMask::kOne / 2) / SelectionMask::kOne);
        }
    }
    return out;
}

std::vector<LayerId> layersFromMime(const QMimeData* data) {
    std::vector<LayerId> ids;
    if (!data || !data->hasFormat(QString::fromLatin1(kLayerMime)))
        return ids;
    for (const QByteArray& part : data->data(QString::fromLatin1(kLayerMime)).split(',')) {
        bool ok = false;
        const uint value = part.toUInt(&ok);
        if (ok)
            ids.emplace_back(value);
    }
    return ids;
}

}

LayerTreeModel::LayerTreeModel(QObject* parent) : QAbstractItemModel(parent) {
    m_thumbTimer.setSingleShot(true);
    m_thumbTimer.setInterval(kThumbDelayMs);
    connect(&m_thumbTimer, &QTimer::timeout, this, &LayerTreeModel::flushThumbnails);
}

void LayerTreeModel::setDocument(Document* doc) {
    if (doc && doc == m_doc.data())
        return;
    beginResetModel();
    if (m_doc)
        m_doc->disconnect(this);
    m_doc = doc;
    m_shape.clear();
    m_thumbs.clear();
    m_maskThumbs.clear();
    m_dirtyThumbs.clear();
    m_thumbTimer.stop();
    m_current = kNoLayer;
    if (doc) {
        connect(doc, &Document::layersChanged, this, &LayerTreeModel::onLayersChanged);
        connect(doc, &Document::currentLayerChanged, this, &LayerTreeModel::onCurrentLayerChanged);
        connect(doc, &Document::layerPixelsChanged, this, &LayerTreeModel::onLayerPixelsChanged);
        connect(doc, &Document::maskTargetChanged, this, &LayerTreeModel::onMaskTargetChanged);
        // Opening the timeline or adding frames shows the share marks; sharing changes them.
        connect(doc, &Document::animationChanged, this, [this] {
            if (const int rows = rowCount(); rows > 0)
                emit dataChanged(index(0, 0), index(rows - 1, 0), {ShareableRole, SharedRole});
        });
        connect(doc, &QObject::destroyed, this, [this] { setDocument(nullptr); });
        m_current = doc->currentLayerId();
        collectShape(doc->layers().root(), kNoLayer, m_shape);
    }
    endResetModel();
}

LayerId LayerTreeModel::layerAt(const QModelIndex& index) const {
    return index.isValid() ? LayerId(static_cast<std::uint32_t>(index.internalId())) : kNoLayer;
}

QModelIndex LayerTreeModel::indexOf(LayerId id) const {
    if (!m_doc || !id)
        return {};
    const LayerNode* folder = m_doc->layers().parentOf(id);
    if (!folder)
        return {};
    const int count = static_cast<int>(folder->children.size());
    return createIndex(rowOfChild(m_doc->layers().indexOf(id), count), 0, static_cast<quintptr>(id.value()));
}

QModelIndex LayerTreeModel::index(int row, int column, const QModelIndex& parent) const {
    if (!m_doc || column != 0 || row < 0)
        return {};
    const LayerNode* folder = parent.isValid() ? m_doc->layer(layerAt(parent)) : &m_doc->layers().root();
    if (!folder || !folder->isFolder())
        return {};
    const int count = static_cast<int>(folder->children.size());
    if (row >= count)
        return {};
    const LayerId id = folder->children[toSize(rowOfChild(row, count))].props.id;
    return createIndex(row, 0, static_cast<quintptr>(id.value()));
}

QModelIndex LayerTreeModel::parent(const QModelIndex& child) const {
    if (!m_doc || !child.isValid())
        return {};
    const LayerNode* folder = m_doc->layers().parentOf(layerAt(child));
    if (!folder || folder == &m_doc->layers().root())
        return {};
    return indexOf(folder->props.id);
}

int LayerTreeModel::rowCount(const QModelIndex& parent) const {
    if (!m_doc || parent.column() > 0)
        return 0;
    const LayerNode* folder = parent.isValid() ? m_doc->layer(layerAt(parent)) : &m_doc->layers().root();
    return folder && folder->isFolder() ? static_cast<int>(folder->children.size()) : 0;
}

int LayerTreeModel::columnCount(const QModelIndex&) const {
    return 1;
}

QVariant LayerTreeModel::data(const QModelIndex& index, int role) const {
    if (!m_doc || !index.isValid())
        return {};
    const LayerNode* node = m_doc->layer(layerAt(index));
    if (!node)
        return {};
    const LayerProps& p = node->props;
    switch (role) {
    case Qt::DisplayRole:
    case Qt::EditRole: return p.name;
    case Qt::DecorationRole:
        if (node->isFolder())
            return {};
        return thumbnail(p.id);
    case LayerIdRole: return static_cast<quint32>(p.id.value());
    case VisibleRole: return p.visible;
    case IsFolderRole: return node->isFolder();
    case ClippingRole: return p.clipping;
    case LockedRole: return p.locked;
    case PreserveOpacityRole: return p.preserveOpacity;
    case ModeTextRole: return QStringLiteral("%1 %2%").arg(blendModeName(p.mode)).arg(p.opacity);
    case PaperRole: return !node->isFolder() && p.paper.isSet();
    case IsCurrentRole: return p.id == m_doc->currentLayerId();
    case IsTextRole: return node->isText();
    case HasMaskRole: return node->mask.has_value();
    case MaskEnabledRole: return p.maskEnabled;
    case MaskLinkedRole: return p.maskLinked;
    case MaskThumbRole: return node->mask ? maskThumbnail(p.id) : QImage();
    case MaskTargetRole: return p.id == m_doc->currentLayerId() && m_doc->maskTarget();
    case ShareableRole: {
        const Animation& a = m_doc->animation();
        return (a.isAnimation() || a.view.timelineShown) && !index.parent().isValid();
    }
    case SharedRole: return m_doc->animation().isShared(p.id);
    default: return {};
    }
}

bool LayerTreeModel::setData(const QModelIndex& index, const QVariant& value, int role) {
    if (!m_doc || !index.isValid())
        return false;
    const LayerNode* node = m_doc->layer(layerAt(index));
    if (!node)
        return false;
    LayerProps props = node->props;
    if (role == Qt::EditRole) {
        const QString name = value.toString().trimmed();
        if (name.isEmpty()) {
            emit editRefused(tr("A layer needs a name."));
            return false;
        }
        props.name = name;
    } else if (role == VisibleRole) {
        props.visible = value.toBool();
    } else if (role == MaskEnabledRole && node->mask) {
        props.maskEnabled = value.toBool();
    } else if (role == MaskLinkedRole && node->mask) {
        props.maskLinked = value.toBool();
    } else if (role == SharedRole) {
        const auto shared = m_doc->setLayerShared(props.id, value.toBool());
        if (!shared && shared.error() != EditError::NoChange)
            refuse(shared.error());
        return shared.ok();
    } else {
        return false;
    }
    const auto result = m_doc->setLayerProps(props.id, props);
    if (!result)
        refuse(result.error());
    return result.ok();
}

Qt::ItemFlags LayerTreeModel::flags(const QModelIndex& index) const {
    if (!index.isValid())
        return Qt::ItemIsDropEnabled;
    Qt::ItemFlags f = Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsEditable | Qt::ItemIsDragEnabled;
    if (data(index, IsFolderRole).toBool())
        f |= Qt::ItemIsDropEnabled;
    return f;
}

Qt::DropActions LayerTreeModel::supportedDropActions() const {
    return Qt::MoveAction;
}

Qt::DropActions LayerTreeModel::supportedDragActions() const {
    return Qt::MoveAction;
}

QStringList LayerTreeModel::mimeTypes() const {
    return {QString::fromLatin1(kLayerMime)};
}

QMimeData* LayerTreeModel::mimeData(const QModelIndexList& indexes) const {
    QList<QByteArray> ids;
    for (const QModelIndex& i : indexes)
        if (i.isValid())
            ids << QByteArray::number(layerAt(i).value());
    if (ids.isEmpty())
        return nullptr;
    auto* mime = new QMimeData;
    mime->setData(QString::fromLatin1(kLayerMime), ids.join(','));
    return mime;
}

bool LayerTreeModel::canDropMimeData(const QMimeData* mime, Qt::DropAction action, int, int, const QModelIndex& parent) const {
    if (!m_doc || (action != Qt::MoveAction && action != Qt::IgnoreAction))
        return false;
    const std::vector<LayerId> ids = layersFromMime(mime);
    if (ids.empty() || !std::ranges::all_of(ids, [this](LayerId id) { return m_doc->layer(id) != nullptr; }))
        return false;
    // A layer lands between rows or onto a folder. Whether a folder would land inside itself is the Document's
    // call, so that the user is told why.
    return !parent.isValid() || data(parent, IsFolderRole).toBool();
}

bool LayerTreeModel::dropMimeData(const QMimeData* mime, Qt::DropAction action, int row, int column, const QModelIndex& parent) {
    if (!canDropMimeData(mime, action, row, column, parent))
        return false;
    if (action == Qt::IgnoreAction)
        return true;

    const std::vector<LayerId> ids = layersFromMime(mime);
    const LayerId target = layerAt(parent);
    const LayerNode* folder = parent.isValid() ? m_doc->layer(target) : &m_doc->layers().root();
    int count = static_cast<int>(folder->children.size());
    int displayRow = row < 0 ? (parent.isValid() ? 0 : count) : std::min(row, count);

    // Document::moveLayers wants the index among the folder's children once the layers have been taken out. Within the
    // same folder that removal shifts every row below each of them up by one. With `count` rows left, layers that are
    // to appear at display row d have d rows above them and count-d below, which makes count-d their child index.
    const int rows = count;
    const int dropRow = displayRow;
    for (const LayerId id : ids) {
        if (m_doc->layers().parentOf(id) != folder)
            continue;
        if (dropRow > rowOfChild(m_doc->layers().indexOf(id), rows))
            --displayRow;
        --count;
    }
    const auto result = m_doc->moveLayers(ids, target, count - displayRow);
    if (!result)
        refuse(result.error());
    return result.ok();
}

void LayerTreeModel::onLayersChanged() {
    Shape shape;
    collectShape(m_doc->layers().root(), kNoLayer, shape);
    m_thumbs.clear();  // undo/redo and merges change pixels without a layerPixelsChanged
    m_maskThumbs.clear();
    if (shape == m_shape) {
        // Only properties changed: keep the rows, so the view keeps its expansion, scroll position and editor.
        emitRowsChanged(m_doc->layers().root(), {});
        return;
    }
    beginResetModel();
    m_shape = std::move(shape);
    m_dirtyThumbs.clear();
    endResetModel();
}

void LayerTreeModel::emitRowsChanged(const LayerNode& folder, const QModelIndex& parent) {
    const int count = static_cast<int>(folder.children.size());
    if (count == 0)
        return;
    emit dataChanged(index(0, 0, parent), index(count - 1, 0, parent));
    for (int row = 0; row < count; ++row) {
        const LayerNode& child = folder.children[toSize(rowOfChild(row, count))];
        if (child.isFolder())
            emitRowsChanged(child, index(row, 0, parent));
    }
}

void LayerTreeModel::onCurrentLayerChanged(LayerId id) {
    const LayerId old = m_current;
    m_current = id;
    for (const LayerId changed : {old, id}) {
        const QModelIndex i = indexOf(changed);
        if (i.isValid())
            emit dataChanged(i, i, {IsCurrentRole, MaskTargetRole});
    }
}

void LayerTreeModel::onMaskTargetChanged() {
    const QModelIndex i = indexOf(m_current);
    if (i.isValid())
        emit dataChanged(i, i, {MaskTargetRole});
}

const QImage& LayerTreeModel::thumbnail(LayerId id) const {
    auto it = m_thumbs.find(id.value());
    if (it == m_thumbs.end()) {
        const LayerNode* node = m_doc->layer(id);
        it = m_thumbs.insert(id.value(), node ? node->pixels.thumbnail(kThumbSize) : QImage());
    }
    return it.value();
}

const QImage& LayerTreeModel::maskThumbnail(LayerId id) const {
    auto it = m_maskThumbs.find(id.value());
    if (it == m_maskThumbs.end()) {
        const LayerNode* node = m_doc->layer(id);
        it = m_maskThumbs.insert(id.value(), node && node->mask ? maskPicture(*node->mask, kThumbSize) : QImage());
    }
    return it.value();
}

void LayerTreeModel::onLayerPixelsChanged(LayerId id) {
    m_dirtyThumbs.insert(id.value());
    // Not restarted by later events: a long stroke would otherwise never refresh the thumbnail until it ends.
    if (!m_thumbTimer.isActive())
        m_thumbTimer.start();
}

void LayerTreeModel::flushThumbnails() {
    const QSet<quint32> dirty = std::exchange(m_dirtyThumbs, {});
    for (const quint32 value : dirty) {
        const LayerId id(value);
        const LayerNode* node = m_doc ? m_doc->layer(id) : nullptr;
        if (!node)
            continue;
        if (!node->isFolder())
            m_thumbs.insert(value, node->pixels.thumbnail(kThumbSize));
        m_maskThumbs.remove(value);  // a stroke on the mask reports the layer too
        const QModelIndex i = indexOf(id);
        if (i.isValid())
            emit dataChanged(i, i, {Qt::DecorationRole, MaskThumbRole});
    }
}

void LayerTreeModel::refuse(EditError error) {
    if (error != EditError::NoChange)
        emit editRefused(describe(error));
}

}
