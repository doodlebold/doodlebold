#include "core/document/Document.h"
#include "core/layers/Compositor.h"
#include "../SignalCounter.h"

#include <QUndoStack>
#include <algorithm>

#include <gtest/gtest.h>

using namespace doodlebold;

namespace {

void addProbeLayer(Document& doc) {
    TiledImage layer(doc.size());
    const auto set = [&](int x, int y, std::array<Channel, 4> px) {
        auto* tile = layer.writableTile(x / Tile::kSize, y / Tile::kSize);
        std::ranges::copy(px, tile->at(x % Tile::kSize, y % Tile::kSize));
    };
    set(3, 3, {kChannelOne / 2, 0, 0, kChannelOne / 2});
    set(20, 20, {0, 0, kChannelOne, kChannelOne});
    EXPECT_TRUE(doc.addImageLayer(std::move(layer), QStringLiteral("probe")).ok());
}

QColor shown(Document& doc, int x, int y) {
    return doc.projection().pixelColor(x, y);
}

}

TEST(CanvasBackground, ModesHaveTheirOrderNamesAndStableKeys) {
    EXPECT_EQ(kBackgroundModeCount, 8);
    const char* labels[] = {"White",
                            "Black",
                            "Color",
                            "Transparent (Light Checker)",
                            "Transparent (Dark Checker)",
                            "Transparent (White)",
                            "Transparent (Black)",
                            "Transparent (Color)"};
    for (int i = 0; i < kBackgroundModeCount; ++i) {
        const auto mode = static_cast<BackgroundMode>(i);
        EXPECT_EQ(backgroundModeLabel(mode), QString::fromLatin1(labels[i]));
        EXPECT_EQ(backgroundModeFromKey(backgroundModeKey(mode)), mode);
        EXPECT_EQ(CanvasBackground{mode}.isTransparent(), i >= 3);
    }
    EXPECT_FALSE(backgroundModeFromKey(QStringLiteral("mauve")).has_value());
    EXPECT_EQ(CanvasBackground{}.mode, BackgroundMode::White);
    EXPECT_EQ(CanvasBackground{}.color, QColor(128, 128, 128));
}

TEST(CanvasBackground, PaperColorsFollowTheMode) {
    CanvasBackground b;
    b.color = QColor(255, 128, 0);
    const std::pair<BackgroundMode, QColor> expected[] = {
        {BackgroundMode::White, Qt::white},
        {BackgroundMode::Black, Qt::black},
        {BackgroundMode::Color, QColor(255, 128, 0)},
        {BackgroundMode::TransparencyWhite, Qt::white},
        {BackgroundMode::TransparencyBlack, Qt::black},
        {BackgroundMode::TransparencyColor, QColor(255, 128, 0)},
    };
    for (const auto& [mode, color] : expected) {
        b.mode = mode;
        EXPECT_EQ(b.paperColor(), color) << backgroundModeLabel(mode).toStdString();
    }
    EXPECT_EQ(CanvasBackground::fromPaper(Qt::white).mode, BackgroundMode::White);
    EXPECT_EQ(CanvasBackground::fromPaper(Qt::black).mode, BackgroundMode::Black);
    EXPECT_EQ(CanvasBackground::fromPaper(Qt::transparent).mode, BackgroundMode::TransparencyWhite);
    const CanvasBackground cream = CanvasBackground::fromPaper(QColor(250, 245, 230));
    EXPECT_EQ(cream.mode, BackgroundMode::Color);
    EXPECT_EQ(cream.color, QColor(250, 245, 230));
}

TEST(CanvasBackground, CheckerboardAlternatesAndAnchorsAtTheCorner) {
    CanvasBackground b;
    b.mode = BackgroundMode::TransparencyBrightChecker;
    const Backdrop bright = b.backdrop();
    ASSERT_GT(bright.square, 0);
    EXPECT_NE(bright.a, bright.b);
    const int s = bright.square;
    EXPECT_EQ(bright.at(0, 0), bright.a);
    EXPECT_EQ(bright.at(s - 1, s - 1), bright.a);
    EXPECT_EQ(bright.at(s, 0), bright.b);
    EXPECT_EQ(bright.at(0, s), bright.b);
    EXPECT_EQ(bright.at(s, s), bright.a);
    EXPECT_EQ(bright.at(5 * s + 1, 3 * s + 2), bright.a);
    EXPECT_EQ(bright.at(5 * s, 4 * s), bright.b);
    b.mode = BackgroundMode::TransparencyDarkChecker;
    const Backdrop dark = b.backdrop();
    EXPECT_NE(dark, bright);
    EXPECT_LT(qGray(dark.a), qGray(bright.a));
    EXPECT_LT(qGray(dark.b), qGray(bright.b));
    EXPECT_GT(bright.square, 0);
    EXPECT_EQ(dark.square, bright.square);
    b.mode = BackgroundMode::TransparencyBlack;
    const Backdrop flat = b.backdrop();
    EXPECT_EQ(flat.square, 0);
    EXPECT_EQ(flat.at(0, 0), QColor(Qt::black).rgb());
    EXPECT_EQ(flat.at(77, 13), QColor(Qt::black).rgb());
}

TEST(CanvasDisplay, OpaqueModesShowTheirColourAndBlendOverIt) {
    for (const auto& [mode, color] :
         {std::pair{BackgroundMode::White, QColor(Qt::white)}, std::pair{BackgroundMode::Black, QColor(Qt::black)}}) {
        Document doc(QSize(40, 40), CanvasBackground{mode}, 72.0);
        addProbeLayer(doc);
        EXPECT_EQ(shown(doc, 0, 0), color);
        EXPECT_EQ(shown(doc, 20, 20), QColor(Qt::blue));
        const QColor mixed = shown(doc, 3, 3);
        EXPECT_NEAR(mixed.red(), mode == BackgroundMode::White ? 255 : 128, 1);
        EXPECT_NEAR(mixed.green(), mode == BackgroundMode::White ? 128 : 0, 1);
    }
    CanvasBackground color;
    color.mode = BackgroundMode::Color;
    color.color = QColor(10, 200, 30);
    Document doc(QSize(40, 40), color, 72.0);
    addProbeLayer(doc);
    EXPECT_EQ(shown(doc, 0, 0), QColor(10, 200, 30));
    EXPECT_EQ(shown(doc, 39, 39), QColor(10, 200, 30));
}

TEST(CanvasDisplay, TransparencyWhiteShowsFlatWhiteButTheDocumentIsTransparent) {
    Document doc(QSize(40, 40), CanvasBackground{BackgroundMode::TransparencyWhite}, 72.0);
    addProbeLayer(doc);
    EXPECT_EQ(shown(doc, 0, 0), QColor(Qt::white));
    EXPECT_EQ(shown(doc, 20, 20), QColor(Qt::blue));
    EXPECT_NEAR(shown(doc, 3, 3).green(), 128, 1);
    EXPECT_TRUE(doc.background().isTransparent());
    EXPECT_EQ(doc.currentLayer()->pixels.pixel(0, 0)[3], 0);
    EXPECT_EQ(doc.layers().layerCount(), 2);
}

TEST(CanvasDisplay, CheckerboardsShowThroughTransparentPixels) {
    for (const BackgroundMode mode : {BackgroundMode::TransparencyBrightChecker, BackgroundMode::TransparencyDarkChecker}) {
        CanvasBackground b{mode};
        const Backdrop pattern = b.backdrop();
        Document doc(QSize(40, 40), b, 72.0);
        addProbeLayer(doc);
        for (const QPoint p : {QPoint(0, 0), QPoint(pattern.square, 0), QPoint(0, pattern.square),
                               QPoint(pattern.square + 1, pattern.square + 2), QPoint(39, 39)})
            EXPECT_EQ(doc.projection().pixel(p.x(), p.y()), pattern.at(p.x(), p.y())) << p.x() << "," << p.y();
        EXPECT_EQ(shown(doc, 20, 20), QColor(Qt::blue));
        const QColor mixed = shown(doc, 3, 3);
        EXPECT_NEAR(mixed.red(), (255 + qRed(pattern.a)) / 2, 1);
        EXPECT_NEAR(mixed.green(), qGreen(pattern.a) / 2, 1);
    }
}

TEST(CanvasDisplay, BlendModesSeeTransparencyNotPaperOnATransparentCanvas) {
    for (const BackgroundMode mode : {BackgroundMode::White, BackgroundMode::TransparencyWhite}) {
        Document doc(QSize(8, 8), CanvasBackground{mode}, 72.0);
        TiledImage layer(doc.size());
        auto* tile = layer.writableTile(0, 0);
        std::copy_n(std::array<Channel, 4>{kChannelOne, kChannelOne / 2, 0, kChannelOne}.begin(), 4, tile->at(1, 1));
        ASSERT_TRUE(doc.addImageLayer(std::move(layer), QStringLiteral("m")).ok());
        LayerProps p = doc.currentLayer()->props;
        p.mode = BlendMode::Multiply;
        ASSERT_TRUE(doc.setLayerProps(doc.currentLayerId(), p).ok());
        EXPECT_NEAR(shown(doc, 1, 1).red(), 255, 1);
        EXPECT_NEAR(shown(doc, 1, 1).green(), 128, 1);
    }
}

TEST(CanvasDisplay, ChangingTheBackgroundRecompositesEverythingAndIsUndoable) {
    Document doc(QSize(40, 40), CanvasBackground{}, 72.0);
    addProbeLayer(doc);
    ASSERT_EQ(shown(doc, 0, 0), QColor(Qt::white));
    test::SignalCounter background(&doc, &Document::backgroundChanged);
    test::SignalCounter image(&doc, &Document::imageChanged);
    const int steps = doc.undoStack()->count();

    CanvasBackground dark{BackgroundMode::TransparencyDarkChecker};
    ASSERT_TRUE(doc.setBackground(dark).ok());
    EXPECT_EQ(doc.background(), dark);
    EXPECT_EQ(background.count(), 1);
    EXPECT_GE(image.count(), 1);
    EXPECT_EQ(doc.projection().pixel(0, 0), dark.backdrop().a);
    EXPECT_EQ(doc.undoStack()->count(), steps + 1) << "changing the background is an edit";
    EXPECT_TRUE(doc.isModified());
    EXPECT_EQ(doc.undoStack()->undoText(), QStringLiteral("Canvas Background"));

    const auto same = doc.setBackground(dark);
    ASSERT_FALSE(same.ok());
    EXPECT_EQ(same.error(), EditError::NoChange);
    EXPECT_EQ(doc.undoStack()->count(), 2);

    ASSERT_TRUE(doc.undo().ok());
    EXPECT_EQ(doc.background(), CanvasBackground{});
    EXPECT_EQ(shown(doc, 0, 0), QColor(Qt::white));
    EXPECT_EQ(background.count(), 2);
    ASSERT_TRUE(doc.redo().ok());
    EXPECT_EQ(doc.background(), dark);
    EXPECT_EQ(doc.projection().pixel(0, 0), dark.backdrop().a);
}

TEST(CanvasDisplay, SwitchingBetweenOpaqueModesChangesThePaper) {
    Document doc(QSize(40, 40), CanvasBackground{}, 72.0);
    addProbeLayer(doc);
    CanvasBackground color{BackgroundMode::Color, QColor(10, 20, 30)};
    ASSERT_TRUE(doc.setBackground(color).ok());
    EXPECT_EQ(shown(doc, 0, 0), QColor(10, 20, 30));
    color.color = QColor(11, 20, 30);
    ASSERT_TRUE(doc.setBackground(color).ok());
    EXPECT_EQ(shown(doc, 0, 0), QColor(11, 20, 30));
}

TEST(DocumentResolution, DefaultsTo72AndTravelsWithTheContent) {
    Document plain(QSize(10, 10), Qt::white);
    EXPECT_DOUBLE_EQ(plain.resolution(), 72.0);
    EXPECT_EQ(plain.background(), CanvasBackground{});
    Document dense(QSize(10, 10), CanvasBackground{BackgroundMode::Black}, 350.0);
    EXPECT_DOUBLE_EQ(dense.resolution(), 350.0);
    const DocumentContent content = dense.content();
    EXPECT_DOUBLE_EQ(content.resolution, 350.0);
    EXPECT_EQ(content.background().mode, BackgroundMode::Black);
    Document copy(content);
    EXPECT_DOUBLE_EQ(copy.resolution(), 350.0);
    EXPECT_EQ(copy.background(), content.background());
    EXPECT_EQ(copy.projection().pixelColor(0, 0), QColor(Qt::black));
}
