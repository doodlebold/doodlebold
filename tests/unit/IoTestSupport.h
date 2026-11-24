#pragma once

#include "core/document/DocumentContent.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <random>

namespace doodlebold::testing {

inline void scribble(TiledImage& img, const QRect& area, unsigned seed) {
    std::mt19937 rng(seed);
    for (int y = area.top(); y <= area.bottom(); ++y)
        for (int x = area.left(); x <= area.right(); ++x) {
            const int a = static_cast<int>(rng() % (kChannelOne + 1));
            Tile* t = img.writableTile(x / Tile::kSize, y / Tile::kSize);
            Channel* p = t->at(x % Tile::kSize, y % Tile::kSize);
            p[0] = static_cast<Channel>(rng() % (a + 1));
            p[1] = static_cast<Channel>(rng() % (a + 1));
            p[2] = static_cast<Channel>(rng() % (a + 1));
            p[3] = static_cast<Channel>(a);
        }
}

inline LayerNode raster(LayerStack& s, const QString& name, BlendMode mode, int opacity) {
    LayerNode n = s.makeRaster(name);
    n.props.mode = mode;
    n.props.opacity = opacity;
    return n;
}

inline DocumentContent richContent() {
    DocumentContent c;
    LayerStack& s = c.state.layers;
    s = LayerStack(QSize(200, 150));

    LayerNode bg = raster(s, QStringLiteral("Background"), BlendMode::Normal, 100);
    scribble(bg.pixels = TiledImage(s.size()), QRect(0, 0, 200, 150), 1);
    bg.pixels.writableTile(1, 1)->px.fill(9669);
    s.insert(std::move(bg), kNoLayer, -1);

    LayerNode folder = s.makeFolder(QStringLiteral("Gruppe é中"));
    folder.props.mode = BlendMode::PassThrough;
    folder.props.expanded = false;
    folder.props.opacity = 77;
    const LayerId folderId = s.insert(std::move(folder), kNoLayer, -1);

    LayerNode ink = raster(s, QStringLiteral("Ink"), BlendMode::Multiply, 55);
    ink.pixels = TiledImage(s.size());
    scribble(ink.pixels, QRect(60, 10, 90, 20), 2);
    ink.props.clipping = true;
    ink.props.preserveOpacity = true;
    s.insert(std::move(ink), folderId, -1);

    LayerNode deepen = raster(s, QStringLiteral("Deepen"), BlendMode::Deepen, 80);
    deepen.pixels = TiledImage(s.size());
    scribble(deepen.pixels, QRect(130, 100, 70, 50), 3);
    deepen.props.locked = true;
    s.insert(std::move(deepen), folderId, 0);

    LayerNode hidden = raster(s, QStringLiteral("Hidden"), BlendMode::Overlay, 30);
    hidden.props.visible = false;
    hidden.pixels = TiledImage(s.size());
    s.insert(std::move(hidden), kNoLayer, -1);

    c.state.current = s.root().children[1].children[0].props.id;
    c.state.background = CanvasBackground::fromPaper(QColor(250, 245, 230));
    c.resolution = 300.0;
    c.view = ViewState{3.5, -37.5, true, QPointF(12.25, -7.5)};

    SelectionMask mask(s.size());
    mask.setUniform(0, 0, SelectionMask::kOne);
    std::vector<std::uint16_t> row(100);
    for (std::size_t i = 0; i < row.size(); ++i)
        row[i] = static_cast<std::uint16_t>(i * 160);
    for (int y = 70; y < 90; ++y)
        mask.writeRow(y, 100, static_cast<int>(row.size()), row.data());
    mask.squeeze();
    c.state.selection = mask;
    return c;
}

inline void expectSameImage(const TiledImage& a, const TiledImage& b, const QString& what) {
    ASSERT_EQ(a.size(), b.size()) << qPrintable(what);
    for (int ty = 0; ty < a.tilesY(); ++ty)
        for (int tx = 0; tx < a.tilesX(); ++tx) {
            const Tile* ta = a.tile(tx, ty);
            const Tile* tb = b.tile(tx, ty);
            const bool aNull = !ta || std::ranges::all_of(ta->px, [](Channel v) { return v == 0; });
            const bool bNull = !tb || std::ranges::all_of(tb->px, [](Channel v) { return v == 0; });
            ASSERT_EQ(aNull, bNull) << qPrintable(what) << " tile " << tx << "," << ty;
            if (!aNull) {
                ASSERT_EQ(ta->px, tb->px) << qPrintable(what) << " tile " << tx << "," << ty;
            }
        }
}

inline void expectSameNode(const LayerNode& a, const LayerNode& b, bool sameIds = true) {
    SCOPED_TRACE(qPrintable(a.props.name));
    LayerProps pa = a.props, pb = b.props;
    if (!sameIds)
        pa.id = pb.id = kNoLayer;
    EXPECT_TRUE(pa == pb) << "properties of " << qPrintable(a.props.name) << " differ";
    EXPECT_TRUE(a.text == b.text) << "text of " << qPrintable(a.props.name);
    ASSERT_EQ(a.mask.has_value(), b.mask.has_value()) << "mask of " << qPrintable(a.props.name);
    if (a.mask) {
        ASSERT_EQ(a.mask->size(), b.mask->size());
        for (int y = 0; y < a.mask->size().height(); ++y)
            for (int x = 0; x < a.mask->size().width(); ++x)
                ASSERT_EQ(a.mask->at(x, y), b.mask->at(x, y)) << "mask of " << qPrintable(a.props.name) << " at " << x << "," << y;
    }
    if (!a.isFolder()) {
        expectSameImage(a.pixels, b.pixels, a.props.name);
        return;
    }
    ASSERT_EQ(a.children.size(), b.children.size());
    for (std::size_t i = 0; i < a.children.size(); ++i)
        expectSameNode(a.children[i], b.children[i], sameIds);
}

inline void expectSameContent(const DocumentContent& a, const DocumentContent& b) {
    ASSERT_EQ(a.size(), b.size());
    EXPECT_EQ(a.state.background, b.state.background);
    EXPECT_DOUBLE_EQ(a.resolution, b.resolution);
    EXPECT_EQ(a.state.current, b.state.current);
    EXPECT_EQ(a.state.layers.nextIdValue(), b.state.layers.nextIdValue());
    ASSERT_EQ(a.state.layers.root().children.size(), b.state.layers.root().children.size());
    for (std::size_t i = 0; i < a.state.layers.root().children.size(); ++i)
        expectSameNode(a.state.layers.root().children[i], b.state.layers.root().children[i]);
    ASSERT_EQ(a.state.selection.has_value(), b.state.selection.has_value());
    if (a.state.selection) {
        EXPECT_TRUE(*a.state.selection == *b.state.selection);
        for (int y = 0; y < a.size().height(); ++y)
            for (int x = 0; x < a.size().width(); ++x)
                ASSERT_EQ(a.state.selection->at(x, y), b.state.selection->at(x, y)) << x << "," << y;
    }
    ASSERT_EQ(a.view.has_value(), b.view.has_value());
    if (a.view) {
        EXPECT_EQ(a.view->zoom, b.view->zoom);
        EXPECT_EQ(a.view->rotationDeg, b.view->rotationDeg);
        EXPECT_EQ(a.view->flipH, b.view->flipH);
        EXPECT_EQ(a.view->pan, b.view->pan);
    }
}

}
