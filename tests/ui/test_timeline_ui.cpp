#include "../unit/FfmpegTestSupport.h"
#include "ScreenshotSupport.h"

#include "core/animation/AnimationVideo.h"
#include "core/document/Document.h"
#include "core/options/AppOptions.h"
#include "ui/CanvasView.h"
#include "ui/DocumentTabs.h"
#include "ui/MainWindow.h"
#include "ui/animation/AnimationController.h"
#include "ui/animation/AnimationDialogs.h"
#include "ui/animation/FrameStrip.h"
#include "ui/animation/SoundPlayback.h"
#include "ui/animation/TimelinePanel.h"
#include "ui/animation/TimelineTitleButtons.h"
#include "ui/animation/TimeRuler.h"
#include "ui/animation/TrackHeaders.h"
#include "ui/animation/TrackLabels.h"
#include "core/animation/AnimationOps.h"
#include "ui/panels/LayerTreeModel.h"

#include <QAbstractButton>
#include <QAction>
#include <QDialog>
#include <QPointingDevice>
#include <QTabletEvent>
#include <QElapsedTimer>
#include <QFile>
#include <QLabel>
#include <QMdiArea>
#include <QPushButton>
#include <QRadioButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QWheelEvent>
#include <QWidgetAction>
#include <QStandardPaths>
#include <QSlider>
#include <QMenu>
#include <QSpinBox>
#include <QStatusBar>
#include <QKeyEvent>
#include <QSplitter>
#include <QTemporaryDir>
#include <QMouseEvent>
#include <QToolButton>
#include <QTreeView>

#include <algorithm>
#include <set>

#include <gtest/gtest.h>

#include <cstdio>
#include <functional>
#include <memory>

using namespace doodlebold;

namespace {

void settle() {
    for (int i = 0; i < 5; ++i) {
        QCoreApplication::processEvents();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
    }
}

bool waitFor(const std::function<bool()>& done, int ms) {
    QElapsedTimer clock;
    clock.start();
    while (!done() && clock.elapsed() < ms)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    return done();
}

void mouse(QWidget* w, QEvent::Type type, const QPoint& pos, Qt::KeyboardModifiers mods = {}) {
    const Qt::MouseButtons held = type == QEvent::MouseButtonRelease ? Qt::NoButton : Qt::MouseButtons(Qt::LeftButton);
    const Qt::MouseButton button = type == QEvent::MouseMove ? Qt::NoButton : Qt::LeftButton;
    QMouseEvent e(type, QPointF(pos), w->mapToGlobal(QPointF(pos)), button, held, mods);
    QCoreApplication::sendEvent(w, &e);
}
void press(QWidget* w, const QPoint& pos, Qt::KeyboardModifiers mods = {}) {
    mouse(w, QEvent::MouseButtonPress, pos, mods);
}
void move(QWidget* w, const QPoint& pos) {
    mouse(w, QEvent::MouseMove, pos);
}
void release(QWidget* w, const QPoint& pos, Qt::KeyboardModifiers mods = {}) {
    mouse(w, QEvent::MouseButtonRelease, pos, mods);
}
void click(QWidget* w, const QPoint& pos, Qt::KeyboardModifiers mods = {}) {
    press(w, pos, mods);
    release(w, pos, mods);
}
void click(QAbstractButton* b) {
    b->click();
}

// A control of the timeline by name: an item of one of its menus is triggered, a button pressed and released.
void use(TimelinePanel* panel, const char* name) {
    if (QAction* a = panel->action(QString::fromLatin1(name))) {
        a->trigger();
        return;
    }
    QAbstractButton* b = panel->button(QString::fromLatin1(name));
    ASSERT_TRUE(b) << name;
    click(static_cast<QWidget*>(b), b->rect().center());
}

std::vector<int> holds(const Animation& a) {
    std::vector<int> out;
    for (const AnimationFrame& f : a.track()->frames)
        out.push_back(f.hold);
    return out;
}

// A controller that answers its questions without dialogs.
class ScriptedAnimation : public AnimationController {
public:
    using AnimationController::AnimationController;
    bool keepAnswer = true;
    int keepAsked = 0;
    QString videoFile;
    std::optional<AddVideoChoice> videoChoice;
    QString exportPath;
    QStringList errors;

protected:
    bool confirmKeepOnly(int, int) override {
        ++keepAsked;
        return keepAnswer;
    }
    QString askVideoFile() override { return videoFile; }
    std::optional<AddVideoChoice> askAddVideo(const QString&, const VideoInfo&, const Document&) override { return videoChoice; }
    QString askExportPath(const QString&) override { return exportPath; }
    void showError(const QString&, const QString& text) override { errors << text; }
};

class TimelineUiTest : public testing::Test {
protected:
    void SetUp() override {
        QStandardPaths::setTestModeEnabled(true);
        test::applyTheme();
        AppOptions::settings().clear();
        window = std::make_unique<MainWindow>();
        window->resize(1400, 900);
        window->show();
        settle();
        window->newDocument();
        settle();
        doc = window->tabs()->currentDocument();
    }
    QAction* command(const char* id) { return window->command(QString::fromLatin1(id)); }
    TimelinePanel* panel() { return window->animation()->panel(); }
    FrameStrip* strip() { return panel()->strip(); }
    QWidget* card() { return window->tabs()->timelinePanel(); }
    TimelineTitleButtons* titleButtons() { return window->tabs()->timelineButtonsOf(doc); }
    void showTimeline() {
        if (!command("canvas.showTimeline")->isChecked())
            command("canvas.showTimeline")->trigger();
        settle();
    }
    void frames(int count) {
        while (doc->frameCount() < count)
            command("canvas.newFrame")->trigger();
        settle();
    }
    std::unique_ptr<MainWindow> window;
    Document* doc = nullptr;
};

// A controller of its own, on a panel of its own, for the dialogs' answers.
struct ControllerRig {
    TimelinePanel panel;
    ScriptedAnimation controller{&panel, [](bool) { return test::systemFfmpeg(); }, nullptr};
    Document doc{QSize(64, 48), Qt::white};
    ControllerRig() {
        panel.resize(900, 360);  // the bars over and under the lanes, and the lanes' least height
        controller.setDocument(&doc);
        controller.setTimelineShown(true);
    }
};

}

TEST_F(TimelineUiTest, ShowTimelineOpensItUnderTheCanvasAndTheTitleBarButtonFollows) {
    ASSERT_TRUE(card());
    EXPECT_FALSE(card()->isVisible());
    QAction* show = command("canvas.showTimeline");
    ASSERT_TRUE(show);
    EXPECT_TRUE(show->isCheckable());
    EXPECT_EQ(show->shortcut(), QKeySequence(QStringLiteral("Shift+T")));
    ASSERT_TRUE(titleButtons());
    EXPECT_FALSE(titleButtons()->timelineButton()->isChecked());
    show->trigger();
    settle();
    EXPECT_TRUE(card()->isVisible());
    EXPECT_TRUE(titleButtons()->timelineButton()->isChecked());
    EXPECT_GT(card()->geometry().top(), window->tabs()->area()->geometry().top());  // under the canvases
    // The title bar's button hides it again.
    click(titleButtons()->timelineButton());
    settle();
    EXPECT_FALSE(card()->isVisible());
    EXPECT_FALSE(show->isChecked());
}

TEST_F(TimelineUiTest, TheFrameCommandsAreInTheCanvasMenuWithTheirKeys) {
    EXPECT_EQ(command("canvas.newFrame")->shortcut(), QKeySequence(QStringLiteral("Shift+N")));
    EXPECT_FALSE(command("canvas.deleteFrame")->isEnabled());
    EXPECT_FALSE(command("canvas.keepOnlyFrame")->isEnabled());
    EXPECT_FALSE(command("canvas.removeVideo")->isEnabled());
    command("canvas.newFrame")->trigger();
    settle();
    EXPECT_EQ(doc->frameCount(), 2);
    EXPECT_TRUE(command("canvas.deleteFrame")->isEnabled());
    EXPECT_TRUE(command("canvas.keepOnlyFrame")->isEnabled());
    command("canvas.duplicateFrame")->trigger();
    EXPECT_EQ(doc->frameCount(), 3);
    command("canvas.deleteFrame")->trigger();
    EXPECT_EQ(doc->frameCount(), 2);
    EXPECT_EQ(command("animation.previousFrame")->shortcut(), QKeySequence(QStringLiteral(",")));
    EXPECT_EQ(command("animation.nextFrame")->shortcut(), QKeySequence(QStringLiteral(".")));
    const int now = doc->currentFrame();
    command("animation.previousFrame")->trigger();
    EXPECT_EQ(doc->currentFrame(), now - 1);
    command("animation.nextFrame")->trigger();
    EXPECT_EQ(doc->currentFrame(), now);
}

TEST_F(TimelineUiTest, TheTitleBarShowsTheFrameWhileTheTimelineIsHidden) {
    frames(3);
    ASSERT_TRUE(titleButtons()->frameChip()->isVisible());
    EXPECT_EQ(titleButtons()->frameText(), QStringLiteral("Frame 03 / 03"));
    click(titleButtons()->findChild<QToolButton*>(QStringLiteral("frameChipPrevious")));
    EXPECT_EQ(doc->currentFrame(), 1);
    EXPECT_EQ(titleButtons()->frameText(), QStringLiteral("Frame 02 / 03"));
    EXPECT_EQ(window->findChild<QLabel*>(QStringLiteral("frameStatus"))->text(), QStringLiteral("Frame 2 of 3"));
    showTimeline();
    EXPECT_FALSE(titleButtons()->frameChip()->isVisible());
}

TEST_F(TimelineUiTest, TheButtonsAddHoldAndDeleteFrames) {
    showTimeline();
    use(panel(), "timelineNewFrame");
    use(panel(), "timelineNewFrame");
    EXPECT_EQ(doc->frameCount(), 3);
    // One Hold item (it asks for the beats) in place of Hold +1, Hold −1 and a field, in the Frame menu.
    EXPECT_FALSE(panel()->action(QStringLiteral("timelineHoldMore")));
    EXPECT_FALSE(panel()->action(QStringLiteral("timelineHoldLess")));
    ASSERT_TRUE(panel()->action(QStringLiteral("timelineHoldFrame")));
    EXPECT_EQ(panel()->action(QStringLiteral("timelineHoldFrame"))->text(), QStringLiteral("Hold …"));
    emit panel() -> holdSetRequested(2);  // what its question answers
    EXPECT_EQ(doc->animation().holdOf(2), 2);
    use(panel(), "timelineDuplicate");
    EXPECT_EQ(doc->frameCount(), 4);
    EXPECT_EQ(doc->animation().holdOf(3), 2);
    use(panel(), "timelineDelete");
    EXPECT_EQ(doc->frameCount(), 3);
}

TEST_F(TimelineUiTest, ClickingAFrameGoesToItAndABoxSelectsSeveral) {
    showTimeline();
    frames(4);
    settle();
    const QRect first = strip()->cellRect(0);
    click(strip(), first.center());
    EXPECT_EQ(doc->currentFrame(), 0);
    // A box from below the second frame's label to the fourth.
    const QPoint from(strip()->cellRect(1).left() + 4, strip()->cellRect(1).bottom() + FrameStrip::kLabelHeight + 4);
    const QPoint to(strip()->cellRect(3).center());
    press(strip(), QPoint(from.x(), strip()->height() - 2));
    move(strip(), to);
    release(strip(), to);
    EXPECT_EQ(panel()->selectedFrames(), (std::vector<int>{1, 2, 3}));
    EXPECT_EQ(window->findChild<QLabel*>(QStringLiteral("frameStatus"))->text(), QStringLiteral("3 frames selected"));
    emit panel() -> holdSetRequested(2);
    EXPECT_EQ(doc->animation().totalBeats(), 7);
}

TEST_F(TimelineUiTest, SelectedFramesMoveAsAGroup) {
    showTimeline();
    frames(4);
    std::vector<int> ids;
    for (const AnimationFrame& f : doc->animation().track()->frames)
        ids.push_back(f.id);
    click(strip(), strip()->cellRect(0).center());
    click(strip(), strip()->cellRect(1).center(), Qt::ShiftModifier);
    ASSERT_EQ(panel()->selectedFrames(), (std::vector<int>{0, 1}));
    const QPoint grab = strip()->cellRect(0).center();
    const QPoint drop(strip()->cellRect(3).right() + 3, grab.y());
    press(strip(), grab);
    move(strip(), grab + QPoint(20, 0));
    move(strip(), drop);
    release(strip(), drop);
    std::vector<int> now;
    for (const AnimationFrame& f : doc->animation().track()->frames)
        now.push_back(f.id);
    EXPECT_EQ(now, (std::vector<int>{ids[2], ids[3], ids[0], ids[1]}));
}

TEST_F(TimelineUiTest, DraggingAFramesRightEdgeHoldsItAsOneUndoStep) {
    showTimeline();
    frames(2);
    const QRect cell = strip()->cellRect(0);
    const QPoint edge(cell.right(), cell.center().y());
    ASSERT_TRUE(strip()->onRightEdge(0, edge));
    press(strip(), edge);
    move(strip(), edge + QPoint(strip()->beatWidth(), 0));
    move(strip(), edge + QPoint(strip()->beatWidth() * 2, 0));
    release(strip(), edge + QPoint(strip()->beatWidth() * 2, 0));
    EXPECT_EQ(doc->animation().holdOf(0), 3);
    ASSERT_TRUE(doc->undo());
    EXPECT_EQ(doc->animation().holdOf(0), 1);
}

TEST_F(TimelineUiTest, TheLayersPanelMarksSharedLayersAndPinsThemAtTheBottom) {
    const LayerId sky = doc->currentLayerId();
    ASSERT_TRUE(doc->addLayer());
    frames(2);
    ASSERT_TRUE(doc->goToFrame(0));  // the frame the two layers belong to
    auto* tree = window->findChild<QTreeView*>(QStringLiteral("layerTree"));
    ASSERT_TRUE(tree);
    auto* model = static_cast<LayerTreeModel*>(tree->model());
    const QModelIndex skyRow = model->indexOf(sky);
    ASSERT_TRUE(skyRow.isValid());
    EXPECT_TRUE(skyRow.data(LayerTreeModel::ShareableRole).toBool());
    EXPECT_FALSE(skyRow.data(LayerTreeModel::SharedRole).toBool());
    ASSERT_TRUE(model->setData(skyRow, true, LayerTreeModel::SharedRole));
    EXPECT_TRUE(doc->animation().isShared(sky));
    EXPECT_EQ(model->indexOf(sky).row(), model->rowCount() - 1);  // the bottom row
    EXPECT_TRUE(model->indexOf(sky).data(LayerTreeModel::SharedRole).toBool());
    ASSERT_TRUE(model->setData(model->indexOf(sky), false, LayerTreeModel::SharedRole));
    EXPECT_FALSE(doc->animation().isShared(sky));
    ASSERT_TRUE(doc->keepOnlyCurrentFrame());  // a picture again shows no marks
    EXPECT_FALSE(model->indexOf(sky).data(LayerTreeModel::ShareableRole).toBool());
}

TEST_F(TimelineUiTest, PlaybackRunsThroughTheFramesAndLoops) {
    showTimeline();
    frames(3);
    ASSERT_TRUE(doc->setPlayback(30, true));
    ASSERT_TRUE(doc->goToFrame(0));
    AnimationController* c = window->animation();
    c->togglePlay();
    ASSERT_TRUE(c->isPlaying());
    EXPECT_EQ(panel()->button(QStringLiteral("timelinePlay"))->text(), QStringLiteral("Stop"));
    EXPECT_FALSE(doc->hasUnderlay());  // no onion skins while it plays
    std::set<int> seen;
    bool looped = false;
    int last = 0;
    waitFor(
        [&] {
            const int f = doc->currentFrame();
            looped = looped || f < last;
            last = f;
            seen.insert(f);
            return looped && seen.size() == 3;
        },
        3000);
    EXPECT_EQ(seen.size(), 3u);
    EXPECT_TRUE(looped);
    c->togglePlay();
    EXPECT_FALSE(c->isPlaying());
    EXPECT_TRUE(doc->hasUnderlay());
    ASSERT_TRUE(doc->setPlayback(30, false));
    (void)doc->goToFrame(0);
    c->togglePlay();
    EXPECT_TRUE(waitFor([&] { return !c->isPlaying(); }, 3000));  // without a loop it stops at the end
    EXPECT_EQ(doc->currentFrame(), 2);
}

TEST_F(TimelineUiTest, OnionSkinButtonTogglesTheNeighbours) {
    showTimeline();
    frames(2);
    QAction* onion = panel()->action(QStringLiteral("timelineOnionSkin"));
    ASSERT_TRUE(onion->isChecked());
    EXPECT_TRUE(doc->hasUnderlay());
    onion->trigger();
    EXPECT_FALSE(doc->animation().view.onionSkin);
    EXPECT_FALSE(doc->hasUnderlay());
    EXPECT_FALSE(doc->isModified() && doc->undoStack()->undoText() == QStringLiteral("Onion skin"));
}

TEST_F(TimelineUiTest, KeepOnlyThisFrameAsksFirst) {
    ControllerRig rig;
    ASSERT_TRUE(rig.doc.newFrame());
    ASSERT_TRUE(rig.doc.newFrame());
    rig.controller.keepAnswer = false;
    EXPECT_FALSE(rig.controller.keepOnlyThisFrame());
    EXPECT_EQ(rig.doc.frameCount(), 3);
    rig.controller.keepAnswer = true;
    EXPECT_TRUE(rig.controller.keepOnlyThisFrame());
    EXPECT_EQ(rig.doc.frameCount(), 1);
    EXPECT_EQ(rig.controller.keepAsked, 2);
    EXPECT_EQ(keepOnlyQuestion(4, 2),
              QStringLiteral("Delete the other 3 frames and keep frame 03 as an ordinary picture?\n\nYou can undo this."));
    EXPECT_EQ(keepOnlyButtonText(2), QStringLiteral("Keep Frame 03"));
}

TEST_F(TimelineUiTest, ASoundClipGoesOnATrackOfItsOwnAtTheBottomAtTheCurrentFrame) {
    ControllerRig rig;
    ASSERT_TRUE(rig.doc.newFrame());
    ASSERT_TRUE(rig.controller.addSoundFile(QStringLiteral("/sounds/a.wav"), QByteArray("RIFF"), 0.5));
    ASSERT_EQ(rig.doc.animation().sounds.size(), 1u);
    const SoundClip& clip = rig.doc.animation().sounds.front();
    EXPECT_EQ(clip.name, QStringLiteral("a.wav"));
    EXPECT_EQ(clip.startBeat, 1);
    rig.panel.show();
    settle();
    const QRect r = rig.panel.strip()->soundRect(clip.id);
    EXPECT_GT(r.top(), rig.panel.strip()->cellRect(0).bottom());  // its track is at the bottom, under the frames track
    EXPECT_EQ(r.left(), rig.panel.strip()->cellRect(1).left());
    EXPECT_EQ(rig.doc.animation().tracks[0].kind, TrackKind::Sound);
}

TEST_F(TimelineUiTest, TheAddVideoDialogOffersTheTimingsRatesAndCache) {
    VideoInfo info;
    info.size = QSize(1920, 1080);
    info.fps = 30;
    info.seconds = 9.4;
    info.hasSound = true;
    Animation a;
    AddVideoDialog dialog(QStringLiteral("walk-cycle.mp4"), info, a, QSize(2000, 1125));
    for (const char* name : {"videoFollow", "videoKeepTiming", "videoRate12", "videoRate15", "videoRate16", "videoRate24",
                             "videoRateSource", "videoRateCustom", "videoStartFirst", "videoStartCurrent", "videoOpacity", "videoKeepSound",
                             "videoCacheJpeg", "videoCacheLossless", "addVideoButton"})
        EXPECT_TRUE(dialog.findChild<QWidget*>(QString::fromLatin1(name))) << name;
    dialog.setRate(16);
    EXPECT_EQ(dialog.pictureCount(), 150);
    EXPECT_TRUE(dialog.summary().contains(QStringLiteral("150 frames")));
    EXPECT_DOUBLE_EQ(dialog.choice().fps, 16.0);
    EXPECT_TRUE(dialog.choice().follow);
    EXPECT_TRUE(dialog.choice().keepSound);
    dialog.setFollow(false);
    EXPECT_DOUBLE_EQ(dialog.choice().fps, a.fps);  // keeps the animation's timing
}

TEST_F(TimelineUiTest, AVideoGoesUnderTheFramesAndAnAnimationExportsAsAVideo) {
    if (!test::systemFfmpeg())
        GTEST_SKIP() << "no FFmpeg on this machine";
    QTemporaryDir dir;
    ControllerRig rig;
    ASSERT_TRUE(rig.doc.fillLayer(rig.doc.currentLayerId(), QColor(200, 30, 30)));
    ASSERT_TRUE(rig.doc.newFrame());
    ASSERT_TRUE(rig.doc.fillLayer(rig.doc.currentLayerId(), QColor(30, 30, 200)));
    rig.controller.exportPath = dir.filePath(QStringLiteral("anim.mp4"));
    ASSERT_TRUE(rig.controller.exportVideo()) << rig.controller.errors.join(QLatin1Char('\n')).toStdString();
    const auto info = probeVideo(*test::systemFfmpeg(), rig.controller.exportPath);
    ASSERT_TRUE(info);
    EXPECT_EQ(info.value().size, QSize(64, 48));

    // New from Video makes a canvas of the video's size with the video under its frames.
    Document* made = nullptr;
    QObject::connect(&rig.controller, &AnimationController::documentCreated, [&](Document* d) { made = d; });
    rig.controller.videoFile = rig.controller.exportPath;
    AddVideoChoice choice;
    choice.fps = 12;
    choice.opacity = 100;
    choice.keepSound = false;
    rig.controller.videoChoice = choice;
    ASSERT_TRUE(rig.controller.newFromVideo());
    ASSERT_TRUE(made);
    std::unique_ptr<Document> owned(made);
    EXPECT_EQ(made->size(), QSize(64, 48));
    ASSERT_TRUE(made->animation().video);
    EXPECT_EQ(made->frameCount(), 2);
    rig.controller.waitForVideo();
    ASSERT_TRUE(rig.controller.videoFrames(made));
    EXPECT_TRUE(made->hasUnderlay());
    EXPECT_GT(qRed(made->displayImage().pixel(32, 24)), 150);
    ASSERT_TRUE(made->goToFrame(1));
    rig.controller.waitForVideo();  // the picture is read on the loader's thread
    EXPECT_GT(qBlue(made->displayImage().pixel(32, 24)), 150);
    EXPECT_EQ(made->projection().pixel(32, 24), 0xffffffffu);  // never painted on
    rig.controller.removeVideo();
    EXPECT_FALSE(made->animation().video);
    EXPECT_EQ(made->displayImage().pixel(32, 24), 0xffffffffu);
    rig.controller.setDocument(nullptr);
}

// For looking at by hand (DOODLEBOLD_SCREENSHOT_DIR): a ball over a shared sky, held frames, a sound clip, with the timeline
// shown, then hidden with the frame chip in the title bar.
TEST_F(TimelineUiTest, ScreenshotsOfTheTimeline) {
    const LayerId sky = doc->currentLayerId();
    ASSERT_TRUE(doc->fillLayer(sky, QColor(208, 226, 246)));
    ASSERT_TRUE(doc->addLayer());
    ASSERT_TRUE(doc->setLayerShared(sky, true));
    for (int i = 0; i < 8; ++i) {
        if (i > 0) {
            ASSERT_TRUE(doc->newFrame());
        }
        auto target = doc->paintTarget(doc->currentLayerId());
        ASSERT_TRUE(target);
        const QRect ball(200 + i * 120, 150 + (i % 3) * 60, 120, 80);
        target.value()->fill(ball, QColor(232, 86, 72));
        doc->notifyPixelsChanged(doc->currentLayerId(), ball);
    }
    ASSERT_TRUE(doc->changeHold({0, 1, 2}, +1));
    ASSERT_TRUE(doc->changeHold({4}, +2));
    SoundClip clip;
    clip.name = QStringLiteral("a.wav");
    clip.data = std::make_shared<const QByteArray>("RIFF");
    clip.seconds = 0.5;
    clip.startBeat = 3;
    ASSERT_TRUE(doc->addSound(clip));
    ASSERT_TRUE(doc->goToFrame(4));
    showTimeline();
    waitFor([] { return false; }, 400);  // the thumbnails fill in
    EXPECT_TRUE(test::saveScreenshot(window->grab(), QStringLiteral("timeline_window")));
    EXPECT_TRUE(test::saveScreenshot(card()->grab(), QStringLiteral("timeline_panel")));
    command("canvas.showTimeline")->trigger();
    settle();
    EXPECT_TRUE(test::saveScreenshot(window->grab(), QStringLiteral("timeline_hidden_chip")));
    // A second track over the first: a still frame held for the first three beats; the playhead on beat 6.
    command("canvas.showTimeline")->trigger();
    ASSERT_TRUE(doc->addTrack());
    ASSERT_TRUE(doc->fillLayer(doc->currentLayerId(), QColor(60, 160, 90, 120)));
    ASSERT_TRUE(doc->changeHold({0}, +2));
    ASSERT_TRUE(doc->renameTrack(1, QStringLiteral("Grass")));
    ASSERT_TRUE(doc->goToBeat(5));
    window->tabs()->setTimelineHeight(380);
    waitFor([] { return false; }, 400);
    EXPECT_TRUE(test::saveScreenshot(card()->grab(), QStringLiteral("timeline_tracks")));
}

// The test: a song-length video followed by the drawings (thousands of frames) must not make the timeline slow. Only
// what is on screen is painted and frames' places are kept, so painting, clicking and dragging a box cost the same as for a
// few frames (before, each mouse move of a box took over 20 ms here).
TEST_F(TimelineUiTest, AThousandsOfFramesTimelineStaysQuick) {
    ControllerRig rig;
    VideoReference v;
    v.path = QStringLiteral("/nowhere/long.mp4");
    v.sourceSize = QSize(640, 360);
    v.sourceFps = 24.0;
    v.seconds = 250.0;
    v.fps = 12.0;
    ASSERT_TRUE(rig.doc.setVideo(v, true));
    ASSERT_GE(rig.doc.frameCount(), 3000);
    rig.panel.resize(1200, 300);
    rig.panel.show();
    settle();
    FrameStrip* s = rig.panel.strip();
    s->setBeatWidth(40);
    s->setLaneScale(100);  // the beats' labels shown: the box below starts on them
    settle();
    QElapsedTimer clock;
    clock.start();
    for (int i = 0; i < 10; ++i) {
        s->repaint();
        EXPECT_EQ(s->frameAt(s->cellRect(2500).center()), 2500);
        click(s, s->cellRect(i * 2).center());
    }
    const QPoint start(5, s->cellRect(0).bottom() + FrameStrip::kLabelHeight / 2);
    press(s, start);
    for (int i = 1; i <= 50; ++i) {
        move(s, start + QPoint(i * 20, -40));
        s->repaint();
    }
    release(s, start + QPoint(1000, -40));
    EXPECT_LT(clock.elapsed(), 600);
    EXPECT_GE(s->selectedFrames().size(), 20u);
}

namespace {

QKeyEvent key(QEvent::Type type, int code, Qt::KeyboardModifiers mods = {}) {
    return QKeyEvent(type, code, mods);
}

// A 16-bit mono WAV of `seconds`: silent for its first half, a loud square wave for the rest.
QByteArray halfLoudWav(double seconds) {
    const int rate = 8000;
    const int count = static_cast<int>(seconds * rate);
    QByteArray pcm;
    for (int i = 0; i < count; ++i) {
        const qint16 v = static_cast<qint16>(i < count / 2 ? 0 : (i % 20 < 10 ? 30000 : -30000));
        pcm.append(reinterpret_cast<const char*>(&v), 2);
    }
    QByteArray wav("RIFF");
    const auto u32 = [&](quint32 v) { wav.append(reinterpret_cast<const char*>(&v), 4); };
    const auto u16 = [&](quint16 v) { wav.append(reinterpret_cast<const char*>(&v), 2); };
    u32(static_cast<quint32>(36 + pcm.size()));
    wav.append("WAVEfmt ");
    u32(16);
    u16(1);
    u16(1);
    u32(rate);
    u32(rate * 2);
    u16(2);
    u16(16);
    wav.append("data");
    u32(static_cast<quint32>(pcm.size()));
    return wav + pcm;
}

}

// The timeline showed for every canvas at once. Each canvas keeps its own open or closed.
TEST_F(TimelineUiTest, EachCanvasHasItsOwnTimeline) {
    showTimeline();
    Document* first = doc;
    EXPECT_TRUE(card()->isVisible());
    window->newDocument();
    settle();
    Document* second = window->tabs()->currentDocument();
    ASSERT_NE(second, first);
    EXPECT_FALSE(card()->isVisible());
    EXPECT_FALSE(command("canvas.showTimeline")->isChecked());
    EXPECT_FALSE(window->tabs()->timelineButtonsOf(second)->timelineButton()->isChecked());
    EXPECT_TRUE(window->tabs()->timelineButtonsOf(first)->timelineButton()->isChecked());
    window->tabs()->setCurrentIndex(0);
    settle();
    EXPECT_EQ(window->tabs()->currentDocument(), first);
    EXPECT_TRUE(card()->isVisible());
    EXPECT_TRUE(command("canvas.showTimeline")->isChecked());
}

TEST_F(TimelineUiTest, TheTimelineIsMadeTallerOrShorterWithAHandle) {
    showTimeline();
    auto* splitter = window->findChild<QSplitter*>(QStringLiteral("timelineSplitter"));
    ASSERT_TRUE(splitter);
    EXPECT_EQ(splitter->widget(1), card());
    window->tabs()->setTimelineHeight(420);
    settle();
    EXPECT_NEAR(card()->height(), 420, 2);
    window->tabs()->setTimelineHeight(190);
    settle();
    EXPECT_NEAR(card()->height(), 190, 2);
    EXPECT_GT(strip()->parentWidget()->height(), 0);
}

// Clips always looked selected. Only the clicked one is drawn in the accent colour, and Delete removes it.
TEST_F(TimelineUiTest, AClickedSoundClipIsSelectedAndDeleteRemovesIt) {
    ControllerRig rig;
    ASSERT_TRUE(rig.controller.addSoundFile(QStringLiteral("/sounds/a.wav"), QByteArray("RIFF"), 2.0));
    const int id = rig.doc.animation().sounds.front().id;
    rig.panel.show();
    settle();
    FrameStrip* s = rig.panel.strip();
    const QRect r = s->soundRect(id);
    const QPoint inside(r.center().x(), r.bottom() - 3);
    const QRgb before = s->grab().toImage().pixel(inside);
    EXPECT_TRUE(s->selectedSounds().empty());
    click(s, r.center());
    EXPECT_EQ(s->selectedSounds(), std::vector<int>{id});
    EXPECT_NE(s->grab().toImage().pixel(inside), before);
    auto overrideDelete = key(QEvent::ShortcutOverride, Qt::Key_Delete);
    QCoreApplication::sendEvent(s, &overrideDelete);
    EXPECT_TRUE(overrideDelete.isAccepted());  // the timeline's, not the menus'
    auto press = key(QEvent::KeyPress, Qt::Key_Delete);
    QCoreApplication::sendEvent(s, &press);
    EXPECT_TRUE(rig.doc.animation().sounds.empty());
    EXPECT_TRUE(s->selectedSounds().empty());
}

TEST_F(TimelineUiTest, DraggingASoundClipsEndsTrimsIt) {
    ControllerRig rig;
    ASSERT_TRUE(rig.controller.addSoundFile(QStringLiteral("/sounds/a.wav"), QByteArray("RIFF"), 2.0));
    const int id = rig.doc.animation().sounds.front().id;
    rig.panel.show();
    settle();
    FrameStrip* s = rig.panel.strip();
    const int beat = s->beatWidth();
    QRect r = s->soundRect(id);
    QPoint end(r.right() - 2, r.center().y());
    press(s, end);
    move(s, end - QPoint(6 * beat, 0));
    release(s, end - QPoint(6 * beat, 0));
    const auto clip = [&] { return rig.doc.animation().sounds.front(); };
    EXPECT_NEAR(clip().trimEnd, 0.5, 0.01);
    EXPECT_NEAR(clip().playedSeconds(), 1.5, 0.01);
    r = s->soundRect(id);
    const QPoint start(r.left() + 2, r.center().y());
    press(s, start);
    move(s, start + QPoint(2 * beat, 0));
    release(s, start + QPoint(2 * beat, 0));
    EXPECT_EQ(clip().startBeat, 2);
    EXPECT_NEAR(clip().trimStart, 2.0 / 12.0, 1e-9);
    EXPECT_EQ(s->soundRect(id).left(), r.left() + 2 * beat);
    ASSERT_TRUE(rig.doc.undo());
    EXPECT_EQ(rig.doc.animation().sounds.front().startBeat, 0);
}

TEST_F(TimelineUiTest, ASoundClipCanStartAtAnExactSecond) {
    ControllerRig rig;
    ASSERT_TRUE(rig.controller.addSoundFile(QStringLiteral("/sounds/a.wav"), QByteArray("RIFF"), 1.0));
    const int id = rig.doc.animation().sounds.front().id;
    rig.controller.setSoundStart(id, 4.0);
    EXPECT_EQ(rig.doc.animation().sounds.front().startBeat, 48);
    EXPECT_DOUBLE_EQ(rig.doc.animation().sounds.front().startSeconds(12.0), 4.0);
}

TEST_F(TimelineUiTest, ASoundClipShowsItsWaveform) {
    if (!test::systemFfmpeg())
        GTEST_SKIP() << "no FFmpeg on this machine";
    ControllerRig rig;
    ASSERT_TRUE(rig.controller.addSoundFile(QStringLiteral("/sounds/a.wav"), halfLoudWav(1.0), 1.0));
    const SoundClip clip = rig.doc.animation().sounds.front();
    rig.panel.show();
    settle();
    EXPECT_FALSE(rig.controller.soundPeaks(clip));  // measured on a thread, the first time it is asked for
    rig.controller.waitForPeaks();
    const SoundPeaks* peaks = rig.controller.soundPeaks(clip);
    ASSERT_TRUE(peaks);
    EXPECT_LT(peaks->loudest(0.1, 0.4), 5);
    EXPECT_GT(peaks->loudest(0.6, 0.9), 200);
    // Drawn: the loud half has lines reaching near the clip's edges, the quiet half only a hairline.
    const QRect r = rig.panel.strip()->soundRect(clip.id);
    const QImage shot = rig.panel.strip()->grab().toImage();
    const QColor body = shot.pixelColor(r.left() + r.width() / 4, r.top() + 8);
    EXPECT_NE(shot.pixelColor(r.left() + r.width() * 3 / 4, r.top() + 8), body);
}

TEST_F(TimelineUiTest, CtrlASelectsEveryFrameAndTheBeatNumbersStartABox) {
    showTimeline();
    frames(5);
    panel()->findChild<QSlider*>(QStringLiteral("timelineTrackHeight"))->setValue(100);  // the beats' labels shown
    auto override = key(QEvent::ShortcutOverride, Qt::Key_A, Qt::ControlModifier);
    QCoreApplication::sendEvent(strip(), &override);
    EXPECT_TRUE(override.isAccepted());
    auto selectAll = key(QEvent::KeyPress, Qt::Key_A, Qt::ControlModifier);
    QCoreApplication::sendEvent(strip(), &selectAll);
    EXPECT_EQ(panel()->selectedFrames().size(), 5u);
    // A box can start on the beat numbers under the frames …
    const QPoint from = strip()->labelRect(1).center();
    press(strip(), from);
    move(strip(), strip()->cellRect(3).center());
    release(strip(), strip()->cellRect(3).center());
    EXPECT_EQ(panel()->selectedFrames(), (std::vector<int>{1, 2, 3}));
    // … and a click on one still picks its frame.
    click(strip(), strip()->labelRect(4).center());
    EXPECT_EQ(doc->currentFrame(), 4);
    EXPECT_EQ(panel()->selectedFrames(), (std::vector<int>{4}));
}

TEST_F(TimelineUiTest, HoldFrameTakesAnExactNumberAndLoopIsOnTheTimeline) {
    showTimeline();
    frames(3);
    click(strip(), strip()->cellRect(0).center());
    click(strip(), strip()->cellRect(1).center(), Qt::ShiftModifier);
    emit panel() -> holdSetRequested(5);  // Hold Frame …'s answer, for every selected frame
    EXPECT_EQ(holds(doc->animation()), (std::vector<int>{5, 5, 1}));
    QAction* loop = panel()->action(QStringLiteral("timelineLoop"));
    ASSERT_TRUE(loop);
    EXPECT_TRUE(loop->isCheckable());
    EXPECT_EQ(loop->isChecked(), doc->animation().loop);
    const bool was = doc->animation().loop;
    loop->trigger();
    EXPECT_EQ(doc->animation().loop, !was);
    EXPECT_EQ(loop->isChecked(), !was);
}

// The share button was easy to miss: with the timeline open every top-level layer shows it, even before a second frame.
TEST_F(TimelineUiTest, TheShareButtonShowsOnceTheTimelineIsOpen) {
    auto* tree = window->findChild<QTreeView*>(QStringLiteral("layerTree"));
    auto* model = static_cast<LayerTreeModel*>(tree->model());
    const QModelIndex row = model->indexOf(doc->currentLayerId());
    EXPECT_FALSE(row.data(LayerTreeModel::ShareableRole).toBool());
    showTimeline();
    EXPECT_TRUE(model->indexOf(doc->currentLayerId()).data(LayerTreeModel::ShareableRole).toBool());
    ASSERT_TRUE(model->setData(model->indexOf(doc->currentLayerId()), true, LayerTreeModel::SharedRole));
    EXPECT_TRUE(doc->animation().isShared(doc->currentLayerId()));
}

// The tracks: frames on one track draw over another's; each has a row with an eye, its name, Clear and a menu.
TEST_F(TimelineUiTest, TracksHaveTheirOwnLanesAndHeaders) {
    showTimeline();
    frames(3);
    use(panel(), "timelineAddTrackButton");
    settle();
    ASSERT_EQ(doc->animation().trackCount(), 2);
    EXPECT_EQ(doc->currentTrack(), 1);
    EXPECT_LT(strip()->laneTop(1), strip()->laneTop(0));  // the upper track on top
    TrackHeaders* headers = panel()->headers();
    ASSERT_TRUE(headers);
    EXPECT_EQ(headers->trackRow(1).top(), strip()->slotRect(1).top());
    // A frame of the lower track makes it current; Ctrl+A then selects its frames only.
    click(strip(), strip()->cellRect(0, 1).center());
    EXPECT_EQ(doc->currentTrack(), 0);
    EXPECT_EQ(doc->currentFrame(), 1);
    auto selectAll = QKeyEvent(QEvent::KeyPress, Qt::Key_A, Qt::ControlModifier);
    QCoreApplication::sendEvent(strip(), &selectAll);
    EXPECT_EQ(panel()->selectedFrames(), (std::vector<int>{0, 1, 2}));
    // New Frame goes on the current track.
    use(panel(), "timelineNewFrame");
    EXPECT_EQ(doc->animation().tracks[0].frameCount(), 4);
    EXPECT_EQ(doc->animation().tracks[1].frameCount(), 1);
    // Clicking the upper track's row chooses it, without moving the playhead.
    const int beat = doc->beat();
    click(headers, headers->trackRow(1).center());
    EXPECT_EQ(doc->currentTrack(), 1);
    EXPECT_EQ(doc->beat(), beat);
}

// Tracks, sound and video can be hidden; what is hidden doesn't play or show.
TEST_F(TimelineUiTest, TheHeadersHideAndLockTracksOfEveryKind) {
    ControllerRig rig;
    ASSERT_TRUE(rig.doc.fillLayer(rig.doc.currentLayerId(), QColor(200, 30, 30)));
    ASSERT_TRUE(rig.doc.addTrack());
    ASSERT_TRUE(rig.doc.fillLayer(rig.doc.currentLayerId(), QColor(30, 30, 200)));
    ASSERT_TRUE(rig.controller.addSoundFile(QStringLiteral("/sounds/a.wav"), QByteArray("RIFF"), 1.0));
    VideoReference v;
    v.path = QStringLiteral("/nowhere/v.mp4");
    v.sourceSize = QSize(64, 48);
    v.seconds = 1.0;
    v.fps = 12.0;
    ASSERT_TRUE(rig.doc.setVideo(v, false));
    const Animation& a = rig.doc.animation();
    // Imports go to the bottom: the video's track under the sound's, then the frames tracks.
    ASSERT_EQ(a.trackCount(), 4);
    EXPECT_EQ(a.tracks[0].kind, TrackKind::Video);
    EXPECT_EQ(a.tracks[1].kind, TrackKind::Sound);
    EXPECT_EQ(a.tracks[2].kind, TrackKind::Frames);
    EXPECT_EQ(a.tracks[3].kind, TrackKind::Frames);
    rig.panel.resize(1000, 520);
    rig.panel.show();
    settle();
    TrackHeaders* h = rig.panel.headers();
    // Square rows that touch, the eye with the lock beside it (the redesign's one row of controls per track).
    for (int t = 1; t < 4; ++t)
        EXPECT_EQ(h->trackRow(t).bottom() + 1, h->trackRow(t - 1).top()) << t;
    EXPECT_LT(h->lockRect(h->trackRow(2)).right(), h->eyeRect(h->trackRow(2)).left());
    EXPECT_EQ(h->lockRect(h->trackRow(2)).top(), h->eyeRect(h->trackRow(2)).top());
    EXPECT_GT(qBlue(rig.doc.projection().pixel(10, 10)), 150);
    click(h, h->eyeRect(h->trackRow(3)).center());
    EXPECT_TRUE(rig.doc.animation().tracks[3].hidden);
    EXPECT_GT(qRed(rig.doc.projection().pixel(10, 10)), 150);
    click(h, h->eyeRect(h->trackRow(2)).center());  // the only frames track shown stays shown
    EXPECT_FALSE(rig.doc.animation().tracks[2].hidden);
    click(h, h->eyeRect(h->trackRow(3)).center());
    click(h, h->eyeRect(h->trackRow(1)).center());
    EXPECT_TRUE(rig.doc.animation().tracks[1].hidden);
    EXPECT_FALSE(rig.doc.animation().heard(rig.doc.animation().sounds.front()));
    click(h, h->eyeRect(h->trackRow(0)).center());
    EXPECT_FALSE(rig.doc.animation().videoShown());
    // Locked: its frames can't change and its layers can't be painted.
    click(h, h->trackRow(3).center());
    click(h, h->lockRect(h->trackRow(3)).center());
    EXPECT_TRUE(rig.doc.animation().tracks[3].locked);
    EXPECT_EQ(rig.doc.currentTrack(), 3);
    EXPECT_FALSE(rig.doc.newFrame());
    EXPECT_FALSE(rig.doc.paintTarget(rig.doc.currentLayerId()));
    click(h, h->lockRect(h->trackRow(3)).center());
    EXPECT_TRUE(rig.doc.newFrame());
}

// Tracks can be dragged up and down.
TEST_F(TimelineUiTest, DraggingATrackRowMovesTheTrack) {
    ControllerRig rig;
    ASSERT_TRUE(rig.doc.addTrack());
    ASSERT_TRUE(rig.doc.renameTrack(1, QStringLiteral("Top")));
    rig.panel.resize(1000, 420);
    rig.panel.show();
    settle();
    TrackHeaders* h = rig.panel.headers();
    const QPoint from = h->trackRow(1).center();
    const QPoint to = h->trackRow(0).center();
    press(h, from);
    move(h, from + QPoint(0, 10));
    move(h, to);
    release(h, to);
    EXPECT_EQ(rig.doc.animation().tracks[0].name, QStringLiteral("Top"));
    // A click without dragging chooses the track.
    click(h, h->trackRow(1).center());
    EXPECT_EQ(rig.doc.currentTrack(), 1);
}

// Dragging a clip upwards made its track vanish while it still played. It stays on a sound track.
TEST_F(TimelineUiTest, AClipDraggedOverFramesStaysOnItsSoundTrack) {
    ControllerRig rig;
    ASSERT_TRUE(rig.controller.addSoundFile(QStringLiteral("/sounds/a.wav"), QByteArray("RIFF"), 1.0));
    ASSERT_TRUE(rig.controller.addSoundFile(QStringLiteral("/sounds/b.wav"), QByteArray("RIFF"), 1.0));
    rig.panel.resize(1000, 420);
    rig.panel.show();
    settle();
    FrameStrip* s = rig.panel.strip();
    const Animation& a = rig.doc.animation();
    const int b = a.sounds[1].id;
    const int track = a.sounds[1].track;
    QRect r = s->soundRect(b);
    // Upwards past the top: onto no sound track, so it stays where it is.
    press(s, r.center());
    move(s, r.center() + QPoint(s->beatWidth() * 3, -4));
    move(s, QPoint(r.center().x() + s->beatWidth() * 3, 1));
    release(s, QPoint(r.center().x() + s->beatWidth() * 3, 1));
    EXPECT_EQ(rig.doc.animation().sounds[1].track, track);
    EXPECT_EQ(rig.doc.animation().sounds[1].startBeat, 3);
    // Down onto the other clip's sound track, past the end of the clip there: it goes onto it.
    r = s->soundRect(b);
    const QPoint below(r.center().x() + s->beatWidth() * 12, s->soundRect(a.sounds[0].id).center().y());
    press(s, r.center());
    move(s, r.center() + QPoint(s->beatWidth() * 12, 8));
    move(s, below);
    release(s, below);
    EXPECT_EQ(rig.doc.animation().sounds[1].track, rig.doc.animation().sounds[0].track);
    EXPECT_EQ(rig.doc.animation().sounds[1].startBeat, 15);
    // Over the frames track it keeps to its own.
    r = s->soundRect(b);
    const QPoint frames(r.center().x() + s->beatWidth() * 2, s->cellRect(0).center().y());
    press(s, r.center());
    move(s, r.center() + QPoint(0, 8));
    move(s, frames);
    release(s, frames);
    EXPECT_EQ(rig.doc.animation().sounds[1].track, rig.doc.animation().sounds[0].track);
}

TEST_F(TimelineUiTest, EndAtSetsWhereAClipStops) {
    ControllerRig rig;
    ASSERT_TRUE(rig.controller.addSoundFile(QStringLiteral("/sounds/a.wav"), QByteArray("RIFF"), 6.0));
    const int id = rig.doc.animation().sounds.front().id;
    rig.controller.setSoundStart(id, 1.0);
    rig.controller.setSoundEnd(id, 4.0);
    const SoundClip clip = rig.doc.animation().sounds.front();
    EXPECT_DOUBLE_EQ(clip.endSeconds(12.0), 4.0);
    EXPECT_NEAR(clip.trimEnd, 3.0, 1e-9);
    rig.controller.setSoundEnd(id, 1.01);  // less than a beat after it starts
    EXPECT_DOUBLE_EQ(rig.doc.animation().sounds.front().endSeconds(12.0), 4.0);
    rig.controller.setSoundEnd(id, 30.0);  // past the file's end: as far as it goes
    EXPECT_DOUBLE_EQ(rig.doc.animation().sounds.front().trimEnd, 0.0);
}

// Del clears, as Clear does; with every frame of the track selected (Ctrl+A) it is Clear Track.
TEST_F(TimelineUiTest, DelClearsTheSelectedFrames) {
    showTimeline();
    ASSERT_TRUE(doc->fillLayer(doc->currentLayerId(), Qt::red));
    command("canvas.newFrame")->trigger();
    ASSERT_TRUE(doc->fillLayer(doc->currentLayerId(), Qt::green));
    command("canvas.newFrame")->trigger();
    ASSERT_TRUE(doc->fillLayer(doc->currentLayerId(), Qt::blue));
    settle();
    click(strip(), strip()->cellRect(1).center());
    auto del = QKeyEvent(QEvent::KeyPress, Qt::Key_Delete, Qt::NoModifier);
    QCoreApplication::sendEvent(strip(), &del);
    EXPECT_EQ(doc->frameCount(), 3);
    EXPECT_FALSE(animation_ops::frameHasDrawing(doc->state(), 0, 1));
    EXPECT_TRUE(animation_ops::frameHasDrawing(doc->state(), 0, 2));
    auto selectAll = QKeyEvent(QEvent::KeyPress, Qt::Key_A, Qt::ControlModifier);
    QCoreApplication::sendEvent(strip(), &selectAll);
    QCoreApplication::sendEvent(strip(), &del);
    EXPECT_EQ(doc->frameCount(), 1);
    EXPECT_FALSE(animation_ops::frameHasDrawing(doc->state(), 0, 0));
    ASSERT_TRUE(doc->undo());
    EXPECT_EQ(doc->frameCount(), 3);
}

TEST_F(TimelineUiTest, PlaybackShowsEveryTrackAtEachBeat) {
    showTimeline();
    ASSERT_TRUE(doc->fillLayer(doc->currentLayerId(), Qt::red));
    ASSERT_TRUE(doc->newFrame());
    ASSERT_TRUE(doc->fillLayer(doc->currentLayerId(), Qt::green));
    ASSERT_TRUE(doc->addTrack());  // one frame, its layer empty: the first track shows through
    ASSERT_TRUE(doc->setPlayback(30, false));
    ASSERT_TRUE(doc->goToBeat(0));
    std::set<QRgb> seen;
    window->animation()->togglePlay();
    EXPECT_TRUE(waitFor(
        [&] {
            seen.insert(doc->projection().pixel(5, 5));
            return !window->animation()->isPlaying();
        },
        3000));
    EXPECT_TRUE(seen.count(QColor(Qt::red).rgba()));
    EXPECT_TRUE(seen.count(QColor(Qt::green).rgba()));
    EXPECT_EQ(doc->currentTrack(), 1);  // playing doesn't change the current track
}

// Thousands of frames were still slow in the window (canvas, Layers panel, timeline together).
// Going to a frame, New Frame, a hold and a click cost about the same as with a few frames (before: 28 to 110 ms each here).
TEST_F(TimelineUiTest, AThousandsOfFramesCanvasStaysQuickInTheWindow) {
    VideoReference v;
    v.path = QStringLiteral("/nowhere/long.mp4");
    v.sourceSize = QSize(640, 360);
    v.seconds = 250.0;
    v.fps = 12.0;
    ASSERT_TRUE(doc->setVideo(v, true));
    ASSERT_GE(doc->frameCount(), 3000);
    showTimeline();
    settle();
    QElapsedTimer clock;
    clock.start();
    for (int i = 0; i < 10; ++i) {
        command("animation.nextFrame")->trigger();
        settle();
        (void)doc->goToFrame(250 * i + 40);
        settle();
        (void)doc->changeHold({doc->currentFrame()}, 1);
        settle();
        click(strip(), strip()->cellRect(doc->currentFrame() + 1).center());
        settle();
    }
    command("canvas.newFrame")->trigger();
    settle();
    EXPECT_LT(clock.elapsed(), 1500);  // 41 steps
}

// Import the same sound twice, drag the lower one up: it vanished (it slid past the other clip,
// out of sight). Where it doesn't fit it stays on its own track, under the pointer.
TEST_F(TimelineUiTest, TheSameSoundTwiceDraggedUpStaysInSight) {
    ControllerRig rig;
    ASSERT_TRUE(rig.controller.addSoundFile(QStringLiteral("/sounds/a.wav"), QByteArray("RIFF"), 2.0));
    ASSERT_TRUE(rig.controller.addSoundFile(QStringLiteral("/sounds/a.wav"), QByteArray("RIFF"), 2.0));
    rig.panel.resize(1000, 420);
    rig.panel.show();
    settle();
    FrameStrip* s = rig.panel.strip();
    const Animation& a = rig.doc.animation();
    const int lower = s->soundRect(a.sounds[0].id).top() > s->soundRect(a.sounds[1].id).top() ? a.sounds[0].id : a.sounds[1].id;
    const auto clip = [&] { return *std::ranges::find_if(a.sounds, [&](const SoundClip& c) { return c.id == lower; }); };
    const int track = clip().track;
    const QRect r = s->soundRect(lower);
    press(s, r.center());
    for (int i = 1; i <= 10; ++i)
        move(s, r.center() - QPoint(0, i * 6));
    release(s, r.center() - QPoint(0, 60));
    EXPECT_EQ(clip().track, track);
    EXPECT_EQ(clip().startBeat, 0);
    EXPECT_EQ(s->soundRect(lower), r);
}

// A video's sound kept playing when its track was hidden during playback.
TEST_F(TimelineUiTest, HidingASoundTrackWhilePlayingSilencesItAtOnce) {
    ControllerRig rig;
    ASSERT_TRUE(rig.doc.newFrame());
    ASSERT_TRUE(rig.doc.setPlayback(2, true));  // slow: it plays through the test
    ASSERT_TRUE(rig.controller.addSoundFile(QStringLiteral("/videos/clip.wav"), QByteArray("RIFF"), 60.0));
    const SoundClip clip = rig.doc.animation().sounds.front();
    const int track = rig.doc.animation().indexOfTrack(clip.track);
    ASSERT_TRUE(rig.doc.goToFrame(0));
    rig.controller.togglePlay();
    ASSERT_TRUE(rig.controller.isPlaying());
    EXPECT_EQ(rig.controller.playingSounds(), std::vector<int>{clip.id});
    ASSERT_TRUE(rig.doc.setTrackHidden(track, true));
    EXPECT_TRUE(rig.controller.playingSounds().empty());
    ASSERT_TRUE(rig.doc.setTrackHidden(track, false));
    EXPECT_EQ(rig.controller.playingSounds(), std::vector<int>{clip.id});
    ASSERT_TRUE(rig.doc.setSoundMuted(clip.id, true));
    EXPECT_TRUE(rig.controller.playingSounds().empty());
    rig.controller.stop();
}

// After a click on the canvas, frames chosen in the timeline and Delete cleared the canvas's
// layer. Delete clears the frames chosen in the timeline until the next click elsewhere.
TEST_F(TimelineUiTest, DeleteClearsTheFramesChosenInTheTimeline) {
    showTimeline();
    ASSERT_TRUE(doc->fillLayer(doc->currentLayerId(), Qt::red));
    command("canvas.newFrame")->trigger();
    ASSERT_TRUE(doc->fillLayer(doc->currentLayerId(), Qt::green));
    command("canvas.newFrame")->trigger();
    ASSERT_TRUE(doc->fillLayer(doc->currentLayerId(), Qt::blue));
    settle();
    QWidget* canvas = window->tabs()->currentCanvas();
    ASSERT_TRUE(canvas);
    click(canvas, canvas->rect().center());
    click(strip(), strip()->cellRect(0).center());
    click(strip(), strip()->cellRect(1).center(), Qt::ShiftModifier);
    canvas->setFocus();  // the canvas takes the keyboard when the pointer passes over it
    command("layer.erase")->trigger();
    EXPECT_FALSE(animation_ops::frameHasDrawing(doc->state(), 0, 0));
    EXPECT_FALSE(animation_ops::frameHasDrawing(doc->state(), 0, 1));
    EXPECT_TRUE(animation_ops::frameHasDrawing(doc->state(), 0, 2));
    // A click on the canvas gives Delete back to the layer.
    ASSERT_TRUE(doc->goToFrame(2));
    click(canvas, canvas->rect().center());
    EXPECT_FALSE(strip()->selectionActive());
}

// Nothing showed how much time passes. A ruler over the lanes, in beats, labelled ("0f, 6f, 12f"; TheRulerLabelsBeatsAsTheMock),
// that moves the playhead.
TEST_F(TimelineUiTest, ATimeRulerShowsTheBeatsAndMovesThePlayhead) {
    showTimeline();
    frames(40);
    ASSERT_TRUE(doc->goToFrame(0));
    settle();
    TimeRuler* ruler = panel()->ruler();
    ASSERT_TRUE(ruler);
    EXPECT_LT(ruler->geometry().bottom(), strip()->parentWidget()->mapTo(panel(), QPoint(0, 0)).y() + 1);  // over the lanes
    EXPECT_EQ(ruler->xOfBeat(14), strip()->mapTo(panel(), QPoint(strip()->cellRect(14).left(), 0)).x() - ruler->x());
    click(ruler, QPoint(ruler->xOfBeat(14) + 3, TimeRuler::kHeight / 2));
    EXPECT_EQ(doc->beat(), 14);
    EXPECT_EQ(doc->currentFrame(), 14);
}

// Zoom out to see more tracks, as the frames zoom out.
TEST_F(TimelineUiTest, TheTracksCanBeMadeShorterToSeeMore) {
    showTimeline();
    use(panel(), "timelineAddTrackButton");
    use(panel(), "timelineAddTrackButton");
    auto* height = panel()->findChild<QSlider*>(QStringLiteral("timelineTrackHeight"));
    EXPECT_EQ(height->value(), FrameStrip::kDefaultLaneScale);  // the starting height
    height->setValue(100);
    settle();
    const int tall = strip()->slotRect(0).height();
    ASSERT_TRUE(height);
    height->setValue(FrameStrip::kMinLaneScale);
    settle();
    EXPECT_LT(strip()->slotRect(0).height(), tall / 2);
    EXPECT_EQ(strip()->labelHeight(), 0);  // the beat numbers go when the lanes are small
    TrackHeaders* h = panel()->headers();
    EXPECT_EQ(h->trackRow(0).height(), strip()->slotRect(0).height());
    EXPECT_EQ(h->trackRow(1).bottom() + 1, h->trackRow(0).top());
    EXPECT_LT(h->lockRect(h->trackRow(0)).right(), h->eyeRect(h->trackRow(0)).left());  // beside the eye
    click(strip(), strip()->cellRect(0, 0).center());
    EXPECT_EQ(doc->currentTrack(), 0);
}

// A frame still loading looked blank; it shows a loading sign until its picture is ready.
TEST_F(TimelineUiTest, AFrameStillLoadingShowsASign) {
    ControllerRig rig;
    ASSERT_TRUE(rig.doc.fillLayer(rig.doc.currentLayerId(), Qt::white));
    rig.panel.resize(900, 300);
    FrameStrip* s = rig.panel.strip();
    s->resize(800, 200);
    const QRect cell = s->cellRect(0);
    const QPoint onRing(cell.center().x() - 9, cell.center().y());
    const QImage loading = s->grab().toImage();  // nothing read yet: the sign's ring, not the frame's white
    EXPECT_LT(loading.pixelColor(onRing).lightness(), 200);
    rig.panel.show();
    EXPECT_TRUE(waitFor(
        [&] {
            settle();
            return s->grab().toImage().pixelColor(onRing).lightness() == 255;  // the white frame's thumbnail
        },
        2000));
}

namespace {

SoundClip namedClip(const QString& name, double seconds) {
    SoundClip c;
    c.name = name;
    c.data = std::make_shared<const QByteArray>("RIFF");
    c.seconds = seconds;
    return c;
}

const SoundClip& clipOf(const Document& doc, int id) {
    const std::vector<SoundClip>& sounds = doc.animation().sounds;
    return *std::ranges::find_if(sounds, [id](const SoundClip& c) { return c.id == id; });
}

}

// With the timeline showing, a click in it made the main window tile off the left of the screen
// (COSMIC on Wayland, 2560 x 1440). Its rows of buttons and its hint asked for 1749 px of window, more than a tiled half of
// the screen, so the window had to grow past its tile. Shown and clicked anywhere, the timeline leaves the window's place,
// size, least size and state as they were.
TEST_F(TimelineUiTest, ClickingTheTimelineLeavesTheWindowAsItIs) {
    window->resize(1270, 1380);  // a tiled half of the screen, narrower than the timeline's rows laid side by side
    settle();
    const QRect geometry = window->geometry();
    const QSize least = window->minimumSize();
    const Qt::WindowStates state = window->windowState();
    const auto unchanged = [&](const char* after) {
        settle();
        EXPECT_EQ(window->geometry(), geometry) << after;
        EXPECT_EQ(window->minimumSize(), least) << after;
        EXPECT_EQ(window->windowState(), state) << after;
    };
    showTimeline();
    unchanged("showing the timeline");
    frames(4);
    const int clip = doc->addSound(namedClip(QStringLiteral("a sound with a rather long name, as songs have.wav"), 1.0)).value();
    unchanged("a sound");
    FrameStrip* s = strip();
    click(s, s->cellRect(1).center());
    unchanged("a click on a frame");
    click(s, s->cellRect(3).center(), Qt::ShiftModifier);
    click(s, s->cellRect(0).center(), Qt::ControlModifier);
    unchanged("Shift and Ctrl+click");
    press(s, QPoint(4, s->cellRect(0).center().y()));
    move(s, s->cellRect(2).center());
    release(s, s->cellRect(2).center());
    unchanged("a selection box");
    click(s, s->soundRect(clip).center());
    unchanged("a click on a clip");
    click(panel()->headers(), panel()->headers()->trackRow(doc->currentTrack()).center());
    click(panel()->ruler(), QPoint(panel()->ruler()->xOfBeat(2), TimeRuler::kHeight / 2));
    unchanged("the headers and the ruler");
    for (const char* name : {"timelinePrevious", "timelineNext", "timelineNewFrame", "timelineDuplicate", "timelineLoop",
                             "timelineOnionSkin", "timelineFit", "timelineAddTrackButton", "timelinePlay", "timelinePlay"})
        use(panel(), name);
    unchanged("the buttons");
}

// Del after frames chosen in the timeline sometimes cleared them and sometimes the canvas.
// - A pen's press on the canvas comes without a mouse press, so after painting the timeline still had Delete.
// - A click on a panel that doesn't take the keyboard left it with the timeline, and the timeline cleared its frames though it
//   wasn't used last.
// Every press counts now, the mouse's, a pen's or a finger's, and both ways Delete arrives ask the same: what was used last.
TEST_F(TimelineUiTest, DeleteActsOnWhatWasUsedLast) {
    showTimeline();
    QWidget* canvas = window->tabs()->currentCanvas();
    ASSERT_TRUE(canvas);
    const auto paintFrames = [&] {
        for (int f = 0; f < 3; ++f) {
            ASSERT_TRUE(doc->goToFrame(f));
            ASSERT_TRUE(doc->fillLayer(doc->currentLayerId(), Qt::red));
        }
    };
    frames(3);
    paintFrames();
    const auto chooseFrames01 = [&] {  // the second frame is the current one
        click(strip(), strip()->cellRect(0).center());
        click(strip(), strip()->cellRect(1).center(), Qt::ShiftModifier);
        ASSERT_EQ(strip()->selectedFrames(), (std::vector<int>{0, 1}));
    };
    // Delete as the window delivers it: to the widget with the keyboard if it takes it, else to Canvas > Clear.
    const auto del = [&] {
        QWidget* focus = window->focusWidget() ? window->focusWidget() : window.get();
        auto override = key(QEvent::ShortcutOverride, Qt::Key_Delete);
        QCoreApplication::sendEvent(focus, &override);
        if (override.isAccepted()) {
            auto press = key(QEvent::KeyPress, Qt::Key_Delete);
            QCoreApplication::sendEvent(focus, &press);
        } else {
            command("layer.erase")->trigger();
        }
    };
    const auto drawn = [&](int frame) { return animation_ops::frameHasDrawing(doc->state(), 0, frame); };

    // The timeline used last, with the keyboard: its frames.
    chooseFrames01();
    del();
    EXPECT_FALSE(drawn(0));
    EXPECT_FALSE(drawn(1));
    EXPECT_TRUE(drawn(2));
    paintFrames();

    // A pen's press elsewhere, as on the canvas: the layer, though the timeline still has the keyboard.
    chooseFrames01();
    const QPointF at(10, 10);
    QTabletEvent pen(QEvent::TabletPress, QPointingDevice::primaryPointingDevice(), at, window->statusBar()->mapToGlobal(at), 0.5, 0, 0, 0,
                     0, 0, Qt::NoModifier, Qt::LeftButton, Qt::LeftButton);
    QCoreApplication::sendEvent(window->statusBar(), &pen);
    EXPECT_FALSE(strip()->selectionActive());
    EXPECT_EQ(window->focusWidget(), strip());
    del();
    EXPECT_TRUE(drawn(0));
    EXPECT_FALSE(drawn(1));
    paintFrames();

    // A press on the timeline's buttons, which don't take the keyboard, is using it: it has the keyboard again, and keeps
    // Delete when the canvas takes the keyboard as the pointer passes over it.
    chooseFrames01();
    click(window->statusBar(), QPoint(10, 10));
    EXPECT_FALSE(strip()->selectionActive());
    canvas->setFocus();
    use(panel(), "timelineZoomIn");
    EXPECT_TRUE(strip()->selectionActive());
    EXPECT_EQ(window->focusWidget(), strip());
    canvas->setFocus();
    del();
    EXPECT_FALSE(drawn(0));
    EXPECT_FALSE(drawn(1));
    paintFrames();

    // A dialog belongs to what opened it: a press there leaves Delete where it was.
    chooseFrames01();
    QDialog dialog(window.get());
    dialog.show();
    settle();
    click(&dialog, QPoint(5, 5));
    EXPECT_TRUE(strip()->selectionActive());
    dialog.close();

    // Hiding the timeline ends it.
    command("canvas.showTimeline")->trigger();
    settle();
    EXPECT_FALSE(strip()->selectionActive());
    showTimeline();
    EXPECT_FALSE(strip()->selectionActive());
}

// Feedback: "if the timeline is focused, pressing space should start playing it."
TEST_F(TimelineUiTest, SpacePlaysOrStopsWhileTheTimelineHasTheKeyboard) {
    showTimeline();
    frames(3);
    click(strip(), strip()->cellRect(0).center());
    ASSERT_EQ(window->focusWidget(), strip());
    auto override = key(QEvent::ShortcutOverride, Qt::Key_Space);
    QCoreApplication::sendEvent(strip(), &override);
    EXPECT_TRUE(override.isAccepted());  // the timeline's, not a menu's
    auto space = key(QEvent::KeyPress, Qt::Key_Space);
    QCoreApplication::sendEvent(strip(), &space);
    EXPECT_TRUE(window->animation()->isPlaying());
    QKeyEvent held(QEvent::KeyPress, Qt::Key_Space, Qt::NoModifier, QString(), true);
    QCoreApplication::sendEvent(strip(), &held);  // held down, it doesn't stop and start again
    EXPECT_TRUE(window->animation()->isPlaying());
    auto again = key(QEvent::KeyPress, Qt::Key_Space);
    QCoreApplication::sendEvent(strip(), &again);
    EXPECT_FALSE(window->animation()->isPlaying());
    // The canvas keeps Space for panning while it has the keyboard.
    window->tabs()->currentCanvas()->setFocus();
    auto onCanvas = key(QEvent::KeyPress, Qt::Key_Space);
    QCoreApplication::sendEvent(window->tabs()->currentCanvas(), &onCanvas);
    auto released = key(QEvent::KeyRelease, Qt::Key_Space);
    QCoreApplication::sendEvent(window->tabs()->currentCanvas(), &released);
    EXPECT_FALSE(window->animation()->isPlaying());
}

// Feedback: "i cant seem to select multiple sounds ... These should also be able to be moved in the tracks
// individually, as well as in group when selected."
TEST_F(TimelineUiTest, SeveralClipsAreChosenAndMovedTogether) {
    ControllerRig rig;
    ASSERT_TRUE(rig.controller.addSoundFile(QStringLiteral("/sounds/a.wav"), QByteArray("RIFF"), 1.0));  // 12 beats
    ASSERT_TRUE(rig.controller.addSoundFile(QStringLiteral("/sounds/b.wav"), QByteArray("RIFF"), 1.0));
    ASSERT_TRUE(rig.controller.addSoundFile(QStringLiteral("/sounds/c.wav"), QByteArray("RIFF"), 1.0));
    const Animation& an = rig.doc.animation();
    const int a = an.sounds[0].id, b = an.sounds[1].id, c = an.sounds[2].id;
    const int trackA = clipOf(rig.doc, a).track, trackB = clipOf(rig.doc, b).track;
    ASSERT_TRUE(rig.doc.moveSound(b, 20, trackB));
    ASSERT_TRUE(rig.doc.moveSound(c, 40, clipOf(rig.doc, c).track));
    rig.panel.resize(1000, 520);
    rig.panel.show();
    FrameStrip* s = rig.panel.strip();
    s->setBeatWidth(10);
    settle();
    const auto chosen = [&] { return s->selectedSounds(); };
    const auto centre = [&](int id) { return s->soundRect(id).center(); };

    // Ctrl+click adds or removes a clip, Shift+click adds one.
    click(s, centre(a));
    EXPECT_EQ(chosen(), std::vector<int>{a});
    click(s, centre(b), Qt::ControlModifier);
    EXPECT_EQ(chosen(), (std::vector<int>{a, b}));
    click(s, centre(b), Qt::ControlModifier);
    EXPECT_EQ(chosen(), std::vector<int>{a});
    click(s, centre(c), Qt::ShiftModifier);
    EXPECT_EQ(chosen(), (std::vector<int>{a, c}));

    // Dragging one of them moves both by the same beats, as one undo step.
    const int steps = rig.doc.undoStack()->count();
    press(s, centre(a));
    move(s, centre(a) + QPoint(3 * s->beatWidth(), 0));
    release(s, centre(a) + QPoint(3 * s->beatWidth(), 0));
    EXPECT_EQ(clipOf(rig.doc, a).startBeat, 3);
    EXPECT_EQ(clipOf(rig.doc, c).startBeat, 43);
    EXPECT_EQ(rig.doc.undoStack()->count(), steps + 1);
    EXPECT_EQ(chosen(), (std::vector<int>{a, c}));

    // Up a sound track together: b onto a's track, c onto b's.
    click(s, centre(b));
    click(s, centre(c), Qt::ControlModifier);
    const QPoint up(centre(b).x(), centre(a).y());
    press(s, centre(b));
    move(s, centre(b) + QPoint(0, -8));
    move(s, up);
    release(s, up);
    EXPECT_EQ(clipOf(rig.doc, b).track, trackA);
    EXPECT_EQ(clipOf(rig.doc, c).track, trackB);
    EXPECT_EQ(clipOf(rig.doc, b).startBeat, 20);

    // Where one would overlap another clip, they stay where they last fitted.
    press(s, centre(b));
    move(s, centre(b) - QPoint(10 * s->beatWidth(), 0));  // b over a
    release(s, centre(b) - QPoint(10 * s->beatWidth(), 0));
    EXPECT_EQ(clipOf(rig.doc, b).startBeat, 20);
    EXPECT_EQ(clipOf(rig.doc, c).startBeat, 43);

    // A click without a drag leaves that one alone selected, and alone it moves alone.
    click(s, centre(b));
    EXPECT_EQ(chosen(), std::vector<int>{b});
    press(s, centre(b));
    move(s, centre(b) + QPoint(2 * s->beatWidth(), 0));
    release(s, centre(b) + QPoint(2 * s->beatWidth(), 0));
    EXPECT_EQ(clipOf(rig.doc, b).startBeat, 22);
    EXPECT_EQ(clipOf(rig.doc, c).startBeat, 43);

    // A box drawn from a sound track's empty space picks every clip it touches: b (22 to 34) and c (43 to 55), not a (3 to 15).
    const QPoint from(s->xOfBeat(30), centre(c).y());
    ASSERT_EQ(s->soundAt(from), 0);
    press(s, from);
    move(s, from + QPoint(20, 0));
    move(s, QPoint(s->xOfBeat(50), centre(b).y()));
    release(s, QPoint(s->xOfBeat(50), centre(b).y()));
    EXPECT_EQ(chosen(), (std::vector<int>{b, c}));

    // Delete removes them, one undo step.
    auto overrideDelete = key(QEvent::ShortcutOverride, Qt::Key_Delete);
    QCoreApplication::sendEvent(s, &overrideDelete);
    EXPECT_TRUE(overrideDelete.isAccepted());
    auto del = key(QEvent::KeyPress, Qt::Key_Delete);
    QCoreApplication::sendEvent(s, &del);
    ASSERT_EQ(rig.doc.animation().sounds.size(), 1u);
    EXPECT_EQ(rig.doc.animation().sounds.front().id, a);
    ASSERT_TRUE(rig.doc.undo());
    EXPECT_EQ(rig.doc.animation().sounds.size(), 3u);
}

namespace {

void wheel(QWidget* w, const QPoint& pos, int angle, Qt::KeyboardModifiers mods = {}) {
    QWheelEvent e(QPointF(pos), w->mapToGlobal(QPointF(pos)), QPoint(), QPoint(0, angle), Qt::NoButton, mods, Qt::NoScrollPhase, false);
    QCoreApplication::sendEvent(w, &e);
}

std::vector<int> frameIds(const Document& doc, int track) {
    std::vector<int> out;
    for (const AnimationFrame& f : doc.animation().tracks[static_cast<std::size_t>(track)].frames)
        out.push_back(f.id);
    return out;
}

}

// Feedback: "It works in a single track, but I should be able to move them from all tracks."
// A box picks the frames and clips it touches on every track, and dragging one of them moves them all, as one undo step.
TEST_F(TimelineUiTest, FramesAndClipsChosenOnSeveralTracksMoveTogether) {
    ControllerRig rig;
    ASSERT_TRUE(rig.doc.newFrame());
    ASSERT_TRUE(rig.doc.addTrack());
    ASSERT_TRUE(rig.doc.newFrame());
    ASSERT_TRUE(rig.controller.addSoundFile(QStringLiteral("/sounds/a.wav"), QByteArray("RIFF"), 1.0));  // 12 beats
    const int clip = rig.doc.animation().sounds.front().id;
    ASSERT_TRUE(rig.doc.moveSound(clip, 0, clipOf(rig.doc, clip).track));
    const int lower = 1, upper = 2;  // over the sound track
    ASSERT_EQ(rig.doc.animation().tracks[upper].kind, TrackKind::Frames);
    rig.panel.resize(1000, 520);
    rig.panel.show();
    FrameStrip* s = rig.panel.strip();
    s->setBeatWidth(30);
    settle();
    const std::vector<int> lowerBefore = frameIds(rig.doc, lower), upperBefore = frameIds(rig.doc, upper);

    // From the margin left of the upper track's first frame down to the clip, over the first frame of each track.
    const QPoint from(3, s->cellRect(upper, 0).center().y());
    const QPoint to(s->cellRect(upper, 0).left() + 10, s->soundRect(clip).center().y());
    press(s, from);
    move(s, from + QPoint(4, 4));
    move(s, to);
    release(s, to);
    std::vector<FrameStrip::Cell> cells = s->selectedCells();
    ASSERT_EQ(cells.size(), 2u);
    EXPECT_EQ(cells[0].track, lower);
    EXPECT_EQ(cells[1].track, upper);
    EXPECT_EQ(s->selectedSounds(), std::vector<int>{clip});

    const int steps = rig.doc.undoStack()->count();
    const QPoint grab = s->cellRect(lower, 0).center();
    const QPoint drop = grab + QPoint(s->beatWidth() + 6, 0);  // past the middle of the next frame
    press(s, grab);
    move(s, grab + QPoint(8, 0));
    move(s, drop);
    release(s, drop);
    EXPECT_EQ(frameIds(rig.doc, lower), (std::vector<int>{lowerBefore[1], lowerBefore[0]}));
    EXPECT_EQ(frameIds(rig.doc, upper), (std::vector<int>{upperBefore[1], upperBefore[0]}));
    EXPECT_EQ(clipOf(rig.doc, clip).startBeat, 1);
    EXPECT_EQ(rig.doc.undoStack()->count(), steps + 1);
    ASSERT_TRUE(rig.doc.undo());
    EXPECT_EQ(frameIds(rig.doc, lower), lowerBefore);
    EXPECT_EQ(frameIds(rig.doc, upper), upperBefore);
    EXPECT_EQ(clipOf(rig.doc, clip).startBeat, 0);

    // Ctrl+click picks frames of another track too; a plain click leaves one alone selected.
    click(s, s->cellRect(lower, 1).center());
    click(s, s->cellRect(upper, 0).center(), Qt::ControlModifier);
    EXPECT_EQ(s->selectedCells().size(), 2u);
    EXPECT_TRUE(s->selectedSounds().empty());
    click(s, s->cellRect(upper, 0).center());
    EXPECT_EQ(s->selectedCells().size(), 1u);
}

// Feedback: "i cannot seem to move the video frame track or remove frames from it", and "I cant seem to
// move or edit individual video frames, they are unselectable. They should act like canvases."
TEST_F(TimelineUiTest, VideoFramesAreChosenMovedHeldAndDeletedLikeDrawnOnes) {
    ControllerRig rig;
    VideoReference v;
    v.path = QStringLiteral("/nowhere/walk.mp4");
    v.sourceSize = QSize(640, 360);
    v.seconds = 1.0;
    v.fps = 12;
    ASSERT_TRUE(rig.doc.setVideo(v, false));
    const int video = rig.doc.animation().videoTrack();
    rig.panel.resize(1000, 400);
    rig.panel.show();
    FrameStrip* s = rig.panel.strip();
    s->setBeatWidth(30);
    settle();
    const auto pictures = [&](std::size_t count = 0) {
        std::vector<int> out;
        for (const AnimationFrame& f : rig.doc.animation().tracks[static_cast<std::size_t>(video)].frames)
            if (count == 0 || out.size() < count)
                out.push_back(f.picture);
        return out;
    };

    click(s, s->cellRect(video, 2).center());
    EXPECT_EQ(rig.doc.currentTrack(), video);
    EXPECT_EQ(rig.doc.currentFrame(), 2);
    click(s, s->cellRect(video, 3).center(), Qt::ControlModifier);
    EXPECT_EQ(s->selectedFrames(), (std::vector<int>{2, 3}));
    // Dragged past the middle of the sixth picture, the two go after it.
    const QPoint grab = s->cellRect(video, 2).center();
    const QPoint drop = s->cellRect(video, 5).center() + QPoint(2, 0);
    press(s, grab);
    move(s, grab + QPoint(8, 0));
    move(s, drop);
    release(s, drop);
    EXPECT_EQ(pictures(7), (std::vector<int>{0, 1, 4, 5, 2, 3, 6}));

    // Held by its right edge.
    const QRect first = s->cellRect(video, 0);
    const QPoint edge(first.right(), first.center().y());
    press(s, edge);
    move(s, edge + QPoint(s->beatWidth(), 0));
    move(s, edge + QPoint(2 * s->beatWidth(), 0));
    release(s, edge + QPoint(2 * s->beatWidth(), 0));
    EXPECT_EQ(rig.doc.animation().tracks[static_cast<std::size_t>(video)].frames[0].hold, 3);

    // Delete takes the chosen frame out; Del empties one, its picture gone.
    click(s, s->cellRect(video, 1).center());
    use(&rig.panel, "timelineDelete");
    EXPECT_EQ(pictures(3), (std::vector<int>{0, 4, 5}));
    click(s, s->cellRect(video, 1).center());
    QKeyEvent del(QEvent::KeyPress, Qt::Key_Delete, Qt::NoModifier);
    QCoreApplication::sendEvent(s, &del);
    EXPECT_EQ(pictures(3), (std::vector<int>{0, -1, 5}));
    EXPECT_EQ(static_cast<int>(pictures().size()), 11);
}

// Feedback: "Scrolling when the mouse is on a track's black square doesnt scroll the timeline up or down",
// "Shift+mousescroll should scroll the timeline horizontally".
TEST_F(TimelineUiTest, TheWheelScrollsTheTracksFromTheHeadersAndShiftScrollsSideways) {
    ControllerRig rig;
    for (int i = 0; i < 40; ++i)
        ASSERT_TRUE(rig.doc.newFrame());
    for (int i = 0; i < 6; ++i)
        ASSERT_TRUE(rig.doc.addTrack());
    rig.panel.resize(900, 300);
    rig.panel.show();
    settle();
    auto* scroll = rig.panel.findChild<QScrollArea*>(QStringLiteral("timelineScroll"));
    ASSERT_TRUE(scroll);
    QScrollBar* down = scroll->verticalScrollBar();
    QScrollBar* along = scroll->horizontalScrollBar();
    ASSERT_GT(down->maximum(), 0);
    ASSERT_GT(along->maximum(), 0);
    TrackHeaders* headers = rig.panel.headers();
    wheel(headers, QPoint(60, 40), -120);
    const int wasDown = down->value();
    EXPECT_GT(wasDown, 0);
    wheel(headers, QPoint(60, 40), 120);
    EXPECT_LT(down->value(), wasDown);
    wheel(headers, QPoint(60, 40), -120);
    ASSERT_EQ(down->value(), wasDown);
    wheel(rig.panel.strip(), QPoint(100, 40), -120, Qt::ShiftModifier);
    EXPECT_GT(along->value(), 0);
    EXPECT_EQ(down->value(), wasDown);  // not a page down, as Qt's own Shift+wheel is
    wheel(headers, QPoint(20, 20), -120, Qt::ShiftModifier);
    EXPECT_GT(along->value(), 2 * QApplication::wheelScrollLines());
}

// Feedback: "on first play, the player freezes and audio crackles. Subsequent plays are fine."
// The first player started Qt Multimedia at Play (half a second to a second); now the clips are ready before.
TEST_F(TimelineUiTest, TheSoundIsReadyBeforeTheFirstPlay) {
    if (!SoundPlayback::isAvailable())
        GTEST_SKIP() << "built without Qt Multimedia";
    ControllerRig rig;
    ASSERT_TRUE(rig.doc.newFrame());
    ASSERT_TRUE(rig.controller.addSoundFile(QStringLiteral("/sounds/a.wav"), QByteArray("RIFF"), 1.0));
    settle();
    const SoundPlayback& sound = rig.controller.soundPlayback();
    EXPECT_EQ(sound.playersMade(), 1);
    EXPECT_EQ(qgetenv("QT_FFMPEG_DECODING_HW_DEVICE_TYPES"), QByteArray(","));
    rig.controller.togglePlay();
    ASSERT_TRUE(rig.controller.isPlaying());
    EXPECT_EQ(rig.controller.playingSounds().size(), 1u);
    EXPECT_EQ(sound.playersMade(), 1);  // Play opened nothing
    rig.controller.stop();
    rig.controller.togglePlay();
    rig.controller.stop();
    EXPECT_EQ(sound.playersMade(), 1);
    ASSERT_TRUE(rig.controller.addSoundFile(QStringLiteral("/sounds/b.wav"), QByteArray("RIFF2"), 1.0));
    settle();
    EXPECT_EQ(sound.playersMade(), 2);
}

// Feedback: "When using the timeline and switching frames, doodlebold should remember what layer i was using when i switch
// back. It always resets to the first layer."
TEST_F(TimelineUiTest, EachFrameRemembersItsLayerThroughTheTimeline) {
    showTimeline();
    ASSERT_TRUE(doc->addLayer());
    ASSERT_TRUE(doc->addLayer());
    frames(3);
    auto* tree = window->findChild<QTreeView*>(QStringLiteral("layerTree"));
    ASSERT_TRUE(tree);
    auto* model = static_cast<LayerTreeModel*>(tree->model());
    const auto pickRow = [&](int row) {
        const QModelIndex index = model->index(row, 0);
        click(tree->viewport(), tree->visualRect(index).center());
        settle();
        return model->layerAt(index);
    };
    // Frame 0: the middle layer; frame 1: the bottom one.
    click(strip(), strip()->cellRect(0).center());
    settle();
    const LayerId middle0 = pickRow(1);
    ASSERT_EQ(doc->currentLayerId(), middle0);
    click(strip(), strip()->cellRect(1).center());
    settle();
    const LayerId bottom1 = pickRow(model->rowCount() - 1);
    ASSERT_EQ(doc->currentLayerId(), bottom1);
    click(strip(), strip()->cellRect(2).center());
    settle();
    const auto selectedRows = [&] {
        std::vector<LayerId> out;
        for (const QModelIndex& row : tree->selectionModel()->selectedRows())
            out.push_back(model->layerAt(row));
        return out;
    };
    click(strip(), strip()->cellRect(0).center());
    settle();
    EXPECT_EQ(doc->currentLayerId(), middle0);
    EXPECT_EQ(selectedRows(), std::vector<LayerId>{middle0});
    EXPECT_EQ(model->layerAt(tree->currentIndex()), middle0);
    use(panel(), "timelineNext");
    settle();
    EXPECT_EQ(doc->currentFrame(), 1);
    EXPECT_EQ(doc->currentLayerId(), bottom1);
    EXPECT_EQ(selectedRows(), std::vector<LayerId>{bottom1});
    // With the layers list holding the keyboard, as after a click on it, and the frame keys.
    window->activateWindow();
    tree->setFocus();
    settle();
    command("animation.previousFrame")->trigger();
    settle();
    EXPECT_EQ(doc->currentFrame(), 0);
    EXPECT_EQ(doc->currentLayerId(), middle0);
    command("animation.nextFrame")->trigger();
    settle();
    EXPECT_EQ(doc->currentLayerId(), bottom1);
    // A layer picked on the canvas's own (not through the list) is remembered too.
    window->tabs()->currentCanvas()->setFocus();
    command("animation.previousFrame")->trigger();
    settle();
    EXPECT_EQ(doc->currentLayerId(), middle0);
}

// The same with two tracks: the lower track's frame never leaves the stack while the upper track's layer is used, and coming
// back to it brings back its layer, not its top one. And a new frame starts on the layer that was in use.
TEST_F(TimelineUiTest, EachTrackRemembersItsLayer) {
    showTimeline();
    ASSERT_TRUE(doc->addLayer());
    frames(2);
    auto* tree = window->findChild<QTreeView*>(QStringLiteral("layerTree"));
    auto* model = static_cast<LayerTreeModel*>(tree->model());
    const auto pickLayer = [&](LayerId id) {
        click(tree->viewport(), tree->visualRect(model->indexOf(id)).center());
        settle();
    };
    click(strip(), strip()->cellRect(0, 0).center());
    settle();
    const AnimationTrack& lower = doc->animation().tracks[0];
    ASSERT_EQ(lower.layers.size(), 2u);
    const LayerId lowerBottom = lower.layers.front();
    pickLayer(lowerBottom);
    ASSERT_EQ(doc->currentLayerId(), lowerBottom);
    use(panel(), "timelineAddTrackButton");
    settle();
    ASSERT_EQ(doc->currentTrack(), 1);
    pickLayer(doc->animation().tracks[1].layers.front());
    click(strip(), strip()->cellRect(0, 0).center());
    settle();
    EXPECT_EQ(doc->currentTrack(), 0);
    EXPECT_EQ(doc->currentLayerId(), lowerBottom);
    // + Frame from here: the new frame's copy of the bottom layer is the current one.
    use(panel(), "timelineNewFrame");
    settle();
    const AnimationTrack& after = doc->animation().tracks[0];
    EXPECT_EQ(doc->currentLayerId(), after.layers.front());
}

// The own steps: Layer 2 picked in the layers list, painted on with the pen, another frame painted, and back.
TEST_F(TimelineUiTest, ThePickedLayerStaysSelectedAfterPaintingAndSwitchingFrames) {
    showTimeline();
    ASSERT_TRUE(doc->addLayer());
    ASSERT_TRUE(doc->addLayer());  // Layer 1, 2 and 3, Layer 3 current
    use(panel(), "timelineNewFrame");
    use(panel(), "timelineNewFrame");
    settle();
    auto* tree = window->findChild<QTreeView*>(QStringLiteral("layerTree"));
    auto* model = static_cast<LayerTreeModel*>(tree->model());
    CanvasView* canvas = window->tabs()->currentCanvas();
    const auto stroke = [&] {
        const QPointF from(canvas->width() / 2.0 - 30, canvas->height() / 2.0 - 15);
        const auto pen = [&](QEvent::Type type, QPointF at) {
            QTabletEvent e(type, QPointingDevice::primaryPointingDevice(), at, canvas->mapToGlobal(at), 0.6, 0, 0, 0, 0, 0, Qt::NoModifier,
                           Qt::LeftButton, type == QEvent::TabletRelease ? Qt::NoButton : Qt::LeftButton);
            QCoreApplication::sendEvent(canvas, &e);
        };
        pen(QEvent::TabletPress, from);
        for (int i = 1; i <= 10; ++i)
            pen(QEvent::TabletMove, from + QPointF(i * 6, i * 3));
        pen(QEvent::TabletRelease, from + QPointF(60, 30));
        ASSERT_TRUE(waitFor([&] { return !canvas->isPainting(); }, 10000));
        settle();
    };
    const auto layerNamed = [&](const QString& name) {
        for (int row = 0; row < model->rowCount(); ++row)
            if (doc->layer(model->layerAt(model->index(row, 0)))->props.name == name)
                return model->layerAt(model->index(row, 0));
        return kNoLayer;
    };
    const auto selectedName = [&] {
        const LayerNode* n = doc->layer(model->layerAt(tree->currentIndex()));
        const auto rows = tree->selectionModel()->selectedRows();
        return n && rows.size() == 1 && rows.front() == tree->currentIndex() ? n->props.name : QStringLiteral("(none)");
    };
    click(strip(), strip()->cellRect(0).center());
    settle();
    const LayerId layer2 = layerNamed(QStringLiteral("Layer2"));
    ASSERT_NE(layer2, kNoLayer);
    click(tree->viewport(), tree->visualRect(model->indexOf(layer2)).center());
    settle();
    ASSERT_EQ(doc->currentLayerId(), layer2);
    stroke();
    EXPECT_EQ(selectedName(), QStringLiteral("Layer2"));
    click(strip(), strip()->cellRect(1).center());
    settle();
    stroke();
    click(strip(), strip()->cellRect(0).center());
    settle();
    EXPECT_EQ(doc->currentLayerId(), layer2);
    EXPECT_EQ(selectedName(), QStringLiteral("Layer2"));
    // The ruler and the title bar's frame buttons too.
    click(panel()->ruler(), QPoint(panel()->ruler()->xOfBeat(1) + 2, 10));
    settle();
    click(panel()->ruler(), QPoint(panel()->ruler()->xOfBeat(0) + 2, 10));
    settle();
    EXPECT_EQ(selectedName(), QStringLiteral("Layer2"));
    // A new frame made from here starts on its copy of Layer2, not on its top layer.
    use(panel(), "timelineNewFrame");
    settle();
    EXPECT_EQ(doc->currentFrame(), 1);
    const AnimationTrack& t = doc->animation().tracks[0];
    ASSERT_EQ(t.layers.size(), 3u);
    EXPECT_EQ(doc->currentLayerId(), t.layers[1]);  // the middle one, as Layer2 is
    EXPECT_EQ(model->layerAt(tree->currentIndex()), t.layers[1]);
}

// A layer shared by every frame stays the one in use when the frame changes.
TEST_F(TimelineUiTest, ASharedLayerStaysSelectedAcrossFrames) {
    showTimeline();
    const LayerId sky = doc->currentLayerId();
    ASSERT_TRUE(doc->addLayer());
    frames(3);
    ASSERT_TRUE(doc->goToFrame(0));
    settle();
    auto* tree = window->findChild<QTreeView*>(QStringLiteral("layerTree"));
    auto* model = static_cast<LayerTreeModel*>(tree->model());
    ASSERT_TRUE(model->setData(model->indexOf(sky), true, LayerTreeModel::SharedRole));
    settle();
    click(tree->viewport(), tree->visualRect(model->indexOf(sky)).center());
    settle();
    ASSERT_EQ(doc->currentLayerId(), sky);
    use(panel(), "timelineNext");
    settle();
    EXPECT_EQ(doc->currentLayerId(), sky);
    EXPECT_EQ(model->layerAt(tree->currentIndex()), sky);
    click(strip(), strip()->cellRect(2).center());
    settle();
    EXPECT_EQ(doc->currentLayerId(), sky);
    click(panel()->ruler(), QPoint(panel()->ruler()->xOfBeat(0) + 2, 10));
    settle();
    EXPECT_EQ(doc->currentLayerId(), sky);
}

// The redesigned layout: the top bar's Timeline menu, undo and redo,
// the Frame menu (Delete at its foot), Play with its menu; the speed, Export, + Add and ⋯ on the right; the bottom bar's
// transport on the left, the play range, and the zoom and maximise on the right. No Remove Video button.
TEST_F(TimelineUiTest, TheBarsFollowTheMock) {
    showTimeline();
    TimelinePanel* p = panel();
    const auto x = [&](const char* name) {
        QAbstractButton* b = p->button(QString::fromLatin1(name));
        return b ? b->mapTo(p, QPoint(0, 0)).x() : -1;
    };
    const auto y = [&](const char* name) { return p->button(QString::fromLatin1(name))->mapTo(p, QPoint(0, 0)).y(); };
    const std::vector<const char*> top = {"timelineMenu",  "timelineUndo",   "timelineRedo",    "timelineFrameMenu", "timelinePlay",
                                          "timelineSpeed", "timelineExport", "timelineAddMenu", "timelineMore"};
    for (std::size_t i = 1; i < top.size(); ++i)
        EXPECT_LT(x(top[i - 1]), x(top[i])) << top[i];
    const std::vector<const char*> bottom = {"timelineFirst", "timelinePrevious", "timelinePlayBottom", "timelineNext",
                                             "timelineLast",  "timelineZoomOut",  "timelineZoomIn",     "timelineMaximize"};
    for (std::size_t i = 1; i < bottom.size(); ++i)
        EXPECT_LT(x(bottom[i - 1]), x(bottom[i])) << bottom[i];
    EXPECT_GT(y("timelineFirst"), y("timelineMenu") + strip()->height() / 2);
    // The menus hold the rest: the Frame menu ends in Delete, after a separator; Play's holds Loop and Settings.
    const auto menuOf = [&](const char* name) { return qobject_cast<QToolButton*>(p->button(QString::fromLatin1(name)))->menu(); };
    const QList<QAction*> frame = menuOf("timelineFrameMenu")->actions();
    ASSERT_GE(frame.size(), 5);
    EXPECT_EQ(frame.back()->objectName(), QStringLiteral("timelineDelete"));
    EXPECT_TRUE(frame[frame.size() - 2]->isSeparator());
    EXPECT_TRUE(menuOf("timelinePlay")->actions().contains(p->action(QStringLiteral("timelineLoop"))));
    EXPECT_TRUE(menuOf("timelinePlay")->actions().contains(p->action(QStringLiteral("timelineSettings"))));
    for (const char* name : {"timelineAddSound", "timelineAddVideo"})
        EXPECT_TRUE(menuOf("timelineAddMenu")->actions().contains(p->action(QString::fromLatin1(name)))) << name;
    EXPECT_FALSE(p->action(QStringLiteral("timelineAddTrack")));  // + Add Track under the rows only
    EXPECT_TRUE(p->button(QStringLiteral("timelineAddTrackButton")));
    EXPECT_TRUE(menuOf("timelineMore")->actions().contains(p->action(QStringLiteral("timelineOnionSkin"))));
    EXPECT_EQ(p->button(QStringLiteral("timelinePlay"))->property("role").toString(), QStringLiteral("primary"));
    EXPECT_FALSE(p->button(QStringLiteral("timelineRemoveVideo")));
    EXPECT_FALSE(p->action(QStringLiteral("timelineRemoveVideo")));
    EXPECT_TRUE(command("canvas.removeVideo"));  // the Canvas menu still takes a video away
    // The card's own title gives way to the Timeline menu.
    auto* title = card()->findChild<QLabel*>(QStringLiteral("cardTitle"));
    EXPECT_TRUE(title && !title->isVisible());
    // The panel still fits the tiled half of the screen, every button's text whole.
    window->resize(1280, 900);
    settle();
    EXPECT_LE(window->width(), 1280);
    for (QAbstractButton* b : p->findChildren<QAbstractButton*>())
        if (b->isVisible() && b->minimumWidth() != b->maximumWidth()) {  // the track bar's icons have their own fixed size
            EXPECT_GE(b->width(), b->sizeHint().width()) << b->objectName().toStdString();
        }
}

// The top bar's undo and redo are the canvas's; the transport's first and last go to the ends; maximise makes the timeline
// as tall as the window allows and gives its height back.
TEST_F(TimelineUiTest, UndoRedoFirstLastAndMaximize) {
    showTimeline();
    frames(4);
    EXPECT_TRUE(panel()->button(QStringLiteral("timelineUndo"))->isEnabled());
    EXPECT_FALSE(panel()->button(QStringLiteral("timelineRedo"))->isEnabled());
    use(panel(), "timelineUndo");
    EXPECT_EQ(doc->frameCount(), 3);
    EXPECT_TRUE(panel()->button(QStringLiteral("timelineRedo"))->isEnabled());
    use(panel(), "timelineRedo");
    EXPECT_EQ(doc->frameCount(), 4);
    use(panel(), "timelineFirst");
    EXPECT_EQ(doc->currentFrame(), 0);
    use(panel(), "timelineLast");
    EXPECT_EQ(doc->currentFrame(), 3);
    const int height = window->tabs()->timelineHeight();
    use(panel(), "timelineMaximize");
    settle();
    EXPECT_GT(window->tabs()->timelineHeight(), height);
    use(panel(), "timelineMaximize");
    settle();
    EXPECT_EQ(window->tabs()->timelineHeight(), height);
}

// The play range: Start and End set the beats Play plays and loops over, and its duration; End at the last beat follows the
// animation as it grows. Play's speed is playback only.
TEST_F(TimelineUiTest, ThePlayRangeAndSpeed) {
    showTimeline();
    frames(6);
    auto* start = panel()->findChild<QSpinBox*>(QStringLiteral("timelineRangeStart"));
    auto* end = panel()->findChild<QSpinBox*>(QStringLiteral("timelineRangeEnd"));
    QAbstractButton* duration = panel()->button(QStringLiteral("timelineDuration"));
    ASSERT_TRUE(start && end && duration);
    EXPECT_EQ(start->value(), 0);
    EXPECT_EQ(end->value(), 5);
    EXPECT_EQ(duration->text(), QStringLiteral("Duration: 6f"));  // 6 beats
    start->setValue(2);
    end->setValue(3);
    EXPECT_EQ(doc->animation().playFirst(), 2);
    EXPECT_EQ(doc->animation().playLast(), 3);
    EXPECT_EQ(duration->text(), QStringLiteral("Duration: 2f"));
    ASSERT_TRUE(doc->setPlayback(60, true));
    ASSERT_TRUE(doc->goToFrame(0));
    AnimationController* c = window->animation();
    c->togglePlay();
    ASSERT_TRUE(c->isPlaying());
    std::set<int> seen;
    QElapsedTimer clock;
    clock.start();
    while (clock.elapsed() < 400) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
        seen.insert(doc->beat());
    }
    c->stop();
    EXPECT_EQ(seen, (std::set<int>{2, 3}));  // from the range's start, looping inside it
    end->setValue(5);  // the last beat: follows the animation
    EXPECT_EQ(doc->animation().playEnd, -1);
    frames(8);
    EXPECT_EQ(end->value(), 7);
    // The speed menu: playback only, no undo step.
    const int steps = doc->undoStack()->count();
    use(panel(), "timelineSpeed50");
    EXPECT_EQ(doc->animation().view.speed, 0.5);
    EXPECT_EQ(panel()->button(QStringLiteral("timelineSpeed"))->text(), QStringLiteral("0.5x"));
    EXPECT_EQ(doc->undoStack()->count(), steps);
}

// The ruler labels beats from 0, as the mock: "0f, 6f, 12f".
TEST_F(TimelineUiTest, TheRulerLabelsBeatsAsTheMock) {
    EXPECT_EQ(TimeRuler::beatLabel(12), QStringLiteral("12f"));
    EXPECT_EQ(TimeRuler::beatLabelStep(104, 12), 1);
    EXPECT_EQ(TimeRuler::beatLabelStep(20, 12), 4);
    EXPECT_EQ(TimeRuler::beatLabelStep(14, 12), 6);
    EXPECT_EQ(TimeRuler::beatLabelStep(8, 12), 12);
    EXPECT_EQ(TimeRuler::beatLabelStep(2, 12), 60);  // whole seconds (5) when zoomed right out
}

// Feedback: "Ctrl+mouse wheel on the timeline should zoom in and out. Ctrl+shift+mouse wheel should increase/decrease height
// of the timeline."
TEST_F(TimelineUiTest, CtrlWheelZoomsAndCtrlShiftWheelChangesTheHeight) {
    showTimeline();
    frames(4);
    auto* zoom = panel()->findChild<QSlider*>(QStringLiteral("timelineZoom"));
    auto* height = panel()->findChild<QSlider*>(QStringLiteral("timelineTrackHeight"));
    zoom->setValue(40);
    settle();
    const QPoint over = strip()->cellRect(1).center();
    const int beatUnder = (over.x() - FrameStrip::kMargin) / strip()->beatWidth();
    const int screenX = strip()->mapTo(panel(), over).x();
    wheel(strip(), over, 120, Qt::ControlModifier);
    EXPECT_EQ(strip()->beatWidth(), 50);  // a quarter wider
    EXPECT_EQ(zoom->value(), 50);
    wheel(strip(), strip()->mapFrom(panel(), QPoint(screenX, strip()->mapTo(panel(), over).y())), -120, Qt::ControlModifier);
    EXPECT_EQ(strip()->beatWidth(), 40);
    // The beat under the pointer stays under it.
    const int nowAt = strip()->mapFrom(panel(), QPoint(screenX, 0)).x();
    EXPECT_EQ((nowAt - FrameStrip::kMargin) / strip()->beatWidth(), beatUnder);
    // From over the headers and the ruler too.
    wheel(panel()->ruler(), QPoint(20, 10), 120, Qt::ControlModifier);
    EXPECT_EQ(strip()->beatWidth(), 50);
    const int was = height->value();
    wheel(strip(), over, -120, Qt::ControlModifier | Qt::ShiftModifier);
    EXPECT_EQ(height->value(), was - 10);
    EXPECT_EQ(strip()->laneScale(), was - 10);
    wheel(panel()->headers(), QPoint(20, 20), 120, Qt::ControlModifier | Qt::ShiftModifier);
    EXPECT_EQ(strip()->laneScale(), was);
    EXPECT_EQ(strip()->beatWidth(), 50);  // the height alone
}

// Feedback: "I should be able to adjust the strength of the onion skin." Not an undo step, as the onion skin's switch.
TEST_F(TimelineUiTest, TheOnionSkinsStrengthHasASlider) {
    showTimeline();
    frames(2);
    auto* strength = panel()->findChild<QSlider*>(QStringLiteral("timelineOnionStrength"));
    ASSERT_TRUE(strength);
    EXPECT_EQ(strength->minimum(), TimelineView::kMinOnionOpacity);
    EXPECT_EQ(strength->maximum(), TimelineView::kMaxOnionOpacity);
    EXPECT_EQ(strength->value(), doc->animation().view.onionOpacity);
    const int steps = doc->undoStack()->count();
    strength->setValue(70);
    EXPECT_EQ(doc->animation().view.onionOpacity, 70);
    EXPECT_EQ(doc->undoStack()->count(), steps);
    // The Settings dialog's change shows on the slider.
    TimelineView view = doc->animation().view;
    view.onionOpacity = 15;
    doc->setTimelineView(view);
    EXPECT_EQ(strength->value(), 15);
    use(panel(), "timelineOnionSkin");
    EXPECT_FALSE(strength->isEnabled());  // no onion skin, no strength
}

// The redesign: a track folds to a thin row and opens again, from its chevron or its menu; folded, its frames are
// still clickable.
TEST_F(TimelineUiTest, TracksFoldAndOpen) {
    showTimeline();
    frames(3);
    use(panel(), "timelineAddTrackButton");
    settle();
    TrackHeaders* h = panel()->headers();
    const int open = strip()->slotRect(0).height();
    click(h, h->chevronRect(h->trackRow(0)).center());
    settle();
    EXPECT_TRUE(doc->animation().view.collapsed.count(doc->animation().tracks[0].id));
    EXPECT_TRUE(strip()->collapsed(0));
    EXPECT_EQ(strip()->laneHeight(0), FrameStrip::kCollapsedLaneHeight);
    EXPECT_LT(strip()->slotRect(0).height(), open / 2);
    EXPECT_EQ(h->trackRow(0).height(), strip()->slotRect(0).height());
    EXPECT_EQ(h->trackRow(1).bottom() + 1, h->trackRow(0).top());
    click(strip(), strip()->cellRect(0, 2).center());
    EXPECT_EQ(doc->currentTrack(), 0);
    EXPECT_EQ(doc->currentFrame(), 2);
    click(h, h->chevronRect(h->trackRow(0)).center());
    settle();
    EXPECT_FALSE(strip()->collapsed(0));
    EXPECT_EQ(strip()->slotRect(0).height(), open);
}

// A track's row, as the mock's: its picture's tile, its name, then the lock, the eye and the fold chevron on the right; the
// current one filled with the accent. The bar over the rows acts on the current track.
TEST_F(TimelineUiTest, ATrackRowAndTheTrackBar) {
    ControllerRig rig;
    ASSERT_TRUE(rig.doc.addTrack());
    rig.panel.show();
    settle();
    TrackHeaders* h = rig.panel.headers();
    const QRect row = h->trackRow(0);
    EXPECT_EQ(h->eyeRect(row).top(), h->lockRect(row).top());
    EXPECT_LT(h->thumbRect(row).right(), h->lockRect(row).left());
    EXPECT_LT(h->lockRect(row).right(), h->eyeRect(row).left());
    EXPECT_LT(h->eyeRect(row).right(), h->chevronRect(row).left());
    EXPECT_LE(h->chevronRect(row).right(), row.right());
    EXPECT_TRUE(h->volumeRect(0).isEmpty());  // a frames track has no volume
    const QImage shot = h->grab().toImage();
    const QRgb accent = Theme::color(Theme::Token::Accent).rgb();
    EXPECT_EQ(shot.pixelColor(h->trackRow(1).right() - 2, h->trackRow(1).top() + 2).rgb(), accent);  // the current track
    EXPECT_NE(shot.pixelColor(row.right() - 2, row.top() + 2).rgb(), accent);
    // The bar: lock, hide, duplicate and delete the current track.
    ASSERT_EQ(rig.doc.currentTrack(), 1);
    use(&rig.panel, "timelineTrackLock");
    EXPECT_TRUE(rig.doc.animation().tracks[1].locked);
    use(&rig.panel, "timelineTrackLock");
    EXPECT_FALSE(rig.doc.animation().tracks[1].locked);
    use(&rig.panel, "timelineTrackEye");
    EXPECT_TRUE(rig.doc.animation().tracks[1].hidden);  // a hidden track is no longer the current one
    ASSERT_TRUE(rig.doc.setTrackHidden(1, false));
    (void)rig.doc.goToCell(1, 0);
    ASSERT_EQ(rig.doc.currentTrack(), 1);
    ASSERT_TRUE(rig.doc.renameTrack(1, QStringLiteral("Ball")));
    use(&rig.panel, "timelineTrackDuplicate");
    ASSERT_EQ(rig.doc.animation().trackCount(), 3);
    EXPECT_EQ(rig.doc.animation().tracks[2].name, QStringLiteral("Ball copy"));
    EXPECT_EQ(rig.doc.currentTrack(), 2);
    use(&rig.panel, "timelineTrackDelete");
    EXPECT_EQ(rig.doc.animation().trackCount(), 2);
    // + Add Track under the rows.
    use(&rig.panel, "timelineAddTrackButton");
    EXPECT_EQ(rig.doc.animation().trackCount(), 3);
}

// The redesign: a sound track's row has a speaker that mutes it and its volume, one undo step a drag.
TEST_F(TimelineUiTest, ASoundTrackHasMuteAndVolume) {
    ControllerRig rig;
    rig.panel.resize(1000, 420);
    ASSERT_TRUE(rig.controller.addSoundFile(QStringLiteral("/sounds/a.wav"), QByteArray("RIFF"), 1.0));
    rig.panel.show();
    settle();
    const Animation& a = rig.doc.animation();
    const int sound = a.indexOfTrack(a.sounds.front().track);
    TrackHeaders* h = rig.panel.headers();
    const QRect volume = h->volumeRect(sound);
    ASSERT_FALSE(volume.isEmpty());
    const int steps = rig.doc.undoStack()->count();
    press(h, QPoint(volume.right() - 2, volume.center().y()));
    move(h, QPoint(volume.left() + volume.width() / 2, volume.center().y()));
    release(h, QPoint(volume.left() + volume.width() / 2, volume.center().y()));
    EXPECT_NEAR(rig.doc.animation().tracks[static_cast<std::size_t>(sound)].volume, 50, 3);
    EXPECT_EQ(rig.doc.undoStack()->count(), steps + 1);
    EXPECT_NEAR(rig.doc.animation().gainOf(rig.doc.animation().sounds.front()), 0.5, 0.03);
    click(h, h->eyeRect(h->trackRow(sound)).center());
    EXPECT_TRUE(rig.doc.animation().tracks[static_cast<std::size_t>(sound)].hidden);
    EXPECT_FALSE(rig.doc.animation().heard(rig.doc.animation().sounds.front()));
    // Short rows keep the volume, beside the name.
    rig.panel.findChild<QSlider*>(QStringLiteral("timelineTrackHeight"))->setValue(FrameStrip::kMinLaneScale);
    settle();
    const QRect inline_ = h->volumeRect(sound);
    EXPECT_FALSE(inline_.isEmpty());
    EXPECT_TRUE(h->trackRow(sound).contains(inline_));
    EXPECT_LT(inline_.right(), h->eyeRect(h->trackRow(sound)).left());
}

// The redesign: moves snap to frames, the sound clips' edges and the playhead. Zoomed out, a clip dragged near a
// frame's start lands on it; a trimmed end snaps at any zoom.
TEST_F(TimelineUiTest, ClipsAndHoldsSnap) {
    ControllerRig rig;
    rig.panel.resize(1000, 420);
    rig.panel.show();
    for (int i = 0; i < 3; ++i)
        ASSERT_TRUE(rig.doc.newFrame());
    ASSERT_TRUE(rig.doc.changeHold({0}, +4));  // frames start at beats 0, 5, 6, 7
    (void)rig.doc.goToBeat(7);
    settle();
    FrameStrip* s = rig.panel.strip();
    s->setBeatWidth(FrameStrip::kMinBeatWidth);
    settle();
    EXPECT_EQ(s->snapBeat(4), 5);   // a beat (8 px) from a frame's start: snaps onto it
    EXPECT_EQ(s->snapBeat(2), 2);   // nothing within 10 px
    EXPECT_EQ(s->snapBeat(1), 0);
    EXPECT_FALSE(s->snapTarget(2.5));
    EXPECT_EQ(s->snapTarget(6.9), 7);
    // A clip on its own track, dragged from beat 9 to near beat 4: it lands on 5.
    ASSERT_TRUE(rig.controller.addSoundFile(QStringLiteral("/sounds/a.wav"), QByteArray("RIFF"), 2.0));
    const int id = rig.doc.animation().sounds.front().id;
    ASSERT_TRUE(rig.doc.moveSound(id, 12, rig.doc.animation().sounds.front().track));
    settle();
    EXPECT_EQ(s->snapBeat(11), 12);  // the clip's own start counts for others
    EXPECT_EQ(s->snapBeat(11, {id}), 11);  // but not for itself
    const QRect r = s->soundRect(id);
    const QPoint grab(r.left() + r.width() / 2, r.center().y());
    press(s, grab);
    move(s, grab + QPoint(-4 * s->beatWidth(), 0));
    move(s, grab + QPoint(-8 * s->beatWidth(), 0));
    release(s, grab + QPoint(-8 * s->beatWidth(), 0));
    EXPECT_EQ(rig.doc.animation().sounds.front().startBeat, 5);  // 4 asked, 5 a frame's start
    // A frame's hold snaps without its own track's frames, which move with it: beat 6 is a frame's start, but left out it
    // goes to the playhead (7) or the clip's start (5).
    EXPECT_EQ(s->snapBeat(6), 6);
    EXPECT_NE(s->snapBeat(6, {}, rig.doc.currentTrack()), 6);
}

// The redesign: larger thumbnails, stronger outlines, and a playhead line with one track too, under its chip.
TEST_F(TimelineUiTest, ThePlayheadShowsWithOneTrack) {
    showTimeline();
    frames(3);
    ASSERT_TRUE(doc->goToFrame(1));
    strip()->setBeatWidth(FrameStrip::kMaxBeatWidth);
    settle();
    EXPECT_EQ(strip()->cellHeight(), FrameStrip::kCellHeight * strip()->laneScale() / 100);
    const QImage shot = strip()->grab().toImage();
    const QColor accent = Theme::color(Theme::Token::AccentHover);
    // The playhead's line runs above the cells, from the top.
    const int x = strip()->xOfBeat(doc->beat());
    EXPECT_EQ(shot.pixelColor(x, 2).rgb(), accent.rgb());
    // The ruler's chip over the playhead.
    const QImage ruler = panel()->ruler()->grab().toImage();
    EXPECT_EQ(ruler.pixelColor(panel()->ruler()->xOfBeat(doc->beat()) + 1, 3).rgb(), accent.rgb());  // above its number
}

// The redesigned timeline, to look at: empty, with frames, with a sound, folded and open, and the onion skins at
// three strengths (DOODLEBOLD_SCREENSHOT_DIR).
TEST_F(TimelineUiTest, ScreenshotsOfTheRedesignedTimeline) {
    window->resize(1280, 900);  // the tiled half of the screen
    showTimeline();
    window->tabs()->setTimelineHeight(330);
    waitFor([] { return false; }, 300);
    EXPECT_TRUE(test::saveScreenshot(card()->grab(), QStringLiteral("redesign_1_empty")));

    const LayerId sky = doc->currentLayerId();
    ASSERT_TRUE(doc->fillLayer(sky, QColor(208, 226, 246)));
    ASSERT_TRUE(doc->addLayer());
    ASSERT_TRUE(doc->setLayerShared(sky, true));
    for (int i = 0; i < 10; ++i) {
        if (i > 0) {
            ASSERT_TRUE(doc->newFrame());
        }
        auto target = doc->paintTarget(doc->currentLayerId());
        ASSERT_TRUE(target);
        const QRect ball(160 + i * 110, 200 + (i % 3) * 70, 150, 110);
        target.value()->fill(ball, QColor(232, 86, 72));
        doc->notifyPixelsChanged(doc->currentLayerId(), ball);
    }
    ASSERT_TRUE(doc->changeHold({2}, +1));
    ASSERT_TRUE(doc->goToFrame(3));
    TimelineView view = doc->animation().view;
    view.onionSkin = false;
    doc->setTimelineView(view);
    waitFor([] { return false; }, 500);  // the thumbnails fill in
    EXPECT_TRUE(test::saveScreenshot(card()->grab(), QStringLiteral("redesign_2_frames")));

    // A second drawn track and a sound with its waveform.
    ASSERT_TRUE(doc->addTrack());
    ASSERT_TRUE(doc->renameTrack(1, QStringLiteral("Grass")));
    ASSERT_TRUE(doc->fillLayer(doc->currentLayerId(), QColor(60, 160, 90, 120)));
    ASSERT_TRUE(doc->changeHold({0}, +3));
    if (test::systemFfmpeg()) {
        ASSERT_TRUE(doc->goToBeat(1));
        ASSERT_TRUE(window->animation()->addSoundFile(QStringLiteral("/sounds/step.wav"), halfLoudWav(0.8), 0.8));
        settle();
        const SoundClip& clip = doc->animation().sounds.front();
        const int sound = doc->animation().indexOfTrack(clip.track);
        (void)window->animation()->soundPeaks(clip);
        window->animation()->waitForPeaks();
        ASSERT_TRUE(doc->setTrackVolume(sound, 70));
    }
    const auto& tracks = doc->animation().tracks;
    const int drawn = static_cast<int>(std::ranges::find_if(tracks, [](const AnimationTrack& t) { return t.isDrawn(); }) -
                                       tracks.begin());  // the first, the balls'
    ASSERT_TRUE(doc->goToCell(drawn, 4));
    window->tabs()->setTimelineHeight(560);  // room for the three tracks
    waitFor([] { return false; }, 500);
    EXPECT_TRUE(test::saveScreenshot(card()->grab(), QStringLiteral("redesign_3_audio_expanded")));
    // Beside the mock: the 1280 px window, and the narrowest the window goes.
    EXPECT_TRUE(test::saveScreenshot(card()->grab(), QStringLiteral("mock_compare_1280")));
    window->resize(1, 900);
    settle();
    waitFor([] { return false; }, 300);
    EXPECT_TRUE(test::saveScreenshot(window->grab(), QStringLiteral("mock_compare_narrowest_window")));
    EXPECT_TRUE(test::saveScreenshot(card()->grab(), QStringLiteral("mock_compare_narrowest")));
    window->resize(1280, 900);
    settle();
    // Every track folded, then the drawn ones open again.
    view = doc->animation().view;
    for (const AnimationTrack& t : doc->animation().tracks)
        view.collapsed.insert(t.id);
    doc->setTimelineView(view);
    waitFor([] { return false; }, 200);
    EXPECT_TRUE(test::saveScreenshot(card()->grab(), QStringLiteral("redesign_4_collapsed")));
    view.collapsed.clear();
    doc->setTimelineView(view);
    window->tabs()->setTimelineHeight(330);
    // Zoomed out a little, and the tracks shorter (Ctrl+wheel and Ctrl+Shift+wheel).
    panel()->findChild<QSlider*>(QStringLiteral("timelineZoom"))->setValue(40);
    panel()->findChild<QSlider*>(QStringLiteral("timelineTrackHeight"))->setValue(60);
    waitFor([] { return false; }, 300);
    EXPECT_TRUE(test::saveScreenshot(card()->grab(), QStringLiteral("redesign_5_zoomed_shorter")));

    // The onion skins at three strengths: the window, canvas and timeline.
    panel()->findChild<QSlider*>(QStringLiteral("timelineTrackHeight"))->setValue(100);
    view = doc->animation().view;
    view.onionSkin = true;
    view.onionBefore = 2;
    view.onionAfter = 2;
    doc->setTimelineView(view);
    for (int t = 0; t < doc->animation().trackCount(); ++t)
        if (doc->animation().tracks[static_cast<std::size_t>(t)].name == QStringLiteral("Grass")) {
            ASSERT_TRUE(doc->setTrackHidden(t, true));  // the balls alone
        }
    (void)doc->goToCell(drawn, 4);  // there already, unless hiding Grass moved the current track
    window->tabs()->currentCanvas()->resetView();
    for (int strength : {10, 40, 80}) {
        panel()->findChild<QSlider*>(QStringLiteral("timelineOnionStrength"))->setValue(strength);
        waitFor([] { return false; }, 300);
        EXPECT_TRUE(test::saveScreenshot(window->grab(), QStringLiteral("redesign_onion_%1").arg(strength)));
    }
}

// The list after trying the timeline build ("Remove keyframes … Keep the UI changes, save for …").

// 1, 2: no tweens, no Auto Key or Keyframe; 3: the tracks' Height back in the bottom bar, as master has it.
TEST_F(TimelineUiTest, NoTweensAndTheHeightIsBackInTheBottomBar) {
    showTimeline();
    for (const char* name : {"timelineAutoKey", "timelineKeyframe", "timelineShowTweens", "timelineCurveX"}) {
        EXPECT_FALSE(panel()->button(QString::fromLatin1(name))) << name;
        EXPECT_FALSE(panel()->action(QString::fromLatin1(name))) << name;
    }
    auto* height = panel()->findChild<QSlider*>(QStringLiteral("timelineTrackHeight"));
    ASSERT_TRUE(height);
    EXPECT_TRUE(height->isVisible());  // in the bar, not in a menu
    EXPECT_GT(height->mapTo(panel(), QPoint(0, 0)).y(),
              panel()->button(QStringLiteral("timelineFirst"))->mapTo(panel(), QPoint(0, 0)).y() - 1);
    height->setValue(FrameStrip::kMinLaneScale);
    EXPECT_EQ(strip()->laneScale(), FrameStrip::kMinLaneScale);
}

// 4: frames or time on the ruler, the frames' labels and the play range; a click on the Duration or ⋯ ▸ Show Time switches,
// and the choice is kept in the settings.
TEST_F(TimelineUiTest, FramesOrTime) {
    showTimeline();
    frames(6);
    QAbstractButton* duration = panel()->button(QStringLiteral("timelineDuration"));
    auto* start = panel()->findChild<QSpinBox*>(QStringLiteral("timelineRangeStart"));
    ASSERT_TRUE(duration && start);
    EXPECT_FALSE(panel()->showsTime());
    EXPECT_EQ(duration->text(), QStringLiteral("Duration: 6f"));
    start->setValue(3);
    EXPECT_EQ(start->text(), QStringLiteral("Start: 3f"));
    use(panel(), "timelineDuration");
    EXPECT_TRUE(panel()->showsTime());
    EXPECT_EQ(duration->text(), QStringLiteral("Duration: 0.25s"));  // 3 beats at 12 fps
    EXPECT_EQ(start->text(), QStringLiteral("Start: 0.25s"));
    EXPECT_EQ(TimeRuler::beatLabel(6, 12.0, true), QStringLiteral("0.50s"));
    EXPECT_TRUE(panel()->action(QStringLiteral("timelineShowTime"))->isChecked());
    // Kept: a new timeline starts so.
    TimelinePanel another;
    EXPECT_TRUE(another.showsTime());
    use(panel(), "timelineShowTime");
    EXPECT_FALSE(panel()->showsTime());
    EXPECT_EQ(start->text(), QStringLiteral("Start: 3f"));
}

// 6: the menus' on/off items are switches drawn alike everywhere, not the platform's tick; the chosen speed has a drawn mark.
TEST_F(TimelineUiTest, MenusUseSwitchesNotTicks) {
    showTimeline();
    for (const char* name : {"timelineOnionSkin", "timelineLoop", "timelineShowTime"}) {
        auto* action = qobject_cast<QWidgetAction*>(panel()->action(QString::fromLatin1(name)));
        ASSERT_TRUE(action) << name;
        auto* row = qobject_cast<QToolButton*>(action->defaultWidget());
        ASSERT_TRUE(row) << name;
        EXPECT_EQ(row->property("role").toString(), QStringLiteral("menuSwitch"));
        EXPECT_EQ(row->isChecked(), action->isChecked()) << name;
        row->click();  // the row turns the item
        EXPECT_EQ(row->isChecked(), action->isChecked()) << name;
        row->click();
    }
    for (QAction* speed : panel()->findChildren<QAction*>())
        if (speed->objectName().startsWith(QStringLiteral("timelineSpeed"))) {
            EXPECT_FALSE(speed->isCheckable()) << speed->objectName().toStdString();  // a mark, not a tick
        }
}

// 7, 8: + Add Track has a row of its own under the track rows: with ten tracks it never covers one, and scrolled to the
// bottom the last row shows whole above it.
TEST_F(TimelineUiTest, AddTrackNeverCoversTheRows) {
    showTimeline();
    for (int i = 0; i < 9; ++i)
        use(panel(), "timelineAddTrackButton");
    settle();
    ASSERT_EQ(doc->animation().trackCount(), 10);
    QAbstractButton* add = panel()->button(QStringLiteral("timelineAddTrackButton"));
    TrackHeaders* h = panel()->headers();
    const QRect addAt(add->mapTo(panel(), QPoint(0, 0)), add->size());
    const QRect rowsAt(h->mapTo(panel(), QPoint(0, 0)), h->size());
    EXPECT_GE(addAt.top(), rowsAt.bottom());  // under the rows, never over them
    EXPECT_LE(addAt.bottom(), panel()->rect().bottom());  // and whole
    EXPECT_GE(add->height(), add->sizeHint().height());
    auto* scroll = panel()->findChild<QScrollArea*>(QStringLiteral("timelineScroll"));
    QWidget* bar = panel()->findChild<QWidget*>(QStringLiteral("timelineTrackBar"));
    const QImage barBefore = bar->grab().toImage();
    scroll->verticalScrollBar()->setValue(scroll->verticalScrollBar()->maximum());
    settle();
    EXPECT_LE(h->trackRow(0).bottom(), h->height() + FrameStrip::kTrackGap);  // the bottom track's row in sight
    // Scrolled part way, the top row partly off: the rows start under the track bar and the lanes under the ruler, in step,
    // and nothing of a row shows in the bar ("Track 5" once showed under its icons).
    scroll->verticalScrollBar()->setValue(scroll->verticalScrollBar()->maximum() / 2 + FrameStrip::kTrackGap * 3);
    settle();
    const int top = h->mapTo(panel(), QPoint(0, 0)).y();
    EXPECT_GE(top, bar->mapTo(panel(), QPoint(0, bar->height())).y());
    EXPECT_EQ(top, scroll->viewport()->mapTo(panel(), QPoint(0, 0)).y());
    EXPECT_GE(scroll->mapTo(panel(), QPoint(0, 0)).y(), panel()->ruler()->mapTo(panel(), QPoint(0, panel()->ruler()->height())).y());
    const int cut = h->trackAt(QPoint(h->width() / 2, 0));
    ASSERT_GE(cut, 0);
    EXPECT_LT(h->trackRow(cut).top(), 0);  // a row is cut at the top
    EXPECT_EQ(h->trackRow(cut).top(), strip()->slotRect(cut).top() - scroll->verticalScrollBar()->value());  // in step
    EXPECT_EQ(bar->grab().toImage(), barBefore);  // the bar unchanged by the rows under it
    EXPECT_EQ(bar->grab().toImage().pixelColor(bar->width() / 2, bar->height() - 1).rgb(), Theme::color(Theme::Token::Border).rgb());
}

// 9: label colours: the row's whole block in the track's colour (feedback: "color the entire block"), its lane tinted; one
// undo step.
TEST_F(TimelineUiTest, TracksHaveLabelColours) {
    showTimeline();
    use(panel(), "timelineAddTrackButton");
    settle();
    ASSERT_TRUE(doc->setTrackLabel(0, 1));
    settle();
    TrackHeaders* h = panel()->headers();
    const QImage rows = h->grab().toImage();
    const QRect row = h->trackRow(0);
    const QColor red = track_labels::colour(1);
    for (const QPoint& at :
         {QPoint(row.left() + 2, row.center().y()), QPoint(row.center().x(), row.top() + 3), QPoint(row.right() - 3, row.bottom() - 3)}) {
        const QColor c = rows.pixelColor(at);
        EXPECT_GT(c.red(), c.green() + 40) << at.x() << "," << at.y();  // red across the whole row, not a strip
    }
    const QColor other = rows.pixelColor(h->trackRow(1).center().x(), h->trackRow(1).top() + 3);
    EXPECT_LT(other.red(), other.blue());  // the unlabelled (current) row is the accent, not red
    EXPECT_TRUE(red.isValid());
    const QImage lanes = strip()->grab().toImage();
    const QPoint between(strip()->width() - 4, strip()->slotRect(0).center().y());
    EXPECT_GT(lanes.pixelColor(between).red(), lanes.pixelColor(strip()->width() - 4, strip()->slotRect(1).center().y()).red());
    ASSERT_TRUE(doc->undo());
    EXPECT_EQ(doc->animation().tracks[0].label, 0);
}

// 10, 11: Ctrl+click and Shift+click choose several tracks; Del with the rows holding the keyboard deletes them in one step;
// Del on the canvas still empties the layer, the tracks left alone.
TEST_F(TimelineUiTest, SeveralTracksChosenAndDeletedWithDel) {
    showTimeline();
    for (int i = 0; i < 4; ++i)
        use(panel(), "timelineAddTrackButton");
    settle();
    ASSERT_EQ(doc->animation().trackCount(), 5);
    TrackHeaders* h = panel()->headers();
    const auto name = [&](int t) { return h->trackRow(t).center() - QPoint(0, 0); };
    h->setFocus();
    click(h, name(1));
    click(h, name(3), Qt::ControlModifier);
    EXPECT_EQ(h->selectedTracks(), (std::vector<int>{1, 3}));
    click(h, name(1));
    click(h, name(3), Qt::ShiftModifier);
    EXPECT_EQ(h->selectedTracks(), (std::vector<int>{1, 2, 3}));
    click(h, name(2), Qt::ControlModifier);
    EXPECT_EQ(h->selectedTracks(), (std::vector<int>{1, 3}));
    EXPECT_EQ(window->focusWidget(), h);  // the rows keep the keyboard, not the lanes
    const auto del = [&](QWidget* focus) {
        auto override = QKeyEvent(QEvent::ShortcutOverride, Qt::Key_Delete, Qt::NoModifier);
        QCoreApplication::sendEvent(focus, &override);
        if (override.isAccepted()) {
            auto press = QKeyEvent(QEvent::KeyPress, Qt::Key_Delete, Qt::NoModifier);
            QCoreApplication::sendEvent(focus, &press);
        } else {
            command("layer.erase")->trigger();
        }
    };
    // On the canvas, Del is the layer's.
    ASSERT_TRUE(doc->fillLayer(doc->currentLayerId(), Qt::red));
    CanvasView* canvas = window->tabs()->currentCanvas();
    canvas->setFocus();
    del(canvas);
    EXPECT_EQ(doc->animation().trackCount(), 5);
    EXPECT_TRUE(doc->currentLayer()->pixels.isEmpty());
    // On the rows, the chosen tracks go, in one step.
    h->setFocus();
    const int steps = doc->undoStack()->count();
    del(h);
    EXPECT_EQ(doc->animation().trackCount(), 3);
    EXPECT_EQ(doc->undoStack()->count(), steps + 1);
    ASSERT_TRUE(doc->undo());
    EXPECT_EQ(doc->animation().trackCount(), 5);
}

// 12: a custom speed, 0.1 to 10 times.
TEST_F(TimelineUiTest, ACustomSpeedUpToTenTimes) {
    showTimeline();
    frames(2);
    panel()->askSpeed = [](double) { return std::optional<double>(6.5); };
    use(panel(), "timelineSpeedCustom");
    EXPECT_DOUBLE_EQ(doc->animation().view.speed, 6.5);
    EXPECT_EQ(panel()->button(QStringLiteral("timelineSpeed"))->text(), QStringLiteral("6.5x"));
    EXPECT_EQ(panel()->action(QStringLiteral("timelineSpeedCustom"))->text(), QStringLiteral("Custom (6.5x) …"));
    panel()->askSpeed = [](double) { return std::optional<double>(25.0); };
    use(panel(), "timelineSpeedCustom");
    EXPECT_DOUBLE_EQ(doc->animation().view.speed, TimelineView::kMaxSpeed);
    panel()->askSpeed = [](double) { return std::optional<double>(); };  // Cancel
    use(panel(), "timelineSpeedCustom");
    EXPECT_DOUBLE_EQ(doc->animation().view.speed, TimelineView::kMaxSpeed);
    use(panel(), "timelineSpeed100");
    EXPECT_DOUBLE_EQ(doc->animation().view.speed, 1.0);
}

// The list, for them to look at (DOODLEBOLD_SCREENSHOT_DIR): label colours and two tracks chosen, time shown, the
// menus' switches, ten tracks scrolled to the bottom over + Add Track, and the narrowest window.
TEST_F(TimelineUiTest, ScreenshotsForReview) {
    window->resize(1280, 900);
    showTimeline();
    window->tabs()->setTimelineHeight(400);
    frames(8);
    for (int i = 0; i < 3; ++i)
        use(panel(), "timelineAddTrackButton");
    for (int t = 0; t < 4; ++t) {
        ASSERT_TRUE(doc->renameTrack(
            t, QStringList{QStringLiteral("Background"), QStringLiteral("Body"), QStringLiteral("Face"), QStringLiteral("Effects")}[t]));
        ASSERT_TRUE(doc->setTrackLabel(t, t * 2 + 1));
    }
    TrackHeaders* h = panel()->headers();
    click(h, h->trackRow(1).center());
    click(h, h->trackRow(2).center(), Qt::ControlModifier);
    waitFor([] { return false; }, 400);
    EXPECT_TRUE(test::saveScreenshot(card()->grab(), QStringLiteral("list_1280_frames")));
    panel()->setShowTime(true);
    waitFor([] { return false; }, 200);
    EXPECT_TRUE(test::saveScreenshot(card()->grab(), QStringLiteral("list_1280_time")));
    panel()->setShowTime(false);
    for (const char* name : {"timelineMore", "timelinePlay", "timelineSpeed"}) {
        QMenu* menu = qobject_cast<QToolButton*>(panel()->button(QString::fromLatin1(name)))->menu();
        menu->adjustSize();
        EXPECT_TRUE(test::saveScreenshot(menu->grab(), QStringLiteral("list_menu_%1").arg(QString::fromLatin1(name).mid(8))));
    }
    for (int i = 0; i < 6; ++i)
        use(panel(), "timelineAddTrackButton");
    auto* scroll = panel()->findChild<QScrollArea*>(QStringLiteral("timelineScroll"));
    settle();
    scroll->verticalScrollBar()->setValue(scroll->verticalScrollBar()->maximum());
    waitFor([] { return false; }, 300);
    EXPECT_TRUE(test::saveScreenshot(card()->grab(), QStringLiteral("list_ten_tracks_bottom")));
    scroll->verticalScrollBar()->setValue(scroll->verticalScrollBar()->maximum() / 2 + FrameStrip::kTrackGap * 3);  // a row cut
    waitFor([] { return false; }, 300);
    EXPECT_TRUE(test::saveScreenshot(card()->grab(), QStringLiteral("list_ten_tracks_middle")));
    window->resize(1, 900);
    settle();
    waitFor([] { return false; }, 300);
    EXPECT_TRUE(test::saveScreenshot(window->grab(), QStringLiteral("list_narrowest_window")));
    EXPECT_TRUE(test::saveScreenshot(card()->grab(), QStringLiteral("list_narrowest")));
}

// The next changes: the timeline starts at the zoom and height of their picture (timeline-default-zoom-height.png:
// 58 px a beat, the tracks at 65 %, the beats' labels left out), and its dialogs can't be stretched.
TEST_F(TimelineUiTest, TheTimelineStartsAtItsDefaultZoomAndHeight) {
    showTimeline();
    frames(3);
    EXPECT_EQ(strip()->beatWidth(), FrameStrip::kDefaultBeatWidth);
    EXPECT_EQ(panel()->findChild<QSlider*>(QStringLiteral("timelineZoom"))->value(), FrameStrip::kDefaultBeatWidth);
    EXPECT_EQ(strip()->laneScale(), FrameStrip::kDefaultLaneScale);
    EXPECT_EQ(panel()->findChild<QSlider*>(QStringLiteral("timelineTrackHeight"))->value(), FrameStrip::kDefaultLaneScale);
    EXPECT_EQ(strip()->labelHeight(), 0);
    EXPECT_EQ(strip()->cellHeight(), FrameStrip::kCellHeight * FrameStrip::kDefaultLaneScale / 100);
    EXPECT_EQ(TimeRuler::beatLabelStep(FrameStrip::kDefaultBeatWidth, 12), 2);  // labels every 2 beats, 0.17 s at 12 fps
}

TEST_F(TimelineUiTest, TheTimelinesDialogsCantBeStretched) {
    Animation a;
    VideoInfo info;
    info.size = QSize(640, 360);
    info.fps = 24;
    info.seconds = 221.1;
    info.hasSound = true;
    AddVideoDialog video(QStringLiteral("a rather long song's video, as the was (Official).mp4"), info, a, QSize(2000, 1125));
    video.show();
    settle();
    EXPECT_EQ(video.minimumSize(), video.maximumSize());
    const QSize was = video.size();
    video.resize(was.width(), was.height() * 2);  // stretched by hand
    settle();
    EXPECT_EQ(video.size(), was);
    // Its contents fit: the last control's bottom inside it.
    const QList<QAbstractButton*> buttons = video.findChildren<QAbstractButton*>();
    for (QAbstractButton* b : buttons)
        if (b->isVisible()) {
            EXPECT_LE(b->mapTo(&video, QPoint(0, b->height())).y(), video.height()) << b->text().toStdString();
        }
    TimelineSettingsDialog settings(a);
    settings.show();
    settle();
    EXPECT_EQ(settings.minimumSize(), settings.maximumSize());
    EXPECT_TRUE(test::saveScreenshot(video.grab(), QStringLiteral("dialog_add_video")));
    EXPECT_TRUE(test::saveScreenshot(settings.grab(), QStringLiteral("dialog_timeline_settings")));
}
