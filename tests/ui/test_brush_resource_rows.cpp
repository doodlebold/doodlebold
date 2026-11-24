#include "core/brush/BrushResources.h"
#include "core/tools/ToolBox.h"
#include "ui/Theme.h"
#include "ui/ValueBar.h"
#include "ui/panels/BrushPreviewStrip.h"
#include "ui/panels/BrushResourceList.h"
#include "ui/panels/BrushResourceSection.h"
#include "ui/panels/ToolPanel.h"
#include "ui/panels/ToolParameters.h"
#include "ScreenshotSupport.h"

#include <QAbstractItemView>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QScreen>
#include <QScrollBar>
#include <QSettings>
#include <QStyleOptionViewItem>
#include <QStyledItemDelegate>
#include <QPainter>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QToolButton>

#include <gtest/gtest.h>

#include <memory>

using namespace doodlebold;

namespace {

constexpr SlotRef kPencil{0, 0}, kBrush{0, 1}, kMarker{0, 3};

class BrushResourceRowsTest : public testing::Test {
protected:
    void SetUp() override {
        QStandardPaths::setTestModeEnabled(true);
        test::applyTheme();
        library = std::make_unique<BrushResourceLibrary>(folder.filePath(QStringLiteral("brushes")), BrushResourceLibrary::BuiltIns::Left);
        QImage picture(64, 64, QImage::Format_Grayscale8);
        picture.fill(128);
        ASSERT_TRUE(library
                        ->add({{ResourceKind::BlotMap, QStringLiteral("Blots One"), picture},
                               {ResourceKind::BlotMap, QStringLiteral("Blots Two"), picture},
                               {ResourceKind::Shape, QStringLiteral("Tip One"), picture},
                               {ResourceKind::Bristle, QStringLiteral("Hair One"), picture},
                               {ResourceKind::Texture, QStringLiteral("Weave"), picture},
                               {ResourceKind::Texture, QStringLiteral("Speckle"), picture}})
                        .ok());
        panel = std::make_unique<ToolPanel>(&tools, library.get());
        panel->resize(320, 900);
        panel->show();
        ASSERT_TRUE(tools.selectSlot(kBrush).ok());
        QCoreApplication::processEvents();
    }

    template <typename T> T* child(const char* name) const {
        T* w = panel->findChild<T*>(QString::fromLatin1(name));
        EXPECT_NE(w, nullptr) << name;
        return w;
    }
    QToolButton* tipHeader() const { return child<QWidget>("tipShapeSection")->findChild<QToolButton*>("sectionHeader"); }
    bool tipShapeOpen() const { return tipHeader()->isChecked(); }
    void openTipShape(bool open = true) {
        if (tipHeader()->isChecked() != open)
            tipHeader()->click();
        QCoreApplication::processEvents();
    }
    QStringList entries(const BrushResourceList* list) const {
        QStringList out;
        for (int i = 0; i < list->count(); ++i)
            out << list->itemText(i);
        return out;
    }
    void pick(BrushResourceList* list, const QString& text) {
        const int i = list->findText(text);
        ASSERT_GE(i, 0) << text.toStdString();
        list->setCurrentIndex(i);
        emit list->activated(i);
    }

    QTemporaryDir folder;
    ToolBox tools;
    std::unique_ptr<BrushResourceLibrary> library;
    std::unique_ptr<ToolPanel> panel;
};

}

TEST_F(BrushResourceRowsTest, ListsShowNoneTheResourcesAndImportEntries) {
    auto* forms = child<BrushResourceList>("brushFormList");
    auto* textures = child<BrushResourceList>("brushTextureList");
    const QStringList f = entries(forms), t = entries(textures);
    EXPECT_EQ(f.first(), BrushResourceLibrary::kSimpleCircle);
    EXPECT_TRUE(f.contains(QStringLiteral("Blots One")));
    EXPECT_TRUE(f.contains(QStringLiteral("Tip One")));
    EXPECT_TRUE(f.contains(QStringLiteral("Import Brush Shape Picture ...")));
    EXPECT_TRUE(f.contains(QStringLiteral("Import Bristle Picture ...")));
    EXPECT_EQ(t.first(), BrushResourceLibrary::kNoTexture);
    EXPECT_TRUE(t.contains(QStringLiteral("Weave")));
    EXPECT_EQ(t.last(), QStringLiteral("Import Texture ..."));
    EXPECT_FALSE(f.contains(QStringLiteral("Import Pack ...")));
    EXPECT_FALSE(forms->itemIcon(forms->findText(QStringLiteral("Blots One"))).isNull()) << "a picture of the bitmap";
    EXPECT_EQ(forms->currentText(), BrushResourceLibrary::kSimpleCircle) << "a new tool's form";
}

TEST_F(BrushResourceRowsTest, ChoosingAFormSetsTheToolsFormAndEnablesItsOptions) {
    auto* forms = child<BrushResourceList>("brushFormList");
    auto* strength = child<ValueBar>("formStrengthBar");
    EXPECT_FALSE(strength->isEnabled()) << "no form: no strength";
    pick(forms, QStringLiteral("Blots One"));
    EXPECT_EQ(tools.settings().form, QStringLiteral("Blots One"));
    EXPECT_TRUE(strength->isEnabled());
    strength->setValue(80);
    EXPECT_EQ(tools.settings().formStrength, 80);

    openTipShape();
    child<ValueBar>("formScaleBar")->setValue(150);
    child<QCheckBox>("formInvertCheck")->setChecked(true);
    EXPECT_EQ(tools.settings().formScale, 150);
    EXPECT_TRUE(tools.settings().formInvert);

    pick(child<BrushResourceList>("brushTextureList"), QStringLiteral("Speckle"));
    EXPECT_EQ(tools.settings().texture, QStringLiteral("Speckle"));
}

TEST_F(BrushResourceRowsTest, TheRowsFollowTheCurrentTool) {
    BrushSettings s = tools.settings();
    s.form = QStringLiteral("Blots Two");
    s.formStrength = 70;
    s.texture = QStringLiteral("Weave");
    s.textureStrength = 40;
    tools.setSettings(s);
    EXPECT_EQ(child<BrushResourceList>("brushFormList")->currentText(), QStringLiteral("Blots Two"));
    EXPECT_EQ(child<ValueBar>("formStrengthBar")->value(), 70);
    EXPECT_EQ(child<BrushResourceList>("brushTextureList")->currentText(), QStringLiteral("Weave"));
    EXPECT_EQ(child<ValueBar>("textureStrengthBar")->value(), 40);
    ASSERT_TRUE(tools.selectSlot(kPencil).ok());
    EXPECT_EQ(child<BrushResourceList>("brushFormList")->currentText(), BrushResourceLibrary::kSimpleCircle);
}

TEST_F(BrushResourceRowsTest, AFormMissingFromTheLibraryIsKeptGreyed) {
    BrushSettings s = tools.settings();
    s.form = QStringLiteral("Deleted Since");
    tools.setSettings(s);
    auto* forms = child<BrushResourceList>("brushFormList");
    EXPECT_EQ(forms->currentText(), QStringLiteral("Deleted Since"));
    EXPECT_EQ(tools.settings().form, QStringLiteral("Deleted Since")) << "not changed by showing it";
}

TEST_F(BrushResourceRowsTest, ImportingChoosesTheNewEntryAndABadFileIsReported) {
    QImage grain(64, 64, QImage::Format_Grayscale8);
    grain.fill(128);
    const QString file = folder.filePath(QStringLiteral("Rough Grain.png"));
    ASSERT_TRUE(grain.save(file));
    auto* textures = child<BrushResourceList>("brushTextureList");
    textures->importFile(ResourceKind::Texture, file);
    EXPECT_EQ(tools.settings().texture, QStringLiteral("Rough Grain"));
    EXPECT_EQ(textures->currentText(), QStringLiteral("Rough Grain"));
    EXPECT_GE(child<BrushResourceList>("brushTextureList")->findText(QStringLiteral("Rough Grain")), 0);

    QString message;
    QObject::connect(panel->parameters(), &ToolParameters::refused, [&](const QString& m) { message = m; });
    const QString bad = folder.filePath(QStringLiteral("broken.png"));
    QFile f(bad);
    ASSERT_TRUE(f.open(QIODevice::WriteOnly));
    f.write("not an image");
    f.close();
    child<BrushResourceList>("brushFormList")->importFile(ResourceKind::Shape, bad);
    EXPECT_TRUE(message.contains(QStringLiteral("broken.png"))) << message.toStdString();
    EXPECT_EQ(tools.settings().form, BrushResourceLibrary::kSimpleCircle) << "unchanged";
}

TEST_F(BrushResourceRowsTest, TheScaleRangeIs10To500) {
    openTipShape();
    for (const char* name : {"formScaleBar", "textureScaleBar"}) {
        EXPECT_EQ(child<ValueBar>(name)->minimum(), 10) << name;
        EXPECT_EQ(child<ValueBar>(name)->maximum(), 500) << name;
    }
}

TEST_F(BrushResourceRowsTest, TheTextureListHasALayerPaperEntryAndItsScaleIsGreyed) {
    auto* textures = child<BrushResourceList>("brushTextureList");
    const QStringList t = entries(textures);
    ASSERT_GE(t.size(), 3);
    EXPECT_EQ(t[0], BrushResourceLibrary::kNoTexture);
    EXPECT_EQ(t[1], BrushResourceLibrary::kLayerPaper);
    EXPECT_FALSE(textures->itemData(1, Qt::ToolTipRole).toString().isEmpty()) << "says what it does here";
    pick(textures, BrushResourceLibrary::kLayerPaper);
    EXPECT_EQ(tools.settings().texture, BrushResourceLibrary::kLayerPaper);
    openTipShape();
    EXPECT_FALSE(child<ValueBar>("textureScaleBar")->isEnabled());
    EXPECT_TRUE(child<ValueBar>("textureStrengthBar")->isEnabled()) << "Intensity works as for any texture";
    EXPECT_TRUE(child<QCheckBox>("textureInvertCheck")->isEnabled()) << "so does Invert";
    pick(textures, QStringLiteral("Weave"));
    EXPECT_TRUE(child<ValueBar>("textureScaleBar")->isEnabled());
    EXPECT_EQ(textures->itemData(1, Qt::ToolTipRole).toString().contains(QStringLiteral("no paper texture"), Qt::CaseInsensitive), false)
        << "the old caveat is gone";
}

TEST_F(BrushResourceRowsTest, TheBodyAndTheStrengthFollowTheKindOfForm) {
    openTipShape();
    auto* forms = child<BrushResourceList>("brushFormList");
    auto* strength = child<ValueBar>("formStrengthBar");
    const auto shown = [&](const char* name) { return child<QWidget>(name)->isVisibleTo(panel.get()); };

    pick(forms, QStringLiteral("Blots One"));
    EXPECT_TRUE(shown("formScaleBar"));
    EXPECT_FALSE(shown("bristleMinimumBar"));
    EXPECT_FALSE(shown("shapeAngleBar"));
    strength->setValue(65);
    EXPECT_EQ(tools.settings().formStrength, 65) << "Strength";

    pick(forms, QStringLiteral("Hair One"));
    EXPECT_FALSE(shown("formScaleBar"));
    EXPECT_TRUE(shown("bristleMinimumBar"));
    EXPECT_TRUE(shown("bristleDirectionCombo"));
    EXPECT_EQ(strength->value(), tools.settings().bristleSize) << "the strength field is Hair Width";
    strength->setValue(30);
    EXPECT_EQ(tools.settings().bristleSize, 30);
    EXPECT_EQ(tools.settings().formStrength, 65) << "the blot strength is kept";
    child<ValueBar>("bristleRandomizeBar")->setValue(15);
    auto* direction = child<QComboBox>("bristleDirectionCombo");
    direction->setCurrentIndex(2);
    emit direction->activated(2);
    EXPECT_EQ(tools.settings().bristleRandomize, 15);
    EXPECT_EQ(tools.settings().bristleDirection, 2);

    pick(forms, QStringLiteral("Tip One"));
    EXPECT_TRUE(shown("shapeAngleBar"));
    EXPECT_FALSE(shown("bristleMinimumBar"));
    child<ValueBar>("shapeSpacingBar")->setValue(33);
    EXPECT_EQ(tools.settings().shapeSpacing, 33);
    strength->setValue(60);
    EXPECT_EQ(tools.settings().shapeDensity, 60) << "the strength field is Density";
}

TEST_F(BrushResourceRowsTest, ThePixelArtPenHasNoFormOrTexture) {
    openTipShape();
    tools.selectOrCreateToolOfType(ToolType::PixelArtPen);
    EXPECT_FALSE(child<QWidget>("formSection")->isVisibleTo(panel.get()));
    EXPECT_FALSE(child<QWidget>("textureSection")->isVisibleTo(panel.get()));
    EXPECT_FALSE(child<QWidget>("tipShapeSection")->isVisibleTo(panel.get())) << "no section, no summary lines";
    EXPECT_FALSE(child<QWidget>("tipShapeSummary")->isVisibleTo(panel.get()));
    EXPECT_TRUE(tipShapeOpen()) << "the fold state is kept for the next tool";
    ASSERT_TRUE(tools.selectSlot(kBrush).ok());
    EXPECT_TRUE(child<QWidget>("formSection")->isVisibleTo(panel.get()));
    EXPECT_TRUE(child<QWidget>("tipShapeSection")->isVisibleTo(panel.get()));
}

// The paper switch for erasing, on at the start as the reference's, is every tool's.
TEST_F(BrushResourceRowsTest, ThePaperSwitchForErasingIsEveryTools) {
    openTipShape();
    tools.selectOrCreateToolOfType(ToolType::Eraser);
    ASSERT_EQ(tools.settings().tool, ToolType::Eraser);
    pick(child<BrushResourceList>("brushTextureList"), QStringLiteral("Weave"));
    auto* eraseLow = child<QCheckBox>("textureEraseLowCheck");
    EXPECT_TRUE(eraseLow->isVisibleTo(panel.get()));
    EXPECT_TRUE(eraseLow->isChecked()) << "on at the start";
    EXPECT_FALSE(eraseLow->toolTip().isEmpty());
    eraseLow->click();
    EXPECT_FALSE(tools.settings().textureEraseLow);
    ASSERT_TRUE(tools.selectSlot(kPencil).ok());
    EXPECT_TRUE(eraseLow->isVisibleTo(panel.get())) << "the Pencil's too, for the transparent colour";
}

TEST_F(BrushResourceRowsTest, TheCloneStampHasFormAndTexture) {
    openTipShape();
    tools.selectOrCreateToolOfType(ToolType::Clone);
    ASSERT_EQ(tools.settings().tool, ToolType::Clone);
    EXPECT_TRUE(child<QWidget>("tipShapeSection")->isVisibleTo(panel.get()));
    EXPECT_TRUE(child<QWidget>("formSection")->isVisibleTo(panel.get()));
    EXPECT_TRUE(child<QWidget>("textureSection")->isVisibleTo(panel.get()));
    pick(child<BrushResourceList>("brushFormList"), QStringLiteral("Blots One"));
    EXPECT_EQ(tools.settings().form, QStringLiteral("Blots One"));
    EXPECT_TRUE(test::saveScreenshot(panel->grab(), QStringLiteral("brush_panel_clone_stamp")));
}

TEST_F(BrushResourceRowsTest, TheSectionIsFoldedToOneLineEachAndOpensToTwoBoxes) {
    auto* section = child<QWidget>("tipShapeSection");
    EXPECT_TRUE(section->isVisibleTo(panel.get()));
    EXPECT_FALSE(tipShapeOpen()) << "folded at first";
    EXPECT_EQ(tipHeader()->text(), QStringLiteral("Tip shape && texture")) << "the & is escaped: not a mnemonic";
    EXPECT_EQ(tipHeader()->font().capitalization(), QFont::AllUppercase);

    auto* formLine = child<QLabel>("tipShapeSummary");
    auto* textureLine = child<QLabel>("textureSummary");
    EXPECT_TRUE(formLine->isVisibleTo(panel.get()));
    EXPECT_TRUE(textureLine->isVisibleTo(panel.get()));
    EXPECT_EQ(formLine->text(), BrushResourceLibrary::kSimpleCircle);
    EXPECT_EQ(textureLine->text(), BrushResourceLibrary::kNoTexture);
    EXPECT_FALSE(child<QWidget>("formSection")->isVisibleTo(panel.get()));
    EXPECT_FALSE(child<QWidget>("textureSection")->isVisibleTo(panel.get()));

    BrushSettings s = tools.settings();
    s.form = QStringLiteral("Blots One");
    s.texture = QStringLiteral("Speckle");
    tools.setSettings(s);
    EXPECT_EQ(formLine->text(), QStringLiteral("Blots One"));
    EXPECT_EQ(textureLine->text(), QStringLiteral("Speckle"));
    ASSERT_TRUE(tools.selectSlot(kPencil).ok());
    EXPECT_EQ(formLine->text(), BrushResourceLibrary::kSimpleCircle) << "another tool: its own";
    ASSERT_TRUE(tools.selectSlot(kBrush).ok());
    EXPECT_EQ(textureLine->text(), QStringLiteral("Speckle"));

    openTipShape();
    EXPECT_TRUE(tipShapeOpen());
    EXPECT_FALSE(formLine->isVisibleTo(panel.get())) << "the lines give way to the boxes";
    EXPECT_FALSE(textureLine->isVisibleTo(panel.get()));
    EXPECT_TRUE(child<QWidget>("formSection")->isVisibleTo(panel.get()));
    EXPECT_TRUE(child<QWidget>("textureSection")->isVisibleTo(panel.get()));
    EXPECT_EQ(panel->findChild<QToolButton*>(QStringLiteral("formExpander")), nullptr);
    EXPECT_EQ(panel->findChild<QToolButton*>(QStringLiteral("textureExpander")), nullptr);
    const auto titleOf = [&](const char* box) {
        for (const QLabel* l : child<QWidget>(box)->findChildren<QLabel*>())
            if (l->property("role").toString() == QStringLiteral("boxTitle"))
                return l->text();
        return QString();
    };
    EXPECT_EQ(titleOf("formSection"), QStringLiteral("Tip shape"));
    EXPECT_EQ(titleOf("textureSection"), QStringLiteral("Texture"));
    EXPECT_LT(child<QWidget>("formSection")->y(), child<QWidget>("textureSection")->y()) << "Tip shape first";
}

TEST_F(BrushResourceRowsTest, TheFoldStateIsKeptAcrossToolsAndSettingsChanges) {
    openTipShape();
    ASSERT_TRUE(tools.selectSlot(kPencil).ok());
    EXPECT_TRUE(tipShapeOpen());
    pick(child<BrushResourceList>("brushFormList"), QStringLiteral("Blots One"));
    BrushSettings s = tools.settings();
    s.size = 123;
    tools.setSettings(s);
    EXPECT_TRUE(tipShapeOpen()) << "choosing and editing do not fold it";
    EXPECT_TRUE(child<QWidget>("formSection")->isVisibleTo(panel.get()));
    openTipShape(false);
    EXPECT_FALSE(tipShapeOpen());
    ASSERT_TRUE(tools.selectSlot(kBrush).ok());
    EXPECT_FALSE(tipShapeOpen()) << "folded stays folded";
    EXPECT_FALSE(child<QWidget>("formSection")->isVisibleTo(panel.get()));
}

TEST_F(BrushResourceRowsTest, TheStrengthIsAFullRowLabelledByWhatItIs) {
    openTipShape();
    auto* forms = child<BrushResourceList>("brushFormList");
    const auto label = [&](const char* name) { return child<QLabel>(name)->text(); };
    EXPECT_EQ(label("formStrengthLabel"), QStringLiteral("Mess.")) << "a blot map, as the reference labels it";
    pick(forms, QStringLiteral("Blots One"));
    EXPECT_EQ(label("formStrengthLabel"), QStringLiteral("Mess."));
    pick(forms, QStringLiteral("Hair One"));
    EXPECT_EQ(label("formStrengthLabel"), QStringLiteral("Hair"));
    pick(forms, QStringLiteral("Tip One"));
    EXPECT_EQ(label("formStrengthLabel"), QStringLiteral("Dens."));
    EXPECT_EQ(label("textureStrengthLabel"), QStringLiteral("Intensity")) << "spelled out";

    pick(forms, QStringLiteral("Blots One"));
    QCoreApplication::processEvents();
    const auto x = [&](const char* name) { return child<QWidget>(name)->mapTo(panel.get(), QPoint(0, 0)).x(); };
    EXPECT_EQ(x("formStrengthBar"), x("formScaleBar"));
    EXPECT_EQ(child<ValueBar>("formStrengthBar")->width(), child<ValueBar>("formScaleBar")->width());
    EXPECT_GT(child<ValueBar>("formStrengthBar")->width(), 100);
    EXPECT_EQ(x("textureStrengthBar"), x("textureScaleBar"));
    EXPECT_GT(child<QWidget>("brushFormList")->width(), child<ValueBar>("formStrengthBar")->width()) << "the list spans the box";
    EXPECT_EQ(x("formStrengthLabel"), x("formScaleBar") - child<QWidget>("formStrengthLabel")->width() - 8)
        << "labels in one column, 8 px from the bars";
    EXPECT_GE(x("formInvertCheck"), x("formScaleBar"));

    pick(forms, BrushResourceLibrary::kSimpleCircle);
    EXPECT_FALSE(child<QLabel>("formStrengthLabel")->isEnabled());
}

TEST_F(BrushResourceRowsTest, TheBoxesShowAPercentWhereTheNumberIsAShare) {
    for (const char* name :
         {"formStrengthBar", "textureStrengthBar", "formScaleBar", "textureScaleBar", "bristleMinimumBar", "bristleRandomizeBar",
          "shapeAngleJitterBar", "shapeScalingBar", "shapeSizeJitterBar", "shapeSpacingBar", "shapeScatteringBar"})
        EXPECT_TRUE(child<ValueBar>(name)->text().endsWith(QLatin1Char('%')))
            << name << ": " << child<ValueBar>(name)->text().toStdString();
    EXPECT_TRUE(child<ValueBar>("shapeAngleBar")->text().endsWith(QStringLiteral("°")));
    EXPECT_EQ(child<ValueBar>("bristleRandomizeBar")->maximum(), 20);

    BrushSettings s = tools.settings();
    s.texture = QStringLiteral("Weave");
    s.textureStrength = 75;
    s.textureScale = 100;
    s.shapeAngle = -30;
    tools.setSettings(s);
    EXPECT_EQ(child<ValueBar>("textureStrengthBar")->text(), QStringLiteral("75%"));
    EXPECT_EQ(child<ValueBar>("textureScaleBar")->text(), QStringLiteral("100%"));
    EXPECT_EQ(child<ValueBar>("shapeAngleBar")->text(), QStringLiteral("-30°"));

    openTipShape();
    auto* bar = child<ValueBar>("textureStrengthBar");
    bar->editValue();
    auto* editor = bar->findChild<QLineEdit*>(QStringLiteral("valueEditor"));
    ASSERT_NE(editor, nullptr);
    editor->setText(QStringLiteral("43%"));
    QKeyEvent enter(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
    QCoreApplication::sendEvent(editor, &enter);
    EXPECT_EQ(tools.settings().textureStrength, 43);
}

TEST_F(BrushResourceRowsTest, ScreenshotsOfTheGroups) {
    const auto shoot = [&](const QString& name) {
        QCoreApplication::processEvents();
        panel->preview()->image();
        QPixmap shot(panel->size());
        shot.fill(Theme::color(Theme::Token::Surface));
        panel->render(&shot, QPoint(), QRegion(), QWidget::DrawChildren);
        EXPECT_TRUE(test::saveScreenshot(shot, name));
    };
    tools.setColor(QColor(0x1f, 0x3a, 0x8a));
    panel->resize(330, 1100);

    ASSERT_TRUE(tools.selectSlot(kBrush).ok());
    BrushSettings s = tools.settings();
    s.size = 500;
    s.minSize = 0;
    s.minDensity = 0;
    tools.setSettings(s);
    shoot(QStringLiteral("brush_panel_fur"));

    ASSERT_TRUE(tools.selectSlot(kPencil).ok());
    s = tools.settings();
    s.form = QStringLiteral("Tip One");
    s.texture = QStringLiteral("Speckle");
    s.textureStrength = 75;
    tools.setSettings(s);
    openTipShape();
    shoot(QStringLiteral("brush_panel_pencil_tip_shape_open"));
    pick(child<BrushResourceList>("brushFormList"), QStringLiteral("Hair One"));
    shoot(QStringLiteral("brush_panel_pencil_bristle_open"));
    pick(child<BrushResourceList>("brushFormList"), QStringLiteral("Blots One"));
    shoot(QStringLiteral("brush_panel_pencil_blots_open"));
    openTipShape(false);

    ASSERT_TRUE(tools.selectSlot(kMarker).ok());
    shoot(QStringLiteral("brush_panel_marker"));
    tools.selectOrCreateToolOfType(ToolType::PixelArtPen);
    shoot(QStringLiteral("brush_panel_pixel_art_pen"));
    ASSERT_TRUE(tools.selectToolOfType(ToolType::Blur));
    shoot(QStringLiteral("brush_panel_blur"));
    ASSERT_TRUE(tools.selectToolOfType(ToolType::Smudge));
    shoot(QStringLiteral("brush_panel_smudge"));
}

TEST_F(BrushResourceRowsTest, Screenshot) {
    openTipShape();
    pick(child<BrushResourceList>("brushFormList"), QStringLiteral("Blots One"));
    QCoreApplication::processEvents();
    EXPECT_TRUE(test::saveScreenshot(panel->grab(), QStringLiteral("brush_resource_rows")));
}

// The reference's own labels (feedback: "Same labels and controls"): each control's name, its choices and its buttons' tips
// read as the reference's do. Only what shows is renamed; the stored keys stay, so saved tools load as they were.
TEST_F(BrushResourceRowsTest, TheLabelsAreTheReferences) {
    openTipShape();
    QStringList shown;
    for (const QLabel* l : panel->findChildren<QLabel*>())
        shown << l->text();
    for (const QCheckBox* c : panel->findChildren<QCheckBox*>())
        shown << c->text();
    for (const QComboBox* c : panel->findChildren<QComboBox*>())
        for (int i = 0; i < c->count(); ++i)
            shown << c->itemText(i);
    QString tips;
    for (const QWidget* w : panel->findChildren<QWidget*>())
        tips += w->toolTip() + QLatin1Char('\n');
    for (const char* label : {"Paint mixing", "Hair Min", "Angle Control", "Ang. Jitter", "Size Jitter", "Scattering",
                              "Invert for Transparency", "None", "Auto", "Pen Direction"})
        EXPECT_TRUE(shown.contains(QString::fromUtf8(label))) << label;
    for (const char* old : {"Mixing", "Smallest Hair", "Rotation", "Angle Variation", "Size Variation", "Scatter", "Erase Low Places",
                            "Fixed", "Follow Stroke", "Follow Pen", "Strength", "Hair Width"})
        EXPECT_FALSE(shown.contains(QString::fromUtf8(old))) << old;
    for (const char* tip : {"Pressure changes the size", "Pressure changes the density", "Pressure changes the blending",
                            "Pressure changes the dilution", "Edge Shape 1", "Edge Shape 3", "Edge Shape 5"})
        EXPECT_TRUE(tips.contains(QString::fromUtf8(tip))) << tip;
    for (const char* old : {"Size follows pressure", "Density follows pressure", "Blending follows pressure", "Dilution follows pressure"})
        EXPECT_FALSE(tips.contains(QString::fromUtf8(old))) << old;
}

// Not a check: with DOODLEBOLD_PANEL_SHOTS naming a folder, the tool panel drawn offscreen, the Pencil with Paper Grain and
// the Pastel blot map (DOODLEBOLD_PANEL_BLOTMAP names its picture) and the Marker. Skipped otherwise.
TEST(PanelShots, ThePencilAndTheMarkerWithTheNewControls) {
    const QString out = qEnvironmentVariable("DOODLEBOLD_PANEL_SHOTS");
    if (out.isEmpty())
        GTEST_SKIP() << "DOODLEBOLD_PANEL_SHOTS not set";
    QStandardPaths::setTestModeEnabled(true);
    test::applyTheme();
    QTemporaryDir folder;
    BrushResourceLibrary library(folder.filePath(QStringLiteral("brushes")), BrushResourceLibrary::BuiltIns::Included);
    const QImage blots(qEnvironmentVariable("DOODLEBOLD_PANEL_BLOTMAP"));
    ASSERT_FALSE(blots.isNull());
    ASSERT_TRUE(
        library.add({{ResourceKind::BlotMap, QStringLiteral("Pastel_blotmap (1)"), blots.convertToFormat(QImage::Format_Grayscale8)}})
            .ok());
    ToolBox tools;
    ToolPanel panel(&tools, &library);
    panel.resize(340, 1700);
    panel.show();
    const auto open = [&] {
        for (QToolButton* header : panel.findChildren<QToolButton*>(QStringLiteral("sectionHeader")))
            if (!header->isChecked())
                header->click();
        QCoreApplication::processEvents();
    };
    ASSERT_TRUE(tools.selectSlot(SlotRef{0, 0}).ok());
    BrushSettings pencil = tools.settings();
    pencil.texture = QStringLiteral("Paper Grain");
    pencil.form = QStringLiteral("Pastel_blotmap (1)");
    tools.setSettings(pencil);
    open();
    ASSERT_TRUE(panel.grab().save(out + QStringLiteral("/panel-pencil-paper-blotmap.png")));
    ASSERT_TRUE(tools.selectSlot(SlotRef{0, 3}).ok());
    open();
    ASSERT_TRUE(panel.grab().save(out + QStringLiteral("/panel-marker.png")));
}

// Feedback on the Tip shape list: "can we add some indicator below the name to say whether its a blotmap, tip / etc?".
// Every picture in the list carries its kind, the one its file was kept or imported as, shown on a second line under its name; "Round"
// and the Import items have none. The Import items name the kinds as the rows do.
TEST_F(BrushResourceRowsTest, EveryTipPictureShowsItsKind) {
    auto* forms = child<BrushResourceList>("brushFormList");
    int pictures = 0;
    for (int i = 0; i < forms->count(); ++i) {
        const BrushResource* r = library->form(forms->itemText(i));
        const QString kind = forms->itemData(i, BrushResourceList::kKindRole).toString();
        if (!r) {
            EXPECT_TRUE(kind.isEmpty()) << forms->itemText(i).toStdString();
            continue;
        }
        ++pictures;
        EXPECT_EQ(kind, BrushResourceList::kindLabel(r->kind)) << forms->itemText(i).toStdString();
    }
    EXPECT_EQ(pictures, 4);
    EXPECT_EQ(forms->itemData(forms->findText(QStringLiteral("Blots One")), BrushResourceList::kKindRole).toString(),
              QStringLiteral("Blot map"));
    EXPECT_EQ(forms->itemData(forms->findText(QStringLiteral("Tip One")), BrushResourceList::kKindRole).toString(),
              QStringLiteral("Brush shape"));
    EXPECT_EQ(forms->itemData(forms->findText(QStringLiteral("Hair One")), BrushResourceList::kKindRole).toString(),
              QStringLiteral("Bristle"));
    const QStringList f = entries(forms);
    EXPECT_TRUE(f.contains(QStringLiteral("Import Brush Shape Picture ...")));
    EXPECT_TRUE(f.contains(QStringLiteral("Import Blot Map Picture ...")));
    EXPECT_TRUE(f.contains(QStringLiteral("Import Bristle Picture ...")));
    // A row with a kind draws a second line under the name, and is tall enough for both: painted by the list's own delegate and by a
    // plain one, the two differ below the name's line, and only there.
    const QModelIndex blots = forms->model()->index(forms->findText(QStringLiteral("Blots One")), 0);
    QStyleOptionViewItem option;
    option.initFrom(forms->view());
    option.font = forms->view()->font();
    option.widget = forms->view();
    const QSize row = forms->itemDelegate()->sizeHint(option, blots);
    const QFontMetrics nameLine(option.font);
    EXPECT_GT(row.height(), nameLine.height() * 3 / 2);
    option.rect = QRect(QPoint(0, 0), QSize(200, row.height()));
    const auto painted = [&](QAbstractItemDelegate* delegate) {
        QImage image(option.rect.size(), QImage::Format_ARGB32);
        image.fill(Qt::black);
        QPainter p(&image);
        delegate->paint(&p, option, blots);
        return image;
    };
    QStyledItemDelegate plain;
    const QImage withKind = painted(forms->itemDelegate()), without = painted(&plain);
    int belowName = 0;
    for (int y = row.height() / 2 + nameLine.height() / 4; y < row.height(); ++y)
        for (int x = 0; x < row.width(); ++x)
            belowName += withKind.pixel(x, y) != without.pixel(x, y) ? 1 : 0;
    EXPECT_GT(belowName, 20) << "the kind's line";
    // A texture is one kind: its list has no second lines.
    auto* textures = child<BrushResourceList>("brushTextureList");
    for (int i = 0; i < textures->count(); ++i)
        EXPECT_TRUE(textures->itemData(i, BrushResourceList::kKindRole).toString().isEmpty());
}

// Feedback: "The tip / blotmap scrollbar is unordered. We should order it by grouping it by type and name", and chose A to Z, brush
// shapes first, then blot maps, then bristles, as the Import items are. An imported picture goes into its kind's group.
TEST_F(BrushResourceRowsTest, TipPicturesAreGroupedByKindThenNamedAToZ) {
    QImage picture(64, 64, QImage::Format_Grayscale8);
    picture.fill(128);
    ASSERT_TRUE(library
                    ->add({{ResourceKind::BlotMap, QStringLiteral("aardvark blots"), picture},
                           {ResourceKind::Shape, QStringLiteral("Zebra Tip"), picture},
                           {ResourceKind::Bristle, QStringLiteral("Bristle A"), picture},
                           {ResourceKind::Shape, QStringLiteral("Middle Tip"), picture}})
                    .ok());
    auto* forms = child<BrushResourceList>("brushFormList");
    QStringList pictures;
    for (int i = 0; i < forms->count(); ++i)
        if (library->form(forms->itemText(i)))
            pictures << forms->itemText(i);
    EXPECT_EQ(pictures,
              (QStringList{"Middle Tip", "Tip One", "Zebra Tip", "aardvark blots", "Blots One", "Blots Two", "Bristle A", "Hair One"}));
    const QStringList f = entries(forms);
    EXPECT_EQ(f.first(), BrushResourceLibrary::kSimpleCircle);
    EXPECT_EQ(f.mid(f.size() - 3),
              (QStringList{"Import Brush Shape Picture ...", "Import Blot Map Picture ...", "Import Bristle Picture ..."}));
}

// The Tip shape list open at the Tools panel's narrowest width, with Doodlebold's own pictures: it opens at least as wide as
// its box, inside the screen, and scrolls when its rows are more than it shows.
TEST(TipShapeList, OpensAndScrollsAtTheNarrowestPanel) {
    QStandardPaths::setTestModeEnabled(true);
    QTemporaryDir settingsDir, pictures;
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDir.path());
    test::applyTheme();
    BrushResourceLibrary library(pictures.path());
    ToolBox tools;
    ToolPanel panel(&tools, &library);
    panel.resize(260, 900);
    panel.show();
    ASSERT_TRUE(tools.selectToolOfType(ToolType::Brush));
    auto* header = panel.findChild<QWidget*>(QStringLiteral("tipShapeSection"))->findChild<QToolButton*>(QStringLiteral("sectionHeader"));
    if (!header->isChecked())
        header->click();
    panel.resize(panel.minimumSizeHint().width(), 900);
    for (int i = 0; i < 3; ++i)
        QCoreApplication::processEvents();
    auto* forms = panel.findChild<BrushResourceList*>(QStringLiteral("brushFormList"));
    ASSERT_NE(forms, nullptr);
    QStringList builtIns;
    for (int i = 0; i < forms->count(); ++i)
        if (library.form(forms->itemText(i)))
            builtIns << forms->itemText(i);
    EXPECT_EQ(builtIns, (QStringList{"Chalk", "Charcoal", "Flat Nib", "Soft Edge", "Splatter", "Blots", "Fine Blots", "Mottled Blots",
                                     "Flat Bristle", "Round Bristle", "Sparse Bristle"}))
        << "grouped by kind, each A to Z";
    forms->setCurrentIndex(forms->findText(QStringLiteral("Chalk")));
    forms->showPopup();
    for (int i = 0; i < 5; ++i)
        QCoreApplication::processEvents();
    QWidget* popup = forms->view()->window();
    ASSERT_TRUE(popup->isVisible());
    EXPECT_GE(popup->width(), forms->width());
    if (const QScreen* screen = popup->screen()) {
        EXPECT_TRUE(screen->availableGeometry().contains(popup->geometry())) << "inside the screen";
    }
    const int rows = forms->count();
    int shownHeight = 0;
    for (int i = 0; i < rows; ++i)
        shownHeight += forms->view()->sizeHintForRow(i);
    if (shownHeight > forms->view()->viewport()->height()) {
        EXPECT_TRUE(forms->view()->verticalScrollBar()->maximum() > 0) << "scrolls to the rows it can't show";
    }
    EXPECT_TRUE(test::saveScreenshot(popup->grab(), QStringLiteral("tip-shape-list-open")));
    if (QScreen* screen = popup->screen(); screen && QGuiApplication::platformName() == QLatin1String("xcb")) {
        EXPECT_TRUE(test::saveScreenshot(screen->grabWindow(0), QStringLiteral("tip-shape-list-open-screen")));
    }
    forms->hidePopup();
}
