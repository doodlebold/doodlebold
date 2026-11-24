#include "../unit/FfmpegTestSupport.h"
#include "ScreenshotSupport.h"

#include "core/document/Document.h"
#include "core/io/DoodleFormat.h"
#include "core/io/FileIo.h"
#include "core/timelapse/TimelapseReplay.h"
#include "core/paint/StrokeController.h"
#include "ui/CanvasView.h"
#include "ui/DocumentTabs.h"
#include "ui/DoodleboldStyle.h"
#include "ui/MainWindow.h"
#include "ui/QuickBar.h"
#include "ui/timelapse/RecordingPill.h"
#include "ui/timelapse/TimelapseController.h"
#include "ui/timelapse/TimelapseDialogs.h"
#include "core/options/AppOptions.h"

#include <QAbstractButton>
#include <QAction>
#include <QButtonGroup>
#include <QCheckBox>
#include <QDockWidget>
#include <QDateTime>
#include <QElapsedTimer>
#include <QFontMetrics>
#include <QGuiApplication>
#include <QWidgetAction>
#include <QFileInfo>
#include <QLabel>
#include <QMdiSubWindow>
#include <QMenu>
#include <QMenuBar>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPushButton>
#include <QSettings>
#include <QSlider>
#include <QSpinBox>
#include <QStandardPaths>
#include <QTemporaryDir>

#include <gtest/gtest.h>

#include <cstdio>
#include <functional>
#include <memory>

using namespace doodlebold;

namespace {

InputSample at(double x, double y) {
    InputSample s;
    s.pos = QPointF(x, y);
    return s;
}

void paintLine(Document& doc, double y, const QColor& colour = Qt::red, int size = 20) {
    StrokeController stroke(&doc);
    BrushSettings st = defaultSettings(ToolType::Pencil);
    st.size = size;
    st.pressureSize = false;  // two events are all start, which tapers from no pressure
    ASSERT_TRUE(stroke.begin(at(10, y), colour, st));
    stroke.add(at(110, y));
    stroke.finish();
}

bool waitFor(const std::function<bool()>& done, int ms) {
    QElapsedTimer clock;
    clock.start();
    while (!done() && clock.elapsed() < ms)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    return done();
}

void settle() {
    for (int i = 0; i < 5; ++i) {
        QCoreApplication::processEvents();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
    }
}

class TimelapseUiTest : public testing::Test {
protected:
    void SetUp() override {
        QStandardPaths::setTestModeEnabled(true);
        test::applyTheme();
        AppOptions::settings().clear();  // the tests share one settings file: every test starts from the defaults
        AppOptions().setValues([] {
            AppOptions::Values v;
            v.recordNewCanvases = true;  // these tests are about recording, which is off by default
            return v;
        }());
        window = std::make_unique<MainWindow>();
        window->resize(1400, 900);
        window->show();
        settle();
    }
    Document* newCanvas() {
        window->newDocument();
        settle();
        return window->tabs()->currentDocument();
    }
    RecordingPill* pill(Document* doc) { return window->tabs()->pillOf(doc); }
    QAction* menuAction(const char* id) { return window->command(QString::fromLatin1(id)); }
    std::unique_ptr<MainWindow> window;
};

class ScriptedTimelapse : public TimelapseController {
public:
    using TimelapseController::TimelapseController;
    std::optional<TimelapseExportChoice> choice;
    QString exportPath;
    bool deleteAnswer = false;
    int deleteAsked = 0;
    QString deleteName;
    int deleteSteps = 0;
    bool restartAnswer = false;
    int restartAsked = 0;
    QStringList errors;

protected:
    std::optional<TimelapseExportChoice> askExport(std::shared_ptr<const Timelapse>, const QString&, const QString&) override {
        return choice;
    }
    QString askExportPath(const QString&) override { return exportPath; }
    bool confirmDelete(const QString& name, int steps) override {
        ++deleteAsked;
        deleteName = name;
        deleteSteps = steps;
        return deleteAnswer;
    }
    bool confirmRestart(const QString&, int) override {
        ++restartAsked;
        return restartAnswer;
    }
    void showError(const QString&, const QString& text) override { errors << text; }
};

}

TEST_F(TimelapseUiTest, ANewCanvasRecordsAndItsHeaderShowsHowManySteps) {
    Document* doc = newCanvas();
    ASSERT_TRUE(doc);
    EXPECT_TRUE(doc->isRecording()) << "on by default for every new canvas";
    RecordingPill* mark = pill(doc);
    ASSERT_TRUE(mark);
    EXPECT_TRUE(mark->isVisible());
    EXPECT_EQ(mark->text(), QStringLiteral("REC · 0 steps"));
    paintLine(*doc, 40);
    EXPECT_EQ(mark->text(), QStringLiteral("REC · 1 step"));
    paintLine(*doc, 90, Qt::blue);
    ASSERT_TRUE(doc->undo());
    EXPECT_EQ(mark->text(), QStringLiteral("REC · 3 steps"));
    EXPECT_EQ(RecordingPill::groupedNumber(1204), QString::fromUtf8("1 204"));
    EXPECT_EQ(RecordingPill::groupedNumber(120), QStringLiteral("120"));
    EXPECT_EQ(RecordingPill::groupedNumber(1234567), QString::fromUtf8("1 234 567"));
}

TEST_F(TimelapseUiTest, ANarrowCanvasWindowHidesHeaderItemsRatherThanCuttingThemOff) {
    Document* doc = newCanvas();
    RecordingPill* mark = pill(doc);
    ASSERT_TRUE(mark);
    QMdiSubWindow* sub = window->tabs()->windowAt(0);
    sub->showNormal();
    const int buttons = 6 + 3 * (DoodleboldStyle::kButtonSize + DoodleboldStyle::kButtonGap);
    const auto check = [&](QWidget* item, int width) {
        if (!item->isVisibleTo(sub))
            return;
        EXPECT_GE(item->x(), DoodleboldStyle::kFrameMargin) << "cut off at the left at width " << width;
        EXPECT_LT(item->geometry().right(), sub->width() - DoodleboldStyle::kFrameMargin - buttons)
            << "under the buttons at width " << width;
    };
    bool sawTheMark = false, sawItGone = false;
    for (int width = 700; width >= 120; width -= 20) {
        sub->resize(width, 300);
        settle();
        check(mark, width);
        sawTheMark = sawTheMark || mark->isVisibleTo(sub);
        sawItGone = sawItGone || !mark->isVisibleTo(sub);
        if (width == 300) {
            EXPECT_TRUE(test::saveScreenshot(window->grab(), QStringLiteral("canvas_header_narrow")));
        }
    }
    EXPECT_TRUE(sawTheMark);
    EXPECT_TRUE(sawItGone);

    sub->resize(700, 300);
    settle();
    EXPECT_TRUE(mark->isVisibleTo(sub)) << "it comes back with the room";
}

TEST_F(TimelapseUiTest, TheMarkSitsAtTheRightEndOfTheHeaderJustLeftOfTheWindowButtons) {
    Document* doc = newCanvas();
    RecordingPill* mark = pill(doc);
    ASSERT_TRUE(mark);
    const QMdiSubWindow* sub = window->tabs()->windowAt(0);
    EXPECT_EQ(mark->parentWidget(), sub);
    const int margin = DoodleboldStyle::kFrameMargin;
    EXPECT_GE(mark->y(), margin);
    EXPECT_LE(mark->geometry().bottom(), DoodleboldStyle::kTitleBarHeight) << "inside the header, not over the canvas";
    const int headerRight = sub->width() - margin;
    const int buttonsLeft = headerRight - (6 + 3 * (DoodleboldStyle::kButtonSize + DoodleboldStyle::kButtonGap));
    EXPECT_LT(mark->geometry().right(), buttonsLeft);
    EXPECT_GT(mark->geometry().right(), buttonsLeft - 20) << "right beside the buttons";
    EXPECT_GT(mark->x(), sub->width() / 2);
    EXPECT_GE(sub->property(DoodleboldStyle::kHeaderInsetProperty).toInt(), headerRight - mark->x());
    EXPECT_TRUE(test::saveScreenshot(window->grab(), QStringLiteral("timelapse_rec_mark")));
    QMdiSubWindow* restored = window->tabs()->windowAt(0);
    restored->showNormal();
    restored->resize(700, 500);
    settle();
    const int rightEnd = restored->width() - margin - (6 + 3 * (DoodleboldStyle::kButtonSize + DoodleboldStyle::kButtonGap));
    EXPECT_LT(mark->geometry().right(), rightEnd);
    EXPECT_GT(mark->geometry().right(), rightEnd - 20);
    EXPECT_TRUE(mark->isVisible());
}

TEST_F(TimelapseUiTest, PausingShowsPausedWithoutRedAndResumingGoesOn) {
    Document* doc = newCanvas();
    paintLine(*doc, 40);
    RecordingPill* mark = pill(doc);
    QMenu* menu = mark->buildMenu(mark);
    ASSERT_TRUE(menu);
    QAction* pause = menu->findChild<QAction*>(QStringLiteral("recordingPause"));
    ASSERT_TRUE(pause);
    EXPECT_EQ(pause->text(), QStringLiteral("Pause Recording"));
    pause->trigger();
    EXPECT_TRUE(doc->isRecordingPaused());
    EXPECT_EQ(mark->text(), QStringLiteral("Paused · 1 step"));
    paintLine(*doc, 80, Qt::blue);
    EXPECT_EQ(doc->recordedSteps(), 1) << "nothing is recorded while paused";
    EXPECT_FALSE(menuAction("file.recordTimelapse")->isChecked()) << "paused: Record Timelapse is not checked";
    EXPECT_TRUE(test::saveScreenshot(window->grab(), QStringLiteral("timelapse_paused")));

    QMenu* resumeMenu = mark->buildMenu(mark);
    QAction* resume = resumeMenu->findChild<QAction*>(QStringLiteral("recordingPause"));
    EXPECT_EQ(resume->text(), QStringLiteral("Resume Recording"));
    resume->trigger();
    EXPECT_FALSE(doc->isRecordingPaused());
    EXPECT_EQ(mark->text(), QStringLiteral("REC · 1 step"));
    EXPECT_TRUE(menuAction("file.recordTimelapse")->isChecked());
}

TEST_F(TimelapseUiTest, ThePillsMenuHasTheHeadingBlockThenPauseReplayExportRestartDeleteAndTheOptionsCheckBox) {
    Document* doc = newCanvas();
    paintLine(*doc, 40);
    QMenu* menu = pill(doc)->buildMenu(window.get());
    const QList<QAction*> actions = menu->actions();
    QStringList texts;
    for (const QAction* a : actions)
        texts << (a->isSeparator()                                    ? QStringLiteral("---")
                  : a->objectName() == QLatin1String("recordingInfo") ? QStringLiteral("<heading>")
                                                                      : a->text());
    EXPECT_EQ(texts, (QStringList{"<heading>", "---", "Pause Recording", "Timelapse Replay", "Export Timelapse …",
                                  "Restart the Recording …", "Delete the Recording …"}));
    auto* title = menu->findChild<QLabel*>(QStringLiteral("recordingMenuTitle"));
    auto* details = menu->findChild<QLabel*>(QStringLiteral("recordingMenuDetails"));
    ASSERT_TRUE(title && details);
    EXPECT_EQ(title->text(), QStringLiteral("Timelapse of this canvas"));
    EXPECT_TRUE(title->font().bold());
    EXPECT_FALSE(details->font().bold());
    EXPECT_LT(details->font().pixelSize(), QFontMetrics(title->font()).height());
    EXPECT_TRUE(details->text().startsWith(QStringLiteral("1 step since ")));
    EXPECT_TRUE(details->text().contains(QStringLiteral(" of painting · ")));
    EXPECT_TRUE(details->text().endsWith(QStringLiteral(" in the file")));
    EXPECT_NE(actions[0]->objectName(), QString());
    EXPECT_TRUE(qobject_cast<QWidgetAction*>(actions[0])) << "a widget, so it cannot be clicked as a command";
    menu->popup(window->mapToGlobal(QPoint(300, 200)));
    settle();
    EXPECT_TRUE(test::saveScreenshot(menu->grab(), QStringLiteral("timelapse_pill_menu")));
    menu->close();
}

TEST_F(TimelapseUiTest, ThePillsMenuHasNoOptionsCheckBox) {
    Document* doc = newCanvas();
    QMenu* menu = pill(doc)->buildMenu(window.get());
    EXPECT_FALSE(menu->findChild<QAction*>(QStringLiteral("recordingNewCanvases"))) << "Record new canvases is in Options only";
    for (const QAction* a : menu->actions())
        EXPECT_FALSE(a->text().contains(QStringLiteral("Record new canvases")));
}

TEST_F(TimelapseUiTest, TheHeadingLineSaysWhichStepsSinceWhenHowLongAndHowBig) {
    const qint64 oct29 = QDateTime(QDate(2026, 10, 29), QTime(12, 0)).toMSecsSinceEpoch();
    EXPECT_EQ(RecordingPill::infoText(1204, oct29, (2 * 60 + 41) * 60000ll, 38'000'000),
              QString::fromUtf8("1\u2009204 steps since 29 Oct · 2 h 41 min of painting · 38 MB in the file"));
    EXPECT_EQ(RecordingPill::infoText(7, oct29, 7 * 60000ll, 2'400'000),
              QStringLiteral("7 steps since 29 Oct · 7 min of painting · 2.4 MB in the file"));
    EXPECT_EQ(RecordingPill::infoText(1, 0, 12000, 30'000), QStringLiteral("1 step · 12 s of painting · 30 KB in the file"))
        << "a recording without a start date leaves out the \"since\"";
}

TEST_F(TimelapseUiTest, ACanvasWithoutARecordingShowsAGreyRecThatStartsOneAgain) {
    EXPECT_FALSE(menuAction("file.exportTimelapse")->isEnabled()) << "no canvas";
    EXPECT_FALSE(menuAction("file.timelapseReplay")->isEnabled());
    EXPECT_FALSE(menuAction("file.recordTimelapse")->isEnabled());
    EXPECT_FALSE(menuAction("file.restartRecording")->isEnabled());
    EXPECT_FALSE(menuAction("file.deleteRecording")->isEnabled());
    Document* doc = newCanvas();
    for (const char* id :
         {"file.timelapseReplay", "file.exportTimelapse", "file.recordTimelapse", "file.restartRecording", "file.deleteRecording"})
        EXPECT_TRUE(menuAction(id)->isEnabled()) << id;
    doc->deleteRecording();
    settle();
    EXPECT_TRUE(pill(doc)->isVisible());
    EXPECT_EQ(pill(doc)->text(), QStringLiteral("REC"));
    EXPECT_GT(window->tabs()->windowAt(0)->property(DoodleboldStyle::kHeaderInsetProperty).toInt(), 0) << "the title keeps clear of it";
    EXPECT_TRUE(test::saveScreenshot(window->grab(), QStringLiteral("timelapse_rec_off")));
    for (const char* id : {"file.timelapseReplay", "file.exportTimelapse", "file.restartRecording", "file.deleteRecording"})
        EXPECT_FALSE(menuAction(id)->isEnabled()) << id;
    EXPECT_TRUE(menuAction("file.recordTimelapse")->isEnabled());
    EXPECT_FALSE(menuAction("file.recordTimelapse")->isChecked());
    QMenu* menu = pill(doc)->buildMenu(window.get());
    EXPECT_EQ(menu->findChild<QLabel*>(QStringLiteral("recordingMenuDetails"))->text(), QStringLiteral("Not recording"));
    EXPECT_FALSE(menu->findChild<QAction*>(QStringLiteral("recordingPause")));
    for (const char* name : {"recordingReplay", "recordingExport", "recordingRestart", "recordingDelete"})
        EXPECT_FALSE(menu->findChild<QAction*>(QString::fromLatin1(name))->isEnabled()) << name;
    menu->findChild<QAction*>(QStringLiteral("recordingStart"))->trigger();
    settle();
    EXPECT_TRUE(doc->isRecording());
    EXPECT_EQ(pill(doc)->text(), QStringLiteral("REC · 0 steps"));
    EXPECT_TRUE(menuAction("file.recordTimelapse")->isChecked());
}

TEST_F(TimelapseUiTest, RecordNewCanvasesOffStartsNoRecording) {
    AppOptions::Values v = window->options()->values();
    v.recordNewCanvases = false;
    window->options()->setValues(v);
    Document* doc = newCanvas();
    EXPECT_FALSE(doc->isRecording());
    EXPECT_EQ(pill(doc)->text(), QStringLiteral("REC")) << "the grey mark, to start one by hand";
    paintLine(*doc, 40);
    EXPECT_EQ(doc->recordedSteps(), 0);
    v.recordNewCanvases = true;
    window->options()->setValues(v);
    EXPECT_TRUE(newCanvas()->isRecording());
}

TEST(TimelapseDefaults, NewCanvasesDoNotRecordUnlessTheOptionIsOn) {
    QStandardPaths::setTestModeEnabled(true);
    AppOptions::settings().clear();
    EXPECT_FALSE(AppOptions::Values{}.recordNewCanvases);
    MainWindow window;
    window.newDocument();
    ASSERT_TRUE(window.tabs()->currentDocument());
    EXPECT_FALSE(window.tabs()->currentDocument()->isRecording());
}

TEST_F(TimelapseUiTest, RecordTimelapseIsCheckedWhileRecordingPausesWhenUncheckedAndStartsARecordingWhenThereIsNone) {
    QAction* record = menuAction("file.recordTimelapse");
    ASSERT_TRUE(record->isCheckable());
    EXPECT_EQ(record->text(), QStringLiteral("Record Timelapse"));
    Document* doc = newCanvas();
    paintLine(*doc, 40);
    EXPECT_TRUE(record->isChecked());
    record->trigger();
    EXPECT_TRUE(doc->isRecordingPaused());
    EXPECT_FALSE(record->isChecked());
    EXPECT_EQ(pill(doc)->text(), QStringLiteral("Paused · 1 step"));
    record->trigger();
    EXPECT_FALSE(doc->isRecordingPaused());
    EXPECT_TRUE(record->isChecked());

    AppOptions::Values v = window->options()->values();
    v.recordNewCanvases = false;
    window->options()->setValues(v);
    Document* plain = newCanvas();
    EXPECT_FALSE(plain->isRecording());
    EXPECT_FALSE(record->isChecked());
    paintLine(*plain, 40);
    record->trigger();
    EXPECT_TRUE(plain->isRecording());
    EXPECT_EQ(plain->recordedSteps(), 0);
    EXPECT_TRUE(record->isChecked());
    EXPECT_TRUE(pill(plain)->isVisible());
    paintLine(*plain, 80, Qt::blue);
    EXPECT_EQ(pill(plain)->text(), QStringLiteral("REC · 1 step"));

    window->tabs()->setCurrentIndex(0);
    settle();
    EXPECT_TRUE(record->isChecked());
}

TEST(RecordingPill, ItsMenuAsksForReplayExportAndDeleteWithoutDoingThem) {
    Document doc(QSize(64, 64), Qt::white);
    doc.startRecording();
    RecordingPill mark(&doc);
    std::vector<RecordingPill::Command> asked;
    QObject::connect(&mark, &RecordingPill::commandRequested, [&](RecordingPill::Command c) { asked.push_back(c); });
    QMenu* menu = mark.buildMenu(&mark);
    menu->findChild<QAction*>(QStringLiteral("recordingReplay"))->trigger();
    menu->findChild<QAction*>(QStringLiteral("recordingExport"))->trigger();
    menu->findChild<QAction*>(QStringLiteral("recordingRestart"))->trigger();
    menu->findChild<QAction*>(QStringLiteral("recordingDelete"))->trigger();
    menu->findChild<QAction*>(QStringLiteral("recordingPause"))->trigger();
    EXPECT_EQ(asked, (std::vector<RecordingPill::Command>{RecordingPill::Command::Replay, RecordingPill::Command::Export,
                                                          RecordingPill::Command::Restart, RecordingPill::Command::Delete,
                                                          RecordingPill::Command::Pause}));
    EXPECT_TRUE(doc.isRecording()) << "the pill only asks";
    EXPECT_FALSE(doc.isRecordingPaused());
}

TEST_F(TimelapseUiTest, AFileOpenedWithoutARecordingStartsOneFromThePictureAsOpened) {
    QTemporaryDir dir;
    Document source(QSize(100, 80), Qt::white);
    paintLine(source, 30);
    const QString path = dir.filePath(QStringLiteral("plain.doodle"));
    ASSERT_TRUE(io::saveFile(source.content(), path, DoodleFormat()).ok());
    window->openFiles({path});
    settle();
    Document* doc = window->tabs()->currentDocument();
    ASSERT_TRUE(doc);
    EXPECT_TRUE(doc->isRecording());
    EXPECT_EQ(doc->recordedSteps(), 0);
    const QImage opened = doc->projection().copy();
    paintLine(*doc, 60, Qt::blue);
    const auto recording = doc->timelapse();
    ASSERT_EQ(recording->stepCount(), 1);
    TimelapseReplay replay(*recording);
    EXPECT_EQ(replay.seek(0).convertToFormat(QImage::Format_RGBA8888_Premultiplied), opened)
        << "the first picture is the one that was opened";
}

TEST_F(TimelapseUiTest, ARecordingSavedInTheFileComesBackAndGoesOnEvenWhenNewCanvasesAreNotRecorded) {
    QTemporaryDir dir;
    Document source(QSize(100, 80), Qt::white);
    source.startRecording();
    paintLine(source, 30);
    paintLine(source, 60, Qt::blue);
    source.setRecordingPaused(true);
    const QString path = dir.filePath(QStringLiteral("recorded.doodle"));
    ASSERT_TRUE(io::saveFile(source.content(), path, DoodleFormat()).ok());
    AppOptions::Values v = window->options()->values();
    v.recordNewCanvases = false;
    window->options()->setValues(v);
    window->openFiles({path});
    settle();
    Document* doc = window->tabs()->currentDocument();
    ASSERT_TRUE(doc);
    EXPECT_TRUE(doc->isRecording());
    EXPECT_EQ(doc->recordedSteps(), 2);
    EXPECT_TRUE(doc->isRecordingPaused()) << "it was paused when saved";
    EXPECT_EQ(pill(doc)->text(), QStringLiteral("Paused · 2 steps"));
    doc->setRecordingPaused(false);
    paintLine(*doc, 20, Qt::green);
    EXPECT_EQ(pill(doc)->text(), QStringLiteral("REC · 3 steps"));
}

TEST_F(TimelapseUiTest, AStrokeIsFramedByTheTimerWhileItRunsAndOnlyThen) {
    Document* doc = newCanvas();
    const auto frames = [&] { return doc->timelapse()->frameCount(); };
    ASSERT_EQ(frames(), 1);
    waitFor([] { return false; }, 250);
    EXPECT_EQ(frames(), 1) << "no stroke, no frames";
    StrokeController stroke(doc);
    BrushSettings st = defaultSettings(ToolType::Pencil);
    st.size = 20;
    ASSERT_TRUE(stroke.begin(at(10, 50), Qt::red, st));
    for (int i = 1; i <= 6; ++i) {
        stroke.add(at(10 + i * 20.0, 50));
        waitFor([] { return false; }, 110);
    }
    stroke.finish();
    const int framed = frames();
    EXPECT_GE(framed, 1 + 4) << "about six frames in 0.66 s of drawing";
    EXPECT_LE(framed, 1 + 6 + 1);
    EXPECT_EQ(doc->recordedSteps(), 1) << "the frames are not steps";
    EXPECT_EQ(doc->timelapse()->steps.back()->kind, TimelapseStep::Kind::Edit) << "the step at pen lift ends the stroke";
    waitFor([] { return false; }, 300);
    EXPECT_EQ(frames(), framed) << "the timer stopped with the stroke";
    ASSERT_TRUE(stroke.begin(at(10, 120), Qt::blue, st));
    stroke.add(at(150, 120));
    waitFor([] { return false; }, 250);
    stroke.cancel();
    EXPECT_EQ(frames(), framed);
    EXPECT_EQ(doc->timelapse()->path(true).size(), std::size_t(framed));
}

namespace {

std::shared_ptr<const Timelapse> sampleRecording(int strokes = 4, bool undoOne = true) {
    Document doc(QSize(2000, 1000), Qt::white);
    doc.startRecording();
    for (int i = 0; i < strokes; ++i)
        paintLine(doc, 60 + 120 * i, QColor::fromHsv(i * 60, 255, 255), 80);
    if (undoOne) {
        EXPECT_TRUE(doc.undo());
    }
    return doc.timelapse();
}

template <typename T> T* child(QWidget* parent, const char* name) {
    T* c = parent->findChild<T*>(QString::fromLatin1(name));
    EXPECT_NE(c, nullptr) << name;
    return c;
}

}

namespace {

class ExportTimelapseDialogTest : public testing::Test {
protected:
    void SetUp() override { test::applyTheme(); }
};

const QString kName = QStringLiteral("sky.doodle");

void paintFramed(Document& doc, double y, int frames, const QColor& colour, int size) {
    StrokeController stroke(&doc);
    BrushSettings st = defaultSettings(ToolType::Pencil);
    st.size = size;
    ASSERT_TRUE(stroke.begin(at(5, y), colour, st));
    // The path holds back one event, and a pen sample is laid once all its stamps are ready: from these two the first frame shows
    // paint.
    stroke.add(at(7.5, y));
    stroke.add(at(10, y));
    for (int i = 1; i <= frames; ++i) {
        stroke.add(at(10 + i * 1900.0 / frames, y));
        doc.captureStrokeFrame();
    }
    stroke.finish();
}

std::shared_ptr<const Timelapse> framedRecording(int strokes, int frames, bool undoOne = false) {
    Document doc(QSize(2000, 1000), Qt::white);
    doc.startRecording();
    for (int i = 0; i < strokes; ++i)
        paintFramed(doc, 60 + 120.0 * (i % 7), frames, QColor::fromHsv((i * 53) % 360, 230, 235), 70);
    if (undoOne) {
        EXPECT_TRUE(doc.undo());
    }
    return doc.timelapse();
}

}

TEST_F(ExportTimelapseDialogTest, StartsWithFullLengthAHoldOfThreeAndMoreSettingsClosed) {
    ExportTimelapseDialog dialog(sampleRecording(), kName, QStringLiteral("H.264 High profile"));
    const TimelapseExportChoice c = dialog.choice();
    EXPECT_EQ(c.options.length, TimelapseLength::Full);
    EXPECT_EQ(c.options.holdSeconds, 3);
    EXPECT_TRUE(c.options.leaveOutUndone);
    EXPECT_TRUE(c.soundFile.isEmpty());
    EXPECT_EQ(c.crf, 18);
    EXPECT_EQ(c.maxBitrateKbps, 0);
    EXPECT_EQ(c.maxFileMb, 0.0);
    EXPECT_EQ(child<QLabel>(&dialog, "timelapseTitle")->text(), QStringLiteral("Export Timelapse"));
    EXPECT_EQ(child<QLabel>(&dialog, "timelapseSubtitle")->text(), QStringLiteral("sky.doodle · 4 strokes · 6 frames recorded"));
    auto* full = child<QAbstractButton>(&dialog, "timelapseFullLength");
    auto* thirty = child<QAbstractButton>(&dialog, "timelapse30Seconds");
    EXPECT_TRUE(full->isChecked());
    EXPECT_FALSE(thirty->isChecked());
    EXPECT_EQ(full->findChildren<QLabel*>().at(0)->text(), QStringLiteral("Full length"));
    EXPECT_EQ(full->findChildren<QLabel*>().at(1)->text(),
              QStringLiteral("Every recorded frame: each stroke drawn in as you drew it. 0:00 for this canvas."));
    EXPECT_EQ(thirty->findChildren<QLabel*>().at(0)->text(), QStringLiteral("30 seconds"));
    EXPECT_EQ(thirty->findChildren<QLabel*>().at(1)->text(), QStringLiteral("The whole painting sped up to fit 30 s, for posting."));
    auto* panel = child<QWidget>(&dialog, "timelapseSettingsPanel");
    EXPECT_TRUE(panel->isHidden());
    auto* more = child<QAbstractButton>(&dialog, "timelapseMoreSettings");
    EXPECT_EQ(more->text(), QStringLiteral("More settings"));
    EXPECT_FALSE(more->isChecked());
    EXPECT_EQ(dialog.findChild<QWidget*>(QStringLiteral("timelapseRate")), nullptr) << "the rate is fixed and not shown";
    auto* hold = child<QSpinBox>(&dialog, "timelapseHoldSeconds");
    EXPECT_EQ(hold->value(), 3);
    EXPECT_EQ(hold->minimum(), 0);
    EXPECT_EQ(hold->maximum(), 30);
    EXPECT_EQ(child<QCheckBox>(&dialog, "timelapseLeaveOutUndone")->text(), QStringLiteral("Leave them out"));
    EXPECT_TRUE(child<QLabel>(&dialog, "timelapseSize")->text().startsWith(QStringLiteral("1920 × 960 ")));
    EXPECT_TRUE(child<QLabel>(&dialog, "timelapseSize")->text().contains(QStringLiteral("(fits 1080p)")));
    EXPECT_EQ(dialog.plan().frameSize, QSize(1920, 960));
    EXPECT_EQ(dialog.plan().frames.size(), 4u + 90u) << "the path of 4 pictures and the hold";
    EXPECT_TRUE(child<QLabel>(&dialog, "timelapseSoundNone")->isVisibleTo(panel));
    EXPECT_EQ(child<QPushButton>(&dialog, "timelapseSound")->text(), QStringLiteral("Add a sound file …"));
    const auto* summary = child<QLabel>(&dialog, "timelapseSummary");
    EXPECT_TRUE(summary->text().startsWith(QStringLiteral("MP4 · about ")));
    EXPECT_TRUE(summary->text().endsWith(QStringLiteral(" MB")));
    EXPECT_EQ(summary->toolTip(), QStringLiteral("H.264 High profile"));
}

TEST_F(ExportTimelapseDialogTest, MoreSettingsOpensAndClosesTheSettings) {
    ExportTimelapseDialog dialog(sampleRecording(), kName, QString());
    dialog.show();
    settle();
    auto* panel = child<QWidget>(&dialog, "timelapseSettingsPanel");
    auto* more = child<QAbstractButton>(&dialog, "timelapseMoreSettings");
    EXPECT_FALSE(panel->isVisible());
    const int closedHeight = dialog.height(), closedWidth = dialog.width();
    const QRect cardsClosed = child<QWidget>(&dialog, "timelapseLength")->geometry();
    EXPECT_TRUE(test::saveScreenshot(dialog.grab(), QStringLiteral("timelapse_export_dialog")));
    more->click();
    settle();
    EXPECT_TRUE(more->isChecked());
    EXPECT_TRUE(panel->isVisible());
    EXPECT_TRUE(child<QWidget>(&dialog, "timelapseQuality")->isVisible());
    EXPECT_GE(dialog.height(), closedHeight);
    EXPECT_EQ(dialog.width(), closedWidth) << "it opens downward only";
    EXPECT_EQ(child<QWidget>(&dialog, "timelapseLength")->geometry(), cardsClosed) << "the Length cards stay as they were";
    EXPECT_GE(panel->mapTo(&dialog, QPoint(0, 0)).y(), more->mapTo(&dialog, QPoint(0, 0)).y() + more->height()) << "under the button";
    EXPECT_LE(panel->mapTo(&dialog, QPoint(0, 0)).y() + panel->height(), dialog.height()) << "inside the dialog";
    EXPECT_TRUE(test::saveScreenshot(dialog.grab(), QStringLiteral("timelapse_export_dialog_more")));
    more->click();
    settle();
    EXPECT_FALSE(panel->isVisible());
    EXPECT_LE(dialog.height(), closedHeight + 2);
    EXPECT_EQ(dialog.width(), closedWidth);
}

TEST_F(ExportTimelapseDialogTest, HasTwoColumnsAndExportIsTheRightmostDefaultButton) {
    ExportTimelapseDialog dialog(framedRecording(3, 8), kName, QString());
    dialog.show();
    settle();
    auto* preview = child<QLabel>(&dialog, "timelapsePreview");
    EXPECT_EQ(preview->size(), QSize(560, 400));
    auto* cards = child<QWidget>(&dialog, "timelapseLength");
    EXPECT_GT(cards->mapTo(&dialog, QPoint(0, 0)).x(), preview->geometry().right()) << "the length is in the right column";
    auto* full = child<QAbstractButton>(&dialog, "timelapseFullLength");
    auto* thirty = child<QAbstractButton>(&dialog, "timelapse30Seconds");
    EXPECT_LT(full->geometry().bottom(), thirty->geometry().top()) << "the two cards are one over the other";
    auto* scrubber = child<QSlider>(&dialog, "timelapseScrubber");
    EXPECT_GT(scrubber->mapTo(&dialog, QPoint(0, 0)).y(), preview->geometry().bottom()) << "the scrubber is under the picture";
    EXPECT_LE(scrubber->geometry().right(), preview->geometry().right() + 1);
    auto* time = child<QLabel>(&dialog, "timelapseVideoTime");
    auto* length = child<QLabel>(&dialog, "timelapseVideoLength");
    EXPECT_LT(time->x(), length->x()) << "the time left, the length right";
    EXPECT_EQ(length->text(), QStringLiteral("0:03"));
    EXPECT_EQ(time->text(), QStringLiteral("0:03"));
    QPushButton* exportButton = nullptr;
    QPushButton* cancel = nullptr;
    for (QPushButton* b : dialog.findChildren<QPushButton*>()) {
        if (b->text() == QStringLiteral("Export …"))
            exportButton = b;
        if (b->text() == QStringLiteral("Cancel"))
            cancel = b;
    }
    ASSERT_TRUE(exportButton && cancel);
    EXPECT_TRUE(exportButton->isDefault());
    EXPECT_GT(exportButton->mapTo(&dialog, QPoint(0, 0)).x(), cancel->mapTo(&dialog, QPoint(0, 0)).x()) << "Export rightmost";
    const auto* summary = child<QLabel>(&dialog, "timelapseSummary");
    EXPECT_LT(summary->x(), cancel->mapTo(&dialog, QPoint(0, 0)).x()) << "the line about the file at the left";
}

TEST_F(ExportTimelapseDialogTest, TheTwoLengthsAreFullAndThirtySeconds) {
    ExportTimelapseDialog dialog(framedRecording(3, 8), kName, QString());
    auto* full = child<QAbstractButton>(&dialog, "timelapseFullLength");
    auto* thirty = child<QAbstractButton>(&dialog, "timelapse30Seconds");
    EXPECT_EQ(dialog.plan().frames.size(), 28u + 90u);
    EXPECT_EQ(dialog.plan().fps, 30);
    EXPECT_EQ(dialog.plan().heldFrames, 90);
    EXPECT_EQ(full->findChildren<QLabel*>().at(1)->text(),
              QStringLiteral("Every recorded frame: each stroke drawn in as you drew it. 0:00 for this canvas."));
    thirty->click();
    EXPECT_TRUE(thirty->isChecked());
    EXPECT_FALSE(full->isChecked());
    EXPECT_EQ(dialog.choice().options.length, TimelapseLength::Seconds30);
    EXPECT_EQ(dialog.plan().frames.size(), 30u * 30 + 90) << "the whole painting sped up to 30 s";
    EXPECT_EQ(dialog.plan().frames.front(), 0);
    EXPECT_EQ(child<QLabel>(&dialog, "timelapseVideoLength")->text(), QStringLiteral("0:33"));
    EXPECT_EQ(dialog.scrubberPosition(), 30 * 30 + 90 - 1) << "still at the end";
    full->click();
    EXPECT_EQ(dialog.choice().options.length, TimelapseLength::Full);
    EXPECT_EQ(dialog.plan().frames.size(), 28u + 90u);
    EXPECT_EQ(child<QWidget>(&dialog, "timelapseLength")->findChildren<QAbstractButton*>().size(), 2);
    EXPECT_EQ(dialog.findChild<QWidget*>(QStringLiteral("timelapseFromEmpty")), nullptr);
    EXPECT_EQ(dialog.findChild<QWidget*>(QStringLiteral("timelapseCustomSeconds")), nullptr);
    EXPECT_EQ(dialog.findChild<QWidget*>(QStringLiteral("timelapseCustomRate")), nullptr);
}

TEST_F(ExportTimelapseDialogTest, TheFullLengthCardSaysHowLongTheCanvasTakes) {
    ExportTimelapseDialog dialog(framedRecording(20, 15), kName, QString());
    EXPECT_EQ(child<QAbstractButton>(&dialog, "timelapseFullLength")->findChildren<QLabel*>().at(1)->text(),
              QStringLiteral("Every recorded frame: each stroke drawn in as you drew it. 0:10 for this canvas."));
    EXPECT_EQ(child<QLabel>(&dialog, "timelapseVideoLength")->text(), QStringLiteral("0:13"));
    EXPECT_EQ(child<QLabel>(&dialog, "timelapseSubtitle")->text(), QStringLiteral("sky.doodle · 20 strokes · 321 frames recorded"));
}

TEST_F(ExportTimelapseDialogTest, TheUndoneStrokesAndTheHoldChangeThePlanAndTheScrubber) {
    ExportTimelapseDialog dialog(framedRecording(4, 5, true), kName, QString());
    auto* scrubber = child<QSlider>(&dialog, "timelapseScrubber");
    const int kept = 1 + 3 * 6;
    EXPECT_EQ(dialog.plan().frames.size(), std::size_t(kept + 90));
    EXPECT_EQ(scrubber->maximum(), kept + 90 - 1);
    EXPECT_EQ(scrubber->value(), scrubber->maximum()) << "starts at the finished picture";
    child<QCheckBox>(&dialog, "timelapseLeaveOutUndone")->setChecked(false);
    EXPECT_EQ(dialog.plan().frames.size(), std::size_t(1 + 4 * 6 + 1 + 90)) << "the undone stroke and the undo step are pictures now";
    EXPECT_FALSE(dialog.choice().options.leaveOutUndone);
    auto* hold = child<QSpinBox>(&dialog, "timelapseHoldSeconds");
    hold->setValue(10);
    EXPECT_EQ(dialog.plan().heldFrames, 300);
    hold->setValue(0);
    EXPECT_EQ(dialog.plan().heldFrames, 0) << "0 is no hold";
    EXPECT_EQ(dialog.plan().frames.size(), std::size_t(1 + 4 * 6 + 1));
    EXPECT_EQ(dialog.choice().options.holdSeconds, 0);
    EXPECT_EQ(scrubber->maximum(), 1 + 4 * 6) << "frames 0 … 25";
}

TEST_F(ExportTimelapseDialogTest, PlayShowsTheVideoInThePreview) {
    ExportTimelapseDialog dialog(framedRecording(3, 8), kName, QString());
    child<QSpinBox>(&dialog, "timelapseHoldSeconds")->setValue(0);
    auto* play = child<QPushButton>(&dialog, "timelapsePlay");
    auto* scrubber = child<QSlider>(&dialog, "timelapseScrubber");
    auto* time = child<QLabel>(&dialog, "timelapseVideoTime");
    auto* preview = child<QLabel>(&dialog, "timelapsePreview");
    ASSERT_EQ(scrubber->value(), scrubber->maximum());
    ASSERT_EQ(scrubber->maximum(), 27);
    dialog.setScrubberPosition(0);
    const QImage empty = preview->pixmap().toImage();
    scrubber->setValue(scrubber->maximum());
    play->click();
    EXPECT_TRUE(dialog.playing());
    EXPECT_EQ(play->text(), QStringLiteral("Pause"));
    EXPECT_EQ(scrubber->value(), 0) << "at the end: from the start";
    EXPECT_EQ(preview->pixmap().toImage(), empty);
    EXPECT_TRUE(waitFor([&] { return scrubber->value() >= 10; }, 3000));
    EXPECT_NE(preview->pixmap().toImage(), empty);
    play->click();
    EXPECT_FALSE(dialog.playing());
    EXPECT_EQ(play->text(), QStringLiteral("Play"));
    const int paused = scrubber->value();
    EXPECT_GE(paused, 10);
    EXPECT_EQ(time->text(), timelapseClock(paused / 30.0));
    dialog.play(true);
    EXPECT_TRUE(waitFor([&] { return !dialog.playing(); }, 3000));
    EXPECT_EQ(scrubber->value(), scrubber->maximum());
    dialog.setScrubberPosition(0);
    dialog.play(true);
    child<QCheckBox>(&dialog, "timelapseLeaveOutUndone")->setChecked(false);
    EXPECT_FALSE(dialog.playing()) << "the frames changed";
}

TEST_F(ExportTimelapseDialogTest, StrokesDrawInFrameByFrame) {
    ExportTimelapseDialog dialog(framedRecording(1, 8), kName, QString());
    child<QSpinBox>(&dialog, "timelapseHoldSeconds")->setValue(0);
    auto* preview = child<QLabel>(&dialog, "timelapsePreview");
    const auto redPixels = [&] {
        const QImage image = preview->pixmap().toImage();
        int n = 0;
        for (int y = 0; y < image.height(); ++y)
            for (int x = 0; x < image.width(); ++x)
                n += qRed(image.pixel(x, y)) > 200 && qGreen(image.pixel(x, y)) < 80 && qBlue(image.pixel(x, y)) < 80;
        return n;
    };
    int before = -1;
    for (int frame = 0; frame <= 8; ++frame) {
        dialog.setScrubberPosition(frame);
        const int now = redPixels();
        EXPECT_GE(now, before) << "frame " << frame;
        if (frame > 1 && frame < 8) {
            EXPECT_GT(now, before) << "frame " << frame << " has more of the stroke than the frame before";
        }
        before = now;
    }
    EXPECT_GT(before, 0);
}

TEST_F(ExportTimelapseDialogTest, TheScrubberShowsThePictureAtThatStep) {
    ExportTimelapseDialog dialog(sampleRecording(2, false), kName, QString());
    auto* preview = child<QLabel>(&dialog, "timelapsePreview");
    child<QSpinBox>(&dialog, "timelapseHoldSeconds")->setValue(0);
    dialog.setScrubberPosition(0);
    const QImage empty = preview->pixmap().toImage();
    ASSERT_FALSE(empty.isNull());
    dialog.setScrubberPosition(2);
    const QImage finished = preview->pixmap().toImage();
    EXPECT_NE(empty, finished);
    EXPECT_EQ(dialog.previewedSize(), QSize(2000, 1000));
    EXPECT_LE(finished.width(), 560);
    EXPECT_LE(finished.height(), 400);
    const auto hasRed = [](const QImage& image) {
        for (int y = 0; y < image.height(); ++y) {
            for (int x = 0; x < image.width(); ++x) {
                if (qRed(image.pixel(x, y)) > 200 && qGreen(image.pixel(x, y)) < 80)
                    return true;
            }
        }
        return false;
    };
    EXPECT_FALSE(hasRed(empty));
    EXPECT_TRUE(hasRed(finished));
}

namespace {

class SoundDialog : public ExportTimelapseDialog {
public:
    using ExportTimelapseDialog::ExportTimelapseDialog;
    QString answer;

protected:
    QString askSoundFile() override { return answer; }
};

}

TEST_F(ExportTimelapseDialogTest, ASoundFileShowsAsAChipThatRemovesIt) {
    SoundDialog dialog(sampleRecording(), kName, QString());
    child<QAbstractButton>(&dialog, "timelapseMoreSettings")->click();
    auto* chip = child<QAbstractButton>(&dialog, "timelapseSoundChip");
    EXPECT_FALSE(chip->isVisibleTo(&dialog));
    dialog.answer = QStringLiteral("/music/rain.ogg");
    child<QAbstractButton>(&dialog, "timelapseSound")->click();
    EXPECT_EQ(dialog.choice().soundFile, QStringLiteral("/music/rain.ogg"));
    EXPECT_TRUE(chip->isVisibleTo(&dialog));
    EXPECT_FALSE(child<QLabel>(&dialog, "timelapseSoundNone")->isVisibleTo(&dialog));
    EXPECT_TRUE(chip->text().contains(QStringLiteral("rain.ogg")));
    chip->click();
    EXPECT_TRUE(dialog.choice().soundFile.isEmpty());
    EXPECT_FALSE(chip->isVisibleTo(&dialog));
    EXPECT_TRUE(child<QLabel>(&dialog, "timelapseSoundNone")->isVisibleTo(&dialog));
}

TEST_F(ExportTimelapseDialogTest, HasQualityRows) {
    ExportTimelapseDialog dialog(sampleRecording(), kName, QString());
    child<QCheckBox>(&dialog, "timelapseCapRate")->setChecked(true);
    child<QCheckBox>(&dialog, "timelapseCapSize")->setChecked(true);
    const TimelapseExportChoice c = dialog.choice();
    EXPECT_EQ(c.maxBitrateKbps, 8000);
    EXPECT_EQ(c.maxFileMb, 512.0);
    auto* quality = child<QWidget>(&dialog, "timelapseQuality");
    const QList<QAbstractButton*> q = quality->findChildren<QAbstractButton*>();
    ASSERT_EQ(q.size(), 3);
    q[2]->click();
    EXPECT_EQ(dialog.choice().crf, 28);
}

namespace {

Document* paintedCanvas(MainWindow* window, int strokes) {
    window->newDocument();
    settle();
    Document* doc = window->tabs()->currentDocument();
    for (int i = 0; i < strokes; ++i)
        paintFramed(*doc, 60 + 90.0 * i, 6, QColor::fromHsv((i * 70) % 360, 230, 235), 40);
    return doc;
}

QDockWidget* dockNamed(MainWindow* window, const char* name) {
    return window->findChild<QDockWidget*>(QString::fromLatin1(name));
}

}

TEST_F(TimelapseUiTest, TimelapseReplayPlaysTheRecordingInTheCanvasWindowAndDoneGivesTheCanvasBack) {
    Document* doc = paintedCanvas(window.get(), 3);
    const QImage finished = doc->projection().copy();
    const int undoIndex = doc->undoStack()->index();
    CanvasView* view = window->tabs()->currentCanvas();
    EXPECT_FALSE(window->isReplaying());
    EXPECT_TRUE(menuAction("file.timelapseReplay")->isEnabled());
    ASSERT_TRUE(pill(doc)->isVisible());

    menuAction("file.timelapseReplay")->trigger();
    settle();
    ASSERT_TRUE(window->isReplaying());
    TimelapseReplayView* replay = window->tabs()->replayOf(doc);
    ASSERT_TRUE(replay);
    EXPECT_EQ(replay->parentWidget(), window->tabs()->windowAt(0));
    EXPECT_EQ(replay->geometry(), view->geometry());
    EXPECT_TRUE(replay->isVisible());
    QLabel* badge = window->tabs()->badgeOf(doc);
    ASSERT_TRUE(badge);
    EXPECT_EQ(badge->text(), QStringLiteral("Timelapse Replay"));
    EXPECT_TRUE(badge->isVisible());
    EXPECT_FALSE(pill(doc)->isVisible());
    EXPECT_LE(badge->geometry().bottom(), DoodleboldStyle::kTitleBarHeight) << "in the header, not over the canvas";
    auto* bar = replay->findChild<QWidget*>(QStringLiteral("replayBar"));
    ASSERT_TRUE(bar);
    EXPECT_EQ(bar->width(), 640);
    EXPECT_NEAR(bar->geometry().center().x(), replay->width() / 2, 1);
    EXPECT_EQ(replay->height() - bar->geometry().bottom() - 1, 18);
    auto* play = replay->findChild<QPushButton*>(QStringLiteral("replayPlay"));
    auto* scrubber = replay->findChild<QSlider*>(QStringLiteral("replayScrubber"));
    auto* clock = replay->findChild<QLabel*>(QStringLiteral("replayClock"));
    auto* exportButton = replay->findChild<QPushButton*>(QStringLiteral("replayExport"));
    auto* done = replay->findChild<QPushButton*>(QStringLiteral("replayDone"));
    ASSERT_TRUE(play && scrubber && clock && exportButton && done);
    EXPECT_EQ(play->text(), QStringLiteral("Play"));
    EXPECT_EQ(exportButton->text(), QStringLiteral("Export …"));
    EXPECT_EQ(done->text(), QStringLiteral("Done"));
    EXPECT_LT(play->x(), scrubber->x());
    EXPECT_LT(scrubber->x(), clock->x());
    EXPECT_LT(clock->x(), exportButton->x());
    EXPECT_LT(exportButton->x(), done->x());
    EXPECT_EQ(replay->frameCount(), 1 + 3 * 7);
    EXPECT_EQ(scrubber->maximum(), replay->frameCount() - 1);
    EXPECT_EQ(replay->position(), replay->frameCount() - 1);
    EXPECT_EQ(clock->text(), QStringLiteral("0:00 / 0:00"));
    EXPECT_EQ(replay->picture().convertToFormat(QImage::Format_RGBA8888_Premultiplied), finished);
    EXPECT_LE(replay->pictureRect().bottom(), bar->geometry().top()) << "the picture is clear of the bar";
    EXPECT_TRUE(test::saveScreenshot(window->grab(), QStringLiteral("timelapse_replay_end")));

    for (const char* name : {"navigatorDock", "colorDock", "toolsDock", "layersDock"}) {
        QDockWidget* dock = dockNamed(window.get(), name);
        ASSERT_TRUE(dock) << name;
        EXPECT_FALSE(dock->isEnabled()) << name;
    }
    EXPECT_FALSE(window->quickBar()->isEnabled());
    for (const char* id : {"edit.undo", "file.save", "layer.newNormal", "file.timelapseReplay", "file.deleteRecording",
                           "file.recordTimelapse", "file.close"})
        EXPECT_FALSE(menuAction(id)->isEnabled()) << id;
    EXPECT_TRUE(menuAction("file.exportTimelapse")->isEnabled());
    EXPECT_TRUE(menuAction("file.new")->isEnabled());
    QMouseEvent press(QEvent::MouseButtonPress, QPointF(200, 200), QPointF(200, 200), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QMouseEvent release(QEvent::MouseButtonRelease, QPointF(200, 200), QPointF(200, 200), Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(replay, &press);
    QApplication::sendEvent(replay, &release);
    EXPECT_FALSE(doc->isPainting());
    EXPECT_EQ(doc->undoStack()->index(), undoIndex);
    EXPECT_EQ(doc->projection(), finished);

    play->click();
    EXPECT_TRUE(replay->playing());
    EXPECT_EQ(play->text(), QStringLiteral("Pause"));
    EXPECT_LT(replay->position(), replay->frameCount() - 1);
    EXPECT_TRUE(waitFor([&] { return replay->position() >= 8; }, 3000));
    EXPECT_NE(replay->picture().convertToFormat(QImage::Format_RGBA8888_Premultiplied), finished) << "strokes are half drawn";
    play->click();
    EXPECT_FALSE(replay->playing());
    EXPECT_EQ(play->text(), QStringLiteral("Play"));
    EXPECT_EQ(scrubber->value(), replay->position());
    scrubber->setValue(0);
    QImage white(doc->size(), QImage::Format_RGBA8888_Premultiplied);
    white.fill(Qt::white);
    EXPECT_EQ(replay->picture().convertToFormat(QImage::Format_RGBA8888_Premultiplied), white);
    scrubber->setValue(replay->frameCount() - 8);
    play->click();
    EXPECT_TRUE(waitFor([&] { return !replay->playing(); }, 3000));
    EXPECT_EQ(replay->position(), replay->frameCount() - 1);
    EXPECT_TRUE(test::saveScreenshot(window->grab(), QStringLiteral("timelapse_replay")));
    scrubber->setValue(replay->frameCount() / 2);
    EXPECT_TRUE(test::saveScreenshot(window->grab(), QStringLiteral("timelapse_replay_mid")));

    done->click();
    settle();
    EXPECT_FALSE(window->isReplaying());
    EXPECT_EQ(window->tabs()->replayOf(doc), nullptr);
    EXPECT_EQ(window->tabs()->badgeOf(doc), nullptr);
    EXPECT_TRUE(pill(doc)->isVisible()) << "the REC mark is back";
    for (const char* name : {"navigatorDock", "colorDock", "toolsDock", "layersDock"})
        EXPECT_TRUE(dockNamed(window.get(), name)->isEnabled()) << name;
    EXPECT_TRUE(window->quickBar()->isEnabled());
    for (const char* id : {"edit.undo", "file.save", "layer.newNormal", "file.timelapseReplay", "file.deleteRecording",
                           "file.recordTimelapse", "file.close"})
        EXPECT_TRUE(menuAction(id)->isEnabled()) << id;
    EXPECT_EQ(doc->projection(), finished);
    EXPECT_EQ(doc->undoStack()->index(), undoIndex);
    EXPECT_EQ(doc->recordedSteps(), 3) << "watching recorded nothing";
    paintFramed(*doc, 400, 4, Qt::black, 30);
    EXPECT_EQ(doc->recordedSteps(), 4);
    EXPECT_TRUE(window->startReplay(doc));
    EXPECT_EQ(window->tabs()->replayOf(doc)->frameCount(), 1 + 3 * 7 + 5);
    window->endReplay();
    EXPECT_FALSE(window->isReplaying());
}

TEST_F(TimelapseUiTest, EscapeEndsTheReplayAndExportAsksForTheExportDialog) {
    Document* doc = paintedCanvas(window.get(), 2);
    ASSERT_TRUE(window->startReplay(doc));
    EXPECT_FALSE(window->startReplay(doc)) << "one replay at a time";
    TimelapseReplayView* replay = window->tabs()->replayOf(doc);
    ASSERT_TRUE(replay);
    int exportAsked = 0;
    QObject::connect(replay, &TimelapseReplayView::exportRequested, [&] { ++exportAsked; });
    replay->setPosition(0);
    replay->play(true);
    ASSERT_TRUE(replay->playing());
    // The window's own handler runs the real controller's dialog: leave it out here by only watching the signal's first part.
    QKeyEvent space(QEvent::KeyPress, Qt::Key_Space, Qt::NoModifier);
    QApplication::sendEvent(replay, &space);
    EXPECT_FALSE(replay->playing()) << "Space is Pause";
    QApplication::sendEvent(replay, &space);
    EXPECT_TRUE(replay->playing()) << "and Play";
    QKeyEvent escape(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
    QApplication::sendEvent(replay, &escape);
    settle();
    EXPECT_FALSE(window->isReplaying());
    EXPECT_EQ(window->tabs()->replayOf(doc), nullptr);
    EXPECT_TRUE(pill(doc)->isVisible());
    EXPECT_EQ(exportAsked, 0);
}

TEST_F(TimelapseUiTest, TheReplayFollowsItsWindowAndEndsWhenAnotherCanvasComesToTheFront) {
    Document* first = paintedCanvas(window.get(), 2);
    Document* second = paintedCanvas(window.get(), 1);
    window->tabs()->setCurrentIndex(0);
    settle();
    ASSERT_EQ(window->tabs()->currentDocument(), first);
    ASSERT_TRUE(window->startReplay(first));
    QMdiSubWindow* sub = window->tabs()->windowAt(0);
    sub->showNormal();
    sub->resize(700, 520);
    settle();
    TimelapseReplayView* replay = window->tabs()->replayOf(first);
    ASSERT_TRUE(replay);
    EXPECT_EQ(replay->geometry(), window->tabs()->canvasAt(0)->geometry()) << "covers the canvas wherever the window puts it";
    auto* bar = replay->findChild<QWidget*>(QStringLiteral("replayBar"));
    EXPECT_LE(bar->width(), 640);
    EXPECT_GE(bar->geometry().left(), 0);
    EXPECT_LE(bar->geometry().right(), replay->width());
    window->tabs()->setCurrentIndex(1);
    settle();
    EXPECT_FALSE(window->isReplaying()) << "the replay belongs to its own canvas";
    EXPECT_EQ(window->tabs()->replayOf(first), nullptr);
    EXPECT_EQ(window->tabs()->currentDocument(), second);
    EXPECT_TRUE(dockNamed(window.get(), "layersDock")->isEnabled());
    second->deleteRecording();
    EXPECT_FALSE(window->startReplay(second));
}

TEST_F(TimelapseUiTest, DeletingTheRecordingAsksFirstAndSaysItCannotBeUndone) {
    Document* doc = newCanvas();
    paintLine(*doc, 40);
    ScriptedTimelapse timelapse(window.get());
    timelapse.deleteAnswer = false;
    EXPECT_FALSE(timelapse.deleteRecording(doc));
    EXPECT_EQ(timelapse.deleteAsked, 1);
    EXPECT_EQ(timelapse.deleteSteps, 1);
    EXPECT_TRUE(doc->isRecording()) << "No keeps it";
    timelapse.deleteAnswer = true;
    EXPECT_TRUE(timelapse.deleteRecording(doc));
    EXPECT_FALSE(doc->isRecording());
    EXPECT_EQ(pill(doc)->text(), QStringLiteral("REC")) << "the grey mark stays";
    EXPECT_FALSE(timelapse.deleteRecording(doc)) << "nothing left to delete, nothing asked";
    EXPECT_EQ(timelapse.deleteAsked, 2);
    const QString question = TimelapseController::deleteQuestion(QStringLiteral("sky.doodle"), 1204);
    EXPECT_TRUE(question.contains(QStringLiteral("cannot be undone")));
    EXPECT_TRUE(question.contains(QString::fromUtf8("1 204")));
    EXPECT_TRUE(question.contains(QStringLiteral("sky.doodle")));
}

TEST_F(TimelapseUiTest, RestartingAsksThenStartsAgainFromThePictureAsItIs) {
    Document* doc = newCanvas();
    paintLine(*doc, 40);
    paintLine(*doc, 90, Qt::blue);
    ScriptedTimelapse timelapse(window.get());
    EXPECT_FALSE(timelapse.restartRecording(doc));
    EXPECT_EQ(timelapse.restartAsked, 1);
    EXPECT_EQ(doc->recordedSteps(), 2) << "No keeps it";
    timelapse.restartAnswer = true;
    EXPECT_TRUE(timelapse.restartRecording(doc));
    EXPECT_TRUE(doc->isRecording());
    EXPECT_EQ(doc->recordedSteps(), 0) << "the old steps are gone";
    EXPECT_EQ(pill(doc)->text(), QStringLiteral("REC · 0 steps"));
    const std::shared_ptr<const Timelapse> recording = doc->timelapse();
    ASSERT_TRUE(recording);
    TimelapseReplay replay(*recording);
    EXPECT_EQ(replay.seek(0).pixelColor(60, 40), doc->projection().pixelColor(60, 40));
    EXPECT_EQ(replay.seek(0).pixelColor(60, 90), doc->projection().pixelColor(60, 90));
    paintLine(*doc, 60, Qt::red);
    EXPECT_EQ(doc->recordedSteps(), 1);
    const QString question = TimelapseController::restartQuestion(QStringLiteral("sky.doodle"), 1204);
    EXPECT_TRUE(question.contains(QStringLiteral("cannot be undone")));
    EXPECT_TRUE(question.contains(RecordingPill::groupedNumber(1204)));
}

TEST_F(TimelapseUiTest, ExportingWritesAnMp4ThroughFfmpeg) {
    const auto& tools = test::systemFfmpeg();
    if (!tools)
        GTEST_SKIP() << "no FFmpeg on this machine";
    QTemporaryDir dir;
    Document* doc = newCanvas();
    paintLine(*doc, 40);
    paintLine(*doc, 90, Qt::blue);
    ScriptedTimelapse timelapse(window.get());
    timelapse.choice = TimelapseExportChoice{};
    timelapse.choice->options.length = TimelapseLength::Full;
    timelapse.choice->crf = 28;
    timelapse.exportPath = dir.filePath(QStringLiteral("t"));
    ASSERT_TRUE(timelapse.exportTimelapse(doc)) << timelapse.errors.join(QLatin1Char('\n')).toStdString();
    const QString file = dir.filePath(QStringLiteral("t.mp4"));
    ASSERT_TRUE(QFileInfo(file).isFile());
    const auto info = test::probeClip(*tools, file);
    ASSERT_TRUE(info);
    EXPECT_EQ(info->size, fitTimelapseSize(doc->size())) << "the canvas fitted within 1920 × 1080";
    EXPECT_NEAR(info->duration, 3.1, 0.2) << "the first picture and two strokes, one frame each at 30 fps, and the hold";

    timelapse.choice.reset();
    timelapse.exportPath = dir.filePath(QStringLiteral("none.mp4"));
    EXPECT_FALSE(timelapse.exportTimelapse(doc));
    EXPECT_FALSE(QFileInfo::exists(timelapse.exportPath));
}

TEST_F(TimelapseUiTest, ExportingARecordingWithoutStepsSaysSo) {
    Document* doc = newCanvas();
    ScriptedTimelapse timelapse(window.get());
    timelapse.choice = TimelapseExportChoice{};
    timelapse.exportPath = QStringLiteral("/nonexistent/never.mp4");
    if (!test::systemFfmpeg())
        GTEST_SKIP() << "no FFmpeg on this machine";
    EXPECT_FALSE(timelapse.exportTimelapse(doc));
    ASSERT_EQ(timelapse.errors.size(), 1);
    EXPECT_TRUE(timelapse.errors[0].contains(QStringLiteral("Nothing has been recorded")));
}

// A pen stroke on the canvas records itself being drawn at any zoom: at 41 %, a common working zoom, the view shows the stroke as
// reduced blocks and its layer waits for the pen to lift, and the frames are those blocks enlarged. About 0.8 s of drawing,
// events every 8 ms with the event loop running between, as under a hand. Offscreen the canvas never paints, so no frames come
// at all: this runs under a display (QT_QPA_PLATFORM=xcb, LIBGL_ALWAYS_SOFTWARE=1, xvfb-run).
TEST_F(TimelapseUiTest, APenStrokeOnTheCanvasRecordsItselfBeingDrawnAtAnyZoom) {
    if (QGuiApplication::platformName() == QLatin1String("offscreen"))
        GTEST_SKIP() << "the canvas paints only on a display";
    for (const double zoom : {0.41, 1.0}) {
        Document* doc = newCanvas();
        CanvasView* canvas = window->tabs()->currentCanvas();
        ASSERT_NE(canvas, nullptr);
        canvas->setZoom(zoom, QPointF(canvas->rect().center()));
        settle();
        const auto pen = [&](QEvent::Type type, const QPointF& at) {
            QMouseEvent e(type, at, canvas->mapToGlobal(at), type == QEvent::MouseMove ? Qt::NoButton : Qt::LeftButton,
                          type == QEvent::MouseButtonRelease ? Qt::NoButton : Qt::LeftButton, Qt::NoModifier);
            QCoreApplication::sendEvent(canvas, &e);
        };
        pen(QEvent::MouseButtonPress, QPointF(5, 5));  // the first click makes the canvas the current one
        pen(QEvent::MouseButtonRelease, QPointF(5, 5));
        settle();
        const std::size_t before = doc->timelapse()->steps.size();
        const QPointF from(canvas->width() * 0.2, canvas->height() * 0.5), to(canvas->width() * 0.8, canvas->height() * 0.5);
        constexpr int kEvents = 100;
        QElapsedTimer clock;
        clock.start();
        for (int i = 0; i <= kEvents; ++i) {
            pen(i == 0 ? QEvent::MouseButtonPress : QEvent::MouseMove, from + (to - from) * (double(i) / kEvents));
            while (clock.elapsed() < (i + 1) * 8)
                QCoreApplication::processEvents(QEventLoop::AllEvents, 2);
        }
        pen(QEvent::MouseButtonRelease, to);
        waitFor([] { return false; }, 500);
        const auto recording = doc->timelapse();
        int partials = 0;
        for (std::size_t i = before; i < recording->steps.size(); ++i)
            partials += recording->steps[i]->kind == TimelapseStep::Kind::Partial;
        EXPECT_GE(partials, 5) << "a frame every tenth of a second of the stroke, at zoom " << zoom;
        EXPECT_EQ(recording->steps.back()->kind, TimelapseStep::Kind::Edit);
        TimelapseReplay replay(*recording);
        EXPECT_EQ(replay.seek(int(recording->steps.size()) - 1).convertToFormat(QImage::Format_RGBA8888_Premultiplied), doc->projection())
            << "the stroke's step is exact";
        std::printf("zoom %.2f: %d frames of the stroke, recording %lld bytes\n", zoom, partials,
                    static_cast<long long>(recording->byteSize()));
    }
}
