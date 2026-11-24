#include "core/document/Document.h"
#include "core/selection/SelectionOps.h"

#include <cmath>
#include <numbers>

#include <gtest/gtest.h>

using namespace doodlebold;

namespace {

void must(const EditResult<>& r) {
    ASSERT_TRUE(r.ok()) << qPrintable(describe(r.error()));
}

struct Probe {
    Document doc{QSize(200, 150), Qt::white};
    LayerId l1, l2, l3;
    Probe() {
        l1 = doc.currentLayerId();
        must(doc.fillLayer(l1, Qt::red));
        l2 = doc.addLayer().value();
        must(doc.setSelection(selection::rectangle(doc.size(), QRectF(20, 65, 160, 20), false)));
        must(doc.fillLayer(l2, Qt::blue));
        must(doc.deselect());
        LayerProps p = doc.layer(l2)->props;
        p.opacity = 50;
        must(doc.setLayerProps(l2, p));
        l3 = doc.addLayer().value();
        must(doc.fillLayer(l3, Qt::green));
        p = doc.layer(l3)->props;
        p.visible = false;
        must(doc.setLayerProps(l3, p));
        doc.setCurrentLayer(l2);
    }
    std::vector<QString> names() const {
        std::vector<QString> out;
        for (const LayerNode& n : doc.layers().root().children)
            out.push_back(n.props.name);
        return out;
    }
};

}

TEST(LayerCommands, TransferEmptiesTheLayerButKeepsIt) {
    Probe p;
    const QImage before = p.doc.projection().copy();
    must(p.doc.transferDown(p.l2));
    EXPECT_TRUE(p.doc.layer(p.l2)->pixels.isEmpty());
    EXPECT_EQ(p.doc.layer(p.l2)->props.opacity, 50) << "its properties stay";
    EXPECT_EQ(p.doc.currentLayerId(), p.l2) << "and it stays current";
    EXPECT_EQ(p.doc.projection(), before) << "the picture does not change";
    EXPECT_NE(p.doc.layer(p.l1)->pixels.pixel(100, 75), p.doc.layer(p.l1)->pixels.pixel(100, 10)) << "the band is in Layer1";
    EXPECT_EQ(p.doc.transferDown(p.l2).error(), EditError::NoChange) << "nothing left to transfer";
}

TEST(LayerCommands, MergeVisibleMakesANewTopLayerAndKeepsHiddenOnes) {
    Probe p;
    const QImage before = p.doc.projection().copy();
    const auto merged = p.doc.mergeVisible();
    ASSERT_TRUE(merged);
    EXPECT_EQ(p.names(), (std::vector<QString>{"Layer3", "Layer4"})) << "the hidden layer below, the new one on top";
    EXPECT_EQ(p.doc.currentLayerId(), merged.value());
    EXPECT_EQ(p.doc.layer(merged.value())->props.opacity, 100);
    EXPECT_EQ(p.doc.projection(), before);
}

TEST(LayerCommands, FlattenLeavesOneLayer) {
    Probe p;
    const QImage before = p.doc.projection().copy();
    ASSERT_TRUE(p.doc.flattenImage());
    EXPECT_EQ(p.names(), (std::vector<QString>{"Layer4"})) << "hidden layers are dropped";
    EXPECT_EQ(p.doc.projection(), before);
}

TEST(LayerCommands, ReverseCanvasMirrorsLayersMasksAndSelection) {
    Probe p;
    must(p.doc.addLayerMask(p.l1));
    must(p.doc.setSelection(selection::rectangle(p.doc.size(), QRectF(0, 0, 30, 150), false)));
    must(p.doc.clearLayer(p.l1));
    must(p.doc.reverseCanvas(true));
    EXPECT_EQ(p.doc.layer(p.l1)->mask->at(185, 10), 0) << "the hidden strip is on the right now";
    EXPECT_EQ(p.doc.layer(p.l1)->mask->at(10, 10), SelectionMask::kOne);
    ASSERT_TRUE(p.doc.selection());
    EXPECT_EQ(p.doc.selection()->at(190, 10), SelectionMask::kOne);
    EXPECT_EQ(p.doc.selection()->at(10, 10), 0);
    const auto band = p.doc.layer(p.l2)->pixels.contentBounds(true);
    EXPECT_EQ(band, QRect(20, 65, 160, 20)) << "symmetric, so in place";
    must(p.doc.reverseCanvas(false));
    EXPECT_EQ(p.doc.layer(p.l2)->pixels.contentBounds(true), QRect(20, 65, 160, 20).translated(0, 150 - 65 - 20 - 65));
}

TEST(LayerCommands, CopyBySelectionBoundsTakesTheWholeRectangle) {
    Document doc(QSize(100, 100), Qt::white);
    must(doc.fillLayer(doc.currentLayerId(), Qt::red));
    QPolygonF circle;
    for (int i = 0; i < 64; ++i)
        circle << QPointF(40 + 20 * std::cos(i * std::numbers::pi / 32), 40 + 20 * std::sin(i * std::numbers::pi / 32));
    must(doc.setSelection(selection::polygon(doc.size(), circle)));
    const auto plain = doc.copy(doc.currentLayerId());
    const auto bounds = doc.copyBounds(doc.currentLayerId());
    ASSERT_TRUE(plain && bounds);
    EXPECT_EQ(bounds.value().image.size(), QSize(40, 40));
    EXPECT_EQ(qAlpha(bounds.value().image.pixel(0, 0)), 255) << "the corner outside the ellipse is copied too";
    EXPECT_LT(qAlpha(plain.value().image.pixel(0, 0)), 255) << "the ordinary Copy follows the selection";
    must(doc.deselect());
    EXPECT_FALSE(doc.copyBounds(doc.currentLayerId())) << "needs a selection";
}

TEST(LayerCommands, TrimCanvasCropsToTheSelectionAndKeepsIt) {
    Probe p;
    const auto colourAt = [&](int x, int y) { return p.doc.layer(p.l2)->pixels.pixel(x, y); };
    const auto before = colourAt(60, 70);
    must(p.doc.setSelection(selection::rectangle(p.doc.size(), QRectF(50, 30, 101, 71), false)));
    QSize resized;
    QObject::connect(&p.doc, &Document::canvasResized, [&](const QSize& s) { resized = s; });
    must(p.doc.trimCanvas());
    EXPECT_EQ(p.doc.size(), QSize(101, 71));
    EXPECT_EQ(resized, QSize(101, 71));
    EXPECT_EQ(p.doc.projection().size(), QSize(101, 71));
    EXPECT_EQ(colourAt(10, 40), before) << "(60, 70) is now (10, 40)";
    ASSERT_TRUE(p.doc.selection());
    EXPECT_EQ(p.doc.selection()->bounds(true), QRect(0, 0, 101, 71)) << "the selection stays";
    ASSERT_TRUE(p.doc.undo());
    EXPECT_EQ(p.doc.size(), QSize(200, 150));
    EXPECT_EQ(resized, QSize(200, 150));
    EXPECT_EQ(colourAt(60, 70), before);
    must(p.doc.deselect());
    EXPECT_FALSE(p.doc.trimCanvas()) << "needs a selection";
}

TEST(LayerCommands, RotateCanvasTurnsEverythingAndSwapsTheSides) {
    Probe p;
    const QImage before = p.doc.projection().copy();
    must(p.doc.rotateCanvas(true));
    EXPECT_EQ(p.doc.size(), QSize(150, 200));
    EXPECT_EQ(p.doc.layer(p.l2)->pixels.contentBounds(true), QRect(65, 20, 20, 160));
    EXPECT_EQ(p.doc.projection().pixel(150 - 1 - 70, 100), before.pixel(100, 70));
    must(p.doc.rotateCanvas(false));
    EXPECT_EQ(p.doc.size(), QSize(200, 150));
    EXPECT_EQ(p.doc.projection(), before) << "a turn back restores the picture exactly";
}
