#include "core/document/Document.h"
#include "core/document/LayerOps.h"
#include "core/io/DoodleFormat.h"
#include "core/paint/StrokeController.h"
#include "core/selection/SelectionOps.h"

#include <QBuffer>

#include <gtest/gtest.h>

using namespace doodlebold;

namespace {

// A mouse, so the two-event strokes here are at full pressure from end to end.
InputSample at(double x, double y) {
    InputSample s;
    s.pos = QPointF(x, y);
    s.kind = PointerKind::Mouse;
    return s;
}

constexpr int kHalfLevel = SelectionMask::kOne / 255 / 2;

void stroke(Document& doc, ToolType tool, const QColor& colour, double y = 40) {
    StrokeController sc(&doc);
    BrushSettings st = defaultSettings(tool);
    st.size = 200;
    ASSERT_TRUE(sc.begin(at(10, y), colour, st));
    sc.add(at(110, y));
    sc.finish();
}

std::unique_ptr<Document> maskedDocument() {
    auto doc = std::make_unique<Document>(QSize(120, 80), Qt::white);
    const LayerId id = doc->currentLayerId();
    EXPECT_TRUE(doc->fillLayer(id, Qt::black));
    EXPECT_TRUE(doc->addLayerMask(id));
    return doc;
}

}

TEST(LayerMasks, MaskValueIsTheColoursLuma) {
    EXPECT_EQ(layer_ops::maskValueOf(QColor(0, 0, 0)), 0);
    EXPECT_EQ(layer_ops::maskValueOf(QColor(255, 0, 0)), 76);
    EXPECT_EQ(layer_ops::maskValueOf(QColor(0, 255, 0)), 150);
    EXPECT_EQ(layer_ops::maskValueOf(QColor(0, 0, 255)), 29);
    EXPECT_EQ(layer_ops::maskValueOf(QColor(128, 128, 128)), 128);
    EXPECT_EQ(layer_ops::maskValueOf(QColor(255, 255, 0)), 226);
    EXPECT_EQ(layer_ops::maskValueOf(QColor(40, 120, 200)), 105);
}

TEST(LayerMasks, NewMaskShowsEverythingAndBecomesTheTarget) {
    auto doc = maskedDocument();
    const LayerNode* layer = doc->currentLayer();
    ASSERT_TRUE(layer->mask);
    EXPECT_EQ(layer->mask->at(60, 40), SelectionMask::kOne);
    EXPECT_TRUE(layer->props.maskEnabled);
    EXPECT_TRUE(layer->props.maskLinked);
    EXPECT_TRUE(doc->maskTarget());
    EXPECT_EQ(doc->projection().pixel(60, 40), 0xff000000u) << "a white mask changes nothing";
    const auto again = doc->addLayerMask(doc->currentLayerId());
    ASSERT_FALSE(again);
    EXPECT_EQ(again.error(), EditError::HasLayerMask);
    ASSERT_TRUE(doc->undo());
    EXPECT_FALSE(doc->currentLayer()->mask);
    EXPECT_FALSE(doc->maskTarget()) << "a layer without a mask is its own target";
}

TEST(LayerMasks, PaintingTheMaskHidesAndUndoRestores) {
    auto doc = maskedDocument();
    const TiledImage pixels = doc->currentLayer()->pixels;
    stroke(*doc, ToolType::Pencil, Qt::black);
    const LayerNode* layer = doc->currentLayer();
    EXPECT_LE(layer->mask->at(60, 40), kHalfLevel) << "black paint hides";
    EXPECT_EQ(layer->mask->at(60, 75), SelectionMask::kOne) << "away from the stroke";
    EXPECT_EQ(layer->pixels.tiles(), pixels.tiles()) << "the layer's pixels are not touched";
    EXPECT_EQ(doc->projection().pixel(60, 40), 0xffffffffu) << "the paper shows through";
    ASSERT_TRUE(doc->undo());
    EXPECT_EQ(doc->currentLayer()->mask->at(60, 40), SelectionMask::kOne);
    EXPECT_TRUE(doc->maskTarget()) << "undo keeps the mask as the target";
    ASSERT_TRUE(doc->redo());
    EXPECT_LE(doc->currentLayer()->mask->at(60, 40), kHalfLevel);
}

TEST(LayerMasks, ColourPaintsItsLumaAndTheEraserHides) {
    auto doc = maskedDocument();
    stroke(*doc, ToolType::Pencil, QColor(255, 0, 0), 20);
    stroke(*doc, ToolType::Eraser, Qt::black, 60);
    const SelectionMask& mask = *doc->currentLayer()->mask;
    EXPECT_NEAR(mask.at(60, 20), fromUnorm8(76), kHalfLevel) << "red paints 76 of 255";
    EXPECT_LE(mask.at(60, 60), kHalfLevel) << "the Eraser hides, as black paint does";
}

TEST(LayerMasks, SwitchedOffHiddenOrLockedMasksAreNotPainted) {
    auto doc = maskedDocument();
    LayerProps p = doc->currentLayer()->props;
    p.maskEnabled = false;
    ASSERT_TRUE(doc->setLayerProps(p.id, p));
    EXPECT_EQ(doc->projection().pixel(60, 40), 0xff000000u);
    StrokeController sc(doc.get());
    const auto refused = sc.begin(at(10, 40), Qt::black, defaultSettings(ToolType::Pencil));
    ASSERT_FALSE(refused);
    EXPECT_EQ(refused.error(), EditError::MaskDisabled);
}

TEST(LayerMasks, TargetFollowsTheLastChoiceButNotIntoFolders) {
    auto doc = maskedDocument();
    const LayerId first = doc->currentLayerId();
    ASSERT_TRUE(doc->addLayer());
    EXPECT_FALSE(doc->maskTarget()) << "the new layer has no mask";
    const LayerId second = doc->currentLayerId();
    ASSERT_TRUE(doc->addLayerMask(second));
    doc->setCurrentLayer(first);
    EXPECT_TRUE(doc->maskTarget()) << "the mask stays chosen on another layer with a mask";
    doc->setMaskTarget(false);
    doc->setCurrentLayer(second);
    EXPECT_FALSE(doc->maskTarget());
    ASSERT_TRUE(doc->addFolder());
    const LayerId folder = doc->currentLayerId();
    ASSERT_TRUE(doc->addLayerMask(folder)) << "folders take masks";
    doc->setCurrentLayer(second);
    doc->setMaskTarget(true);
    doc->setCurrentLayer(folder);
    EXPECT_FALSE(doc->maskTarget()) << "a folder's mask is not chosen on the way";
}

TEST(LayerMasks, DeleteApplyEraseAndFillActOnTheMask) {
    auto doc = maskedDocument();
    const LayerId id = doc->currentLayerId();
    ASSERT_TRUE(doc->fillLayer(id, QColor(128, 128, 128)));
    EXPECT_EQ(doc->currentLayer()->mask->at(5, 5), fromUnorm8(128)) << "Fill sets the colour's luma";
    EXPECT_EQ(doc->currentLayer()->pixels.pixel(5, 5)[3], kChannelOne) << "the layer is not filled";

    ASSERT_TRUE(doc->applyLayerMask(id));
    EXPECT_FALSE(doc->currentLayer()->mask);
    EXPECT_FALSE(doc->maskTarget());
    EXPECT_EQ(doc->currentLayer()->pixels.pixel(5, 5)[3], fromUnorm8(128)) << "the mask is multiplied into the pixels";
    ASSERT_TRUE(doc->undo());

    ASSERT_TRUE(doc->clearLayer(id));
    EXPECT_EQ(doc->currentLayer()->mask->at(5, 5), 0) << "Erase hides everything";
    EXPECT_FALSE(doc->currentLayer()->pixels.isEmpty());

    ASSERT_TRUE(doc->deleteLayerMask(id));
    EXPECT_FALSE(doc->currentLayer()->mask);
    EXPECT_FALSE(doc->maskTarget());
    EXPECT_EQ(doc->projection().pixel(5, 5), 0xff000000u);
}

TEST(LayerMasks, EraseOnTheMaskFollowsTheSelection) {
    auto doc = maskedDocument();
    ASSERT_TRUE(doc->setSelection(selection::rectangle(doc->size(), QRectF(0, 0, 60, 80), false)));
    ASSERT_TRUE(doc->clearLayer(doc->currentLayerId()));
    EXPECT_EQ(doc->currentLayer()->mask->at(10, 10), 0);
    EXPECT_EQ(doc->currentLayer()->mask->at(100, 10), SelectionMask::kOne);
}

TEST(LayerMasks, ApplyKeepsWhatASwitchedOffMaskShowed) {
    auto doc = maskedDocument();
    const LayerId id = doc->currentLayerId();
    ASSERT_TRUE(doc->clearLayer(id));
    LayerProps p = doc->currentLayer()->props;
    p.maskEnabled = false;
    ASSERT_TRUE(doc->setLayerProps(id, p));
    ASSERT_TRUE(doc->applyLayerMask(id));
    EXPECT_EQ(doc->currentLayer()->pixels.pixel(5, 5)[3], kChannelOne);
    ASSERT_TRUE(doc->addFolder());
    ASSERT_TRUE(doc->addLayerMask(doc->currentLayerId()));
    const auto folder = doc->applyLayerMask(doc->currentLayerId());
    ASSERT_FALSE(folder);
    EXPECT_EQ(folder.error(), EditError::NotARasterLayer);
}

TEST(LayerMasks, OnlyALinkedMaskMovesWithTheLayer) {
    auto doc = maskedDocument();
    const LayerId id = doc->currentLayerId();
    ASSERT_TRUE(doc->setSelection(selection::rectangle(doc->size(), QRectF(0, 0, 20, 80), false)));
    ASSERT_TRUE(doc->clearLayer(id));
    ASSERT_TRUE(doc->deselect());
    ASSERT_TRUE(doc->moveLayerExactly(id, transform::Turn::None, QPoint(30, 0)));
    EXPECT_EQ(doc->currentLayer()->mask->at(40, 10), 0) << "linked: the hidden band moved too";
    ASSERT_TRUE(doc->undo());
    LayerProps p = doc->currentLayer()->props;
    p.maskLinked = false;
    ASSERT_TRUE(doc->setLayerProps(id, p));
    ASSERT_TRUE(doc->moveLayerExactly(id, transform::Turn::None, QPoint(30, 0)));
    EXPECT_EQ(doc->currentLayer()->mask->at(10, 10), 0) << "unlinked: the mask stayed";
    EXPECT_EQ(doc->currentLayer()->mask->at(40, 10), SelectionMask::kOne);
}

// A linked mask goes with the painting, and away from it stays as it was: moved over the painting's box alone, it came out
// hidden everywhere else, so anything painted there later didn't show.
TEST(LayerMasks, MovingALayerLeavesItsMaskAwayFromThePaintingAsItWas) {
    Document doc(QSize(120, 80), Qt::white);
    const LayerId id = doc.currentLayerId();
    doc.paintTarget(id).value()->fill(QRect(10, 10, 20, 20), Qt::black);
    ASSERT_TRUE(doc.addLayerMask(id));
    ASSERT_TRUE(doc.setSelection(selection::rectangle(doc.size(), QRectF(80, 0, 20, 80), false)));
    ASSERT_TRUE(doc.clearLayer(id));  // a hidden band on the mask, away from the painting
    ASSERT_TRUE(doc.deselect());
    const auto expectTheMaskAsItWas = [&](const char* how) {
        const SelectionMask& mask = *doc.currentLayer()->mask;
        EXPECT_EQ(mask.at(5, 5), SelectionMask::kOne) << how << ": where the painting was";
        EXPECT_EQ(mask.at(50, 60), SelectionMask::kOne) << how << ": away from it";
        EXPECT_EQ(mask.at(90, 40), 0) << how << ": the hidden band";
        EXPECT_EQ(mask.at(110, 40), SelectionMask::kOne) << how;
    };
    ASSERT_TRUE(doc.moveLayerExactly(id, transform::Turn::None, QPoint(30, 20)));
    expectTheMaskAsItWas("moved");
    EXPECT_EQ(doc.currentLayer()->mask->at(45, 35), SelectionMask::kOne) << "where the painting went";
    ASSERT_TRUE(doc.undo());
    ASSERT_TRUE(doc.moveLayerExactly(id, transform::Turn::Clockwise));
    expectTheMaskAsItWas("turned");
    ASSERT_TRUE(doc.undo());
    ASSERT_TRUE(doc.transformLayer(id, transform::Quad::of(QRectF(40, 30, 30, 30)), transform::Interpolation::Bilinear));
    expectTheMaskAsItWas("transformed");
    ASSERT_TRUE(doc.undo());
    ASSERT_TRUE(doc.moveLayersExactly({id}, QPoint(30, 20)));
    expectTheMaskAsItWas("moved with others");
}

TEST(LayerMasks, FolderMasksAndTheLinkSurviveDoodleboldFiles) {
    auto doc = maskedDocument();
    LayerProps p = doc->currentLayer()->props;
    p.maskLinked = false;
    ASSERT_TRUE(doc->setLayerProps(p.id, p));
    ASSERT_TRUE(doc->addFolder());
    const LayerId folder = doc->currentLayerId();
    ASSERT_TRUE(doc->addLayerMask(folder));
    ASSERT_TRUE(doc->setSelection(selection::rectangle(doc->size(), QRectF(10, 10, 30, 30), false)));
    ASSERT_TRUE(doc->clearLayer(folder));
    ASSERT_TRUE(doc->deselect());

    QByteArray bytes;
    QBuffer out(&bytes);
    out.open(QIODevice::WriteOnly);
    ASSERT_TRUE(DoodleFormat().write(doc->content(), out, {}));
    QBuffer in(&bytes);
    in.open(QIODevice::ReadOnly);
    const auto loaded = DoodleFormat().read(in);
    ASSERT_TRUE(loaded) << qPrintable(loaded.error().describe());
    const LayerStack& layers = loaded.value().content.state.layers;
    const LayerNode* f = layers.find(folder);
    ASSERT_TRUE(f && f->mask);
    EXPECT_EQ(f->mask->at(20, 20), 0);
    EXPECT_EQ(f->mask->at(60, 60), SelectionMask::kOne);
    const LayerNode* layer = layers.find(p.id);
    ASSERT_TRUE(layer && layer->mask);
    EXPECT_FALSE(layer->props.maskLinked);
}
