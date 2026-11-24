#pragma once

#include <QByteArray>
#include <QImage>
#include <QRect>
#include <QSize>

#include <cstdint>
#include <memory>
#include <vector>

namespace doodlebold {

// What the composited picture looked like after every finished step, stored as only the 64 × 64 tiles that changed since the
// step before; the first step holds the whole picture. While a stroke is drawn a frame is taken every tenth of a second (a
// Partial step) and belongs to the step that ends the stroke. Pictures are 8-bit RGB as the canvas shows them.
inline constexpr int kTimelapseTileSize = 64;

struct TimelapseTile {
    std::int32_t x = 0;
    std::int32_t y = 0;
    QByteArray blob;
};

struct TimelapseStep {
    enum class Kind : std::uint8_t {
        First,  // the picture the recording began with (always step 0)
        Edit,   // a finished action: a stroke, fill, filter, transform, layer change …
        Undo,
        Redo,
        Partial,  // a frame taken while a stroke was being drawn; it belongs to the Edit that ends the stroke
    };
    Kind kind = Kind::Edit;
    bool full = false;     // the tiles hold the whole picture: the first step, and a step that changed the canvas size
    bool resumed = false;  // the first step recorded after the file was opened again: the undo history before it is gone
    bool strokeStart = false;  // a Partial step: the first frame of its stroke (the frames before it belong to another)
    std::uint32_t activeMs = 0;  // time spent painting up to and including this step (long pauses are cut short)
    QSize size;
    std::vector<TimelapseTile> tiles;
};
using TimelapseStepPtr = std::shared_ptr<const TimelapseStep>;

class Timelapse {
public:
    std::vector<TimelapseStepPtr> steps;
    bool paused = false;
    std::int64_t startedMs = 0;

    bool isEmpty() const { return steps.empty(); }
    // The finished steps after the first picture (frames taken mid-stroke are not steps): what "REC · 1 204 steps" counts.
    int stepCount() const;
    int editCount() const;
    int frameCount() const { return static_cast<int>(steps.size()); }
    std::int64_t byteSize() const;
    std::int64_t paintedMs() const { return steps.empty() ? 0 : steps.back()->activeMs; }
    QSize finalSize() const { return steps.empty() ? QSize() : steps.back()->size; }

    // Steps that make the timelapse, in order, as indexes into steps; the first picture is always first. A stroke's frames come
    // with the step that ends it; frames of a stroke that never became a step are not on it. leaveOutUndone: an undone, not
    // redone step is left out with its frames and the undo step that took it back; a step undone then redone stays once.
    // Without it every step is shown.
    std::vector<int> path(bool leaveOutUndone) const;
};

QSize timelapseTileSize(int tx, int ty, const QSize& canvas);

// A tile's stored pixels: one codec byte, then one colour (a tile of one colour) or the rows with each pixel less its left
// neighbour, compressed; the alpha is not kept.
QByteArray encodeTimelapseTile(const std::uint8_t* rgba, int stride, int width, int height);
// True if `blob` could be the stored pixels of a tile of this size (the header is checked, not the pixels).
bool isTimelapseBlob(const QByteArray& blob, int width, int height);
bool decodeTimelapseTile(const QByteArray& blob, int width, int height, std::uint8_t* rgba, int stride);

// The frame rate of the video: one picture of the path per frame, as Procreate does.
inline constexpr int kTimelapseFps = 30;
enum class TimelapseLength {
    Full,
    Seconds30
};
inline constexpr int kTimelapseShortSeconds = 30;
int timelapseFrameCount(TimelapseLength length, int pathSteps, int fps = kTimelapseFps);
// `canvas` scaled down to fit in `box` (never up), keeping its shape, with even sides for 4:2:0 video.
QSize fitTimelapseSize(const QSize& canvas, const QSize& box = QSize(1920, 1080));
// `frames` indexes into `path`'s values: the first and last stay first and last; with fewer steps than frames each is held for
std::vector<int> resampleTimelapse(const std::vector<int>& path, int frames);

}
