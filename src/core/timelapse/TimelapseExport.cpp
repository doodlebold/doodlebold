#include "core/timelapse/TimelapseExport.h"

#include "core/timelapse/TimelapseReplay.h"

#include <QCoreApplication>
#include <QPainter>

#include <algorithm>
#include <cstdlib>

namespace doodlebold {

TimelapsePlan planTimelapse(const Timelapse& recording, const TimelapseExportOptions& options) {
    TimelapsePlan plan;
    if (recording.isEmpty())
        return plan;
    plan.frameSize = fitTimelapseSize(recording.finalSize(), options.box);
    plan.fps = kTimelapseFps;
    const std::vector<int> path = recording.path(options.leaveOutUndone);
    // Every picture of the path is a frame; the short length samples the path to fit its seconds.
    plan.frames = options.length == TimelapseLength::Full
                      ? path
                      : resampleTimelapse(path, timelapseFrameCount(options.length, int(path.size()), plan.fps));
    if (options.holdSeconds > 0) {
        plan.heldFrames = std::clamp(options.holdSeconds, 0, TimelapseExportOptions::kMaxHoldSeconds) * plan.fps;
        plan.frames.insert(plan.frames.end(), static_cast<std::size_t>(plan.heldFrames), plan.frames.back());
    }
    return plan;
}

QImage timelapseFrame(const QImage& picture, const QSize& frameSize) {
    const QSize fitted = picture.size().scaled(frameSize, Qt::KeepAspectRatio);
    // The frame's sides are rounded down to even numbers: a picture of the same shape fills it.
    if (std::abs(fitted.width() - frameSize.width()) <= 1 && std::abs(fitted.height() - frameSize.height()) <= 1) {
        return picture.size() == frameSize
                   ? picture.convertToFormat(QImage::Format_RGBA8888)
                   : picture.scaled(frameSize, Qt::IgnoreAspectRatio, Qt::SmoothTransformation).convertToFormat(QImage::Format_RGBA8888);
    }
    QImage frame(frameSize, QImage::Format_RGBA8888);
    frame.fill(Qt::black);
    const QImage scaled = picture.size() == fitted ? picture : picture.scaled(fitted, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    QPainter p(&frame);
    p.drawImage(QPoint((frameSize.width() - fitted.width()) / 2, (frameSize.height() - fitted.height()) / 2), scaled);
    return frame;
}

TimelapseExportResult exportTimelapse(const FfmpegTools& tools, const Timelapse& recording, const TimelapsePlan& plan,
                                      EncodeSettings settings, const AudioPlan& audio, const TimelapseProgress& progress) {
    TimelapseExportResult result;
    if (plan.frames.empty()) {
        result.error = QCoreApplication::translate("TimelapseExport", "There is nothing recorded to export.");
        return result;
    }
    settings.size = plan.frameSize;
    settings.rate = {plan.fps, 1};
    settings.duration = plan.seconds();
    FrameEncoder encoder(tools);
    if (!encoder.start(settings, audio)) {
        result.error = encoder.errorText();
        return result;
    }
    TimelapseReplay replay(recording);
    QImage frame;
    int shown = -1;
    const int total = static_cast<int>(plan.frames.size());
    for (int i = 0; i < total; ++i) {
        if (progress && !progress(i, total)) {
            encoder.cancel();
            result.cancelled = true;
            return result;
        }
        const int step = plan.frames[static_cast<std::size_t>(i)];
        if (step != shown) {
            frame = timelapseFrame(replay.seek(step), plan.frameSize);
            shown = step;
        }
        if (!encoder.write(frame)) {
            result.error = encoder.errorText();
            encoder.cancel();
            return result;
        }
    }
    if (progress)
        progress(total, total);
    if (!encoder.finish()) {
        result.error = encoder.errorText();
        return result;
    }
    result.ok = true;
    return result;
}

}
