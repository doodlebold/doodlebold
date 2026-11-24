// The area finding the Magic Wand and the Bucket share, the Bucket's fill and settings, and the key table of
// Options › Keys.
#include "core/document/Document.h"
#include "core/options/AppOptions.h"
#include "core/options/KeyTable.h"
#include "core/selection/MagicWand.h"
#include "core/tools/ToolBox.h"
#include "core/tools/ToolBoxJson.h"

#include <gtest/gtest.h>

#include <memory>

using namespace doodlebold;
using selection::WandMode;
using selection::WandOptions;

namespace {

constexpr int kOne = SelectionMask::kOne;
const QSize kSize(60, 40);

TiledImage transparent() {
    return TiledImage(kSize);
}

TiledImage white() {
    TiledImage img(kSize);
    img.fill(Qt::white);
    return img;
}

WandOptions options(WandMode mode, int threshold = 0, int gap = 0, bool antialias = false) {
    WandOptions o;
    o.mode = mode;
    o.colourThreshold = threshold;
    o.transparencyRange = threshold;
    o.gapTolerance = gap;
    o.antialias = antialias;
    return o;
}

int fullPixels(const SelectionMask& m) {
    int n = 0;
    for (int y = 0; y < m.size().height(); ++y)
        for (int x = 0; x < m.size().width(); ++x)
            n += m.at(x, y) == kOne;
    return n;
}

// A black outline two pixels thick round (10, 10) … (49, 29), with an opening `gap` px tall in its right wall from y = 19.
TiledImage ring(int gap = 0) {
    TiledImage img = transparent();
    img.fill(QRect(10, 10, 40, 2), Qt::black);
    img.fill(QRect(10, 28, 40, 2), Qt::black);
    img.fill(QRect(10, 10, 2, 20), Qt::black);
    img.fill(QRect(48, 10, 2, 20), Qt::black);
    if (gap > 0)
        img.fill(QRect(48, 19, 2, gap), Qt::transparent);
    return img;
}

}

TEST(AreaFinding, SimilarColoredAreaStopsAtALine) {
    TiledImage img = white();
    img.fill(QRect(30, 0, 1, 40), Qt::black);
    const SelectionMask m = selection::magicWand(img, {10, 10}, options(WandMode::SimilarColour));
    EXPECT_EQ(m.at(29, 10), kOne);
    EXPECT_EQ(m.at(30, 10), 0);
    EXPECT_EQ(m.at(31, 10), 0);
    EXPECT_EQ(fullPixels(m), 30 * 40);
}

TEST(AreaFinding, ColorThresholdTakesInColoursThatClose) {
    TiledImage img = white();
    img.fill(QRect(5, 5, 1, 1), QColor(245, 245, 245));
    EXPECT_EQ(selection::magicWand(img, {20, 20}, options(WandMode::SimilarColour, 0)).at(5, 5), 0);
    EXPECT_EQ(selection::magicWand(img, {20, 20}, options(WandMode::SimilarColour, 9)).at(5, 5), 0);
    EXPECT_EQ(selection::magicWand(img, {20, 20}, options(WandMode::SimilarColour, 10)).at(5, 5), kOne);
}

TEST(AreaFinding, AllSimilarColoredPixelsReachAcrossLines) {
    TiledImage img = white();
    img.fill(QRect(30, 0, 1, 40), Qt::black);
    img.fill(QRect(5, 5, 4, 4), Qt::red);
    img.fill(QRect(40, 20, 4, 4), Qt::red);
    const SelectionMask connected = selection::magicWand(img, {6, 6}, options(WandMode::SimilarColour));
    EXPECT_EQ(fullPixels(connected), 16);
    const SelectionMask all = selection::magicWand(img, {6, 6}, options(WandMode::AllSimilar));
    EXPECT_EQ(fullPixels(all), 32);
    EXPECT_EQ(all.at(41, 21), kOne);
}

TEST(AreaFinding, EnclosedTransparentAreaIsTheInsideOfTheLines) {
    const TiledImage img = ring();
    const SelectionMask inside = selection::magicWand(img, {30, 20}, options(WandMode::EnclosedTransparent));
    EXPECT_EQ(fullPixels(inside), 36 * 16);
    EXPECT_EQ(inside.at(12, 12), kOne);
    EXPECT_EQ(inside.at(11, 12), 0) << "not the line";
    EXPECT_EQ(inside.at(5, 5), 0) << "not the outside";
    EXPECT_TRUE(selection::magicWand(img, {10, 10}, options(WandMode::EnclosedTransparent)).isEmpty()) << "a click on the line";
}

TEST(AreaFinding, EnclosedTransparentAreaReachesUnderTheLinesSoftEdge) {
    TiledImage img = ring();
    img.fill(QRect(12, 12, 1, 16), QColor(0, 0, 0, 160));  // the left wall's fringe, more solid nearer the wall
    img.fill(QRect(13, 12, 1, 16), QColor(0, 0, 0, 60));
    const SelectionMask enclosed = selection::magicWand(img, {30, 20}, options(WandMode::EnclosedTransparent));
    EXPECT_EQ(enclosed.at(13, 20), kOne);
    EXPECT_EQ(enclosed.at(12, 20), kOne);
    EXPECT_EQ(enclosed.at(11, 20), 0) << "never into the solid line";
    const SelectionMask similar = selection::magicWand(img, {30, 20}, options(WandMode::SimilarColour));
    EXPECT_EQ(similar.at(13, 20), 0) << "Similar Colored Area leaves the fringe";
}

TEST(AreaFinding, GapToleranceClosesAGapAndStillFillsToTheLines) {
    const TiledImage img = ring(2);
    const SelectionMask leaks = selection::magicWand(img, {30, 20}, options(WandMode::EnclosedTransparent));
    EXPECT_EQ(leaks.at(55, 5), kOne) << "without a tolerance the fill runs out through the gap";

    const SelectionMask closed = selection::magicWand(img, {30, 20}, options(WandMode::EnclosedTransparent, 0, 2));
    EXPECT_EQ(closed.at(55, 5), 0);
    EXPECT_EQ(closed.at(52, 20), 0);
    for (const QPoint p : {QPoint(30, 20), QPoint(12, 20), QPoint(12, 12), QPoint(47, 27), QPoint(30, 12)})
        EXPECT_EQ(closed.at(p.x(), p.y()), kOne) << p.x() << ", " << p.y();

    const SelectionMask wider = selection::magicWand(ring(4), {30, 20}, options(WandMode::EnclosedTransparent, 0, 2));
    EXPECT_EQ(wider.at(55, 5), kOne) << "a gap wider than the tolerance stays open";
}

TEST(AreaFinding, AClickInAPlaceNarrowerThanTheGapStillFillsIt) {
    TiledImage img = transparent();
    img.fill(QRect(0, 10, 60, 1), Qt::black);
    img.fill(QRect(0, 13, 60, 1), Qt::black);  // a channel two pixels high
    const SelectionMask m = selection::magicWand(img, {30, 11}, options(WandMode::EnclosedTransparent, 0, 10));
    EXPECT_EQ(m.at(30, 11), kOne);
    EXPECT_EQ(m.at(5, 12), kOne);
    EXPECT_EQ(m.at(30, 5), 0);
}

TEST(AreaFinding, AntiAliasingSoftensTheEdgeOutward) {
    TiledImage img = white();
    img.fill(QRect(30, 0, 1, 40), Qt::black);
    const SelectionMask soft = selection::magicWand(img, {10, 10}, options(WandMode::SimilarColour, 0, 0, true));
    EXPECT_EQ(soft.at(29, 10), kOne) << "the area stays whole";
    EXPECT_EQ(soft.at(30, 10), (4 * kOne + 8) / 16);
    EXPECT_EQ(soft.at(31, 10), 0);
}

TEST(AreaFinding, ASeedOffTheImageFindsNothing) {
    EXPECT_TRUE(selection::magicWand(white(), {-1, 5}, options(WandMode::SimilarColour)).isEmpty());
    EXPECT_TRUE(selection::magicWand(white(), {60, 5}, options(WandMode::SimilarColour)).isEmpty());
}

TEST(BucketFill, FillsTheAreaAsOneUndoStep) {
    Document doc(kSize, Qt::white);
    const LayerId layer = doc.currentLayerId();
    const SelectionMask area = selection::magicWand(ring(), {30, 20}, options(WandMode::EnclosedTransparent));
    const int steps = doc.undoStack()->count();
    ASSERT_TRUE(doc.fillArea(layer, Qt::red, area).ok());
    EXPECT_EQ(doc.undoStack()->count(), steps + 1);
    EXPECT_EQ(toUnorm8(doc.layer(layer)->pixels.pixel(30, 20)), (std::array<std::uint8_t, 4>{255, 0, 0, 255}));
    EXPECT_EQ(doc.layer(layer)->pixels.pixel(5, 5)[3], 0);
    EXPECT_FALSE(doc.selection()) << "the area isn't left as a selection";
    ASSERT_TRUE(doc.undo().ok());
    EXPECT_EQ(doc.layer(layer)->pixels.pixel(30, 20)[3], 0);
}

TEST(BucketFill, KeepsPreserveOpacityAndRefusesALockedLayerOrNothing) {
    Document doc(kSize, Qt::white);
    const LayerId layer = doc.currentLayerId();
    const SelectionMask everywhere = selection::all(kSize);
    EXPECT_EQ(doc.fillArea(layer, Qt::red, SelectionMask(kSize)).error(), EditError::NoChange);

    ASSERT_TRUE(doc.fillArea(layer, Qt::blue, selection::rectangle(kSize, QRectF(0, 0, 10, 10), false)).ok());
    LayerProps props = doc.layer(layer)->props;
    props.preserveOpacity = true;
    ASSERT_TRUE(doc.setLayerProps(layer, props).ok());
    ASSERT_TRUE(doc.fillArea(layer, Qt::red, everywhere).ok());
    EXPECT_EQ(toUnorm8(doc.layer(layer)->pixels.pixel(5, 5)), (std::array<std::uint8_t, 4>{255, 0, 0, 255}));
    EXPECT_EQ(doc.layer(layer)->pixels.pixel(20, 20)[3], 0) << "Preserve Opacity keeps the transparent part";

    props.locked = true;
    ASSERT_TRUE(doc.setLayerProps(layer, props).ok());
    EXPECT_EQ(doc.fillArea(layer, Qt::green, everywhere).error(), EditError::LayerLocked);
}

TEST(BucketSettingsTest, AreClampedAndSavedWithTheTool) {
    BrushSettings s = defaultSettings(ToolType::Bucket);
    EXPECT_EQ(s.bucket.mode, WandMode::SimilarColour);
    s.bucket.colourThreshold = 999;
    s.bucket.gapTolerance = -3;
    EXPECT_EQ(s.clamped().bucket.colourThreshold, 255);
    EXPECT_EQ(s.clamped().bucket.gapTolerance, 0);

    ToolBoxState state = defaultToolBoxState();
    ToolSlot& bucket = *state.groups[0].cells[5];
    ASSERT_EQ(bucket.type(), ToolType::Bucket);
    bucket.settings.bucket = {WandMode::EnclosedTransparent, 40, 7, false, false, true};
    bucket.key = QStringLiteral("Shift+B");
    const auto loaded = toolBoxStateFromJson(toJson(state));
    ASSERT_TRUE(loaded.ok());
    const ToolSlot& back = *loaded.value().groups[0].cells[5];
    EXPECT_EQ(back.settings.bucket, bucket.settings.bucket);
    EXPECT_EQ(back.key, QStringLiteral("Shift+B"));
    EXPECT_TRUE(loaded.value().groups[0].cells[0]->key.isEmpty());
}

namespace {

ToolBoxState trayOf(std::initializer_list<std::initializer_list<ToolType>> groups) {
    ToolBoxState s;
    for (const auto& types : groups) {
        ToolGroup g{QStringLiteral("G"), {}};
        for (ToolType t : types)
            g.cells.emplace_back(ToolSlot{toolTypeName(t), defaultSettings(t), std::nullopt, {}});
        g.cells.resize(9);
        s.groups.push_back(std::move(g));
    }
    return s;
}

std::vector<ToolType> typesIn(const ToolGroup& g) {
    std::vector<ToolType> types;
    for (const auto& cell : g.cells)
        if (cell)
            types.push_back(cell->type());
    return types;
}

}

// Feedback: "Bucket was not on the tray ... bucket should be there" (where the Blur is).
TEST(BucketPlacement, AnOldDefaultTrayGetsTheBucketWhereTheBlurWasAndTheBlurAtTheEnd) {
    ToolBoxState old = trayOf({{ToolType::Pencil, ToolType::Brush, ToolType::WaterColor, ToolType::Marker, ToolType::Eraser, ToolType::Blur,
                                ToolType::Smudge, ToolType::PixelArtPen, ToolType::Clone},
                               {}});
    old.current = {0, 5};
    QJsonObject json = toJson(old);
    json.remove(QStringLiteral("bucketPlaced"));
    const auto loaded = toolBoxStateFromJson(json);
    ASSERT_TRUE(loaded.ok());
    EXPECT_EQ(typesIn(loaded.value().groups[0]), typesIn(defaultToolBoxState().groups[0]));
    EXPECT_EQ(loaded.value().current, (SlotRef{0, 9})) << "the current tool is still the Blur";
}

TEST(BucketPlacement, ABucketTheUserMadeSwapsPlacesWithTheBlur) {
    ToolBoxState s = trayOf({{ToolType::Pencil, ToolType::Blur, ToolType::Eraser}, {ToolType::Bucket}});
    s.groups[1].cells[0]->name = QStringLiteral("My Bucket");
    placeBucket(s);
    EXPECT_EQ(s.groups[0].cells[1]->name, QStringLiteral("My Bucket"));
    EXPECT_EQ(s.groups[1].cells[0]->type(), ToolType::Blur);
}

TEST(BucketPlacement, ATrayWithoutABlurGetsTheBucketInItsFirstEmptyCell) {
    ToolBoxState s = trayOf({{ToolType::Pencil, ToolType::Eraser}});
    s.groups[0].cells[1].reset();
    placeBucket(s);
    EXPECT_EQ(s.groups[0].cells[1]->type(), ToolType::Bucket);
}

TEST(BucketPlacement, HappensOnceAFileIsMarked) {
    ToolBoxState s = trayOf({{ToolType::Pencil, ToolType::Blur}});
    const auto marked = toolBoxStateFromJson(toJson(s));
    ASSERT_TRUE(marked.ok());
    EXPECT_EQ(typesIn(marked.value().groups[0]), (std::vector<ToolType>{ToolType::Pencil, ToolType::Blur}))
        << "a tray saved with the Bucket placed keeps its own tools, even when the Bucket was deleted";
}

TEST(TrayKeys, AKeyStaysWithItsToolThroughRenameAndMoveAndIsNotCopied) {
    ToolBox tools;
    ASSERT_TRUE(tools.setSlotKey({0, 3}, QStringLiteral("Ctrl+K")).ok());
    ASSERT_TRUE(tools.renameSlot({0, 3}, QStringLiteral("Inky")).ok());
    ASSERT_TRUE(tools.moveSlot({0, 3}, {1, 0}).ok());
    EXPECT_EQ(tools.slot({1, 0})->key, QStringLiteral("Ctrl+K"));
    const auto copy = tools.duplicateSlot({1, 0});
    ASSERT_TRUE(copy.ok());
    EXPECT_TRUE(tools.slot(copy.value())->key.isEmpty()) << "a key picks one tool";
    EXPECT_EQ(tools.setSlotKey({0, 3}, QStringLiteral("K")).error(), ToolBoxError::SlotEmpty);
}

namespace {

KeyTable sampleTable() {
    const auto k = [](const char* text) { return QKeySequence::fromString(QString::fromLatin1(text)); };
    return KeyTable({{QStringLiteral("file.new"), QStringLiteral("File"), QStringLiteral("New"), {k("Ctrl+N")}, {k("Ctrl+N")}},
                     {QStringLiteral("file.open"), QStringLiteral("File"), QStringLiteral("Open"), {k("Ctrl+O")}, {k("Ctrl+O")}},
                     {QStringLiteral("window.fullScreen"),
                      QStringLiteral("Window"),
                      QStringLiteral("Full Screen"),
                      {k("F11"), k("Shift+Tab")},
                      {k("F11"), k("Shift+Tab")}},
                     {trayRowId(0, 2), QStringLiteral("Tray: Basic"), QStringLiteral("Water Color"), {}, {}}});
}

}

TEST(KeyTableTest, AKeyMovesToTheRowItIsGivenAndTheLoserIsNamed) {
    KeyTable t = sampleTable();
    const std::optional<int> loser = t.assign(1, QKeySequence(QStringLiteral("Ctrl+N")));
    ASSERT_TRUE(loser);
    EXPECT_EQ(*loser, 0);
    EXPECT_TRUE(t.row(0).keys.isEmpty());
    EXPECT_EQ(t.row(1).keys, (QList<QKeySequence>{QKeySequence(QStringLiteral("Ctrl+N"))}));
    EXPECT_FALSE(t.assign(3, QKeySequence(QStringLiteral("Alt+W"))));
    EXPECT_EQ(t.rowWithKey(QKeySequence(QStringLiteral("Alt+W"))), 3);
    EXPECT_FALSE(t.isDefault(1));

    const std::vector<int> taken = t.restoreDefault(0);
    EXPECT_EQ(taken, std::vector<int>{1}) << "Open gives Ctrl+N back";
    EXPECT_TRUE(t.isDefault(0));
    EXPECT_TRUE(t.row(1).keys.isEmpty());

    t.clear(2);
    EXPECT_TRUE(t.row(2).keys.isEmpty());
    t.restoreAllDefaults();
    for (int i = 0; i < t.size(); ++i)
        EXPECT_TRUE(t.isDefault(i)) << i;
    EXPECT_TRUE(t.row(3).keys.isEmpty()) << "a tray tool starts with no key";
}

TEST(KeyTableTest, TrayRowsAndKeyTextRoundTrip) {
    EXPECT_EQ(trayPlace(trayRowId(1, 7)), (std::optional<std::pair<int, int>>{{1, 7}}));
    EXPECT_FALSE(trayPlace(QStringLiteral("file.new")));
    EXPECT_FALSE(trayPlace(QStringLiteral("tray:x:1")));
    const QList<QKeySequence> keys{QKeySequence(QStringLiteral("F11")), QKeySequence(QStringLiteral("Shift+Tab"))};
    EXPECT_EQ(keysFromText(keysText(keys)), keys);
    EXPECT_TRUE(keysFromText(QString()).isEmpty());
}

TEST(KeyTableTest, OnlyChangedCommandsAreSaved) {
    KeyTable t = sampleTable();
    key_settings::save(t);
    EXPECT_TRUE(key_settings::load().isEmpty());
    t.assign(1, QKeySequence(QStringLiteral("Ctrl+N")));
    t.assign(3, QKeySequence(QStringLiteral("W")));
    key_settings::save(t);
    const auto saved = key_settings::load();
    EXPECT_EQ(saved.size(), 2) << "New lost its key and Open has a new one; the tray row is saved with the tray";
    EXPECT_TRUE(saved.value(QStringLiteral("file.new")).isEmpty());
    EXPECT_EQ(saved.value(QStringLiteral("file.open")), (QList<QKeySequence>{QKeySequence(QStringLiteral("Ctrl+N"))}));
    t.restoreAllDefaults();
    key_settings::save(t);
    EXPECT_TRUE(key_settings::load().isEmpty());
}

TEST(HeldKeyChoices, ChoosingAKeyThePartnerHasSwapsThem) {
    HeldKeys h;
    h = swappedHeldKey(h, &HeldKeys::zoom, Qt::AltModifier);
    EXPECT_EQ(h.zoom, Qt::AltModifier);
    EXPECT_EQ(h.rotate, Qt::ControlModifier);
    h = swappedHeldKey(h, &HeldKeys::move, Qt::AltModifier);
    EXPECT_EQ(h.move, Qt::AltModifier);
    EXPECT_EQ(h.pick, Qt::ControlModifier);
    h = swappedHeldKey(h, &HeldKeys::pick, Qt::ShiftModifier);
    EXPECT_EQ(h.pick, Qt::ShiftModifier);
    EXPECT_EQ(h.move, Qt::AltModifier) << "no clash, no swap";
}

TEST(HeldKeyChoices, ClampingRepairsAClashAndAKeyThatCantBeHeldAlone) {
    AppOptions::Values v;
    v.held.zoom = v.held.rotate = Qt::ShiftModifier;
    v.held.pick = v.held.move = Qt::AltModifier;
    v.held.pan = Qt::Key_Control;
    const HeldKeys h = AppOptions::clamped(v).held;
    EXPECT_NE(h.zoom, h.rotate);
    EXPECT_NE(h.pick, h.move);
    EXPECT_EQ(h.pan, int(Qt::Key_Space));
    v.held.pan = int(Qt::ControlModifier) | Qt::Key_A;
    EXPECT_EQ(AppOptions::clamped(v).held.pan, int(Qt::Key_Space));
    v.held.pan = Qt::Key_Z;
    EXPECT_EQ(AppOptions::clamped(v).held.pan, int(Qt::Key_Z));
}
