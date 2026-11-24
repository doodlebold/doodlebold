#include "core/layers/LayerStack.h"

#include <gtest/gtest.h>

using namespace doodlebold;

namespace {

QStringList names(const LayerStack& s) {
    QStringList out;
    for (const LayerNode* n : s.displayOrder())
        out << n->props.name;
    return out;
}

}

TEST(LayerStack, InsertOrderAndDisplayOrder) {
    LayerStack s(QSize(10, 10));
    const LayerId a = s.insert(s.makeRaster(QStringLiteral("A")), kNoLayer, -1);
    const LayerId f = s.insert(s.makeFolder(QStringLiteral("F")), kNoLayer, -1);
    s.insert(s.makeRaster(QStringLiteral("B")), f, -1);
    s.insert(s.makeRaster(QStringLiteral("C")), f, -1);
    EXPECT_EQ(names(s), (QStringList{"F", "C", "B", "A"}));
    EXPECT_EQ(s.depthOf(a), 0);
    EXPECT_EQ(s.depthOf(s.below(f)), 1);
    EXPECT_EQ(s.below(f), s.find(s.below(f))->props.id);
    EXPECT_EQ(s.layerCount(), 4);
}

TEST(LayerStack, MoveRejectsCyclesAndReorders) {
    LayerStack s(QSize(10, 10));
    const LayerId a = s.insert(s.makeRaster(QStringLiteral("A")), kNoLayer, -1);
    const LayerId f = s.insert(s.makeFolder(QStringLiteral("F")), kNoLayer, -1);
    const LayerId g = s.insert(s.makeFolder(QStringLiteral("G")), f, -1);
    EXPECT_FALSE(s.move(f, g, 0));
    EXPECT_FALSE(s.move(f, f, 0));
    EXPECT_TRUE(s.move(a, g, 0));
    EXPECT_EQ(s.parentOf(a)->props.id, g);
    EXPECT_TRUE(s.isAncestor(f, a));
    EXPECT_TRUE(s.move(a, kNoLayer, 0));
    EXPECT_EQ(names(s), (QStringList{"F", "G", "A"}));
}

TEST(LayerStack, NamesFollowTheLayerNumberPattern) {
    LayerStack s(QSize(10, 10));
    s.insert(s.makeRaster(s.nextLayerName()), kNoLayer, -1);
    s.insert(s.makeRaster(s.nextLayerName()), kNoLayer, -1);
    EXPECT_EQ(names(s), (QStringList{"Layer2", "Layer1"}));
    EXPECT_EQ(s.nextFolderName(), QStringLiteral("Folder1"));
}

TEST(LayerStack, CopyIsIndependentSnapshot) {
    LayerStack s(QSize(64, 64));
    const LayerId a = s.insert(s.makeRaster(QStringLiteral("A")), kNoLayer, -1);
    s.find(a)->pixels.fill(Qt::red);
    const LayerStack snap = s;
    s.find(a)->pixels.fill(Qt::blue);
    s.find(a)->props.opacity = 10;
    EXPECT_EQ(toUnorm8(snap.find(a)->pixels.pixel(0, 0))[0], 255);
    EXPECT_EQ(snap.find(a)->props.opacity, 100);
}
