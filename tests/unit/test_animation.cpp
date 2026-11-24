#include "core/animation/AnimationOps.h"
#include "core/document/Document.h"
#include "core/io/DoodleFormat.h"

#include <QBuffer>
#include <QJsonObject>
#include <QJsonDocument>
#include <QJsonArray>

#include <algorithm>
#include <functional>
#include <stdexcept>

#include <gtest/gtest.h>

using namespace doodlebold;

namespace {

// Paints `color` over `area` of the current layer and records it as one undo step, as a brush stroke would.
void paint(Document& doc, const QRect& area, const QColor& color) {
    const LayerId id = doc.currentLayerId();
    auto target = doc.paintTarget(id);
    ASSERT_TRUE(target);
    TiledImage* image = target.value();
    TileMap before;
    const QRect tiles = image->tileRange(area);
    for (int ty = tiles.top(); ty <= tiles.bottom(); ++ty)
        for (int tx = tiles.left(); tx <= tiles.right(); ++tx)
            before[TileCoord{tx, ty}] = image->tileShared(tx, ty);
    image->fill(area, color);
    doc.notifyPixelsChanged(id, area);
    doc.commitPixelEdit(id, before, QStringLiteral("Paint"));
}

QRgb shown(Document& doc, int x, int y) {
    return doc.projection().pixel(x, y);
}

QByteArray saved(const DocumentContent& c) {
    QBuffer buf;
    buf.open(QIODevice::WriteOnly);
    EXPECT_TRUE(DoodleFormat().write(c, buf, {}).ok());
    return buf.data();
}

FileResult<LoadedDocument> parse(const QByteArray& bytes) {
    QBuffer buf;
    buf.setData(bytes);
    buf.open(QIODevice::ReadOnly);
    return DoodleFormat().read(buf);
}

std::vector<int> holds(const Animation& a) {
    std::vector<int> out;
    for (const AnimationFrame& f : a.track()->frames)
        out.push_back(f.hold);
    return out;
}

}

TEST(Animation, APictureIsOneFrameOfOneBeat) {
    Document doc(QSize(64, 64), Qt::white);
    EXPECT_EQ(doc.frameCount(), 1);
    EXPECT_EQ(doc.currentFrame(), 0);
    EXPECT_FALSE(doc.animation().isAnimation());
    EXPECT_EQ(doc.animation().totalBeats(), 1);
}

TEST(Animation, NewFrameIsEmptyWithTheSameLayersAndBecomesCurrent) {
    Document doc(QSize(64, 64), Qt::white);
    paint(doc, QRect(0, 0, 10, 10), Qt::red);
    const QString name = doc.currentLayer()->props.name;
    auto added = doc.newFrame();
    ASSERT_TRUE(added);
    EXPECT_EQ(added.value(), 1);
    EXPECT_EQ(doc.frameCount(), 2);
    EXPECT_EQ(doc.currentFrame(), 1);
    ASSERT_NE(doc.currentLayer(), nullptr);
    EXPECT_EQ(doc.currentLayer()->props.name, name);
    EXPECT_TRUE(doc.currentLayer()->pixels.isEmpty());
    EXPECT_EQ(shown(doc, 5, 5), 0xffffffffu);
    ASSERT_TRUE(doc.goToFrame(0));
    EXPECT_EQ(shown(doc, 5, 5), QColor(Qt::red).rgba());
}

TEST(Animation, EachFrameKeepsItsOwnPaintingAndUndoGoesBackToItsFrame) {
    Document doc(QSize(64, 64), Qt::white);
    ASSERT_TRUE(doc.newFrame());
    paint(doc, QRect(0, 0, 10, 10), Qt::blue);
    ASSERT_TRUE(doc.goToFrame(0));
    EXPECT_EQ(shown(doc, 5, 5), 0xffffffffu);
    ASSERT_TRUE(doc.undo());
    EXPECT_EQ(doc.currentFrame(), 1);
    EXPECT_EQ(shown(doc, 5, 5), 0xffffffffu);
    ASSERT_TRUE(doc.redo());
    EXPECT_EQ(shown(doc, 5, 5), QColor(Qt::blue).rgba());
}

TEST(Animation, HoldsSetTheBeatsOfEachFrame) {
    Document doc(QSize(32, 32), Qt::white);
    ASSERT_TRUE(doc.newFrame());
    ASSERT_TRUE(doc.newFrame());
    ASSERT_TRUE(doc.changeHold({0, 2}, +1));
    ASSERT_TRUE(doc.changeHold({2}, +1));
    EXPECT_EQ(holds(doc.animation()), (std::vector<int>{2, 1, 3}));
    EXPECT_EQ(doc.animation().totalBeats(), 6);
    EXPECT_EQ(doc.animation().startBeat(2), 3);
    EXPECT_EQ(doc.animation().frameAtBeat(1), 0);
    EXPECT_EQ(doc.animation().frameAtBeat(2), 1);
    EXPECT_EQ(doc.animation().frameAtBeat(5), 2);
    EXPECT_EQ(doc.animation().frameAtBeat(99), 2);
    const auto less = doc.changeHold({1}, -1);
    ASSERT_FALSE(less);
    EXPECT_EQ(less.error(), EditError::NoChange);
    ASSERT_TRUE(doc.setFrameHold(1, 4, 77));
    ASSERT_TRUE(doc.setFrameHold(1, 5, 77));
    EXPECT_EQ(doc.animation().track()->frames[1].hold, 5);
    ASSERT_TRUE(doc.undo());  // the drag was one step
    EXPECT_EQ(doc.animation().track()->frames[1].hold, 1);
}

TEST(Animation, DuplicateCopiesThePaintingAfterTheFrames) {
    Document doc(QSize(64, 64), Qt::white);
    paint(doc, QRect(0, 0, 10, 10), Qt::red);
    ASSERT_TRUE(doc.changeHold({0}, +2));
    auto copy = doc.duplicateFrames({0});
    ASSERT_TRUE(copy);
    EXPECT_EQ(copy.value(), 1);
    EXPECT_EQ(doc.currentFrame(), 1);
    EXPECT_EQ(doc.animation().track()->frames[1].hold, 3);
    EXPECT_EQ(shown(doc, 5, 5), QColor(Qt::red).rgba());
    // The copy is a layer of its own: painting it leaves the first frame alone.
    paint(doc, QRect(0, 0, 10, 10), Qt::green);
    ASSERT_TRUE(doc.goToFrame(0));
    EXPECT_EQ(shown(doc, 5, 5), QColor(Qt::red).rgba());
}

TEST(Animation, DeleteKeepsAtLeastOneFrameAndMovesToTheNextOne) {
    Document doc(QSize(32, 32), Qt::white);
    ASSERT_TRUE(doc.newFrame());
    ASSERT_TRUE(doc.newFrame());
    const int third = doc.animation().track()->frames[2].id;
    ASSERT_TRUE(doc.goToFrame(1));
    ASSERT_TRUE(doc.deleteFrames({1}));
    EXPECT_EQ(doc.frameCount(), 2);
    EXPECT_EQ(doc.animation().track()->frames[size_t(doc.currentFrame())].id, third);
    const auto all = doc.deleteFrames({0, 1});
    ASSERT_FALSE(all);
    EXPECT_EQ(all.error(), EditError::LastFrame);
}

TEST(Animation, MovingFramesKeepsTheirOrderAndTheCurrentFrame) {
    Document doc(QSize(32, 32), Qt::white);
    for (int i = 0; i < 4; ++i)
        ASSERT_TRUE(doc.newFrame());
    std::vector<int> ids;
    for (const AnimationFrame& f : doc.animation().track()->frames)
        ids.push_back(f.id);
    ASSERT_TRUE(doc.goToFrame(1));
    ASSERT_TRUE(doc.moveFrames({1, 2}, 3));  // among the three that stay, after the third
    std::vector<int> now;
    for (const AnimationFrame& f : doc.animation().track()->frames)
        now.push_back(f.id);
    EXPECT_EQ(now, (std::vector<int>{ids[0], ids[3], ids[4], ids[1], ids[2]}));
    EXPECT_EQ(doc.currentFrame(), 3);
    EXPECT_EQ(doc.moveFrames({3, 4}, 3).error(), EditError::NoChange);
}

TEST(Animation, KeepOnlyThisFrameMakesAPictureAgain) {
    Document doc(QSize(64, 64), Qt::white);
    paint(doc, QRect(0, 0, 10, 10), Qt::red);
    ASSERT_TRUE(doc.newFrame());
    paint(doc, QRect(20, 20, 10, 10), Qt::blue);
    ASSERT_TRUE(doc.keepOnlyCurrentFrame());
    EXPECT_EQ(doc.frameCount(), 1);
    EXPECT_TRUE(doc.animation().tracks.empty());
    EXPECT_EQ(shown(doc, 25, 25), QColor(Qt::blue).rgba());
    EXPECT_EQ(shown(doc, 5, 5), 0xffffffffu);
    ASSERT_TRUE(doc.undo());
    EXPECT_EQ(doc.frameCount(), 2);
}

TEST(Animation, ASharedLayerShowsInEveryFrameAtTheBottom) {
    Document doc(QSize(64, 64), Qt::white);
    const LayerId sky = doc.currentLayerId();
    paint(doc, QRect(0, 0, 64, 64), Qt::cyan);
    ASSERT_TRUE(doc.addLayer());
    const LayerId ball = doc.currentLayerId();
    ASSERT_TRUE(doc.setLayerShared(ball, true));
    // Pinned at the bottom, below the layer that stays the frame's own.
    EXPECT_EQ(doc.layers().root().children.front().props.id, ball);
    ASSERT_TRUE(doc.setLayerShared(ball, false));
    ASSERT_TRUE(doc.setLayerShared(sky, true));
    EXPECT_EQ(doc.layers().root().children.front().props.id, sky);
    ASSERT_TRUE(doc.newFrame());
    EXPECT_EQ(shown(doc, 5, 5), QColor(Qt::cyan).rgba());
    EXPECT_TRUE(doc.layers().find(sky));
    EXPECT_NE(doc.currentLayerId(), sky);
    // Moving an own layer below it puts it back above.
    ASSERT_TRUE(doc.layerToBack(doc.currentLayerId()));
    EXPECT_EQ(doc.layers().root().children.front().props.id, sky);
}

TEST(Animation, MakingASharedLayerPerFrameLeavesEveryFrameAsItWas) {
    Document doc(QSize(64, 64), Qt::white);
    const LayerId sky = doc.currentLayerId();
    paint(doc, QRect(0, 0, 64, 64), Qt::cyan);
    ASSERT_TRUE(doc.addLayer());
    ASSERT_TRUE(doc.setLayerShared(sky, true));
    ASSERT_TRUE(doc.newFrame());
    ASSERT_TRUE(doc.newFrame());
    ASSERT_TRUE(doc.goToFrame(1));
    ASSERT_TRUE(doc.setLayerShared(sky, false));
    for (int f = 0; f < 3; ++f) {
        ASSERT_TRUE(doc.goToFrame(f) || f == 1);
        EXPECT_EQ(shown(doc, 5, 5), QColor(Qt::cyan).rgba()) << f;
    }
    // Each frame now has its own copy: painting one changes only that frame.
    ASSERT_TRUE(doc.goToFrame(0));
    const LayerId own = doc.layers().root().children.front().props.id;
    doc.setCurrentLayer(own);
    paint(doc, QRect(0, 0, 10, 10), Qt::red);
    ASSERT_TRUE(doc.goToFrame(2));
    EXPECT_EQ(shown(doc, 5, 5), QColor(Qt::cyan).rgba());
}

TEST(Animation, OnionSkinsShowTheNeighboursOnlyOnTheDisplay) {
    Document doc(QSize(64, 64), Qt::white);
    paint(doc, QRect(0, 0, 10, 10), Qt::black);
    ASSERT_TRUE(doc.newFrame());
    EXPECT_TRUE(doc.hasUnderlay());
    EXPECT_EQ(shown(doc, 5, 5), 0xffffffffu);  // the projection, which tools read, has no onion skin
    const QRgb onion = doc.displayImage().pixel(5, 5);
    EXPECT_NE(onion, 0xffffffffu);
    EXPECT_GT(qRed(onion), qBlue(onion));  // the frame before is tinted red
    TimelineView off = doc.animation().view;
    off.onionSkin = false;
    doc.setTimelineView(off);
    EXPECT_FALSE(doc.hasUnderlay());
    EXPECT_EQ(doc.displayImage().pixel(5, 5), 0xffffffffu);
}

TEST(Animation, TheVideoPictureIsUnderTheDrawings) {
    Document doc(QSize(32, 32), Qt::white);
    paint(doc, QRect(0, 0, 8, 8), Qt::red);
    VideoReference v;
    v.path = QStringLiteral("/nowhere/walk.mp4");
    v.sourceSize = QSize(32, 32);
    v.seconds = 1.0;
    v.fps = 12;
    v.opacity = 100;
    ASSERT_TRUE(doc.setVideo(v, false));
    QImage picture(32, 32, QImage::Format_RGBA8888_Premultiplied);
    picture.fill(Qt::blue);
    doc.setVideoPicture(picture);
    EXPECT_EQ(doc.displayImage().pixel(4, 4), QColor(Qt::red).rgba());
    EXPECT_EQ(doc.displayImage().pixel(20, 20), QColor(Qt::blue).rgba());
    EXPECT_EQ(shown(doc, 20, 20), 0xffffffffu);
    ASSERT_TRUE(doc.removeVideo());
    EXPECT_FALSE(doc.hasUnderlay());
}

TEST(Animation, DrawingsFollowTheVideoGrowsTheFrames) {
    Document doc(QSize(32, 32), Qt::white);
    VideoReference v;
    v.path = QStringLiteral("/nowhere/walk.mp4");
    v.seconds = 2.0;
    v.fps = 15;
    ASSERT_TRUE(doc.setVideo(v, true));
    EXPECT_EQ(doc.frameCount(), 30);
    EXPECT_DOUBLE_EQ(doc.animation().fps, 15.0);
    EXPECT_EQ(doc.currentFrame(), 0);
    EXPECT_EQ(doc.animation().videoPictureAt(29), 29);
    EXPECT_EQ(doc.animation().videoPictureAt(30), 29);  // past its end its last frame stays, as any track's
}

// An imported sound goes on a track of its own, at the bottom, as a video does.
TEST(Animation, AnImportedSoundGoesOnASoundTrackOfItsOwnAtTheBottom) {
    Document doc(QSize(32, 32), Qt::white);
    SoundClip a;
    a.name = QStringLiteral("a.wav");
    a.data = std::make_shared<const QByteArray>("RIFF");
    a.seconds = 1.0;
    auto first = doc.addSound(a);
    ASSERT_TRUE(first);
    SoundClip b = a;
    b.name = QStringLiteral("b.wav");
    b.startBeat = 6;
    auto second = doc.addSound(b);
    ASSERT_TRUE(second);
    const Animation& anim = doc.animation();
    ASSERT_EQ(anim.trackCount(), 3);  // the frames track and two sound tracks under it, the newer at the bottom
    EXPECT_EQ(anim.tracks[2].kind, TrackKind::Frames);
    EXPECT_EQ(anim.indexOfTrack(anim.sounds[0].track), 1);
    EXPECT_EQ(anim.indexOfTrack(anim.sounds[1].track), 0);
    EXPECT_EQ(anim.tracks[1].kind, TrackKind::Sound);
    EXPECT_EQ(anim.tracks[0].name, QStringLiteral("b.wav"));
    EXPECT_EQ(doc.currentTrack(), 2);  // the current track stays the frames one
    // A video goes at the bottom too, under the sounds.
    VideoReference v;
    v.path = QStringLiteral("/nowhere/walk.mp4");
    v.seconds = 1.0;
    ASSERT_TRUE(doc.setVideo(v, false));
    EXPECT_EQ(doc.animation().tracks[0].kind, TrackKind::Video);
    EXPECT_EQ(doc.animation().tracks[3].kind, TrackKind::Frames);
    EXPECT_EQ(doc.currentTrack(), 3);
    ASSERT_TRUE(doc.removeVideo());
    ASSERT_TRUE(doc.removeSound(first.value()));
    EXPECT_EQ(doc.animation().sounds.size(), 1u);
    EXPECT_EQ(doc.animation().trackCount(), 3);  // its track stays, empty, until deleted
}

namespace {

SoundClip oneSecond(const char* name) {
    SoundClip c;
    c.name = QString::fromLatin1(name);
    c.data = std::make_shared<const QByteArray>("RIFF");
    c.seconds = 1.0;
    return c;
}

const SoundClip& clipOf(const Document& doc, int id) {
    for (const SoundClip& c : doc.animation().sounds)
        if (c.id == id)
            return c;
    throw std::out_of_range("no such clip");
}

}

// The report: a clip could be dragged over another one, and down and down without end.
// The reports: a clip could be dragged over another one; dragged upwards its track vanished while it still played.
TEST(Animation, SoundClipsNeverOverlapAndStayOnSoundTracks) {
    Document doc(QSize(32, 32), Qt::white);
    const int a = doc.addSound(oneSecond("a.wav")).value();  // beats 0 … 12 at 12 fps
    SoundClip later = oneSecond("b.wav");
    later.startBeat = 30;
    const int b = doc.addSound(later).value();
    const int trackA = clipOf(doc, a).track;
    const int trackB = clipOf(doc, b).track;
    // Onto the other track where it doesn't fit as dropped (a report: it jumped out of sight): it stays on its own.
    ASSERT_TRUE(doc.moveSound(b, 4, trackA));
    EXPECT_EQ(clipOf(doc, b).track, trackB);
    EXPECT_EQ(clipOf(doc, b).startBeat, 4);
    // Where it fits, it goes onto it.
    ASSERT_TRUE(doc.moveSound(b, 20, trackA));
    EXPECT_EQ(clipOf(doc, b).track, trackA);
    // Along its track onto the middle of the first clip: it goes right after it, the nearer free place.
    ASSERT_TRUE(doc.moveSound(b, 6, trackA));
    EXPECT_EQ(clipOf(doc, b).startBeat, 12);
    // Just past the start of the first one: right before it has no room (it would start before 0), so after it again.
    EXPECT_FALSE(doc.moveSound(b, 1, trackA));
    // Over a frames track (upwards past the frames), it stays on its own sound track.
    const int framesTrack = doc.animation().tracks.back().id;
    ASSERT_TRUE(doc.moveSound(b, 40, framesTrack));
    EXPECT_EQ(clipOf(doc, b).track, trackA);
    EXPECT_EQ(clipOf(doc, b).startBeat, 40);
    EXPECT_EQ(doc.animation().tracks.back().kind, TrackKind::Frames);
}

// Several clips chosen at once move together, and never onto another clip.
TEST(Animation, SeveralClipsMoveTogetherAndNeverOverlap) {
    Document doc(QSize(32, 32), Qt::white);
    const int a = doc.addSound(oneSecond("a.wav")).value();  // beats 0 … 12 at 12 fps
    SoundClip later = oneSecond("b.wav");
    later.startBeat = 30;
    const int b = doc.addSound(later).value();
    SoundClip last = oneSecond("c.wav");
    last.startBeat = 60;
    const int c = doc.addSound(last).value();
    const int trackA = clipOf(doc, a).track, trackB = clipOf(doc, b).track, trackC = clipOf(doc, c).track;
    const int steps = doc.undoStack()->count();
    // a and b on along their tracks, and b onto a's: one undo step.
    ASSERT_TRUE(doc.placeSounds({{a, 5, trackA}, {b, 35, trackA}}));
    EXPECT_EQ(clipOf(doc, a).startBeat, 5);
    EXPECT_EQ(clipOf(doc, b).startBeat, 35);
    EXPECT_EQ(clipOf(doc, b).track, trackA);
    EXPECT_EQ(doc.undoStack()->count(), steps + 1);
    // Where any would overlap a clip, none moves: c is at 60 on its own track.
    EXPECT_FALSE(doc.placeSounds({{a, 55, trackC}, {b, 80, trackA}}));
    // Nor onto each other, before the first beat, or off the sound tracks.
    EXPECT_FALSE(doc.placeSounds({{a, 30, trackA}, {b, 35, trackA}}));
    EXPECT_FALSE(doc.placeSounds({{a, -1, trackA}, {b, 35, trackA}}));
    EXPECT_FALSE(doc.placeSounds({{a, 5, doc.animation().tracks.back().id}}));
    EXPECT_EQ(clipOf(doc, a).startBeat, 5);
    EXPECT_EQ(clipOf(doc, a).track, trackA);
    // Into a free place on c's track, both of them.
    ASSERT_TRUE(doc.placeSounds({{a, 20, trackC}, {b, 40, trackC}}));
    EXPECT_EQ(clipOf(doc, a).track, trackC);
    ASSERT_TRUE(doc.undo());
    EXPECT_EQ(clipOf(doc, a).track, trackA);
    EXPECT_EQ(clipOf(doc, b).startBeat, 35);
    // Not off or onto a locked track.
    ASSERT_TRUE(doc.setTrackLocked(doc.animation().indexOfTrack(trackB), true));
    EXPECT_FALSE(doc.placeSounds({{a, 5, trackB}}));
    // Removed together, one undo step; with one on a locked track, none goes.
    ASSERT_TRUE(doc.removeSounds({a, b}));
    EXPECT_EQ(doc.animation().sounds.size(), 1u);
    ASSERT_TRUE(doc.undo());
    EXPECT_EQ(doc.animation().sounds.size(), 3u);
    ASSERT_TRUE(doc.setTrackLocked(doc.animation().indexOfTrack(trackA), true));
    EXPECT_FALSE(doc.removeSounds({a, c}));
    EXPECT_EQ(doc.animation().sounds.size(), 3u);
}

TEST(Animation, ASoundClipIsTrimmedAtEitherEnd) {
    Document doc(QSize(32, 32), Qt::white);
    SoundClip clip = oneSecond("a.wav");
    clip.seconds = 4.0;
    const int id = doc.addSound(clip).value();
    // Two beats off the start: the rest of the sound stays where it was under the frames.
    ASSERT_TRUE(doc.trimSound(id, 2, 2.0 / 12.0, 0.0));
    EXPECT_EQ(clipOf(doc, id).startBeat, 2);
    EXPECT_NEAR(clipOf(doc, id).playedSeconds(), 4.0 - 2.0 / 12.0, 1e-9);
    ASSERT_TRUE(doc.trimSound(id, 2, 2.0 / 12.0, 1.5));
    EXPECT_NEAR(clipOf(doc, id).endSeconds(12.0), 2.5, 1e-9);
    // Less than a beat left to play, or a trim that would overlap a neighbour, is refused.
    EXPECT_FALSE(doc.trimSound(id, 2, 2.0, 1.99));
    SoundClip next = oneSecond("b.wav");
    next.startBeat = 31;
    const int other = doc.addSound(next).value();
    ASSERT_TRUE(doc.moveSound(other, 31, clipOf(doc, id).track));  // ends at beat 30: it fits after it
    ASSERT_EQ(clipOf(doc, other).track, clipOf(doc, id).track);
    EXPECT_FALSE(doc.trimSound(id, 2, 2.0 / 12.0, 0.0));  // its end would reach beat 48, over the clip at 31
    // A changed frame rate keeps beats, so a clip that comes to overlap another moves to a new sound track.
    ASSERT_TRUE(doc.setPlayback(24, true));
    EXPECT_NE(clipOf(doc, id).track, clipOf(doc, other).track);
}

TEST(Animation, HoldForSetsTheExactBeatsOfEveryFrame) {
    Document doc(QSize(32, 32), Qt::white);
    ASSERT_TRUE(doc.newFrame());
    ASSERT_TRUE(doc.newFrame());
    ASSERT_TRUE(doc.setFramesHold({0, 2}, 7));
    EXPECT_EQ(holds(doc.animation()), (std::vector<int>{7, 1, 7}));
    EXPECT_FALSE(doc.setFramesHold({0, 2}, 7));
    ASSERT_TRUE(doc.setFramesHold({1}, 5000));
    EXPECT_EQ(doc.animation().holdOf(1), AnimationFrame::kMaxHold);
    ASSERT_TRUE(doc.undo());
    EXPECT_EQ(holds(doc.animation()), (std::vector<int>{7, 1, 7}));
}

TEST(Animation, CanvasEditsChangeEveryFrame) {
    Document doc(QSize(64, 32), Qt::white);
    paint(doc, QRect(0, 0, 8, 8), Qt::red);
    ASSERT_TRUE(doc.duplicateFrames({0}));
    ASSERT_TRUE(doc.rotateCanvas(true));
    EXPECT_EQ(doc.size(), QSize(32, 64));
    ASSERT_TRUE(doc.goToFrame(0));
    EXPECT_EQ(doc.framePicture(1).pixel(28, 4), QColor(Qt::red).rgba());
    EXPECT_EQ(shown(doc, 28, 4), QColor(Qt::red).rgba());
}

TEST(AnimationFile, FramesSharedLayersSoundsAndVideoSurviveSaving) {
    Document doc(QSize(64, 64), Qt::white);
    const LayerId sky = doc.currentLayerId();
    paint(doc, QRect(0, 0, 64, 64), Qt::cyan);
    ASSERT_TRUE(doc.addLayer());
    paint(doc, QRect(0, 0, 8, 8), Qt::red);
    ASSERT_TRUE(doc.setLayerShared(sky, true));
    ASSERT_TRUE(doc.newFrame());
    paint(doc, QRect(30, 30, 8, 8), Qt::blue);
    ASSERT_TRUE(doc.changeHold({1}, +2));
    ASSERT_TRUE(doc.setPlayback(24, false));
    SoundClip clip;
    clip.name = QStringLiteral("a.wav");
    clip.data = std::make_shared<const QByteArray>(QByteArray("RIFF\0\0\0\0WAVE", 12));
    clip.seconds = 0.5;
    clip.startBeat = 3;
    ASSERT_TRUE(doc.addSound(clip));
    VideoReference v;
    v.path = QStringLiteral("/videos/walk.mp4");
    v.sourceSize = QSize(1920, 1080);
    v.sourceFps = 30;
    v.seconds = 9.4;
    v.fps = 16;
    v.opacity = 40;
    ASSERT_TRUE(doc.setVideo(v, false));
    ASSERT_TRUE(doc.goToFrame(0));

    const QByteArray bytes = saved(doc.content());
    EXPECT_EQ(quint8(bytes[8]), DoodleFormat::kAnimationVersion);
    auto loaded = parse(bytes);
    ASSERT_TRUE(loaded) << loaded.error().detail.toStdString();
    Document back(loaded.value().content);
    const Animation& a = back.animation();
    ASSERT_EQ(back.frameCount(), 2);
    EXPECT_EQ(back.currentFrame(), 0);
    EXPECT_EQ(holds(a), (std::vector<int>{1, 3}));
    EXPECT_EQ(a.shared, std::vector<LayerId>{sky});
    EXPECT_DOUBLE_EQ(a.fps, 24.0);
    EXPECT_FALSE(a.loop);
    ASSERT_EQ(a.sounds.size(), 1u);
    EXPECT_EQ(a.sounds[0].name, clip.name);
    EXPECT_EQ(*a.sounds[0].data, *clip.data);
    EXPECT_EQ(a.sounds[0].startBeat, 3);
    ASSERT_TRUE(a.video);
    EXPECT_EQ(*a.video, v);
    EXPECT_EQ(back.projection().pixel(4, 4), QColor(Qt::red).rgba());
    EXPECT_EQ(back.framePicture(1).pixel(34, 34), QColor(Qt::blue).rgba());
    EXPECT_EQ(back.framePicture(1).pixel(4, 4), QColor(Qt::cyan).rgba());
    ASSERT_TRUE(back.goToFrame(1));
    EXPECT_EQ(back.projection().pixel(34, 34), QColor(Qt::blue).rgba());
}

TEST(AnimationFile, APictureIsStillWrittenAsTheOldVersion) {
    Document doc(QSize(32, 32), Qt::white);
    paint(doc, QRect(0, 0, 8, 8), Qt::red);
    const QByteArray bytes = saved(doc.content());
    EXPECT_EQ(quint8(bytes[8]), DoodleFormat::kVersion);
    auto loaded = parse(bytes);
    ASSERT_TRUE(loaded);
    EXPECT_TRUE(loaded.value().content.state.animation.tracks.empty());
}

TEST(AnimationFile, AFileThatLosesASoundIsRefused) {
    Document doc(QSize(32, 32), Qt::white);
    SoundClip clip;
    clip.name = QStringLiteral("a.wav");
    clip.data = std::make_shared<const QByteArray>("x");
    ASSERT_TRUE(doc.addSound(clip));
    DocumentContent c = doc.content();
    QByteArray bytes = saved(c);
    const auto at = bytes.indexOf("SOND");
    ASSERT_GT(at, 0);
    bytes[at] = 'X';
    EXPECT_FALSE(parse(bytes));
}

TEST(AnimationFile, TrimsAndAnOpenTimelineSurviveSaving) {
    Document doc(QSize(32, 32), Qt::white);
    ASSERT_TRUE(doc.newFrame());
    SoundClip clip = oneSecond("a.wav");
    clip.seconds = 3.0;
    const int id = doc.addSound(clip).value();
    ASSERT_TRUE(doc.trimSound(id, 1, 1.0 / 12.0, 0.75));
    TimelineView view = doc.animation().view;
    view.timelineShown = true;
    doc.setTimelineView(view);
    auto loaded = parse(saved(doc.content()));
    ASSERT_TRUE(loaded) << loaded.error().detail.toStdString();
    const Animation& a = loaded.value().content.state.animation;
    ASSERT_EQ(a.sounds.size(), 1u);
    EXPECT_DOUBLE_EQ(a.sounds[0].trimStart, 1.0 / 12.0);
    EXPECT_DOUBLE_EQ(a.sounds[0].trimEnd, 0.75);
    EXPECT_TRUE(a.view.timelineShown);
    view.timelineShown = false;
    doc.setTimelineView(view);
    EXPECT_FALSE(parse(saved(doc.content())).value().content.state.animation.view.timelineShown);
}

namespace {

int topLevelCount(const Document& doc) {
    return static_cast<int>(doc.state().layers.root().children.size());
}

// Two tracks: the first with a red frame and a green one, the second (over it, current) with one frame, blue in a corner.
void twoTracks(Document& doc) {
    paint(doc, QRect(0, 0, 32, 32), Qt::red);
    ASSERT_TRUE(doc.newFrame());
    paint(doc, QRect(0, 0, 32, 32), Qt::green);
    ASSERT_TRUE(doc.addTrack());
    paint(doc, QRect(0, 0, 8, 8), Qt::blue);
}

}

TEST(AnimationTracks, AnUpperTrackDrawsOverALowerOneAndItsLastFrameStays) {
    Document doc(QSize(32, 32), Qt::white);
    twoTracks(doc);
    const Animation& a = doc.animation();
    ASSERT_EQ(a.trackCount(), 2);
    EXPECT_EQ(doc.currentTrack(), 1);
    EXPECT_EQ(a.tracks[1].name, QStringLiteral("Track 2"));
    EXPECT_EQ(doc.beat(), 1);  // the new track came in at the playhead, on the green frame
    EXPECT_EQ(shown(doc, 0, 0), QColor(Qt::blue).rgba());
    EXPECT_EQ(shown(doc, 20, 20), QColor(Qt::green).rgba());
    EXPECT_EQ(topLevelCount(doc), 2);  // one layer of each track's frame at the playhead
    ASSERT_TRUE(doc.goToBeat(0));
    EXPECT_EQ(shown(doc, 20, 20), QColor(Qt::red).rgba());
    EXPECT_EQ(shown(doc, 0, 0), QColor(Qt::blue).rgba());
    EXPECT_EQ(a.totalBeats(), 2);
    // The second track has one beat; at beat 1 its last frame still shows.
    EXPECT_EQ(animation_ops::exportedBeat(doc.state(), 1).pixel(0, 0), QColor(Qt::blue).rgba());
    EXPECT_EQ(animation_ops::exportedBeat(doc.state(), 1).pixel(20, 20), QColor(Qt::green).rgba());
}

// A hidden track shows and plays nothing.
TEST(AnimationTracks, AHiddenTrackShowsNothingAndOneTrackStaysShown) {
    Document doc(QSize(32, 32), Qt::white);
    twoTracks(doc);
    ASSERT_TRUE(doc.setTrackHidden(1, true));
    EXPECT_EQ(shown(doc, 0, 0), QColor(Qt::green).rgba());
    EXPECT_EQ(topLevelCount(doc), 1);  // its layers are out of the stack, so nothing can paint them
    EXPECT_EQ(animation_ops::exportedBeat(doc.state(), 1).pixel(0, 0), QColor(Qt::green).rgba());
    EXPECT_FALSE(doc.setTrackHidden(0, true));  // the only one shown
    ASSERT_TRUE(doc.goToBeat(0));
    EXPECT_EQ(shown(doc, 0, 0), QColor(Qt::red).rgba());
    ASSERT_TRUE(doc.undo());  // the beat isn't an undo step: undo shows the track again
    EXPECT_FALSE(doc.animation().tracks[1].hidden);
    EXPECT_EQ(shown(doc, 0, 0), QColor(Qt::blue).rgba());
}

TEST(AnimationTracks, FrameCommandsActOnTheCurrentTrack) {
    Document doc(QSize(32, 32), Qt::white);
    twoTracks(doc);
    ASSERT_TRUE(doc.newFrame());  // on the second track
    EXPECT_EQ(doc.animation().tracks[1].frameCount(), 2);
    EXPECT_EQ(doc.animation().tracks[0].frameCount(), 2);
    EXPECT_EQ(doc.frameCount(), 2);
    ASSERT_TRUE(doc.changeHold({0}, +2));
    EXPECT_EQ(doc.animation().tracks[1].holdOf(0), 3);
    EXPECT_EQ(doc.animation().tracks[0].holdOf(0), 1);
    // Going to a frame of the first track makes it current, with its layer.
    ASSERT_TRUE(doc.goToCell(0, 0));
    EXPECT_EQ(doc.currentTrack(), 0);
    EXPECT_EQ(doc.animation().trackOfLayer(doc.currentLayerId()), 0);
    EXPECT_EQ(doc.beat(), 0);
    ASSERT_TRUE(doc.deleteFrames({1}));
    EXPECT_EQ(doc.animation().tracks[0].frameCount(), 1);
    EXPECT_FALSE(doc.deleteFrames({0}));  // a track keeps a frame
}

TEST(AnimationTracks, ANewLayerJoinsTheTrackOfTheLayerUnderIt) {
    Document doc(QSize(32, 32), Qt::white);
    twoTracks(doc);
    const LayerId upper = doc.currentLayerId();
    const LayerId lower = doc.state().layers.root().children.front().props.id;
    ASSERT_EQ(doc.animation().trackOfLayer(lower), 0);
    const auto added = doc.addLayer();
    ASSERT_TRUE(added);
    EXPECT_EQ(doc.animation().trackOfLayer(added.value()), 1);
    // Choosing the lower track's layer chooses its track; a layer added over it joins that track and stays below the upper's.
    doc.setCurrentLayer(lower);
    EXPECT_EQ(doc.currentTrack(), 0);
    const auto second = doc.addLayer();
    ASSERT_TRUE(second);
    EXPECT_EQ(doc.animation().trackOfLayer(second.value()), 0);
    const auto& kids = doc.state().layers.root().children;
    ASSERT_EQ(kids.size(), 4u);
    EXPECT_EQ(kids[0].props.id, lower);
    EXPECT_EQ(kids[1].props.id, second.value());
    EXPECT_EQ(kids[2].props.id, upper);
}

TEST(AnimationTracks, ClearMoveRenameAndDeleteTracks) {
    Document doc(QSize(32, 32), Qt::white);
    twoTracks(doc);
    ASSERT_TRUE(doc.clearTrack(0));
    const AnimationTrack& first = doc.animation().tracks[0];
    EXPECT_EQ(first.frameCount(), 1);
    EXPECT_EQ(first.holdOf(0), 1);  // every frame goes and no hold is left
    EXPECT_FALSE(animation_ops::frameHasDrawing(doc.state(), 0, 0));
    EXPECT_EQ(shown(doc, 20, 20), QColor(Qt::white).rgba());
    EXPECT_FALSE(doc.clearTrack(0));  // empty already
    ASSERT_TRUE(doc.goToCell(0, 0));
    ASSERT_TRUE(doc.setFramesHold({0}, 4));
    ASSERT_TRUE(doc.clearTrack(0));  // an empty frame held for beats: the hold goes too
    EXPECT_EQ(doc.animation().tracks[0].holdOf(0), 1);
    ASSERT_TRUE(doc.goToCell(1, 0));
    ASSERT_TRUE(doc.renameTrack(1, QStringLiteral("  Character ")));
    EXPECT_EQ(doc.animation().tracks[1].name, QStringLiteral("Character"));
    ASSERT_TRUE(doc.moveTrack(1, 0));
    EXPECT_EQ(doc.animation().tracks[0].name, QStringLiteral("Character"));
    EXPECT_EQ(doc.currentTrack(), 0);  // the current track moved with it
    EXPECT_EQ(doc.animation().trackOfLayer(doc.state().layers.root().children.front().props.id), 0);
    ASSERT_TRUE(doc.deleteTrack(1));
    EXPECT_EQ(doc.animation().trackCount(), 1);
    EXPECT_FALSE(doc.deleteTrack(0));
    EXPECT_EQ(shown(doc, 0, 0), QColor(Qt::blue).rgba());
}

TEST(AnimationTracks, UndoingAStrokeOnAnotherTrackGoesBackToIt) {
    Document doc(QSize(32, 32), Qt::white);
    twoTracks(doc);
    ASSERT_TRUE(doc.newFrame());  // the blue frame of the second track is set aside
    ASSERT_TRUE(doc.goToCell(0, 0));
    ASSERT_TRUE(doc.undo());  // the new frame
    ASSERT_TRUE(doc.undo());  // the blue stroke: its track and frame are shown again
    EXPECT_EQ(doc.currentTrack(), 1);
    EXPECT_EQ(shown(doc, 0, 0), QColor(Qt::green).rgba());
    ASSERT_TRUE(doc.redo());
    EXPECT_EQ(shown(doc, 0, 0), QColor(Qt::blue).rgba());
}

TEST(AnimationFile, TracksSurviveSavingAndAOneRowFileOpensAsOneTrack) {
    Document doc(QSize(32, 32), Qt::white);
    twoTracks(doc);
    ASSERT_TRUE(doc.renameTrack(1, QStringLiteral("Ball")));
    SoundClip clip = oneSecond("a.wav");
    const int id = doc.addSound(clip).value();
    ASSERT_TRUE(doc.setSoundMuted(id, true));
    const int soundTrack = doc.animation().indexOfTrack(clipOf(doc, id).track);
    ASSERT_TRUE(doc.setTrackHidden(soundTrack, true));
    ASSERT_TRUE(doc.setTrackLocked(soundTrack, true));
    const QByteArray bytes = saved(doc.content());
    EXPECT_EQ(quint8(bytes[8]), DoodleFormat::kAnimationVersion);
    auto loaded = parse(bytes);
    ASSERT_TRUE(loaded) << loaded.error().detail.toStdString();
    Document back(loaded.value().content);
    const Animation& a = back.animation();
    ASSERT_EQ(a.trackCount(), 3);
    EXPECT_EQ(a.tracks[2].name, QStringLiteral("Ball"));
    EXPECT_EQ(a.tracks[0].kind, TrackKind::Sound);  // imported at the bottom
    EXPECT_TRUE(a.tracks[0].hidden);
    EXPECT_TRUE(a.tracks[0].locked);
    EXPECT_EQ(a.tracks[1].frameCount(), 2);
    EXPECT_EQ(back.currentTrack(), 2);
    EXPECT_EQ(back.beat(), 1);
    EXPECT_TRUE(a.sounds.front().muted);
    EXPECT_EQ(a.sounds.front().track, a.tracks[0].id);
    EXPECT_EQ(back.projection().pixel(0, 0), QColor(Qt::blue).rgba());
    EXPECT_EQ(back.projection().pixel(20, 20), QColor(Qt::green).rgba());
    ASSERT_TRUE(back.goToBeat(0));
    EXPECT_EQ(back.projection().pixel(20, 20), QColor(Qt::red).rgba());

    // The same frames as version 20 wrote them, before tracks: one row of frames and the current one.
    Document one(QSize(32, 32), Qt::white);
    paint(one, QRect(0, 0, 32, 32), Qt::red);
    ASSERT_TRUE(one.newFrame());
    paint(one, QRect(0, 0, 32, 32), Qt::green);
    QBuffer in;
    in.setData(saved(one.content()));
    in.open(QIODevice::ReadOnly);
    QBuffer out;
    out.open(QIODevice::WriteOnly);
    QByteArray head = in.read(12);
    head[8] = static_cast<char>(DoodleFormat::kOneTrackVersion);
    out.write(head);
    for (auto chunk = doodle_file::readChunk(in); chunk; chunk = doodle_file::readChunk(in)) {
        QByteArray payload = chunk.value().payload;
        if (chunk.value().is("MNFT")) {
            QJsonObject root = QJsonDocument::fromJson(qUncompress(payload)).object();
            QJsonObject animation = root.value(QStringLiteral("animation")).toObject();
            const QJsonObject track = animation.value(QStringLiteral("tracks")).toArray().first().toObject();
            animation[QStringLiteral("frames")] = track.value(QStringLiteral("frames"));
            animation[QStringLiteral("current")] = track.value(QStringLiteral("current"));
            for (const char* key : {"tracks", "currentTrack", "beat", "nextTrackId"})
                animation.remove(QString::fromLatin1(key));
            root[QStringLiteral("animation")] = animation;
            payload = qCompress(QJsonDocument(root).toJson(QJsonDocument::Compact));
        }
        ASSERT_TRUE(doodle_file::writeChunk(out, chunk.value().type.data(), payload));
        if (chunk.value().is("END "))
            break;
    }
    auto old = parse(out.data());
    ASSERT_TRUE(old) << old.error().detail.toStdString();
    Document opened(old.value().content);
    ASSERT_EQ(opened.animation().trackCount(), 1);
    EXPECT_EQ(opened.frameCount(), 2);
    EXPECT_EQ(opened.currentFrame(), 1);
    EXPECT_EQ(opened.animation().trackOfLayer(opened.currentLayerId()), 0);
    EXPECT_EQ(opened.projection().pixel(4, 4), QColor(Qt::green).rgba());
    ASSERT_TRUE(opened.goToFrame(0));
    EXPECT_EQ(opened.projection().pixel(4, 4), QColor(Qt::red).rgba());
}

namespace {

// A saved file as an older version wrote it: `change` edits the manifest's "animation" object.
QByteArray asVersion(const QByteArray& file, int version, const std::function<void(QJsonObject&)>& change) {
    QBuffer in;
    in.setData(file);
    in.open(QIODevice::ReadOnly);
    QBuffer out;
    out.open(QIODevice::WriteOnly);
    QByteArray head = in.read(12);
    head[8] = static_cast<char>(version);
    out.write(head);
    for (auto chunk = doodle_file::readChunk(in); chunk; chunk = doodle_file::readChunk(in)) {
        QByteArray payload = chunk.value().payload;
        if (chunk.value().is("MNFT")) {
            QJsonObject root = QJsonDocument::fromJson(qUncompress(payload)).object();
            QJsonObject animation = root.value(QStringLiteral("animation")).toObject();
            change(animation);
            root[QStringLiteral("animation")] = animation;
            payload = qCompress(QJsonDocument(root).toJson(QJsonDocument::Compact));
        }
        EXPECT_TRUE(doodle_file::writeChunk(out, chunk.value().type.data(), payload));
        if (chunk.value().is("END "))
            break;
    }
    return out.data();
}

}

// Version 21 kept clips in rows of one sound lane and the video in a row of its own; they open as tracks.
TEST(AnimationFile, AVersion21LaneOfSoundsOpensAsSoundTracks) {
    Document doc(QSize(32, 32), Qt::white);
    ASSERT_TRUE(doc.newFrame());
    ASSERT_TRUE(doc.addSound(oneSecond("a.wav")));
    const QByteArray old = asVersion(saved(doc.content()), DoodleFormat::kFramesTracksVersion, [](QJsonObject& a) {
        QJsonArray tracks;
        for (const auto& t : a.value(QStringLiteral("tracks")).toArray())
            if (t.toObject().value(QStringLiteral("kind")).toString() == QLatin1String("frames"))
                tracks.append(t);
        a[QStringLiteral("tracks")] = tracks;
        a[QStringLiteral("currentTrack")] = 0;  // among the frames tracks, the only kind version 21 had
        QJsonArray sounds;
        for (const auto& v : a.value(QStringLiteral("sounds")).toArray()) {
            QJsonObject clip = v.toObject();
            clip.remove(QStringLiteral("track"));
            clip[QStringLiteral("row")] = 0;
            sounds.append(clip);
        }
        a[QStringLiteral("sounds")] = sounds;
        a[QStringLiteral("soundsHidden")] = true;
    });
    auto loaded = parse(old);
    ASSERT_TRUE(loaded) << loaded.error().detail.toStdString();
    Document back(loaded.value().content);
    const Animation& a = back.animation();
    ASSERT_EQ(a.trackCount(), 2);
    EXPECT_EQ(a.tracks[1].kind, TrackKind::Sound);
    EXPECT_TRUE(a.tracks[1].hidden);
    EXPECT_EQ(a.sounds.front().track, a.tracks[1].id);
    EXPECT_EQ(back.frameCount(), 2);
}

TEST(AnimationTracks, ALockedTrackKeepsItsFramesAndClips) {
    Document doc(QSize(32, 32), Qt::white);
    twoTracks(doc);
    const int clip = doc.addSound(oneSecond("a.wav")).value();
    const int soundTrack = doc.animation().indexOfTrack(clipOf(doc, clip).track);
    const int top = doc.currentTrack();  // the frames tracks are over the sound track
    ASSERT_TRUE(doc.setTrackLocked(top, true));
    ASSERT_TRUE(doc.setTrackLocked(soundTrack, true));
    EXPECT_FALSE(doc.newFrame());
    EXPECT_FALSE(doc.changeHold({0}, 1));
    EXPECT_FALSE(doc.clearTrack(top));
    EXPECT_FALSE(doc.deleteTrack(top));
    EXPECT_FALSE(doc.paintTarget(doc.currentLayerId()));
    EXPECT_FALSE(doc.moveSound(clip, 20, clipOf(doc, clip).track));
    EXPECT_FALSE(doc.removeSound(clip));
    EXPECT_FALSE(doc.removeSounds({clip}));
    EXPECT_TRUE(doc.setTrackHidden(top, true));  // hiding still works
    ASSERT_TRUE(doc.goToCell(top - 1, 0));
    EXPECT_TRUE(doc.newFrame());  // the other track isn't locked
}

namespace {

// Two seconds at 12 pictures a second, from beat 3.
VideoReference twoSecondVideo() {
    VideoReference v;
    v.path = QStringLiteral("/videos/walk.mp4");
    v.sourceSize = QSize(640, 360);
    v.seconds = 2.0;
    v.fps = 12;
    v.startBeat = 3;
    return v;
}

// The first `count` frames' pictures (all of them for 0).
std::vector<int> pictures(const AnimationTrack& t, std::size_t count = 0) {
    std::vector<int> out;
    for (const AnimationFrame& f : t.frames)
        if (count == 0 || out.size() < count)
            out.push_back(f.picture);
    return out;
}

const AnimationTrack& videoTrackOf(const Document& doc) {
    return doc.animation().tracks[static_cast<std::size_t>(doc.animation().videoTrack())];
}

}

// Feedback: "I cant seem to move or edit individual video frames, they are unselectable. They should act
// like canvases." The video's pictures are the frames of its track: chosen, moved, held, duplicated and deleted like any.
TEST(AnimationVideoFrames, TheVideosPicturesAreTheFramesOfItsTrack) {
    Document doc(QSize(32, 32), Qt::white);
    ASSERT_TRUE(doc.setVideo(twoSecondVideo(), false));
    const auto t = [&]() -> const AnimationTrack& { return videoTrackOf(doc); };
    ASSERT_EQ(t().frameCount(), 25);  // an empty frame held to where it starts, then a picture each
    EXPECT_EQ(t().frames[0].picture, -1);
    EXPECT_EQ(t().frames[0].hold, 3);
    EXPECT_EQ(t().frames[1].picture, 0);
    EXPECT_EQ(t().frames[24].picture, 23);
    EXPECT_EQ(doc.animation().videoPictureAt(2), -1);
    EXPECT_EQ(doc.animation().videoPictureAt(3), 0);
    EXPECT_EQ(doc.animation().totalBeats(), 27);  // the video's track counts for the length
    for (std::size_t i = 1; i < t().frames.size(); ++i)
        EXPECT_TRUE(t().frames[i].layers.empty());  // none painted on: no layers kept (the first is in the stack)

    const int video = doc.animation().videoTrack();
    ASSERT_TRUE(doc.goToCell(video, 1));
    EXPECT_EQ(doc.currentTrack(), video);
    ASSERT_TRUE(doc.moveFrames({1, 2}, 3));  // pictures 0 and 1 after picture 3
    EXPECT_EQ(pictures(t(), 7), (std::vector<int>{-1, 2, 3, 0, 1, 4, 5}));
    ASSERT_TRUE(doc.setFramesHold({1}, 4));
    EXPECT_EQ(doc.animation().videoPictureAt(6), 2);
    ASSERT_TRUE(doc.duplicateFrames({2}));
    EXPECT_EQ(t().frames[3].picture, 3);
    ASSERT_TRUE(doc.deleteFrames({2, 3}));
    EXPECT_EQ(pictures(t(), 4), (std::vector<int>{-1, 2, 0, 1}));
    // Del empties a video frame: its picture goes, the frame and its hold stay.
    ASSERT_TRUE(doc.clearFrames({1}));
    EXPECT_EQ(t().frames[1].picture, -1);
    EXPECT_EQ(t().frames[1].hold, 4);
    // The opacity keeps the frames; another rate makes them again.
    ASSERT_TRUE(doc.setVideoOpacity(80));
    EXPECT_EQ(t().frames[1].picture, -1);
    VideoReference faster = twoSecondVideo();
    faster.fps = 24;
    ASSERT_TRUE(doc.setVideo(faster, false));
    EXPECT_EQ(videoTrackOf(doc).frameCount(), 49);
    ASSERT_TRUE(doc.removeVideo());
    EXPECT_EQ(doc.animation().videoTrack(), -1);
    EXPECT_EQ(doc.animation().totalBeats(), 1);
}

// Painting a video frame paints a layer of its own, over its picture; one never painted on keeps no layer.
TEST(AnimationVideoFrames, AVideoFrameIsPaintedOnOverItsPicture) {
    Document doc(QSize(32, 32), Qt::white);
    ASSERT_TRUE(doc.setVideo(twoSecondVideo(), false));
    const int video = doc.animation().videoTrack();
    const int drawn = doc.animation().currentTrack;
    ASSERT_TRUE(doc.goToCell(video, 5));
    EXPECT_EQ(doc.animation().trackOfLayer(doc.currentLayerId()), video);
    paint(doc, QRect(0, 0, 8, 8), Qt::red);
    EXPECT_TRUE(animation_ops::frameHasDrawing(doc.state(), video, 5));
    // The picture is drawn under the frame's drawing, over the lower tracks.
    LayerNode picture = animation_ops::videoNode(QImage(32, 32, QImage::Format_RGBA8888_Premultiplied), 40);
    const LayerStack shown = animation_ops::displayStack(doc.state(), picture, {});
    const auto& kids = shown.root().children;
    const auto at = [&](LayerId id) {
        return std::ranges::find_if(kids, [&](const LayerNode& n) { return n.props.id == id; }) - kids.begin();
    };
    const auto videoAt =
        std::ranges::find_if(kids, [](const LayerNode& n) { return n.props.name == QLatin1String("video"); }) - kids.begin();
    EXPECT_LT(videoAt, at(doc.currentLayerId()));
    ASSERT_TRUE(doc.goToCell(video, 6));
    ASSERT_TRUE(doc.goToCell(drawn, 0));
    EXPECT_EQ(videoTrackOf(doc).frames[5].layers.size(), 1u);  // painted: kept
    EXPECT_TRUE(videoTrackOf(doc).frames[6].layers.empty());   // only visited: let go
    EXPECT_EQ(doc.projection().pixel(4, 4), 0xffffffffu);
    ASSERT_TRUE(doc.goToBeat(videoTrackOf(doc).startBeat(5)));
    EXPECT_EQ(doc.projection().pixel(4, 4), QColor(Qt::red).rgba());  // shown with the frames under it
}

// Feedback: "It works in a single track, but I should be able to move them from all tracks."
TEST(AnimationTracks, FramesOfSeveralTracksMoveTogetherAsOneStep) {
    Document doc(QSize(32, 32), Qt::white);
    twoTracks(doc);  // the lower track: red, green; the upper: blue
    ASSERT_TRUE(doc.newFrame());
    const Animation& a = doc.animation();
    const int lower = 0, upper = 1;
    const int red = a.tracks[lower].frames[0].id, blue = a.tracks[upper].frames[0].id;
    const int steps = doc.undoStack()->count();
    ASSERT_TRUE(doc.moveFrameGroups({{lower, {0}, 1}, {upper, {0}, 1}}));
    EXPECT_EQ(a.tracks[lower].frames[1].id, red);
    EXPECT_EQ(a.tracks[upper].frames[1].id, blue);
    EXPECT_EQ(doc.undoStack()->count(), steps + 1);
    EXPECT_EQ(a.tracks[static_cast<std::size_t>(a.currentTrack)].frameAtBeat(a.beat), 0);  // the playhead stays on its frame
    EXPECT_EQ(doc.moveFrameGroups({{lower, {1}, 1}, {upper, {1}, 1}}).error(), EditError::NoChange);
    ASSERT_TRUE(doc.setTrackLocked(upper, true));
    EXPECT_EQ(doc.moveFrameGroups({{lower, {1}, 0}, {upper, {1}, 0}}).error(), EditError::TrackLocked);
    EXPECT_EQ(a.tracks[lower].frames[1].id, red);  // neither moved
    ASSERT_TRUE(doc.undo());
    ASSERT_TRUE(doc.undo());
    EXPECT_EQ(a.tracks[lower].frames[0].id, red);
    EXPECT_EQ(a.tracks[upper].frames[0].id, blue);
}

TEST(AnimationFile, VideoFramesKeepTheirPicturesAndDrawings) {
    Document doc(QSize(32, 32), Qt::white);
    ASSERT_TRUE(doc.setVideo(twoSecondVideo(), false));
    const int video = doc.animation().videoTrack();
    ASSERT_TRUE(doc.goToCell(video, 3));
    ASSERT_TRUE(doc.moveFrames({3}, 1));
    paint(doc, QRect(0, 0, 8, 8), Qt::red);
    ASSERT_TRUE(doc.goToCell(doc.animation().videoTrack() == 0 ? 1 : 0, 0));
    auto loaded = parse(saved(doc.content()));
    ASSERT_TRUE(loaded) << loaded.error().detail.toStdString();
    Document back(loaded.value().content);
    const AnimationTrack& t = videoTrackOf(back);
    EXPECT_EQ(pictures(t), pictures(videoTrackOf(doc)));
    EXPECT_EQ(t.frames[1].picture, 2);
    EXPECT_TRUE(animation_ops::frameHasDrawing(back.state(), back.animation().videoTrack(), 1));
}

// Files from before the video's pictures were frames: its track had none, and gets them as it opens.
TEST(AnimationFile, AVideoTrackWithoutFramesGetsThemAsItOpens) {
    Document doc(QSize(32, 32), Qt::white);
    ASSERT_TRUE(doc.newFrame());
    ASSERT_TRUE(doc.setVideo(twoSecondVideo(), false));
    ASSERT_TRUE(doc.setTrackHidden(doc.animation().videoTrack(), true));  // no frame of it in the stack, as such files had
    const QByteArray old = asVersion(saved(doc.content()), DoodleFormat::kAnimationVersion, [](QJsonObject& a) {
        QJsonArray tracks;
        for (const auto& v : a.value(QStringLiteral("tracks")).toArray()) {
            QJsonObject t = v.toObject();
            if (t.value(QStringLiteral("kind")).toString() == QLatin1String("video")) {
                t[QStringLiteral("frames")] = QJsonArray();
                t[QStringLiteral("layers")] = QJsonArray();
                t[QStringLiteral("current")] = -1;
            }
            tracks.append(t);
        }
        a[QStringLiteral("tracks")] = tracks;
    });
    auto loaded = parse(old);
    ASSERT_TRUE(loaded) << loaded.error().detail.toStdString();
    Document back(loaded.value().content);
    EXPECT_EQ(pictures(videoTrackOf(back)), pictures(videoTrackOf(doc)));
    EXPECT_EQ(back.frameCount(), 2);
}

// The timeline redesign: a sound track's volume is an undo step, kept in the file with the folded tracks; a frames track has
// none.
TEST(Animation, ASoundTracksVolumeAndTheFoldedTracksAreSaved) {
    Document doc(QSize(32, 32), Qt::white);
    ASSERT_TRUE(doc.newFrame());
    SoundClip clip;
    clip.name = QStringLiteral("a.wav");
    clip.data = std::make_shared<const QByteArray>(QByteArray("RIFF\0\0\0\0WAVE", 12));
    clip.seconds = 0.5;
    ASSERT_TRUE(doc.addSound(clip));
    const Animation& a = doc.animation();
    const int sound = static_cast<int>(std::ranges::find_if(a.tracks, [](const AnimationTrack& t) { return t.kind == TrackKind::Sound; }) -
                                       a.tracks.begin());
    const int frames = a.currentTrack;
    EXPECT_EQ(a.tracks[static_cast<std::size_t>(sound)].volume, AnimationTrack::kFullVolume);
    EXPECT_DOUBLE_EQ(a.gainOf(a.sounds.front()), 1.0);
    EXPECT_FALSE(doc.setTrackVolume(frames, 50));  // a frames track has no volume
    ASSERT_TRUE(doc.setTrackVolume(sound, 40));
    EXPECT_DOUBLE_EQ(doc.animation().gainOf(doc.animation().sounds.front()), 0.4);
    EXPECT_EQ(doc.setTrackVolume(sound, 40).error(), EditError::NoChange);
    ASSERT_TRUE(doc.setTrackVolume(sound, 500));  // clamped to full
    EXPECT_EQ(doc.animation().tracks[static_cast<std::size_t>(sound)].volume, AnimationTrack::kFullVolume);
    ASSERT_TRUE(doc.undo());
    EXPECT_EQ(doc.animation().tracks[static_cast<std::size_t>(sound)].volume, 40);
    ASSERT_TRUE(doc.setTrackLocked(sound, true));
    EXPECT_EQ(doc.setTrackVolume(sound, 10).error(), EditError::TrackLocked);
    ASSERT_TRUE(doc.setTrackLocked(sound, false));
    TimelineView view = doc.animation().view;
    view.collapsed.insert(doc.animation().tracks[static_cast<std::size_t>(sound)].id);
    doc.setTimelineView(view);

    auto loaded = parse(saved(doc.content()));
    ASSERT_TRUE(loaded) << loaded.error().detail.toStdString();
    Document back(loaded.value().content);
    EXPECT_EQ(back.animation().tracks[static_cast<std::size_t>(sound)].volume, 40);
    EXPECT_EQ(back.animation().view.collapsed, view.collapsed);
}

// Duplicate Track (the timeline's track bar): every frame's layers copied, the same pixels and holds, new ids; one undo step.
TEST(Animation, DuplicateTrackCopiesEveryFrame) {
    Document doc(QSize(32, 32), Qt::white);
    ASSERT_TRUE(doc.fillLayer(doc.currentLayerId(), Qt::red));
    ASSERT_TRUE(doc.newFrame());
    ASSERT_TRUE(doc.fillLayer(doc.currentLayerId(), Qt::blue));
    ASSERT_TRUE(doc.changeHold({1}, +2));
    const int copied = doc.duplicateTrack(0).value();
    const Animation& a = doc.animation();
    ASSERT_EQ(a.trackCount(), 2);
    EXPECT_EQ(copied, 1);
    EXPECT_EQ(a.currentTrack, 1);
    EXPECT_EQ(a.tracks[1].frameCount(), 2);
    EXPECT_EQ(a.tracks[1].holdOf(1), 3);
    EXPECT_EQ(a.tracks[1].name, a.tracks[0].name + QStringLiteral(" copy"));
    EXPECT_NE(a.tracks[1].frames[0].id, a.tracks[0].frames[0].id);
    ASSERT_TRUE(doc.goToFrame(0));
    EXPECT_EQ(qRed(doc.projection().pixel(4, 4)), 255);
    ASSERT_TRUE(doc.goToFrame(1));
    EXPECT_EQ(qBlue(doc.projection().pixel(4, 4)), 255);
    ASSERT_TRUE(doc.undo());
    EXPECT_EQ(doc.animation().trackCount(), 1);
}

// The play range and Play's speed are kept in the file; the range stays inside the animation.
TEST(Animation, ThePlayRangeAndSpeedAreSaved) {
    Document doc(QSize(32, 32), Qt::white);
    for (int i = 0; i < 5; ++i)
        ASSERT_TRUE(doc.newFrame());
    EXPECT_EQ(doc.animation().playFirst(), 0);
    EXPECT_EQ(doc.animation().playLast(), 5);
    ASSERT_TRUE(doc.setPlayRange(2, 4));
    EXPECT_EQ(doc.setPlayRange(2, 4).error(), EditError::NoChange);
    ASSERT_TRUE(doc.setPlayRange(3, 99));  // past the end: the end, following the animation
    EXPECT_EQ(doc.animation().playEnd, -1);
    ASSERT_TRUE(doc.setPlayRange(1, 4));
    TimelineView view = doc.animation().view;
    view.speed = 0.5;
    doc.setTimelineView(view);
    auto loaded = parse(saved(doc.content()));
    ASSERT_TRUE(loaded) << loaded.error().detail.toStdString();
    Document back(loaded.value().content);
    EXPECT_EQ(back.animation().playFirst(), 1);
    EXPECT_EQ(back.animation().playLast(), 4);
    EXPECT_EQ(back.animation().view.speed, 0.5);
    ASSERT_TRUE(back.deleteFrames({0, 1, 2}));  // the range shrinks with the animation
    EXPECT_LE(back.animation().playLast(), back.animation().totalBeats() - 1);
}

// A file saved by the timeline build that had tweens opens: its tracks' keyframes and pivots are let go (feedback: "Remove
// keyframes"; "dropping the keys is fine").
TEST(AnimationFile, AFileWithTheTweenBuildsKeyframesOpens) {
    Document doc(QSize(32, 32), Qt::white);
    ASSERT_TRUE(doc.newFrame());
    const QByteArray withKeys = asVersion(saved(doc.content()), DoodleFormat::kAnimationVersion, [](QJsonObject& a) {
        QJsonArray tracks = a.value(QStringLiteral("tracks")).toArray();
        QJsonObject t = tracks[0].toObject();
        t[QStringLiteral("keys")] = QJsonArray{QJsonObject{{QStringLiteral("beat"), 0},
                                                           {QStringLiteral("x"), 12.0},
                                                           {QStringLiteral("y"), 0.0},
                                                           {QStringLiteral("scale"), 1.0},
                                                           {QStringLiteral("rotation"), 30.0},
                                                           {QStringLiteral("opacity"), 50.0},
                                                           {QStringLiteral("easing"), QStringLiteral("easeIn")}}};
        t[QStringLiteral("pivot")] = QJsonArray{16.0, 16.0};
        tracks[0] = t;
        a[QStringLiteral("tracks")] = tracks;
    });
    auto loaded = parse(withKeys);
    ASSERT_TRUE(loaded) << loaded.error().detail.toStdString();
    Document back(loaded.value().content);
    EXPECT_EQ(back.frameCount(), 2);
}

// Delete with several tracks chosen is one step, refused whole when any can't go; a track's label colour is an undo step,
// kept in the file and by Duplicate Track.
TEST(Animation, SeveralTracksGoInOneStepAndTracksHaveLabelColours) {
    Document doc(QSize(32, 32), Qt::white);
    ASSERT_TRUE(doc.newFrame());
    ASSERT_TRUE(doc.addTrack());
    ASSERT_TRUE(doc.addTrack());
    ASSERT_TRUE(doc.addTrack());
    ASSERT_EQ(doc.animation().trackCount(), 4);
    const int steps = doc.undoStack()->count();
    ASSERT_TRUE(doc.deleteTracks({1, 3}));
    EXPECT_EQ(doc.animation().trackCount(), 2);
    EXPECT_EQ(doc.undoStack()->count(), steps + 1);
    EXPECT_EQ(doc.deleteTracks({0, 1}).error(), EditError::LastTrack);  // not every drawn track
    EXPECT_EQ(doc.animation().trackCount(), 2);
    ASSERT_TRUE(doc.undo());
    EXPECT_EQ(doc.animation().trackCount(), 4);

    ASSERT_TRUE(doc.setTrackLabel(1, 3));
    EXPECT_EQ(doc.animation().tracks[1].label, 3);
    EXPECT_EQ(doc.setTrackLabel(1, AnimationTrack::kLabels + 1).error(), EditError::NoSuchTrack);
    ASSERT_TRUE(doc.duplicateTrack(1));
    EXPECT_EQ(doc.animation().tracks[2].label, 3);
    auto loaded = parse(saved(doc.content()));
    ASSERT_TRUE(loaded) << loaded.error().detail.toStdString();
    Document back(loaded.value().content);
    EXPECT_EQ(back.animation().tracks[1].label, 3);
    EXPECT_EQ(back.animation().tracks[0].label, 0);
    ASSERT_TRUE(doc.undo());
    ASSERT_TRUE(doc.undo());
    EXPECT_EQ(doc.animation().tracks[1].label, 0);
}
