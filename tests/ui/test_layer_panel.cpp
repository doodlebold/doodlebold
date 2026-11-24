#include "core/brush/BrushResources.h"
#include "core/document/Document.h"
#include "core/selection/SelectionOps.h"
#include "ui/Theme.h"
#include "ui/panels/LayerPanel.h"
#include "ui/panels/LayerTreeModel.h"
#include "ui/ValueBar.h"
#include "ui/panels/BrushResourceList.h"
#include "ScreenshotSupport.h"

#include <algorithm>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QIcon>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMimeData>
#include <QMouseEvent>
#include <QPixmap>
#include <QScrollBar>
#include <QTemporaryDir>
#include <QTimer>
#include <QToolButton>
#include <QTreeView>
#include <QUndoStack>

#include <functional>
#include <memory>
#include <vector>

#include <gtest/gtest.h>

using namespace doodlebold;

namespace {

using Role = LayerTreeModel::Role;

// root (bottom → top): bottom, folder [inner1, inner2], top. As listed: top, folder, inner2, inner1, bottom.
struct Sample {
    Document doc{QSize(64, 48), Qt::white};
    LayerId bottom, folder, inner1, inner2, top;

    Sample() {
        bottom = doc.currentLayerId();
        folder = doc.addFolder().value();
        inner1 = doc.addLayer().value();
        must(doc.moveLayer(inner1, folder, 0));
        inner2 = doc.addLayer().value();
        doc.setCurrentLayer(folder);
        top = doc.addLayer().value();
        doc.undoStack()->clear();
    }

    static void must(const EditResult<>& r) { ASSERT_TRUE(r.ok()) << describe(r.error()).toStdString(); }

    std::vector<LayerId> children(LayerId parent) const {
        const LayerNode* n = parent ? doc.layer(parent) : &doc.layers().root();
        std::vector<LayerId> out;
        out.reserve(n->children.size());
        for (const LayerNode& c : n->children)
            out.push_back(c.props.id);
        return out;
    }
    int steps() { return doc.undoStack()->count(); }
};

using Ids = std::vector<LayerId>;

void wait(int ms) {
    QEventLoop loop;
    QTimer::singleShot(ms, &loop, &QEventLoop::quit);
    loop.exec();
}

void collect(const LayerTreeModel& model, const QModelIndex& parent, Ids& out) {
    for (int r = 0; r < model.rowCount(parent); ++r) {
        const QModelIndex i = model.index(r, 0, parent);
        out.push_back(model.layerAt(i));
        collect(model, i, out);
    }
}

bool dragTo(LayerTreeModel& model, LayerId layer, int row, const QModelIndex& parent) {
    std::unique_ptr<QMimeData> mime(model.mimeData({model.indexOf(layer)}));
    return model.dropMimeData(mime.get(), Qt::MoveAction, row, 0, parent);
}

LayerProps propsOf(const Document& doc, LayerId id) {
    return doc.layer(id)->props;
}

void setProps(Document& doc, LayerId id, const std::function<void(LayerProps&)>& change) {
    LayerProps p = propsOf(doc, id);
    change(p);
    ASSERT_TRUE(doc.setLayerProps(id, p).ok());
}

void paintRect(Document& doc, LayerId id, const QRect& r, const QColor& c) {
    auto target = doc.paintTarget(id);
    ASSERT_TRUE(target.ok());
    target.value()->fill(r, c);
    doc.notifyPixelsChanged(id, r);
}

template <typename W> W* child(QObject& panel, const char* name) {
    W* w = panel.findChild<W*>(QString::fromLatin1(name));
    EXPECT_NE(w, nullptr) << name;
    return w;
}

}

TEST(LayerTreeModel, MirrorsTheDocumentTopFirstWithNestedFolders) {
    Sample s;
    LayerTreeModel model;
    model.setDocument(&s.doc);

    ASSERT_EQ(model.rowCount(), 3);
    EXPECT_EQ(model.columnCount(), 1);
    EXPECT_EQ(model.layerAt(model.index(0, 0)), s.top);
    EXPECT_EQ(model.layerAt(model.index(1, 0)), s.folder);
    EXPECT_EQ(model.layerAt(model.index(2, 0)), s.bottom);
    const QModelIndex folder = model.index(1, 0);
    ASSERT_EQ(model.rowCount(folder), 2);
    EXPECT_EQ(model.layerAt(model.index(0, 0, folder)), s.inner2);
    EXPECT_EQ(model.layerAt(model.index(1, 0, folder)), s.inner1);
    EXPECT_EQ(model.rowCount(model.index(0, 0)), 0);

    Ids rows;
    collect(model, {}, rows);
    Ids expected;
    for (const LayerNode* n : s.doc.layers().displayOrder())
        expected.push_back(n->props.id);
    EXPECT_EQ(rows, expected);

    for (const LayerId id : rows) {
        const QModelIndex i = model.indexOf(id);
        ASSERT_TRUE(i.isValid());
        EXPECT_EQ(model.data(i, Role::LayerIdRole).toUInt(), id.value());
        EXPECT_EQ(model.layerAt(i), id);
        const LayerNode* parent = s.doc.layers().parentOf(id);
        EXPECT_EQ(model.layerAt(i.parent()), parent->props.id);
        EXPECT_EQ(model.index(i.row(), 0, i.parent()), i);
    }
    EXPECT_FALSE(model.indexOf(LayerId(9999)).isValid());
    EXPECT_TRUE(model.data(model.indexOf(s.folder), Role::IsFolderRole).toBool());
    EXPECT_FALSE(model.data(model.indexOf(s.top), Role::IsFolderRole).toBool());
    EXPECT_TRUE(model.data(model.indexOf(s.top), Role::IsCurrentRole).toBool());
    EXPECT_FALSE(model.data(model.indexOf(s.bottom), Role::IsCurrentRole).toBool());
}

TEST(LayerTreeModel, FollowsStructuralChangesAndNullDocument) {
    Sample s;
    LayerTreeModel model;
    int resets = 0;
    QObject::connect(&model, &QAbstractItemModel::modelReset, &model, [&resets] { ++resets; });
    model.setDocument(&s.doc);
    EXPECT_EQ(resets, 1);

    const LayerId added = s.doc.addLayer().value();
    EXPECT_EQ(resets, 2);
    EXPECT_EQ(model.layerAt(model.index(0, 0)), added);
    EXPECT_EQ(model.rowCount(), 4);
    s.doc.undoStack()->undo();
    EXPECT_EQ(model.rowCount(), 3);

    model.setDocument(nullptr);
    EXPECT_EQ(model.rowCount(), 0);
    EXPECT_FALSE(model.index(0, 0).isValid());
}

TEST(LayerTreeModel, PropertyEditsKeepTheRows) {
    Sample s;
    LayerTreeModel model;
    model.setDocument(&s.doc);
    int resets = 0;
    QObject::connect(&model, &QAbstractItemModel::modelReset, &model, [&resets] { ++resets; });
    int changed = 0;
    QObject::connect(&model, &QAbstractItemModel::dataChanged, &model, [&changed] { ++changed; });

    setProps(s.doc, s.inner1, [](LayerProps& p) { p.opacity = 70; });
    EXPECT_EQ(resets, 0);
    EXPECT_GT(changed, 0);
    EXPECT_EQ(model.data(model.indexOf(s.inner1), Role::ModeTextRole).toString(), QStringLiteral("Normal 70%"));
}

TEST(LayerTreeModel, RolesDescribeTheLayer) {
    Sample s;
    setProps(s.doc, s.inner1, [](LayerProps& p) {
        p.name = QStringLiteral("Deepen");
        p.mode = BlendMode::Multiply;
        p.opacity = 75;
        p.clipping = true;
        p.locked = true;
        p.preserveOpacity = true;
    });
    LayerTreeModel model;
    model.setDocument(&s.doc);
    const QModelIndex i = model.indexOf(s.inner1);
    EXPECT_EQ(model.data(i, Qt::DisplayRole).toString(), QStringLiteral("Deepen"));
    EXPECT_EQ(model.data(i, Qt::EditRole).toString(), QStringLiteral("Deepen"));
    EXPECT_EQ(model.data(i, Role::ModeTextRole).toString(), QStringLiteral("Multiply 75%"));
    EXPECT_TRUE(model.data(i, Role::ClippingRole).toBool());
    EXPECT_TRUE(model.data(i, Role::LockedRole).toBool());
    EXPECT_TRUE(model.data(i, Role::PreserveOpacityRole).toBool());
    EXPECT_TRUE(model.data(i, Role::VisibleRole).toBool());
    EXPECT_EQ(model.data(model.indexOf(s.folder), Role::ModeTextRole).toString(), QStringLiteral("Normal 100%"));

    const QImage thumb = qvariant_cast<QImage>(model.data(i, Qt::DecorationRole));
    ASSERT_FALSE(thumb.isNull());
    EXPECT_LE(std::max(thumb.width(), thumb.height()), LayerTreeModel::kThumbSize);
    EXPECT_FALSE(model.data(model.indexOf(s.folder), Qt::DecorationRole).isValid());
}

TEST(LayerTreeModel, SetDataRenamesAndTogglesVisibility) {
    Sample s;
    LayerTreeModel model;
    model.setDocument(&s.doc);
    QStringList refused;
    QObject::connect(&model, &LayerTreeModel::editRefused, &model, [&refused](const QString& m) { refused << m; });

    const QModelIndex i = model.indexOf(s.inner1);
    EXPECT_TRUE(model.setData(i, false, Role::VisibleRole));
    EXPECT_FALSE(s.doc.layer(s.inner1)->props.visible);
    EXPECT_FALSE(model.data(model.indexOf(s.inner1), Role::VisibleRole).toBool());
    EXPECT_TRUE(model.setData(model.indexOf(s.inner1), QStringLiteral("x"), Qt::EditRole));
    EXPECT_EQ(s.doc.layer(s.inner1)->props.name, QStringLiteral("x"));
    EXPECT_TRUE(refused.isEmpty());

    const int steps = s.steps();
    EXPECT_FALSE(model.setData(model.indexOf(s.inner1), QStringLiteral("   "), Qt::EditRole));
    EXPECT_EQ(refused.size(), 1);
    EXPECT_FALSE(model.setData(model.indexOf(s.inner1), QStringLiteral("x"), Qt::EditRole));
    EXPECT_EQ(refused.size(), 1);
    EXPECT_EQ(s.steps(), steps);
    EXPECT_EQ(s.doc.layer(s.inner1)->props.name, QStringLiteral("x"));

    s.doc.setPainting(true);
    EXPECT_FALSE(model.setData(model.indexOf(s.inner1), QStringLiteral("y"), Qt::EditRole));
    ASSERT_EQ(refused.size(), 2);
    EXPECT_EQ(refused.last(), describe(EditError::StrokeInProgress));
}

TEST(LayerTreeModel, ExpandingAFolderIsNotADocumentEdit) {
    Sample s;
    LayerPanel panel;
    panel.setDocument(&s.doc);
    auto* view = child<QTreeView>(panel, "layerTree");
    auto* model = panel.findChild<LayerTreeModel*>();
    ASSERT_TRUE(view && model);
    const QModelIndex folder = model->indexOf(s.folder);
    EXPECT_TRUE(view->isExpanded(folder));

    view->collapse(folder);
    EXPECT_FALSE(view->isExpanded(folder));
    EXPECT_TRUE(s.doc.layer(s.folder)->props.expanded);
    EXPECT_EQ(s.steps(), 0);

    ASSERT_TRUE(s.doc.addLayer().ok());
    EXPECT_FALSE(view->isExpanded(model->indexOf(s.folder)));
    view->expand(model->indexOf(s.folder));
    ASSERT_TRUE(s.doc.addLayer().ok());
    EXPECT_TRUE(view->isExpanded(model->indexOf(s.folder)));
}

TEST(LayerTreeModel, ThumbnailsAreCoalescedAndRefreshed) {
    Sample s;
    LayerTreeModel model;
    model.setDocument(&s.doc);
    const QModelIndex i = model.indexOf(s.inner1);
    QImage before = qvariant_cast<QImage>(model.data(i, Qt::DecorationRole));
    ASSERT_FALSE(before.isNull());
    EXPECT_EQ(before.pixelColor(before.width() / 2, before.height() / 2).alpha(), 0);

    int decorationUpdates = 0;
    QObject::connect(&model, &QAbstractItemModel::dataChanged, &model,
                     [&](const QModelIndex& tl, const QModelIndex&, const QList<int>& roles) {
                         if (tl == model.indexOf(s.inner1) && roles.contains(Qt::DecorationRole))
                             ++decorationUpdates;
                     });
    for (int i2 = 0; i2 < 5; ++i2)
        paintRect(s.doc, s.inner1, QRect(0, 0, 64, 48), Qt::red);
    EXPECT_EQ(decorationUpdates, 0);
    wait(LayerTreeModel::kThumbDelayMs * 3);
    EXPECT_EQ(decorationUpdates, 1);
    const QImage after = qvariant_cast<QImage>(model.data(model.indexOf(s.inner1), Qt::DecorationRole));
    EXPECT_EQ(after.pixelColor(after.width() / 2, after.height() / 2), QColor(Qt::red));
}

TEST(LayerTreeModel, DropReordersWithinTheTopLevel) {
    {
        Sample s;
        LayerTreeModel model;
        model.setDocument(&s.doc);
        ASSERT_TRUE(dragTo(model, s.bottom, 0, {}));
        EXPECT_EQ(s.children({}), (Ids{s.folder, s.top, s.bottom}));
        EXPECT_EQ(s.steps(), 1);
    }
    {
        Sample s;
        LayerTreeModel model;
        model.setDocument(&s.doc);
        ASSERT_TRUE(dragTo(model, s.top, 3, {}));
        EXPECT_EQ(s.children({}), (Ids{s.top, s.bottom, s.folder}));
    }
    {
        Sample s;
        LayerTreeModel model;
        model.setDocument(&s.doc);
        ASSERT_TRUE(dragTo(model, s.top, 2, {}));
        EXPECT_EQ(s.children({}), (Ids{s.bottom, s.top, s.folder}));
    }
    {
        Sample s;
        LayerTreeModel model;
        model.setDocument(&s.doc);
        ASSERT_TRUE(dragTo(model, s.top, -1, {}));
        EXPECT_EQ(s.children({}), (Ids{s.top, s.bottom, s.folder}));
    }
}

TEST(LayerTreeModel, DropOntoItsOwnPlaceChangesNothing) {
    Sample s;
    LayerTreeModel model;
    model.setDocument(&s.doc);
    QStringList refused;
    QObject::connect(&model, &LayerTreeModel::editRefused, &model, [&refused](const QString& m) { refused << m; });
    EXPECT_FALSE(dragTo(model, s.top, 0, {}));
    EXPECT_FALSE(dragTo(model, s.top, 1, {}));
    EXPECT_FALSE(dragTo(model, s.inner2, 0, model.indexOf(s.folder)));
    EXPECT_FALSE(dragTo(model, s.inner2, -1, model.indexOf(s.folder)));
    EXPECT_EQ(s.steps(), 0);
    EXPECT_TRUE(refused.isEmpty());
    EXPECT_EQ(s.children({}), (Ids{s.bottom, s.folder, s.top}));
}

TEST(LayerTreeModel, DropMovesIntoAndOutOfFolders) {
    {
        Sample s;
        LayerTreeModel model;
        model.setDocument(&s.doc);
        ASSERT_TRUE(dragTo(model, s.top, -1, model.indexOf(s.folder)));
        EXPECT_EQ(s.children({}), (Ids{s.bottom, s.folder}));
        EXPECT_EQ(s.children(s.folder), (Ids{s.inner1, s.inner2, s.top}));
        EXPECT_EQ(s.steps(), 1);
        s.doc.undoStack()->undo();
        EXPECT_EQ(s.children({}), (Ids{s.bottom, s.folder, s.top}));
    }
    {
        Sample s;
        LayerTreeModel model;
        model.setDocument(&s.doc);
        ASSERT_TRUE(dragTo(model, s.bottom, 1, model.indexOf(s.folder)));
        EXPECT_EQ(s.children(s.folder), (Ids{s.inner1, s.bottom, s.inner2}));
    }
    {
        Sample s;
        LayerTreeModel model;
        model.setDocument(&s.doc);
        ASSERT_TRUE(dragTo(model, s.top, 2, model.indexOf(s.folder)));
        EXPECT_EQ(s.children(s.folder), (Ids{s.top, s.inner1, s.inner2}));
    }
    {
        Sample s;
        LayerTreeModel model;
        model.setDocument(&s.doc);
        ASSERT_TRUE(dragTo(model, s.inner1, 0, {}));
        EXPECT_EQ(s.children({}), (Ids{s.bottom, s.folder, s.top, s.inner1}));
        EXPECT_EQ(s.children(s.folder), (Ids{s.inner2}));
    }
    {
        Sample s;
        LayerTreeModel model;
        model.setDocument(&s.doc);
        ASSERT_TRUE(dragTo(model, s.inner2, 2, model.indexOf(s.folder)));
        EXPECT_EQ(s.children(s.folder), (Ids{s.inner2, s.inner1}));
        ASSERT_TRUE(dragTo(model, s.inner2, 0, model.indexOf(s.folder)));
        EXPECT_EQ(s.children(s.folder), (Ids{s.inner1, s.inner2}));
    }
}

TEST(LayerTreeModel, FolderCannotBeDroppedIntoItselfOrItsDescendants) {
    Sample s;
    const LayerId nested = s.doc.addFolder().value();
    Sample::must(s.doc.moveLayer(nested, s.folder, 5));
    s.doc.undoStack()->clear();
    LayerTreeModel model;
    model.setDocument(&s.doc);
    QStringList refused;
    QObject::connect(&model, &LayerTreeModel::editRefused, &model, [&refused](const QString& m) { refused << m; });

    EXPECT_FALSE(dragTo(model, s.folder, -1, model.indexOf(s.folder)));
    ASSERT_EQ(refused.size(), 1);
    EXPECT_EQ(refused.last(), describe(EditError::InvalidMove));
    EXPECT_FALSE(dragTo(model, s.folder, -1, model.indexOf(nested)));
    EXPECT_EQ(refused.size(), 2);
    EXPECT_FALSE(dragTo(model, s.folder, 0, model.indexOf(nested)));
    EXPECT_EQ(refused.size(), 3);
    EXPECT_EQ(s.steps(), 0);
    EXPECT_EQ(s.children({}), (Ids{s.bottom, s.folder, s.top}));
    EXPECT_EQ(s.children(s.folder).back(), nested);
}

TEST(LayerTreeModel, OnlyFoldersAndGapsAcceptDrops) {
    Sample s;
    LayerTreeModel model;
    model.setDocument(&s.doc);
    std::unique_ptr<QMimeData> mime(model.mimeData({model.indexOf(s.top)}));
    ASSERT_TRUE(mime);
    EXPECT_TRUE(model.canDropMimeData(mime.get(), Qt::MoveAction, -1, 0, model.indexOf(s.folder)));
    EXPECT_TRUE(model.canDropMimeData(mime.get(), Qt::MoveAction, 1, 0, {}));
    EXPECT_FALSE(model.canDropMimeData(mime.get(), Qt::MoveAction, -1, 0, model.indexOf(s.bottom)));
    EXPECT_FALSE(model.canDropMimeData(mime.get(), Qt::CopyAction, -1, 0, model.indexOf(s.folder)));
    QMimeData foreign;
    foreign.setText(QStringLiteral("hello"));
    EXPECT_FALSE(model.canDropMimeData(&foreign, Qt::MoveAction, -1, 0, model.indexOf(s.folder)));
    EXPECT_EQ(model.mimeTypes().size(), 1);
    EXPECT_EQ(model.flags(model.indexOf(s.folder)) & Qt::ItemIsDropEnabled, Qt::ItemIsDropEnabled);
    EXPECT_FALSE(model.flags(model.indexOf(s.top)) & Qt::ItemIsDropEnabled);
    EXPECT_TRUE(model.flags(model.indexOf(s.top)) & Qt::ItemIsDragEnabled);
}

TEST(LayerPanel, SelectingARowSetsTheCurrentLayerAndViceVersa) {
    Sample s;
    LayerPanel panel;
    panel.setDocument(&s.doc);
    auto* view = child<QTreeView>(panel, "layerTree");
    auto* model = panel.findChild<LayerTreeModel*>();
    ASSERT_TRUE(view && model);
    EXPECT_EQ(view->currentIndex(), model->indexOf(s.doc.currentLayerId()));

    view->selectionModel()->setCurrentIndex(model->indexOf(s.inner1), QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
    EXPECT_EQ(s.doc.currentLayerId(), s.inner1);
    EXPECT_EQ(s.steps(), 0);

    s.doc.setCurrentLayer(s.bottom);
    EXPECT_EQ(view->currentIndex(), model->indexOf(s.bottom));
    EXPECT_TRUE(view->selectionModel()->isSelected(model->indexOf(s.bottom)));
    EXPECT_FALSE(view->selectionModel()->isSelected(model->indexOf(s.inner1)));

    ASSERT_TRUE(s.doc.addLayer().ok());
    EXPECT_EQ(view->currentIndex(), model->indexOf(s.doc.currentLayerId()));
}

TEST(LayerPanel, ClickingTheRowSelectsAndTheEyeToggles) {
    Sample s;
    LayerPanel panel;
    panel.resize(320, 600);
    panel.setDocument(&s.doc);
    panel.show();
    QCoreApplication::processEvents();
    auto* view = child<QTreeView>(panel, "layerTree");
    auto* model = panel.findChild<LayerTreeModel*>();
    ASSERT_TRUE(view && model);

    const auto click = [view](const QPoint& pos, QEvent::Type type = QEvent::MouseButtonPress) {
        QMouseEvent press(type, QPointF(pos), QPointF(view->viewport()->mapToGlobal(pos)), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QCoreApplication::sendEvent(view->viewport(), &press);
        QMouseEvent release(QEvent::MouseButtonRelease, QPointF(pos), QPointF(view->viewport()->mapToGlobal(pos)), Qt::LeftButton,
                            Qt::NoButton, Qt::NoModifier);
        QCoreApplication::sendEvent(view->viewport(), &release);
    };

    const QRect bottomRow = view->visualRect(model->indexOf(s.bottom));
    ASSERT_TRUE(bottomRow.isValid());
    click(QPoint(bottomRow.left() + 120, bottomRow.center().y()));
    EXPECT_EQ(s.doc.currentLayerId(), s.bottom);

    const QRect topRow = view->visualRect(model->indexOf(s.top));
    click(QPoint(topRow.left() + 4 + 9, topRow.center().y() - 9));
    EXPECT_FALSE(s.doc.layer(s.top)->props.visible);
    EXPECT_EQ(s.doc.currentLayerId(), s.bottom);
    click(QPoint(view->visualRect(model->indexOf(s.top)).left() + 4 + 9, topRow.center().y() - 9));
    EXPECT_TRUE(s.doc.layer(s.top)->props.visible);

    const QRect innerRow = view->visualRect(model->indexOf(s.inner1));
    const QPoint onName(innerRow.left() + 130, innerRow.center().y());
    QMouseEvent dbl(QEvent::MouseButtonDblClick, QPointF(onName), QPointF(view->viewport()->mapToGlobal(onName)), Qt::LeftButton,
                    Qt::LeftButton, Qt::NoModifier);
    QCoreApplication::sendEvent(view->viewport(), &dbl);
    auto* editor = view->viewport()->findChild<QLineEdit*>();
    ASSERT_NE(editor, nullptr);
    editor->setText(QStringLiteral("Sketch"));
    QKeyEvent enter(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
    QCoreApplication::sendEvent(editor, &enter);
    QCoreApplication::processEvents();
    EXPECT_EQ(s.doc.layer(s.inner1)->props.name, QStringLiteral("Sketch"));
}

TEST(LayerPanel, ControlsFollowTheCurrentLayerWithoutMakingUndoSteps) {
    Sample s;
    setProps(s.doc, s.inner1, [](LayerProps& p) {
        p.mode = BlendMode::Multiply;
        p.opacity = 35;
        p.preserveOpacity = true;
        p.clipping = true;
        p.locked = true;
    });
    s.doc.undoStack()->clear();
    LayerPanel panel;
    panel.setDocument(&s.doc);
    auto* combo = child<QComboBox>(panel, "blendModeCombo");
    auto* bar = child<ValueBar>(panel, "opacityBar");

    s.doc.setCurrentLayer(s.inner1);
    EXPECT_EQ(combo->currentData().toInt(), static_cast<int>(BlendMode::Multiply));
    EXPECT_EQ(bar->value(), 35);
    EXPECT_EQ(bar->text(), QStringLiteral("35%"));
    EXPECT_TRUE(child<QCheckBox>(panel, "preserveOpacityCheck")->isChecked());
    EXPECT_TRUE(child<QCheckBox>(panel, "clippingCheck")->isChecked());
    EXPECT_TRUE(child<QCheckBox>(panel, "lockCheck")->isChecked());

    s.doc.setCurrentLayer(s.bottom);
    EXPECT_EQ(combo->currentData().toInt(), static_cast<int>(BlendMode::Normal));
    EXPECT_EQ(bar->value(), 100);
    EXPECT_FALSE(child<QCheckBox>(panel, "lockCheck")->isChecked());
    EXPECT_EQ(s.steps(), 0);

    setProps(s.doc, s.bottom, [](LayerProps& p) { p.opacity = 20; });
    EXPECT_EQ(bar->value(), 20);
    s.doc.undoStack()->undo();
    EXPECT_EQ(bar->value(), 100);
}

TEST(LayerPanel, OpacityBarEditsTheLayer) {
    Sample s;
    LayerPanel panel;
    panel.setDocument(&s.doc);
    auto* bar = child<ValueBar>(panel, "opacityBar");
    const LayerId id = s.doc.currentLayerId();

    bar->setValue(40);
    EXPECT_EQ(s.doc.layer(id)->props.opacity, 40);
    EXPECT_EQ(s.steps(), 1);
    bar->setValue(30);
    EXPECT_EQ(s.steps(), 2);
    s.doc.undoStack()->undo();
    EXPECT_EQ(s.doc.layer(id)->props.opacity, 40);
}

TEST(LayerPanel, SliderDragIsOneUndoStep) {
    Sample s;
    LayerPanel panel;
    panel.setDocument(&s.doc);
    auto* bar = child<ValueBar>(panel, "opacityBar");
    const LayerId id = s.doc.currentLayerId();

    const auto drag = [&](std::initializer_list<int> values) {
        emit bar->dragStarted();
        for (int v : values)
            bar->setValue(v);
        emit bar->dragFinished();
    };
    drag({90, 75, 60, 42});
    EXPECT_EQ(s.doc.layer(id)->props.opacity, 42);
    EXPECT_EQ(bar->value(), 42);
    EXPECT_EQ(s.steps(), 1);

    drag({30, 20});
    EXPECT_EQ(s.steps(), 2);
    EXPECT_EQ(s.doc.layer(id)->props.opacity, 20);

    s.doc.undoStack()->undo();
    EXPECT_EQ(s.doc.layer(id)->props.opacity, 42);
    EXPECT_EQ(bar->value(), 42);
    s.doc.undoStack()->undo();
    EXPECT_EQ(s.doc.layer(id)->props.opacity, 100);
}

TEST(LayerPanel, ModeComboEditsTheLayer) {
    Sample s;
    LayerPanel panel;
    panel.setDocument(&s.doc);
    auto* combo = child<QComboBox>(panel, "blendModeCombo");
    const LayerId id = s.doc.currentLayerId();

    EXPECT_EQ(combo->itemData(0).toInt(), static_cast<int>(BlendMode::Normal));
    EXPECT_EQ(combo->itemText(0), blendModeName(BlendMode::Normal));
    EXPECT_EQ(combo->findData(static_cast<int>(BlendMode::PassThrough)), -1);
    int separators = 0;
    for (int i = 0; i < combo->count(); ++i)
        separators += combo->itemData(i, Qt::AccessibleDescriptionRole).toString() == QStringLiteral("separator");
    EXPECT_EQ(separators, 6);

    const int multiply = combo->findData(static_cast<int>(BlendMode::Multiply));
    ASSERT_GE(multiply, 0);
    combo->setCurrentIndex(multiply);
    EXPECT_EQ(s.doc.layer(id)->props.mode, BlendMode::Multiply);
    EXPECT_EQ(s.steps(), 1);

    ASSERT_GE(combo->findData(static_cast<int>(BlendMode::LinearBurn)), 0);
    setProps(s.doc, id, [](LayerProps& p) { p.mode = BlendMode::LinearBurn; });
    EXPECT_EQ(combo->currentData().toInt(), static_cast<int>(BlendMode::LinearBurn));
    s.doc.setCurrentLayer(s.bottom);
    EXPECT_GE(combo->findData(static_cast<int>(BlendMode::LinearBurn)), 0);
    EXPECT_NE(combo->currentData().toInt(), static_cast<int>(BlendMode::LinearBurn));
}

TEST(LayerPanel, FolderModeListStartsWithPassThrough) {
    Sample s;
    LayerPanel panel;
    panel.setDocument(&s.doc);
    auto* combo = child<QComboBox>(panel, "blendModeCombo");
    s.doc.setCurrentLayer(s.folder);
    ASSERT_GT(combo->count(), 2);
    EXPECT_EQ(combo->itemData(0).toInt(), static_cast<int>(BlendMode::PassThrough));
    EXPECT_EQ(combo->itemText(0), blendModeName(BlendMode::PassThrough));
    EXPECT_EQ(combo->itemText(0), QStringLiteral("Pass Through"));
    EXPECT_EQ(combo->currentData().toInt(), static_cast<int>(BlendMode::Normal)) << "a new folder is Normal";
    EXPECT_FALSE(child<QCheckBox>(panel, "preserveOpacityCheck")->isEnabled());

    combo->setCurrentIndex(combo->findData(static_cast<int>(BlendMode::Multiply)));
    EXPECT_EQ(s.doc.layer(s.folder)->props.mode, BlendMode::Multiply);
    s.doc.setCurrentLayer(s.bottom);
    EXPECT_EQ(combo->findData(static_cast<int>(BlendMode::PassThrough)), -1);
    EXPECT_TRUE(child<QCheckBox>(panel, "preserveOpacityCheck")->isEnabled());
}

TEST(LayerPanel, CheckBoxesEditTheLayer) {
    Sample s;
    LayerPanel panel;
    panel.setDocument(&s.doc);
    const LayerId id = s.doc.currentLayerId();
    child<QCheckBox>(panel, "preserveOpacityCheck")->setChecked(true);
    EXPECT_TRUE(s.doc.layer(id)->props.preserveOpacity);
    child<QCheckBox>(panel, "clippingCheck")->setChecked(true);
    EXPECT_TRUE(s.doc.layer(id)->props.clipping);
    child<QCheckBox>(panel, "lockCheck")->setChecked(true);
    EXPECT_TRUE(s.doc.layer(id)->props.locked);
    EXPECT_EQ(s.steps(), 3);
}

TEST(LayerPanel, ButtonsAddDuplicateMergeAndDelete) {
    Sample s;
    LayerPanel panel;
    panel.setDocument(&s.doc);
    QStringList refused;
    QObject::connect(&panel, &LayerPanel::editRefused, &panel, [&refused](const QString& m) { refused << m; });

    const int count = s.doc.layers().layerCount();
    child<QToolButton>(panel, "newLayerButton")->click();
    EXPECT_EQ(s.doc.layers().layerCount(), count + 1);
    EXPECT_EQ(s.steps(), 1);
    child<QToolButton>(panel, "newFolderButton")->click();
    EXPECT_EQ(s.doc.layers().layerCount(), count + 2);
    EXPECT_TRUE(s.doc.currentLayer()->isFolder());
    child<QToolButton>(panel, "deleteButton")->click();
    EXPECT_EQ(s.doc.layers().layerCount(), count + 1);

    const LayerId before = s.doc.currentLayerId();
    child<QToolButton>(panel, "duplicateButton")->click();
    EXPECT_EQ(s.doc.layers().layerCount(), count + 2);
    EXPECT_NE(s.doc.currentLayerId(), before);
    const int layers = s.doc.layers().layerCount();
    child<QToolButton>(panel, "mergeDownButton")->click();
    EXPECT_EQ(s.doc.layers().layerCount(), layers - 1);
    EXPECT_TRUE(refused.isEmpty());

    s.doc.setCurrentLayer(s.bottom);
    child<QToolButton>(panel, "mergeDownButton")->click();
    ASSERT_EQ(refused.size(), 1);
    EXPECT_EQ(refused.last(), describe(EditError::NothingBelow));
}

namespace {

// Selects rows the way Ctrl+click does.
void selectRows(LayerPanel& panel, const LayerTreeModel& model, std::initializer_list<LayerId> ids) {
    auto* view = child<QTreeView>(panel, "layerTree");
    QItemSelectionModel::SelectionFlags flags = QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows;
    for (const LayerId id : ids) {
        view->selectionModel()->setCurrentIndex(model.indexOf(id), flags);
        flags = QItemSelectionModel::Select | QItemSelectionModel::Rows;
    }
}

}

TEST(LayerPanel, TreeSelectsSeveralLayersAndKeepsTheCurrentOneAmongThem) {
    Sample s;
    LayerPanel panel;
    panel.setDocument(&s.doc);
    auto* model = panel.findChild<LayerTreeModel*>();
    ASSERT_TRUE(model);

    EXPECT_EQ(panel.selectedLayers(), std::vector<LayerId>{s.top});
    selectRows(panel, *model, {s.top, s.bottom});
    EXPECT_EQ(panel.selectedLayers().size(), 2u);
    EXPECT_EQ(s.doc.currentLayerId(), s.bottom);

    auto* view = child<QTreeView>(panel, "layerTree");
    view->selectionModel()->select(model->indexOf(s.bottom), QItemSelectionModel::Deselect | QItemSelectionModel::Rows);
    EXPECT_EQ(panel.selectedLayers(), std::vector<LayerId>{s.top});
    EXPECT_EQ(s.doc.currentLayerId(), s.top);

    view->selectionModel()->select(model->indexOf(s.top), QItemSelectionModel::Deselect | QItemSelectionModel::Rows);
    EXPECT_EQ(panel.selectedLayers(), std::vector<LayerId>{s.top}) << "one layer always stays selected";
    EXPECT_TRUE(view->selectionModel()->isRowSelected(model->indexOf(s.top).row(), {}));
}

TEST(LayerPanel, CtrlAndShiftClicksOnRowsSelectSeveralLayers) {
    Sample s;
    LayerPanel panel;
    panel.resize(236, 500);
    panel.setDocument(&s.doc);
    panel.show();
    QCoreApplication::processEvents();
    auto* view = child<QTreeView>(panel, "layerTree");
    auto* model = panel.findChild<LayerTreeModel*>();
    const auto clickName = [&](LayerId id, Qt::KeyboardModifiers mods) {
        const QPointF pos = view->visualRect(model->indexOf(id)).center() + QPoint(40, 0);
        const QPointF global = view->viewport()->mapToGlobal(pos);
        QMouseEvent press(QEvent::MouseButtonPress, pos, global, Qt::LeftButton, Qt::LeftButton, mods);
        QMouseEvent release(QEvent::MouseButtonRelease, pos, global, Qt::LeftButton, Qt::NoButton, mods);
        QCoreApplication::sendEvent(view->viewport(), &press);
        QCoreApplication::sendEvent(view->viewport(), &release);
        QCoreApplication::processEvents();
    };
    const auto selected = [&panel] {
        std::vector<LayerId> ids = panel.selectedLayers();
        std::ranges::sort(ids);
        return ids;
    };
    const auto sorted = [](std::vector<LayerId> ids) {
        std::ranges::sort(ids);
        return ids;
    };

    clickName(s.top, Qt::NoModifier);
    EXPECT_EQ(selected(), sorted({s.top}));
    clickName(s.bottom, Qt::ControlModifier);
    EXPECT_EQ(selected(), sorted({s.top, s.bottom}));
    EXPECT_EQ(s.doc.currentLayerId(), s.bottom);

    clickName(s.bottom, Qt::ControlModifier);
    EXPECT_EQ(selected(), sorted({s.top})) << "a second Ctrl+click takes the row out";
    EXPECT_EQ(s.doc.currentLayerId(), s.top) << "the current layer stays among the selected ones";
    clickName(s.top, Qt::ControlModifier);
    EXPECT_EQ(selected(), sorted({s.top})) << "the last selected row stays";

    clickName(s.inner2, Qt::ShiftModifier);
    EXPECT_EQ(selected(), sorted({s.top, s.folder, s.inner2}));

    clickName(s.inner1, Qt::NoModifier);
    EXPECT_EQ(selected(), sorted({s.inner1})) << "a plain click ends it";
    EXPECT_EQ(s.doc.currentLayerId(), s.inner1);
}

TEST(LayerPanel, DraggingSeveralLayersMovesThemTogetherInOneStep) {
    Sample s;
    LayerPanel panel;
    panel.setDocument(&s.doc);
    auto* model = panel.findChild<LayerTreeModel*>();
    selectRows(panel, *model, {s.top, s.bottom});
    s.doc.undoStack()->clear();

    // Into the folder, above its two layers.
    std::unique_ptr<QMimeData> mime(model->mimeData({model->indexOf(s.top), model->indexOf(s.bottom)}));
    ASSERT_TRUE(model->dropMimeData(mime.get(), Qt::MoveAction, 0, 0, model->indexOf(s.folder)));
    EXPECT_EQ(s.children(s.folder), (std::vector<LayerId>{s.inner1, s.inner2, s.bottom, s.top})) << "their order stays";
    EXPECT_EQ(s.children(kNoLayer), std::vector<LayerId>{s.folder});
    EXPECT_EQ(s.steps(), 1);
    EXPECT_EQ(panel.selectedLayers().size(), 2u) << "they stay selected";
    Sample::must(s.doc.undo());
    EXPECT_EQ(s.children(kNoLayer), (std::vector<LayerId>{s.bottom, s.folder, s.top}));

    // A selected folder cannot go inside itself, and what is in it goes with it.
    std::unique_ptr<QMimeData> folderMime(model->mimeData({model->indexOf(s.folder), model->indexOf(s.inner1)}));
    EXPECT_FALSE(model->dropMimeData(folderMime.get(), Qt::MoveAction, 0, 0, model->indexOf(s.folder)));
    ASSERT_TRUE(model->dropMimeData(folderMime.get(), Qt::MoveAction, 0, 0, {}));
    EXPECT_EQ(s.children(kNoLayer), (std::vector<LayerId>{s.bottom, s.top, s.folder}));
    EXPECT_EQ(s.children(s.folder), (std::vector<LayerId>{s.inner1, s.inner2}));
}

TEST(LayerPanel, DroppingLayersWhereTheyAreIsNoStep) {
    Sample s;
    LayerPanel panel;
    panel.setDocument(&s.doc);
    auto* model = panel.findChild<LayerTreeModel*>();
    s.doc.undoStack()->clear();
    const auto before = s.children(kNoLayer);
    std::unique_ptr<QMimeData> mime(model->mimeData({model->indexOf(s.top), model->indexOf(s.folder)}));
    model->dropMimeData(mime.get(), Qt::MoveAction, 0, 0, {});
    EXPECT_EQ(s.children(kNoLayer), before);
    EXPECT_EQ(s.steps(), 0);
}

namespace {

// The layer list driven by a pen's tablet events or the mouse's, as the window hands them to its viewport.
struct ListDrag {
    QTreeView* view;
    LayerTreeModel* model;
    QPointingDevice stylus{QStringLiteral("test pen"),
                           1,
                           QInputDevice::DeviceType::Stylus,
                           QPointingDevice::PointerType::Pen,
                           QInputDevice::Capability::Position | QInputDevice::Capability::Pressure,
                           1,
                           3};

    QPoint onRow(LayerId id, int dy = 0) const {
        const QRect r = view->visualRect(model->indexOf(id));
        return {r.left() + 120, r.center().y() + dy};  // on the name, clear of the eye and the thumbnails
    }
    QPoint topEdgeOf(LayerId id) const { return view->visualRect(model->indexOf(id)).topLeft() + QPoint(120, 1); }

    // With `asTheAppDoes`, each tablet event the list leaves unaccepted is followed by the mouse event Qt makes from it in the app.
    bool asTheAppDoes = false;

    void pen(QEvent::Type type, const QPoint& at) {
        const bool up = type == QEvent::TabletRelease;
        QTabletEvent event(type, &stylus, QPointF(at), QPointF(view->viewport()->mapToGlobal(at)), up ? 0.0 : 0.6, 0, 0, 0, 0, 0,
                           Qt::NoModifier, Qt::LeftButton, up ? Qt::NoButton : Qt::LeftButton);
        QCoreApplication::sendEvent(view->viewport(), &event);
        if (asTheAppDoes && !event.isAccepted())
            mouse(type == QEvent::TabletPress ? QEvent::MouseButtonPress : up ? QEvent::MouseButtonRelease : QEvent::MouseMove, at);
    }
    void mouse(QEvent::Type type, const QPoint& at) {
        const bool up = type == QEvent::MouseButtonRelease;
        QMouseEvent event(type, QPointF(at), QPointF(view->viewport()->mapToGlobal(at)), Qt::LeftButton, up ? Qt::NoButton : Qt::LeftButton,
                          Qt::NoModifier);
        QCoreApplication::sendEvent(view->viewport(), &event);
    }
    // From one point to another in small steps, as a hand moves: the pen's own events, or the mouse's.
    // A tap (from == to) wobbles by a pixel or two on the way, as a pen in the hand does.
    void drag(const QPoint& from, const QPoint& to, bool withPen) {
        const int steps = 12;
        withPen ? pen(QEvent::TabletPress, from) : mouse(QEvent::MouseButtonPress, from);
        for (int i = 1; i <= steps; ++i) {
            const QPoint at = from + (to - from) * i / steps + (from == to ? QPoint(i % 3, i % 2) : QPoint());
            withPen ? pen(QEvent::TabletMove, at) : mouse(QEvent::MouseMove, at);
        }
        withPen ? pen(QEvent::TabletRelease, to) : mouse(QEvent::MouseButtonRelease, to);
        QCoreApplication::processEvents();
    }
};

}

// Feedback: "When using the pen, i cant drag the layers up and down."
TEST(LayerPanel, ThePenDragsLayersUpAndDownAndIntoFolders) {
    Sample s;
    LayerPanel panel;
    panel.resize(320, 600);
    panel.setDocument(&s.doc);
    panel.show();
    QCoreApplication::processEvents();
    ListDrag list{child<QTreeView>(panel, "layerTree"), panel.findChild<LayerTreeModel*>()};
    ASSERT_TRUE(list.view && list.model);

    // The bottom layer up above the top one: one step.
    s.doc.setCurrentLayer(s.bottom);
    list.drag(list.onRow(s.bottom), list.topEdgeOf(s.top), true);
    EXPECT_EQ(s.children(kNoLayer), (Ids{s.folder, s.top, s.bottom}));
    EXPECT_EQ(s.steps(), 1);
    EXPECT_EQ(s.doc.currentLayerId(), s.bottom) << "it stays the layer in use";

    // And down again, onto the middle of the folder's row: into the folder, at its top.
    list.drag(list.onRow(s.bottom), list.onRow(s.folder), true);
    EXPECT_EQ(s.children(kNoLayer), (Ids{s.folder, s.top}));
    EXPECT_EQ(s.children(s.folder), (Ids{s.inner1, s.inner2, s.bottom}));
    EXPECT_EQ(s.steps(), 2);

    // A row not yet chosen moves too: the pen's press chooses it.
    list.drag(list.onRow(s.inner1), list.topEdgeOf(s.top), true);
    EXPECT_EQ(s.children(kNoLayer), (Ids{s.folder, s.top, s.inner1}));
    EXPECT_EQ(s.steps(), 3);
    EXPECT_TRUE(test::saveScreenshot(panel.grab(), QStringLiteral("layer_list_after_pen_drags")));

    // Mid-drag the list shows where the row will land: here, between the top row and the folder.
    const QPoint from = list.onRow(s.inner1), between = list.topEdgeOf(s.folder);
    list.pen(QEvent::TabletPress, from);
    for (int i = 1; i <= 12; ++i)
        list.pen(QEvent::TabletMove, from + (between - from) * i / 12);
    EXPECT_TRUE(test::saveScreenshot(panel.grab(), QStringLiteral("layer_list_pen_dragging")));
    list.pen(QEvent::TabletRelease, between);
    EXPECT_EQ(s.children(kNoLayer), (Ids{s.folder, s.inner1, s.top}));
    EXPECT_EQ(s.steps(), 4);
    Sample::must(s.doc.undo());
    EXPECT_EQ(s.children(kNoLayer), (Ids{s.folder, s.top, s.inner1}));

    // A tap of the pen moves nothing, nor does a drag that ends where it began.
    list.drag(list.onRow(s.top), list.onRow(s.top), true);
    list.drag(list.onRow(s.top), list.onRow(s.top, 2), true);
    EXPECT_EQ(s.children(kNoLayer), (Ids{s.folder, s.top, s.inner1}));
    EXPECT_EQ(s.steps(), 4) << "the undone step, and none from the taps";
}

// The pen as the app hands it over: each of its events the list doesn't take followed by the mouse event Qt makes from it. A drag
// still moves the rows, several together and still chosen after; a tap still chooses a row and switches the eye; and the next click
// after a drag is a click.
TEST(LayerPanel, ThePenWithTheMouseEventsQtMakesFromItDragsTapsAndClicks) {
    Sample s;
    LayerPanel panel;
    panel.resize(320, 600);
    panel.setDocument(&s.doc);
    panel.show();
    QCoreApplication::processEvents();
    ListDrag list{child<QTreeView>(panel, "layerTree"), panel.findChild<LayerTreeModel*>()};
    ASSERT_TRUE(list.view && list.model);
    list.asTheAppDoes = true;

    // A tap on a row's name chooses it; a tap on its eye hides it and leaves the choice.
    list.drag(list.onRow(s.bottom), list.onRow(s.bottom), true);
    EXPECT_EQ(s.doc.currentLayerId(), s.bottom);
    const QRect topRow = list.view->visualRect(list.model->indexOf(s.top));
    const QPoint eye(topRow.left() + 4 + 9, topRow.center().y() - 9);
    list.drag(eye, eye, true);
    EXPECT_FALSE(s.doc.layer(s.top)->props.visible);
    EXPECT_EQ(s.doc.currentLayerId(), s.bottom);
    list.drag(eye, eye, true);
    EXPECT_TRUE(s.doc.layer(s.top)->props.visible);
    EXPECT_EQ(s.children(kNoLayer), (Ids{s.bottom, s.folder, s.top})) << "a tap that wobbles on the eye moves nothing";
    const int before = s.steps();

    // Two chosen rows, dragged by one of them onto the folder: both go in, in one step, and stay chosen.
    selectRows(panel, *list.model, {s.top, s.bottom});
    const QPoint from = list.onRow(s.top), onFolder = list.onRow(s.folder);
    list.pen(QEvent::TabletPress, from);
    for (int i = 1; i <= 12; ++i)
        list.pen(QEvent::TabletMove, from + (onFolder - from) * i / 12);
    EXPECT_TRUE(test::saveScreenshot(panel.grab(), QStringLiteral("layer_list_pen_dragging_onto_folder")));
    list.pen(QEvent::TabletRelease, onFolder);
    QCoreApplication::processEvents();
    EXPECT_EQ(s.children(kNoLayer), Ids{s.folder});
    EXPECT_EQ(s.children(s.folder), (Ids{s.inner1, s.inner2, s.bottom, s.top})) << "in their order";
    EXPECT_EQ(s.steps(), before + 1);
    EXPECT_EQ(panel.selectedLayers().size(), 2u) << "both still chosen";

    // The next click, with the mouse, chooses the row clicked.
    list.mouse(QEvent::MouseButtonPress, list.onRow(s.inner1));
    list.mouse(QEvent::MouseButtonRelease, list.onRow(s.inner1));
    EXPECT_EQ(s.doc.currentLayerId(), s.inner1);
    EXPECT_EQ(panel.selectedLayers().size(), 1u);

    // And a drag of one row up, out of the folder, to the top of the list.
    list.drag(list.onRow(s.inner1), list.topEdgeOf(s.folder), true);
    EXPECT_EQ(s.children(kNoLayer), (Ids{s.folder, s.inner1}));
    EXPECT_EQ(s.steps(), before + 2);
}

// The mouse drags the rows by the same path, inside the list: no window-system drag, the same drop as before.
TEST(LayerPanel, TheMouseDragsLayersAndEscapePutsThemBack) {
    Sample s;
    LayerPanel panel;
    panel.resize(320, 600);
    panel.setDocument(&s.doc);
    panel.show();
    QCoreApplication::processEvents();
    ListDrag list{child<QTreeView>(panel, "layerTree"), panel.findChild<LayerTreeModel*>()};
    ASSERT_TRUE(list.view && list.model);
    EXPECT_TRUE(list.view->dragEnabled());

    s.doc.setCurrentLayer(s.top);
    list.drag(list.onRow(s.top), list.onRow(s.bottom, 6), false);
    EXPECT_EQ(s.children(kNoLayer), (Ids{s.top, s.bottom, s.folder})) << "below the bottom row's middle: under it";
    EXPECT_EQ(s.steps(), 1);

    // Escape during the drag: nothing moves.
    list.mouse(QEvent::MouseButtonPress, list.onRow(s.top));
    for (int dy = 4; dy <= 80; dy += 4)
        list.mouse(QEvent::MouseMove, list.onRow(s.top) + QPoint(0, dy));
    QKeyEvent escape(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
    QCoreApplication::sendEvent(list.view, &escape);
    list.mouse(QEvent::MouseButtonRelease, list.onRow(s.folder));
    EXPECT_EQ(s.children(kNoLayer), (Ids{s.top, s.bottom, s.folder}));
    EXPECT_EQ(s.steps(), 1);
}

namespace {

// How many of a region's pixels differ between two pictures of the list.
int differing(const QImage& a, const QImage& b, const QRect& region) {
    int count = 0;
    for (int y = region.top(); y <= region.bottom(); ++y)
        for (int x = region.left(); x <= region.right(); ++x)
            count += a.pixel(x, y) != b.pixel(x, y) ? 1 : 0;
    return count;
}

}

// Feedback: "Add both back": while rows are dragged, a picture of them follows the pointer, with the pen and the mouse alike.
TEST(LayerPanel, ThePictureOfTheDraggedRowsFollowsThePointer) {
    for (const bool withPen : {true, false}) {
        Sample s;
        LayerPanel panel;
        panel.resize(320, 600);
        panel.setDocument(&s.doc);
        panel.show();
        QCoreApplication::processEvents();
        ListDrag list{child<QTreeView>(panel, "layerTree"), panel.findChild<LayerTreeModel*>()};
        ASSERT_TRUE(list.view && list.model);
        s.doc.setCurrentLayer(s.top);
        QCoreApplication::processEvents();
        QWidget* viewport = list.view->viewport();
        const QImage still = viewport->grab().toImage();

        // Below the rows, where the list is empty: the row's picture there, and moved down with the pointer.
        const int rowsEnd = list.view->visualRect(list.model->indexOf(s.bottom)).bottom();
        ASSERT_GT(viewport->height() - rowsEnd, 120) << "room below the rows";
        const QPoint from = list.onRow(s.top), below(from.x(), rowsEnd + 40), further = below + QPoint(0, 40);
        const auto press = [&](const QPoint& at) {
            withPen ? list.pen(QEvent::TabletPress, at) : list.mouse(QEvent::MouseButtonPress, at);
        };
        const auto move = [&](const QPoint& at) { withPen ? list.pen(QEvent::TabletMove, at) : list.mouse(QEvent::MouseMove, at); };
        press(from);
        for (int i = 1; i <= 12; ++i)
            move(from + (below - from) * i / 12);
        const QImage first = viewport->grab().toImage();
        const QRect atBelow(QPoint(20, below.y() - 6), QPoint(200, below.y() + 6)), atFurther = atBelow.translated(0, 40);
        EXPECT_GT(differing(still, first, atBelow), 200) << "the picture under the pointer, " << (withPen ? "pen" : "mouse");
        EXPECT_LT(differing(still, first, atFurther), 20) << "and not yet further down";
        if (withPen) {
            EXPECT_TRUE(test::saveScreenshot(panel.grab(), QStringLiteral("layer_list_drag_picture")));
        }
        move(further);
        const QImage second = viewport->grab().toImage();
        EXPECT_GT(differing(still, second, atFurther), 200) << "it follows the pointer";
        EXPECT_LT(differing(still, second, atBelow.adjusted(0, -16, 0, -16)), 20) << "and leaves where it was";
        withPen ? list.pen(QEvent::TabletRelease, further) : list.mouse(QEvent::MouseButtonRelease, further);
        QCoreApplication::processEvents();
        EXPECT_EQ(s.children(kNoLayer), (Ids{s.top, s.bottom, s.folder})) << "below the rows: the bottom of the list";
        EXPECT_EQ(s.steps(), 1);
        EXPECT_EQ(differing(still, viewport->grab().toImage(), atFurther), 0) << "no picture once dropped";
    }
}

// And the list scrolls while the pointer is held at its top or bottom edge, so a layer goes past the rows it shows. Escape
// stops it with the rest of the drag.
TEST(LayerPanel, TheListScrollsWhileADragIsHeldAtItsEdge) {
    Sample s;
    std::vector<LayerId> added;
    s.doc.setCurrentLayer(s.top);
    added.reserve(12);
    for (int i = 0; i < 12; ++i)
        added.push_back(s.doc.addLayer().value());
    s.doc.undoStack()->clear();
    LayerPanel panel;
    panel.resize(320, 520);
    panel.setDocument(&s.doc);
    panel.show();
    QCoreApplication::processEvents();
    ListDrag list{child<QTreeView>(panel, "layerTree"), panel.findChild<LayerTreeModel*>()};
    ASSERT_TRUE(list.view && list.model);
    QScrollBar* bar = list.view->verticalScrollBar();
    ASSERT_GT(bar->maximum(), 0) << "more rows than the list shows";
    const int height = list.view->viewport()->height();
    const auto holdUntil = [](const std::function<bool()>& done) {
        QElapsedTimer clock;
        clock.start();
        while (!done() && clock.elapsed() < 5000)
            wait(20);
    };

    // The top layer with the pen, held at the bottom edge: the list scrolls to its end, and the layer lands there.
    bar->setValue(0);
    const LayerId moved = added.back();
    s.doc.setCurrentLayer(moved);
    QCoreApplication::processEvents();
    const QPoint from = list.onRow(moved), atBottom(from.x(), height - 4);
    list.pen(QEvent::TabletPress, from);
    for (int i = 1; i <= 12; ++i)
        list.pen(QEvent::TabletMove, from + (atBottom - from) * i / 12);
    holdUntil([&] { return bar->value() == bar->maximum(); });
    EXPECT_EQ(bar->value(), bar->maximum()) << "scrolled to the end while held";
    list.pen(QEvent::TabletRelease, atBottom);
    QCoreApplication::processEvents();
    const auto root = s.children(kNoLayer);
    const auto at = std::ranges::find(root, moved) - root.begin();
    EXPECT_LE(at, 1) << "among the bottom rows, past those the list showed";
    EXPECT_EQ(s.steps(), 1);

    // The bottom layer with the mouse, held at the top edge: back to the top, and the layer lands at the top of the list.
    bar->setValue(bar->maximum());
    s.doc.setCurrentLayer(s.bottom);
    QCoreApplication::processEvents();
    const QPoint low = list.onRow(s.bottom), atTop(low.x(), 3);
    list.mouse(QEvent::MouseButtonPress, low);
    for (int i = 1; i <= 12; ++i)
        list.mouse(QEvent::MouseMove, low + (atTop - low) * i / 12);
    holdUntil([&] { return bar->value() == 0; });
    EXPECT_EQ(bar->value(), 0);
    list.mouse(QEvent::MouseButtonRelease, atTop);
    QCoreApplication::processEvents();
    EXPECT_EQ(s.children(kNoLayer).back(), s.bottom) << "at the top of the list";
    EXPECT_EQ(s.steps(), 2);

    // Escape while it scrolls: it stops, and nothing moves.
    bar->setValue(0);
    const auto before = s.children(kNoLayer);
    const QPoint start = list.onRow(s.bottom);
    list.mouse(QEvent::MouseButtonPress, start);
    for (int i = 1; i <= 12; ++i)
        list.mouse(QEvent::MouseMove, start + (atBottom - start) * i / 12);
    wait(400);
    QKeyEvent escape(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
    QCoreApplication::sendEvent(list.view, &escape);
    const int stopped = bar->value();
    wait(400);
    EXPECT_EQ(bar->value(), stopped) << "no more scrolling";
    list.mouse(QEvent::MouseButtonRelease, atBottom);
    EXPECT_EQ(s.children(kNoLayer), before);
    EXPECT_EQ(s.steps(), 2);
}

TEST(LayerPanel, AClippedLayerShowsARedBarAtTheRowsEdge) {
    test::applyTheme();
    Sample s;
    LayerProps props = s.doc.layer(s.top)->props;
    props.clipping = true;
    Sample::must(s.doc.setLayerProps(s.top, props));
    s.doc.setCurrentLayer(s.bottom);
    LayerPanel panel;
    panel.resize(236, 500);
    panel.setDocument(&s.doc);
    panel.show();
    QCoreApplication::processEvents();
    auto* view = child<QTreeView>(panel, "layerTree");
    auto* model = panel.findChild<LayerTreeModel*>();
    const QImage shot = view->viewport()->grab().toImage();
    const auto edgeColour = [&](LayerId id) {
        const QRect row = view->visualRect(model->indexOf(id));
        return shot.pixelColor(row.left() + 2, row.center().y());
    };
    const QColor clipped = edgeColour(s.top);
    EXPECT_GT(clipped.red(), 200);
    EXPECT_LT(clipped.green(), 100);
    EXPECT_NE(edgeColour(s.folder), clipped);
    EXPECT_NE(edgeColour(s.bottom), clipped);
    EXPECT_TRUE(test::saveScreenshot(panel.grab(), QStringLiteral("layer_panel_clipped")));
}

TEST(LayerPanel, SeveralSelectedLayersLeaveOnlyDeleteClearAndMerge) {
    Sample s;
    LayerPanel panel;
    panel.setDocument(&s.doc);
    auto* model = panel.findChild<LayerTreeModel*>();
    const auto enabled = [&panel](const char* name) { return child<QToolButton>(panel, name)->isEnabled(); };
    const char* const oneLayerOnly[] = {"newLayerButton",  "newFolderButton", "duplicateButton",
                                        "mergeDownButton", "newMaskButton",   "applyMaskButton"};

    EXPECT_TRUE(enabled("clearLayerButton"));
    EXPECT_TRUE(enabled("deleteButton"));
    EXPECT_FALSE(enabled("mergeLayersButton")) << "merging needs two layers";
    EXPECT_TRUE(enabled("newLayerButton"));

    selectRows(panel, *model, {s.top, s.bottom});
    EXPECT_TRUE(enabled("clearLayerButton"));
    EXPECT_TRUE(enabled("deleteButton"));
    EXPECT_TRUE(enabled("mergeLayersButton"));
    for (const char* name : oneLayerOnly)
        EXPECT_FALSE(enabled(name)) << name;
    EXPECT_FALSE(child<QComboBox>(panel, "blendModeCombo")->isEnabled());
    EXPECT_TRUE(child<QTreeView>(panel, "layerTree")->dragEnabled()) << "several layers drag together";

    selectRows(panel, *model, {s.top});
    EXPECT_FALSE(enabled("mergeLayersButton"));
    EXPECT_TRUE(enabled("newLayerButton"));
    EXPECT_TRUE(child<QComboBox>(panel, "blendModeCombo")->isEnabled());
    EXPECT_TRUE(child<QTreeView>(panel, "layerTree")->dragEnabled());
}

TEST(LayerPanel, ClearDeleteAndMergeButtonsActOnEverySelectedLayerInOneStep) {
    Sample s;
    LayerPanel panel;
    panel.setDocument(&s.doc);
    auto* model = panel.findChild<LayerTreeModel*>();
    const auto paint = [&s](LayerId id, QRgb colour) {
        s.doc.setCurrentLayer(id);
        Sample::must(s.doc.fillLayer(id, QColor::fromRgba(colour)));
    };
    paint(s.bottom, qRgb(200, 0, 0));
    paint(s.top, qRgb(0, 200, 0));
    s.doc.undoStack()->clear();

    selectRows(panel, *model, {s.top, s.bottom});
    child<QToolButton>(panel, "clearLayerButton")->click();
    EXPECT_TRUE(s.doc.layer(s.top)->pixels.isEmpty());
    EXPECT_TRUE(s.doc.layer(s.bottom)->pixels.isEmpty());
    EXPECT_EQ(s.steps(), 1);
    EXPECT_EQ(panel.selectedLayers().size(), 2u) << "clearing keeps the selection";
    Sample::must(s.doc.undo());
    EXPECT_FALSE(s.doc.layer(s.top)->pixels.isEmpty());
    EXPECT_FALSE(s.doc.layer(s.bottom)->pixels.isEmpty());

    const int count = s.doc.layers().layerCount();
    child<QToolButton>(panel, "mergeLayersButton")->click();
    EXPECT_EQ(s.doc.layers().layerCount(), count - 1);
    EXPECT_FALSE(s.doc.layer(s.top));
    EXPECT_EQ(s.doc.currentLayerId(), s.bottom);
    EXPECT_EQ(panel.selectedLayers(), std::vector<LayerId>{s.bottom});
    Sample::must(s.doc.undo());
    EXPECT_TRUE(s.doc.layer(s.top));

    selectRows(panel, *model, {s.top, s.inner1});
    child<QToolButton>(panel, "deleteButton")->click();
    EXPECT_EQ(s.doc.layers().layerCount(), count - 2);
    EXPECT_FALSE(s.doc.layer(s.top));
    EXPECT_FALSE(s.doc.layer(s.inner1));
    EXPECT_EQ(panel.selectedLayers(), std::vector<LayerId>{s.doc.currentLayerId()});
}

TEST(LayerPanel, ButtonsFitThePanelInTwoRows) {
    Sample s;
    LayerPanel panel;
    panel.setDocument(&s.doc);
    panel.resize(236, 500);
    panel.show();
    QCoreApplication::processEvents();
    const auto topOf = [&panel](const char* name) { return child<QToolButton>(panel, name)->geometry().top(); };
    const auto* lastInRow = child<QToolButton>(panel, "deleteButton");
    EXPECT_EQ(topOf("newLayerButton"), topOf("applyMaskButton"));
    EXPECT_EQ(topOf("mergeDownButton"), topOf("deleteButton"));
    EXPECT_GT(topOf("mergeDownButton"), topOf("newLayerButton"));
    EXPECT_LE(lastInRow->geometry().right(), lastInRow->parentWidget()->width());
    EXPECT_LE(panel.minimumSizeHint().width(), 236);
}

TEST(LayerPanel, SavesAScreenshotWithSeveralLayersSelected) {
    test::applyTheme();
    Sample s;
    LayerPanel panel;
    panel.resize(236, 460);
    panel.setDocument(&s.doc);
    panel.show();
    QCoreApplication::processEvents();
    selectRows(panel, *panel.findChild<LayerTreeModel*>(), {s.top, s.inner2, s.bottom});
    QCoreApplication::processEvents();
    EXPECT_TRUE(test::saveScreenshot(panel.grab(), QStringLiteral("layer_panel_several")));
    selectRows(panel, *panel.findChild<LayerTreeModel*>(), {s.top});
    QCoreApplication::processEvents();
    EXPECT_TRUE(test::saveScreenshot(panel.grab(), QStringLiteral("layer_panel_one")));
}

TEST(LayerPanel, MaskButtonsThumbnailsAndSwitches) {
    Sample s;
    LayerPanel panel;
    panel.resize(320, 600);
    panel.setDocument(&s.doc);
    panel.show();
    QCoreApplication::processEvents();
    auto* view = child<QTreeView>(panel, "layerTree");
    auto* model = panel.findChild<LayerTreeModel*>();
    auto* newMask = child<QToolButton>(panel, "newMaskButton");
    auto* apply = child<QToolButton>(panel, "applyMaskButton");
    ASSERT_TRUE(view && model && newMask && apply);
    const auto click = [view](const QPoint& pos) {
        QMouseEvent press(QEvent::MouseButtonPress, QPointF(pos), QPointF(view->viewport()->mapToGlobal(pos)), Qt::LeftButton,
                          Qt::LeftButton, Qt::NoModifier);
        QCoreApplication::sendEvent(view->viewport(), &press);
        QMouseEvent release(QEvent::MouseButtonRelease, QPointF(pos), QPointF(view->viewport()->mapToGlobal(pos)), Qt::LeftButton,
                            Qt::NoButton, Qt::NoModifier);
        QCoreApplication::sendEvent(view->viewport(), &release);
    };

    s.doc.setCurrentLayer(s.top);
    EXPECT_TRUE(newMask->isEnabled());
    EXPECT_FALSE(apply->isEnabled());
    newMask->click();
    ASSERT_TRUE(s.doc.layer(s.top)->mask);
    EXPECT_TRUE(s.doc.maskTarget());
    EXPECT_FALSE(newMask->isEnabled());
    EXPECT_TRUE(apply->isEnabled());
    const QModelIndex index = model->indexOf(s.top);
    EXPECT_TRUE(index.data(Role::MaskTargetRole).toBool());

    const QRect row = view->visualRect(index);
    const int thumb = row.left() + 4 + 18 + 8 + 14, switches = thumb + 40 + 3, mask = switches + 14 + 3;
    click(QPoint(thumb + 20, row.center().y()));
    EXPECT_FALSE(s.doc.maskTarget()) << "the layer's thumbnail chooses the layer";
    click(QPoint(mask + 20, row.center().y()));
    EXPECT_TRUE(s.doc.maskTarget()) << "the mask's thumbnail chooses the mask";
    click(QPoint(switches + 7, row.center().y() - 8));
    EXPECT_FALSE(s.doc.layer(s.top)->props.maskEnabled);
    click(QPoint(switches + 7, row.center().y() + 8));
    EXPECT_FALSE(s.doc.layer(s.top)->props.maskLinked);
    EXPECT_EQ(s.doc.currentLayerId(), s.top);

    const int layers = s.doc.layers().layerCount();
    child<QToolButton>(panel, "deleteButton")->click();
    EXPECT_FALSE(s.doc.layer(s.top)->mask) << "Delete removes the target mask";
    EXPECT_EQ(s.doc.layers().layerCount(), layers);
    EXPECT_FALSE(s.doc.maskTarget());
    newMask->click();
    apply->click();
    EXPECT_FALSE(s.doc.layer(s.top)->mask);
}

TEST(LayerPanel, DeleteButtonIsOffWhileThereIsOnlyOneLayer) {
    Document doc(QSize(32, 32), Qt::white);
    LayerPanel panel;
    panel.setDocument(&doc);
    auto* remove = child<QToolButton>(panel, "deleteButton");
    EXPECT_FALSE(remove->isEnabled());
    remove->click();
    EXPECT_EQ(doc.layers().layerCount(), 1);
    EXPECT_EQ(doc.undoStack()->count(), 0);

    ASSERT_TRUE(doc.addLayer());
    EXPECT_TRUE(remove->isEnabled());
    remove->click();
    EXPECT_EQ(doc.layers().layerCount(), 1);
    EXPECT_FALSE(remove->isEnabled());
    ASSERT_TRUE(doc.undo());
    EXPECT_TRUE(remove->isEnabled()) << "undo brings the second layer back";
    ASSERT_TRUE(doc.undo());
    EXPECT_FALSE(remove->isEnabled());

    ASSERT_TRUE(doc.addLayerMask(doc.currentLayerId()));
    EXPECT_TRUE(doc.maskTarget());
    EXPECT_TRUE(remove->isEnabled()) << "with the mask as the target, Delete removes the mask";
    remove->click();
    EXPECT_FALSE(doc.currentLayer()->mask);
    EXPECT_FALSE(remove->isEnabled());
}

TEST(LayerPanel, RefusedControlEditIsReportedAndRevertedInTheControl) {
    Sample s;
    LayerPanel panel;
    panel.setDocument(&s.doc);
    QStringList refused;
    QObject::connect(&panel, &LayerPanel::editRefused, &panel, [&refused](const QString& m) { refused << m; });
    auto* bar = child<ValueBar>(panel, "opacityBar");
    s.doc.setPainting(true);
    bar->setValue(10);
    ASSERT_EQ(refused.size(), 1);
    EXPECT_EQ(refused.first(), describe(EditError::StrokeInProgress));
    EXPECT_EQ(bar->value(), 100);
    s.doc.setPainting(false);
}

TEST(LayerPanel, NoDocumentDisablesTheControlsAndSurvivesDocumentDeletion) {
    auto doc = std::make_unique<Document>(QSize(32, 32), Qt::white);
    LayerPanel panel;
    EXPECT_FALSE(child<QToolButton>(panel, "newLayerButton")->isEnabled());
    EXPECT_FALSE(child<QComboBox>(panel, "blendModeCombo")->isEnabled());
    panel.setDocument(doc.get());
    EXPECT_TRUE(child<QToolButton>(panel, "newLayerButton")->isEnabled());
    EXPECT_TRUE(child<QComboBox>(panel, "blendModeCombo")->isEnabled());
    EXPECT_EQ(panel.document(), doc.get());

    doc.reset();
    EXPECT_EQ(panel.document(), nullptr);
    EXPECT_FALSE(child<QToolButton>(panel, "deleteButton")->isEnabled());
    EXPECT_FALSE(child<ValueBar>(panel, "opacityBar")->isEnabled());
    EXPECT_EQ(panel.findChild<LayerTreeModel*>()->rowCount(), 0);

    panel.setDocument(nullptr);
    EXPECT_FALSE(child<QToolButton>(panel, "newLayerButton")->isEnabled());
}

TEST(LayerPanel, SwitchingDocumentsRebindsEverything) {
    Sample a, b;
    LayerPanel panel;
    panel.setDocument(&a.doc);
    panel.setDocument(&b.doc);
    child<ValueBar>(panel, "opacityBar")->setValue(55);
    EXPECT_EQ(b.doc.currentLayer()->props.opacity, 55);
    EXPECT_EQ(a.doc.currentLayer()->props.opacity, 100);
    a.doc.setCurrentLayer(a.bottom);
    EXPECT_EQ(panel.findChild<QTreeView*>()->currentIndex(), panel.findChild<LayerTreeModel*>()->indexOf(b.top));
}

TEST(LayerPanel, IconsLoad) {
    for (const char* name :
         {"layer-new", "folder-new", "layer-duplicate", "layer-delete", "layer-merge-down", "visible", "hidden", "lock", "folder",
          "folder-open", "layer-merge", "layer-clear", "mask-new", "mask-apply", "mask-on", "mask-link", "target-mask", "tool-pencil"}) {
        const QIcon icon(QStringLiteral(":/icons/%1.svg").arg(QString::fromLatin1(name)));
        EXPECT_FALSE(icon.isNull()) << name;
        EXPECT_FALSE(icon.pixmap(24, 24).isNull()) << name;
    }
}

TEST(LayerPanel, SavesAScreenshotOfASampleDocument) {
    test::applyTheme();

    Document doc(QSize(320, 240), QColor(Qt::white));
    const auto add = [&doc](const QString& name, BlendMode mode, int opacity, const QColor& color, const QRect& area) {
        const LayerId id = doc.addLayer().value();
        paintRect(doc, id, area, color);
        setProps(doc, id, [&](LayerProps& p) {
            p.name = name;
            p.mode = mode;
            p.opacity = opacity;
        });
        return id;
    };
    const LayerId background = doc.currentLayerId();
    paintRect(doc, background, QRect(0, 0, 320, 240), QColor(0x8f, 0xc1, 0xe8));
    setProps(doc, background, [](LayerProps& p) { p.name = QStringLiteral("Background"); });
    add(QStringLiteral("Shadow"), BlendMode::Multiply, 75, QColor(0x55, 0x3a, 0x7a), QRect(40, 170, 240, 50));
    const LayerId folder = doc.addFolder().value();
    setProps(doc, folder, [](LayerProps& p) { p.name = QStringLiteral("Character"); });
    const LayerId skin = add(QStringLiteral("Skin"), BlendMode::Normal, 100, QColor(0xf0, 0xc8, 0xa0), QRect(110, 50, 100, 140));
    const LayerId blush = add(QStringLiteral("Blush"), BlendMode::Multiply, 60, QColor(0xff, 0x90, 0xa0), QRect(125, 110, 70, 18));
    const LayerId hair = add(QStringLiteral("Hair"), BlendMode::Normal, 100, QColor(0x5a, 0x34, 0x20), QRect(100, 20, 120, 50));
    const LayerId lines =
        add(QStringLiteral("Line art with a rather long name"), BlendMode::Normal, 100, QColor(0x20, 0x20, 0x28), QRect(100, 18, 120, 6));
    setProps(doc, blush, [](LayerProps& p) { p.clipping = true; });
    setProps(doc, lines, [](LayerProps& p) { p.locked = true; });
    for (const LayerId id : {skin, blush, hair, lines})
        Sample::must(doc.moveLayer(id, folder, 1000));
    doc.setCurrentLayer(folder);
    add(QStringLiteral("Highlights"), BlendMode::Screen, 40, QColor(0xff, 0xf0, 0xb0), QRect(0, 0, 320, 70));
    const LayerId sketch = add(QStringLiteral("Sketch"), BlendMode::Normal, 100, QColor(0x80, 0x80, 0x80), QRect(0, 0, 320, 240));
    setProps(doc, sketch, [](LayerProps& p) { p.visible = false; });
    Sample::must(doc.addLayerMask(hair));
    Sample::must(doc.setSelection(selection::rectangle(doc.size(), QRectF(0, 0, 160, 240), false)));
    Sample::must(doc.clearLayer(hair));
    Sample::must(doc.deselect());
    doc.setCurrentLayer(blush);

    LayerPanel panel;
    panel.setDocument(&doc);
    panel.resize(300, 640);
    panel.show();
    QCoreApplication::processEvents();
    const QPixmap shot = panel.grab();
    ASSERT_FALSE(shot.isNull());

    EXPECT_TRUE(test::saveScreenshot(shot, QStringLiteral("layer_panel")));
}

// The Layer Effect is a greyed placeholder until layer styles replace it: it opens nothing and says so in its tip.
TEST(LayerPanelEffect, TheLayerEffectIsAGreyedPlaceholder) {
    Sample s;
    LayerPanel panel;
    panel.setDocument(&s.doc);
    panel.resize(260, 600);
    panel.show();
    QCoreApplication::processEvents();
    auto* effect = child<QToolButton>(panel, "layerEffectExpander");
    EXPECT_TRUE(effect->isVisible());
    EXPECT_FALSE(effect->isEnabled());
    EXPECT_FALSE(effect->isCheckable());
    EXPECT_EQ(effect->toolTip(), QStringLiteral("TODO: Implement"));
    EXPECT_EQ(panel.findChild<QWidget*>(QStringLiteral("paperList")), nullptr) << "nothing of the paper texture is left to reach";
    EXPECT_EQ(panel.findChild<QWidget*>(QStringLiteral("paperIntensityBar")), nullptr);
}

// A layer that has a paper texture (from a file made before) keeps it: it is still shown, saved and marked "Pap" in its row.
TEST(LayerPanelEffect, ALayerWithPaperKeepsItAndShowsPap) {
    Sample s;
    LayerProps props = s.doc.layer(s.bottom)->props;
    props.paper.texture = QStringLiteral("Weave");
    ASSERT_TRUE(s.doc.setLayerProps(s.bottom, props, 0));
    LayerTreeModel model;
    model.setDocument(&s.doc);
    EXPECT_TRUE(model.data(model.indexOf(s.bottom), Role::PaperRole).toBool());
    EXPECT_FALSE(model.data(model.indexOf(s.top), Role::PaperRole).toBool());
    EXPECT_EQ(s.doc.layer(s.bottom)->props.paper.texture, QStringLiteral("Weave"));
}
