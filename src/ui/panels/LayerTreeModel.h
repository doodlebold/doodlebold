#pragma once

#include "core/document/EditError.h"
#include "core/layers/LayerId.h"

#include <QAbstractItemModel>
#include <QHash>
#include <QImage>
#include <QPointer>
#include <QSet>
#include <QTimer>

#include <utility>
#include <vector>

namespace doodlebold {

class Document;
struct LayerNode;

class LayerTreeModel : public QAbstractItemModel {
    Q_OBJECT
public:
    enum Role {
        LayerIdRole = Qt::UserRole,
        VisibleRole,
        IsFolderRole,
        ClippingRole,
        LockedRole,
        PreserveOpacityRole,
        ModeTextRole,
        IsCurrentRole,
        IsTextRole,
        HasMaskRole,
        MaskEnabledRole,
        MaskLinkedRole,
        MaskThumbRole,
        MaskTargetRole,
        PaperRole,
        // In an animation, a top-level layer is shared by every frame or belongs to the current frame only (the mark at the
        // row's right; setting it shares the layer or makes it per-frame again).
        ShareableRole,
        SharedRole,
    };

    static constexpr int kThumbSize = 40;  // component metric: a layer's thumbnail
    static constexpr int kThumbDelayMs = 150;

    explicit LayerTreeModel(QObject* parent = nullptr);

    void setDocument(Document* doc);
    Document* document() const { return m_doc.data(); }

    QModelIndex indexOf(LayerId id) const;
    LayerId layerAt(const QModelIndex& index) const;

    QModelIndex index(int row, int column, const QModelIndex& parent = {}) const override;
    QModelIndex parent(const QModelIndex& child) const override;
    int rowCount(const QModelIndex& parent = {}) const override;
    int columnCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    bool setData(const QModelIndex& index, const QVariant& value, int role = Qt::EditRole) override;
    Qt::ItemFlags flags(const QModelIndex& index) const override;

    Qt::DropActions supportedDropActions() const override;
    Qt::DropActions supportedDragActions() const override;
    QStringList mimeTypes() const override;
    QMimeData* mimeData(const QModelIndexList& indexes) const override;
    bool canDropMimeData(const QMimeData* mime, Qt::DropAction action, int row, int column, const QModelIndex& parent) const override;
    // `row` is a display row among the children of `parent` (an invalid parent is the top level). Row -1 means "onto
    // the item": the top of a folder, or the bottom of the top level when dropped on empty space.
    bool dropMimeData(const QMimeData* mime, Qt::DropAction action, int row, int column, const QModelIndex& parent) override;

signals:
    void editRefused(const QString& message);

private:
    using Shape = std::vector<std::pair<LayerId, LayerId>>;

    void onLayersChanged();
    void onCurrentLayerChanged(LayerId id);
    void onMaskTargetChanged();
    void onLayerPixelsChanged(LayerId id);
    void flushThumbnails();
    void emitRowsChanged(const LayerNode& folder, const QModelIndex& parent);
    const QImage& thumbnail(LayerId id) const;
    const QImage& maskThumbnail(LayerId id) const;
    void refuse(EditError error);

    QPointer<Document> m_doc;
    Shape m_shape;
    LayerId m_current;
    mutable QHash<quint32, QImage> m_thumbs;
    mutable QHash<quint32, QImage> m_maskThumbs;
    QSet<quint32> m_dirtyThumbs;
    QTimer m_thumbTimer;
};

}
