#include "FfmpegTestSupport.h"

#include "core/animation/AnimationVideo.h"
#include "core/document/Document.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>

#include <gtest/gtest.h>

using namespace doodlebold;

namespace {

// A second of 48 kHz stereo silence as a WAV file.
QByteArray silentWav(double seconds) {
    const auto frames = static_cast<quint32>(48000 * seconds);
    const quint32 data = frames * 4;
    QByteArray out;
    const auto u32 = [&](quint32 v) { out.append(reinterpret_cast<const char*>(&v), 4); };
    const auto u16 = [&](quint16 v) { out.append(reinterpret_cast<const char*>(&v), 2); };
    out.append("RIFF");
    u32(36 + data);
    out.append("WAVEfmt ");
    u32(16);
    u16(1);
    u16(2);
    u32(48000);
    u32(48000 * 4);
    u16(4);
    u16(16);
    out.append("data");
    u32(data);
    out.append(QByteArray(static_cast<qsizetype>(data), '\0'));
    return out;
}

}

TEST(AnimationVideo, ExportPlanHoldsEachFrameForItsBeats) {
    Animation a;
    a.tracks.resize(1);
    a.tracks[0].frames.resize(3);
    a.tracks[0].frames[0].hold = 2;
    a.tracks[0].frames[2].hold = 3;
    a.fps = 12;
    const AnimationExportPlan plan = planAnimationExport(a, QSize(40, 30));
    // Each beat names the first beat showing the same frames, whose picture it reuses.
    EXPECT_EQ(plan.beats, (std::vector<int>{0, 0, 2, 3, 3, 3}));
    // A second track changing at beat 1 splits the first frame's beats; a hidden one changes nothing.
    AnimationTrack over;
    over.frames.resize(2);
    a.tracks.push_back(over);
    EXPECT_EQ(planAnimationExport(a, QSize(40, 30)).beats, (std::vector<int>{0, 1, 2, 3, 3, 3}));
    a.tracks[1].hidden = true;
    EXPECT_EQ(planAnimationExport(a, QSize(40, 30)).beats, (std::vector<int>{0, 0, 2, 3, 3, 3}));
    EXPECT_EQ(plan.rate, (FrameRate{12, 1}));
    EXPECT_DOUBLE_EQ(plan.seconds(), 0.5);
    EXPECT_EQ(planAnimationExport(Animation{}, QSize(4, 4)).beats, std::vector<int>{0});
}

TEST(AnimationVideo, FractionalRatesBecomeFractions) {
    EXPECT_EQ(frameRateOf(24), (FrameRate{24, 1}));
    EXPECT_EQ(frameRateOf(23.976), (FrameRate{2997, 125}));
}

TEST(AnimationVideo, SoundClipsAreDelayedToTheirBeatsAndMixed) {
    QTemporaryDir dir;
    SoundClip a;
    a.id = 1;
    a.name = QStringLiteral("a.wav");
    a.data = std::make_shared<const QByteArray>(silentWav(0.2));
    a.startBeat = 6;
    SoundClip late = a;
    late.id = 2;
    late.startBeat = 200;  // after the end: left out
    const AudioPlan plan = soundClipsPlan({a, late}, {}, 12, 2.0, dir.path());
    ASSERT_EQ(plan.inputs.size(), 1);
    EXPECT_TRUE(QFile::exists(plan.inputs[0]));
    EXPECT_TRUE(plan.filterGraph.contains(QStringLiteral("adelay=500|500")));
    EXPECT_TRUE(plan.filterGraph.contains(QStringLiteral("amix=inputs=1")));
    EXPECT_EQ(plan.output, QStringLiteral("mix"));
    EXPECT_TRUE(soundClipsPlan({}, {}, 12, 2.0, dir.path()).isEmpty());
    // A trimmed clip plays only its part.
    a.seconds = 0.2;
    a.trimStart = 0.05;
    a.trimEnd = 0.1;
    EXPECT_TRUE(
        soundClipsPlan({a}, {}, 12, 2.0, dir.path()).filterGraph.contains(QStringLiteral("atrim=start=0.050000:duration=0.050000,")));
}

TEST(AnimationVideo, PicturesAreFittedIntoTheCanvas) {
    EXPECT_EQ(fittedSize(QSize(1920, 1080), QSize(2000, 2000)), QSize(2000, 1125));
    EXPECT_EQ(fittedSize(QSize(100, 400), QSize(200, 200)), QSize(50, 200));
    VideoReference v;
    v.path = QStringLiteral("/v/walk.mp4");
    v.sourceSize = QSize(1920, 1080);
    v.fps = 16;
    const QStringList args = videoExtractArguments(v, QSize(2000, 1125), QStringLiteral("/cache/x"));
    EXPECT_TRUE(args.contains(QStringLiteral("fps=16.000000,scale=2000:1125:flags=bicubic")));
    EXPECT_TRUE(args.last().endsWith(QStringLiteral("%06d.jpg")));
    v.lossless = true;
    EXPECT_TRUE(videoExtractArguments(v, QSize(2000, 1125), QStringLiteral("/cache/x")).last().endsWith(QStringLiteral("%06d.png")));
}

TEST(AnimationVideo, AnExportedAnimationComesBackAsAVideoUnderTheFrames) {
    const auto& tools = test::systemFfmpeg();
    if (!tools)
        GTEST_SKIP() << "no FFmpeg on this machine";
    QTemporaryDir dir;
    Document doc(QSize(64, 48), Qt::white);
    ASSERT_TRUE(doc.fillLayer(doc.currentLayerId(), QColor(200, 30, 30)));
    ASSERT_TRUE(doc.newFrame());
    ASSERT_TRUE(doc.fillLayer(doc.currentLayerId(), QColor(30, 30, 200)));
    ASSERT_TRUE(doc.changeHold({1}, +1));
    SoundClip clip;
    clip.name = QStringLiteral("a.wav");
    clip.data = std::make_shared<const QByteArray>(silentWav(0.1));
    ASSERT_TRUE(doc.addSound(clip));

    const AnimationExportPlan plan = planAnimationExport(doc.animation(), doc.size());
    EncodeSettings settings;
    settings.output = dir.filePath(QStringLiteral("walk.mp4"));
    const AudioPlan audio = soundClipsPlan(doc.animation().sounds, {}, doc.animation().fps, plan.seconds(), dir.path());
    const AnimationExportResult done = exportAnimation(*tools, plan, settings, audio, [&](int f) { return doc.framePicture(f); }, {});
    ASSERT_TRUE(done.ok) << done.error.toStdString();

    const auto info = probeVideo(*tools, settings.output);
    ASSERT_TRUE(info) << info.error().toStdString();
    EXPECT_EQ(info.value().size, QSize(64, 48));
    EXPECT_NEAR(info.value().fps, 12.0, 0.01);
    EXPECT_NEAR(info.value().seconds, 0.25, 0.1);
    EXPECT_TRUE(info.value().hasSound);

    VideoReference v;
    v.path = settings.output;
    v.sourceSize = info.value().size;
    v.sourceFps = info.value().fps;
    v.seconds = info.value().seconds;
    v.fps = 12;
    VideoFrames frames(v, QSize(128, 96), dir.filePath(QStringLiteral("cache")));
    EXPECT_FALSE(frames.isCached());
    const auto extracted = frames.extract(*tools, {});
    ASSERT_TRUE(extracted) << extracted.error().toStdString();
    EXPECT_TRUE(frames.isCached());
    const QImage first = frames.picture(0);
    ASSERT_EQ(first.size(), QSize(128, 96));
    EXPECT_GT(qRed(first.pixel(64, 48)), 150);
    const QImage last = frames.picture(2);
    EXPECT_GT(qBlue(last.pixel(64, 48)), 150);
    EXPECT_TRUE(frames.picture(50).isNull());

    const auto sound = extractVideoSound(*tools, settings.output);
    ASSERT_TRUE(sound) << sound.error().toStdString();
    EXPECT_TRUE(sound.value().startsWith("RIFF"));
}

TEST(AnimationVideo, AWaveformIsTheLoudestSampleOfEachHundredthOfASecond) {
    // A quiet tenth of a second, then a loud one, at 8 kHz.
    QByteArray samples;
    for (int i = 0; i < 1600; ++i) {
        const qint16 v = static_cast<qint16>((i < 800 ? 1000 : 32000) * (i % 2 ? 1 : -1));
        samples.append(reinterpret_cast<const char*>(&v), 2);
    }
    const SoundPeaks peaks = peaksOf(samples, 8000);
    ASSERT_EQ(peaks.peaks.size(), 20u);
    EXPECT_LT(peaks.loudest(0.0, 0.05), 10);
    EXPECT_GT(peaks.loudest(0.15, 0.15), 240);
    EXPECT_GT(peaks.loudest(0.05, 0.12), 240);  // reaches into the loud part
    EXPECT_EQ(peaks.loudest(5.0, 6.0), 0);       // past the end
}

// A sound track's volume goes into the export as FFmpeg's volume filter; at full volume nothing is added.
TEST(AnimationVideo, AClipIsExportedAtItsTracksVolume) {
    QTemporaryDir dir;
    SoundClip a;
    a.id = 1;
    a.name = QStringLiteral("a.wav");
    a.data = std::make_shared<const QByteArray>(silentWav(0.2));
    EXPECT_TRUE(soundClipsPlan({a}, {0.4}, 12, 2.0, dir.path()).filterGraph.contains(QStringLiteral("volume=0.4")));
    EXPECT_FALSE(soundClipsPlan({a}, {1.0}, 12, 2.0, dir.path()).filterGraph.contains(QStringLiteral("volume=")));
}
