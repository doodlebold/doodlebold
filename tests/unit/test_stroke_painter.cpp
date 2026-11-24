// What any stroke owes the canvas, through the one way every tool paints (StrokePainter).
#include "core/brush/BrushSettings.h"
#include "core/stroke/StrokePainter.h"

#include <gtest/gtest.h>

#include <QColor>

using namespace doodlebold;

namespace {

InputSample at(double x, double y, double pressure = 1.0) {
    InputSample s;
    s.pos = QPointF(x, y);
    s.pressure = pressure;
    return s;
}

BrushSettings tool(ToolType type, double px) {
    BrushSettings s = defaultSettings(type);
    s.size = static_cast<int>(px * 10.0);
    return s;
}

TiledImage line(ToolType type, QColor colour, double px, TiledImage image) {
    StrokePainter painter;
    painter.begin(&image, colour, tool(type, px), at(40, 50));
    for (int x = 50; x <= 160; x += 10)
        painter.add(at(x, 50));
    painter.end();
    return image;
}

}

TEST(StrokePainter, EndGivesBackTheTilesAsTheyWere) {
    TiledImage image(QSize(100, 100));
    image.fill(Qt::blue);
    const std::array<Channel, 4> before = image.pixel(50, 50);
    StrokePainter painter;
    painter.begin(&image, Qt::red, tool(ToolType::Pencil, 10), at(50, 50));
    painter.add(at(52, 50));
    EXPECT_TRUE(painter.active());
    const TileMap originals = painter.end();
    EXPECT_FALSE(painter.active());
    ASSERT_FALSE(originals.empty());
    EXPECT_NE(image.pixel(50, 50), before);
    for (const auto& [coord, tile] : originals)
        image.setTile(coord.x, coord.y, tile);
    EXPECT_EQ(image.pixel(50, 50), before);
}

TEST(StrokePainter, TheSameInputAlwaysPaintsTheSamePixels) {
    for (const ToolType type : {ToolType::Pencil, ToolType::Brush, ToolType::WaterColor, ToolType::Marker}) {
        const TiledImage a = line(type, Qt::darkGreen, 20, TiledImage(QSize(200, 100)));
        const TiledImage b = line(type, Qt::darkGreen, 20, TiledImage(QSize(200, 100)));
        for (int y = 0; y < 100; ++y)
            for (int x = 0; x < 200; ++x)
                ASSERT_EQ(a.pixel(x, y), b.pixel(x, y)) << toolTypeKey(type).toStdString() << " at " << x << ", " << y;
    }
}

TEST(StrokePainter, NothingOutsideTheTipChanges) {
    for (const ToolType type : {ToolType::Pencil, ToolType::Brush, ToolType::Marker}) {
        TiledImage image(QSize(200, 100));
        image.fill(Qt::yellow);
        const std::array<Channel, 4> yellow = image.pixel(0, 0);
        const TiledImage painted = line(type, Qt::black, 10, image);
        EXPECT_EQ(painted.pixel(100, 5), yellow) << toolTypeKey(type).toStdString() << ": far above the line";
        EXPECT_EQ(painted.pixel(5, 50), yellow) << toolTypeKey(type).toStdString() << ": before its start";
        EXPECT_NE(painted.pixel(100, 50), yellow) << toolTypeKey(type).toStdString() << ": on the line";
    }
}

TEST(StrokePainter, TheEraserLeavesAnEmptyLayerEmpty) {
    const TiledImage erased = line(ToolType::Eraser, Qt::black, 20, TiledImage(QSize(200, 100)));
    for (int y = 0; y < 100; ++y)
        for (int x = 0; x < 200; ++x)
            ASSERT_EQ(erased.pixel(x, y)[3], 0) << x << ", " << y;
}
