#include "core/layers/BlendOps.h"
#include "core/document/Projection.h"
#include "core/layers/Compositor.h"

#include <gtest/gtest.h>

using namespace doodlebold;

namespace {

QImage render(const LayerStack& s, const QColor& paper = Qt::white) {
    QImage out(s.size(), QImage::Format_RGBA8888_Premultiplied);
    Compositor::renderOpaque(s, QRect(QPoint(0, 0), s.size()), paper, &out);
    return out;
}

LayerId addFilled(LayerStack& s, const QColor& c, LayerId parent = kNoLayer) {
    LayerNode n = s.makeRaster(s.nextLayerName());
    n.pixels.fill(c);
    return s.insert(std::move(n), parent, -1);
}

}

TEST(Compositor, EmptyStackShowsPaper) {
    LayerStack s(QSize(70, 70));
    EXPECT_EQ(render(s, QColor(10, 20, 30)).pixel(69, 69), qRgb(10, 20, 30));
}

TEST(Compositor, NormalOpacityAndVisibility) {
    LayerStack s(QSize(16, 16));
    addFilled(s, QColor(0, 0, 255));
    const LayerId top = addFilled(s, QColor(255, 0, 0));
    s.find(top)->props.opacity = 50;
    EXPECT_EQ(render(s).pixel(3, 3), qRgb(128, 0, 128));
    s.find(top)->props.visible = false;
    EXPECT_EQ(render(s).pixel(3, 3), qRgb(0, 0, 255));
}

TEST(Compositor, ClippingLayerOnlyShowsOverItsBase) {
    LayerStack s(QSize(64, 64));
    LayerNode base = s.makeRaster(QStringLiteral("base"));
    base.pixels.fill(QRect(0, 0, 32, 64), QColor(0, 0, 255));
    s.insert(std::move(base), kNoLayer, -1);
    const LayerId clip = addFilled(s, QColor(255, 0, 0));
    s.find(clip)->props.clipping = true;
    const QImage out = render(s);
    EXPECT_EQ(out.pixel(10, 10), qRgb(255, 0, 0));
    EXPECT_EQ(out.pixel(50, 10), qRgb(255, 255, 255));
}

TEST(Compositor, PassThroughFolderMatchesFlatLayers) {
    LayerStack flat(QSize(8, 8)), folded(QSize(8, 8));
    addFilled(flat, QColor(30, 160, 90));
    const LayerId t1 = addFilled(flat, QColor(220, 40, 10, 200));
    flat.find(t1)->props.mode = BlendMode::Screen;
    addFilled(folded, QColor(30, 160, 90));
    const LayerId f = folded.insert(folded.makeFolder(QStringLiteral("F")), kNoLayer, -1);
    folded.find(f)->props.mode = BlendMode::PassThrough;
    const LayerId t2 = addFilled(folded, QColor(220, 40, 10, 200), f);
    folded.find(t2)->props.mode = BlendMode::Screen;
    EXPECT_EQ(render(flat).pixel(1, 1), render(folded).pixel(1, 1));
}

TEST(Compositor, FlattenOfOneNormalLayerIsTheLayer) {
    LayerStack s(QSize(70, 70));
    const LayerId a = addFilled(s, QColor(12, 34, 56, 78));
    const TiledImage flat = Compositor::flatten({s.find(a)}, s.size());
    EXPECT_EQ(flat.pixel(69, 69), s.find(a)->pixels.pixel(69, 69));
}

TEST(BlendOps, ZeroAlphaSourceLeavesBackdropForEveryMode) {
    for (int m = 1; m < kBlendModeCount; ++m) {
        float dst[4] = {0.3f, 0.2f, 0.1f, 0.6f};
        const float src[4] = {0, 0, 0, 0};
        blend::blendSpan(static_cast<BlendMode>(m), src, 1.0f, dst, 1);
        EXPECT_FLOAT_EQ(dst[0], 0.3f) << blendModeInfo(static_cast<BlendMode>(m)).uiName;
        EXPECT_FLOAT_EQ(dst[3], 0.6f);
    }
}

TEST(BlendOps, OpaqueSourceOverTransparentShowsSourceForEveryMode) {
    for (int m = 1; m < kBlendModeCount; ++m) {
        float dst[4] = {0, 0, 0, 0};
        const float src[4] = {0.25f, 0.5f, 0.75f, 1.0f};
        blend::blendSpan(static_cast<BlendMode>(m), src, 1.0f, dst, 1);
        EXPECT_NEAR(dst[1], 0.5f, 1e-6) << blendModeInfo(static_cast<BlendMode>(m)).uiName;
        EXPECT_FLOAT_EQ(dst[3], 1.0f);
    }
}

namespace {
SelectionMask uniformMask(const QSize& size, int value) {
    SelectionMask m(size);
    const std::vector<std::uint16_t> row(std::size_t(size.width()), static_cast<std::uint16_t>(value));
    for (int y = 0; y < size.height(); ++y)
        m.writeRow(y, 0, size.width(), row.data());
    m.squeeze();
    return m;
}
}

TEST(Compositor, LayerMaskOnANormalLayerActsLikeAlpha) {
    LayerStack s(QSize(8, 8));
    addFilled(s, QColor(0, 0, 255));
    const LayerId top = addFilled(s, QColor(255, 0, 0));
    s.find(top)->mask = uniformMask(s.size(), SelectionMask::kOne);
    EXPECT_EQ(render(s).pixel(2, 2), qRgb(255, 0, 0));
    s.find(top)->mask = uniformMask(s.size(), 0);
    EXPECT_EQ(render(s).pixel(2, 2), qRgb(0, 0, 255));
    s.find(top)->mask = uniformMask(s.size(), SelectionMask::kOne / 2);
    s.find(top)->props.opacity = 100;
    const QRgb half = render(s).pixel(2, 2);
    s.find(top)->mask.reset();
    s.find(top)->props.opacity = 50;
    const QRgb byOpacity = render(s).pixel(2, 2);
    EXPECT_NEAR(qRed(half), qRed(byOpacity), 1);
    EXPECT_NEAR(qBlue(half), qBlue(byOpacity), 1);
}

// A projection keeps each zoomed-out level it was asked for: coming back to one, or bringing it up to date a band at a time, it shows
// what a fresh one does.
TEST(Projection, AReducedLevelComingBackShowsWhatAFreshOneDoes) {
    const QSize size(300, 200);
    const CanvasBackground paper = CanvasBackground::fromPaper(Qt::white);
    LayerStack s(size);
    const LayerId layer = addFilled(s, QColor(0, 0, 255, 128));
    Projection kept(size, paper);
    kept.reducedImage(s, 2);
    kept.reducedImage(s, 3);
    QImage red(50, 40, QImage::Format_ARGB32);
    red.fill(QColor(255, 0, 0));
    const QPoint at(100, 70);
    s.find(layer)->pixels.writeImage(red, at);
    kept.invalidate(QRect(at, red.size()));
    const QRect band(0, 0, size.width(), 64);
    kept.reducedImage(s, 1, band);
    for (const int level : {2, 3, 1}) {
        Projection fresh(size, paper);
        EXPECT_EQ(kept.reducedImage(s, level), fresh.reducedImage(s, level)) << level;
    }
}
