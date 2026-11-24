#pragma once

#include "core/animation/Animation.h"
#include "core/ffmpeg/Ffmpeg.h"
#include "core/ffmpeg/FrameEncoder.h"
#include "core/util/Result.h"

#include <QImage>
#include <QSize>
#include <QString>

#include <functional>
#include <list>
#include <utility>
#include <vector>

namespace doodlebold {

// What FFmpeg says about a video file.
struct VideoInfo {
    QSize size;
    double fps = 0.0;
    double seconds = 0.0;
    bool hasSound = false;
};
Result<VideoInfo, QString> probeVideo(const FfmpegTools& tools, const QString& file);
// The size a picture of `source` has fitted into `box`, keeping its shape.
QSize fittedSize(const QSize& source, const QSize& box);

// `done` of `total` steps; return false to cancel.
using VideoProgress = std::function<bool(int done, int total)>;

// A video's pictures at the timeline's rate, fitted into the canvas, cached on disk as JPEG (or PNG for "Lossless") in a
// folder of their own; only a few near the frame drawn on are kept in memory. The video file isn't changed.
class VideoFrames {
public:
    VideoFrames(VideoReference video, QSize canvas, QString cacheRoot);

    static QString defaultCacheRoot();
    QString folder() const;  // depends on the file, its time stamp, the rate, the canvas size and the picture format
    bool isCached() const;
    // Blocking: run it on a worker thread. Writes every picture into folder().
    Result<int, QString> extract(const FfmpegTools& tools, const VideoProgress& progress) const;
    // Picture `index` on a transparent canvas-sized image, or a null image when there is none (-1, or past the end).
    QImage picture(int index);
    // The picture as it is in the cache (not fitted into the canvas), decoded straight at `height` pixels high: much quicker
    // than the whole picture, for the timeline's video lane. Not kept in memory.
    QImage smallPicture(int index, int height) const;
    QString picturePath(int index) const;
    const VideoReference& video() const { return m_video; }
    QSize canvas() const { return m_canvas; }

    static constexpr int kMemoryPictures = 8;

private:
    VideoReference m_video;
    QSize m_canvas;
    QString m_root;
    std::list<std::pair<int, QImage>> m_recent;  // most recently used first
};

// The FFmpeg arguments that write a video's pictures at `fps`, fitted into `box`, as numbered files in `folder`.
QStringList videoExtractArguments(const VideoReference& video, const QSize& box, const QString& folder);
// The video's sound as a 48 kHz stereo WAV file's bytes, to keep as a sound clip.
Result<QByteArray, QString> extractVideoSound(const FfmpegTools& tools, const QString& file);

// A sound's loudness over time, for the timeline's waveform: the loudest sample of every 1/kPerSecond s, as 0 … 255.
struct SoundPeaks {
    static constexpr int kPerSecond = 100;
    std::vector<quint8> peaks;

    // The loudest of the peaks from `from` to `to` seconds into the file (at least the one at `from`); 0 past the end.
    quint8 loudest(double from, double to) const;
};
// From 16-bit mono samples at `rate` Hz.
SoundPeaks peaksOf(const QByteArray& samples, int rate);
// Decodes a sound clip's file with FFmpeg and measures it. `name` gives the file's type by its suffix.
Result<SoundPeaks, QString> readSoundPeaks(const FfmpegTools& tools, const QByteArray& data, const QString& name);

// Export Video: every frame of the animation, each held for its beats, at the animation's rate; the sound clips mixed in at
// their places. `soundFolder` is where the clips' files are written for FFmpeg to read.
struct AnimationExportPlan {
    QSize size;
    FrameRate rate;
    std::vector<int> beats;  // for each beat, the beat whose picture it shows (the first of a run showing the same frames)
    double seconds() const { return rate.fps() > 0 ? double(beats.size()) / rate.fps() : 0.0; }
};
AnimationExportPlan planAnimationExport(const Animation& animation, const QSize& canvas);
FrameRate frameRateOf(double fps);
// The mix of the clips, each delayed to its start, as loud as its gain (0 … 1 of its file, Animation::gainOf; the same count
// as the clips), and cut at the end of the animation. Writes the clips' files into folder.
AudioPlan soundClipsPlan(const std::vector<SoundClip>& clips, const std::vector<double>& gains, double fps, double seconds,
                         const QString& folder);

struct AnimationExportResult {
    bool ok = false;
    bool cancelled = false;
    QString error;
};
// Blocking: run it on a worker thread. `picture` gives frame i as the canvas shows it without onion skins.
AnimationExportResult exportAnimation(const FfmpegTools& tools, const AnimationExportPlan& plan, EncodeSettings settings,
                                      const AudioPlan& audio, const std::function<QImage(int frame)>& picture,
                                      const VideoProgress& progress);

}
