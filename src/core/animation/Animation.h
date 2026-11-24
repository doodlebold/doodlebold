#pragma once

#include "core/layers/LayerStack.h"

#include <QByteArray>
#include <QSize>
#include <QString>

#include <algorithm>

#include <memory>
#include <optional>
#include <set>
#include <vector>

namespace doodlebold {

// One frame of a track. Its own layers ("per-frame" layers) sit in the document's layer stack while it is its track's frame
// at the playhead; the other frames keep theirs here. A frame is shown for `hold` beats (the timeline's cells); a beat lasts
// 1/fps s. A frame of the video track also has the video's picture under its layers.
struct AnimationFrame {
    int id = 0;                     // stable while the frame lives, unique across tracks: selection and thumbnails follow it
    std::vector<LayerNode> layers;  // bottom → top; empty while its layers are in the stack
    LayerId current = kNoLayer;     // the layer that was current when the frame was left
    int hold = 1;
    int picture = -1;               // on the video track, the video's picture under it (VideoFrames::picture); -1: none

    static constexpr int kMaxHold = 999;
};

// A sound clip in the timeline's sound lane. The file's bytes are kept in the canvas (saved in its .doodle file), so the
// animation plays and exports the same wherever the file goes.
struct SoundClip {
    int id = 0;
    QString name;                             // the file name it came from, e.g. "a.wav"
    std::shared_ptr<const QByteArray> data;   // the file's bytes, as they were
    int startBeat = 0;                        // where its played part starts, in beats from the first frame
    bool muted = false;                       // shown, but not played or exported
    double seconds = 0.0;                     // the file's length
    double trimStart = 0.0;                   // seconds of the file left out at its start
    double trimEnd = 0.0;                     // and at its end
    int track = 0;                            // the id of the sound track it is on; clips on one track never overlap

    // What plays: the file's length less the trimmed parts.
    double playedSeconds() const { return std::max(0.0, seconds - trimStart - trimEnd); }
    double startSeconds(double fps) const { return startBeat / fps; }
    double endSeconds(double fps) const { return startSeconds(fps) + playedSeconds(); }
    bool operator==(const SoundClip& o) const {
        return id == o.id && name == o.name && data == o.data && startBeat == o.startBeat && seconds == o.seconds &&
               trimStart == o.trimStart && trimEnd == o.trimEnd && track == o.track && muted == o.muted;
    }
};

// A video under the frames, to draw over. The video file is not copied or changed: the canvas keeps its path, and its
// pictures are cached on disk (VideoFrames). Which picture is under which beat is up to the video track's frames, one per
// picture when the video is added; `startBeat` is where they start then.
struct VideoReference {
    QString path;
    QSize sourceSize;
    double sourceFps = 0.0;
    double seconds = 0.0;
    bool hasSound = false;
    double fps = 12.0;           // how many of its pictures a second are used: one per beat
    int startBeat = 0;           // the beat its first picture is under
    int opacity = 40;            // percent
    bool lossless = false;       // the cache keeps PNG pictures instead of JPEG

    int pictureCount() const;     // pictures at `fps` over its length
    // Whether the pictures are those of `o` (the same file read at the same rate), whatever its opacity.
    bool samePictures(const VideoReference& o) const { return path == o.path && fps == o.fps && seconds == o.seconds; }
    bool operator==(const VideoReference&) const = default;
};

// How the timeline shows and plays the animation. Changing these is not an undo step.
struct TimelineView {
    bool timelineShown = false;  // the canvas's timeline is open under it while it is the current canvas
    bool onionSkin = true;
    int onionBefore = 1;         // frames before the current one shown as onion skins
    int onionAfter = 1;
    int onionOpacity = 30;       // percent, for the nearest; farther ones fade
    std::set<int> collapsed;     // ids of the tracks folded to a thin row, so a canvas with many tracks stays manageable
    double speed = 1.0;          // Play's speed, times the frame rate: playback only, the frames and export keep their rate
    bool operator==(const TimelineView&) const = default;

    // The speeds the timeline's speed menu offers, shown as "1.0x": 0.25× to 2×,
    // the range video players' speed menus usually offer. Custom … takes any from kMinSpeed to kMaxSpeed (up
    // to ×10; a tenth, the least the field offers, still moves a 12 fps animation about a frame a second).
    static constexpr double kSpeeds[] = {0.25, 0.5, 0.75, 1.0, 1.5, 2.0};
    static constexpr double kMinSpeed = 0.1;
    static constexpr double kMaxSpeed = 10.0;

    // The onion skins' strength, percent, as the Settings dialog and the timeline's slider offer it: below 5 the neighbours
    // can't be told from the paper, above 80 they can't be told from the frame drawn on (the dialog's range since onion
    // skins came; the timeline's slider shares it).
    static constexpr int kMinOnionOpacity = 5;
    static constexpr int kMaxOnionOpacity = 80;
};

// What a track holds: frames to draw on, sound clips, or the video to draw over.
enum class TrackKind {
    Frames,
    Sound,
    Video
};

// A row of the timeline. A frames track: at every beat each shown one shows its frame for that beat, the upper tracks over
// the lower ones (a background on one, a character on another); past its last frame a track keeps showing that frame. A
// sound track holds clips (SoundClip::track). The video track is a frames track whose frames each show one of the video's
// pictures under their layers, drawn where the track lies among the others: its frames are chosen, moved, held, deleted and
// painted like any others. A hidden track shows and plays nothing: a frames track's layers stay out of the stack, a sound
// track's clips are silent, the video isn't drawn. A locked track's frames, clips or video can't be changed or painted.
struct AnimationTrack {
    int id = 0;
    QString name;
    TrackKind kind = TrackKind::Frames;
    bool hidden = false;
    bool locked = false;
    std::vector<AnimationFrame> frames;  // a frames track's: never empty; none for the others
    int current = 0;                     // the frame whose layers are in the stack; -1 while hidden
    std::vector<LayerId> layers;         // those layers: top-level ids, bottom → top
    int volume = kFullVolume;            // a sound track's loudness, percent of its clips' own: played and exported so
    int label = 0;                       // its label colour, to sort tracks by (1 … kLabels, the timeline's palette); 0: none

    // How many label colours a track can have (the timeline's palette, TrackLabels): eight, as many as can be told apart at a
    // glance in a row's strip.
    static constexpr int kLabels = 8;
    // Percent: a sound track's clips as loud as their files. The track's slider goes from silent up to this, not past it,
    // so a clip never plays louder than its file and can't clip (volume was asked for, not a gain).
    static constexpr int kFullVolume = 100;

    bool hasFrames() const { return kind != TrackKind::Sound; }  // a drawn track's or the video's
    bool isDrawn() const { return kind == TrackKind::Frames; }
    int frameCount() const { return static_cast<int>(frames.size()); }
    int holdOf(int frame) const;
    int startBeat(int frame) const;  // the first beat of `frame`
    int totalBeats() const;
    // The frame shown at `beat` (0-based); past the end, the last frame.
    int frameAtBeat(int beat) const;
    int indexOfFrame(int id) const;  // -1 if none
    bool owns(LayerId layer) const { return std::ranges::find(layers, layer) != layers.end(); }
};

// The animation of a canvas. A canvas without tracks is an ordinary picture: one frame, its layers all in the stack.
// The frame-by-index accessors are the current track's: the frames track the timeline's frame commands act on.
struct Animation {
    std::vector<AnimationTrack> tracks;   // bottom → top; empty: an ordinary picture
    int currentTrack = 0;                 // always a frames track
    int beat = 0;                         // the playhead
    std::vector<LayerId> shared;          // top-level layers shown in every frame, kept at the bottom of the stack
    double fps = 12.0;
    bool loop = true;
    int playStart = 0;                    // the first beat Play plays and loops from
    int playEnd = -1;                     // the last beat it plays (included); -1: the animation's last beat, as it grows
    std::vector<SoundClip> sounds;
    std::optional<VideoReference> video;  // with a video track
    TimelineView view;
    int nextFrameId = 1;
    int nextSoundId = 1;
    int nextTrackId = 1;

    static constexpr double kMinFps = 1.0;
    static constexpr double kMaxFps = 60.0;

    bool isAnimation() const { return tracks.size() > 1 || (tracks.size() == 1 && tracks.front().frames.size() > 1); }
    int trackCount() const { return static_cast<int>(tracks.size()); }
    const AnimationTrack* track() const { return tracks.empty() ? nullptr : &tracks[static_cast<std::size_t>(currentTrack)]; }
    int frameCount() const { return tracks.empty() ? 1 : track()->frameCount(); }
    // The current track's frame at the playhead.
    int currentFrame() const { return tracks.empty() ? 0 : track()->frameAtBeat(beat); }
    int holdOf(int frame) const { return tracks.empty() ? 1 : track()->holdOf(frame); }
    int startBeat(int frame) const { return tracks.empty() ? 0 : track()->startBeat(frame); }
    int totalBeats() const;  // the longest track's
    int frameAtBeat(int at) const { return tracks.empty() ? 0 : track()->frameAtBeat(at); }
    int indexOfFrame(int id) const { return tracks.empty() ? -1 : track()->indexOfFrame(id); }
    int frameId(int frame) const;
    int trackOfLayer(LayerId id) const;  // the track whose frame has it in the stack; -1 if none
    int indexOfTrack(int id) const;
    int videoTrack() const;  // its index; -1 without a video
    // The video's picture under `beat`: its track's frame there (past its end, the last frame, as any track); -1: none.
    int videoPictureAt(int atBeat) const;
    // Whether a clip plays: not muted, and its track shown.
    bool heard(const SoundClip& clip) const;
    // How loud a clip plays, 0 … 1 of its file: its sound track's volume.
    double gainOf(const SoundClip& clip) const;
    bool videoShown() const;
    // Whether the track holding the layer (by its top-level layer), the clip or the video is locked.
    bool lockedLayer(LayerId topLevel) const;
    bool lockedSound(const SoundClip& clip) const;
    bool isShared(LayerId id) const;
    double seconds() const { return totalBeats() / fps; }
    // The play range as beats inside the animation: first … last, both included.
    int playFirst() const;
    int playLast() const;
};

}
