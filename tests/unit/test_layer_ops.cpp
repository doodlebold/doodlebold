#include "core/document/Document.h"
#include "core/document/LayerOps.h"

#include <gtest/gtest.h>

using namespace doodlebold;

namespace {

DocumentState makeState(int layers = 1) {
    DocumentState s;
    s.layers = LayerStack(QSize(64, 64));
    for (int i = 0; i < layers; ++i)
        s.current = s.layers.insert(s.layers.makeRaster(s.layers.nextLayerName()), kNoLayer, -1);
    return s;
}

}

TEST(LayerOps, AddLayerGoesAboveCurrentInSameFolder) {
    DocumentState s = makeState();
    const LayerId f = layer_ops::addFolder(s).value();
    const LayerId inner = s.layers.insert(s.layers.makeRaster(QStringLiteral("inner")), f, -1);
    s.current = inner;
    const LayerId added = layer_ops::addLayer(s).value();
    EXPECT_EQ(s.layers.parentOf(added)->props.id, f);
    EXPECT_EQ(s.layers.indexOf(added), s.layers.indexOf(inner) + 1);
    EXPECT_EQ(s.current, added);
}

TEST(LayerOps, FoldersNestTenLevelsDeep) {
    DocumentState s = makeState();
    LayerId parent = kNoLayer;
    for (int i = 0; i < 9; ++i)
        parent = s.layers.insert(s.layers.makeFolder(QStringLiteral("f")), parent, -1);
    s.current = s.layers.insert(s.layers.makeRaster(QStringLiteral("inner")), parent, -1);
    const auto tenth = layer_ops::addFolder(s);
    ASSERT_TRUE(tenth.ok()) << "the tenth level";
    s.current = s.layers.insert(s.layers.makeRaster(QStringLiteral("in the tenth")), tenth.value(), -1);
    const int before = s.layers.layerCount();
    const auto eleventh = layer_ops::addFolder(s);
    ASSERT_FALSE(eleventh.ok());
    EXPECT_EQ(eleventh.error(), EditError::FoldersTooDeep);
    EXPECT_EQ(s.layers.layerCount(), before);
    EXPECT_TRUE(layer_ops::addLayer(s).ok()) << "layers still go into the tenth folder";
}

TEST(LayerOps, MovingAFolderCountsTheFoldersInsideIt) {
    DocumentState s = makeState();
    LayerId deep = kNoLayer;
    for (int i = 0; i < 8; ++i)
        deep = s.layers.insert(s.layers.makeFolder(QStringLiteral("f")), deep, -1);
    const LayerId outer = s.layers.insert(s.layers.makeFolder(QStringLiteral("outer")), kNoLayer, -1);
    const LayerId inner = s.layers.insert(s.layers.makeFolder(QStringLiteral("inner")), outer, -1);
    EXPECT_TRUE(layer_ops::move(s, inner, deep, 0).ok()) << "inner becomes the ninth level";
    EXPECT_TRUE(layer_ops::move(s, inner, outer, 0).ok()) << "and back";
    EXPECT_TRUE(layer_ops::move(s, outer, deep, 0).ok()) << "outer and inner become the ninth and tenth levels";
    EXPECT_TRUE(layer_ops::move(s, outer, kNoLayer, -1).ok()) << "and back";
    const LayerId innermost = s.layers.insert(s.layers.makeFolder(QStringLiteral("innermost")), inner, -1);
    const auto tooDeep = layer_ops::move(s, outer, deep, 0);
    ASSERT_FALSE(tooDeep.ok());
    EXPECT_EQ(tooDeep.error(), EditError::FoldersTooDeep);
    EXPECT_EQ(s.layers.parentOf(outer), &s.layers.root()) << "it stays where it was";
    EXPECT_EQ(s.layers.parentOf(innermost)->props.id, inner);
    EXPECT_EQ(layer_ops::move(s, outer, innermost, 0).error(), EditError::InvalidMove) << "into itself is still another refusal";
}

// The layer panel's drag and drop moves through moveLayers: folders nested past ten levels make a file that won't open.
TEST(LayerOps, MovingSeveralLayersCountsTheFoldersInsideThem) {
    DocumentState s = makeState();
    LayerId deep = kNoLayer;
    for (int i = 0; i < 9; ++i)
        deep = s.layers.insert(s.layers.makeFolder(QStringLiteral("f")), deep, -1);
    const LayerId outer = s.layers.insert(s.layers.makeFolder(QStringLiteral("outer")), kNoLayer, -1);
    const LayerId inner = s.layers.insert(s.layers.makeFolder(QStringLiteral("inner")), outer, -1);
    const LayerId loose = s.layers.insert(s.layers.makeRaster(QStringLiteral("loose")), kNoLayer, -1);
    const auto tooDeep = layer_ops::moveLayers(s, {outer, loose}, deep, 0);
    ASSERT_FALSE(tooDeep.ok());
    EXPECT_EQ(tooDeep.error(), EditError::FoldersTooDeep);
    EXPECT_EQ(s.layers.parentOf(outer), &s.layers.root()) << "nothing moved";
    EXPECT_EQ(s.layers.parentOf(loose), &s.layers.root());
    EXPECT_EQ(s.layers.parentOf(inner)->props.id, outer);
    EXPECT_TRUE(layer_ops::moveLayers(s, {inner, loose}, deep, 0).ok()) << "one folder still fits as the tenth level";
}

TEST(LayerOps, DuplicateSharesPixelsAndGetsFreshIds) {
    DocumentState s = makeState();
    const LayerId f = layer_ops::addFolder(s).value();
    const LayerId child = s.layers.insert(s.layers.makeRaster(QStringLiteral("c")), f, -1);
    s.layers.find(child)->pixels.fill(Qt::red);
    const LayerId copy = layer_ops::duplicate(s, f).value();
    ASSERT_NE(copy, f);
    const LayerNode* c = s.layers.find(copy);
    ASSERT_EQ(c->children.size(), 1u);
    EXPECT_NE(c->children[0].props.id, child);
    EXPECT_EQ(c->children[0].pixels.tileShared(0, 0), s.layers.find(child)->pixels.tileShared(0, 0));
}

TEST(LayerOps, RemoveSelectsLayerBelowAndKeepsOne) {
    DocumentState s = makeState(3);
    const auto order = s.layers.displayOrder();
    const LayerId top = order[0]->props.id, middle = order[1]->props.id;
    ASSERT_TRUE(layer_ops::remove(s, top));
    EXPECT_EQ(s.current, middle);
    DocumentState single = makeState(1);
    EXPECT_EQ(layer_ops::remove(single, single.current).error(), EditError::LastLayer);
}

TEST(LayerOps, MergeDownErrors) {
    DocumentState s = makeState(1);
    EXPECT_EQ(layer_ops::mergeDown(s, s.current).error(), EditError::NothingBelow);
    EXPECT_EQ(layer_ops::mergeDown(s, LayerId(999)).error(), EditError::NoSuchLayer);
    const LayerId bottom = s.current;
    const LayerId top = layer_ops::addLayer(s).value();
    s.layers.find(bottom)->props.locked = true;
    EXPECT_EQ(layer_ops::mergeDown(s, top).error(), EditError::LayerLocked);
}

TEST(LayerOps, MergeDownKeepsLowerPropsAndClippedResult) {
    DocumentState s = makeState(1);
    const LayerId bottom = s.current;
    s.layers.find(bottom)->pixels.fill(QRect(0, 0, 32, 64), QColor(0, 0, 255));
    s.layers.find(bottom)->props.opacity = 70;
    const LayerId top = layer_ops::addLayer(s).value();
    s.layers.find(top)->pixels.fill(QColor(255, 0, 0));
    s.layers.find(top)->props.clipping = true;
    ASSERT_TRUE(layer_ops::mergeDown(s, top));
    const LayerNode* merged = s.layers.find(bottom);
    EXPECT_EQ(merged->props.opacity, 70);
    EXPECT_EQ(toUnorm8(merged->pixels.pixel(10, 10))[0], 255);
    EXPECT_EQ(toUnorm8(merged->pixels.pixel(50, 10))[3], 0);
}

TEST(LayerOps, MovesAndOrderHelpers) {
    DocumentState s = makeState(3);
    const LayerId bottom = s.layers.root().children[0].props.id;
    EXPECT_EQ(layer_ops::lower(s, bottom).error(), EditError::NoChange);
    ASSERT_TRUE(layer_ops::toFront(s, bottom));
    EXPECT_EQ(s.layers.indexOf(bottom), 2);
    ASSERT_TRUE(layer_ops::lower(s, bottom));
    EXPECT_EQ(s.layers.indexOf(bottom), 1);
    const LayerId f = layer_ops::addFolder(s).value();
    EXPECT_EQ(layer_ops::move(s, f, f, 0).error(), EditError::InvalidMove);
}

TEST(LayerOps, SetPropsNormalisesAndDetectsNoChange) {
    DocumentState s = makeState();
    LayerProps p = s.layers.find(s.current)->props;
    EXPECT_EQ(layer_ops::setProps(s, s.current, p).error(), EditError::NoChange);
    p.opacity = 140;
    p.mode = BlendMode::PassThrough;
    EXPECT_EQ(layer_ops::setProps(s, s.current, p).error(), EditError::NoChange);
    p.opacity = -5;
    ASSERT_TRUE(layer_ops::setProps(s, s.current, p));
    EXPECT_EQ(s.layers.find(s.current)->props.opacity, 0);
    EXPECT_EQ(s.layers.find(s.current)->props.mode, BlendMode::Normal);
}

TEST(LayerOps, FillRespectsPreserveOpacityAndLocks) {
    DocumentState s = makeState();
    LayerNode* n = s.layers.find(s.current);
    n->pixels.fill(QRect(0, 0, 10, 10), QColor(255, 255, 255, 64));
    n->props.preserveOpacity = true;
    ASSERT_TRUE(layer_ops::fill(s, s.current, QColor(255, 0, 0)));
    EXPECT_EQ(toUnorm8(n->pixels.pixel(5, 5))[3], 64);
    EXPECT_EQ(toUnorm8(n->pixels.pixel(5, 5))[0], 64);
    EXPECT_EQ(toUnorm8(n->pixels.pixel(30, 30))[3], 0);
    n->props.locked = true;
    EXPECT_EQ(layer_ops::fill(s, s.current, Qt::blue).error(), EditError::LayerLocked);
    EXPECT_EQ(layer_ops::clear(s, s.current).error(), EditError::LayerLocked);
}

TEST(LayerOpsSeveralLayers, MergeGoesIntoTheLowestAndMatchesRepeatedMergeDown) {
    Document doc(QSize(32, 32), Qt::white);
    const LayerId low = doc.currentLayerId();
    const LayerId mid = doc.addLayer().value();
    const LayerId high = doc.addLayer().value();
    ASSERT_TRUE(doc.fillLayer(low, QColor(200, 0, 0)).ok());
    ASSERT_TRUE(doc.fillLayer(mid, QColor(0, 200, 0)).ok());
    ASSERT_TRUE(doc.fillLayer(high, QColor(0, 0, 200)).ok());
    LayerProps props = doc.layer(high)->props;
    props.opacity = 50;
    ASSERT_TRUE(doc.setLayerProps(high, props).ok());
    props = doc.layer(low)->props;
    props.opacity = 80;
    props.name = QStringLiteral("Base");
    ASSERT_TRUE(doc.setLayerProps(low, props).ok());

    Document stepwise(QSize(32, 32), Qt::white);
    const LayerId sLow = stepwise.currentLayerId();
    const LayerId sMid = stepwise.addLayer().value();
    const LayerId sHigh = stepwise.addLayer().value();
    ASSERT_TRUE(stepwise.fillLayer(sLow, QColor(200, 0, 0)).ok());
    ASSERT_TRUE(stepwise.fillLayer(sMid, QColor(0, 200, 0)).ok());
    ASSERT_TRUE(stepwise.fillLayer(sHigh, QColor(0, 0, 200)).ok());
    props = stepwise.layer(sHigh)->props;
    props.opacity = 50;
    ASSERT_TRUE(stepwise.setLayerProps(sHigh, props).ok());
    ASSERT_TRUE(stepwise.mergeDown(sHigh).ok());
    ASSERT_TRUE(stepwise.mergeDown(sMid).ok());

    const int steps = doc.undoStack()->count();
    const auto merged = doc.mergeLayers({high, low, mid});
    ASSERT_TRUE(merged.ok());
    EXPECT_EQ(merged.value(), low);
    EXPECT_EQ(doc.undoStack()->count(), steps + 1);
    EXPECT_EQ(doc.layers().layerCount(), 1);
    const LayerNode* result = doc.layer(low);
    ASSERT_TRUE(result);
    EXPECT_EQ(result->props.name, QStringLiteral("Base"));
    EXPECT_EQ(result->props.opacity, 80);
    EXPECT_EQ(doc.currentLayerId(), low);
    const Tile* ours = result->pixels.tile(0, 0);
    const Tile* theirs = stepwise.layer(sLow)->pixels.tile(0, 0);
    ASSERT_TRUE(ours && theirs);
    EXPECT_EQ(ours->px[0], theirs->px[0]);
    EXPECT_EQ(ours->px[1], theirs->px[1]);
    EXPECT_EQ(ours->px[2], theirs->px[2]);
    EXPECT_EQ(ours->px[3], theirs->px[3]);

    ASSERT_TRUE(doc.undo().ok());
    EXPECT_EQ(doc.layers().layerCount(), 3);
}

TEST(LayerOpsSeveralLayers, MergeTakesAFolderWithItsLayersAndHiddenLayersGoIn) {
    Document doc(QSize(32, 32), Qt::white);
    const LayerId low = doc.currentLayerId();
    const LayerId folder = doc.addFolder().value();
    const LayerId inside = doc.addLayer().value();
    ASSERT_TRUE(doc.moveLayer(inside, folder, 0).ok());
    ASSERT_TRUE(doc.fillLayer(inside, QColor(0, 200, 0)).ok());
    const LayerId hidden = doc.addLayer().value();
    ASSERT_TRUE(doc.moveLayer(hidden, kNoLayer, 2).ok());
    ASSERT_TRUE(doc.fillLayer(hidden, QColor(0, 0, 200)).ok());
    LayerProps props = doc.layer(hidden)->props;
    props.visible = false;
    ASSERT_TRUE(doc.setLayerProps(hidden, props).ok());

    ASSERT_TRUE(doc.mergeLayers({folder, inside, hidden}).ok());
    EXPECT_FALSE(doc.layer(inside));
    EXPECT_FALSE(doc.layer(hidden));
    const LayerNode* result = doc.layer(folder);
    ASSERT_TRUE(result);
    EXPECT_FALSE(result->isFolder());
    EXPECT_NE(result->props.mode, BlendMode::PassThrough);
    EXPECT_TRUE(result->children.empty());
    EXPECT_FALSE(result->pixels.isEmpty());
    EXPECT_TRUE(doc.layer(low));
}

TEST(LayerOpsSeveralLayers, MergeRefusesLockedLayersAndNeedsTwo) {
    Document doc(QSize(32, 32), Qt::white);
    const LayerId low = doc.currentLayerId();
    const LayerId high = doc.addLayer().value();
    EXPECT_EQ(doc.mergeLayers({high}).error(), EditError::NoChange);
    LayerProps props = doc.layer(low)->props;
    props.locked = true;
    ASSERT_TRUE(doc.setLayerProps(low, props).ok());
    EXPECT_EQ(doc.mergeLayers({low, high}).error(), EditError::LayerLocked);
    EXPECT_EQ(doc.layers().layerCount(), 2);
}

TEST(LayerOpsSeveralLayers, DeleteIsOneStepAndKeepsALayer) {
    Document doc(QSize(32, 32), Qt::white);
    const LayerId low = doc.currentLayerId();
    const LayerId mid = doc.addLayer().value();
    const LayerId high = doc.addLayer().value();
    const int steps = doc.undoStack()->count();
    ASSERT_TRUE(doc.deleteLayers({high, mid}).ok());
    EXPECT_EQ(doc.undoStack()->count(), steps + 1);
    EXPECT_EQ(doc.layers().layerCount(), 1);
    EXPECT_EQ(doc.currentLayerId(), low);
    ASSERT_TRUE(doc.undo().ok());
    EXPECT_EQ(doc.layers().layerCount(), 3);
    EXPECT_EQ(doc.deleteLayers({low, mid, high}).error(), EditError::LastLayer);
    EXPECT_EQ(doc.layers().layerCount(), 3);
}

TEST(LayerOpsSeveralLayers, ClearDoesWhatItCanAndRefusesOnlyIfNothingCould) {
    Document doc(QSize(32, 32), Qt::white);
    const LayerId open = doc.currentLayerId();
    const LayerId locked = doc.addLayer().value();
    const LayerId folder = doc.addFolder().value();
    ASSERT_TRUE(doc.fillLayer(open, QColor(200, 0, 0)).ok());
    ASSERT_TRUE(doc.fillLayer(locked, QColor(0, 200, 0)).ok());
    LayerProps props = doc.layer(locked)->props;
    props.locked = true;
    ASSERT_TRUE(doc.setLayerProps(locked, props).ok());

    ASSERT_TRUE(doc.clearLayers({open, locked, folder}).ok());
    EXPECT_TRUE(doc.layer(open)->pixels.isEmpty());
    EXPECT_FALSE(doc.layer(locked)->pixels.isEmpty());
    EXPECT_EQ(doc.clearLayers({locked, folder}).error(), EditError::LayerLocked);
    EXPECT_EQ(doc.clearLayers({open}).error(), EditError::NoChange);
}

TEST(LayerOps, ANewFolderIsNormalAndCanBePassThrough) {
    DocumentState s = makeState();
    const LayerId folder = layer_ops::addFolder(s).value();
    EXPECT_EQ(s.layers.find(folder)->props.mode, BlendMode::Normal);
    LayerProps props = s.layers.find(folder)->props;
    props.mode = BlendMode::PassThrough;
    ASSERT_TRUE(layer_ops::setProps(s, folder, props));
    EXPECT_EQ(s.layers.find(folder)->props.mode, BlendMode::PassThrough);
}

TEST(LayerOps, DeletingTheLastLayerIsRefusedEvenWithFoldersLeft) {
    DocumentState s = makeState();
    const LayerId layer = s.current;
    layer_ops::addFolder(s).value();
    EXPECT_EQ(layer_ops::remove(s, layer).error(), EditError::LastLayer);
    DocumentState deleteFolder = makeState();
    const LayerId empty = layer_ops::addFolder(deleteFolder).value();
    EXPECT_TRUE(layer_ops::remove(deleteFolder, empty)) << "the folder can go: the layer stays";

    DocumentState inFolder = makeState();
    const LayerId holder = layer_ops::addFolder(inFolder).value();
    const LayerId inner = inFolder.layers.insert(inFolder.layers.makeRaster(QStringLiteral("inner")), holder, -1);
    const LayerId outer = inFolder.layers.root().children.front().props.id;
    ASSERT_TRUE(layer_ops::remove(inFolder, outer)) << "the layer inside the folder still counts";
    DocumentState copy = inFolder;  // a refused edit leaves its state half changed
    EXPECT_EQ(layer_ops::remove(copy, holder).error(), EditError::LastLayer);
    EXPECT_EQ(layer_ops::remove(inFolder, inner).error(), EditError::LastLayer);
}

TEST(LayerOpsSeveralLayers, DeleteRefusesToLeaveOnlyFolders) {
    Document doc(QSize(32, 32), Qt::white);
    const LayerId first = doc.currentLayerId();
    const LayerId second = doc.addLayer().value();
    ASSERT_TRUE(doc.addFolder().ok());
    EXPECT_EQ(doc.deleteLayers({first, second}).error(), EditError::LastLayer);
    EXPECT_EQ(doc.layers().layerCount(), 3);
}
