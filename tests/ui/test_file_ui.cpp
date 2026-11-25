#include "core/options/AppOptions.h"
#include "core/document/Document.h"
#include "core/io/FileIo.h"
#include "core/io/FormatRegistry.h"
#include "core/tools/ToolBoxStore.h"
#include "ui/CanvasView.h"
#include "ui/DocumentTabs.h"
#include "ui/FileController.h"
#include "ui/JpegQualityDialog.h"
#include "ui/NewCanvasDialog.h"
#include "ui/MainWindow.h"
#include "ScreenshotSupport.h"

#include <QAction>
#include <QCheckBox>
#include <QComboBox>
#include <QElapsedTimer>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QListWidget>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QProgressBar>
#include <QPushButton>
#include <QRadioButton>
#include <QScrollBar>
#include <QToolButton>
#include <QDropEvent>
#include <QFile>
#include <QImage>
#include <QMenuBar>
#include <QMimeData>
#include "core/util/AppIdentity.h"
#include <QSettings>
#include <QSpinBox>
#include <QStandardPaths>
#include <QStatusBar>
#include <QTemporaryDir>
#include <QUndoStack>
#include <QUrl>

#include <gtest/gtest.h>

#include <memory>
#include <set>

using namespace doodlebold;

namespace {

class ScriptedFiles : public FileController {
public:
    using FileController::FileController;

    std::optional<NewCanvasAnswer> newAnswer;
    std::optional<NewCanvasRequest> lastNewRequest;
    int newAsked = 0;
    QStringList openAnswer;
    QString saveAnswer;
    QString saveFormat;
    QString exportAnswer;
    std::optional<ExportOptions> optionsAnswer{ExportOptions{}};
    int saveAsked = 0, optionsAsked = 0, exportAsked = 0;
    QStringList errors;
    QStringList notes;
    std::optional<ExportOptions> lastOptionsShown;

protected:
    std::optional<NewCanvasAnswer> askNewCanvas(const NewCanvasRequest& request) override {
        ++newAsked;
        lastNewRequest = request;
        return newAnswer;
    }
    QStringList askOpenPaths() override { return openAnswer; }
    QString askSavePath(const Document*, const DocumentFormat** chosen) override {
        ++saveAsked;
        *chosen = FormatRegistry::instance().byId(saveFormat);
        return saveAnswer;
    }
    QString askExportPath(const QString&, const DocumentFormat&) override {
        ++exportAsked;
        return exportAnswer;
    }
    std::optional<ExportOptions> askExportOptions(const DocumentFormat&, const DocumentContent&, const ExportOptions& initial) override {
        ++optionsAsked;
        lastOptionsShown = initial;
        return optionsAnswer;
    }
    void showError(const QString&, const QString& text) override { errors << text; }
    void showNotes(const QString&, const QStringList& n) override { notes << n; }
    bool askScaleDown(const QString&, const QString& what, QSize size) override {
        scaleDownAsked << QStringLiteral("%1 %2x%3").arg(what).arg(size.width()).arg(size.height());
        return scaleDownAnswer;
    }

public:
    bool scaleDownAnswer = true;
    QStringList scaleDownAsked;
};

class TestTabs : public DocumentTabs {
public:
    using DocumentTabs::DocumentTabs;
    UnsavedChoice choice = UnsavedChoice::Cancel;
    int asked = 0;

protected:
    UnsavedChoice askUnsaved(const Document*) override {
        ++asked;
        return choice;
    }
};

class FileUiTest : public testing::Test {
protected:
    void SetUp() override {
        QStandardPaths::setTestModeEnabled(true);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDir.path());
        files = std::make_unique<ScriptedFiles>(&tabs, FormatRegistry::instance(), nullptr);
        QObject::connect(files.get(), &FileController::documentCreated, &tabs, [this](Document* d, bool) { tabs.addDocument(d); });
        QObject::connect(files.get(), &FileController::message, &tabs, [this](const QString& m) { messages << m; });
        tabs.setSaveHandler([this](Document* d) { return files->save(d); });
    }

    QString path(const char* name) const { return dir.filePath(QString::fromLatin1(name)); }
    Document* doc() const { return tabs.currentDocument(); }
    Document* paintedDocument(const QSize& size = QSize(90, 70)) {
        auto* d = new Document(size, QColor(240, 230, 220));
        tabs.addDocument(d);
        (void)d->fillLayer(d->currentLayerId(), QColor(10, 120, 200));
        return d;
    }

    QTemporaryDir dir, settingsDir;
    TestTabs tabs{nullptr};
    std::unique_ptr<ScriptedFiles> files;
    QStringList messages;
};

QImage solid(const QSize& size, const QColor& c) {
    QImage img(size, QImage::Format_RGBA8888);
    img.fill(c);
    return img;
}

}

namespace {

void type(QLineEdit* edit, const QString& text) {
    edit->setText(text);
    emit edit->textEdited(text);
}

void choose(QComboBox* combo, int index) {
    combo->setCurrentIndex(index);
    emit combo->activated(index);
}

QStringList itemTexts(const QComboBox* combo) {
    QStringList out;
    for (int i = 0; i < combo->count(); ++i)
        out << combo->itemText(i);
    return out;
}

const QString kTimes = QStringLiteral(" × ");

QLabel* labelNamed(const QWidget* w, const char* name) {
    return w->findChild<QLabel*>(QString::fromLatin1(name));
}

void waitUntilIdle(const JpegQualityDialog& dlg) {
    QElapsedTimer timer;
    timer.start();
    while (!dlg.isIdle() && timer.elapsed() < 10000)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    ASSERT_TRUE(dlg.isIdle());
}

QImage detailedCanvas(const QSize& size = QSize(256, 256)) {
    QImage img(size, QImage::Format_RGB888);
    img.fill(Qt::white);
    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QPen(Qt::red, 18, Qt::SolidLine, Qt::RoundCap));
    p.drawLine(30, 40, 226, 226);
    p.setPen(QPen(Qt::blue, 18, Qt::SolidLine, Qt::RoundCap));
    p.drawLine(226, 30, 30, 226);
    return img;
}

}

TEST(NewCanvasDialogTest, HasTheExpectedFieldsLabelsAndLists) {
    NewCanvasDialog dlg(QStringLiteral("NewCanvas1"), NewCanvasValues{}, defaultCanvasPresets());
    EXPECT_EQ(dlg.windowTitle(), QStringLiteral("New Canvas"));
    EXPECT_EQ(dlg.fileNameEdit()->text(), QStringLiteral("NewCanvas1"));
    QStringList labels;
    for (const QLabel* l : dlg.findChildren<QLabel*>())
        labels << l->text();
    for (const char* wanted :
         {"File Name :", "Preset :", "Width :", "Height :", "Printing Resolution :", "Background :", "Pixels :", "Printing Metrics :"})
        EXPECT_TRUE(labels.contains(QString::fromLatin1(wanted))) << wanted;

    EXPECT_EQ(dlg.presetCombo()->currentText(), QStringLiteral("----"));
    EXPECT_EQ(itemTexts(dlg.fields()->sizeUnitCombo()), (QStringList{"pixels", "inches", "cm", "mm"}));
    EXPECT_EQ(itemTexts(dlg.fields()->resolutionUnitCombo()), (QStringList{"pixels/inch", "pixels/cm"}));
    const QComboBox* bg = dlg.fields()->backgroundCombo();
    ASSERT_EQ(bg->count(), 9);
    EXPECT_EQ(itemTexts(bg), (QStringList{"White", "Black", "Color", "", "Transparent (Light Checker)", "Transparent (Dark Checker)",
                                          "Transparent (White)", "Transparent (Black)", "Transparent (Color)"}));
    EXPECT_EQ(bg->currentText(), QStringLiteral("White"));
    EXPECT_TRUE(dlg.fields()->swatch()->isEnabled());
    EXPECT_FALSE(dlg.configureButton()->toolTip().isEmpty());
    EXPECT_EQ(dlg.presetCombo()->count(), 9);
    EXPECT_EQ(dlg.presetCombo()->itemText(1), QString::fromUtf8("512 × 512 - 72ppi"));
    EXPECT_EQ(dlg.presetCombo()->itemText(8), QStringLiteral("A4 - 350ppi"));
    auto* view = qobject_cast<QListView*>(dlg.presetCombo()->view());
    ASSERT_TRUE(view);
    EXPECT_TRUE(view->isRowHidden(0));
}

// A large canvas shows the dialog working until it's made; a small one closes at once, with no sign of it.
TEST(NewCanvasDialogTest, ShowsItIsWorkingOnlyForALargeCanvas) {
    NewCanvasValues v;
    v.pixels = QSize(16000, 16000);
    NewCanvasDialog large(QStringLiteral("NewCanvas1"), v, defaultCanvasPresets());
    std::function<void()> finish;
    large.createWhileShowing = [&finish](const NewCanvasAnswer&, std::function<void()> done) { finish = std::move(done); };
    large.show();
    large.accept();
    QCoreApplication::processEvents();
    const auto* bar = large.findChild<QProgressBar*>(QStringLiteral("newCanvasWorking"));
    ASSERT_NE(bar, nullptr);
    EXPECT_TRUE(bar->isVisible());
    ASSERT_TRUE(finish);
    EXPECT_EQ(large.result(), 0);  // still up, and pressing Cancel does nothing while it works
    large.reject();
    EXPECT_TRUE(large.isVisible());
    finish();
    EXPECT_EQ(large.result(), QDialog::Accepted);

    v.pixels = QSize(2000, 2000);
    NewCanvasDialog small(QStringLiteral("NewCanvas2"), v, defaultCanvasPresets());
    bool asked = false;
    small.createWhileShowing = [&asked](const NewCanvasAnswer&, const std::function<void()>&) { asked = true; };
    small.show();
    small.accept();
    QCoreApplication::processEvents();
    EXPECT_EQ(small.findChild<QProgressBar*>(QStringLiteral("newCanvasWorking")), nullptr);
    EXPECT_FALSE(asked);
    EXPECT_EQ(small.result(), QDialog::Accepted);
}

TEST(NewCanvasDialogTest, ShowsTheValuesItWasGivenAndItsReadouts) {
    NewCanvasValues v;
    v.pixels = QSize(128, 96);
    NewCanvasDialog dlg(QStringLiteral("NewCanvas1"), v, defaultCanvasPresets());
    EXPECT_EQ(dlg.fields()->widthEdit()->text(), QStringLiteral("128"));
    EXPECT_EQ(dlg.fields()->heightEdit()->text(), QStringLiteral("96"));
    EXPECT_EQ(dlg.fields()->resolutionEdit()->text(), QStringLiteral("72"));
    EXPECT_EQ(labelNamed(&dlg, "pixelsLabel")->text(), QStringLiteral("128") + kTimes + QStringLiteral("96"));
    EXPECT_EQ(labelNamed(&dlg, "metricsLabel")->text(), QString::fromUtf8("45.2mm × 33.9mm (72 pixels/inch)"));
    EXPECT_EQ(dlg.answer().values, v);
    EXPECT_EQ(dlg.answer().fileName, QStringLiteral("NewCanvas1"));
}

TEST(NewCanvasDialogTest, ChoosingAPresetFillsTheFieldsAndEditingResetsTheCombo) {
    NewCanvasDialog dlg(QStringLiteral("NewCanvas1"), NewCanvasValues{}, defaultCanvasPresets());
    choose(dlg.presetCombo(), 8);
    EXPECT_EQ(dlg.presetCombo()->currentText(), QStringLiteral("A4 - 350ppi"));
    EXPECT_EQ(dlg.fields()->widthEdit()->text(), QStringLiteral("210"));
    EXPECT_EQ(dlg.fields()->heightEdit()->text(), QStringLiteral("297"));
    EXPECT_EQ(dlg.fields()->sizeUnitCombo()->currentText(), QStringLiteral("mm"));
    EXPECT_EQ(dlg.fields()->resolutionEdit()->text(), QStringLiteral("350"));
    EXPECT_EQ(labelNamed(&dlg, "pixelsLabel")->text(), QStringLiteral("2894") + kTimes + QStringLiteral("4093"));
    EXPECT_EQ(labelNamed(&dlg, "metricsLabel")->text(), QString::fromUtf8("210mm × 297mm (350 pixels/inch)"));
    EXPECT_EQ(dlg.answer().values.pixels, QSize(2894, 4093));
    EXPECT_EQ(dlg.fields()->backgroundCombo()->currentText(), QStringLiteral("White"));

    type(dlg.fields()->widthEdit(), QStringLiteral("100"));
    EXPECT_EQ(dlg.presetCombo()->currentIndex(), 0);
    EXPECT_EQ(dlg.presetCombo()->currentText(), QStringLiteral("----"));
    EXPECT_EQ(labelNamed(&dlg, "pixelsLabel")->text(), QStringLiteral("1378") + kTimes + QStringLiteral("4093"));

    choose(dlg.presetCombo(), 3);
    EXPECT_EQ(dlg.presetCombo()->currentIndex(), 0);
    EXPECT_EQ(labelNamed(&dlg, "pixelsLabel")->text(), QStringLiteral("1378") + kTimes + QStringLiteral("4093"));

    choose(dlg.presetCombo(), 8);
    dlg.fields()->sizeUnitCombo()->setCurrentIndex(0);
    EXPECT_EQ(dlg.presetCombo()->currentIndex(), 0);
    EXPECT_EQ(dlg.fields()->widthEdit()->text(), QStringLiteral("2894"));
}

TEST(NewCanvasDialogTest, UnitsAndResolutionConvert) {
    NewCanvasDialog dlg(QStringLiteral("NewCanvas1"), NewCanvasValues{}, defaultCanvasPresets());
    choose(dlg.presetCombo(), 8);
    dlg.fields()->sizeUnitCombo()->setCurrentIndex(0);
    type(dlg.fields()->widthEdit(), QStringLiteral("1000"));
    type(dlg.fields()->heightEdit(), QStringLiteral("500"));
    type(dlg.fields()->resolutionEdit(), QStringLiteral("300"));
    EXPECT_EQ(labelNamed(&dlg, "pixelsLabel")->text(), QStringLiteral("1000") + kTimes + QStringLiteral("500"));
    EXPECT_EQ(labelNamed(&dlg, "metricsLabel")->text(), QString::fromUtf8("84.7mm × 42.3mm (300 pixels/inch)"));
    dlg.fields()->sizeUnitCombo()->setCurrentIndex(1);
    EXPECT_EQ(dlg.fields()->widthEdit()->text(), QStringLiteral("3.333"));
    EXPECT_EQ(dlg.fields()->heightEdit()->text(), QStringLiteral("1.667"));
    dlg.fields()->sizeUnitCombo()->setCurrentIndex(2);
    EXPECT_EQ(dlg.fields()->widthEdit()->text(), QStringLiteral("8.47"));
    dlg.fields()->sizeUnitCombo()->setCurrentIndex(3);
    EXPECT_EQ(dlg.fields()->widthEdit()->text(), QStringLiteral("84.7"));
    type(dlg.fields()->widthEdit(), QStringLiteral("100"));
    type(dlg.fields()->heightEdit(), QStringLiteral("50.5"));
    EXPECT_EQ(labelNamed(&dlg, "pixelsLabel")->text(), QStringLiteral("1181") + kTimes + QStringLiteral("596"));
    EXPECT_EQ(labelNamed(&dlg, "metricsLabel")->text(), QString::fromUtf8("100mm × 50.5mm (300 pixels/inch)"));
    dlg.fields()->sizeUnitCombo()->setCurrentIndex(0);
    EXPECT_EQ(dlg.fields()->widthEdit()->text(), QStringLiteral("1181"));
    EXPECT_EQ(dlg.fields()->heightEdit()->text(), QStringLiteral("596"));

    choose(dlg.presetCombo(), 8);
    dlg.fields()->resolutionUnitCombo()->setCurrentIndex(1);
    EXPECT_EQ(dlg.fields()->resolutionEdit()->text(), QStringLiteral("137.8"));
    EXPECT_EQ(labelNamed(&dlg, "pixelsLabel")->text(), QStringLiteral("2894") + kTimes + QStringLiteral("4093"));
    EXPECT_EQ(labelNamed(&dlg, "metricsLabel")->text(), QString::fromUtf8("210mm × 297mm (137.8 pixels/cm)"));
}

TEST(NewCanvasDialogTest, BackgroundComboAndSwatchSetTheAnswer) {
    NewCanvasDialog dlg(QStringLiteral("NewCanvas1"), NewCanvasValues{}, defaultCanvasPresets());
    dlg.fields()->backgroundCombo()->setCurrentIndex(8);
    EXPECT_EQ(dlg.answer().values.background.mode, BackgroundMode::TransparencyColor);
    dlg.fields()->backgroundCombo()->setCurrentIndex(1);
    EXPECT_EQ(dlg.answer().values.background.mode, BackgroundMode::Black);
    int asked = 0;
    dlg.fields()->pickColor = [&](const QColor& current) -> std::optional<QColor> {
        ++asked;
        EXPECT_EQ(current, QColor(128, 128, 128));
        return QColor(255, 128, 0);
    };
    dlg.fields()->swatch()->click();
    EXPECT_EQ(asked, 1);
    EXPECT_EQ(dlg.answer().values.background, (CanvasBackground{BackgroundMode::Color, QColor(255, 128, 0)}))
        << "a colour picked over Black would be hidden, so it's Color";
    EXPECT_EQ(dlg.fields()->backgroundCombo()->currentData().toInt(), static_cast<int>(BackgroundMode::Color));
    dlg.fields()->pickColor = [](const QColor&) -> std::optional<QColor> { return std::nullopt; };
    dlg.fields()->backgroundCombo()->setCurrentIndex(1);
    dlg.fields()->swatch()->click();
    EXPECT_EQ(dlg.answer().values.background, (CanvasBackground{BackgroundMode::Black, QColor(255, 128, 0)})) << "Cancel changes nothing";
}

TEST(NewCanvasDialogTest, AColourPickedOverATransparencyStaysTransparent) {
    NewCanvasDialog dlg(QStringLiteral("NewCanvas1"), NewCanvasValues{}, defaultCanvasPresets());
    dlg.fields()->backgroundCombo()->setCurrentIndex(dlg.fields()->backgroundCombo()->findData(
        static_cast<int>(BackgroundMode::TransparencyBrightChecker)));
    dlg.fields()->pickColor = [](const QColor&) -> std::optional<QColor> { return QColor(10, 200, 30); };
    dlg.fields()->swatch()->click();
    EXPECT_EQ(dlg.answer().values.background, (CanvasBackground{BackgroundMode::TransparencyColor, QColor(10, 200, 30)}));
    EXPECT_EQ(dlg.fields()->backgroundCombo()->currentData().toInt(), static_cast<int>(BackgroundMode::TransparencyColor));
}

TEST(NewCanvasDialogTest, TheSwatchShowsWhatTheCanvasWillShow) {
    NewCanvasDialog dlg(QStringLiteral("NewCanvas1"), NewCanvasValues{}, defaultCanvasPresets());
    QComboBox* combo = dlg.fields()->backgroundCombo();
    const auto colours = [&](BackgroundMode mode) {
        combo->setCurrentIndex(combo->findData(static_cast<int>(mode)));
        const QImage icon = dlg.fields()->swatch()->icon().pixmap(dlg.fields()->swatch()->iconSize()).toImage();
        std::set<QRgb> seen;
        for (int y = 0; y < icon.height(); ++y)
            for (int x = 0; x < icon.width(); ++x)
                seen.insert(icon.pixel(x, y) | 0xff000000u);
        return seen;
    };
    EXPECT_EQ(colours(BackgroundMode::Black), (std::set<QRgb>{qRgb(0, 0, 0)}));
    EXPECT_EQ(colours(BackgroundMode::Color), (std::set<QRgb>{qRgb(128, 128, 128)}));
    EXPECT_EQ(colours(BackgroundMode::TransparencyBrightChecker).size(), 2u) << "its checkerboard";
    const std::set<QRgb> overWhite = colours(BackgroundMode::TransparencyWhite);
    EXPECT_TRUE(overWhite.contains(qRgb(255, 255, 255)));
    EXPECT_GE(overWhite.size(), 3u) << "the checkerboard beside the white, unlike opaque White";
}

TEST(NewCanvasDialogTest, PlusRegistersAPresetAndTheGearEditsTheList) {
    NewCanvasValues v;
    v.pixels = QSize(1000, 700);
    NewCanvasDialog dlg(QStringLiteral("NewCanvas1"), v, defaultCanvasPresets());
    std::vector<CanvasPresetList> announced;
    QObject::connect(&dlg, &NewCanvasDialog::presetsChanged, &dlg, [&](const CanvasPresetList& p) { announced.push_back(p); });

    CanvasPreset initial;
    dlg.registerPreset = [&](const CanvasPreset& start) -> std::optional<CanvasPreset> {
        initial = start;
        CanvasPreset made = start;
        made.name = QStringLiteral("Wide");
        return made;
    };
    dlg.registerButton()->click();
    EXPECT_EQ(initial.width, 1000);
    EXPECT_EQ(initial.height, 700);
    EXPECT_EQ(initial.sizeUnit, SizeUnit::Pixels);
    EXPECT_EQ(initial.resolution, 72);
    ASSERT_EQ(dlg.presets().size(), 9u);
    EXPECT_EQ(dlg.presets().back().name, QStringLiteral("Wide"));
    ASSERT_EQ(announced.size(), 1u);
    EXPECT_EQ(announced[0], dlg.presets());
    EXPECT_EQ(dlg.presetCombo()->currentText(), QStringLiteral("Wide"));

    dlg.registerPreset = [](const CanvasPreset&) -> std::optional<CanvasPreset> { return std::nullopt; };
    dlg.registerButton()->click();
    EXPECT_EQ(dlg.presets().size(), 9u);
    EXPECT_EQ(announced.size(), 1u);

    dlg.configurePresets = [](const CanvasPresetList& current, const NewCanvasValues&) -> std::optional<CanvasPresetList> {
        CanvasPresetList edited = current;
        edited.erase(edited.begin());
        return edited;
    };
    dlg.configureButton()->click();
    EXPECT_EQ(dlg.presets().size(), 8u);
    EXPECT_EQ(announced.size(), 2u);
    EXPECT_EQ(dlg.presetCombo()->currentIndex(), 0);
    EXPECT_EQ(dlg.presetCombo()->itemText(1), QStringLiteral("1024") + kTimes + QStringLiteral("1024 - 72ppi"));
}

TEST(NewCanvasDialogTest, AnEmptyFileNameFallsBackToTheOffered) {
    NewCanvasDialog dlg(QStringLiteral("NewCanvas4"), NewCanvasValues{}, defaultCanvasPresets());
    dlg.fileNameEdit()->setText(QStringLiteral("  "));
    EXPECT_EQ(dlg.answer().fileName, QStringLiteral("NewCanvas4"));
    dlg.fileNameEdit()->setText(QStringLiteral(" Sketch "));
    EXPECT_EQ(dlg.answer().fileName, QStringLiteral("Sketch"));
}

TEST(NewCanvasDialogTest, SizeIsLimitedToWhatDoodleboldOpens) {
    NewCanvasDialog dlg(QStringLiteral("NewCanvas1"), NewCanvasValues{}, defaultCanvasPresets());
    EXPECT_TRUE(dlg.fields()->limitLabel()->isHidden());
    type(dlg.fields()->widthEdit(), QStringLiteral("99999"));
    EXPECT_EQ(dlg.answer().values.pixels.width(), kMaxCanvasSide);
    EXPECT_FALSE(dlg.fields()->limitLabel()->isHidden()) << "it says so plainly";
    EXPECT_EQ(dlg.fields()->limitLabel()->text(), QStringLiteral("Canvases are at most 16384 × 16384 pixels."));
    emit dlg.fields()->widthEdit()->editingFinished();
    EXPECT_EQ(dlg.fields()->widthEdit()->text(), QString::number(kMaxCanvasSide));
    EXPECT_FALSE(dlg.fields()->limitLabel()->isHidden()) << "still, beside the size it was limited to";
    type(dlg.fields()->widthEdit(), QStringLiteral("16384"));
    EXPECT_TRUE(dlg.fields()->limitLabel()->isHidden());
    type(dlg.fields()->heightEdit(), QStringLiteral("abc"));
    EXPECT_GE(dlg.answer().values.pixels.height(), 1);
}

TEST(PresetDialogs, RegisterDialogHasNoneAsTheFirstBackgroundAndNeedsAName) {
    CanvasPreset start;
    start.width = 210;
    start.height = 297;
    start.sizeUnit = SizeUnit::Millimeters;
    start.resolution = 350;
    PresetEditDialog dlg(start);
    EXPECT_EQ(dlg.windowTitle(), QStringLiteral("Register New Canvas Preset"));
    const QComboBox* bg = dlg.fields()->backgroundCombo();
    ASSERT_EQ(bg->count(), 10);
    EXPECT_EQ(itemTexts(bg),
              (QStringList{"[None]", "White", "Black", "Color", "", "Transparent (Light Checker)", "Transparent (Dark Checker)",
                           "Transparent (White)", "Transparent (Black)", "Transparent (Color)"}));
    EXPECT_EQ(bg->currentText(), QStringLiteral("[None]"));
    EXPECT_EQ(labelNamed(&dlg, "pixelsLabel")->text(), QStringLiteral("2894") + kTimes + QStringLiteral("4093"));
    EXPECT_FALSE(dlg.okButton()->isEnabled());
    dlg.nameEdit()->setText(QStringLiteral("My A4"));
    EXPECT_TRUE(dlg.okButton()->isEnabled());
    CanvasPreset made = dlg.preset();
    EXPECT_EQ(made.name, QStringLiteral("My A4"));
    EXPECT_EQ(made.width, 210);
    EXPECT_EQ(made.sizeUnit, SizeUnit::Millimeters);
    EXPECT_EQ(made.resolution, 350);
    EXPECT_FALSE(made.background.has_value());
    dlg.fields()->backgroundCombo()->setCurrentIndex(9);
    made = dlg.preset();
    ASSERT_TRUE(made.background.has_value());
    EXPECT_EQ(made.background->mode, BackgroundMode::TransparencyColor);
}

TEST(PresetDialogs, ConfigureDialogCreatesInsertsModifiesAndDeletes) {
    NewCanvasValues current;
    current.pixels = QSize(800, 600);
    PresetListDialog dlg(defaultCanvasPresets(), current);
    EXPECT_EQ(dlg.windowTitle(), QStringLiteral("Configure Canvas Presets"));
    EXPECT_EQ(dlg.list()->count(), 8);
    EXPECT_EQ(dlg.createButton()->toolTip(), QStringLiteral("Create a new preset and insert it to the selected position."));
    EXPECT_EQ(dlg.separatorButton()->toolTip(), QStringLiteral("Insert a separator to the selected position."));
    EXPECT_EQ(dlg.modifyButton()->toolTip(), QStringLiteral("Modify the selected preset."));
    EXPECT_EQ(dlg.deleteButton()->toolTip(), QStringLiteral("Delete the selected preset."));

    CanvasPreset seen;
    dlg.editPreset = [&](const CanvasPreset& initial) -> std::optional<CanvasPreset> {
        seen = initial;
        CanvasPreset p = initial;
        p.name = QStringLiteral("Edited");
        return p;
    };
    dlg.list()->setCurrentRow(3);
    dlg.createButton()->click();
    EXPECT_EQ(seen.width, 800);
    ASSERT_EQ(dlg.presets().size(), 9u);
    EXPECT_EQ(dlg.presets()[3].name, QStringLiteral("Edited"));
    EXPECT_EQ(dlg.presets()[4].name, QStringLiteral("A5 - 300ppi"));

    dlg.list()->setCurrentRow(0);
    dlg.separatorButton()->click();
    ASSERT_EQ(dlg.presets().size(), 10u);
    EXPECT_TRUE(dlg.presets()[0].separator);
    EXPECT_FALSE(dlg.modifyButton()->isEnabled());

    dlg.list()->setCurrentRow(1);
    dlg.modifyButton()->click();
    EXPECT_EQ(seen.name, QString::fromUtf8("512 × 512 - 72ppi"));
    EXPECT_EQ(dlg.presets()[1].name, QStringLiteral("Edited"));
    EXPECT_EQ(dlg.list()->item(1)->text(), QStringLiteral("Edited"));

    dlg.list()->setCurrentRow(0);
    dlg.deleteButton()->click();
    EXPECT_EQ(dlg.presets().size(), 9u);
    EXPECT_FALSE(dlg.presets()[0].separator);
    EXPECT_EQ(dlg.list()->count(), 9);

    dlg.editPreset = [](const CanvasPreset&) -> std::optional<CanvasPreset> { return std::nullopt; };
    dlg.list()->setCurrentRow(0);
    dlg.modifyButton()->click();
    EXPECT_EQ(dlg.presets()[0].name, QStringLiteral("Edited"));
}

TEST(JpegQualityDialogTest, OnlyJpegAsksForOptions) {
    const FormatRegistry& r = FormatRegistry::instance();
    for (const char* id : {"png", "bmp", "tga", "psd"})
        EXPECT_FALSE(JpegQualityDialog::hasOptions(r.byId(QString::fromLatin1(id))->features())) << id;
    EXPECT_TRUE(JpegQualityDialog::hasOptions(r.byId(QStringLiteral("jpeg"))->features()));
}

TEST(JpegQualityDialogTest, StartsWithTheDefaultSettings) {
    JpegQualityDialog dlg(detailedCanvas(), ExportOptions{}, 72.0);
    EXPECT_EQ(dlg.windowTitle(), QStringLiteral("JPEG Quality"));
    EXPECT_EQ(dlg.qualityBar()->value(), 90);
    EXPECT_FALSE(dlg.horizontalBox()->isChecked());
    EXPECT_FALSE(dlg.verticalBox()->isChecked());
    EXPECT_TRUE(dlg.jpegRadio()->isChecked());
    EXPECT_FALSE(dlg.originalRadio()->isChecked());
    EXPECT_EQ(dlg.scaleCombo()->currentText(), QStringLiteral("100%"));
    QStringList scales;
    for (int i = 0; i < dlg.scaleCombo()->count(); ++i)
        if (!dlg.scaleCombo()->itemText(i).isEmpty())
            scales << dlg.scaleCombo()->itemText(i);
    EXPECT_EQ(scales,
              (QStringList{"0.78%", "1.56%", "3.12%", "6.25%", "12.5%", "25%", "50%", "100%", "200%", "400%", "800%", "1600%", "3200%"}));
    EXPECT_FALSE(dlg.progress()->isTextVisible());
    waitUntilIdle(dlg);
    EXPECT_EQ(labelNamed(&dlg, "horzPixelsLabel")->text(), QStringLiteral("256"));
    EXPECT_EQ(labelNamed(&dlg, "vertPixelsLabel")->text(), QStringLiteral("256"));
    EXPECT_EQ(labelNamed(&dlg, "fileSizeLabel")->text(), dlg.compression().fileSizeText());
    EXPECT_TRUE(labelNamed(&dlg, "rateLabel")->text().startsWith(QStringLiteral("1/")));
    EXPECT_GT(dlg.compression().fileSize, 2000);
    EXPECT_EQ(dlg.options(), ExportOptions{});
    EXPECT_EQ(dlg.progress()->maximum(), 1);
}

TEST(JpegQualityDialogTest, ResultFollowsQualityAndChromaAndMatchesTheFileThatIsWritten) {
    const QImage canvas = detailedCanvas();
    JpegQualityDialog dlg(canvas, ExportOptions{}, 72.0);
    dlg.setDebounceMs(0);
    waitUntilIdle(dlg);
    const qint64 q90 = dlg.compression().fileSize;
    dlg.qualityBar()->setValue(10);
    EXPECT_FALSE(dlg.isIdle());
    EXPECT_EQ(dlg.progress()->maximum(), 0);
    waitUntilIdle(dlg);
    EXPECT_LT(dlg.compression().fileSize, q90);
    EXPECT_EQ(dlg.progress()->maximum(), 1);
    io::JpegSettings s;
    s.quality = 10;
    EXPECT_EQ(dlg.compression().fileSize, io::encodeJpeg(canvas, s).value().size());
    dlg.qualityBar()->setValue(90);
    const qint64 plain = (waitUntilIdle(dlg), dlg.compression().fileSize);
    dlg.horizontalBox()->setChecked(true);
    dlg.verticalBox()->setChecked(true);
    waitUntilIdle(dlg);
    EXPECT_LT(dlg.compression().fileSize, plain);
    ExportOptions o = dlg.options();
    EXPECT_EQ(o.quality, 90);
    EXPECT_TRUE(o.chromaHorizontal);
    EXPECT_TRUE(o.chromaVertical);
}

TEST(JpegQualityDialogTest, ScaleAndImageOnlyChangeThePreview) {
    JpegQualityDialog dlg(detailedCanvas(), ExportOptions{}, 72.0);
    dlg.setDebounceMs(0);
    waitUntilIdle(dlg);
    const io::CompressionResult before = dlg.compression();
    const QImage jpegShown = dlg.preview()->image();
    EXPECT_NE(jpegShown, detailedCanvas());
    EXPECT_EQ(jpegShown.size(), QSize(256, 256));

    dlg.originalRadio()->setChecked(true);
    EXPECT_EQ(dlg.preview()->image(), detailedCanvas());
    dlg.jpegRadio()->setChecked(true);
    EXPECT_EQ(dlg.preview()->image(), jpegShown);

    dlg.scaleCombo()->setCurrentIndex(dlg.scaleCombo()->findText(QStringLiteral("200%")));
    EXPECT_DOUBLE_EQ(dlg.preview()->scalePercent(), 200.0);
    EXPECT_EQ(dlg.preview()->scaledSize(), QSize(512, 512));
    dlg.scaleCombo()->setCurrentIndex(dlg.scaleCombo()->findText(QStringLiteral("0.78%")));
    EXPECT_EQ(dlg.preview()->scaledSize(), QSize(2, 2));
    EXPECT_TRUE(dlg.isIdle());
    EXPECT_EQ(dlg.compression().fileSize, before.fileSize);
}

TEST(JpegQualityDialogTest, ZoomButtonsStepThroughTheListAndFitShowsEverything) {
    JpegQualityDialog dlg(detailedCanvas(), ExportOptions{}, 72.0);
    dlg.resize(640, 492);
    dlg.show();
    waitUntilIdle(dlg);
    auto* zoomIn = dlg.findChild<QPushButton*>(QStringLiteral("zoomInButton"));
    auto* zoomOut = dlg.findChild<QPushButton*>(QStringLiteral("zoomOutButton"));
    auto* fit = dlg.findChild<QPushButton*>(QStringLiteral("fitButton"));
    ASSERT_TRUE(zoomIn && zoomOut && fit);
    EXPECT_EQ(dlg.scaleCombo()->currentText(), QStringLiteral("100%"));
    zoomIn->click();
    EXPECT_EQ(dlg.scaleCombo()->currentText(), QStringLiteral("200%"));
    zoomOut->click();
    zoomOut->click();
    EXPECT_EQ(dlg.scaleCombo()->currentText(), QStringLiteral("50%"));
    for (int i = 0; i < 20; ++i)
        zoomOut->click();
    EXPECT_EQ(dlg.scaleCombo()->currentText(), QStringLiteral("0.78%"));
    for (int i = 0; i < 20; ++i)
        zoomIn->click();
    EXPECT_EQ(dlg.scaleCombo()->currentText(), QStringLiteral("3200%"));
    fit->click();
    const QSize view = dlg.preview()->deviceViewportSize();
    const QSize shown = dlg.preview()->scaledSize();
    EXPECT_LE(shown.width(), view.width());
    EXPECT_LE(shown.height(), view.height());
}

TEST(JpegQualityDialogTest, HundredPercentIsOnePicturePixelPerScreenPixelAtAnyScale) {
    JpegQualityDialog dlg(detailedCanvas(QSize(300, 40)), ExportOptions{}, 72.0);
    dlg.resize(640, 492);
    dlg.show();
    waitUntilIdle(dlg);
    dlg.originalRadio()->setChecked(true);
    JpegPreviewView* view = dlg.preview();
    const QImage shown = view->viewport()->grab().toImage();
    const int row = shown.height() / 2;
    int white = 0;
    for (int x = 0; x < shown.width(); ++x)
        white += shown.pixelColor(x, row) == QColor(Qt::white);
    EXPECT_EQ(white, 300) << "device pixel ratio " << view->viewport()->devicePixelRatioF();

    dlg.scaleCombo()->setCurrentIndex(dlg.scaleCombo()->findText(QStringLiteral("800%")));
    EXPECT_EQ(view->horizontalScrollBar()->maximum(), 2400 - view->deviceViewportSize().width());
#if QT_VERSION >= QT_VERSION_CHECK(6, 6, 0)
    QEvent changed(QEvent::DevicePixelRatioChange);
    QCoreApplication::sendEvent(view, &changed);
    EXPECT_EQ(view->horizontalScrollBar()->maximum(), 2400 - view->deviceViewportSize().width());
#endif
}

TEST(JpegQualityDialogTest, QualityBarSetsTheValueFromClicksKeysAndTheWheel) {
    QualityBar bar(90);
    bar.resize(200, 20);
    EXPECT_EQ(bar.value(), 90);
    bar.setValue(0);
    EXPECT_EQ(bar.value(), 1);
    bar.setValue(500);
    EXPECT_EQ(bar.value(), 100);
    int emitted = 0;
    QObject::connect(&bar, &QualityBar::valueChanged, &bar, [&](int) { ++emitted; });
    bar.setValue(100);
    EXPECT_EQ(emitted, 0);
    QMouseEvent press(QEvent::MouseButtonPress, QPointF(0, 10), QPointF(0, 10), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QCoreApplication::sendEvent(&bar, &press);
    EXPECT_EQ(bar.value(), 1);
    QMouseEvent middle(QEvent::MouseButtonPress, QPointF(100, 10), QPointF(100, 10), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QCoreApplication::sendEvent(&bar, &middle);
    EXPECT_NEAR(bar.value(), 50, 2);
    QKeyEvent right(QEvent::KeyPress, Qt::Key_Right, Qt::NoModifier);
    const int before = bar.value();
    QCoreApplication::sendEvent(&bar, &right);
    EXPECT_EQ(bar.value(), before + 1);
    QKeyEvent pageUp(QEvent::KeyPress, Qt::Key_PageUp, Qt::NoModifier);
    QCoreApplication::sendEvent(&bar, &pageUp);
    EXPECT_EQ(bar.value(), before + 11);
}

TEST(JpegQualityDialogTest, ManyQuickChangesGiveOneUpToDateResult) {
    const QImage canvas = detailedCanvas();
    JpegQualityDialog dlg(canvas, ExportOptions{}, 72.0);
    dlg.setDebounceMs(20);
    waitUntilIdle(dlg);
    for (int q = 90; q >= 40; q -= 5)
        dlg.qualityBar()->setValue(q);
    waitUntilIdle(dlg);
    io::JpegSettings s;
    s.quality = 40;
    EXPECT_EQ(dlg.compression().fileSize, io::encodeJpeg(canvas, s).value().size());
}

TEST_F(FileUiTest, NewCanvasFromTheDialog) {
    NewCanvasValues values;
    values.pixels = QSize(320, 200);
    values.resolution = 300;
    values.background = CanvasBackground{BackgroundMode::Color, QColor(200, 10, 10)};
    files->newAnswer = NewCanvasAnswer{QStringLiteral("My drawing"), values};
    files->newDocument();
    ASSERT_EQ(tabs.count(), 1);
    EXPECT_EQ(doc()->size(), QSize(320, 200));
    EXPECT_EQ(doc()->background().paperColor(), QColor(200, 10, 10));
    EXPECT_DOUBLE_EQ(doc()->resolution(), 300.0);
    EXPECT_EQ(doc()->displayName(), QStringLiteral("My drawing"));
    EXPECT_TRUE(doc()->filePath().isEmpty());
    EXPECT_FALSE(doc()->isModified());
    files->newAnswer.reset();
    files->newDocument();
    EXPECT_EQ(tabs.count(), 1);
}

TEST_F(FileUiTest, NewCanvasOffersNewCanvasNumberedAndTheSixPresets) {
    files->newAnswer.reset();
    files->newDocument();
    ASSERT_TRUE(files->lastNewRequest);
    EXPECT_EQ(files->lastNewRequest->fileName, QStringLiteral("NewCanvas1"));
    EXPECT_EQ(files->lastNewRequest->presets, defaultCanvasPresets());
    files->newDocument();
    EXPECT_EQ(files->lastNewRequest->fileName, QStringLiteral("NewCanvas1"));
    NewCanvasValues values;
    values.pixels = QSize(64, 64);
    files->newAnswer = NewCanvasAnswer{QStringLiteral("NewCanvas1"), values};
    files->newDocument();
    EXPECT_EQ(files->lastNewRequest->fileName, QStringLiteral("NewCanvas1"));
    files->newDocument();
    EXPECT_EQ(files->lastNewRequest->fileName, QStringLiteral("NewCanvas2"));
    files->newDocument();
    EXPECT_EQ(files->lastNewRequest->fileName, QStringLiteral("NewCanvas3"));
}

TEST_F(FileUiTest, NewCanvasRemembersTheLastValuesAndThePresetList) {
    files->newAnswer.reset();
    files->newDocument();
    const NewCanvasValues firstOffer = files->lastNewRequest->values;
    EXPECT_EQ(firstOffer, NewCanvasValues{});

    NewCanvasValues values;
    values.pixels = QSize(2150, 3035);
    values.resolution = 300;
    values.sizeUnit = SizeUnit::Millimeters;
    values.resolutionUnit = ResolutionUnit::PerCentimeter;
    values.background = CanvasBackground{BackgroundMode::TransparencyColor, QColor(255, 128, 0)};
    files->newAnswer = NewCanvasAnswer{QStringLiteral("NewCanvas1"), values};
    files->newDocument();
    files->newAnswer.reset();
    files->newDocument();
    EXPECT_EQ(files->lastNewRequest->values, values);

    ScriptedFiles again(&tabs, FormatRegistry::instance(), nullptr);
    again.newDocument();
    EXPECT_EQ(again.lastNewRequest->values, values);
    EXPECT_EQ(again.lastNewRequest->fileName, QStringLiteral("NewCanvas1"));

    CanvasPresetList mine = defaultCanvasPresets();
    mine.erase(mine.begin());
    CanvasPreset extra;
    extra.name = QStringLiteral("Mine");
    extra.width = 640;
    extra.height = 480;
    mine.push_back(extra);
    FileController::savePresets(mine);
    EXPECT_EQ(FileController::savedPresets(), mine);
    files->newDocument();
    EXPECT_EQ(files->lastNewRequest->presets, mine);
    QSettings damaged = app_identity::settings();
    damaged.setValue(QStringLiteral("files/canvasPresets"), QStringLiteral("not json"));
    damaged.setValue(QStringLiteral("files/newCanvas"), QStringLiteral("{{{"));
    files->newDocument();
    EXPECT_EQ(files->lastNewRequest->presets, defaultCanvasPresets());
    EXPECT_EQ(files->lastNewRequest->values, NewCanvasValues{});
}

TEST_F(FileUiTest, SaveAsThenSaveThenReopenKeepsEverything) {
    Document* d = paintedDocument();
    (void)d->addLayer();
    (void)d->setLayerProps(d->currentLayerId(), [&] {
        LayerProps p = d->currentLayer()->props;
        p.mode = BlendMode::Multiply;
        p.opacity = 33;
        p.name = QStringLiteral("Shadow");
        return p;
    }());
    (void)d->selectAll();
    ASSERT_TRUE(d->isModified());
    EXPECT_EQ(d->titleText(), QStringLiteral("*") + d->displayName());

    files->saveAnswer = path("picture.doodle");
    files->saveFormat = QStringLiteral("doodle");
    ASSERT_TRUE(files->save(d));
    EXPECT_EQ(files->saveAsked, 1);
    EXPECT_TRUE(QFile::exists(path("picture.doodle")));
    EXPECT_FALSE(d->isModified());
    EXPECT_FALSE(d->lostData());
    EXPECT_EQ(d->titleText(), QStringLiteral("picture.doodle"));
    EXPECT_EQ(files->optionsAsked, 0);

    (void)d->addLayer();
    EXPECT_EQ(d->titleText(), QStringLiteral("*picture.doodle"));
    ASSERT_TRUE(files->save(d));
    EXPECT_EQ(files->saveAsked, 1);
    EXPECT_FALSE(d->isModified());

    tabs.addDocument(new Document(QSize(8, 8), Qt::white));
    ASSERT_TRUE(files->openFile(path("picture.doodle")));
    EXPECT_EQ(tabs.count(), 2);
    EXPECT_EQ(doc(), d);

    const DocumentContent before = d->content();
    ASSERT_TRUE(tabs.requestCloseTab(0));
    ASSERT_TRUE(files->openFile(path("picture.doodle")));
    ASSERT_EQ(tabs.count(), 2);
    Document* again = doc();
    EXPECT_EQ(again->displayName(), QStringLiteral("picture.doodle"));
    EXPECT_FALSE(again->isModified());
    EXPECT_EQ(again->content().state.layers.layerCount(), before.state.layers.layerCount());
    EXPECT_EQ(again->content().state.current, before.state.current);
    EXPECT_TRUE(again->selection().has_value());
    EXPECT_EQ(again->background().paperColor(), QColor(240, 230, 220));
    const LayerNode* shadow = nullptr;
    again->layers().forEach([&](const LayerNode& n) {
        if (n.props.name == QStringLiteral("Shadow"))
            shadow = &n;
    });
    ASSERT_TRUE(shadow);
    EXPECT_EQ(shadow->props.mode, BlendMode::Multiply);
    EXPECT_EQ(shadow->props.opacity, 33);
}

TEST_F(FileUiTest, SavingInAFormatThatLosesDataSetsTheBangMarker) {
    Document* d = paintedDocument();
    files->saveAnswer = path("flat.psd");
    files->saveFormat = QStringLiteral("psd");
    ASSERT_TRUE(files->saveAs(d));
    EXPECT_TRUE(QFile::exists(path("flat.psd")));
    EXPECT_TRUE(d->lostData());
    EXPECT_FALSE(d->isModified());
    EXPECT_EQ(d->titleText(), QStringLiteral("!flat.psd"));
    (void)d->addLayer();
    EXPECT_EQ(d->titleText(), QStringLiteral("*!flat.psd"));
    files->saveAsked = 0;
    ASSERT_TRUE(files->save(d));
    EXPECT_EQ(files->saveAsked, 0);
    EXPECT_EQ(d->titleText(), QStringLiteral("!flat.psd"));
    files->saveAnswer = path("flat.doodle");
    files->saveFormat = QStringLiteral("doodle");
    ASSERT_TRUE(files->saveAs(d));
    EXPECT_EQ(d->titleText(), QStringLiteral("flat.doodle"));
}

TEST_F(FileUiTest, SaveAsPicksTheFormatFromTheExtensionOrTheFilter) {
    const FormatRegistry& r = FormatRegistry::instance();
    EXPECT_EQ(FileController::resolveSaveTarget(r, QStringLiteral("/a/b.PNG"), r.byId(QStringLiteral("doodle"))).second,
              r.byId(QStringLiteral("png")));
    const auto bare = FileController::resolveSaveTarget(r, QStringLiteral("/a/b"), r.byId(QStringLiteral("psd")));
    EXPECT_EQ(bare.first, QStringLiteral("/a/b.psd"));
    EXPECT_EQ(bare.second, r.byId(QStringLiteral("psd")));
    EXPECT_EQ(FileController::resolveSaveTarget(r, QStringLiteral("/a/b"), nullptr).second, nullptr);
    Document* d = paintedDocument();
    files->saveAnswer = path("noext");
    files->saveFormat = QStringLiteral("doodle");
    ASSERT_TRUE(files->saveAs(d));
    EXPECT_TRUE(QFile::exists(path("noext.doodle")));
}

TEST_F(FileUiTest, SaveAsPictureAsksForItsOptionsOnceAndRemembersThem) {
    Document* d = paintedDocument();
    files->saveAnswer = path("shot.jpg");
    files->saveFormat = QStringLiteral("jpeg");
    ExportOptions q;
    q.quality = 20;
    files->optionsAnswer = q;
    ASSERT_TRUE(files->saveAs(d));
    EXPECT_EQ(files->optionsAsked, 1);
    QFile f(path("shot.jpg"));
    ASSERT_TRUE(f.open(QIODevice::ReadOnly));
    const qint64 small = f.size();
    f.close();
    EXPECT_TRUE(d->lostData());

    (void)d->addLayer();
    ASSERT_TRUE(files->save(d));
    EXPECT_EQ(files->optionsAsked, 1);
    EXPECT_EQ(files->saveAsked, 1);
    ASSERT_TRUE(f.open(QIODevice::ReadOnly));
    EXPECT_EQ(f.size(), small);
    f.close();

    files->saveAnswer = path("other.jpg");
    files->optionsAnswer.reset();
    EXPECT_FALSE(files->saveAs(d));
    EXPECT_FALSE(QFile::exists(path("other.jpg")));
    EXPECT_EQ(d->filePath(), path("shot.jpg"));
}

TEST_F(FileUiTest, OpeningAPictureMakesAnUnsavedDocumentNamedAfterIt) {
    const QImage img = solid(QSize(40, 30), QColor(30, 60, 90));
    ASSERT_TRUE(img.save(path("photo.png")));
    ASSERT_TRUE(files->openFile(path("photo.png")));
    ASSERT_EQ(tabs.count(), 1);
    EXPECT_EQ(doc()->size(), QSize(40, 30));
    EXPECT_EQ(doc()->displayName(), QStringLiteral("photo.png"));
    EXPECT_FALSE(doc()->isModified());
    EXPECT_TRUE(doc()->filePath().isEmpty());
    const auto px = doc()->currentLayer()->pixels.pixel(5, 5);
    EXPECT_EQ(px[0], fromUnorm8(30));
    EXPECT_EQ(px[3], kChannelOne);

    files->saveAnswer = path("photo.doodle");
    files->saveFormat = QStringLiteral("doodle");
    ASSERT_TRUE(files->save(doc()));
    EXPECT_EQ(files->saveAsked, 1);
    EXPECT_TRUE(QFile::exists(path("photo.doodle")));
}

// Canvases are at most 16384 × 16384: a larger picture opens scaled down to fit, if the user wants.
TEST_F(FileUiTest, OpeningAPictureBeyondTheCanvasLimitOffersToScaleItDown) {
    const QImage img = solid(QSize(kMaxCanvasSide + 4000, 50), QColor(30, 60, 90));
    ASSERT_TRUE(img.save(path("panorama.png")));

    files->scaleDownAnswer = false;
    EXPECT_FALSE(files->openFile(path("panorama.png")));
    EXPECT_EQ(files->scaleDownAsked, QStringList{QStringLiteral("“panorama.png” 20384x50")});
    EXPECT_EQ(tabs.count(), 0);
    EXPECT_TRUE(files->errors.isEmpty()) << "the question said why; declining opens nothing";

    files->scaleDownAnswer = true;
    ASSERT_TRUE(files->openFile(path("panorama.png")));
    EXPECT_EQ(files->scaleDownAsked.size(), 2);
    ASSERT_EQ(tabs.count(), 1);
    EXPECT_EQ(doc()->size(), QSize(kMaxCanvasSide, 40));
    EXPECT_EQ(doc()->displayName(), QStringLiteral("panorama.png"));
    EXPECT_TRUE(doc()->filePath().isEmpty()) << "a save never writes the scaled picture over the original";
    const auto px = doc()->currentLayer()->pixels.pixel(kMaxCanvasSide / 2, 20);
    EXPECT_EQ(px[0], fromUnorm8(30));
}

TEST_F(FileUiTest, ALayeredFileBeyondTheCanvasLimitSaysSoPlainly) {
    Document wide(QSize(kMaxCanvasSide + 1, 8), QColor(240, 230, 220));
    ASSERT_TRUE(io::saveFile(wide.content(), path("wide.doodle"), *FormatRegistry::instance().byId(QStringLiteral("doodle"))).ok());
    EXPECT_FALSE(files->openFile(path("wide.doodle")));
    EXPECT_TRUE(files->scaleDownAsked.isEmpty()) << "a layered file isn't scaled";
    ASSERT_EQ(files->errors.size(), 1);
    EXPECT_TRUE(files->errors[0].contains(QStringLiteral("16385 × 8 pixels; canvases are at most 16384 × 16384")))
        << qPrintable(files->errors[0]);
    EXPECT_EQ(tabs.count(), 0);
}

TEST_F(FileUiTest, NewFromClipboardOffersToScaleDownAPictureBeyondTheCanvasLimit) {
    const QImage tall = solid(QSize(30, kMaxCanvasSide * 2), QColor(200, 100, 0));
    files->scaleDownAnswer = false;
    files->newDocumentFromPicture(tall);
    EXPECT_EQ(tabs.count(), 0);
    ASSERT_EQ(files->scaleDownAsked.size(), 1);
    EXPECT_EQ(files->scaleDownAsked[0], QStringLiteral("The picture on the clipboard 30x32768"));

    files->scaleDownAnswer = true;
    files->newDocumentFromPicture(tall);
    ASSERT_EQ(tabs.count(), 1);
    EXPECT_EQ(doc()->size(), QSize(15, kMaxCanvasSide));

    files->newDocumentFromPicture(solid(QSize(kMaxCanvasSide, 3), Qt::red));
    EXPECT_EQ(files->scaleDownAsked.size(), 2) << "a picture at the limit is not asked about";
    EXPECT_EQ(doc()->size(), QSize(kMaxCanvasSide, 3));
}

TEST_F(FileUiTest, ExportWritesACopyAndLeavesTheDocumentAlone) {
    Document* d = paintedDocument();
    (void)d->addLayer();
    const QString name = d->displayName();
    const FormatRegistry& r = FormatRegistry::instance();

    files->exportAnswer = path("out.png");
    ASSERT_TRUE(files->exportAs(d, *r.byId(QStringLiteral("png"))));
    QImage png(path("out.png"));
    ASSERT_FALSE(png.isNull());
    EXPECT_EQ(png.size(), d->size());
    EXPECT_EQ(png.pixelColor(3, 3), QColor(10, 120, 200));
    EXPECT_TRUE(d->isModified());
    EXPECT_FALSE(d->lostData());
    EXPECT_TRUE(d->filePath().isEmpty());
    EXPECT_EQ(d->displayName(), name);
    EXPECT_EQ(files->optionsAsked, 0);

    files->exportAnswer = path("plain");
    ASSERT_TRUE(files->exportAs(d, *r.byId(QStringLiteral("tga"))));
    EXPECT_TRUE(QFile::exists(path("plain.tga")));

    files->exportAnswer = path("out.bmp");
    ASSERT_TRUE(files->exportAs(d, *r.byId(QStringLiteral("bmp"))));
    EXPECT_EQ(QImage(path("out.bmp")).pixelColor(1, 1), QColor(10, 120, 200));

    files->exportAnswer = path("out.psd");
    ASSERT_TRUE(files->exportAs(d, *r.byId(QStringLiteral("psd"))));
    EXPECT_TRUE(io::loadFile(path("out.psd")).ok());

    files->exportAnswer.clear();
    EXPECT_FALSE(files->exportAs(d, *r.byId(QStringLiteral("png"))));
    files->exportAnswer = path("never.jpg");
    files->optionsAnswer.reset();
    EXPECT_FALSE(files->exportAs(d, *r.byId(QStringLiteral("jpeg"))));
    EXPECT_FALSE(QFile::exists(path("never.jpg")));
}

TEST_F(FileUiTest, JpegExportRemembersQualityAndChromaPerDocumentOnlyWhenOkIsPressed) {
    Document* d = paintedDocument();
    const DocumentFormat& jpeg = *FormatRegistry::instance().byId(QStringLiteral("jpeg"));
    ExportOptions chosen;
    chosen.quality = 33;
    chosen.chromaHorizontal = true;
    files->optionsAnswer = chosen;
    files->exportAnswer = path("a.jpg");
    ASSERT_TRUE(files->exportAs(d, jpeg));
    ASSERT_TRUE(files->lastOptionsShown);
    EXPECT_EQ(*files->lastOptionsShown, ExportOptions{});

    files->exportAnswer = path("b.jpg");
    ASSERT_TRUE(files->exportAs(d, jpeg));
    EXPECT_EQ(*files->lastOptionsShown, chosen);

    ExportOptions other;
    other.quality = 7;
    files->optionsAnswer.reset();
    files->exportAnswer = path("c.jpg");
    EXPECT_FALSE(files->exportAs(d, jpeg));
    files->optionsAnswer = other;
    files->exportAnswer = path("d.jpg");
    ASSERT_TRUE(files->exportAs(d, jpeg));
    EXPECT_EQ(*files->lastOptionsShown, chosen);
    Document* second = paintedDocument();
    files->exportAnswer = path("e.jpg");
    ASSERT_TRUE(files->exportAs(second, jpeg));
    EXPECT_EQ(*files->lastOptionsShown, ExportOptions{});
    QFile f(path("a.jpg"));
    ASSERT_TRUE(f.open(QIODevice::ReadOnly));
    const QByteArray bytes = f.readAll();
    const qsizetype sof = bytes.indexOf(QByteArray("\xff\xc0", 2));
    ASSERT_GT(sof, 0);
    EXPECT_EQ(std::uint8_t(bytes[sof + 11]), 0x21);
}

TEST_F(FileUiTest, JpegExportUsesTheChosenQuality) {
    Document* d = paintedDocument(QSize(200, 200));
    for (int y = 0; y < 200; y += 3)
        (void)d->fillLayer(d->currentLayerId(), QColor((y * 7) % 256, y % 256, 99));
    const DocumentFormat& jpeg = *FormatRegistry::instance().byId(QStringLiteral("jpeg"));
    qint64 sizes[2];
    int i = 0;
    for (int quality : {5, 95}) {
        ExportOptions o;
        o.quality = quality;
        files->optionsAnswer = o;
        files->exportAnswer = path(quality == 5 ? "low.jpg" : "high.jpg");
        ASSERT_TRUE(files->exportAs(d, jpeg));
        QFile f(files->exportAnswer);
        ASSERT_TRUE(f.open(QIODevice::ReadOnly));
        EXPECT_TRUE(f.read(2) == QByteArray("\xff\xd8", 2));
        sizes[i++] = f.size();
    }
    EXPECT_LT(sizes[0], sizes[1]);
}

TEST_F(FileUiTest, ErrorsAreShownAndNothingIsCreated) {
    EXPECT_FALSE(files->openFile(path("missing.doodle")));
    ASSERT_EQ(files->errors.size(), 1);
    EXPECT_TRUE(files->errors[0].contains(QStringLiteral("missing.doodle")));

    Document* d = paintedDocument();
    files->saveAnswer = path("good.doodle");
    files->saveFormat = QStringLiteral("doodle");
    ASSERT_TRUE(files->saveAs(d));
    QFile f(path("good.doodle"));
    ASSERT_TRUE(f.open(QIODevice::ReadWrite));
    QByteArray bytes = f.readAll();
    f.resize(bytes.size() / 2);
    f.close();
    tabs.requestCloseTab(0);
    EXPECT_FALSE(files->openFile(path("good.doodle")));
    ASSERT_EQ(files->errors.size(), 2);
    EXPECT_TRUE(files->errors[1].contains(QStringLiteral("incomplete")));
    EXPECT_EQ(tabs.count(), 0);

    QFile junk(path("junk.doodle"));
    ASSERT_TRUE(junk.open(QIODevice::WriteOnly));
    junk.write("this is no canvas at all");
    junk.close();
    EXPECT_FALSE(files->openFile(path("junk.doodle")));
    EXPECT_TRUE(files->errors[2].contains(QStringLiteral("not an Doodlebold file")));

    Document* e = paintedDocument();
    (void)e->addLayer();
    files->saveAnswer = path("no/such/folder/x.doodle");
    EXPECT_FALSE(files->saveAs(e));
    EXPECT_EQ(files->errors.size(), 4);
    EXPECT_TRUE(e->isModified());
    EXPECT_TRUE(e->filePath().isEmpty());
}

TEST_F(FileUiTest, NotesOfAnImportAreShown) {
    ASSERT_TRUE(files->openFile(QStringLiteral(DOODLEBOLD_FIXTURE_DIR "/imagemagick-8bit.psd")));
    EXPECT_FALSE(files->notes.isEmpty());
    EXPECT_EQ(doc()->layers().layerCount(), 2);
}

TEST_F(FileUiTest, RecentFilesAreKeptNewestFirstAndCapped) {
    EXPECT_TRUE(files->recentFiles().isEmpty());
    for (int i = 0; i < FileController::kMaxRecent + 3; ++i) {
        const QString p = path(qPrintable(QStringLiteral("p%1.png").arg(i)));
        ASSERT_TRUE(solid(QSize(4, 4), Qt::red).save(p));
        ASSERT_TRUE(files->openFile(p));
    }
    const QStringList recent = files->recentFiles();
    ASSERT_EQ(recent.size(), FileController::kMaxRecent);
    EXPECT_TRUE(recent.first().endsWith(QStringLiteral("p12.png")));
    EXPECT_TRUE(recent.last().endsWith(QStringLiteral("p3.png")));
    files->clearRecent();
    EXPECT_TRUE(files->recentFiles().isEmpty());
}

TEST_F(FileUiTest, ClosingAModifiedCanvasCanSaveItFirst) {
    Document* d = paintedDocument();
    (void)d->addLayer();
    tabs.choice = DocumentTabs::UnsavedChoice::Save;
    files->saveAnswer.clear();
    files->saveFormat = QStringLiteral("doodle");
    EXPECT_FALSE(tabs.requestCloseTab(0));
    EXPECT_EQ(tabs.count(), 1);
    EXPECT_TRUE(d->isModified());

    files->saveAnswer = path("closing.doodle");
    EXPECT_TRUE(tabs.requestCloseTab(0));
    EXPECT_EQ(tabs.count(), 0);
    EXPECT_TRUE(QFile::exists(path("closing.doodle")));
    EXPECT_TRUE(io::loadFile(path("closing.doodle")).ok());
}

class FileMenuTest : public testing::Test {
protected:
    void SetUp() override {
        QStandardPaths::setTestModeEnabled(true);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDir.path());
        test::applyTheme();
        QFile::remove(ToolBoxStore::defaultPath());
        window = std::make_unique<MainWindow>();
        window->newDocument();  // before show(): the offscreen platform cannot paint a canvas added to a visible window
        window->show();
        QCoreApplication::processEvents();
    }
    void TearDown() override { QFile::remove(ToolBoxStore::defaultPath()); }

    QAction* command(const char* id) const { return window->command(QString::fromLatin1(id)); }

    QTemporaryDir dir, settingsDir;
    std::unique_ptr<MainWindow> window;
};

TEST_F(FileMenuTest, ActionsThatNeedACanvasFollowTheTabs) {
    QAction* save = command("file.save");
    QAction* exportPng = command("file.export.png");
    ASSERT_TRUE(save && exportPng);
    EXPECT_TRUE(save->isEnabled());
    EXPECT_TRUE(exportPng->isEnabled());
    window->tabs()->requestCloseTab(0);
    EXPECT_EQ(window->tabs()->count(), 0);
    EXPECT_FALSE(save->isEnabled());
    EXPECT_FALSE(exportPng->isEnabled());
    EXPECT_TRUE(command("file.open")->isEnabled());
    EXPECT_TRUE(command("file.new")->isEnabled());
    EXPECT_EQ(window->windowTitle(), QStringLiteral("Doodlebold"));
}

TEST_F(FileMenuTest, StartsWithoutADocument) {
    MainWindow fresh;
    EXPECT_EQ(fresh.tabs()->count(), 0);
    EXPECT_EQ(fresh.tabs()->currentDocument(), nullptr);
    EXPECT_EQ(fresh.windowTitle(), QStringLiteral("Doodlebold"));
}

TEST_F(FileMenuTest, WindowTitleMarksModifiedAndFlattenedFiles) {
    Document* d = window->tabs()->currentDocument();
    ASSERT_TRUE(d);
    EXPECT_EQ(window->windowTitle(), d->displayName() + QStringLiteral(" — Doodlebold"));
    (void)d->addLayer();
    EXPECT_EQ(window->windowTitle(), QStringLiteral("*") + d->displayName() + QStringLiteral(" — Doodlebold"));
    d->markSaved(dir.filePath(QStringLiteral("t.psd")), true);
    EXPECT_EQ(window->windowTitle(), QStringLiteral("!t.psd — Doodlebold"));
    d->markSaved(dir.filePath(QStringLiteral("t.doodle")), false);
    EXPECT_EQ(window->windowTitle(), QStringLiteral("t.doodle — Doodlebold"));
}

TEST_F(FileMenuTest, DroppedFilesOpen) {
    ASSERT_TRUE(solid(QSize(24, 16), Qt::blue).save(dir.filePath(QStringLiteral("dropped.png"))));
    ASSERT_TRUE(solid(QSize(12, 12), Qt::green).save(dir.filePath(QStringLiteral("second.png"))));
    ASSERT_EQ(window->tabs()->count(), 1);  // the canvas made by SetUp (the offscreen platform cannot paint a first tab)

    QMimeData mime;
    mime.setUrls({QUrl::fromLocalFile(dir.filePath(QStringLiteral("dropped.png"))),
                  QUrl::fromLocalFile(dir.filePath(QStringLiteral("second.png")))});
    QDragEnterEvent enter(QPoint(10, 10), Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
    QCoreApplication::sendEvent(window.get(), &enter);
    EXPECT_TRUE(enter.isAccepted());
    QDropEvent drop(QPointF(10, 10), Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
    QCoreApplication::sendEvent(window.get(), &drop);
    QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
    QCoreApplication::processEvents();
    ASSERT_EQ(window->tabs()->count(), 3);
    EXPECT_EQ(window->tabs()->currentDocument()->displayName(), QStringLiteral("second.png"));
    EXPECT_EQ(window->windowTitle(), QStringLiteral("second.png — Doodlebold"));
    EXPECT_EQ(window->tabs()->canvasAt(1)->document()->size(), QSize(24, 16));

    QMimeData text;
    text.setText(QStringLiteral("hello"));
    QDragEnterEvent other(QPoint(10, 10), Qt::CopyAction, &text, Qt::LeftButton, Qt::NoModifier);
    other.setAccepted(false);
    QCoreApplication::sendEvent(window.get(), &other);
    EXPECT_FALSE(other.isAccepted());
}

TEST_F(FileMenuTest, OpenRecentListsTheLatestFiles) {
    ASSERT_TRUE(solid(QSize(8, 8), Qt::red).save(dir.filePath(QStringLiteral("recent1.png"))));
    window->openFiles({dir.filePath(QStringLiteral("recent1.png"))});
    QAction* recent = window->findChild<QAction*>(QStringLiteral("file.recent"));
    ASSERT_TRUE(recent && recent->menu());
    emit recent->menu()->aboutToShow();
    EXPECT_TRUE(recent->isEnabled());
    ASSERT_GE(recent->menu()->actions().size(), 1);
    EXPECT_TRUE(recent->menu()->actions().first()->text().startsWith(QStringLiteral("&1 ")));
    EXPECT_TRUE(recent->menu()->actions().first()->text().contains(QStringLiteral("recent1.png")));
    const int tabs = window->tabs()->count();
    recent->menu()->actions().first()->trigger();
    EXPECT_EQ(window->tabs()->count(), tabs + 1);
}

namespace {

QMenu* canvasBackgroundMenu(const MainWindow& window) {
    const QAction* a = window.findChild<QAction*>(QStringLiteral("canvas.background"));
    return a ? a->menu() : nullptr;
}

QAction* backgroundAction(QMenu* menu, const QString& label) {
    for (QAction* a : menu->actions())
        if (a->text().remove('&') == label)
            return a;
    return nullptr;
}

}

TEST_F(FileMenuTest, CanvasMenuSitsBetweenEditAndLayerWithTheBackgroundEntries) {
    QStringList tops;
    for (const QAction* a : window->menuBar()->actions())
        tops << a->text().remove('&');
    const int canvas = int(tops.indexOf(QStringLiteral("Canvas")));
    ASSERT_GE(canvas, 0);
    EXPECT_EQ(tops.at(canvas - 1), QStringLiteral("Edit"));
    EXPECT_EQ(tops.at(canvas + 1), QStringLiteral("Layer"));

    QMenu* menu = canvasBackgroundMenu(*window);
    ASSERT_TRUE(menu);
    QStringList labels;
    for (const QAction* a : menu->actions())
        labels << (a->isSeparator() ? QStringLiteral("---") : a->text().remove('&'));
    EXPECT_EQ(labels, (QStringList{"Background Color…", "---", "White", "Black", "Color", "Transparent (White)", "Transparent (Black)",
                                   "Transparent (Color)", "Transparent (Light Checker)", "Transparent (Dark Checker)"}));
    for (const QAction* a : menu->actions()) {
        if (!a->isSeparator() && a->text() != QStringLiteral("Background Color…")) {
            EXPECT_TRUE(a->isCheckable()) << qPrintable(a->text());
        }
    }
}

TEST_F(FileMenuTest, CanvasBackgroundChangesTheCanvasIsCheckedAndUndoable) {
    Document* d = window->tabs()->currentDocument();
    ASSERT_TRUE(d);
    QMenu* menu = canvasBackgroundMenu(*window);
    ASSERT_TRUE(menu);
    emit menu->aboutToShow();
    EXPECT_TRUE(backgroundAction(menu, QStringLiteral("White"))->isChecked());
    EXPECT_FALSE(backgroundAction(menu, QStringLiteral("Black"))->isChecked());

    const int steps = d->undoStack()->count();
    backgroundAction(menu, QStringLiteral("Transparent (Dark Checker)"))->trigger();
    EXPECT_EQ(d->background().mode, BackgroundMode::TransparencyDarkChecker);
    EXPECT_EQ(d->undoStack()->count(), steps + 1);
    EXPECT_EQ(d->projection().pixel(0, 0), d->background().backdrop().a);
    emit menu->aboutToShow();
    EXPECT_TRUE(backgroundAction(menu, QStringLiteral("Transparent (Dark Checker)"))->isChecked());
    EXPECT_FALSE(backgroundAction(menu, QStringLiteral("White"))->isChecked());

    backgroundAction(menu, QStringLiteral("Black"))->trigger();
    EXPECT_EQ(d->background().mode, BackgroundMode::Black);
    EXPECT_EQ(d->projection().pixelColor(0, 0), QColor(Qt::black));
    d->undoStack()->undo();
    EXPECT_EQ(d->background().mode, BackgroundMode::TransparencyDarkChecker);
    d->undoStack()->undo();
    EXPECT_EQ(d->background().mode, BackgroundMode::White);
    const int count = d->undoStack()->count();
    backgroundAction(menu, QStringLiteral("White"))->trigger();
    EXPECT_EQ(d->undoStack()->count(), count);
}

TEST_F(FileMenuTest, CanvasBackgroundActionsNeedACanvas) {
    window->tabs()->requestCloseTab(0);
    ASSERT_EQ(window->tabs()->count(), 0);
    QMenu* menu = canvasBackgroundMenu(*window);
    ASSERT_TRUE(menu);
    for (const QAction* a : menu->actions()) {
        if (!a->isSeparator()) {
            EXPECT_FALSE(a->isEnabled()) << qPrintable(a->text());
        }
    }
}

TEST(DialogScreenshots, NewCanvasAndJpegQualityRender) {
    test::applyTheme();
    {
        NewCanvasValues v;
        v.pixels = QSize(2894, 4093);
        v.resolution = 350;
        v.sizeUnit = SizeUnit::Millimeters;
        NewCanvasDialog dlg(QStringLiteral("NewCanvas1"), v, defaultCanvasPresets());
        dlg.show();
        EXPECT_TRUE(test::saveScreenshot(dlg.grab(), QStringLiteral("new_canvas_dialog")));
    }
    {
        CanvasPreset start;
        start.width = 210;
        start.height = 297;
        start.sizeUnit = SizeUnit::Millimeters;
        start.resolution = 350;
        PresetEditDialog dlg(start);
        dlg.show();
        EXPECT_TRUE(test::saveScreenshot(dlg.grab(), QStringLiteral("register_preset_dialog")));
    }
    {
        PresetListDialog dlg(defaultCanvasPresets(), NewCanvasValues{});
        dlg.show();
        EXPECT_TRUE(test::saveScreenshot(dlg.grab(), QStringLiteral("configure_presets_dialog")));
    }
    {
        JpegQualityDialog dlg(detailedCanvas(), ExportOptions{}, 72.0);
        dlg.show();
        waitUntilIdle(dlg);
        EXPECT_TRUE(test::saveScreenshot(dlg.grab(), QStringLiteral("jpeg_quality_dialog")));
    }
}

TEST(NewCanvasLink, KeepsWidthAndHeightInProportionAndIsRemembered) {
    QStandardPaths::setTestModeEnabled(true);
    AppOptions::settings().remove(QStringLiteral("newCanvas/linkSize"));
    const NewCanvasValues values;
    {
        NewCanvasDialog dlg(QStringLiteral("a"), values, defaultCanvasPresets());
        auto* fields = dlg.fields();
        QToolButton* link = fields->linkSizeButton();
        ASSERT_TRUE(link);
        EXPECT_FALSE(link->isChecked()) << "off until the user turns it on";
        type(fields->widthEdit(), QStringLiteral("200"));
        type(fields->heightEdit(), QStringLiteral("100"));
        type(fields->widthEdit(), QStringLiteral("300"));
        EXPECT_EQ(fields->heightEdit()->text(), QStringLiteral("100"));
        type(fields->widthEdit(), QStringLiteral("200"));

        link->click();
        type(fields->widthEdit(), QStringLiteral("400"));
        EXPECT_EQ(fields->heightEdit()->text(), QStringLiteral("200"));
        type(fields->heightEdit(), QStringLiteral("50"));
        EXPECT_EQ(fields->widthEdit()->text(), QStringLiteral("100"));
        EXPECT_DOUBLE_EQ(fields->model().width(), 100.0);
    }
    NewCanvasDialog again(QStringLiteral("a"), values, defaultCanvasPresets());
    EXPECT_TRUE(again.fields()->linkSizeButton()->isChecked()) << "the choice is saved";
    again.fields()->linkSizeButton()->click();
    NewCanvasDialog third(QStringLiteral("a"), values, defaultCanvasPresets());
    EXPECT_FALSE(third.fields()->linkSizeButton()->isChecked());
    AppOptions::settings().remove(QStringLiteral("newCanvas/linkSize"));
}

TEST(NewCanvasLink, TheDialogAsksWindowsForAFixedFrame) {
    NewCanvasDialog dlg(QStringLiteral("a"), NewCanvasValues{}, defaultCanvasPresets());
    EXPECT_TRUE(dlg.windowFlags() & Qt::MSWindowsFixedSizeDialogHint);
}
