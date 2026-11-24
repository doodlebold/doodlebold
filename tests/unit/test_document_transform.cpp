#include "core/document/Document.h"

#include <gtest/gtest.h>

using namespace doodlebold;
using transform::Interpolation;
using transform::Quad;
using transform::Turn;

namespace {

constexpr int kOne = kChannelOne;
const QSize kCanvas(200, 150);

Document* redSquare(Document& doc) {
    TiledImage* px = doc.paintTarget(doc.currentLayerId()).value();
    px->fill(QRect(20, 20, 40, 40), Qt::red);
    return &doc;
}

int alphaAt(const Document& doc, int x, int y) {
    return doc.currentLayer()->pixels.pixel(x, y)[3];
}

}

TEST(DocumentTransform, TransformBoundsAreTheSelectionOrTheContent) {
    Document doc(kCanvas, Qt::white);
    EXPECT_TRUE(doc.transformBounds(doc.currentLayerId()).isEmpty());
    redSquare(doc);
    EXPECT_EQ(doc.transformBounds(doc.currentLayerId()), QRect(20, 20, 40, 40));
    ASSERT_TRUE(doc.setSelection(selection::marquee(kCanvas, QPoint(30, 30), QPoint(39, 39))));
    EXPECT_EQ(doc.transformBounds(doc.currentLayerId()), QRect(30, 30, 10, 10));
}

TEST(DocumentTransform, WholeLayerResizeIsOneUndoStep) {
    Document doc(kCanvas, Qt::white);
    redSquare(doc);
    const QRect box = doc.transformBounds(doc.currentLayerId());
    const int steps = doc.undoStack()->count();
    ASSERT_TRUE(doc.transformLayer(doc.currentLayerId(), Quad::of(QRectF(100, 50, 80, 80)), Interpolation::Bicubic));
    EXPECT_EQ(doc.undoStack()->count(), steps + 1);
    EXPECT_EQ(doc.undoStack()->undoText(), QStringLiteral("Transform Layer"));
    EXPECT_EQ(doc.currentLayer()->pixels.contentBounds(true), QRect(100, 50, 80, 80));
    EXPECT_EQ(alphaAt(doc, 140, 90), kOne);
    EXPECT_EQ(alphaAt(doc, 30, 30), 0);
    ASSERT_TRUE(doc.undo());
    EXPECT_EQ(doc.currentLayer()->pixels.contentBounds(true), box);
    ASSERT_TRUE(doc.redo());
    EXPECT_EQ(alphaAt(doc, 140, 90), kOne);
}

TEST(DocumentTransform, IdentityAndDegenerateTransformsAreRefused) {
    Document doc(kCanvas, Qt::white);
    redSquare(doc);
    const LayerId id = doc.currentLayerId();
    const QRect box = doc.transformBounds(id);
    EXPECT_EQ(doc.transformLayer(id, Quad::of(box), Interpolation::Bicubic).error(), EditError::NoChange);
    Quad flat = Quad::of(box);
    flat.corner[2] = flat.corner[1];
    flat.corner[3] = flat.corner[0];
    EXPECT_EQ(doc.transformLayer(id, flat, Interpolation::Bicubic).error(), EditError::InvalidTransform);
    EXPECT_EQ(doc.moveLayerExactly(id, Turn::None).error(), EditError::NoChange);
    EXPECT_FALSE(doc.currentLayer()->pixels.isEmpty());
}

TEST(DocumentTransform, EmptyFoldersAndLockedLayersAreRefused) {
    Document doc(kCanvas, Qt::white);
    const LayerId empty = doc.currentLayerId();
    EXPECT_EQ(doc.moveLayerExactly(empty, Turn::FlipHorizontal).error(), EditError::NoChange);
    redSquare(doc);
    LayerProps p = doc.currentLayer()->props;
    p.locked = true;
    ASSERT_TRUE(doc.setLayerProps(empty, p));
    EXPECT_EQ(doc.moveLayerExactly(empty, Turn::FlipHorizontal).error(), EditError::LayerLocked);
    EXPECT_EQ(doc.moveLayerExactly(doc.addFolder().value(), Turn::FlipHorizontal).error(), EditError::NotARasterLayer);
    EXPECT_EQ(doc.moveLayerExactly(LayerId(999), Turn::FlipHorizontal).error(), EditError::NoSuchLayer);
}

TEST(DocumentTransform, FlipOfTheWholeLayerIsExactAndUndoable) {
    Document doc(kCanvas, Qt::white);
    redSquare(doc);
    doc.paintTarget(doc.currentLayerId()).value()->fill(QRect(20, 20, 10, 10), Qt::blue);
    ASSERT_TRUE(doc.moveLayerExactly(doc.currentLayerId(), Turn::FlipHorizontal));
    EXPECT_EQ(doc.undoStack()->undoText(), QStringLiteral("Flip Horizontal"));
    EXPECT_EQ(doc.currentLayer()->pixels.pixel(55, 22), TiledImage::premultiplied(Qt::blue));
    EXPECT_EQ(doc.currentLayer()->pixels.pixel(22, 22), TiledImage::premultiplied(Qt::red));
    ASSERT_TRUE(doc.moveLayerExactly(doc.currentLayerId(), Turn::Clockwise));
    EXPECT_EQ(doc.undoStack()->undoText(), QStringLiteral("Rotate 90° CW"));
    ASSERT_TRUE(doc.undo());
    ASSERT_TRUE(doc.undo());
    EXPECT_EQ(doc.currentLayer()->pixels.pixel(22, 22), TiledImage::premultiplied(Qt::blue));
}

TEST(DocumentTransform, WithASelectionOnlyTheSelectedPartMovesAndTheSelectionFollows) {
    Document doc(kCanvas, Qt::white);
    redSquare(doc);
    ASSERT_TRUE(doc.setSelection(selection::marquee(kCanvas, QPoint(30, 30), QPoint(49, 49))));
    ASSERT_TRUE(doc.moveLayerExactly(doc.currentLayerId(), Turn::None, QPoint(100, 0)));
    EXPECT_EQ(alphaAt(doc, 135, 40), kOne);
    EXPECT_EQ(alphaAt(doc, 40, 40), 0);
    EXPECT_EQ(alphaAt(doc, 25, 25), kOne);
    EXPECT_EQ(alphaAt(doc, 55, 55), kOne);
    ASSERT_TRUE(doc.selection().has_value());
    EXPECT_EQ(doc.selection()->bounds(true), QRect(130, 30, 20, 20));
    ASSERT_TRUE(doc.undo());
    EXPECT_EQ(alphaAt(doc, 40, 40), kOne);
    EXPECT_EQ(alphaAt(doc, 135, 40), 0);
    EXPECT_EQ(doc.selection()->bounds(true), QRect(30, 30, 20, 20));
}

TEST(DocumentTransform, ResizingASelectionResizesItsPixelsAndItsMask) {
    Document doc(kCanvas, Qt::white);
    redSquare(doc);
    ASSERT_TRUE(doc.setSelection(selection::marquee(kCanvas, QPoint(30, 30), QPoint(49, 49))));
    ASSERT_TRUE(doc.transformLayer(doc.currentLayerId(), Quad::of(QRectF(100, 60, 40, 40)), Interpolation::Bicubic));
    EXPECT_EQ(doc.selection()->bounds(true), QRect(100, 60, 40, 40));
    EXPECT_EQ(doc.currentLayer()->pixels.contentBounds(true).united(QRect(20, 20, 40, 40)), QRect(20, 20, 120, 80));
    EXPECT_EQ(alphaAt(doc, 120, 80), kOne);
    EXPECT_EQ(alphaAt(doc, 40, 40), 0);
}

TEST(DocumentTransform, MovingAnOutOfCanvasSelectionDeselects) {
    Document doc(kCanvas, Qt::white);
    redSquare(doc);
    ASSERT_TRUE(doc.setSelection(selection::marquee(kCanvas, QPoint(30, 30), QPoint(49, 49))));
    ASSERT_TRUE(doc.moveLayerExactly(doc.currentLayerId(), Turn::None, QPoint(1000, 0)));
    EXPECT_FALSE(doc.selection().has_value());
    EXPECT_EQ(alphaAt(doc, 40, 40), 0);
}

TEST(DocumentClipboard, CopyGivesAStraightPictureWithItsPosition) {
    Document doc(kCanvas, Qt::white);
    redSquare(doc);
    doc.paintTarget(doc.currentLayerId()).value()->fill(QRect(60, 20, 10, 10), QColor(0, 0, 255, 128));
    const auto copy = doc.copy(doc.currentLayerId());
    ASSERT_TRUE(copy);
    EXPECT_EQ(copy.value().offset, QPoint(20, 20));
    EXPECT_EQ(copy.value().image.size(), QSize(50, 40));
    EXPECT_EQ(copy.value().image.format(), QImage::Format_RGBA8888);
    EXPECT_EQ(copy.value().image.pixelColor(0, 0), QColor(255, 0, 0, 255));
    EXPECT_EQ(copy.value().image.pixelColor(45, 5), QColor(0, 0, 255, 128));
    EXPECT_TRUE(doc.currentLayer()->pixels.pixel(25, 25)[3] == kOne);
    EXPECT_FALSE(doc.isModified() && doc.undoStack()->undoText() == QStringLiteral("Copy"));
}

TEST(DocumentClipboard, CopyAlsoGivesThePictureOverWhite) {
    Document doc(kCanvas, Qt::white);
    redSquare(doc);
    doc.paintTarget(doc.currentLayerId()).value()->fill(QRect(60, 20, 10, 10), QColor(0, 0, 255, 128));
    const auto copy = doc.copy(doc.currentLayerId());
    ASSERT_TRUE(copy);
    const QImage& flat = copy.value().onWhite;
    EXPECT_EQ(flat.size(), copy.value().image.size());
    EXPECT_FALSE(flat.hasAlphaChannel());
    EXPECT_EQ(flat.pixelColor(0, 0), QColor(255, 0, 0));
    EXPECT_EQ(flat.pixelColor(45, 30), QColor(255, 255, 255));
    const QColor half = flat.pixelColor(45, 5);
    EXPECT_NEAR(half.red(), 127, 1);
    EXPECT_NEAR(half.green(), 127, 1);
    EXPECT_EQ(half.blue(), 255);
}

TEST(DocumentClipboard, CopyTakesOnlyTheSelectedPartWeightedByCoverage) {
    Document doc(kCanvas, Qt::white);
    redSquare(doc);
    ASSERT_TRUE(doc.setSelection(selection::marquee(kCanvas, QPoint(30, 30), QPoint(49, 39))));
    const auto copy = doc.copy(doc.currentLayerId());
    ASSERT_TRUE(copy);
    EXPECT_EQ(copy.value().offset, QPoint(30, 30));
    EXPECT_EQ(copy.value().image.size(), QSize(20, 10));
    ASSERT_TRUE(doc.setSelection(selection::marquee(kCanvas, QPoint(100, 100), QPoint(110, 110))));
    EXPECT_EQ(doc.copy(doc.currentLayerId()).error(), EditError::NoChange);
}

TEST(DocumentClipboard, CutThenPasteRoundTripsOpaquePixelsAtTheSamePlace) {
    Document doc(kCanvas, Qt::white);
    redSquare(doc);
    const TiledImage before = doc.currentLayer()->pixels;
    ASSERT_TRUE(doc.setSelection(selection::marquee(kCanvas, QPoint(30, 30), QPoint(49, 49))));
    const auto cut = doc.cut(doc.currentLayerId());
    ASSERT_TRUE(cut);
    EXPECT_EQ(doc.undoStack()->undoText(), QStringLiteral("Cut"));
    EXPECT_EQ(alphaAt(doc, 40, 40), 0);
    EXPECT_EQ(alphaAt(doc, 25, 25), kOne);
    EXPECT_TRUE(doc.selection().has_value());

    const LayerId source = doc.currentLayerId();
    const auto pasted = doc.paste(cut.value());
    ASSERT_TRUE(pasted);
    EXPECT_NE(pasted.value(), source);
    EXPECT_EQ(doc.currentLayerId(), pasted.value());
    EXPECT_FALSE(doc.selection().has_value());
    EXPECT_EQ(doc.layers().layerCount(), 2);
    EXPECT_EQ(doc.layers().indexOf(pasted.value()), doc.layers().indexOf(source) + 1);
    for (int y = 28; y < 52; ++y)
        for (int x = 28; x < 52; ++x)
            ASSERT_EQ(doc.currentLayer()->pixels.pixel(x, y)[3], (x >= 30 && x < 50 && y >= 30 && y < 50) ? kOne : 0);
    EXPECT_EQ(doc.currentLayer()->pixels.pixel(40, 40), before.pixel(40, 40));
    ASSERT_TRUE(doc.mergeDown(pasted.value()));
    EXPECT_EQ(doc.currentLayer()->pixels.pixel(40, 40), before.pixel(40, 40));
    EXPECT_EQ(doc.currentLayer()->pixels.pixel(25, 25), before.pixel(25, 25));

    ASSERT_TRUE(doc.undo());
    ASSERT_TRUE(doc.undo());
    EXPECT_EQ(doc.layers().layerCount(), 1);
    EXPECT_TRUE(doc.selection().has_value());
    ASSERT_TRUE(doc.undo());
    EXPECT_EQ(alphaAt(doc, 40, 40), kOne);
}

TEST(DocumentClipboard, PasteWithoutDeselectingKeepsTheSelection) {
    Document doc(kCanvas, Qt::white);
    redSquare(doc);
    ASSERT_TRUE(doc.setSelection(selection::marquee(kCanvas, QPoint(30, 30), QPoint(49, 49))));
    const auto copy = doc.copy(doc.currentLayerId());
    ASSERT_TRUE(copy);
    ASSERT_TRUE(doc.paste(copy.value(), true));
    EXPECT_TRUE(doc.selection().has_value());
}

TEST(DocumentClipboard, ExternalPicturesAreCentredAndClippedToTheCanvas) {
    Document doc(kCanvas, Qt::white);
    QImage picture(60, 40, QImage::Format_ARGB32);
    picture.fill(QColor(10, 200, 30, 255));
    const ClipboardImage clip = ClipboardImage::centred(picture, kCanvas);
    EXPECT_EQ(clip.offset, QPoint(70, 55));
    ASSERT_TRUE(doc.paste(clip));
    EXPECT_EQ(doc.currentLayer()->pixels.contentBounds(true), QRect(70, 55, 60, 40));
    EXPECT_EQ(doc.currentLayer()->pixels.pixel(100, 70), TiledImage::premultiplied(QColor(10, 200, 30)));

    QImage huge(400, 300, QImage::Format_ARGB32);
    huge.fill(Qt::black);
    ASSERT_TRUE(doc.paste(ClipboardImage::centred(huge, kCanvas)));
    EXPECT_EQ(doc.currentLayer()->pixels.contentBounds(true), QRect(0, 0, 200, 150));
    EXPECT_EQ(doc.paste(ClipboardImage{}).error(), EditError::NoChange);
}

TEST(DocumentClipboard, SemiTransparentPixelsSurviveTheEightBitBoundaryWithinOneLevel) {
    Document doc(kCanvas, Qt::white);
    doc.paintTarget(doc.currentLayerId()).value()->fill(QRect(10, 10, 20, 20), QColor(120, 200, 40, 90));
    const TiledImage before = doc.currentLayer()->pixels;
    const auto cut = doc.cut(doc.currentLayerId());
    ASSERT_TRUE(cut);
    ASSERT_TRUE(doc.paste(cut.value()));
    const auto a = doc.currentLayer()->pixels.pixel(15, 15), b = before.pixel(15, 15);
    for (std::size_t c = 0; c < 4; ++c)
        EXPECT_NEAR(a[c], b[c], kOne / 255) << c;
}

TEST(DocumentClipboard, CutFromALockedLayerIsRefusedAndKeepsThePixels) {
    Document doc(kCanvas, Qt::white);
    redSquare(doc);
    LayerProps p = doc.currentLayer()->props;
    p.locked = true;
    ASSERT_TRUE(doc.setLayerProps(doc.currentLayerId(), p));
    EXPECT_EQ(doc.cut(doc.currentLayerId()).error(), EditError::LayerLocked);
    EXPECT_EQ(alphaAt(doc, 40, 40), kOne);
    EXPECT_TRUE(doc.copy(doc.currentLayerId()));
}

namespace {
Document maskedSquare() {
    Document base(kCanvas, Qt::white);
    redSquare(base);
    DocumentContent c = base.content();
    LayerNode* n = c.state.layers.find(c.state.current);
    SelectionMask m(kCanvas);
    const std::vector<std::uint16_t> row(40, SelectionMask::kOne);
    for (int y = 0; y < kCanvas.height(); ++y)
        m.writeRow(y, 0, 40, row.data());
    m.squeeze();
    n->mask = m;
    return Document(std::move(c));
}
}

TEST(DocumentTransform, MovingALayerTakesItsMaskAlong) {
    Document doc(maskedSquare().content());
    const LayerId id = doc.currentLayerId();
    ASSERT_TRUE(doc.moveLayerExactly(id, Turn::None, QPoint(50, 0)));
    const LayerNode* n = doc.currentLayer();
    ASSERT_TRUE(n->mask);
    EXPECT_EQ(n->mask->at(75, 30), SelectionMask::kOne);
    EXPECT_EQ(n->mask->at(95, 30), 0);
    EXPECT_EQ(n->pixels.pixel(75, 30)[3], kOne);
    ASSERT_TRUE(doc.undo());
    EXPECT_EQ(doc.currentLayer()->mask->at(25, 30), SelectionMask::kOne);
    EXPECT_EQ(doc.currentLayer()->mask->at(75, 30), 0);
}

TEST(DocumentTransform, MergeDownBakesTheLowerLayersMask) {
    Document doc(maskedSquare().content());
    const LayerId lower = doc.currentLayerId();
    const LayerId upper = doc.addLayer().value();
    ASSERT_TRUE(doc.mergeDown(upper));
    const LayerNode* n = doc.layers().find(lower);
    ASSERT_TRUE(n);
    EXPECT_FALSE(n->mask);
    EXPECT_EQ(n->pixels.pixel(30, 30)[3], kOne);
    EXPECT_EQ(n->pixels.pixel(50, 30)[3], 0);
}
