#include "IoTestSupport.h"
#include "FfmpegTestSupport.h"

#include "core/document/Document.h"
#include "core/io/ByteIO.h"
#include "core/io/DoodleFormat.h"
#include "core/paint/StrokeController.h"
#include "core/timelapse/Timelapse.h"
#include "core/timelapse/TimelapseExport.h"
#include "core/timelapse/TimelapseFile.h"
#include "core/timelapse/TimelapseRecorder.h"
#include "core/timelapse/TimelapseReplay.h"

#include <QBuffer>
#include <QDateTime>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QTemporaryDir>

#include <gtest/gtest.h>

#include <algorithm>
#include <random>

using namespace doodlebold;
using namespace doodlebold::testing;

namespace {

InputSample at(double x, double y) {
    InputSample s;
    s.pos = QPointF(x, y);
    return s;
}

void paintLine(Document& doc, double y, const QColor& colour = Qt::red, double x0 = 10, double x1 = 110, int size = 20) {
    StrokeController stroke(&doc);
    BrushSettings st = defaultSettings(ToolType::Pencil);
    st.size = size;
    ASSERT_TRUE(stroke.begin(at(x0, y), colour, st));
    stroke.add(at(x1, y));
    stroke.finish();
}

void paintFramed(Document& doc, double y, int segments, std::vector<QImage>* pictures, const QColor& colour = Qt::red,
                 bool cancel = false) {
    StrokeController stroke(&doc);
    BrushSettings st = defaultSettings(ToolType::Pencil);
    st.size = 12;
    ASSERT_TRUE(stroke.begin(at(5, y), colour, st));
    // The path holds back one event, and a pen sample is laid once all its stamps are ready: from these two the first frame shows
    // paint.
    stroke.add(at(7.5, y));
    stroke.add(at(10, y));
    for (int i = 1; i <= segments; ++i) {
        stroke.add(at(10 + i * 150.0 / segments, y));
        doc.captureStrokeFrame();
        if (pictures)
            pictures->push_back(doc.projection().copy());
    }
    if (cancel)
        stroke.cancel();
    else
        stroke.finish();
}

QImage shownAt(TimelapseReplay& replay, int step) {
    return replay.seek(step).convertToFormat(QImage::Format_RGBA8888_Premultiplied);
}

std::vector<TimelapseStep::Kind> kindsOf(const Timelapse& t) {
    std::vector<TimelapseStep::Kind> out;
    out.reserve(t.steps.size());
    for (const auto& s : t.steps)
        out.push_back(s->kind);
    return out;
}

using Kind = TimelapseStep::Kind;

Timelapse sequence(std::initializer_list<Kind> kinds, std::initializer_list<int> resumedAt = {},
                   std::initializer_list<int> strokeStartsAt = {}) {
    Timelapse t;
    int i = 0;
    for (const Kind k : kinds) {
        auto s = std::make_shared<TimelapseStep>();
        s->kind = i == 0 ? Kind::First : k;
        s->resumed = std::ranges::find(resumedAt, i) != resumedAt.end();
        s->strokeStart = std::ranges::find(strokeStartsAt, i) != strokeStartsAt.end();
        t.steps.push_back(std::move(s));
        ++i;
    }
    return t;
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

void expectSameRecording(const Timelapse& a, const Timelapse& b) {
    ASSERT_EQ(a.steps.size(), b.steps.size());
    EXPECT_EQ(a.paused, b.paused);
    EXPECT_EQ(a.startedMs, b.startedMs);
    for (std::size_t i = 0; i < a.steps.size(); ++i) {
        const TimelapseStep &x = *a.steps[i], &y = *b.steps[i];
        EXPECT_EQ(x.kind, y.kind) << i;
        EXPECT_EQ(x.full, y.full) << i;
        EXPECT_EQ(x.resumed, y.resumed) << i;
        EXPECT_EQ(x.strokeStart, y.strokeStart) << i;
        EXPECT_EQ(x.activeMs, y.activeMs) << i;
        EXPECT_EQ(x.size, y.size) << i;
        ASSERT_EQ(x.tiles.size(), y.tiles.size()) << i;
        for (std::size_t k = 0; k < x.tiles.size(); ++k) {
            EXPECT_EQ(x.tiles[k].x, y.tiles[k].x);
            EXPECT_EQ(x.tiles[k].y, y.tiles[k].y);
            EXPECT_EQ(x.tiles[k].blob, y.tiles[k].blob);
        }
    }
}

}

TEST(TimelapseCodec, TilesOfAnySizeRoundTripAndOneColourCostsThreeBytes) {
    std::mt19937 rng(7);
    for (const QSize s : {QSize(64, 64), QSize(64, 5), QSize(3, 64), QSize(1, 1), QSize(37, 11)}) {
        QImage img(s, QImage::Format_RGBA8888_Premultiplied);
        for (int y = 0; y < s.height(); ++y)
            for (int x = 0; x < s.width(); ++x)
                img.setPixel(x, y, qRgb(int(rng() % 256), int(rng() % 256), int(rng() % 256)));
        const QByteArray blob = encodeTimelapseTile(img.constBits(), int(img.bytesPerLine()), s.width(), s.height());
        ASSERT_TRUE(isTimelapseBlob(blob, s.width(), s.height()));
        QImage out(s, QImage::Format_RGBA8888_Premultiplied);
        ASSERT_TRUE(decodeTimelapseTile(blob, s.width(), s.height(), out.bits(), int(out.bytesPerLine())));
        EXPECT_EQ(out, img) << s.width() << "x" << s.height();
    }
    QImage flat(64, 64, QImage::Format_RGBA8888_Premultiplied);
    flat.fill(QColor(12, 200, 99));
    const QByteArray blob = encodeTimelapseTile(flat.constBits(), int(flat.bytesPerLine()), 64, 64);
    EXPECT_EQ(blob.size(), 4);
    QImage out(64, 64, QImage::Format_RGBA8888_Premultiplied);
    ASSERT_TRUE(decodeTimelapseTile(blob, 64, 64, out.bits(), int(out.bytesPerLine())));
    EXPECT_EQ(out, flat);
}

TEST(TimelapseCodec, DamagedBlobsAreRefused) {
    QImage img(64, 64, QImage::Format_RGBA8888_Premultiplied);
    for (int y = 0; y < 64; ++y)
        for (int x = 0; x < 64; ++x)
            img.setPixel(x, y, qRgb(x * 4, y * 4, (x ^ y) * 4));
    QByteArray blob = encodeTimelapseTile(img.constBits(), int(img.bytesPerLine()), 64, 64);
    QImage out(64, 64, QImage::Format_RGBA8888_Premultiplied);
    EXPECT_FALSE(isTimelapseBlob(blob, 32, 64)) << "the blob is for another tile size";
    EXPECT_FALSE(isTimelapseBlob({}, 64, 64));
    EXPECT_FALSE(isTimelapseBlob(blob.left(3), 64, 64));
    QByteArray cut = blob.left(blob.size() / 2);
    EXPECT_FALSE(decodeTimelapseTile(cut, 64, 64, out.bits(), int(out.bytesPerLine())));
    blob[0] = 9;
    EXPECT_FALSE(isTimelapseBlob(blob, 64, 64));
}

TEST(TimelapsePlan, FrameCountsOfTheTwoLengths) {
    EXPECT_EQ(timelapseFrameCount(TimelapseLength::Full, 4812), 4812) << "every picture of the path, one frame each";
    EXPECT_EQ(timelapseFrameCount(TimelapseLength::Full, 0), 1);
    EXPECT_EQ(timelapseFrameCount(TimelapseLength::Seconds30, 4812), 900) << "30 s at 30 fps";
    EXPECT_EQ(timelapseFrameCount(TimelapseLength::Seconds30, 3), 900);
    EXPECT_EQ(timelapseFrameCount(TimelapseLength::Seconds30, 3, 60), 1800);
    EXPECT_EQ(kTimelapseFps, 30);
}

TEST(TimelapsePlan, SizeIsFittedWithin1920By1080NeverUpscaledAndEven) {
    EXPECT_EQ(fitTimelapseSize(QSize(2000, 2000)), QSize(1080, 1080));
    EXPECT_EQ(fitTimelapseSize(QSize(3000, 2000)), QSize(1620, 1080));
    EXPECT_EQ(fitTimelapseSize(QSize(4000, 1000)), QSize(1920, 480));
    EXPECT_EQ(fitTimelapseSize(QSize(1000, 800)), QSize(1000, 800));
    EXPECT_EQ(fitTimelapseSize(QSize(1920, 1080)), QSize(1920, 1080));
    EXPECT_EQ(fitTimelapseSize(QSize(1001, 999)), QSize(1000, 998));
    EXPECT_EQ(fitTimelapseSize(QSize(1, 1)), QSize(2, 2));
    EXPECT_EQ(fitTimelapseSize(QSize(600, 600), QSize(300, 300)), QSize(300, 300));
}

TEST(TimelapsePlan, ResamplingKeepsTheEndsAndHoldsStepsEvenly) {
    const std::vector<int> path = {0, 4, 7, 9};
    const auto frames = resampleTimelapse(path, 12);
    ASSERT_EQ(frames.size(), 12u);
    EXPECT_EQ(frames.front(), 0);
    EXPECT_EQ(frames.back(), 9);
    EXPECT_TRUE(std::ranges::is_sorted(frames));
    for (const int step : path)
        EXPECT_NEAR(double(std::count(frames.begin(), frames.end(), step)), 3.0, 1.0) << step;

    std::vector<int> many(1000);
    for (int i = 0; i < 1000; ++i)
        many[std::size_t(i)] = i;
    const auto skipped = resampleTimelapse(many, 30);
    ASSERT_EQ(skipped.size(), 30u);
    EXPECT_EQ(skipped.front(), 0);
    EXPECT_EQ(skipped.back(), 999);
    EXPECT_TRUE(std::ranges::is_sorted(skipped));

    EXPECT_EQ(resampleTimelapse({5}, 4), (std::vector<int>{5, 5, 5, 5}));
    EXPECT_EQ(resampleTimelapse({1, 2}, 1), (std::vector<int>{2}));
    EXPECT_TRUE(resampleTimelapse({}, 4).empty());
    EXPECT_TRUE(resampleTimelapse({1, 2}, 0).empty());
}

TEST(TimelapsePlan, ThePlanOfAnExportFollowsTheChoices) {
    Document doc(QSize(2000, 1000), Qt::white);
    doc.startRecording();
    paintLine(doc, 40);
    paintLine(doc, 90, Qt::blue);
    ASSERT_TRUE(doc.undo());
    paintLine(doc, 120, Qt::green);
    const Timelapse recording = *doc.timelapse();
    TimelapseExportOptions options;
    options.length = TimelapseLength::Seconds30;
    TimelapsePlan plan = planTimelapse(recording, options);
    EXPECT_EQ(plan.frameSize, QSize(1920, 960));
    EXPECT_EQ(plan.fps, 30);
    EXPECT_EQ(plan.frames.size(), 30u * 30 + 3 * 30);
    EXPECT_EQ(plan.heldFrames, 90);
    EXPECT_EQ(plan.frames.front(), 0) << "the video starts from the first picture";
    EXPECT_EQ(plan.frames.back(), 4);
    EXPECT_EQ(plan.frames[899], 4);
    EXPECT_EQ(std::count(plan.frames.begin(), plan.frames.end(), 2), 0) << "the undone stroke is not shown";
    EXPECT_NEAR(plan.seconds(), 33.0, 1e-9);

    options.length = TimelapseLength::Full;
    options.holdSeconds = 0;
    plan = planTimelapse(recording, options);
    EXPECT_EQ(plan.frames, (std::vector<int>{0, 1, 4}));
    EXPECT_EQ(plan.heldFrames, 0);

    options.leaveOutUndone = false;
    plan = planTimelapse(recording, options);
    EXPECT_EQ(plan.frames, (std::vector<int>{0, 1, 2, 3, 4})) << "the undone stroke and the undo step are pictures";
    options.holdSeconds = 2;
    EXPECT_EQ(planTimelapse(recording, options).frames.size(), 5u + 60u);
    EXPECT_TRUE(planTimelapse(Timelapse{}, options).frames.empty());
}

TEST(TimelapsePlan, FramesAreFittedAndOtherShapesAreCentredOnBlack) {
    QImage picture(400, 200, QImage::Format_RGBA8888);
    picture.fill(Qt::red);
    EXPECT_EQ(timelapseFrame(picture, QSize(200, 100)).size(), QSize(200, 100));
    EXPECT_EQ(timelapseFrame(picture, QSize(400, 200)).pixelColor(5, 5), QColor(Qt::red));
    const QImage tall = timelapseFrame(picture, QSize(100, 200));
    ASSERT_EQ(tall.size(), QSize(100, 200));
    EXPECT_EQ(tall.pixelColor(50, 100), QColor(Qt::red));
    EXPECT_EQ(tall.pixelColor(50, 5), QColor(Qt::black));
    EXPECT_EQ(tall.pixelColor(50, 195), QColor(Qt::black));
    QImage odd(1001, 999, QImage::Format_RGBA8888);
    odd.fill(Qt::blue);
    const QImage f = timelapseFrame(odd, QSize(1000, 998));
    EXPECT_EQ(f.pixelColor(0, 0), QColor(Qt::blue));
    EXPECT_EQ(f.pixelColor(999, 997), QColor(Qt::blue));
}

TEST(TimelapseExport, WritesARealVideoOfThePlansLength) {
    const auto& ffmpeg = test::systemFfmpeg();
    if (!ffmpeg)
        GTEST_SKIP() << "no FFmpeg on this machine";
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    Document doc(QSize(160, 90), Qt::white);
    doc.startRecording();
    paintLine(doc, 20, Qt::red, 10, 150, 12);
    paintLine(doc, 60, Qt::blue, 10, 150, 12);
    const Timelapse recording = *doc.timelapse();
    TimelapseExportOptions options;
    options.length = TimelapseLength::Seconds30;
    options.holdSeconds = 3;
    const TimelapsePlan plan = planTimelapse(recording, options);
    EncodeSettings settings;
    settings.output = dir.filePath(QStringLiteral("timelapse.mp4"));
    settings.crf = 28;
    int last = 0;
    const auto result = exportTimelapse(*ffmpeg, recording, plan, settings, {}, [&](int done, int total) {
        EXPECT_EQ(total, int(plan.frames.size()));
        last = done;
        return true;
    });
    ASSERT_TRUE(result.ok) << qPrintable(result.error);
    EXPECT_EQ(last, int(plan.frames.size()));
    ASSERT_TRUE(QFileInfo(settings.output).isFile());
    const auto info = test::probeClip(*ffmpeg, settings.output);
    ASSERT_TRUE(info);
    EXPECT_EQ(info->size, QSize(160, 90));
    EXPECT_NEAR(info->fps, 30.0, 0.01);
    EXPECT_NEAR(info->duration, 33.0, 0.2);
}

TEST(TimelapseExport, ACancelledExportLeavesNoFile) {
    const auto& ffmpeg = test::systemFfmpeg();
    if (!ffmpeg)
        GTEST_SKIP() << "no FFmpeg on this machine";
    QTemporaryDir dir;
    Document doc(QSize(64, 64), Qt::white);
    doc.startRecording();
    paintLine(doc, 20, Qt::red, 5, 50, 8);
    const Timelapse recording = *doc.timelapse();
    const TimelapsePlan plan = planTimelapse(recording, {});
    EncodeSettings settings;
    settings.output = dir.filePath(QStringLiteral("cancelled.mp4"));
    const auto result = exportTimelapse(*ffmpeg, recording, plan, settings, {}, [](int done, int) { return done < 10; });
    EXPECT_TRUE(result.cancelled);
    EXPECT_FALSE(result.ok);
    EXPECT_FALSE(QFileInfo::exists(settings.output));
}

TEST(TimelapsePath, UndoneStepsAreLeftOutWithTheirUndo) {
    const Timelapse t = sequence({Kind::First, Kind::Edit, Kind::Edit, Kind::Undo, Kind::Edit});
    EXPECT_EQ(t.path(true), (std::vector<int>{0, 1, 4}));
    EXPECT_EQ(t.path(false), (std::vector<int>{0, 1, 2, 3, 4}));
}

TEST(TimelapsePath, UndoneAndRedoneStaysOnce) {
    const Timelapse t = sequence({Kind::First, Kind::Edit, Kind::Edit, Kind::Undo, Kind::Redo});
    EXPECT_EQ(t.path(true), (std::vector<int>{0, 1, 2}));
}

TEST(TimelapsePath, StepsStillUndoneAtTheEndAreLeftOut) {
    const Timelapse t = sequence({Kind::First, Kind::Edit, Kind::Edit, Kind::Undo, Kind::Undo});
    EXPECT_EQ(t.path(true), (std::vector<int>{0}));
}

TEST(TimelapsePath, ADeepUndoThenAnEditDropsWholeBranch) {
    const Timelapse t = sequence({Kind::First, Kind::Edit, Kind::Edit, Kind::Edit, Kind::Undo, Kind::Undo, Kind::Redo, Kind::Edit});
    EXPECT_EQ(t.path(true), (std::vector<int>{0, 1, 2, 7}));
}

TEST(TimelapsePath, AfterTheFileWasOpenedAgainEarlierStepsAreFinal) {
    const Timelapse t = sequence({Kind::First, Kind::Edit, Kind::Edit, Kind::Undo, Kind::Edit, Kind::Edit, Kind::Undo}, {4});
    EXPECT_EQ(t.path(true), (std::vector<int>{0, 1, 4}));
}

TEST(TimelapsePath, AFirstPictureAloneIsOneState) {
    EXPECT_EQ(sequence({Kind::First}).path(true), (std::vector<int>{0}));
    EXPECT_TRUE(Timelapse{}.path(true).empty());
}

TEST(TimelapsePath, AStrokesFramesComeWithItsStepAndGoWithIt) {
    const Timelapse t = sequence({Kind::First, Kind::Partial, Kind::Partial, Kind::Edit, Kind::Partial, Kind::Partial, Kind::Partial,
                                  Kind::Edit, Kind::Undo, Kind::Partial, Kind::Partial, Kind::Edit},
                                 {}, {1, 4, 9});
    EXPECT_EQ(t.path(true), (std::vector<int>{0, 1, 2, 3, 9, 10, 11})) << "B's frames and its undo are left out as a whole";
    EXPECT_EQ(t.path(false), (std::vector<int>{0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11})) << "with undone strokes shown they all play";
    EXPECT_EQ(t.stepCount(), 4) << "frames are not steps";
    EXPECT_EQ(t.editCount(), 3);
    EXPECT_EQ(t.frameCount(), 12);
}

TEST(TimelapsePath, AStrokeUndoneAndRedoneKeepsAllItsFramesOnce) {
    const Timelapse t = sequence({Kind::First, Kind::Partial, Kind::Partial, Kind::Edit, Kind::Undo, Kind::Redo}, {}, {1});
    EXPECT_EQ(t.path(true), (std::vector<int>{0, 1, 2, 3}));
}

TEST(TimelapsePath, FramesOfAStrokeThatNeverBecameAStepAreNotShown) {
    const Timelapse t =
        sequence({Kind::First, Kind::Partial, Kind::Partial, Kind::Partial, Kind::Partial, Kind::Edit, Kind::Partial}, {}, {1, 3, 6});
    EXPECT_EQ(t.path(true), (std::vector<int>{0, 3, 4, 5}));
    EXPECT_EQ(t.path(false), (std::vector<int>{0, 3, 4, 5}));
    EXPECT_EQ(sequence({Kind::First, Kind::Partial, Kind::Undo}, {}, {1}).path(false), (std::vector<int>{0, 2}));
}

TEST(TimelapseRecording, AStrokeIsRecordedAsFramesAndItsStep) {
    Document doc(QSize(200, 150), Qt::white);
    doc.startRecording();
    std::vector<QImage> pictures = {doc.projection().copy()};
    paintFramed(doc, 40, 4, &pictures);
    pictures.push_back(doc.projection().copy());
    const auto recording = doc.timelapse();
    EXPECT_EQ(kindsOf(*recording),
              (std::vector<Kind>{Kind::First, Kind::Partial, Kind::Partial, Kind::Partial, Kind::Partial, Kind::Edit}));
    EXPECT_TRUE(recording->steps[1]->strokeStart);
    EXPECT_FALSE(recording->steps[2]->strokeStart);
    EXPECT_EQ(doc.recordedSteps(), 1) << "the frames are not steps";
    EXPECT_EQ(recording->frameCount(), 6);
    EXPECT_LT(recording->steps[2]->tiles.size(), 12u);
    TimelapseReplay replay(*recording);
    for (const int step : {0, 1, 2, 3, 4, 5, 3, 1, 5, 0})
        EXPECT_EQ(shownAt(replay, step), pictures[std::size_t(step)]) << "step " << step;
    EXPECT_EQ(recording->path(true), (std::vector<int>{0, 1, 2, 3, 4, 5}));
}

TEST(TimelapseRecording, TheStepAtPenLiftStoresWhatTheLastFrameMissed) {
    Document doc(QSize(200, 150), Qt::white);
    doc.startRecording();
    {
        StrokeController stroke(&doc);
        BrushSettings st = defaultSettings(ToolType::Pencil);
        st.size = 12;
        ASSERT_TRUE(stroke.begin(at(10, 40), Qt::red, st));
        stroke.add(at(30, 40));  // the path holds back one event, and a sample is laid once its stamps are ready
        stroke.add(at(45, 40));
        stroke.add(at(60, 40));
        doc.captureStrokeFrame();
        stroke.add(at(150, 40));
        stroke.finish();
    }
    const auto recording = doc.timelapse();
    EXPECT_EQ(kindsOf(*recording), (std::vector<Kind>{Kind::First, Kind::Partial, Kind::Edit}));
    EXPECT_FALSE(recording->steps[2]->tiles.empty());
    TimelapseReplay replay(*recording);
    EXPECT_EQ(shownAt(replay, 2), doc.projection());
    doc.captureStrokeFrame();
    EXPECT_EQ(doc.timelapse()->frameCount(), 3) << "no stroke in progress: no frame";
}

// Zoomed out (at about 41 %, view level 1), a stroke shows as the view's reduced blocks and its layer's tiles
// wait for the stroke's end. Its frames are those blocks enlarged: the stroke grows frame by frame, a little soft, and the
// step at pen lift stores it exactly (before, the frames found nothing changed and the stroke appeared whole).
TEST(TimelapseRecording, ZoomedOutAStrokesFramesAreTheViewsPictureEnlarged) {
    for (const int level : {1, 2}) {
        Document doc(QSize(640, 256), Qt::white);
        doc.startRecording();
        constexpr int kSegments = 6;
        {
            // As the canvas paints: on the stroke's own thread, its frames shown by the view's paint at the view's level.
            StrokeController stroke(&doc);
            stroke.paintInBackground([] {}, [&] { stroke.showPaint(); });
            BrushSettings st = defaultSettings(ToolType::Pencil);
            st.size = 400;  // 40 px
            ASSERT_TRUE(stroke.begin(at(20, 128), Qt::red, st));
            stroke.setViewLevel(level);
            stroke.add(at(25, 128));
            stroke.add(at(30, 128));
            for (int i = 1; i <= kSegments; ++i) {
                stroke.add(at(30 + i * 580.0 / kSegments, 128));
                stroke.waitUntilCaughtUp();
                stroke.showPaint();
                doc.captureStrokeFrame();
            }
            stroke.finishInBackground();
            while (stroke.isFinishing() && !stroke.isPainted())
                stroke.showPaint();
            stroke.showPaint();
            stroke.finish();
        }
        const auto recording = doc.timelapse();
        const auto kinds = kindsOf(*recording);
        ASSERT_EQ(kinds.back(), Kind::Edit);
        const auto partials = std::count(kinds.begin(), kinds.end(), Kind::Partial);
        EXPECT_GE(partials, kSegments - 1) << "level " << level;
        TimelapseReplay replay(*recording);
        const QImage final = doc.projection();
        EXPECT_EQ(shownAt(replay, int(kinds.size()) - 1), final) << "the stroke's own step is exact";
        // Each frame shows more of the stroke, and where it shows it, it looks like the stroke: red along the line.
        int reached = -1;  // the first frame may hold only the first dab
        for (int step = 1; step < int(kinds.size()) - 1; ++step) {
            const QImage frame = shownAt(replay, step);
            int right = 0;
            for (int x = 0; x < frame.width(); ++x)
                if (qRed(frame.pixel(x, 128)) > 200 && qGreen(frame.pixel(x, 128)) < 60)
                    right = x;
            EXPECT_GT(right, reached) << "level " << level << ", frame " << step;
            reached = right;
            EXPECT_NE(frame, final) << "an in-between frame";
        }
        EXPECT_GT(reached, 300) << "the frames follow the stroke across";
        // Soft, not wrong: away from the stroke's edge the enlarged frame is the canvas's colour.
        const QImage last = shownAt(replay, int(kinds.size()) - 2);
        EXPECT_EQ(last.pixel(100, 128), final.pixel(100, 128));
        EXPECT_EQ(last.pixel(100, 30), final.pixel(100, 30));
    }
}

TEST(TimelapseRecording, UndoingAStrokeLeavesOutAllItsFramesAndRedoBringsThemBack) {
    Document doc(QSize(200, 150), Qt::white);
    doc.startRecording();
    paintFramed(doc, 30, 3, nullptr);                // frames 1 2 3, step 4
    paintFramed(doc, 90, 5, nullptr, Qt::blue);      // frames 5 … 9, step 10
    ASSERT_TRUE(doc.undo());                         // 11
    auto recording = doc.timelapse();
    ASSERT_EQ(recording->frameCount(), 12);
    EXPECT_EQ(recording->path(true), (std::vector<int>{0, 1, 2, 3, 4}));
    EXPECT_EQ(recording->path(false).size(), 12u) << "shown, they all play";
    ASSERT_TRUE(doc.redo());                         // 12
    recording = doc.timelapse();
    EXPECT_EQ(recording->path(true), (std::vector<int>{0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10}));
    ASSERT_TRUE(doc.undo());                         // 13
    paintFramed(doc, 120, 2, nullptr, Qt::green);    // frames 14 15, step 16
    recording = doc.timelapse();
    EXPECT_EQ(recording->path(true), (std::vector<int>{0, 1, 2, 3, 4, 14, 15, 16}))
        << "a new stroke after the undo drops the undone one for good";
    EXPECT_EQ(recording->stepCount(), 6);
}

TEST(TimelapseRecording, ACancelledStrokeKeepsNoFramesAndTheNextStepRestoresItsTiles) {
    Document doc(QSize(200, 150), Qt::white);
    doc.startRecording();
    paintFramed(doc, 30, 2, nullptr);  // frames 1 2, step 3
    const QImage before = doc.projection().copy();
    paintFramed(doc, 100, 4, nullptr, Qt::blue, true);
    EXPECT_EQ(doc.projection(), before) << "the cancelled stroke left no trace";
    auto recording = doc.timelapse();
    EXPECT_EQ(kindsOf(*recording), (std::vector<Kind>{Kind::First, Kind::Partial, Kind::Partial, Kind::Edit}))
        << "the cancelled stroke's frames are dropped";
    EXPECT_EQ(doc.recordedSteps(), 1);
    paintFramed(doc, 135, 2, nullptr, Qt::green);
    recording = doc.timelapse();
    ASSERT_EQ(recording->frameCount(), 7);
    TimelapseReplay replay(*recording);
    EXPECT_EQ(shownAt(replay, 6), doc.projection()) << "the blue stroke is not in the recording's last picture";
    EXPECT_EQ(shownAt(replay, 3), before);
    EXPECT_EQ(recording->path(true), (std::vector<int>{0, 1, 2, 3, 4, 5, 6}));
    paintFramed(doc, 60, 2, nullptr, Qt::blue, true);
    ASSERT_TRUE(doc.addLayer());
    EXPECT_EQ(doc.timelapse()->frameCount(), 7);
}

TEST(TimelapseRecording, NothingIsFramedWhilePausedOrWithoutARecording) {
    Document plain(QSize(100, 100), Qt::white);
    paintFramed(plain, 30, 2, nullptr);
    EXPECT_EQ(plain.timelapse(), nullptr);
    Document doc(QSize(200, 100), Qt::white);
    doc.startRecording();
    doc.setRecordingPaused(true);
    paintFramed(doc, 30, 3, nullptr);
    EXPECT_EQ(doc.timelapse()->frameCount(), 1);
}

TEST(TimelapseRecording, AStrokeUndoAndRedoAreSteps) {
    Document doc(QSize(200, 150), Qt::white);
    doc.startRecording();
    EXPECT_TRUE(doc.isRecording());
    EXPECT_EQ(doc.recordedSteps(), 0);
    std::vector<QImage> pictures = {doc.projection().copy()};

    paintLine(doc, 40);
    pictures.push_back(doc.projection().copy());
    ASSERT_TRUE(doc.undo());
    pictures.push_back(doc.projection().copy());
    ASSERT_TRUE(doc.redo());
    pictures.push_back(doc.projection().copy());
    paintLine(doc, 100, Qt::blue);
    pictures.push_back(doc.projection().copy());

    EXPECT_EQ(doc.recordedSteps(), 4);
    const auto recording = doc.timelapse();
    ASSERT_TRUE(recording);
    EXPECT_EQ(kindsOf(*recording), (std::vector<Kind>{Kind::First, Kind::Edit, Kind::Undo, Kind::Redo, Kind::Edit}));
    EXPECT_TRUE(recording->steps[0]->full);
    EXPECT_FALSE(recording->steps[1]->full);
    EXPECT_FALSE(recording->steps[1]->resumed);

    TimelapseReplay replay(*recording);
    for (const int step : {0, 1, 2, 3, 4, 2, 4, 0, 3, 1})
        EXPECT_EQ(shownAt(replay, step), pictures[std::size_t(step)]) << "step " << step;
    EXPECT_LT(recording->steps[1]->tiles.size(), 12u);
    EXPECT_EQ(recording->steps[0]->tiles.size(), 4u * 3u);
    EXPECT_EQ(recording->path(true), (std::vector<int>{0, 1, 4}));
}

TEST(TimelapseRecording, OnlyActionsThatChangeThePictureMakeSteps) {
    Document doc(QSize(100, 100), Qt::white);
    doc.startRecording();
    ASSERT_TRUE(doc.selectAll());
    ASSERT_TRUE(doc.addLayer());
    EXPECT_EQ(doc.recordedSteps(), 0);
    ASSERT_TRUE(doc.fillLayer(doc.currentLayerId(), Qt::green));
    EXPECT_EQ(doc.recordedSteps(), 1);
    ASSERT_TRUE(doc.undo());
    EXPECT_EQ(doc.recordedSteps(), 2);
    ASSERT_TRUE(doc.undo());
    ASSERT_TRUE(doc.undo());
    EXPECT_EQ(doc.recordedSteps(), 2);
    const auto recording = doc.timelapse();
    EXPECT_EQ(kindsOf(*recording), (std::vector<Kind>{Kind::First, Kind::Edit, Kind::Undo}));
}

TEST(TimelapseRecording, ACancelledStrokeAndAnUnchangedPictureMakeNoStep) {
    Document doc(QSize(100, 100), Qt::white);
    doc.startRecording();
    {
        StrokeController stroke(&doc);
        BrushSettings st = defaultSettings(ToolType::Pencil);
        st.size = 20;
        ASSERT_TRUE(stroke.begin(at(10, 10), Qt::red, st));
        stroke.add(at(80, 80));
        stroke.cancel();
    }
    EXPECT_EQ(doc.recordedSteps(), 0);
    paintLine(doc, 90, Qt::blue, 10, 30, 6);
    ASSERT_EQ(doc.recordedSteps(), 1);
    EXPECT_LE(doc.timelapse()->steps[1]->tiles.size(), 4u);
    paintLine(doc, 92, Qt::green, 12, 30, 6);
    ASSERT_EQ(doc.recordedSteps(), 2);
    EXPECT_EQ(doc.timelapse()->steps[2]->tiles.size(), 1u);
}

TEST(TimelapseRecording, ChangingTheCanvasSizeStoresTheWholeNewPicture) {
    Document doc(QSize(130, 70), Qt::white);
    doc.startRecording();
    paintLine(doc, 30, Qt::red, 5, 100, 10);
    const QImage before = doc.projection().copy();
    ASSERT_TRUE(doc.rotateCanvas(true));
    const QImage rotated = doc.projection().copy();
    ASSERT_EQ(rotated.size(), QSize(70, 130));
    ASSERT_TRUE(doc.undo());
    EXPECT_EQ(doc.recordedSteps(), 3);

    const auto recording = doc.timelapse();
    EXPECT_TRUE(recording->steps[2]->full);
    EXPECT_EQ(recording->steps[2]->size, QSize(70, 130));
    EXPECT_EQ(recording->steps[2]->tiles.size(), 2u * 3u);
    EXPECT_TRUE(recording->steps[3]->full);
    EXPECT_EQ(recording->steps[3]->size, QSize(130, 70));
    EXPECT_EQ(recording->finalSize(), QSize(130, 70));
    TimelapseReplay replay(*recording);
    EXPECT_EQ(shownAt(replay, 2), rotated);
    EXPECT_EQ(shownAt(replay, 1), before);
    EXPECT_EQ(shownAt(replay, 3), before);
    EXPECT_EQ(replay.sizeAt(2), QSize(70, 130));
}

TEST(TimelapseRecording, PausingStopsStepsAndResumingStoresWhatChangedMeanwhile) {
    Document doc(QSize(200, 100), Qt::white);
    doc.startRecording();
    paintLine(doc, 20);
    EXPECT_EQ(doc.recordedSteps(), 1);
    doc.setRecordingPaused(true);
    EXPECT_TRUE(doc.isRecordingPaused());
    paintLine(doc, 60, Qt::blue);
    EXPECT_EQ(doc.recordedSteps(), 1);
    doc.setRecordingPaused(false);
    paintLine(doc, 90, Qt::green);
    EXPECT_EQ(doc.recordedSteps(), 2);
    const auto recording = doc.timelapse();
    TimelapseReplay replay(*recording);
    EXPECT_EQ(shownAt(replay, 2), doc.projection());
    EXPECT_NE(shownAt(replay, 1), doc.projection());
}

TEST(TimelapseRecording, DeletingDropsItAndANewCanvasWithoutOneHasNoTimelapse) {
    Document doc(QSize(100, 100), Qt::white);
    EXPECT_FALSE(doc.isRecording());
    EXPECT_EQ(doc.timelapse(), nullptr);
    EXPECT_EQ(doc.content().timelapse, nullptr);
    doc.startRecording();
    paintLine(doc, 40);
    EXPECT_TRUE(doc.content().timelapse);
    doc.deleteRecording();
    EXPECT_FALSE(doc.isRecording());
    EXPECT_EQ(doc.recordedSteps(), 0);
    EXPECT_EQ(doc.content().timelapse, nullptr);
    paintLine(doc, 60);
    doc.startRecording();
    EXPECT_EQ(doc.recordedSteps(), 0);
}

TEST(TimelapseRecording, PaintingTimeIsCountedBetweenStepsWithLongPausesCutShort) {
    LayerStack layers(QSize(64, 64));
    layers.insert(layers.makeRaster(QStringLiteral("L")), kNoLayer, -1);
    TimelapseRecorder recorder(layers, CanvasBackground::fromPaper(Qt::white));
    std::int64_t clock = 1000;
    recorder.setClock([&] { return clock; });
    QImage picture(64, 64, QImage::Format_RGBA8888_Premultiplied);
    picture.fill(Qt::white);
    recorder.markChanged(QRect(0, 0, 64, 64));
    picture.setPixelColor(3, 3, Qt::red);
    clock += 4000;
    ASSERT_TRUE(recorder.record(Kind::Edit, picture));
    EXPECT_EQ(recorder.paintedMs(), 4000);
    clock += 10ll * 60 * 60 * 1000;
    recorder.markChanged(QRect(0, 0, 64, 64));
    picture.setPixelColor(4, 4, Qt::red);
    ASSERT_TRUE(recorder.record(Kind::Edit, picture));
    EXPECT_EQ(recorder.paintedMs(), 4000 + TimelapseRecorder::kIdleCapMs);
    EXPECT_EQ(recorder.snapshot().paintedMs(), 4000 + TimelapseRecorder::kIdleCapMs);
}

TEST(TimelapseRecording, TheSizeGrowsWithWhatIsPaintedAndIsKnownWithoutWaiting) {
    Document doc(QSize(256, 256), Qt::white);
    doc.startRecording();
    for (int i = 0; i < 5; ++i)
        paintLine(doc, 30 + 40 * i, QColor::fromHsv(i * 50, 255, 255));
    const auto recording = doc.timelapse();
    EXPECT_EQ(doc.recordedBytes(), recording->byteSize());
    EXPECT_GT(recording->byteSize(), 16 * 4 * 8);
    EXPECT_LT(recording->byteSize(), 256 * 256 * 3) << "far less than one raw picture";
}

TEST(TimelapseFile, ARecordingRoundTrips) {
    Document doc(QSize(200, 150), Qt::white);
    doc.startRecording();
    paintLine(doc, 40);
    ASSERT_TRUE(doc.undo());
    ASSERT_TRUE(doc.redo());
    paintLine(doc, 90, Qt::blue);
    doc.setRecordingPaused(true);
    const QByteArray file = saved(doc.content());

    const auto loaded = parse(file);
    ASSERT_TRUE(loaded.ok()) << qPrintable(loaded.error().describe());
    ASSERT_TRUE(loaded.value().content.timelapse);
    EXPECT_TRUE(loaded.value().notes.isEmpty());
    expectSameRecording(*doc.timelapse(), *loaded.value().content.timelapse);
    EXPECT_TRUE(loaded.value().content.timelapse->paused);
    EXPECT_NEAR(double(loaded.value().content.timelapse->startedMs), double(QDateTime::currentMSecsSinceEpoch()), 60000.0)
        << "the date the recording began is kept";
    EXPECT_EQ(doc.recordingStartedMs(), loaded.value().content.timelapse->startedMs);
}

// Paused, a canvas resize (Trim, or a turn of a canvas that isn't square) stores no step of the new size: pause records nothing.
// A recording whose last step isn't the canvas's size was then taken for another picture's and lost as the file opened. A paused
// one is kept, and its first step on resuming holds the whole picture at the new size.
TEST(TimelapseFile, ARecordingPausedAcrossACanvasResizeIsKept) {
    Document doc(QSize(200, 150), Qt::white);
    doc.startRecording();
    paintLine(doc, 40);
    doc.setRecordingPaused(true);
    ASSERT_TRUE(doc.rotateCanvas(true));
    const int steps = doc.recordedSteps();
    const auto loaded = parse(saved(doc.content()));
    ASSERT_TRUE(loaded.ok()) << qPrintable(loaded.error().describe());
    EXPECT_TRUE(loaded.value().notes.isEmpty());
    ASSERT_TRUE(loaded.value().content.timelapse);
    EXPECT_EQ(loaded.value().content.timelapse->finalSize(), QSize(200, 150)) << "nothing was recorded while paused";

    Document reopened(loaded.value().content);
    ASSERT_TRUE(reopened.timelapse());
    EXPECT_EQ(reopened.recordedSteps(), steps);
    reopened.setRecordingPaused(false);
    paintLine(reopened, 60, Qt::blue);
    const auto recording = reopened.timelapse();
    EXPECT_TRUE(recording->steps.back()->full);
    EXPECT_EQ(recording->finalSize(), QSize(150, 200));
}

TEST(TimelapseFile, FramesOfStrokesRoundTrip) {
    Document doc(QSize(200, 150), Qt::white);
    doc.startRecording();
    paintFramed(doc, 40, 3, nullptr);
    paintFramed(doc, 90, 2, nullptr, Qt::blue);
    ASSERT_TRUE(doc.undo());
    const auto loaded = parse(saved(doc.content()));
    ASSERT_TRUE(loaded.ok()) << qPrintable(loaded.error().describe());
    ASSERT_TRUE(loaded.value().content.timelapse);
    const Timelapse& back = *loaded.value().content.timelapse;
    expectSameRecording(*doc.timelapse(), back);
    EXPECT_EQ(kindsOf(back), (std::vector<Kind>{Kind::First, Kind::Partial, Kind::Partial, Kind::Partial, Kind::Edit, Kind::Partial,
                                                Kind::Partial, Kind::Edit, Kind::Undo}));
    EXPECT_TRUE(back.steps[1]->strokeStart);
    EXPECT_TRUE(back.steps[5]->strokeStart);
    EXPECT_EQ(back.path(true), (std::vector<int>{0, 1, 2, 3, 4}));
    TimelapseReplay replay(back);
    EXPECT_EQ(shownAt(replay, 3).size(), QSize(200, 150));
    Document again(loaded.value().content);
    paintFramed(again, 120, 2, nullptr, Qt::green);
    EXPECT_EQ(again.timelapse()->path(true).size(), 5u + 3u);
}

TEST(TimelapseFile, AStrokeStartOnAStepThatIsNotAFrameIsDamage) {
    Document doc(QSize(100, 100), Qt::white);
    doc.startRecording();
    paintLine(doc, 40);
    DocumentContent c = doc.content();
    auto bad = std::make_shared<Timelapse>(*c.timelapse);
    auto edit = std::make_shared<TimelapseStep>(*bad->steps[1]);
    edit->strokeStart = true;
    bad->steps[1] = edit;
    c.timelapse = bad;
    EXPECT_FALSE(parse(saved(c)).ok());
}

TEST(TimelapseFile, FilesWithoutARecordingOpenWithoutOne) {
    Document doc(QSize(100, 100), Qt::white);
    paintLine(doc, 40);
    const QByteArray file = saved(doc.content());
    const auto loaded = parse(file);
    ASSERT_TRUE(loaded.ok());
    EXPECT_EQ(loaded.value().content.timelapse, nullptr);
    Document reopened(loaded.value().content);
    EXPECT_FALSE(reopened.isRecording());
}

TEST(TimelapseFile, AReopenedFileKeepsRecordingAndAppends) {
    QByteArray file;
    std::vector<QImage> pictures;
    {
        Document doc(QSize(200, 150), Qt::white);
        doc.startRecording();
        paintLine(doc, 40);
        pictures = {doc.projection().copy()};
        file = saved(doc.content());
    }
    auto loaded = parse(file);
    ASSERT_TRUE(loaded.ok());
    Document doc(loaded.value().content);
    ASSERT_TRUE(doc.isRecording());
    EXPECT_EQ(doc.recordedSteps(), 1);
    EXPECT_EQ(doc.projection(), pictures[0]);
    EXPECT_FALSE(doc.undoStack()->canUndo());
    paintLine(doc, 100, Qt::blue);
    pictures.push_back(doc.projection().copy());
    ASSERT_TRUE(doc.undo());
    ASSERT_TRUE(doc.redo());
    EXPECT_EQ(doc.recordedSteps(), 4);
    const auto recording = doc.timelapse();
    EXPECT_TRUE(recording->steps[2]->resumed);
    EXPECT_FALSE(recording->steps[3]->resumed);
    EXPECT_EQ(recording->path(true), (std::vector<int>{0, 1, 2}));
    TimelapseReplay replay(*recording);
    EXPECT_EQ(shownAt(replay, 1), pictures[0]);
    EXPECT_EQ(shownAt(replay, 2), pictures[1]);
    EXPECT_EQ(shownAt(replay, 4), pictures[1]);
    const auto again = parse(saved(doc.content()));
    ASSERT_TRUE(again.ok());
    expectSameRecording(*recording, *again.value().content.timelapse);
}

TEST(TimelapseFile, ARecordingOfAnotherPictureIsLeftOutWithANote) {
    Document doc(QSize(100, 100), Qt::white);
    doc.startRecording();
    paintLine(doc, 40);
    DocumentContent content = doc.content();
    Document other(QSize(64, 64), Qt::white);
    other.startRecording();
    paintLine(other, 20, Qt::red, 5, 50, 6);
    content.timelapse = other.timelapse();
    const auto loaded = parse(saved(content));
    ASSERT_TRUE(loaded.ok());
    EXPECT_EQ(loaded.value().content.timelapse, nullptr);
    EXPECT_FALSE(loaded.value().notes.isEmpty());
}

TEST(TimelapseFile, TruncatedAndDamagedRecordingsAreErrorsNotCrashes) {
    Document doc(QSize(100, 100), Qt::white);
    doc.startRecording();
    paintLine(doc, 40);
    paintLine(doc, 70, Qt::blue);
    const QByteArray file = saved(doc.content());
    ASSERT_TRUE(parse(file).ok());
    const int length = int(file.size());
    for (int cut = 12; cut < length; cut += std::max(1, length / 150))
        EXPECT_FALSE(parse(file.left(cut)).ok()) << "cut at " << cut;
    for (int i = 12; i < length; i += std::max(1, length / 150)) {
        QByteArray bad = file;
        bad[i] = static_cast<char>(bad[i] ^ 0x5A);
        EXPECT_FALSE(parse(bad).ok()) << "byte " << i;
    }
    QByteArray rebuilt = file.left(12);
    io::ByteReader r(file, io::ByteOrder::Little);
    r.skip(12);
    bool changed = false;
    while (!r.atEnd()) {
        const QByteArray type = r.bytes(4);
        QByteArray payload = r.bytes(r.u64());
        r.u32();
        if (type == "TLST" && !changed) {
            payload[payload.size() - 1] = static_cast<char>(payload[payload.size() - 1] ^ 0x77);
            payload.chop(1);
            payload[payload.size() - 5] = 0;
            changed = true;
        }
        io::ByteWriter w(io::ByteOrder::Little);
        w.bytes(type);
        w.u64(std::uint64_t(payload.size()));
        w.bytes(payload);
        std::uint32_t crc = io::crc32(0, reinterpret_cast<const std::uint8_t*>(type.constData()), 4);
        crc = io::crc32(crc, reinterpret_cast<const std::uint8_t*>(payload.constData()), std::size_t(payload.size()));
        w.u32(crc);
        rebuilt += w.data();
    }
    ASSERT_TRUE(changed);
    EXPECT_FALSE(parse(rebuilt).ok());
}

namespace {

struct StrokeCosts {
    qint64 finishNs = 0;
    qint64 frameNs = 0;
    int frames = 0;
};

StrokeCosts hundredStrokes(Document& doc, int frameEvery) {
    std::mt19937 rng(3);
    QElapsedTimer timer;
    StrokeCosts costs;
    (void)doc.projection();
    StrokeController stroke(&doc);
    BrushSettings st = defaultSettings(ToolType::Pencil);
    st.size = 40;
    for (int i = 0; i < 100; ++i) {
        const QColor colour = QColor::fromHsv(int(rng() % 360), 220, 230);
        const double x = 100 + double(rng() % 400), y = 300 + double(rng() % 1400);
        EXPECT_TRUE(stroke.begin(at(x, y), colour, st));
        for (int k = 1; k <= 80; ++k) {
            stroke.add(at(x + k * 18 + double(rng() % 7), y + k * 6 * std::sin(k * 0.2 + i)));
            if (frameEvery > 0 && k % frameEvery == 0) {
                (void)doc.projection();
                timer.start();
                doc.captureStrokeFrame();
                costs.frameNs += timer.nsecsElapsed();
                ++costs.frames;
            }
        }
        (void)doc.projection();
        timer.start();
        stroke.finish();
        costs.finishNs += timer.nsecsElapsed();
    }
    return costs;
}

}

TEST(TimelapseCost, HundredStrokesOnA2000By2000Canvas) {
    Document plain(QSize(2000, 2000), Qt::white);
    const StrokeCosts without = hundredStrokes(plain, 0);
    Document steps(QSize(2000, 2000), Qt::white);
    steps.startRecording();
    const StrokeCosts stepsOnly = hundredStrokes(steps, 0);
    const auto stepRecording = steps.timelapse();
    ASSERT_EQ(steps.recordedSteps(), 100);
    const auto measure = [&](int frameEvery, const char* label) {
        Document doc(QSize(2000, 2000), Qt::white);
        doc.startRecording();
        const StrokeCosts framed = hundredStrokes(doc, frameEvery);
        const auto recording = doc.timelapse();
        EXPECT_EQ(doc.recordedSteps(), 100);
        std::size_t tiles = 0;
        for (const auto& s : recording->steps)
            tiles += s->tiles.size();
        std::printf("[timelapse cost] %s: finish() takes %.2f ms, a frame takes %.3f ms (%d frames); the recording is %.1f MB in %zu tiles "
                    "(%.1fx)\n",
                    label, double(framed.finishNs) / 1e6 / 100.0, double(framed.frameNs) / 1e6 / framed.frames, framed.frames,
                    double(recording->byteSize()) / 1e6, tiles, double(recording->byteSize()) / double(stepRecording->byteSize()));
        TimelapseReplay replay(*recording);
        EXPECT_EQ(shownAt(replay, recording->frameCount() - 1), doc.projection());
        QImage white(2000, 2000, QImage::Format_RGBA8888_Premultiplied);
        white.fill(Qt::white);
        EXPECT_EQ(shownAt(replay, 0), white);
    };
    std::size_t stepTiles = 0;
    for (const auto& s : stepRecording->steps)
        stepTiles += s->tiles.size();
    std::printf(
        "[timelapse cost] 2000x2000, 100 strokes: finish() takes %.2f ms without recording and %.2f ms with one step per stroke; the "
        "recording is %.1f MB in %zu tiles (one raw picture is 12 MB)\n",
        double(without.finishNs) / 1e6 / 100.0, double(stepsOnly.finishNs) / 1e6 / 100.0, double(stepRecording->byteSize()) / 1e6,
        stepTiles);
    measure(3, "frames at about 650 px/s");
    measure(10, "frames at about 2000 px/s");
}
