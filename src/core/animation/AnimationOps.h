#pragma once

#include "core/document/DocumentState.h"
#include "core/document/EditError.h"

#include <QColor>
#include <QImage>

#include <functional>
#include <optional>
#include <vector>

// Frame and track operations as pure functions on a DocumentState, like layer_ops: Document applies them to a copy of its state
// and records one undo step. At the playhead every shown track has its frame's own layers in the stack, as top-level layers:
// the shared ones at the bottom, then each track's, the lowest track first. Moving the playhead swaps them in and out, so
// painting a frame is painting layers of the stack, the same as on a picture. The frame commands act on the current track.
namespace doodlebold::animation_ops {

// A picture becomes an animation of one track of one frame (nothing changes if it is one already).
void ensureFrames(DocumentState& s);
// Keeps the shared layers at the bottom of the stack, then each track's layers in the tracks' order; a new top-level layer
// joins the track of the layer under it (or over it). Forgets ids that are no longer top-level layers. Document calls it
// after every edit.
void normalize(DocumentState& s);
// Choosing a layer chooses its track as the current one, and its frame remembers it. Not an undo step.
void followLayer(DocumentState& s, LayerId id);

// Not undo steps on their own (Document installs them directly); NoChange when nothing moves.
// The current track's frame: the playhead goes to its first beat (it stays if it is inside the frame already).
EditResult<> goTo(DocumentState& s, int frame);
EditResult<> goToBeat(DocumentState& s, int beat);
// A frame of any track: the track becomes current and its frame's layer the current layer (when the track is shown).
EditResult<> goToCell(DocumentState& s, int track, int frame);

// New Frame: after the current track's frame at the playhead, with empty layers named and set like that frame's own (one empty
// layer if it has none); the playhead goes to it. Returns its index.
EditResult<int> newFrame(DocumentState& s);
// Copies of the current track's frames (in their order) after the last of them; the playhead goes to the copy of the frame
// it was on, or else to the first copy. Returns its index.
EditResult<int> duplicateFrames(DocumentState& s, std::vector<int> frames);
// LastFrame if the track would have none left. The playhead's frame, if deleted, is replaced by the next one left (or the last).
EditResult<> deleteFrames(DocumentState& s, std::vector<int> frames);
// Keep Only This Frame: what the playhead shows stays (every shown track's frame, and the shared layers) as ordinary layers;
// the other frames, the tracks, the sound clips and video go. The canvas is an ordinary picture again. NoChange on a picture.
EditResult<> keepOnly(DocumentState& s);
// Hold More / Hold Less: each frame's hold by `delta` beats, kept in 1 … kMaxHold. NoChange if none changed.
EditResult<> changeHold(DocumentState& s, std::vector<int> frames, int delta);
EditResult<> setHold(DocumentState& s, int frame, int hold);
// Hold For: every one of the frames for `hold` beats (kept in 1 … kMaxHold). NoChange if all are already.
EditResult<> setHolds(DocumentState& s, std::vector<int> frames, int hold);
// Moves the frames, keeping their order, so that they start at index `to` among the frames that stay.
EditResult<> moveFrames(DocumentState& s, std::vector<int> frames, int to);
// Frames of several tracks moved together, each track's as moveFrames does (a track by index, a frames or the video track).
// The playhead stays on the current track's frame. TrackLocked if any of the tracks is locked (then none moves); NoChange if
// none moves.
struct FrameMove {
    int track = 0;
    std::vector<int> frames;
    int to = 0;
};
EditResult<> moveFrameGroups(DocumentState& s, const std::vector<FrameMove>& moves);

// + Track: a frames track above the current track, one frame with one empty layer; it becomes current. Returns its index.
EditResult<int> addTrack(DocumentState& s);
// Duplicate Track: a drawn track's copy above it, every frame's layers copied (new ids, the same pixels) with its holds; it
// becomes current, unlocked. Returns its index. NoSuchTrack for a sound or the video track.
EditResult<int> duplicateTrack(DocumentState& s, int track);
// LastTrack for the only frames track; LastShownTrack for the only shown one. A sound track goes with its clips, the video
// track with the video.
EditResult<> deleteTrack(DocumentState& s, int track);
// Several tracks at once (any order); refused whole when any one can't go (deleteTrack's reasons).
EditResult<> deleteTracks(DocumentState& s, std::vector<int> tracks);
// A track's label colour, 0 (none) … AnimationTrack::kLabels. NoSuchTrack outside that.
EditResult<> setTrackLabel(DocumentState& s, int track, int label);
// Clear Track: every frame goes and no hold is left: one empty frame of one beat, with layers named and set like its frame at
// the playhead. NoChange if it is one empty frame of one beat already. A sound track's clips go.
EditResult<> clearTrack(DocumentState& s, int track);
EditResult<> renameTrack(DocumentState& s, int track, const QString& name);
// To index `to` among the tracks (0: the bottom one).
EditResult<> moveTrack(DocumentState& s, int track, int to);
// A hidden track shows and plays nothing, and is left out of Export Video. LastShownTrack for the only shown frames track.
EditResult<> setTrackHidden(DocumentState& s, int track, bool hidden);
// A locked track's frames, clips or video can't be changed, nor its layers painted (TrackLocked).
EditResult<> setTrackLocked(DocumentState& s, int track, bool locked);
// A sound track's volume, 0 … AnimationTrack::kFullVolume percent (clamped). NoSuchTrack for another kind; TrackLocked when
// locked.
EditResult<> setTrackVolume(DocumentState& s, int track, int volume);
// Del in the timeline: the drawings on the current track's frames go, the frames and their holds stay. Every frame of the
// track: Clear Track. On the video track their pictures go too, and Clear Track empties every frame the same way.
EditResult<> clearFrames(DocumentState& s, std::vector<int> frames);

// Shared: a top-level layer shown in every frame of every track (one inside a folder moves out to the top level). Per-frame
// again: it belongs to the current track, and every other frame of that track keeps a copy of it as its own bottom layer, so
// no frame changes.
EditResult<> setShared(DocumentState& s, LayerId id, bool shared);
EditResult<> setPlayback(DocumentState& s, double fps, bool loop);
// The beats Play plays and loops over, start … end, both included; end -1: to the last beat, however long the animation
// grows. Kept within the animation.
EditResult<> setPlayRange(DocumentState& s, int start, int end);
// On a sound track of its own, named after it, at the bottom of the tracks, at its startBeat. Returns its id.
EditResult<int> addSound(DocumentState& s, SoundClip clip);
// To `startBeat` on the sound track `track` (by id) if it fits there as it is; else, or onto any other kind of track, it stays
// on its own track, where a clip in the way makes it go right after or right before that clip, whichever is nearer; NoChange
// if neither has room.
EditResult<> moveSound(DocumentState& s, int id, int startBeat, int track);
// Several clips moved together: each to its start beat on its sound track (by id), all or none. NoChange if any would start
// before the first beat, leave the sound tracks, be on a locked track or overlap another clip, or if none moves.
struct SoundPlace {
    int id = 0;
    int startBeat = 0;
    int track = 0;
};
EditResult<> placeSounds(DocumentState& s, const std::vector<SoundPlace>& places);
// Trims a clip: the seconds left out at its start and end, and where its played part starts. NoChange when it would play
// less than a beat or overlap another clip on its row.
EditResult<> trimSound(DocumentState& s, int id, int startBeat, double trimStart, double trimEnd);
// A muted clip, or every clip of a hidden sound track, is neither played nor exported.
EditResult<> setSoundMuted(DocumentState& s, int id, bool muted);
EditResult<> removeSound(DocumentState& s, int id);
// Every one of the clips, one undo step; TrackLocked if any is on a locked track (then none goes).
EditResult<> removeSounds(DocumentState& s, const std::vector<int>& ids);
// Adds (with a video track at the bottom, a frame for each picture from its startBeat), replaces or (nullopt) removes the
// video under the frames. The same file at the same rate keeps the track's frames (an opacity change); another makes them
// again.
EditResult<> setVideo(DocumentState& s, std::optional<VideoReference> video);
// Add Video with "Drawings follow the video": the frame rate becomes the video's and the current track's frames grow, one
// beat each, until they reach its last picture. The frames already there keep their places.
EditResult<> followVideo(DocumentState& s, const VideoReference& video);

// A canvas-wide edit (flip, rotate, trim) on the frames whose layers aren't in the stack, with the selection and size the
// stack has before the edit; then on the stack itself.
EditResult<> onEveryFrame(DocumentState& s, const std::function<EditResult<>(DocumentState&)>& op);

// The layers that make up the picture at `beat`: the shared ones and every shown track's frame there, as a stack.
LayerStack beatStack(const DocumentState& s, int beat);
// The same at the first beat of the current track's `frame`.
LayerStack frameStack(const DocumentState& s, int frame);
// A track's frame's own layers (the stack's for the frame at the playhead).
std::vector<const LayerNode*> ownLayers(const DocumentState& s, int track, int frame);
std::vector<const LayerNode*> ownLayers(const DocumentState& s, int frame);  // the current track's
bool frameHasDrawing(const DocumentState& s, int track, int frame);
bool frameHasDrawing(const DocumentState& s, int frame);
// The picture at `beat` as a video shows it: composited onto the canvas's paper colour, opaque (a transparent canvas's paper
// is its light or dark backdrop colour). Safe on any thread with a copy of the state.
QImage exportedBeat(const DocumentState& s, int beat);

// What the canvas shows besides the stack: the video picture (canvas sized; null: none) at the bottom, and the onion skins of
// the current track's neighbouring frames, tinted kOnionBefore / kOnionAfter, right under that track's layers.
inline const QColor kOnionBefore{226, 72, 72};
inline const QColor kOnionAfter{56, 126, 236};
std::vector<LayerNode> onionSkins(const DocumentState& s);
LayerNode videoNode(const QImage& picture, int opacity);
LayerNode videoNode(const TiledImage& picture, int opacity);  // read already (VideoPictureLoader)
// The stack with the underlay nodes in place: `video` (if any) at the bottom, `onions` right under the current track's layers.
LayerStack displayStack(const DocumentState& s, const std::optional<LayerNode>& video, const std::vector<LayerNode>& onions);

}
