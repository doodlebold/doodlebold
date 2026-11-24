#pragma once

#include "core/timelapse/Timelapse.h"
#include "core/ffmpeg/Ffmpeg.h"
#include "core/ffmpeg/FrameEncoder.h"

#include <QImage>
#include <QSize>
#include <QString>

#include <functional>
#include <vector>

namespace doodlebold {

// The video always starts from the first picture and runs at kTimelapseFps.
struct TimelapseExportOptions {
    TimelapseLength length = TimelapseLength::Full;
    int holdSeconds = 3;           // "Hold the finished picture for [3] s" at the end; 0: no hold
    bool leaveOutUndone = true;
    QSize box{1920, 1080};
    static constexpr int kMaxHoldSeconds = 30;
};

struct TimelapsePlan {
    QSize frameSize;
    std::vector<int> frames;  // the length's frames (every picture of the path, or resampled to 30 s), then the held ones
    int heldFrames = 0;
    int fps = kTimelapseFps;
    double seconds() const { return double(frames.size()) / fps; }
};
TimelapsePlan planTimelapse(const Timelapse& recording, const TimelapseExportOptions& options);

// scaled to fit the frame, centred on black when its shape differs (a canvas rotated or trimmed part-way is shown whole).
QImage timelapseFrame(const QImage& picture, const QSize& frameSize);

struct TimelapseExportResult {
    bool ok = false;
    bool cancelled = false;
    QString error;
};
// `done` frames of `total` written; return false to cancel.
using TimelapseProgress = std::function<bool(int done, int total)>;
// Blocking: run it on a worker thread (the FFmpeg process lives on the calling thread).
TimelapseExportResult exportTimelapse(const FfmpegTools& tools, const Timelapse& recording, const TimelapsePlan& plan,
                                      EncodeSettings settings, const AudioPlan& audio, const TimelapseProgress& progress);

}
