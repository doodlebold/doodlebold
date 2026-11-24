#include "core/document/TransformSession.h"

#include <numbers>

#include <gtest/gtest.h>

#include <cmath>

using namespace doodlebold;
using H = TransformSession::Handle;
using transform::Quad;

namespace {

const QSize kCanvas(300, 200);

MarqueeOptions options() {
    return MarqueeOptions{};
}

Document* redSquare(Document& doc) {
    doc.paintTarget(doc.currentLayerId()).value()->fill(QRect(100, 60, 40, 40), Qt::red);
    doc.notifyPixelsChanged(doc.currentLayerId(), QRect(100, 60, 40, 40));
    return &doc;
}

int alphaAt(const Document& doc, int x, int y) {
    return doc.currentLayer()->pixels.pixel(x, y)[3];
}

std::shared_ptr<TransformSession> begin(Document& doc, TransformMode mode) {
    auto s = TransformSession::beginTransform(doc, doc.currentLayerId(), mode, options());
    EXPECT_TRUE(s.ok());
    return s.value();
}

void drag(TransformSession& s, H handle, QPointF from, QPointF to, Qt::KeyboardModifiers mods = {}) {
    s.beginDrag(handle, from);
    s.dragTo(to, mods);
    s.endDrag();
}

}

TEST(TransformSession, RefusesWhatCannotBeTransformed) {
    Document doc(kCanvas, Qt::white);
    EXPECT_EQ(TransformSession::beginTransform(doc, doc.currentLayerId(), TransformMode::Resize, options()).error(), EditError::NoChange);
    redSquare(doc);
    const LayerId layer = doc.currentLayerId();
    ASSERT_TRUE(doc.addFolder());
    EXPECT_EQ(TransformSession::beginTransform(doc, doc.currentLayerId(), TransformMode::Resize, options()).error(),
              EditError::NotARasterLayer);
    doc.setCurrentLayer(layer);
    LayerProps props = doc.currentLayer()->props;
    props.locked = true;
    ASSERT_TRUE(doc.setLayerProps(layer, props));
    EXPECT_EQ(TransformSession::beginTransform(doc, layer, TransformMode::Resize, options()).error(), EditError::LayerLocked);
}

TEST(TransformSession, StartsAtTheBoxAndTouchesNothingUntilCommit) {
    Document doc(kCanvas, Qt::white);
    redSquare(doc);
    const int steps = doc.undoStack()->count();
    auto s = begin(doc, TransformMode::Resize);
    EXPECT_EQ(s->sourceBounds(), QRect(100, 60, 40, 40));
    EXPECT_TRUE(s->isIdentity());
    EXPECT_EQ(s->commit().error(), EditError::NoChange);
    drag(*s, H::Corner2, {140, 100}, {180, 140});
    EXPECT_FALSE(s->isIdentity());
    EXPECT_EQ(doc.undoStack()->count(), steps);
    EXPECT_EQ(alphaAt(doc, 170, 130), 0);
}

TEST(TransformSession, PreviewShowsTheTransformedLayerWithoutChangingTheDocument) {
    Document doc(kCanvas, Qt::white);
    redSquare(doc);
    auto s = begin(doc, TransformMode::Resize);
    s->takeDirty();
    drag(*s, H::Corner2, {140, 100}, {180, 140});
    EXPECT_FALSE(s->takeDirty().isEmpty());
    EXPECT_EQ(s->image().pixelColor(170, 130), QColor(Qt::red));
    EXPECT_EQ(s->image().pixelColor(150, 70), QColor(Qt::red));
    EXPECT_EQ(doc.projection().pixelColor(170, 130), QColor(Qt::white));
    (*s).reset();
    EXPECT_TRUE(s->isIdentity());
    EXPECT_EQ(s->image().pixelColor(170, 130), QColor(Qt::white));
}

TEST(TransformSession, CommitIsOneUndoStepAndMatchesTheDocumentTransform) {
    Document doc(kCanvas, Qt::white);
    redSquare(doc);
    auto s = begin(doc, TransformMode::Resize);
    drag(*s, H::Corner2, {140, 100}, {180, 140});
    const int steps = doc.undoStack()->count();
    ASSERT_TRUE(s->commit());
    EXPECT_EQ(doc.undoStack()->count(), steps + 1);
    EXPECT_EQ(doc.currentLayer()->pixels.contentBounds(true), QRect(100, 60, 80, 80));
    EXPECT_EQ(alphaAt(doc, 170, 130), kChannelOne);
    ASSERT_TRUE(doc.undo());
    EXPECT_EQ(doc.currentLayer()->pixels.contentBounds(true), QRect(100, 60, 40, 40));
}

TEST(TransformSession, ResizeKeepsTheOppositeCornerAndShiftKeepsTheRatio) {
    Document doc(kCanvas, Qt::white);
    redSquare(doc);
    auto s = begin(doc, TransformMode::Resize);
    drag(*s, H::Corner2, {140, 100}, {200, 120});
    EXPECT_EQ(s->quad().corner[0], QPointF(100, 60));
    EXPECT_NEAR(s->quad().corner[2].x(), 200, 1e-9);
    EXPECT_NEAR(s->quad().corner[2].y(), 120, 1e-9);
    (*s).reset();
    drag(*s, H::Corner2, {140, 100}, {200, 120}, Qt::ShiftModifier);
    const QPointF size = s->quad().corner[2] - s->quad().corner[0];
    EXPECT_NEAR(size.x(), size.y(), 1e-9);
    EXPECT_NEAR(size.x(), 100, 1e-9);
    (*s).reset();
    drag(*s, H::Edge1, {140, 80}, {190, 90});
    EXPECT_NEAR(s->quad().corner[1].x(), 190, 1e-9);
    EXPECT_NEAR(s->quad().corner[2].y(), 100, 1e-9);
    (*s).reset();
    drag(*s, H::Corner0, {100, 60}, {160, 120});
    EXPECT_NEAR(s->quad().corner[0].x(), 160, 1e-9);
    EXPECT_NEAR(s->quad().corner[0].y(), 120, 1e-9);
    EXPECT_NEAR(s->quad().corner[2].x(), 140, 1e-9);
}

TEST(TransformSession, FreeformMovesOneCornerAndStretchesEdges) {
    Document doc(kCanvas, Qt::white);
    redSquare(doc);
    auto s = begin(doc, TransformMode::Freeform);
    drag(*s, H::Corner1, {140, 60}, {170, 50});
    EXPECT_EQ(s->quad().corner[1], QPointF(170, 50));
    EXPECT_EQ(s->quad().corner[0], QPointF(100, 60));
    EXPECT_EQ(s->quad().corner[2], QPointF(140, 100));
    (*s).reset();
    drag(*s, H::Edge2, {120, 100}, {150, 130});
    EXPECT_NEAR(s->quad().corner[2].x(), 140, 1e-9);
    EXPECT_NEAR(s->quad().corner[2].y(), 130, 1e-9);
    EXPECT_NEAR(s->quad().corner[3].y(), 130, 1e-9);
}

TEST(TransformSession, DeformEdgesMoveTheirTwoCornersTogether) {
    Document doc(kCanvas, Qt::white);
    redSquare(doc);
    auto s = begin(doc, TransformMode::Deform);
    drag(*s, H::Edge0, {120, 60}, {140, 50});
    EXPECT_EQ(s->quad().corner[0], QPointF(120, 50));
    EXPECT_EQ(s->quad().corner[1], QPointF(160, 50));
    EXPECT_EQ(s->quad().corner[2], QPointF(140, 100));
}

TEST(TransformSession, RotateTurnsAboutTheCentreAndShiftSnaps) {
    Document doc(kCanvas, Qt::white);
    redSquare(doc);
    auto s = begin(doc, TransformMode::Rotate);
    const QPointF c(120, 80);
    drag(*s, H::Outside, {200, 80}, {120, 160});
    const QPointF p0 = s->quad().corner[0];
    EXPECT_NEAR(p0.x(), c.x() + 20, 1e-6);
    EXPECT_NEAR(p0.y(), c.y() - 20, 1e-6);
    (*s).reset();
    drag(*s, H::Outside, {200, 80}, {200, 80 + 40}, Qt::ShiftModifier);
    const QPointF d = s->quad().corner[1] - s->quad().corner[0];
    EXPECT_NEAR(std::atan2(d.y(), d.x()) * 180.0 / std::numbers::pi, 30.0, 1e-6);
}

TEST(TransformSession, DraggingInsideMovesInEveryMode) {
    Document doc(kCanvas, Qt::white);
    redSquare(doc);
    for (TransformMode mode : {TransformMode::Freeform, TransformMode::Resize, TransformMode::Deform, TransformMode::Rotate}) {
        auto s = begin(doc, mode);
        drag(*s, H::Inside, {120, 80}, {130, 95});
        EXPECT_EQ(s->quad().corner[0], QPointF(110, 75)) << transformModeName(mode).toStdString();
        EXPECT_EQ(s->quad().corner[2], QPointF(150, 115));
    }
}

TEST(TransformSession, HitTestFindsCornersEdgesInsideAndOutside) {
    Document doc(kCanvas, Qt::white);
    redSquare(doc);
    auto s = begin(doc, TransformMode::Resize);
    EXPECT_EQ(s->hitTest({101, 61}, 1.0), H::Corner0);
    EXPECT_EQ(s->hitTest({139, 99}, 1.0), H::Corner2);
    EXPECT_EQ(s->hitTest({120, 61}, 1.0), H::Edge0);
    EXPECT_EQ(s->hitTest({139, 80}, 1.0), H::Edge1);
    EXPECT_EQ(s->hitTest({120, 80}, 1.0), H::Inside);
    EXPECT_EQ(s->hitTest({10, 10}, 1.0), H::Outside);
    EXPECT_EQ(s->hitTest({105, 64}, 0.1), H::Inside);
}

TEST(TransformSession, SelectionTravelsWithTheLayerAndOnlyTheSelectedPartMoves) {
    Document doc(kCanvas, Qt::white);
    redSquare(doc);
    ASSERT_TRUE(doc.setSelection(selection::marquee(kCanvas, QPoint(100, 60), QPoint(119, 79))));
    auto s = begin(doc, TransformMode::Resize);
    EXPECT_EQ(s->sourceBounds(), QRect(100, 60, 20, 20));
    drag(*s, H::Inside, {110, 70}, {160, 70});
    ASSERT_TRUE(s->selection());
    EXPECT_EQ(s->selection()->bounds(true), QRect(150, 60, 20, 20));
    EXPECT_EQ(s->image().pixelColor(160, 70), QColor(Qt::red));
    EXPECT_EQ(s->image().pixelColor(110, 70), QColor(Qt::white));
    EXPECT_EQ(s->image().pixelColor(130, 90), QColor(Qt::red));
    ASSERT_TRUE(s->commit());
    EXPECT_EQ(doc.selection()->bounds(true), QRect(150, 60, 20, 20));
    EXPECT_EQ(alphaAt(doc, 160, 70), kChannelOne);
    EXPECT_EQ(alphaAt(doc, 110, 70), 0);
}

TEST(TransformSession, MoveSessionsMoveByWholePixels) {
    Document doc(kCanvas, Qt::white);
    redSquare(doc);
    auto made = TransformSession::beginMove(doc, doc.currentLayerId(), options());
    ASSERT_TRUE(made);
    const auto& s = made.value();
    EXPECT_EQ(s->kind(), TransformSession::Kind::Move);
    EXPECT_EQ(s->hitTest({5, 5}, 1.0), H::Inside);
    drag(*s, H::Inside, {110.2, 70.2}, {125.6, 64.9});
    EXPECT_EQ(s->offset(), QPoint(15, -5));
    EXPECT_EQ(s->image().pixelColor(120, 60), QColor(Qt::red));
    const int steps = doc.undoStack()->count();
    ASSERT_TRUE(s->commit());
    EXPECT_EQ(doc.undoStack()->count(), steps + 1);
    EXPECT_EQ(doc.currentLayer()->pixels.contentBounds(true), QRect(115, 55, 40, 40));
}

TEST(TransformSession, PerspectiveIsPassedToTheDocument) {
    Document doc(kCanvas, Qt::white);
    redSquare(doc);
    auto s = begin(doc, TransformMode::Freeform);
    drag(*s, H::Corner1, {140, 60}, {180, 70});
    s->setPerspective(1.0);
    EXPECT_EQ(s->perspective(), 1.0);
    ASSERT_TRUE(s->commit());
    EXPECT_FALSE(doc.currentLayer()->pixels.isEmpty());
    s->setPerspective(7.0);
    EXPECT_EQ(s->perspective(), 1.0);
}

// The preview recomposites many tiles on several threads; its picture must not share pixels with the document's (a
// shared QImage would be detached by every thread at once).
TEST(TransformSession, PreviewOfALargeCanvasRecompositesSafely) {
    Document doc(QSize(1500, 1200), Qt::white);
    doc.paintTarget(doc.currentLayerId()).value()->fill(QRect(100, 100, 1000, 800), Qt::red);
    doc.notifyPixelsChanged(doc.currentLayerId(), QRect(100, 100, 1000, 800));
    (void)doc.projection();
    auto s = TransformSession::beginTransform(doc, doc.currentLayerId(), TransformMode::Resize, options()).value();
    s->beginDrag(H::Corner2, {1100, 900});
    for (int i = 1; i <= 3; ++i) {
        s->dragTo(QPointF(1100 - i * 50, 900 - i * 40), {});
        const QImage& img = s->image();
        EXPECT_EQ(img.pixelColor(500, 400), QColor(Qt::red));
        EXPECT_EQ(img.pixelColor(1400, 1100), QColor(Qt::white));
    }
    EXPECT_EQ(doc.projection().pixelColor(1050, 850), QColor(Qt::red));
}
