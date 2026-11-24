// The Bucket on the canvas and in the tool panel, and Options › Keys.
#include "core/document/Document.h"
#include "core/input/TabletStats.h"
#include "core/tools/ToolBox.h"
#include "core/tools/ToolBoxStore.h"
#include "ui/CanvasView.h"
#include "ui/DocumentTabs.h"
#include "ui/KeysPage.h"
#include "ui/MainWindow.h"
#include "ui/OptionsDialog.h"
#include "ui/ValueBar.h"
#include "ui/panels/BrushPreviewStrip.h"
#include "ui/panels/BucketPanel.h"
#include "ui/panels/ToolPanel.h"
#include "ScreenshotSupport.h"

#include <QAbstractButton>
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QSpinBox>
#include <QSlider>
#include <QScrollBar>
#include <QScrollArea>
#include <QFile>
#include <QKeyEvent>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QMouseEvent>
#include <QRadioButton>
#include <QSettings>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTimer>
#include <QWheelEvent>

#include <gtest/gtest.h>

#include <memory>

using namespace doodlebold;

namespace {

constexpr int kOne = kChannelOne;
const QSize kDoc(200, 150);
constexpr SlotRef kBucketSlot{0, 5};

template <typename T> T* child(QObject* parent, const char* name) {
    T* w = parent->findChild<T*>(QString::fromLatin1(name));
    EXPECT_NE(w, nullptr) << name;
    return w;
}

// A canvas of 800 × 600 on a 200 × 150 document at 100 %, centred: document point d is at widget point d + (300, 225).
struct BucketRig {
    BucketRig() : doc(kDoc, Qt::white), canvas(&doc, &stats) {
        canvas.setToolBox(&tools);
        canvas.resize(800, 600);
        canvas.resetView();
        EXPECT_TRUE(tools.selectSlot(kBucketSlot).ok());
        BrushSettings s = tools.settings();
        s.bucket.mode = selection::WandMode::EnclosedTransparent;
        s.bucket.mergedSource = false;
        s.bucket.antialias = false;  // exact pixels
        tools.setSettings(s);
        tools.setColor(Qt::red);
        // A black outline round (40, 30) … (119, 89) on the layer.
        for (const QRect& r : {QRect(40, 30, 80, 2), QRect(40, 88, 80, 2), QRect(40, 30, 2, 60), QRect(118, 30, 2, 60)})
            doc.paintTarget(doc.currentLayerId()).value()->fill(r, Qt::black);
        doc.notifyPixelsChanged(doc.currentLayerId(), doc.rect());
    }

    void click(const QPointF& at) {
        const QPointF widget = at + QPointF(300, 225);
        for (const QEvent::Type type : {QEvent::MouseButtonPress, QEvent::MouseButtonRelease}) {
            QMouseEvent e(type, widget, widget, Qt::LeftButton, type == QEvent::MouseButtonPress ? Qt::LeftButton : Qt::NoButton,
                          Qt::NoModifier);
            QCoreApplication::sendEvent(&canvas, &e);
        }
    }
    std::array<std::uint8_t, 4> pixel(int x, int y) const { return toUnorm8(doc.currentLayer()->pixels.pixel(x, y)); }
    void setBucket(const std::function<void(BucketSettings&)>& change) {
        BrushSettings s = tools.settings();
        change(s.bucket);
        tools.setSettings(s);
    }

    TabletStats stats;
    Document doc;
    ToolBox tools;
    CanvasView canvas;
};

constexpr std::array<std::uint8_t, 4> kRed{255, 0, 0, 255};

class BucketTest : public testing::Test {
protected:
    void SetUp() override { test::applyTheme(); }
};

}

TEST_F(BucketTest, ItSitsInTheFirstGroupWhereTheBlurWas) {
    ToolBox tools;
    EXPECT_EQ(tools.slot(kBucketSlot)->type(), ToolType::Bucket);
    EXPECT_EQ(tools.slot(kBucketSlot)->name, QStringLiteral("Bucket"));
    EXPECT_EQ(tools.slot({0, 9})->type(), ToolType::Blur) << "the Blur comes after the Clone Stamp";
}

TEST_F(BucketTest, ThePanelOffersItsChoicesAndEditsTheTool) {
    ToolBox tools;
    ToolPanel panel(&tools);
    panel.resize(320, 760);
    panel.show();
    ASSERT_TRUE(tools.selectSlot(kBucketSlot).ok());
    auto* options = panel.bucketPanel()->parentWidget();
    ASSERT_NE(qobject_cast<QStackedWidget*>(options), nullptr);
    EXPECT_EQ(qobject_cast<QStackedWidget*>(options)->currentWidget(), panel.bucketPanel());
    EXPECT_FALSE(panel.preview()->isVisible()) << "the Bucket paints no stroke to preview";

    QStringList texts;
    for (const QAbstractButton* b : panel.bucketPanel()->findChildren<QAbstractButton*>())
        texts << b->text();
    for (const QLabel* l : panel.bucketPanel()->findChildren<QLabel*>())
        texts << l->text();
    for (const char* text :
         {"Selection Mode", "Enclosed Transparent Area", "Similar Colored Area", "All Similar Colored Pixels", "Color Threshold",
          "Gap Tolerance", "Selection Source", "Current Layer", "Merged Image", "Anti-Aliasing", "Ignore Existing Selection"})
        EXPECT_TRUE(texts.contains(QString::fromLatin1(text))) << text;

    child<QRadioButton>(panel.bucketPanel(), "bucket_allSimilar")->click();
    child<ValueBar>(panel.bucketPanel(), "bucketThresholdBar")->setValue(70);
    child<ValueBar>(panel.bucketPanel(), "bucketGapBar")->setValue(5);
    child<QRadioButton>(panel.bucketPanel(), "bucket_sourceCurrent")->click();
    child<QCheckBox>(panel.bucketPanel(), "bucketIgnoreSelectionCheck")->click();
    const BucketSettings& b = tools.settings().bucket;
    EXPECT_EQ(b.mode, selection::WandMode::AllSimilar);
    EXPECT_EQ(b.colourThreshold, 70);
    EXPECT_EQ(b.gapTolerance, 5);
    EXPECT_FALSE(b.mergedSource);
    EXPECT_TRUE(b.ignoreSelection);
    EXPECT_TRUE(test::saveScreenshot(panel.grab(), QStringLiteral("tool_panel_bucket")));

    ASSERT_TRUE(tools.selectSlot({0, 0}).ok());
    EXPECT_NE(qobject_cast<QStackedWidget*>(options)->currentWidget(), panel.bucketPanel());
    EXPECT_TRUE(panel.preview()->isVisible());
    ASSERT_TRUE(tools.selectSlot(kBucketSlot).ok());
    EXPECT_TRUE(child<QRadioButton>(panel.bucketPanel(), "bucket_allSimilar")->isChecked()) << "the panel follows the tool";
}

TEST_F(BucketTest, AClickFillsTheAreaWithTheMainColourAsOneStep) {
    BucketRig r;
    const int steps = r.doc.undoStack()->count();
    r.click({80.5, 60.5});
    EXPECT_EQ(r.doc.undoStack()->count(), steps + 1);
    EXPECT_EQ(r.pixel(80, 60), kRed);
    EXPECT_EQ(r.pixel(42, 32), kRed) << "up to the line";
    EXPECT_EQ(r.pixel(41, 60), (std::array<std::uint8_t, 4>{0, 0, 0, 255})) << "the line stays";
    EXPECT_EQ(r.pixel(10, 10)[3], 0) << "outside the line";
    ASSERT_TRUE(r.doc.undo().ok());
    EXPECT_EQ(r.pixel(80, 60)[3], 0);
}

TEST_F(BucketTest, ASelectionBoundsTheFillUnlessItIsIgnored) {
    BucketRig r;
    ASSERT_TRUE(r.doc.setSelection(selection::rectangle(kDoc, QRectF(60, 40, 20, 20), false)).ok());
    r.click({100.5, 80.5});  // inside the outline, outside the selection
    EXPECT_EQ(r.pixel(100, 80)[3], 0) << "the fill stays inside the selection";
    EXPECT_EQ(r.pixel(70, 50), kRed);
    EXPECT_EQ(r.doc.selection()->at(70, 50), kOne) << "the selection stays";
    ASSERT_TRUE(r.doc.undo().ok());

    r.setBucket([](BucketSettings& b) { b.ignoreSelection = true; });
    r.click({100.5, 80.5});
    EXPECT_EQ(r.pixel(100, 80), kRed);
    EXPECT_EQ(r.pixel(70, 50), kRed);
}

TEST_F(BucketTest, MergedImageFindsTheAreaInWhatShowsAndFillsTheCurrentLayer) {
    BucketRig r;
    const LayerId lines = r.doc.currentLayerId();
    ASSERT_TRUE(r.doc.addLayer().ok());
    r.setBucket([](BucketSettings& b) {
        b.mergedSource = true;
        b.mode = selection::WandMode::SimilarColour;
    });
    r.click({80.5, 60.5});
    EXPECT_EQ(r.pixel(80, 60), kRed) << "the new, empty layer is filled";
    EXPECT_EQ(r.pixel(10, 10)[3], 0) << "only inside the lines of the layer below";
    EXPECT_EQ(r.doc.layer(lines)->pixels.pixel(80, 60)[3], 0);
}

TEST_F(BucketTest, ALockedLayerRefusesWithAMessage) {
    BucketRig r;
    LayerProps props = r.doc.currentLayer()->props;
    props.locked = true;
    ASSERT_TRUE(r.doc.setLayerProps(r.doc.currentLayerId(), props).ok());
    int messages = 0;
    QObject::connect(&r.canvas, &CanvasView::message, [&messages] { ++messages; });
    const int steps = r.doc.undoStack()->count();
    r.click({80.5, 60.5});
    EXPECT_EQ(messages, 1);
    EXPECT_EQ(r.doc.undoStack()->count(), steps);
}

namespace {

class KeysTest : public testing::Test {
protected:
    void SetUp() override {
        QStandardPaths::setTestModeEnabled(true);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDir.path());
        test::applyTheme();
        QFile::remove(ToolBoxStore::defaultPath());
        window = std::make_unique<MainWindow>();
        window->newDocument();
        window->show();
        QCoreApplication::processEvents();
    }
    void TearDown() override {
        window.reset();
        QFile::remove(ToolBoxStore::defaultPath());
    }

    // The dialog deletes itself once closed: the one shown is the new one.
    OptionsDialog* openOptions() {
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        window->command(QStringLiteral("other.options"))->trigger();
        for (OptionsDialog* d : window->findChildren<OptionsDialog*>())
            if (d->isVisible())
                return d;
        return nullptr;
    }
    static void press(QWidget* to, int key, Qt::KeyboardModifiers mods = {}) {
        QKeyEvent e(QEvent::KeyPress, key, mods);
        QCoreApplication::sendEvent(to, &e);
    }
    // Answers the next "Key Already Used" question with `button` (keysOverwrite or keysRevert) once it shows; `asked` counts it.
    void answerTaken(const char* button) {
        QTimer::singleShot(0, [this, button] {
            auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
            if (!box || box->objectName() != QStringLiteral("keysTakenDialog"))
                return;
            ++asked;
            lastQuestion = box->text();
            box->findChild<QAbstractButton*>(QString::fromLatin1(button))->click();
        });
    }
    static QStringList rowTexts(const KeysPage* page) {
        QStringList texts;
        const auto* list = page->findChild<QListWidget*>(QStringLiteral("keysList"));
        for (int i = 0; i < list->count(); ++i)
            texts << list->item(i)->text();
        return texts;
    }
    static QList<QKeySequence> keysOf(const KeysPage* page, const char* id) {
        return page->table().row(page->table().find(QString::fromLatin1(id))).keys;
    }

    QTemporaryDir settingsDir;
    std::unique_ptr<MainWindow> window;
    int asked = 0;
    QString lastQuestion;
};

const QKeySequence kCtrlN(QStringLiteral("Ctrl+N"));

}

TEST_F(KeysTest, ThePageListsEveryCommandAndTrayToolUnderHeadings) {
    OptionsDialog* dlg = openOptions();
    ASSERT_NE(dlg, nullptr);
    KeysPage* page = dlg->keysPage();
    const QStringList texts = rowTexts(page);
    for (const char* text : {"File", "New", "Open", "Save As", "Edit", "Undo", "Layer", "Tools", "Tray: Basic"})
        EXPECT_TRUE(texts.contains(QString::fromLatin1(text))) << text;
    EXPECT_LT(texts.indexOf(QStringLiteral("File")), texts.indexOf(QStringLiteral("New")));
    EXPECT_EQ(keysOf(page, "file.new"), QList<QKeySequence>{kCtrlN});
    // Every tool has a command, those without a key at the start too.
    for (const char* id : {"tool.marker", "tool.pixelArtPen", "tool.bucket", "tool.blur", "tool.smudge", "tool.text", "tool.move",
                           "tool.zoom", "tool.rotate", "tool.hand", "tool.eyedropper"}) {
        const int row = page->table().find(QString::fromLatin1(id));
        ASSERT_GE(row, 0) << id;
        EXPECT_EQ(page->table().row(row).group, QStringLiteral("Tools")) << id;
        EXPECT_TRUE(page->table().row(row).keys.isEmpty()) << id;
    }
    const int pencil = page->table().find(trayRowId(0, 0));
    ASSERT_GE(pencil, 0);
    EXPECT_EQ(page->table().row(pencil).label, QStringLiteral("Pencil"));

    auto* search = child<QLineEdit>(page, "keysSearch");
    search->setText(QStringLiteral("save"));
    const auto* list = page->findChild<QListWidget*>(QStringLiteral("keysList"));
    int shown = 0;
    for (int i = 0; i < list->count(); ++i)
        shown += !list->item(i)->isHidden();
    EXPECT_EQ(shown, 3) << "the File heading, Save and Save As";
    search->clear();
    dlg->resize(960, 640);
    child<QListWidget>(dlg, "optionsPages")->setCurrentRow(4);
    EXPECT_TRUE(test::saveScreenshot(dlg->grab(), QStringLiteral("options_keys")));
    dlg->reject();
}

TEST_F(KeysTest, AKeyPressedMovesToTheRowAndANoteSaysFromWhere) {
    OptionsDialog* dlg = openOptions();
    KeysPage* page = dlg->keysPage();
    page->selectRow(QStringLiteral("file.open"));
    auto* capture = child<KeyCaptureEdit>(page, "keysCapture");
    answerTaken("keysOverwrite");
    press(capture, Qt::Key_N, Qt::ControlModifier);
    EXPECT_EQ(asked, 1);
    EXPECT_EQ(keysOf(page, "file.open"), QList<QKeySequence>{kCtrlN});
    EXPECT_TRUE(keysOf(page, "file.new").isEmpty());
    EXPECT_TRUE(page->note().contains(QStringLiteral("New"))) << page->note().toStdString();

    press(capture, Qt::Key_Shift);  // a modifier alone is no key
    EXPECT_EQ(keysOf(page, "file.open"), QList<QKeySequence>{kCtrlN});

    child<QAbstractButton>(page, "keysClear")->click();
    EXPECT_TRUE(keysOf(page, "file.open").isEmpty());
    child<QAbstractButton>(page, "keysDefault")->click();
    EXPECT_EQ(keysOf(page, "file.open"), QList<QKeySequence>{QKeySequence(QStringLiteral("Ctrl+O"))});
    EXPECT_TRUE(keysOf(page, "file.new").isEmpty()) << "Default gives back only this row's key";

    page->selectRow(QStringLiteral("file.new"));
    child<QAbstractButton>(page, "keysDefault")->click();
    EXPECT_EQ(keysOf(page, "file.new"), QList<QKeySequence>{kCtrlN});

    page->selectRow(QStringLiteral("edit.undo"));
    press(capture, Qt::Key_F7);
    child<QAbstractButton>(page, "keysRestoreAll")->click();
    EXPECT_EQ(keysOf(page, "edit.undo"), QList<QKeySequence>{QKeySequence(QStringLiteral("Ctrl+Z"))});
    dlg->reject();
}

TEST_F(KeysTest, OkGivesTheWindowTheKeysAndTheyAreKept) {
    OptionsDialog* dlg = openOptions();
    KeysPage* page = dlg->keysPage();
    page->selectRow(QStringLiteral("file.open"));
    press(child<KeyCaptureEdit>(page, "keysCapture"), Qt::Key_F5);
    page->selectRow(QStringLiteral("tool.bucket"));
    answerTaken("keysOverwrite");
    press(child<KeyCaptureEdit>(page, "keysCapture"), Qt::Key_N, Qt::ControlModifier);
    dlg->accept();
    QCoreApplication::processEvents();

    const QKeySequence f5(QStringLiteral("F5"));
    EXPECT_EQ(window->command(QStringLiteral("file.open"))->shortcuts(), QList<QKeySequence>{f5});
    EXPECT_TRUE(window->command(QStringLiteral("file.open"))->text().contains(f5.toString(QKeySequence::NativeText)));
    EXPECT_TRUE(window->command(QStringLiteral("file.new"))->shortcuts().isEmpty());
    EXPECT_FALSE(window->command(QStringLiteral("file.new"))->text().contains(QLatin1Char('\t')));
    EXPECT_EQ(window->command(QStringLiteral("tool.bucket"))->shortcuts(), QList<QKeySequence>{kCtrlN});

    window->command(QStringLiteral("tool.bucket"))->trigger();
    EXPECT_EQ(window->tools()->currentTool().type(), ToolType::Bucket);

    window.reset();
    window = std::make_unique<MainWindow>();
    EXPECT_EQ(window->command(QStringLiteral("file.open"))->shortcuts(), QList<QKeySequence>{f5}) << "kept for the next start";
    EXPECT_TRUE(window->command(QStringLiteral("file.new"))->shortcuts().isEmpty());
    EXPECT_EQ(window->command(QStringLiteral("edit.undo"))->shortcuts(), QList<QKeySequence>{QKeySequence(QStringLiteral("Ctrl+Z"))});
}

TEST_F(KeysTest, ATrayToolsKeyPicksItAndStaysThroughARename) {
    ToolBox* tools = window->tools();
    ASSERT_TRUE(tools->createSlot({1, 0}, ToolType::Marker).ok());
    ASSERT_TRUE(tools->renameSlot({1, 0}, QStringLiteral("My Marker")).ok());
    OptionsDialog* dlg = openOptions();
    KeysPage* page = dlg->keysPage();
    ASSERT_GE(page->table().find(trayRowId(1, 0)), 0);
    EXPECT_EQ(page->table().row(page->table().find(trayRowId(1, 0))).group, QStringLiteral("Tray: Custom"));
    page->selectRow(trayRowId(1, 0));
    press(child<KeyCaptureEdit>(page, "keysCapture"), Qt::Key_K);
    dlg->accept();

    EXPECT_EQ(tools->slot({1, 0})->key, QStringLiteral("K"));
    ASSERT_TRUE(tools->renameSlot({1, 0}, QStringLiteral("Felt Tip")).ok());
    EXPECT_EQ(tools->slot({1, 0})->key, QStringLiteral("K"));
    auto* key = window->findChild<QAction*>(trayRowId(1, 0));
    ASSERT_NE(key, nullptr);
    EXPECT_EQ(key->shortcut(), QKeySequence(Qt::Key_K));
    ASSERT_TRUE(tools->selectSlot({0, 0}).ok());
    key->trigger();
    EXPECT_EQ(tools->currentSlot(), (SlotRef{1, 0}));

    OptionsDialog* again = openOptions();
    const int row = again->keysPage()->table().find(trayRowId(1, 0));
    EXPECT_EQ(again->keysPage()->table().row(row).label, QStringLiteral("Felt Tip"));
    EXPECT_EQ(again->keysPage()->table().row(row).keys, QList<QKeySequence>{QKeySequence(Qt::Key_K)});
    again->reject();
}

TEST_F(KeysTest, HeldKeysSwapAndReachTheCanvas) {
    OptionsDialog* dlg = openOptions();
    KeysPage* page = dlg->keysPage();
    auto* zoom = child<QComboBox>(page, "heldZoomKey");
    auto* rotate = child<QComboBox>(page, "heldRotateKey");
    auto* pick = child<QComboBox>(page, "heldPickKey");
    auto* move = child<QComboBox>(page, "heldMoveKey");
    EXPECT_EQ(zoom->currentText(), QStringLiteral("Ctrl"));
    EXPECT_EQ(rotate->currentText(), QStringLiteral("Alt"));
    const auto choose = [this](QComboBox* combo, const char* text, const char* answer) {
        answerTaken(answer);
        combo->setCurrentIndex(combo->findText(QString::fromLatin1(text)));
        emit combo->activated(combo->currentIndex());
    };
    choose(zoom, "Alt", "keysRevert");
    EXPECT_EQ(asked, 1) << "Rotate has Alt";
    EXPECT_EQ(zoom->currentText(), QStringLiteral("Ctrl")) << "Revert keeps both";
    EXPECT_EQ(rotate->currentText(), QStringLiteral("Alt"));
    choose(zoom, "Alt", "keysOverwrite");
    EXPECT_EQ(rotate->currentText(), QStringLiteral("Ctrl")) << "the two swap";
    choose(pick, "Ctrl", "keysOverwrite");
    EXPECT_EQ(asked, 3);
    EXPECT_EQ(move->currentText(), QStringLiteral("Alt"));
    press(child<KeyCaptureEdit>(page, "heldPanKey"), Qt::Key_Z, Qt::ControlModifier);
    const AppOptions::Values v = dlg->values();
    EXPECT_EQ(v.held.zoom, Qt::AltModifier);
    EXPECT_EQ(v.held.rotate, Qt::ControlModifier);
    EXPECT_EQ(v.held.pick, Qt::ControlModifier);
    EXPECT_EQ(v.held.move, Qt::AltModifier);
    EXPECT_EQ(v.held.pan, int(Qt::Key_Z)) << "held on its own: the modifier is dropped";
    EXPECT_TRUE(v.altPicksColour);
    move->setCurrentIndex(move->findText(QStringLiteral("Off")));
    emit move->activated(move->currentIndex());
    EXPECT_FALSE(dlg->values().ctrlMovesLayers);
    dlg->accept();
    QCoreApplication::processEvents();

    CanvasView* canvas = window->tabs()->currentCanvas();
    const Qt::CursorShape normal = canvas->cursor().shape();
    QKeyEvent space(QEvent::KeyPress, Qt::Key_Space, Qt::NoModifier);
    QCoreApplication::sendEvent(canvas, &space);
    EXPECT_EQ(canvas->cursor().shape(), normal) << "Space no longer pans";
    QKeyEvent spaceUp(QEvent::KeyRelease, Qt::Key_Space, Qt::NoModifier);
    QCoreApplication::sendEvent(canvas, &spaceUp);
    QKeyEvent z(QEvent::KeyPress, Qt::Key_Z, Qt::NoModifier);
    QCoreApplication::sendEvent(canvas, &z);
    EXPECT_EQ(canvas->cursor().shape(), Qt::OpenHandCursor) << "Z does";
    QKeyEvent zUp(QEvent::KeyRelease, Qt::Key_Z, Qt::NoModifier);
    QCoreApplication::sendEvent(canvas, &zUp);
    QKeyEvent ctrl(QEvent::KeyPress, Qt::Key_Control, Qt::NoModifier);
    QCoreApplication::sendEvent(canvas, &ctrl);
    EXPECT_EQ(canvas->cursor().shape(), Qt::CrossCursor) << "Ctrl picks a colour now";
    QKeyEvent ctrlUp(QEvent::KeyRelease, Qt::Key_Control, Qt::ControlModifier);
    QCoreApplication::sendEvent(canvas, &ctrlUp);
}

// A key in use is asked about, "so users cant accidentally change their settings".
TEST_F(KeysTest, AKeyInUseIsAskedAboutAndRevertLeavesItWhereItWas) {
    OptionsDialog* dlg = openOptions();
    KeysPage* page = dlg->keysPage();
    page->selectRow(QStringLiteral("file.open"));
    answerTaken("keysRevert");
    press(child<KeyCaptureEdit>(page, "keysCapture"), Qt::Key_N, Qt::ControlModifier);
    EXPECT_EQ(asked, 1);
    EXPECT_TRUE(lastQuestion.contains(QStringLiteral("New"))) << lastQuestion.toStdString();
    EXPECT_EQ(keysOf(page, "file.new"), QList<QKeySequence>{kCtrlN});
    EXPECT_EQ(keysOf(page, "file.open"), QList<QKeySequence>{QKeySequence(QStringLiteral("Ctrl+O"))});
    EXPECT_TRUE(page->note().contains(QStringLiteral("stays"))) << page->note().toStdString();

    press(child<KeyCaptureEdit>(page, "keysCapture"), Qt::Key_F7);
    QCoreApplication::processEvents();
    EXPECT_EQ(asked, 1) << "a free key isn't asked about";

    // Default asks too when another row has the row's starting key.
    page->selectRow(QStringLiteral("file.save"));
    press(child<KeyCaptureEdit>(page, "keysCapture"), Qt::Key_O, Qt::ControlModifier);
    QCoreApplication::processEvents();
    EXPECT_EQ(asked, 1) << "Ctrl+O was free";
    page->selectRow(QStringLiteral("file.open"));
    answerTaken("keysRevert");
    child<QAbstractButton>(page, "keysDefault")->click();
    EXPECT_EQ(asked, 2);
    EXPECT_EQ(keysOf(page, "file.open"), QList<QKeySequence>{QKeySequence(Qt::Key_F7)});
    EXPECT_EQ(keysOf(page, "file.save"), QList<QKeySequence>{QKeySequence(QStringLiteral("Ctrl+O"))});
    dlg->reject();
}

// The field "should have some indicator that its listening for keys".
TEST_F(KeysTest, TheKeyFieldShowsItIsListeningBeforeAKeyIsPressed) {
    OptionsDialog* dlg = openOptions();
    KeysPage* page = dlg->keysPage();
    page->selectRow(QStringLiteral("file.open"));
    auto* capture = child<KeyCaptureEdit>(page, "keysCapture");
    QFocusEvent out(QEvent::FocusOut, Qt::OtherFocusReason);
    QCoreApplication::sendEvent(capture, &out);
    EXPECT_FALSE(capture->listening());
    const QString keyText = capture->text();
    EXPECT_FALSE(keyText.isEmpty());
    const QColor idle = capture->grab().toImage().pixelColor(capture->width() / 2, 3);

    QFocusEvent in(QEvent::FocusIn, Qt::OtherFocusReason);
    QCoreApplication::sendEvent(capture, &in);
    EXPECT_TRUE(capture->listening());
    EXPECT_TRUE(capture->text().isEmpty());
    EXPECT_TRUE(capture->placeholderText().contains(QStringLiteral("Listening")));
    EXPECT_NE(capture->grab().toImage().pixelColor(capture->width() / 2, 3), idle) << "it changes colour";
    dlg->resize(960, 640);
    child<QListWidget>(dlg, "optionsPages")->setCurrentRow(4);
    EXPECT_TRUE(test::saveScreenshot(dlg->grab(), QStringLiteral("options_keys_listening")));

    QCoreApplication::sendEvent(capture, &out);
    EXPECT_FALSE(capture->listening());
    EXPECT_EQ(capture->text(), keyText) << "no key came: the field shows the row's key again";
    dlg->reject();
}

// Feedback: "escape should not exit the dialogue when its listening. Instead it should stop listening. We should
// also add a label that says (Esc to stop listening)".
TEST_F(KeysTest, EscapeStopsListeningAndOnlyThenClosesTheDialog) {
    OptionsDialog* dlg = openOptions();
    KeysPage* page = dlg->keysPage();
    child<QListWidget>(dlg, "optionsPages")->setCurrentRow(4);
    page->selectRow(QStringLiteral("file.open"));
    for (const auto& [field, hintName] : {std::pair{"keysCapture", "keysEscapeHint"}, std::pair{"heldPanKey", "heldPanEscapeHint"}}) {
        auto* capture = child<KeyCaptureEdit>(page, field);
        auto* hint = child<QLabel>(page, hintName);
        QFocusEvent in(QEvent::FocusIn, Qt::OtherFocusReason);
        QCoreApplication::sendEvent(capture, &in);
        ASSERT_TRUE(capture->listening()) << field;
        EXPECT_TRUE(hint->isVisible()) << field;
        EXPECT_EQ(hint->text(), QStringLiteral("(Esc to stop listening)"));
        const int fieldWidth = capture->width();

        press(capture, Qt::Key_Escape);
        EXPECT_FALSE(capture->listening()) << field;
        EXPECT_FALSE(hint->isVisible()) << field;
        EXPECT_EQ(capture->width(), fieldWidth) << "the hint's room is kept: nothing moves";
        EXPECT_FALSE(capture->text().isEmpty()) << "the key shows again";
        EXPECT_TRUE(dlg->isVisible()) << field;
    }
    EXPECT_EQ(keysOf(page, "file.open"), QList<QKeySequence>{QKeySequence(QStringLiteral("Ctrl+O"))}) << "Escape isn't a key";

    press(child<KeyCaptureEdit>(page, "keysCapture"), Qt::Key_Escape);
    EXPECT_FALSE(dlg->isVisible()) << "not listening, Escape closes the dialog";
}

namespace {

class WheelCounter : public QWidget {
public:
    int wheels = 0;

protected:
    void wheelEvent(QWheelEvent* e) override {
        ++wheels;
        e->accept();
    }
};

}

// Feedback: "i am able to change the select box options by scrolling over them with the mouse wheel".
TEST_F(KeysTest, TheWheelOverASelectBoxScrollsWhatsUnderItInstead) {
    WheelCounter parent;
    auto* combo = new QComboBox(&parent);
    combo->addItems({QStringLiteral("One"), QStringLiteral("Two"), QStringLiteral("Three")});
    combo->setCurrentIndex(1);
    parent.resize(200, 100);
    parent.show();
    const QPointF at(10, 10);
    QWheelEvent down(at, combo->mapToGlobal(at), {}, {0, -120}, Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
    QCoreApplication::sendEvent(combo, &down);
    EXPECT_EQ(combo->currentIndex(), 1);
    EXPECT_EQ(parent.wheels, 1);

    // Deep in a scrolled panel, as the tool settings' select boxes are: the wheel climbs past the rows that ignore it to the
    // scroll area (Qt 6 hands a sent wheel event to one widget only). A spin box and a slider without focus pass it on too.
    QScrollArea area;
    auto* page = new QWidget;
    auto* rows = new QVBoxLayout(page);
    auto* row = new QWidget(page);
    auto* rowLayout = new QHBoxLayout(row);
    auto* deep = new QComboBox(row);
    deep->addItems({QStringLiteral("One"), QStringLiteral("Two")});
    auto* spin = new QSpinBox(row);
    spin->setValue(5);
    auto* slider = new QSlider(Qt::Horizontal, row);
    slider->setValue(10);
    rowLayout->addWidget(deep);
    rowLayout->addWidget(spin);
    rowLayout->addWidget(slider);
    rows->addWidget(row);
    rows->addSpacing(2000);
    area.setWidget(page);
    area.resize(300, 200);
    area.show();
    for (QWidget* control : std::initializer_list<QWidget*>{deep, spin, slider}) {
        area.verticalScrollBar()->setValue(0);
        QWheelEvent onIt(QPointF(5, 5), control->mapToGlobal(QPointF(5, 5)), {}, {0, -120}, Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase,
                         false);
        QCoreApplication::sendEvent(control, &onIt);
        EXPECT_GT(area.verticalScrollBar()->value(), 0) << control->metaObject()->className();
    }
    EXPECT_EQ(deep->currentIndex(), 0);
    EXPECT_EQ(spin->value(), 5);
    EXPECT_EQ(slider->value(), 10);

    OptionsDialog* dlg = openOptions();
    auto* zoom = child<QComboBox>(dlg->keysPage(), "heldZoomKey");
    const int was = zoom->currentIndex();
    QCoreApplication::sendEvent(zoom, &down);
    EXPECT_EQ(zoom->currentIndex(), was);
    dlg->reject();
}
