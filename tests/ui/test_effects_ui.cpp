#include "core/document/Document.h"
#include "core/effects/EffectRender.h"
#include "core/selection/SelectionOps.h"
#include "core/tools/ToolBoxStore.h"
#include "ui/DocumentTabs.h"
#include "ui/EffectController.h"
#include "ui/EffectDialog.h"
#include "ui/MainWindow.h"
#include "ui/ValueBar.h"
#include "ScreenshotSupport.h"

#include <QAction>
#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QElapsedTimer>
#include <QFile>
#include <QFormLayout>
#include <QLabel>
#include <QPainter>
#include <QProgressBar>
#include <QPushButton>
#include <QStandardPaths>
#include <QThread>

#include <gtest/gtest.h>

#include <algorithm>
#include <functional>
#include <memory>

using namespace doodlebold;

namespace {

class EffectsUiTest : public testing::Test {
protected:
    void SetUp() override {
        QStandardPaths::setTestModeEnabled(true);  // never touch the real tools.json
        test::applyTheme();
        QFile::remove(ToolBoxStore::defaultPath());
        window = std::make_unique<MainWindow>();
        window->newDocument();  // before show(): the offscreen platform cannot paint a canvas added to a visible window
        window->show();
        QCoreApplication::processEvents();
        ASSERT_TRUE(doc()->fillLayer(layer(), kColour));
    }
    void TearDown() override {
        window.reset();
        QFile::remove(ToolBoxStore::defaultPath());
    }

    Document* doc() const { return window->tabs()->currentDocument(); }
    LayerId layer() const { return doc()->currentLayerId(); }
    const TiledImage& pixels() const { return doc()->currentLayer()->pixels; }
    int steps() const { return doc()->undoStack()->count(); }
    EffectController* effects() const { return window->effects(); }
    EffectDialog* dialog() const { return effects()->dialog(); }

    void trigger(const char* id) const {
        QAction* a = window->command(QString::fromLatin1(id));
        ASSERT_NE(a, nullptr) << id;
        ASSERT_TRUE(a->isEnabled()) << id;
        a->trigger();
        QCoreApplication::processEvents();
    }
    // Runs the event loop until `done` holds, as the window would between a person's clicks.
    static bool waitUntil(const std::function<bool()>& done, int ms = 20000) {
        QElapsedTimer t;
        t.start();
        while (!done()) {
            if (t.elapsed() > ms)
                return false;
            QCoreApplication::processEvents();
            QThread::msleep(2);
        }
        return true;
    }
    void pressOk() const { dialog()->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click(); }
    void pressCancel() const { dialog()->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Cancel)->click(); }
    // The layer as the effect alone would leave it.
    QImage expected(const EffectSettings& s, const std::optional<SelectionMask>& selection = std::nullopt) const {
        EffectSource source;
        source.pixels = TiledImage(doc()->size());
        source.pixels.fill(kColour);
        source.canvas = doc()->size();
        source.selection = selection;
        return renderEffect(source, s).toImage();
    }

    static inline const QColor kColour{190, 70, 40};
    std::unique_ptr<MainWindow> window;
};

QStringList formLabels(const QDialog* d) {
    QStringList out;
    const auto* form = d->findChild<QFormLayout*>();
    for (int row = 0; row < form->rowCount(); ++row)
        if (QLayoutItem* item = form->itemAt(row, QFormLayout::LabelRole))
            if (const auto* label = qobject_cast<QLabel*>(item->widget()); label && !label->text().isEmpty())
                out << label->text();
    return out;
}

}

TEST_F(EffectsUiTest, EveryEntryWithDotsOpensItsDialog) {
    for (const EffectSpec& spec : allEffects()) {
        if (spec.sliders.empty())
            continue;
        trigger(spec.id);
        ASSERT_NE(dialog(), nullptr) << spec.id;
        EXPECT_TRUE(dialog()->isVisible());
        EXPECT_TRUE(dialog()->isModal()) << "the canvas can't change underneath it";
        EXPECT_EQ(dialog()->windowTitle(), QString::fromUtf8(spec.title));
        EXPECT_EQ(dialog()->sliderCount(), static_cast<int>(spec.sliders.size()));
        EXPECT_TRUE(dialog()->previewBox()->isChecked()) << "Preview starts on";
        EXPECT_EQ(dialog()->optionBox() != nullptr, spec.option != nullptr);
        pressCancel();
        QCoreApplication::processEvents();
        EXPECT_FALSE(effects()->isBusy()) << spec.id;
    }
}

TEST_F(EffectsUiTest, TheDialogsOfferTheScreenshotsSliders) {
    trigger("effects.hueSaturation");
    EXPECT_EQ(formLabels(dialog()), (QStringList{"Hue :", "Saturation :", "Lightness :"}));
    EXPECT_EQ(dialog()->optionBox()->text(), QStringLiteral("Colorize"));
    EXPECT_EQ(dialog()->slider(0)->text(), QStringLiteral("0°"));
    EXPECT_TRUE(test::saveScreenshot(dialog()->grab(), QStringLiteral("effects-hue-saturation")));
    pressCancel();
    trigger("effects.levels");
    EXPECT_EQ(formLabels(dialog()), (QStringList{"Input Black :", "Input White :", "Gamma :", "Output Black :", "Output White :"}));
    EXPECT_EQ(dialog()->slider(1)->value(), 255);
    EXPECT_EQ(dialog()->slider(2)->text(), QStringLiteral("1.00"));
    EXPECT_TRUE(test::saveScreenshot(dialog()->grab(), QStringLiteral("effects-levels")));
    pressCancel();
}

// The reference's dialog: Mode first, Linear to start; Angle only for Linear, the Center only for Radial, in canvas
// pixels from 0 to the canvas's width and height, starting at its centre. Each change shows on the layer.
TEST_F(EffectsUiTest, ChromaticAberrationOffersLinearAndRadialWithTheirOwnControls) {
    trigger("effects.chromaticAberration");
    EXPECT_EQ(formLabels(dialog()), (QStringList{"Mode :", "Amount :", "Angle :", "Center X :", "Center Y :"}));
    QComboBox* mode = dialog()->modeBox();
    ASSERT_NE(mode, nullptr);
    EXPECT_EQ(mode->currentText(), QStringLiteral("Linear"));
    EXPECT_EQ(mode->count(), 2);
    EXPECT_EQ(dialog()->slider(chromatic::kAmount)->text(), QStringLiteral("4 px"));
    EXPECT_EQ(dialog()->slider(chromatic::kAmount)->maximum(), 100);
    const QSize canvas = doc()->size();
    EXPECT_EQ(dialog()->slider(chromatic::kCenterX)->maximum(), canvas.width());
    EXPECT_EQ(dialog()->slider(chromatic::kCenterY)->maximum(), canvas.height());
    EXPECT_EQ(dialog()->slider(chromatic::kCenterX)->value(), std::round(canvas.width() / 2.0));
    EXPECT_EQ(dialog()->slider(chromatic::kCenterY)->value(), std::round(canvas.height() / 2.0));
    EXPECT_TRUE(dialog()->slider(chromatic::kAngle)->isEnabled());
    EXPECT_FALSE(dialog()->slider(chromatic::kCenterX)->isEnabled());
    EXPECT_FALSE(dialog()->slider(chromatic::kCenterY)->isEnabled());
    EXPECT_TRUE(test::saveScreenshot(dialog()->grab(), QStringLiteral("effects-chromatic-linear")));

    EffectSettings want = dialog()->settings();
    ASSERT_TRUE(waitUntil([&] { return pixels().toImage() == expected(want); })) << "Linear at 4 px shows at once";
    mode->setCurrentIndex(chromatic::kRadial);
    dialog()->setValue(chromatic::kAmount, 30);
    dialog()->setValue(chromatic::kCenterX, 10);
    EXPECT_FALSE(dialog()->slider(chromatic::kAngle)->isEnabled());
    EXPECT_TRUE(dialog()->slider(chromatic::kCenterX)->isEnabled());
    EXPECT_TRUE(dialog()->slider(chromatic::kCenterY)->isEnabled());
    want = dialog()->settings();
    EXPECT_EQ(want.mode, chromatic::kRadial);
    EXPECT_EQ(want.value(chromatic::kCenterX), 10);
    ASSERT_TRUE(waitUntil([&] { return pixels().toImage() == expected(want); })) << "Radial about the moved centre";
    EXPECT_TRUE(test::saveScreenshot(dialog()->grab(), QStringLiteral("effects-chromatic-radial")));
    pressOk();
    ASSERT_TRUE(waitUntil([&] { return !effects()->isBusy(); }));
    EXPECT_EQ(pixels().toImage(), expected(want));
}

TEST_F(EffectsUiTest, TheSlidersShowOnTheLayerAndCancelPutsItBack) {
    const QImage before = pixels().toImage();
    const int start = steps();
    trigger("effects.hueSaturation");
    dialog()->setValue(0, 120);
    EffectSettings s = EffectSettings::start(EffectKind::HueSaturation);
    s.values[0] = 120;
    const QImage turned = expected(s);
    ASSERT_TRUE(waitUntil([&] { return pixels().toImage() == turned; })) << "the canvas shows it while the dialog is open";
    dialog()->previewBox()->setChecked(false);
    EXPECT_EQ(pixels().toImage(), before) << "Preview off";
    dialog()->previewBox()->setChecked(true);
    ASSERT_TRUE(waitUntil([&] { return pixels().toImage() == turned; }));
    pressCancel();
    QCoreApplication::processEvents();
    EXPECT_EQ(pixels().toImage(), before);
    EXPECT_EQ(steps(), start);
    EXPECT_EQ(dialog(), nullptr);
}

TEST_F(EffectsUiTest, OkKeepsTheLastValuesAsOneUndoStep) {
    const QImage before = pixels().toImage();
    const int start = steps();
    trigger("effects.levels");
    for (const double v : {200.0, 30.0, 90.0})
        dialog()->setValue(0, v);
    pressOk();
    ASSERT_TRUE(waitUntil([&] { return !effects()->isBusy(); }));
    EffectSettings s = EffectSettings::start(EffectKind::Levels);
    s.values[0] = 90;
    EXPECT_EQ(pixels().toImage(), expected(s));
    EXPECT_EQ(steps(), start + 1);
    EXPECT_EQ(doc()->undoStack()->undoText(), QStringLiteral("Levels"));
    window->command(QStringLiteral("edit.undo"))->trigger();
    EXPECT_EQ(pixels().toImage(), before);
}

TEST_F(EffectsUiTest, OkWithPreviewOffStillApplies) {
    trigger("effects.posterize");
    dialog()->previewBox()->setChecked(false);
    dialog()->setValue(0, 2);
    const QImage before = pixels().toImage();
    QCoreApplication::processEvents();
    EXPECT_EQ(pixels().toImage(), before);
    pressOk();
    ASSERT_TRUE(waitUntil([&] { return !effects()->isBusy(); }));
    EffectSettings s = EffectSettings::start(EffectKind::Posterize);
    s.values[0] = 2;
    EXPECT_EQ(pixels().toImage(), expected(s));
}

TEST_F(EffectsUiTest, InvertAndDesaturateApplyAtOnce) {
    const int start = steps();
    trigger("effects.invert");
    ASSERT_TRUE(waitUntil([&] { return !effects()->isBusy(); }));
    EXPECT_EQ(dialog(), nullptr) << "no dialog";
    EXPECT_EQ(pixels().toImage(), expected(EffectSettings::start(EffectKind::Invert)));
    EXPECT_EQ(steps(), start + 1);
    trigger("effects.desaturate");
    ASSERT_TRUE(waitUntil([&] { return !effects()->isBusy(); }));
    EXPECT_EQ(steps(), start + 2);
    const auto p = pixels().pixel(10, 10);
    EXPECT_EQ(p[0], p[1]);
    EXPECT_EQ(p[1], p[2]);
}

TEST_F(EffectsUiTest, OnlyTheSelectionChanges) {
    ASSERT_TRUE(doc()->setSelection(selection::marquee(doc()->size(), QPoint(100, 100), QPoint(299, 199))));
    const auto outside = pixels().pixel(50, 50);
    trigger("effects.invert");
    ASSERT_TRUE(waitUntil([&] { return !effects()->isBusy(); }));
    EXPECT_EQ(pixels().pixel(50, 50), outside);
    EXPECT_NE(pixels().pixel(150, 150), outside);
    EXPECT_EQ(pixels().toImage(), expected(EffectSettings::start(EffectKind::Invert), doc()->selection()));
}

TEST_F(EffectsUiTest, ALockedLayerIsRefusedWithoutADialog) {
    LayerProps p = doc()->currentLayer()->props;
    p.locked = true;
    ASSERT_TRUE(doc()->setLayerProps(layer(), p));
    trigger("effects.gaussianBlur");
    EXPECT_EQ(dialog(), nullptr);
    EXPECT_FALSE(effects()->isBusy());
}

// OK on a large layer (the whole 2000 × 1500 canvas): the click returns at once, the window keeps handling events while the blur is worked out, the dialog
// shows how far it is, and the result is one undo step.
TEST_F(EffectsUiTest, ALargeLayerAppliesWithoutBlockingTheWindow) {
    trigger("effects.motionBlur");
    dialog()->previewBox()->setChecked(false);
    dialog()->setValue(1, 200);  // every pixel the mean of 201 along a line: seconds of work on the whole canvas
    const int start = steps();
    QElapsedTimer click;
    click.start();
    pressOk();
    EXPECT_LT(click.elapsed(), 200) << "OK returns while the work goes on";
    ASSERT_TRUE(effects()->isBusy());
    EXPECT_TRUE(dialog()->isApplying()) << "the dialog shows how far it is";
    EXPECT_TRUE(dialog()->findChild<QProgressBar*>()->isVisible());
    qint64 longestWait = 0;
    QElapsedTimer between;
    between.start();
    ASSERT_TRUE(waitUntil(
        [&] {
            longestWait = std::max(longestWait, between.restart());
            return !effects()->isBusy();
        },
        120000));
    EXPECT_LT(longestWait, 500) << "the event loop never waits long";
    EXPECT_EQ(steps(), start + 1);
    EXPECT_TRUE(waitUntil([&] { return dialog() == nullptr; })) << "and the dialog closes";
}

// Each effect on a sample picture, written out with the other screenshots for looking at by hand.
TEST(EffectScreenshots, EachEffectOnASamplePicture) {
    const QSize size(320, 200);
    QImage sample(size, QImage::Format_ARGB32_Premultiplied);
    sample.fill(Qt::transparent);
    {
        QPainter p(&sample);
        QLinearGradient sky(0, 0, 320, 0);
        sky.setColorAt(0, QColor(40, 90, 200));
        sky.setColorAt(1, QColor(250, 200, 80));
        p.fillRect(QRect(0, 0, 320, 140), sky);
        p.setRenderHint(QPainter::Antialiasing);
        p.setBrush(QColor(220, 40, 60));
        p.setPen(QPen(Qt::black, 3));
        p.drawEllipse(QPointF(110, 90), 50, 50);
        p.setBrush(Qt::white);
        p.drawRect(QRectF(200, 60, 70, 100));
    }
    EffectSource source;
    source.pixels = TiledImage(size);
    source.pixels.writeImage(sample, QPoint(0, 0));
    source.canvas = size;
    const std::pair<EffectKind, std::vector<double>> cases[] = {
        {EffectKind::BrightnessContrast, {20, 40}},
        {EffectKind::HueSaturation, {90, 30, 0}},
        {EffectKind::Levels, {30, 220, 1.6, 0, 255}},
        {EffectKind::ColorBalance, {40, 0, -40}},
        {EffectKind::Invert, {}},
        {EffectKind::Desaturate, {}},
        {EffectKind::Posterize, {4}},
        {EffectKind::GaussianBlur, {6}},
        {EffectKind::MotionBlur, {30, 40}},
        {EffectKind::RadialBlur, {20, 50, 50}},
        {EffectKind::UnsharpMask, {250, 3, 0}},
        {EffectKind::AddNoise, {20}},
        {EffectKind::ChromaticAberration, {8}},
    };
    EXPECT_TRUE(test::saveScreenshot(QPixmap::fromImage(sample), QStringLiteral("effect-sample")));
    for (const auto& [kind, values] : cases) {
        EffectSettings s = EffectSettings::start(kind);
        std::ranges::copy(values, s.values.begin());
        const QImage out = renderEffect(source, s).toImage();
        EXPECT_NE(out, source.pixels.toImage()) << effectSpec(kind).title;
        EXPECT_TRUE(
            test::saveScreenshot(QPixmap::fromImage(out), QStringLiteral("effect-") + QString::fromLatin1(effectSpec(kind).id + 8)));
    }
}
