#pragma once

#include "core/ffmpeg/Ffmpeg.h"
#include "core/util/Result.h"

#include <QImage>
#include <QSize>
#include <QString>
#include <QStringList>

#include <memory>

class QProcess;

namespace doodlebold {

// Frames per second as a fraction.
struct FrameRate {
    int num = 0;
    int den = 1;

    double fps() const { return num > 0 && den > 0 ? double(num) / den : 0.0; }
    bool operator==(const FrameRate&) const = default;
};

// The sound of an export: the files FFmpeg reads and the filter graph that prepares them as one stream labelled output.
// Empty: no sound.
struct AudioPlan {
    QStringList inputs;
    QString filterGraph;
    QString output;
    bool isEmpty() const { return inputs.isEmpty(); }
};

// Only H.264 in an MP4 for now: High profile, 4:2:0 and the index at the start (fast start), what players and upload sites
// accept everywhere.
struct EncodeSettings {
    QString output;
    QSize size;                 // even sizes only (4:2:0); encoderArguments() rounds down if needed
    FrameRate rate;
    int crf = 18;
    int maxBitrateKbps = 0;     // > 0: the bitrate never goes above it (peaks too: a VBV buffer of twice a second's worth)
    int targetBitrateKbps = 0;  // > 0: an average to aim for (to fit a file size) instead of constant quality
    int audioKbps = 128;
    double duration = 0.0;      // seconds of picture; the sound is cut to it
};

// The H.264 encoder to use: libx264 where the FFmpeg build has it, else libopenh264, else FFmpeg's own mpeg4 (not H.264,
QString chooseVideoEncoder(const FfmpegTools& tools);

QStringList encoderArguments(const EncodeSettings& settings, const AudioPlan& audio, const QString& videoEncoder);

// The average video bitrate that keeps a file of `seconds` under `maxMegabytes` with `audioKbps` of sound, with a margin
int bitrateForFileSize(double maxMegabytes, double seconds, int audioKbps);

class FrameEncoder {
public:
    explicit FrameEncoder(FfmpegTools tools);
    ~FrameEncoder();
    FrameEncoder(const FrameEncoder&) = delete;
    FrameEncoder& operator=(const FrameEncoder&) = delete;

    bool start(const EncodeSettings& settings, const AudioPlan& audio);
    bool write(const QImage& frame);
    bool finish();
    void cancel();
    QString errorText() const { return m_error; }

private:
    FfmpegTools m_tools;
    std::unique_ptr<QProcess> m_process;
    EncodeSettings m_settings;
    QString m_error;
};

// The length of a sound file in seconds, or why it cannot be used.
Result<double, QString> probeSoundDuration(const FfmpegTools& tools, const QString& file);
// One sound file from its start, resampled to 48 kHz stereo, padded or cut to `duration` seconds of picture.
AudioPlan soundFilePlan(const QString& file, double fileDuration, double duration);

}
