#include "core/document/TransformSession.h"

#include <gtest/gtest.h>

#include <cstdint>

using namespace doodlebold;
using H = TransformSession::Handle;

namespace {

const QSize kCanvas(300, 200);

void redSquare(Document& doc) {
    doc.paintTarget(doc.currentLayerId()).value()->fill(QRect(100, 60, 40, 40), Qt::red);
    doc.notifyPixelsChanged(doc.currentLayerId(), QRect(100, 60, 40, 40));
}

std::shared_ptr<TransformSession> begin(Document& doc, TransformMode mode, const MarqueeOptions& o = MarqueeOptions{}) {
    auto s = TransformSession::beginTransform(doc, doc.currentLayerId(), mode, o);
    EXPECT_TRUE(s.ok());
    return s.value();
}

bool samePixels(const TiledImage& a, const TiledImage& b) {
    for (int y = 0; y < kCanvas.height(); ++y)
        for (int x = 0; x < kCanvas.width(); ++x)
            if (a.pixel(x, y) != b.pixel(x, y))
                return false;
    return true;
}

void dragInSteps(TransformSession& s, H handle, QPointF from, QPointF to, int steps = 6) {
    s.beginDrag(handle, from);
    for (int i = 1; i <= steps; ++i)
        s.dragTo(from + (to - from) * (double(i) / steps), {});
}

}

TEST(TransformSessionGpuPreview, DraggingDoesNotResampleOnTheCpuUntilItPausesOrEnds) {
    Document doc(kCanvas, Qt::white);
    redSquare(doc);
    auto s = begin(doc, TransformMode::Freeform);
    s->setGpuPreview(true);
    const std::uint64_t cpu = s->cpuPreviewCount();
    EXPECT_FALSE(s->gpuPreviewActive());

    dragInSteps(*s, H::Corner2, {140, 100}, {190, 150});
    EXPECT_EQ(s->cpuPreviewCount(), cpu);
    EXPECT_EQ(s->gpuPreviewSteps(), 6u);
    EXPECT_TRUE(s->gpuPreviewActive());
    EXPECT_EQ(s->floatingQuad().corner[2], QPointF(190, 150));
    s->takeDirty();
    EXPECT_TRUE(s->takeDirty().isEmpty());

    s->settlePreview();
    EXPECT_EQ(s->cpuPreviewCount(), cpu + 1);
    EXPECT_FALSE(s->gpuPreviewActive());
    EXPECT_FALSE(s->takeDirty().isEmpty());
    EXPECT_EQ(s->image().pixelColor(150, 110), QColor(Qt::red));

    s->dragTo({195, 155}, {});
    EXPECT_TRUE(s->gpuPreviewActive());
    EXPECT_EQ(s->cpuPreviewCount(), cpu + 1);
    s->endDrag();
    EXPECT_EQ(s->cpuPreviewCount(), cpu + 2);
    EXPECT_FALSE(s->gpuPreviewActive());
    EXPECT_EQ(s->image().pixelColor(150, 110), QColor(Qt::red));
}

TEST(TransformSessionGpuPreview, WithoutTheOptInEveryStepResamples) {
    Document doc(kCanvas, Qt::white);
    redSquare(doc);
    auto s = begin(doc, TransformMode::Freeform);
    EXPECT_FALSE(s->gpuPreview());
    const std::uint64_t cpu = s->cpuPreviewCount();
    dragInSteps(*s, H::Corner2, {140, 100}, {190, 150});
    EXPECT_EQ(s->cpuPreviewCount(), cpu + 6);
    EXPECT_EQ(s->gpuPreviewSteps(), 0u);
    EXPECT_FALSE(s->gpuPreviewActive());
}

TEST(TransformSessionGpuPreview, ClickingWithoutMovingDoesNotResample) {
    Document doc(kCanvas, Qt::white);
    redSquare(doc);
    auto s = begin(doc, TransformMode::Freeform);
    s->setGpuPreview(true);
    const std::uint64_t cpu = s->cpuPreviewCount();
    s->beginDrag(H::Corner2, {140, 100});
    s->endDrag();
    EXPECT_EQ(s->cpuPreviewCount(), cpu);
}

TEST(TransformSessionGpuPreview, TheCpuPreviewAfterTheDragIsTheOneWithoutTheGpuPath) {
    for (TransformMode mode : {TransformMode::Resize, TransformMode::Rotate, TransformMode::Freeform, TransformMode::Deform}) {
        Document a(kCanvas, Qt::white), b(kCanvas, Qt::white);
        redSquare(a);
        redSquare(b);
        auto cpu = begin(a, mode);
        auto gpu = begin(b, mode);
        gpu->setGpuPreview(true);
        dragInSteps(*cpu, H::Corner2, {140, 100}, {170, 125});
        dragInSteps(*gpu, H::Corner2, {140, 100}, {170, 125});
        cpu->endDrag();
        gpu->endDrag();
        EXPECT_EQ(cpu->quad(), gpu->quad());
        EXPECT_EQ(cpu->image(), gpu->image()) << int(mode);
    }
}

TEST(TransformSessionGpuPreview, AppliedResultIsTheDocumentsTransformWhateverThePreviewDid) {
    const QPointF to(175, 128);
    for (int perspective : {0, 50}) {
        Document ref(kCanvas, Qt::white), viaGpu(kCanvas, Qt::white), viaCpu(kCanvas, Qt::white);
        redSquare(ref);
        redSquare(viaGpu);
        redSquare(viaCpu);
        MarqueeOptions o;
        o.final = transform::Interpolation::Bicubic;
        o.perspective = perspective;
        auto g = begin(viaGpu, TransformMode::Freeform, o);
        g->setGpuPreview(true);
        auto c = begin(viaCpu, TransformMode::Freeform, o);
        dragInSteps(*g, H::Corner2, {140, 100}, to);
        g->settlePreview();
        g->dragTo(to + QPointF(3, 3), {});
        ASSERT_TRUE(g->gpuPreviewActive());
        const transform::Quad quad = g->quad();
        c->beginDrag(H::Corner2, {140, 100});
        c->dragTo(quad.corner[2], {});
        c->endDrag();
        ASSERT_EQ(c->quad(), quad);

        const int steps = ref.undoStack()->count();
        ASSERT_TRUE(g->commit());
        ASSERT_TRUE(c->commit());
        ASSERT_TRUE(ref.transformLayer(ref.currentLayerId(), quad, o.final, g->perspective()));
        EXPECT_TRUE(samePixels(viaGpu.currentLayer()->pixels, ref.currentLayer()->pixels)) << perspective;
        EXPECT_TRUE(samePixels(viaCpu.currentLayer()->pixels, ref.currentLayer()->pixels)) << perspective;
        EXPECT_EQ(viaGpu.undoStack()->count(), steps + 1);
    }
}

TEST(TransformSessionGpuPreview, MoveSessionsDragTheBoxToo) {
    Document doc(kCanvas, Qt::white);
    redSquare(doc);
    auto made = TransformSession::beginMove(doc, doc.currentLayerId(), MarqueeOptions{});
    ASSERT_TRUE(made.ok());
    const auto& s = made.value();
    s->setGpuPreview(true);
    const std::uint64_t cpu = s->cpuPreviewCount();
    dragInSteps(*s, H::Inside, {120, 80}, {150, 95}, 5);
    EXPECT_EQ(s->cpuPreviewCount(), cpu);
    EXPECT_TRUE(s->gpuPreviewActive());
    const transform::Quad q = s->floatingQuad();
    EXPECT_EQ(q.corner[0], QPointF(130, 75));
    EXPECT_EQ(q.corner[2], QPointF(170, 115));
    s->endDrag();
    EXPECT_EQ(s->image().pixelColor(135, 80), QColor(Qt::red));
    EXPECT_EQ(s->image().pixelColor(110, 70), QColor(Qt::white));
}

TEST(TransformSessionGpuPreview, TheFloatingPictureHasTheLayersBelowAndAboveAroundTheLiftedPixels) {
    Document doc(kCanvas, Qt::white);
    redSquare(doc);
    const LayerId base = doc.currentLayerId();
    ASSERT_TRUE(doc.addLayer());
    doc.paintTarget(doc.currentLayerId()).value()->fill(QRect(10, 10, 20, 20), Qt::blue);
    doc.notifyPixelsChanged(doc.currentLayerId(), QRect(10, 10, 20, 20));
    doc.setCurrentLayer(base);
    LayerProps props = doc.currentLayer()->props;
    props.opacity = 50;
    ASSERT_TRUE(doc.setLayerProps(base, props));

    auto s = begin(doc, TransformMode::Resize);
    EXPECT_FALSE(s->floatingPreviewReady());
    const FloatingPreview& f = s->floatingPreview();
    EXPECT_TRUE(s->floatingPreviewReady());
    EXPECT_EQ(f.box, QRect(100, 60, 40, 40));
    EXPECT_NEAR(f.opacity, 0.5, 1e-9);
    ASSERT_EQ(f.source.size(), QSize(40, 40));
    EXPECT_EQ(f.source.pixelColor(5, 5), QColor(Qt::red));
    ASSERT_EQ(f.below.size(), kCanvas);
    EXPECT_EQ(f.below.pixelColor(110, 70), QColor(Qt::white));
    EXPECT_EQ(f.below.pixelColor(15, 15), QColor(Qt::white));
    ASSERT_FALSE(f.above.isNull());
    EXPECT_EQ(f.above.pixelColor(15, 15), QColor(Qt::blue));
    EXPECT_EQ(f.above.pixelColor(110, 70).alpha(), 0);
}

TEST(TransformSessionGpuPreview, WithASelectionTheBelowPictureKeepsWhatStaysBehind) {
    Document doc(kCanvas, Qt::white);
    redSquare(doc);
    ASSERT_TRUE(doc.setSelection(selection::marquee(kCanvas, QPoint(100, 60), QPoint(119, 99))));
    auto s = begin(doc, TransformMode::Resize);
    const FloatingPreview& f = s->floatingPreview();
    EXPECT_EQ(f.box, QRect(100, 60, 20, 40));
    EXPECT_EQ(f.below.pixelColor(110, 70), QColor(Qt::white));
    EXPECT_EQ(f.below.pixelColor(130, 70), QColor(Qt::red));
    EXPECT_EQ(f.above.isNull(), true);
}
