#include "core/brush/BrushSettings.h"
#include "core/document/Document.h"
#include "core/document/LayerOps.h"
#include "core/io/DoodleFormat.h"
#include "core/io/PsdFormat.h"
#include "core/paint/SelectionStrokeController.h"
#include "core/paint/StrokeController.h"
#include "core/selection/SelectionOps.h"

#include <QBuffer>

#include <gtest/gtest.h>

using namespace doodlebold;

namespace {

const QSize kCanvas(200, 150);
const QColor kInk(200, 30, 30);

InputSample at(double x, double y) {
    InputSample s;
    s.pos = QPointF(x, y);
    return s;
}

// A stroke from (x0, y0) to (x1, y1), a step at a time.
void stroke(Document& doc, ToolType tool, QPointF from, QPointF to) {
    BrushSettings settings = defaultSettings(tool);
    settings.size = 100;
    StrokeController sc(&doc);
    ASSERT_TRUE(sc.begin(at(from.x(), from.y()), kInk, settings).ok());
    constexpr int kSteps = 40;
    for (int i = 1; i <= kSteps; ++i) {
        const QPointF p = from + (to - from) * (double(i) / kSteps);
        sc.add(at(p.x(), p.y()));
    }
    sc.finish();
}

bool hasInk(const TiledImage& image, const QRect& area) {
    for (int y = area.top(); y <= area.bottom(); ++y)
        for (int x = area.left(); x <= area.right(); ++x)
            if (image.pixel(x, y)[3] != 0)
                return true;
    return false;
}

}

TEST(OffCanvas, AStrokeLeavingTheCanvasKeepsDrawing) {
    Document doc(kCanvas, Qt::white);
    const LayerId layer = doc.currentLayerId();
    stroke(doc, ToolType::Pencil, QPointF(100, 75), QPointF(-120, 75));
    const TiledImage& pixels = doc.layer(layer)->pixels;
    EXPECT_TRUE(hasInk(pixels, QRect(50, 70, 20, 10))) << "on the canvas";
    EXPECT_TRUE(hasInk(pixels, QRect(-110, 70, 20, 10))) << "past its left edge";
    EXPECT_LE(pixels.contentBounds(true).left(), -110);

    ASSERT_TRUE(doc.undo().ok());
    EXPECT_TRUE(doc.layer(layer)->pixels.isEmpty()) << "one undo step takes all of it";
    ASSERT_TRUE(doc.redo().ok());
    EXPECT_TRUE(hasInk(doc.layer(layer)->pixels, QRect(-110, 70, 20, 10)));
}

TEST(OffCanvas, EveryToolPaintsPastAllFourEdgesWithoutHarm) {
    for (const ToolType tool : {ToolType::Pencil, ToolType::Eraser, ToolType::Brush, ToolType::WaterColor, ToolType::Marker,
                                ToolType::PixelArtPen, ToolType::Blur, ToolType::Smudge}) {
        Document doc(kCanvas, Qt::white);
        const LayerId layer = doc.currentLayerId();
        ASSERT_TRUE(doc.fillLayer(layer, QColor(20, 90, 200)).ok());
        stroke(doc, ToolType::Pencil, QPointF(100, 75), QPointF(-90, 75));  // paint there for the mixing tools to pick up
        stroke(doc, tool, QPointF(100, 75), QPointF(-90, -80));
        stroke(doc, tool, QPointF(100, 75), QPointF(300, 240));
        stroke(doc, tool, QPointF(150, 30), QPointF(290, -90));
        stroke(doc, tool, QPointF(30, 120), QPointF(-100, 250));
        const QImage shown = doc.projection();
        EXPECT_EQ(shown.size(), kCanvas) << toolTypeName(tool).toStdString();
    }
}

TEST(OffCanvas, OnlyWhatReachesTheCanvasShows) {
    Document doc(kCanvas, Qt::white);
    stroke(doc, ToolType::Pencil, QPointF(-50, 75), QPointF(-120, 75));
    EXPECT_EQ(doc.projection().pixelColor(0, 75), QColor(Qt::white)) << "nothing has reached the canvas";
}

TEST(OffCanvas, MovingTheLayerBringsItBack) {
    Document doc(kCanvas, Qt::white);
    const LayerId layer = doc.currentLayerId();
    stroke(doc, ToolType::Pencil, QPointF(60, 75), QPointF(-120, 75));
    const QRect bounds = doc.layer(layer)->pixels.contentBounds(true);
    ASSERT_LT(bounds.left(), -100);

    ASSERT_TRUE(doc.moveLayerExactly(layer, transform::Turn::None, QPoint(150, 0)).ok());
    const TiledImage& moved = doc.layer(layer)->pixels;
    EXPECT_EQ(moved.contentBounds(true), bounds.translated(150, 0)) << "all of it moved";
    EXPECT_NE(doc.projection().pixelColor(40, 75), QColor(Qt::white)) << "what was past the edge shows now";
}

TEST(OffCanvas, MergingKeepsWhatIsPastTheCanvas) {
    Document doc(kCanvas, Qt::white);
    const LayerId low = doc.currentLayerId();
    stroke(doc, ToolType::Pencil, QPointF(60, 40), QPointF(-120, 40));
    const LayerId high = doc.addLayer().value();
    stroke(doc, ToolType::Pencil, QPointF(60, 90), QPointF(-120, 90));
    ASSERT_TRUE(doc.mergeDown(high).ok());
    EXPECT_TRUE(hasInk(doc.layer(low)->pixels, QRect(-110, 35, 20, 10)));
    EXPECT_TRUE(hasInk(doc.layer(low)->pixels, QRect(-110, 85, 20, 10)));
}

TEST(OffCanvas, ACopyTakesOnlyTheCanvas) {
    Document doc(kCanvas, Qt::white);
    stroke(doc, ToolType::Pencil, QPointF(60, 75), QPointF(-120, 75));
    const auto copied = doc.copy(doc.currentLayerId());
    ASSERT_TRUE(copied.ok());
    EXPECT_LE(copied.value().image.width(), kCanvas.width());
}

TEST(OffCanvas, ASelectionKeepsTheStrokeInsideIt) {
    Document doc(kCanvas, Qt::white);
    const LayerId layer = doc.currentLayerId();
    ASSERT_TRUE(doc.setSelection(selection::marquee(kCanvas, QPoint(0, 0), QPoint(199, 149))).ok());
    stroke(doc, ToolType::Pencil, QPointF(60, 75), QPointF(-120, 75));
    EXPECT_FALSE(hasInk(doc.layer(layer)->pixels, QRect(-110, 70, 20, 10))) << "nothing is selected out there";
    EXPECT_TRUE(hasInk(doc.layer(layer)->pixels, QRect(20, 70, 20, 10)));
}

TEST(OffCanvas, PaintingAMaskPastTheCanvasIsHarmless) {
    Document doc(kCanvas, Qt::white);
    const LayerId layer = doc.currentLayerId();
    ASSERT_TRUE(doc.addLayerMask(layer).ok());
    ASSERT_TRUE(doc.maskTarget());
    stroke(doc, ToolType::Pencil, QPointF(60, 75), QPointF(-120, 75));
    const SelectionMask& mask = *doc.layer(layer)->mask;
    EXPECT_EQ(mask.size(), kCanvas);
    EXPECT_TRUE(mask.bounds(false).intersected(QRect(QPoint(0, 0), kCanvas)) == mask.bounds(false)) << "a mask ends at the canvas";
}

TEST(OffCanvas, TheSelectionPenStopsAtTheCanvas) {
    Document doc(kCanvas, Qt::white);
    BrushSettings settings = defaultSettings(ToolType::Pencil);
    settings.size = 100;
    SelectionStrokeController sc(&doc);
    ASSERT_TRUE(sc.begin(at(40, 75), SelectionBrush::Pen, settings).ok());
    for (int i = 1; i <= 40; ++i)
        sc.add(at(40 - i * 4, 75));
    sc.finish();
    ASSERT_TRUE(doc.selection().has_value());
    EXPECT_GE(doc.selection()->bounds(true).left(), 0);
}

TEST(OffCanvas, ADoodleFileKeepsItAndAPsdOpens) {
    Document doc(kCanvas, Qt::white);
    const LayerId layer = doc.currentLayerId();
    stroke(doc, ToolType::Pencil, QPointF(60, 75), QPointF(-120, 75));
    const QRect bounds = doc.layer(layer)->pixels.contentBounds(true);
    DocumentContent content = doc.content();

    QBuffer doodle;
    doodle.open(QIODevice::ReadWrite);
    ASSERT_TRUE(DoodleFormat().write(content, doodle, {}).ok());
    doodle.seek(0);
    const auto loaded = DoodleFormat().read(doodle);
    ASSERT_TRUE(loaded.ok()) << qPrintable(loaded.error().describe());
    EXPECT_EQ(loaded.value().content.state.layers.root().children.front().pixels.contentBounds(true), bounds);

    QBuffer psd;
    psd.open(QIODevice::ReadWrite);
    ASSERT_TRUE(PsdFormat().write(content, psd, {}).ok());
    psd.seek(0);
    const auto opened = PsdFormat().read(psd);
    ASSERT_TRUE(opened.ok()) << qPrintable(opened.error().describe());
}
