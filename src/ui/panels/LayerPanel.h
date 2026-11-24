#pragma once

#include "core/layers/LayerId.h"

#include <QList>
#include <QPointer>
#include <QWidget>

#include <functional>
#include <memory>
#include <unordered_map>
#include <vector>

class QCheckBox;
class QComboBox;
class QMenu;
class QModelIndex;
class QSlider;
class QSpinBox;
class QToolButton;

namespace doodlebold {

class Document;
class LayerTreeModel;
class LayerTreeView;
class ValueBar;
enum class EditError;
struct LayerNode;
struct LayerProps;

class LayerPanel : public QWidget {
    Q_OBJECT
public:
    explicit LayerPanel(QWidget* parent = nullptr);

    void setDocument(Document* doc);
    Document* document() const { return m_doc.data(); }
    void selectionFromLayer(LayerId id, Qt::KeyboardModifiers mods, bool mask = false);
    // The rows selected in the tree; the current layer is always one of them. View state, never written to the Document.
    const std::vector<LayerId>& selectedLayers() const { return m_selected; }

signals:
    void editRefused(const QString& message);

private:
    void onLayersChanged();
    void onCurrentLayerChanged();
    void onModelReset();
    void onTreeSelectionChanged(const QModelIndex& current);
    void onTreeRowsSelected();

    void syncControls();
    void rebuildModes(const LayerNode& layer);
    void selectCurrent(bool reveal);
    void restoreExpansion(const QModelIndex& parent);
    void rememberExpanded(const QModelIndex& folder, bool expanded);

    void editCurrent(const std::function<void(LayerProps&)>& change, int mergeKey = 0);
    void refuse(EditError error);

    QPointer<Document> m_doc;
    LayerTreeModel* m_model = nullptr;
    LayerTreeView* m_view = nullptr;
    QWidget* m_controls = nullptr;
    QToolButton* m_effectExpander = nullptr;
    QComboBox* m_modeCombo = nullptr;
    ValueBar* m_opacityBar = nullptr;
    QCheckBox* m_preserveCheck = nullptr;
    QCheckBox* m_clippingCheck = nullptr;
    QCheckBox* m_lockCheck = nullptr;
    QWidget* m_buttons = nullptr;
    QToolButton* m_newMaskButton = nullptr;
    QToolButton* m_applyMaskButton = nullptr;
    QToolButton* m_mergeButton = nullptr;
    QToolButton* m_deleteButton = nullptr;
    QList<QToolButton*> m_oneLayerButtons;  // off while several layers are selected
    std::vector<LayerId> m_selected;

    int m_modesKey = -1;
    int m_dragKey = 0;           // undo merge key of the running opacity slider drag; new for every drag
    bool m_dragging = false;
    bool m_syncing = false;      // selection or expansion is being set from the document, not by the user
    std::unordered_map<LayerId, bool> m_expanded;  // folder expansion: view state, never written to the Document
};

}
