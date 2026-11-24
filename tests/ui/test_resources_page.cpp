#include "core/brush/BrushPack.h"
#include "core/brush/BrushResources.h"
#include "core/document/Document.h"
#include "core/input/TabletStats.h"
#include "core/io/DoodleFormat.h"
#include "core/io/FileIo.h"
#include "core/tools/ToolBox.h"
#include "core/view/ViewNavigator.h"
#include "ui/CanvasView.h"
#include "ui/OptionsDialog.h"
#include "ui/PackImportDialog.h"
#include "ui/ResourcesPage.h"
#include "ui/StrokeTimingReadout.h"
#include "ScreenshotSupport.h"

#include <QComboBox>
#include <QDir>
#include <QDropEvent>
#include <QElapsedTimer>
#include <QListWidget>
#include <QMimeData>
#include <QPointingDevice>
#include <QPushButton>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QTabletEvent>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QUrl>

#include <gtest/gtest.h>

#include <memory>

using namespace doodlebold;

namespace {

// The page with its questions answered by the test.
class AnsweredPage : public ResourcesPage {
public:
    using ResourcesPage::ResourcesPage;
    QStringList files;
    bool removeAnswer = true;
    QStringList removeAsked;
    QStringList toolsShown;  // the tools named when Remove was asked
    std::optional<QString> newName;
    QString opened;
    QString pack;
    std::vector<PackUse> packUses;  // empty: the dialog's own guesses
    bool packAccepted = true;
    QStringList packTitles;

protected:
    QStringList askFiles() override { return files; }
    bool askRemove(const QString& name, const QStringList& tools) override {
        removeAsked << name;
        toolsShown = tools;
        return removeAnswer;
    }
    std::optional<QString> askNewName(const QString&) override { return newName; }
    void openFolder(const QString& path) override { opened = path; }
    QString askPack(bool) override { return pack; }
    bool askPackChoices(PackImportDialog& dialog) override {
        packTitles << dialog.windowTitle();
        for (std::size_t i = 0; i < packUses.size(); ++i)
            dialog.setUse(int(i), packUses[i]);
        return packAccepted;
    }
};

class ResourcesPageTest : public testing::Test {
protected:
    void SetUp() override {
        QStandardPaths::setTestModeEnabled(true);
        test::applyTheme();
        library = std::make_unique<BrushResourceLibrary>(folder.filePath(QStringLiteral("brushes")));
        page = std::make_unique<AnsweredPage>(library.get(), &tools);
        page->resize(720, 560);
        page->show();
    }

    QStringList shown() const {
        QStringList out;
        auto* grid = page->findChild<QListWidget*>(QStringLiteral("resourceGrid"));
        for (int i = 0; i < grid->count(); ++i)
            out << grid->item(i)->text();
        return out;
    }
    QPushButton* button(const char* name) const { return page->findChild<QPushButton*>(QString::fromLatin1(name)); }
    QString picture(const QString& name, int grey) const {
        QImage image(16, 16, QImage::Format_Grayscale8);
        image.fill(grey);
        const QString file = folder.filePath(name + QStringLiteral(".png"));
        EXPECT_TRUE(image.save(file));
        return file;
    }

    // Puts the tool in `ref` to paint with the form and texture named.
    void paintWith(SlotRef ref, const QString& form, const QString& texture) {
        ASSERT_TRUE(tools.selectSlot(ref).ok());
        BrushSettings s = tools.settings();
        s.form = form;
        s.texture = texture;
        tools.setSettings(s);
    }

    QTemporaryDir folder;
    std::unique_ptr<BrushResourceLibrary> library;
    ToolBox tools;
    std::unique_ptr<AnsweredPage> page;
};

}

TEST_F(ResourcesPageTest, ShowsOneKindAtATimeAsAGridOfThumbnails) {
    auto* kinds = page->findChild<QComboBox*>(QStringLiteral("resourceKindCombo"));
    ASSERT_TRUE(kinds);
    QStringList labels;
    for (int i = 0; i < kinds->count(); ++i)
        labels << kinds->itemText(i);
    EXPECT_EQ(labels, (QStringList{"Blot Maps", "Bristles", "Brush Shapes", "Brush Textures", "Paper Textures"}));
    EXPECT_EQ(page->kind(), ResourceKind::BlotMap);
    EXPECT_EQ(shown(), (QStringList{"Blots", "Fine Blots", "Mottled Blots"}));
    page->showKind(ResourceKind::Shape);
    EXPECT_TRUE(shown().contains(QStringLiteral("Soft Edge")));
    EXPECT_FALSE(shown().contains(QStringLiteral("Blots")));
    auto* grid = page->findChild<QListWidget*>(QStringLiteral("resourceGrid"));
    EXPECT_EQ(grid->viewMode(), QListView::IconMode);
    EXPECT_FALSE(grid->item(0)->icon().isNull());
    for (const char* name :
         {"resourceAddButton", "resourceRemoveButton", "resourceRenameButton", "resourcePackButton", "resourceFolderButton"})
        EXPECT_TRUE(button(name)) << name;
    page->showKind(ResourceKind::BlotMap);
    EXPECT_TRUE(test::saveScreenshot(page->grab(), QStringLiteral("options_resources")));
}

TEST_F(ResourcesPageTest, BuiltInsCanBeSelectedButNotRemovedOrRenamed) {
    page->select(QStringLiteral("Blots"));
    EXPECT_EQ(page->selectedName(), QStringLiteral("Blots"));
    EXPECT_FALSE(button("resourceRemoveButton")->isEnabled());
    EXPECT_FALSE(button("resourceRenameButton")->isEnabled());
    page->removeSelected();
    EXPECT_TRUE(page->removeAsked.isEmpty());
    EXPECT_NE(library->form(QStringLiteral("Blots")), nullptr);
}

TEST_F(ResourcesPageTest, AddRemoveAndRenameTakeEffectAtOnce) {
    page->files = {picture(QStringLiteral("Speckles"), 30)};
    button("resourceAddButton")->click();
    EXPECT_TRUE(shown().contains(QStringLiteral("Speckles")));
    EXPECT_EQ(page->selectedName(), QStringLiteral("Speckles"));
    EXPECT_EQ(library->form(QStringLiteral("Speckles"))->kind, ResourceKind::BlotMap);
    EXPECT_TRUE(button("resourceRemoveButton")->isEnabled());

    page->newName = QStringLiteral("Dots");
    button("resourceRenameButton")->click();
    EXPECT_TRUE(shown().contains(QStringLiteral("Dots")));
    EXPECT_FALSE(shown().contains(QStringLiteral("Speckles")));
    EXPECT_NE(library->form(QStringLiteral("Dots")), nullptr);
    EXPECT_EQ(page->message(), QStringLiteral("Renamed Speckles to Dots."));

    page->removeAnswer = false;
    button("resourceRemoveButton")->click();
    EXPECT_EQ(page->removeAsked, QStringList{QStringLiteral("Dots")}) << "asked first";
    EXPECT_NE(library->form(QStringLiteral("Dots")), nullptr) << "and kept on No";
    page->removeAnswer = true;
    button("resourceRemoveButton")->click();
    EXPECT_EQ(library->form(QStringLiteral("Dots")), nullptr);
    EXPECT_FALSE(shown().contains(QStringLiteral("Dots")));
}

TEST_F(ResourcesPageTest, RemovingAPictureToolsUseNamesThemAndPutsThemBackToTheirDefault) {
    constexpr SlotRef kPencil{0, 0}, kBrush{0, 1}, kWater{0, 2};
    page->addFiles({picture(QStringLiteral("Speckles"), 30)});
    paintWith(kPencil, QStringLiteral("Speckles"), BrushResourceLibrary::kNoTexture);
    paintWith(kWater, QStringLiteral("Speckles"), QStringLiteral("Paper Grain"));
    tools.reserveSettings();  // Water Color's reserved copy names it too
    paintWith(kBrush, BrushResourceLibrary::kSimpleCircle, QStringLiteral("Speckles"));  // a texture of that name is another picture
    page->select(QStringLiteral("Speckles"));

    page->removeAnswer = false;
    page->removeSelected();
    EXPECT_EQ(page->toolsShown, (QStringList{tools.slot(kPencil)->name, tools.slot(kWater)->name}));
    EXPECT_EQ(tools.slot(kPencil)->settings.form, QStringLiteral("Speckles")) << "No keeps everything";
    EXPECT_NE(library->form(QStringLiteral("Speckles")), nullptr);

    page->removeAnswer = true;
    page->removeSelected();
    EXPECT_EQ(library->form(QStringLiteral("Speckles")), nullptr);
    EXPECT_EQ(tools.slot(kPencil)->settings.form, defaultSettings(ToolType::Pencil).form);
    EXPECT_EQ(tools.slot(kWater)->settings.form, defaultSettings(ToolType::WaterColor).form);
    EXPECT_EQ(tools.slot(kWater)->settings.texture, QStringLiteral("Paper Grain")) << "only the removed picture goes";
    ASSERT_TRUE(tools.slot(kWater)->reserved);
    EXPECT_EQ(tools.slot(kWater)->reserved->form, defaultSettings(ToolType::WaterColor).form);
    EXPECT_EQ(tools.slot(kBrush)->settings.texture, QStringLiteral("Speckles"));
    EXPECT_EQ(page->message(), QStringLiteral("Removed Speckles. 2 tools went back to their default."));
}

TEST_F(ResourcesPageTest, RemovingATextureToolsUsePutsTheirDefaultTextureBack) {
    page->showKind(ResourceKind::Texture);
    page->addFiles({picture(QStringLiteral("Grit"), 90)});
    paintWith({0, 1}, BrushResourceLibrary::kSimpleCircle, QStringLiteral("Grit"));
    page->select(QStringLiteral("Grit"));
    page->removeSelected();
    EXPECT_EQ(page->toolsShown, QStringList{tools.slot({0, 1})->name});
    EXPECT_EQ(tools.settings().texture, defaultSettings(ToolType::Brush).texture);

    page->addFiles({picture(QStringLiteral("Unused"), 90)});
    page->select(QStringLiteral("Unused"));
    page->removeSelected();
    EXPECT_TRUE(page->toolsShown.isEmpty()) << "a picture no tool uses is only asked about";
    EXPECT_EQ(page->message(), QStringLiteral("Removed Unused."));
}

TEST_F(ResourcesPageTest, RemovingAPaperTextureNamesNoTools) {
    page->showKind(ResourceKind::PaperTexture);
    page->addFiles({picture(QStringLiteral("Grit"), 90)});
    paintWith({0, 1}, BrushResourceLibrary::kSimpleCircle, QStringLiteral("Grit"));  // the brush texture list has no Grit
    page->select(QStringLiteral("Grit"));
    page->removeSelected();
    EXPECT_TRUE(page->toolsShown.isEmpty()) << "papers belong to layers";
    EXPECT_EQ(tools.settings().texture, QStringLiteral("Grit"));
}

TEST_F(ResourcesPageTest, AFileOfABuiltInsNameReplacesIt) {
    page->addFiles({picture(QStringLiteral("Blots"), 70)});
    EXPECT_EQ(shown().count(QStringLiteral("Blots")), 1);
    EXPECT_FALSE(library->form(QStringLiteral("Blots"))->builtIn);
    page->select(QStringLiteral("Blots"));
    EXPECT_TRUE(button("resourceRemoveButton")->isEnabled()) << "the file is the user's";
}

TEST_F(ResourcesPageTest, DroppedImageFilesAreAddedAndOtherFilesIgnored) {
    page->showKind(ResourceKind::PaperTexture);
    const std::size_t builtIn = library->ofKind(ResourceKind::PaperTexture).size();
    QMimeData mime;
    QFile text(folder.filePath(QStringLiteral("notes.txt")));
    ASSERT_TRUE(text.open(QIODevice::WriteOnly));
    text.write("x");
    text.close();
    mime.setUrls({QUrl::fromLocalFile(picture(QStringLiteral("Rough Sheet"), 90)), QUrl::fromLocalFile(text.fileName())});
    auto* grid = page->findChild<QListWidget*>(QStringLiteral("resourceGrid"));
    const QPointF at = grid->mapTo(page.get(), grid->rect().center());
    QDragEnterEvent enter(at.toPoint(), Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
    QCoreApplication::sendEvent(page.get(), &enter);
    EXPECT_TRUE(enter.isAccepted());
    QDropEvent drop(at, Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
    QCoreApplication::sendEvent(page.get(), &drop);
    EXPECT_NE(library->paperTexture(QStringLiteral("Rough Sheet")), nullptr);
    EXPECT_EQ(library->ofKind(ResourceKind::PaperTexture).size(), builtIn + 1) << "the picture, not the text file";
}

TEST_F(ResourcesPageTest, OpenFolderOpensThatKindsFolder) {
    page->showKind(ResourceKind::Bristle);
    button("resourceFolderButton")->click();
    EXPECT_EQ(page->opened, library->kindFolder(ResourceKind::Bristle));
    EXPECT_TRUE(QDir(page->opened).exists()) << "made if it wasn't there";
}

TEST_F(ResourcesPageTest, ImportPackAsksWhatToKeepAndAddsIt) {
    const QString pack = folder.filePath(QStringLiteral("Test Pack"));
    QDir().mkpath(pack + QStringLiteral("/blotmap"));
    QDir().mkpath(pack + QStringLiteral("/misc"));
    QImage image(16, 16, QImage::Format_Grayscale8);
    image.fill(20);
    ASSERT_TRUE(image.save(pack + QStringLiteral("/blotmap/Splash.png")));
    ASSERT_TRUE(image.save(pack + QStringLiteral("/misc/Leaf.png")));

    page->packAccepted = false;
    page->importPack(pack);
    EXPECT_EQ(page->packTitles, QStringList{QStringLiteral("Install \"Test Pack\"")});
    EXPECT_EQ(library->form(QStringLiteral("Splash")), nullptr) << "Cancel installs nothing";

    page->packAccepted = true;
    page->packUses = {PackUse::BlotMap, PackUse::Shape};  // misc ticked as brush shapes
    page->importPack(pack);
    EXPECT_NE(library->form(QStringLiteral("Splash")), nullptr);
    EXPECT_EQ(library->form(QStringLiteral("Leaf"))->kind, ResourceKind::Shape);
    EXPECT_EQ(page->message(), QStringLiteral("Added 2 picture(s) from Test Pack."));
    EXPECT_TRUE(shown().contains(QStringLiteral("Splash")));

    page->importPack(folder.filePath(QStringLiteral("nothing.zip")));
    EXPECT_TRUE(page->message().startsWith(QStringLiteral("nothing.zip could not be imported.")));
}

TEST_F(ResourcesPageTest, ThePackDialogShowsFoldersTicksAndWhatWillBeInstalled) {
    BrushPack pack;
    pack.name = QStringLiteral("Test Pack");
    PackFolder blots = guessFolder(QStringLiteral("blotmap"));
    blots.pictures = {QStringLiteral("blotmap/a.png"), QStringLiteral("blotmap/b.png")};
    PackFolder textures = guessFolder(QStringLiteral("Textures"));
    textures.pictures = {QStringLiteral("Textures/t.png")};
    PackFolder other = guessFolder(QStringLiteral("scatter"));
    other.pictures = {QStringLiteral("scatter/s.png")};
    pack.folders = {blots, textures, other};
    pack.leftOut = {QStringLiteral("readme.txt")};
    PackImportDialog dialog(pack);
    EXPECT_EQ(dialog.windowTitle(), QStringLiteral("Install \"Test Pack\""));
    EXPECT_EQ(dialog.uses(), (std::vector<PackUse>{PackUse::BlotMap, PackUse::TextureAndPaper, PackUse::Skip}));
    EXPECT_EQ(dialog.summary(), QStringLiteral("Will install 2 blot map(s), 1 brush texture(s), 1 paper texture(s)."));
    auto* table = dialog.findChild<QTableWidget*>(QStringLiteral("packFolders"));
    ASSERT_TRUE(table);
    EXPECT_EQ(table->item(2, PackImportDialog::FolderColumn)->checkState(), Qt::Unchecked);
    EXPECT_EQ(table->item(1, PackImportDialog::ResultColumn)->text(), QStringLiteral("2 new"));
    EXPECT_EQ(table->item(2, PackImportDialog::ResultColumn)->text(), QStringLiteral("1 left out"));

    dialog.setKept(2, true);
    EXPECT_EQ(dialog.uses()[2], PackUse::Shape) << "ticked, it becomes brush shapes";
    dialog.setKept(0, false);
    EXPECT_EQ(dialog.uses()[0], PackUse::Skip);
    table->item(0, PackImportDialog::FolderColumn)->setCheckState(Qt::Checked);
    EXPECT_EQ(dialog.uses()[0], PackUse::BlotMap) << "ticked again by hand, with its choice kept";
    dialog.setUse(1, PackUse::Skip);
    EXPECT_EQ(table->item(1, PackImportDialog::FolderColumn)->checkState(), Qt::Unchecked);
    dialog.resize(840, 560);
    EXPECT_TRUE(test::saveScreenshot(dialog.grab(), QStringLiteral("pack_import")));
}

TEST_F(ResourcesPageTest, TheOptionsDialogHasAResourcesPage) {
    OptionsDialog dialog(AppOptions::Values{});
    dialog.addResourcesPage(library.get());
    auto* pages = dialog.findChild<QListWidget*>(QStringLiteral("optionsPages"));
    int row = -1;
    for (int i = 0; i < pages->count(); ++i)
        if (pages->item(i)->text() == QStringLiteral("Resources"))
            row = i;
    ASSERT_GE(row, 0);
    pages->setCurrentRow(row);
    auto* stack = dialog.findChild<QStackedWidget*>();
    EXPECT_TRUE(qobject_cast<ResourcesPage*>(stack->currentWidget()));
    EXPECT_EQ(pages->item(row + 1)->text(), QStringLiteral("Timelapse"));
}

namespace {

// A canvas painting with a picture of the library, which the Resources page then removes.
class RemovedPictureTest : public testing::Test {
protected:
    void SetUp() override {
        QStandardPaths::setTestModeEnabled(true);
        test::applyTheme();
        library = std::make_unique<BrushResourceLibrary>(folder.filePath(QStringLiteral("brushes")), BrushResourceLibrary::BuiltIns::Left);
        RebuiltStroke::setResources(library.get());
        canvas = std::make_unique<CanvasView>(&doc, &stats);
        canvas->setToolBox(&tools);
        canvas->resize(800, 500);
        canvas->show();
        QCoreApplication::processEvents();
        canvas->resetView();
        page = std::make_unique<AnsweredPage>(library.get(), &tools);
        blank = doc.projection();
    }
    void TearDown() override { RebuiltStroke::setResources(nullptr); }

    // A blot map and a texture of stripes, and the current tool painting with both.
    void useStripes() {
        QImage stripes(64, 64, QImage::Format_Grayscale8);
        for (int y = 0; y < 64; ++y)
            for (int x = 0; x < 64; ++x)
                stripes.setPixel(x, y, ((x + y) / 6) % 2 ? qRgb(0, 0, 0) : qRgb(255, 255, 255));
        const QString file = folder.filePath(QStringLiteral("Stripes.png"));
        ASSERT_TRUE(stripes.save(file));
        page->addFiles({file});
        page->showKind(ResourceKind::Texture);
        page->addFiles({file});
        BrushSettings s = tools.settings();
        s.size = 300;
        s.form = s.texture = QStringLiteral("Stripes");
        s.textureInvert = true;  // both lie on the canvas: the form keeps the dark stripes, so the paper must let them through
        tools.setSettings(s);
    }
    void remove(ResourceKind kind) {
        page->showKind(kind);
        page->select(QStringLiteral("Stripes"));
        page->removeSelected();
    }
    void pen(QEvent::Type type, const QPointF& docPos) {
        const QPointF at = ViewNavigator(doc.view(), QSizeF(canvas->size()), QSizeF(doc.size())).docToWidget().map(docPos);
        QTabletEvent event(type, QPointingDevice::primaryPointingDevice(), at, at, type == QEvent::TabletRelease ? 0.0 : 0.7, 0.0, 0.0, 0.0,
                           0.0, 0.0, Qt::NoModifier, Qt::LeftButton, type == QEvent::TabletRelease ? Qt::NoButton : Qt::LeftButton);
        QCoreApplication::sendEvent(canvas.get(), &event);
    }
    void moves(double y, int from, int to) {
        for (int i = from; i <= to; ++i)
            pen(QEvent::TabletMove, QPointF(60 + i * 12.0, y));
    }
    void line(double y) {
        pen(QEvent::TabletPress, QPointF(60, y));
        moves(y, 1, 40);
        pen(QEvent::TabletRelease, QPointF(540, y));
        waitUntilIdle();
    }
    void waitUntilIdle() {
        QElapsedTimer clock;
        clock.start();
        while (canvas->isPainting() && clock.elapsed() < 20000)
            QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
    }

    QTemporaryDir folder;
    std::unique_ptr<BrushResourceLibrary> library;
    TabletStats stats;
    Document doc{QSize(600, 300), Qt::white};
    ToolBox tools;
    std::unique_ptr<CanvasView> canvas;
    std::unique_ptr<AnsweredPage> page;
    QImage blank;
};

}

// The report: a picture a tool paints with, removed in the manager. Painting
// with the tool afterwards, undo and redo, and saving and reopening the canvas all go on as before.
TEST_F(RemovedPictureTest, PaintingUndoAndSavingGoOnAfterThePictureIsRemoved) {
    useStripes();
    line(60);
    const QImage striped = doc.projection();
    EXPECT_NE(striped, blank);

    pen(QEvent::TabletPress, QPointF(60, 140));  // removed while a stroke paints with it
    moves(140, 1, 20);
    remove(ResourceKind::BlotMap);
    remove(ResourceKind::Texture);
    moves(140, 21, 40);
    pen(QEvent::TabletRelease, QPointF(540, 140));
    waitUntilIdle();
    EXPECT_EQ(page->toolsShown, QStringList{tools.currentTool().name});
    EXPECT_EQ(library->form(QStringLiteral("Stripes")), nullptr);
    EXPECT_EQ(tools.settings().form, defaultSettings(tools.settings().tool).form);
    EXPECT_EQ(tools.settings().texture, defaultSettings(tools.settings().tool).texture);

    line(220);  // with the default form and texture now
    const QImage painted = doc.projection();
    EXPECT_NE(painted, striped);
    for (int i = 0; i < 3; ++i)
        EXPECT_TRUE(doc.undo().ok());
    EXPECT_EQ(doc.projection(), blank);
    for (int i = 0; i < 3; ++i)
        EXPECT_TRUE(doc.redo().ok());
    EXPECT_EQ(doc.projection(), painted);

    const QString file = folder.filePath(QStringLiteral("after removal.doodle"));
    ASSERT_TRUE(io::saveFile(doc.content(), file, DoodleFormat()).ok());
    auto loaded = io::loadFile(file);
    ASSERT_TRUE(loaded.ok());
    Document reopened(loaded.value().content);
    EXPECT_EQ(reopened.projection(), painted);
}

// A tool can still name a picture that's gone: its file deleted outside Doodlebold, or a saved tray from before. It still paints,
// with the round tip and no texture, and its strokes undo and save.
TEST_F(RemovedPictureTest, AToolNamingAMissingPictureStillPaints) {
    useStripes();
    ASSERT_TRUE(QFile::remove(library->form(QStringLiteral("Stripes"))->file));
    ASSERT_TRUE(QFile::remove(library->texture(QStringLiteral("Stripes"))->file));
    library->reload();
    ASSERT_EQ(library->form(QStringLiteral("Stripes")), nullptr);
    line(150);
    const QImage painted = doc.projection();
    EXPECT_NE(painted, blank);
    EXPECT_EQ(tools.settings().form, QStringLiteral("Stripes")) << "nothing removed it in the manager";

    const QString file = folder.filePath(QStringLiteral("missing.doodle"));
    ASSERT_TRUE(io::saveFile(doc.content(), file, DoodleFormat()).ok());
    auto loaded = io::loadFile(file);
    ASSERT_TRUE(loaded.ok());
    EXPECT_EQ(Document(loaded.value().content).projection(), painted);
    ASSERT_TRUE(doc.undo().ok());
    EXPECT_EQ(doc.projection(), blank);
}

TEST(StrokeTimingReadout, AveragesAndWorstOfAStroke) {
    StrokeTimingReadout readout;
    EXPECT_TRUE(readout.isEmpty());
    readout.eventHandled(400);
    readout.eventHandled(1200);
    readout.inkShown(8000);
    readout.inkShown(16000);
    EXPECT_FALSE(readout.isEmpty());
    EXPECT_EQ(readout.text(), QStringLiteral("Pen to screen: 12.0 ms on average, 16.0 ms at worst\n"
                                             "Each pen event: 0.8 ms on average, 1.2 ms at worst"));
    readout.startStroke();
    EXPECT_TRUE(readout.isEmpty());
    {
        const StrokeTimingReadout::EventScope timed(&readout);
    }
    EXPECT_FALSE(readout.isEmpty());
    EXPECT_TRUE(readout.text().startsWith(QStringLiteral("Pen to screen: not yet")));
    const StrokeTimingReadout::EventScope off(nullptr);
}
