#include "core/ffmpeg/Ffmpeg.h"
#include "core/ffmpeg/FrameEncoder.h"
#include "FfmpegTestSupport.h"

#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QTemporaryDir>

#include <gtest/gtest.h>

using namespace doodlebold;

namespace {

constexpr char kEncoders[] = "Encoders:\n"
                             " V..... = Video\n"
                             " A..... = Audio\n"
                             " ------\n"
                             " V....D libx264              libx264 H.264 / AVC / MPEG-4 AVC / MPEG-4 part 10 (codec h264)\n"
                             " VF...D png                  PNG (Portable Network Graphics) image\n"
                             " A....D aac                  AAC (Advanced Audio Coding)\n"
                             " S..... ass                  ASS (Advanced SubStation Alpha) subtitle\n";

#ifndef Q_OS_WIN
void writeScript(const QString& path, const QByteArray& text, int exitCode = 0) {
    QFile f(path);
    ASSERT_TRUE(f.open(QIODevice::WriteOnly));
    f.write("#!/bin/sh\ncase \"$*\" in *-encoders*) printf '%s' '" + QByteArray(kEncoders) + "';; *) printf '%s\\n' '" + text +
            "';; esac\nexit " + QByteArray::number(exitCode) + "\n");
    f.close();
    f.setPermissions(f.permissions() | QFileDevice::ExeOwner | QFileDevice::ExeUser);
}
#endif

}

namespace {

int index(const QStringList& args, const QString& option) {
    return static_cast<int>(args.indexOf(option));
}

QString valueOf(const QStringList& args, const QString& option) {
    const int i = index(args, option);
    return i >= 0 && i + 1 < args.size() ? args.at(i + 1) : QString();
}

}

TEST(FfmpegLocator, ReadsTheVersionLine) {
    EXPECT_EQ(FfmpegLocator::parseVersion("ffmpeg version 8.1.3 Copyright (c) 2000-2026 the FFmpeg developers\nbuilt with gcc"),
              QStringLiteral("8.1.3"));
    EXPECT_EQ(FfmpegLocator::parseVersion("ffmpeg version n7.1-12-gabcdef Copyright"), QStringLiteral("n7.1-12-gabcdef"));
    EXPECT_TRUE(FfmpegLocator::parseVersion("ffprobe version 8.1.3").isEmpty()) << "not ffmpeg";
    EXPECT_TRUE(FfmpegLocator::parseVersion("").isEmpty());
}

TEST(FfmpegLocator, ReadsTheEncoderList) {
    const QStringList encoders = FfmpegLocator::parseEncoders(kEncoders);
    EXPECT_EQ(encoders, (QStringList{QStringLiteral("libx264"), QStringLiteral("png"), QStringLiteral("aac"), QStringLiteral("ass")}))
        << "the legend lines (\"V..... = Video\") are not encoders";
}

TEST(FfmpegLocator, MediaUrlsAreAbsoluteFileUrls) {
    const QString url = mediaUrl(QStringLiteral("-odd name.mp4"));
    EXPECT_TRUE(url.startsWith(QStringLiteral("file:")));
    EXPECT_FALSE(url.mid(5).startsWith(QLatin1Char('-'))) << "the path is absolute, so it never starts with a dash";
}

#ifndef Q_OS_WIN
TEST(FfmpegLocator, FindsTheConfiguredProgramAndItsProbe) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    writeScript(dir.filePath(QStringLiteral("ffmpeg")), "ffmpeg version 9.9-test Copyright");
    writeScript(dir.filePath(QStringLiteral("ffprobe")), "ffprobe version 9.9-test");

    for (const QString& configured : {dir.path(), dir.filePath(QStringLiteral("ffmpeg"))}) {
        const auto found = FfmpegLocator::locate(configured, {});
        ASSERT_TRUE(found.ok()) << configured.toStdString();
        EXPECT_EQ(found.value().version, QStringLiteral("9.9-test"));
        EXPECT_EQ(found.value().ffprobe, dir.filePath(QStringLiteral("ffprobe")));
        EXPECT_TRUE(found.value().hasEncoder(QStringLiteral("libx264")));
        EXPECT_FALSE(found.value().hasEncoder(QStringLiteral("libvpx-vp9")));
    }
}

TEST(FfmpegLocator, TheToolsFolderComesBeforeThePath) {
    QTemporaryDir tools;
    ASSERT_TRUE(QDir(tools.path()).mkpath(QStringLiteral("bin")));
    writeScript(tools.filePath(QStringLiteral("bin/ffmpeg")), "ffmpeg version 1.0-tools Copyright");
    writeScript(tools.filePath(QStringLiteral("bin/ffprobe")), "ffprobe version 1.0-tools");
    const auto found = FfmpegLocator::locate({}, tools.path());
    ASSERT_TRUE(found.ok());
    EXPECT_EQ(found.value().version, QStringLiteral("1.0-tools")) << "an FFmpeg kept for Doodlebold wins over the system's";
}

TEST(FfmpegLocator, ABrokenConfiguredProgramIsReportedNotSkipped) {
    QTemporaryDir dir;
    writeScript(dir.filePath(QStringLiteral("ffmpeg")), "not ffmpeg at all", 1);
    const auto found = FfmpegLocator::locate(dir.path(), {});
    ASSERT_FALSE(found.ok());
    EXPECT_EQ(found.error(), FfmpegError::NotRunnable);
}

TEST(FfmpegLocator, FfmpegWithoutProbeIsAnError) {
    QTemporaryDir dir;
    writeScript(dir.filePath(QStringLiteral("ffmpeg")), "ffmpeg version 9.9-test Copyright");
    const auto found = FfmpegLocator::check(dir.filePath(QStringLiteral("ffmpeg")));
    ASSERT_FALSE(found.ok()) << "an ffprobe on the PATH isn't taken in its place: the user approved no program there";
    EXPECT_EQ(found.error(), FfmpegError::NoProbe);
}
#endif

// Whatever comes first on the PATH would run as FFmpeg: one found only there runs once the user has said yes to that file.
TEST(FfmpegLocator, OneOnlyOnThePathRunsOnceApproved) {
    QTemporaryDir onPath, empty;
    const QString program = onPath.filePath(FfmpegLocator::executableName(QStringLiteral("ffmpeg")));
#ifdef Q_OS_WIN
    QFile placeholder(program);  // found, not run: the question comes first
    ASSERT_TRUE(placeholder.open(QIODevice::WriteOnly));
    placeholder.close();
#else
    writeScript(program, "ffmpeg version 7.0-path Copyright");
    writeScript(onPath.filePath(QStringLiteral("ffprobe")), "ffprobe version 7.0-path");
#endif
    const QByteArray savedPath = qgetenv("PATH");
    qputenv("PATH", onPath.path().toLocal8Bit());
    const auto unasked = FfmpegLocator::locate({}, empty.path());
    const QString offered = FfmpegLocator::onPath();
    const auto approved = FfmpegLocator::locate({}, empty.path(), program);
    const auto another = FfmpegLocator::locate({}, empty.path(), empty.filePath(QStringLiteral("ffmpeg")));
    qputenv("PATH", savedPath);
    ASSERT_FALSE(unasked.ok());
    EXPECT_EQ(unasked.error(), FfmpegError::NotApproved);
    EXPECT_EQ(QFileInfo(offered).canonicalFilePath(), QFileInfo(program).canonicalFilePath());
    EXPECT_FALSE(!approved.ok() && approved.error() == FfmpegError::NotApproved);
#ifndef Q_OS_WIN
    EXPECT_TRUE(approved.ok());
#endif
    ASSERT_FALSE(another.ok());
    EXPECT_EQ(another.error(), FfmpegError::NotApproved) << "a yes is for that file alone";
}

TEST(FfmpegLocator, NothingFoundIsNotFound) {
    QTemporaryDir empty;
    const QByteArray savedPath = qgetenv("PATH");
    qputenv("PATH", empty.path().toLocal8Bit());
    const auto found = FfmpegLocator::locate({}, empty.path());
    qputenv("PATH", savedPath);
    ASSERT_FALSE(found.ok());
    EXPECT_EQ(found.error(), FfmpegError::NotFound);
    EXPECT_FALSE(describe(FfmpegError::NotFound).isEmpty());
}

TEST(FramePipes, EncoderArgumentsForH264) {
    EncodeSettings s;
    s.output = QStringLiteral("/out/drawover.mp4");
    s.size = QSize(1920, 1080);
    s.rate = {16, 1};
    s.crf = 20;
    s.maxBitrateKbps = 8000;
    s.duration = 12.0;
    const QStringList args = encoderArguments(s, {}, QStringLiteral("libx264"));
    EXPECT_EQ(valueOf(args, QStringLiteral("-s")), QStringLiteral("1920x1080"));
    EXPECT_EQ(valueOf(args, QStringLiteral("-framerate")), QStringLiteral("16/1"));
    EXPECT_EQ(valueOf(args, QStringLiteral("-i")), QStringLiteral("pipe:0"));
    EXPECT_EQ(valueOf(args, QStringLiteral("-c:v")), QStringLiteral("libx264"));
    EXPECT_EQ(valueOf(args, QStringLiteral("-profile:v")), QStringLiteral("high"));
    EXPECT_EQ(valueOf(args, QStringLiteral("-crf")), QStringLiteral("20"));
    EXPECT_EQ(valueOf(args, QStringLiteral("-maxrate")), QStringLiteral("8000k")) << "the peaks are capped";
    EXPECT_EQ(valueOf(args, QStringLiteral("-bufsize")), QStringLiteral("16000k"));
    EXPECT_EQ(valueOf(args, QStringLiteral("-g")), QStringLiteral("32"));
    EXPECT_EQ(valueOf(args, QStringLiteral("-pix_fmt")), QStringLiteral("rgba")) << "the input's format comes first";
    EXPECT_EQ(args.at(args.lastIndexOf(QStringLiteral("-pix_fmt")) + 1), QStringLiteral("yuv420p"));
    EXPECT_EQ(valueOf(args, QStringLiteral("-movflags")), QStringLiteral("+faststart"));
    EXPECT_EQ(index(args, QStringLiteral("-c:a")), -1) << "no sound without an audio plan";
    EXPECT_EQ(index(args, QStringLiteral("-vf")), -1) << "an even size needs no crop";

    s.targetBitrateKbps = 5000;
    s.size = QSize(1279, 721);
    const QStringList target = encoderArguments(s, {}, QStringLiteral("libx264"));
    EXPECT_EQ(valueOf(target, QStringLiteral("-b:v")), QStringLiteral("5000k"));
    EXPECT_EQ(index(target, QStringLiteral("-crf")), -1);
    EXPECT_EQ(valueOf(target, QStringLiteral("-vf")), QStringLiteral("crop=1278:720:0:0"));
}

TEST(FramePipes, EncoderArgumentsWithSound) {
    EncodeSettings s;
    s.output = QStringLiteral("/out/a.mp4");
    s.size = QSize(64, 48);
    s.rate = {16, 1};
    s.audioKbps = 160;
    AudioPlan audio;
    audio.inputs = {QStringLiteral("/clips/skate.mp4"), QStringLiteral("/music/beat.mp3")};
    audio.filterGraph = QStringLiteral("[1:a]anull[a0];[2:a]anull[a1];[a0][a1]amix=inputs=2[mix]");
    audio.output = QStringLiteral("mix");
    const QStringList args = encoderArguments(s, audio, QStringLiteral("libx264"));
    EXPECT_EQ(args.count(QStringLiteral("-i")), 3);
    EXPECT_EQ(valueOf(args, QStringLiteral("-filter_complex")), audio.filterGraph);
    EXPECT_TRUE(args.contains(QStringLiteral("[mix]")));
    EXPECT_EQ(valueOf(args, QStringLiteral("-c:a")), QStringLiteral("aac"));
    EXPECT_EQ(valueOf(args, QStringLiteral("-b:a")), QStringLiteral("160k"));
}

TEST(FramePipes, OtherEncodersGetABitrate) {
    EncodeSettings s;
    s.output = QStringLiteral("/out/a.mp4");
    s.size = QSize(1920, 1080);
    s.rate = {16, 1};
    s.maxBitrateKbps = 2000;
    const QStringList args = encoderArguments(s, {}, QStringLiteral("libopenh264"));
    EXPECT_EQ(valueOf(args, QStringLiteral("-b:v")), QStringLiteral("2000k")) << "the cap is lower than the size's default";
    EXPECT_EQ(index(args, QStringLiteral("-crf")), -1);
}

TEST(FramePipes, EncoderChoiceFallsBack) {
    FfmpegTools t;
    t.encoders = {QStringLiteral("mpeg4"), QStringLiteral("libopenh264")};
    EXPECT_EQ(chooseVideoEncoder(t), QStringLiteral("libopenh264"));
    t.encoders << QStringLiteral("libx264");
    EXPECT_EQ(chooseVideoEncoder(t), QStringLiteral("libx264"));
    t.encoders = {QStringLiteral("png")};
    EXPECT_TRUE(chooseVideoEncoder(t).isEmpty());
}

TEST(FramePipes, BitrateForAFileSize) {
    EXPECT_EQ(bitrateForFileSize(10.0, 12.0, 128), 6466 - 128);
    EXPECT_EQ(bitrateForFileSize(0.01, 600.0, 128), 0) << "nothing fits";
    EXPECT_EQ(bitrateForFileSize(10.0, 0.0, 128), 0);
}

TEST(FramePipes, ACancelledEncodeLeavesNoFile) {
    const auto& tools = test::systemFfmpeg();
    if (!tools)
        GTEST_SKIP() << "no FFmpeg on this machine";
    QTemporaryDir dir;
    EncodeSettings s;
    s.output = dir.filePath(QStringLiteral("cancelled.mp4"));
    s.size = QSize(64, 48);
    s.rate = {16, 1};
    FrameEncoder encoder(*tools);
    ASSERT_TRUE(encoder.start(s, {}));
    QImage frame(s.size, QImage::Format_RGBA8888);
    frame.fill(Qt::white);
    ASSERT_TRUE(encoder.write(frame));
    encoder.cancel();
    EXPECT_FALSE(QFile::exists(s.output));
}

TEST(FramePipes, EncodesFramesAndProbesTheFile) {
    const auto& tools = test::systemFfmpeg();
    if (!tools)
        GTEST_SKIP() << "no FFmpeg on this machine";
    QTemporaryDir dir;
    EncodeSettings s;
    s.output = dir.filePath(QStringLiteral("out.mp4"));
    s.size = QSize(64, 48);
    s.rate = {16, 1};
    s.duration = 1.0;
    FrameEncoder encoder(*tools);
    ASSERT_TRUE(encoder.start(s, {})) << encoder.errorText().toStdString();
    for (int i = 0; i < 16; ++i) {
        QImage frame(s.size, QImage::Format_ARGB32);
        frame.fill(i < 8 ? QColor(200, 40, 40) : QColor(40, 40, 200));
        ASSERT_TRUE(encoder.write(frame)) << encoder.errorText().toStdString();
    }
    ASSERT_TRUE(encoder.finish()) << encoder.errorText().toStdString();
    const auto info = test::probeClip(*tools, s.output);
    ASSERT_TRUE(info);
    EXPECT_EQ(info->size, s.size);
    EXPECT_NEAR(info->duration, 1.0, 0.1);
    EXPECT_FALSE(info->hasAudio);
}

TEST(SoundFilePlan, CutsAndPadsTheFileToThePicture) {
    const AudioPlan plan = soundFilePlan(QStringLiteral("/s/a.mp3"), 10.0, 4.0);
    ASSERT_EQ(plan.inputs, QStringList{QStringLiteral("/s/a.mp3")});
    EXPECT_EQ(plan.output, QStringLiteral("mix"));
    EXPECT_TRUE(plan.filterGraph.startsWith(QStringLiteral("[1:a]atrim=start=0:end=4.000000,")));
    EXPECT_TRUE(plan.filterGraph.contains(QStringLiteral("apad=whole_dur=4.000000,atrim=end=4.000000[mix]")));
    const AudioPlan shorter = soundFilePlan(QStringLiteral("/s/a.mp3"), 2.0, 4.0);
    EXPECT_TRUE(shorter.filterGraph.contains(QStringLiteral("atrim=start=0:end=2.000000,")));
    EXPECT_TRUE(soundFilePlan(QStringLiteral("/s/a.mp3"), 0.0, 4.0).isEmpty());
}
