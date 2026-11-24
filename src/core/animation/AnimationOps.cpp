#include "core/animation/AnimationOps.h"

#include "core/layers/Compositor.h"

#include <algorithm>
#include <functional>
#include <cmath>
#include <map>
#include <memory>
#include <mutex>
#include <numeric>
#include <ranges>
#include <set>

namespace doodlebold::animation_ops {

namespace {

using Failure = EditResult<>;

void reassignIds(LayerStack& stack, LayerNode& n, std::map<LayerId, LayerId>* renamed = nullptr) {
    const LayerId old = n.props.id;
    n.props.id = stack.allocateId();
    if (renamed)
        (*renamed)[old] = n.props.id;
    for (LayerNode& c : n.children)
        reassignIds(stack, c, renamed);
}

// The same layer with nothing drawn on it: its name and settings, no pixels, mask or text.
LayerNode blankCopy(LayerStack& stack, const LayerNode& n, std::map<LayerId, LayerId>& renamed) {
    LayerNode out;
    out.props = n.props;
    if (out.props.kind == LayerKind::Text)
        out.props.kind = LayerKind::Raster;
    out.props.id = stack.allocateId();
    renamed[n.props.id] = out.props.id;
    out.pixels = TiledImage(stack.size());
    for (const LayerNode& c : n.children)
        out.children.push_back(blankCopy(stack, c, renamed));
    return out;
}

LayerNode deepCopy(LayerStack& stack, const LayerNode& n, std::map<LayerId, LayerId>& renamed) {
    LayerNode out = n;  // shares tiles until either side is painted
    reassignIds(stack, out, &renamed);
    return out;
}

AnimationFrame makeFrame(Animation& a) {
    AnimationFrame f;
    f.id = a.nextFrameId++;
    return f;
}

bool overlaps(const SoundClip& a, const SoundClip& b, double fps) {
    constexpr double kTouching = 1e-6;  // one clip ending where the next starts
    return a.startSeconds(fps) < b.endSeconds(fps) - kTouching && b.startSeconds(fps) < a.endSeconds(fps) - kTouching;
}

// Whether `clip` overlaps no other clip on its track.
bool fits(const Animation& a, const SoundClip& clip) {
    return std::ranges::none_of(a.sounds,
                                [&](const SoundClip& o) { return o.id != clip.id && o.track == clip.track && overlaps(o, clip, a.fps); });
}

SoundClip* findSound(Animation& a, int id) {
    const auto it = std::ranges::find_if(a.sounds, [id](const SoundClip& c) { return c.id == id; });
    return it != a.sounds.end() ? &*it : nullptr;
}

// The beat nearest `beat` where `clip` fits on its track: `beat` itself, or right after or right before a clip in the way.
std::optional<int> nearestFreeBeat(const Animation& a, SoundClip clip, int beat) {
    std::vector<int> tries{beat};
    const double length = clip.playedSeconds() * a.fps;
    for (const SoundClip& o : a.sounds) {
        if (o.id == clip.id || o.track != clip.track)
            continue;
        tries.push_back(static_cast<int>(std::ceil(o.endSeconds(a.fps) * a.fps - 1e-6)));
        tries.push_back(static_cast<int>(std::floor(o.startBeat - length + 1e-6)));
    }
    std::ranges::stable_sort(tries, [beat](int x, int y) { return std::abs(x - beat) < std::abs(y - beat); });
    for (int t : tries) {
        clip.startBeat = t;
        if (t >= 0 && fits(a, clip))
            return t;
    }
    return std::nullopt;
}

// A clip that overlaps an earlier one on its track (after the frame rate changed) goes to a new sound track over that one.
void separateSounds(Animation& a) {
    for (std::size_t i = 0; i < a.sounds.size(); ++i) {
        const SoundClip& clip = a.sounds[i];
        const auto inTheWay = [&](const SoundClip& o) { return o.track == clip.track && overlaps(o, clip, a.fps); };
        if (std::none_of(a.sounds.begin(), a.sounds.begin() + std::ptrdiff_t(i), inTheWay))
            continue;
        AnimationTrack t;
        t.id = a.nextTrackId++;
        t.kind = TrackKind::Sound;
        t.name = clip.name;
        t.current = -1;
        const int below = a.indexOfTrack(clip.track);
        a.tracks.insert(a.tracks.begin() + below + 1, std::move(t));
        if (a.currentTrack > below)
            ++a.currentTrack;
        a.sounds[i].track = a.tracks[static_cast<std::size_t>(below + 1)].id;
    }
}

bool hasDrawing(const LayerNode& n) {
    if (n.isFolder())
        return std::ranges::any_of(n.children, [](const LayerNode& c) { return hasDrawing(c); });
    return !n.pixels.isEmpty();
}

// One tile of `color`, made once, so the onion skins built again for the same frames are the same tiles.
TilePtr colourTile(const QColor& color) {
    static std::mutex mutex;
    static std::map<QRgb, TilePtr> tiles;
    const std::scoped_lock lock(mutex);
    TilePtr& tile = tiles[color.rgba()];
    if (!tile) {
        TiledImage one(QSize(Tile::kSize, Tile::kSize));
        one.fill(color);
        tile = one.tileShared(0, 0);
    }
    return tile;
}

// One tile of `color`, shared by every tile of the canvas: a full layer that costs one tile.
TiledImage flatColor(const QSize& size, const QColor& color) {
    const TilePtr tile = colourTile(color);
    TiledImage out(size);
    for (int ty = 0; ty < out.tilesY(); ++ty)
        for (int tx = 0; tx < out.tilesX(); ++tx)
            out.setTile(tx, ty, tile);
    return out;
}

LayerNode folderNode(const QString& name, int opacity, std::vector<LayerNode> children) {
    LayerNode n;
    n.props.kind = LayerKind::Folder;
    n.props.name = name;
    n.props.mode = BlendMode::Normal;
    n.props.opacity = opacity;
    n.children = std::move(children);
    return n;
}

LayerNode onionOf(const DocumentState& s, int frame, int opacity, const QColor& tint) {
    std::vector<LayerNode> own;
    for (const LayerNode* n : ownLayers(s, s.animation.currentTrack, frame))
        own.push_back(*n);
    LayerNode drawing = folderNode(QStringLiteral("frame"), 100, std::move(own));
    LayerNode colour;
    colour.props.name = QStringLiteral("tint");
    colour.props.clipping = true;
    colour.props.opacity = 75;
    colour.pixels = flatColor(s.layers.size(), tint);
    std::vector<LayerNode> parts;
    parts.push_back(std::move(drawing));
    parts.push_back(std::move(colour));
    return folderNode(QStringLiteral("onion"), opacity, std::move(parts));
}

std::vector<LayerNode>& topLevel(DocumentState& s) {
    return s.layers.root().children;
}

AnimationTrack& currentTrack(DocumentState& s) {
    return s.animation.tracks[static_cast<std::size_t>(s.animation.currentTrack)];
}

bool validTrack(const DocumentState& s, int track) {
    return track >= 0 && track < s.animation.trackCount();
}

bool currentTrackLocked(const DocumentState& s) {
    return !s.animation.tracks.empty() && s.animation.tracks[static_cast<std::size_t>(s.animation.currentTrack)].locked;
}

bool validFrame(const DocumentState& s, int frame) {
    return frame >= 0 && frame < s.animation.frameCount();
}

// The current track's frames named, in order, without repeats or ones outside it.
std::vector<int> cleaned(const DocumentState& s, std::vector<int> frames) {
    std::ranges::sort(frames);
    frames.erase(std::ranges::unique(frames).begin(), frames.end());
    std::erase_if(frames, [&](int f) { return !validFrame(s, f); });
    return frames;
}

bool contains(const LayerNode& n, LayerId id) {
    return n.props.id == id || std::ranges::any_of(n.children, [id](const LayerNode& c) { return contains(c, id); });
}

// Drawn tracks shown: at least one stays (the video's doesn't count).
int shownTracks(const Animation& a) {
    return static_cast<int>(
        std::count_if(a.tracks.begin(), a.tracks.end(), [](const AnimationTrack& t) { return t.isDrawn() && !t.hidden; }));
}

int framesTracks(const Animation& a) {
    return static_cast<int>(std::count_if(a.tracks.begin(), a.tracks.end(), [](const AnimationTrack& t) { return t.isDrawn(); }));
}

bool isFramesTrack(const DocumentState& s, int track) {
    return validTrack(s, track) && s.animation.tracks[static_cast<std::size_t>(track)].hasFrames();
}

// The frames track nearest `track` (itself if it is one), for the current track.
int nearestFramesTrack(const Animation& a, int track) {
    for (int d = 0; d < a.trackCount(); ++d)
        for (int t : {track - d, track + d})
            if (t >= 0 && t < a.trackCount() && a.tracks[static_cast<std::size_t>(t)].hasFrames())
                return t;
    return 0;
}

EditResult<> unlessLocked(const DocumentState& s) {
    return currentTrackLocked(s) ? Failure::failure(EditError::TrackLocked) : Failure{};
}

// The layer a video frame is given to paint on while it is at the playhead, as it was made: nothing on it, nothing set.
bool untouchedLayer(const LayerNode& n) {
    LayerProps plain;
    plain.id = n.props.id;
    plain.name = n.props.name;
    return n.props == plain && n.pixels.isEmpty() && n.children.empty() && !n.mask && !n.text;
}

// Moves a track's layers out of the stack into its frame's entry, remembering which of them was current. A video frame's lone
// layer left untouched goes: the thousands of a video's frames keep no layers until they are painted on.
void stashTrack(DocumentState& s, AnimationTrack& t) {
    if (t.current < 0)
        return;
    AnimationFrame& f = t.frames[static_cast<std::size_t>(t.current)];
    std::vector<LayerNode>& kids = topLevel(s);
    const auto own = std::ranges::stable_partition(kids, [&](const LayerNode& n) { return !t.owns(n.props.id); }).begin();
    if (std::any_of(own, kids.end(), [&](const LayerNode& n) { return contains(n, s.current); }))
        f.current = s.current;
    f.layers.assign(std::make_move_iterator(own), std::make_move_iterator(kids.end()));
    kids.erase(own, kids.end());
    if (t.kind == TrackKind::Video && f.layers.size() == 1 && untouchedLayer(f.layers.front())) {
        f.layers.clear();
        f.current = kNoLayer;
    }
    t.layers.clear();
    t.current = -1;
}

// Puts a track's frame's layers into the stack (where normalize's order puts them). A video frame without layers is given an
// empty one, so painting there paints on it.
void loadTrack(DocumentState& s, AnimationTrack& t, int frame) {
    t.current = frame;
    AnimationFrame& f = t.frames[static_cast<std::size_t>(frame)];
    if (t.kind == TrackKind::Video && f.layers.empty()) {
        f.layers.push_back(s.layers.makeRaster(s.layers.nextLayerName()));
        f.current = f.layers.back().props.id;
    }
    for (LayerNode& n : f.layers) {
        t.layers.push_back(n.props.id);
        topLevel(s).push_back(std::move(n));
    }
    f.layers.clear();
}

// A shown track has its frame at the playhead in the stack, a hidden one none.
void installTrack(DocumentState& s, AnimationTrack& t) {
    const int wanted = t.hidden ? -1 : t.frameAtBeat(s.animation.beat);
    if (t.current == wanted)
        return;
    stashTrack(s, t);
    if (wanted >= 0)
        loadTrack(s, t, wanted);
}

// Shared layers at the bottom, then each track's, the lowest track first.
void arrange(DocumentState& s) {
    const Animation& a = s.animation;
    const auto rank = [&](const LayerNode& n) { return a.isShared(n.props.id) ? -1 : a.trackOfLayer(n.props.id); };
    std::stable_sort(topLevel(s).begin(), topLevel(s).end(), [&](const LayerNode& x, const LayerNode& y) { return rank(x) < rank(y); });
}

// The layer a track's frame had current; its top layer if that one is gone.
LayerId rememberedLayer(const DocumentState& s, const AnimationTrack& t) {
    if (t.current < 0)
        return kNoLayer;
    const LayerId remembered = t.frames[static_cast<std::size_t>(t.current)].current;
    const bool mine = std::ranges::any_of(t.layers, [&](LayerId top) {
        const LayerNode* n = s.layers.find(top);
        return n && contains(*n, remembered);
    });
    return mine ? remembered : t.layers.empty() ? kNoLayer : t.layers.back();
}

void keepMaskTarget(DocumentState& s) {
    if (const LayerNode* n = s.layers.find(s.current); !n || !n->mask)
        s.maskTarget = false;
}

// The current track's layer becomes the current layer, when the track is shown and has one.
void chooseTrackLayer(DocumentState& s) {
    if (const LayerId id = rememberedLayer(s, currentTrack(s)); id != kNoLayer)
        s.current = id;
    keepMaskTarget(s);
}

// A current layer that left the stack is replaced: by the current track's, else by the top layer.
void keepCurrentLayer(DocumentState& s) {
    if (s.layers.find(s.current))
        return;
    s.current = rememberedLayer(s, currentTrack(s));
    if (s.current == kNoLayer && !topLevel(s).empty())
        s.current = topLevel(s).back().props.id;
    keepMaskTarget(s);
}

// Every shown track puts its frame at the playhead into the stack; a hidden one none.
void install(DocumentState& s) {
    for (AnimationTrack& t : s.animation.tracks)
        if (t.hasFrames())
            installTrack(s, t);
    arrange(s);
    keepCurrentLayer(s);
}

// The track a new top-level layer joins: the one of the nearest track layer under it, else over it, else the current track
// (or the first shown one).
int trackForNewLayer(const DocumentState& s, std::size_t at) {
    const Animation& a = s.animation;
    const std::vector<LayerNode>& kids = s.layers.root().children;
    for (std::size_t i = at; i-- > 0;)
        if (const int t = a.trackOfLayer(kids[i].props.id); t >= 0)
            return t;
    for (std::size_t i = at + 1; i < kids.size(); ++i)
        if (const int t = a.trackOfLayer(kids[i].props.id); t >= 0)
            return t;
    if (!a.tracks[static_cast<std::size_t>(a.currentTrack)].hidden)
        return a.currentTrack;
    for (std::size_t i = 0; i < a.tracks.size(); ++i)
        if (a.tracks[i].hasFrames() && !a.tracks[i].hidden)
            return static_cast<int>(i);
    return a.currentTrack;
}

// The ids a track has in the stack, in the stack's order.
void reorderTrackLayers(DocumentState& s) {
    for (AnimationTrack& t : s.animation.tracks) {
        std::vector<LayerId> ordered;
        for (const LayerNode& n : topLevel(s))
            if (t.owns(n.props.id))
                ordered.push_back(n.props.id);
        t.layers = std::move(ordered);
    }
}

// The track's frame that a frame edit leaves the playhead on: the same frame (by id) if it is still there.
void stayOnFrame(DocumentState& s, int frameId) {
    const int at = currentTrack(s).indexOfFrame(frameId);
    if (at >= 0 && currentTrack(s).frameAtBeat(s.animation.beat) != at)
        s.animation.beat = currentTrack(s).startBeat(at);
}

AnimationTrack makeTrack(DocumentState& s) {
    Animation& a = s.animation;
    AnimationTrack t;
    t.id = a.nextTrackId++;
    t.name = QStringLiteral("Track %1").arg(t.id);
    t.current = -1;
    return t;
}

// The current track's frame `frame`'s layers, copied (deep: new ids) or blank.
std::vector<LayerNode> copiedLayers(DocumentState& s, int frame, bool blank, LayerId& current) {
    const AnimationTrack& t = currentTrack(s);
    std::map<LayerId, LayerId> renamed;
    std::vector<LayerNode> out;
    for (const LayerNode* n : ownLayers(s, s.animation.currentTrack, frame))
        out.push_back(blank ? blankCopy(s.layers, *n, renamed) : deepCopy(s.layers, *n, renamed));
    const LayerId was = t.current == frame ? rememberedLayer(s, t) : t.frames[static_cast<std::size_t>(frame)].current;
    const auto it = renamed.find(was);
    current = it != renamed.end() ? it->second : out.empty() ? kNoLayer : out.back().props.id;
    return out;
}

}

void ensureFrames(DocumentState& s) {
    Animation& a = s.animation;
    if (!a.tracks.empty())
        return;
    AnimationTrack t = makeTrack(s);
    AnimationFrame f = makeFrame(a);
    f.current = s.current;
    t.frames.push_back(std::move(f));
    t.current = 0;
    for (const LayerNode& n : s.layers.root().children)
        if (!a.isShared(n.props.id))
            t.layers.push_back(n.props.id);
    a.tracks.push_back(std::move(t));
    a.currentTrack = 0;
    a.beat = 0;
}

namespace {

// The video track's frames as the video is added: one a picture, from its startBeat (empty frames before it).
void makeVideoFrames(Animation& a, AnimationTrack& t) {
    const VideoReference& v = *a.video;
    for (int lead = v.startBeat; lead > 0; lead -= AnimationFrame::kMaxHold) {
        AnimationFrame f = makeFrame(a);
        f.hold = std::min(lead, AnimationFrame::kMaxHold);
        t.frames.push_back(std::move(f));
    }
    for (int i = 0; i < v.pictureCount(); ++i) {
        AnimationFrame f = makeFrame(a);
        f.picture = i;
        t.frames.push_back(std::move(f));
    }
    if (t.frames.empty())
        t.frames.push_back(makeFrame(a));
}

// The video has its track, with a frame for each of its pictures, and every clip a sound track: files from before sound and
// video were tracks, or before the video's pictures were frames, and the edits that add or remove them, end up the same.
void keepMediaTracks(DocumentState& s) {
    Animation& a = s.animation;
    const int video = a.videoTrack();
    if (a.video && video < 0) {
        AnimationTrack t;
        t.id = a.nextTrackId++;
        t.kind = TrackKind::Video;
        t.name = QStringLiteral("Video");
        t.current = -1;
        a.tracks.insert(a.tracks.begin(), std::move(t));
        ++a.currentTrack;
    } else if (!a.video && video >= 0) {
        stashTrack(s, a.tracks[static_cast<std::size_t>(video)]);
        a.tracks.erase(a.tracks.begin() + video);
        if (a.currentTrack > video)
            --a.currentTrack;
    }
    if (const int t = a.videoTrack(); t >= 0 && a.tracks[static_cast<std::size_t>(t)].frames.empty()) {
        AnimationTrack& track = a.tracks[static_cast<std::size_t>(t)];
        track.layers.clear();
        track.current = -1;
        makeVideoFrames(a, track);
        installTrack(s, track);
    }
    for (SoundClip& c : a.sounds) {
        const int t = a.indexOfTrack(c.track);
        if (t >= 0 && a.tracks[static_cast<std::size_t>(t)].kind == TrackKind::Sound)
            continue;
        AnimationTrack sound;
        sound.id = a.nextTrackId++;
        sound.kind = TrackKind::Sound;
        sound.name = c.name;
        sound.current = -1;
        c.track = sound.id;
        a.tracks.push_back(std::move(sound));
    }
}

}

// The frame in the stack that holds the current layer remembers it at once, not only when the frame leaves the stack. Its
// track's frame may stay in the stack while another track's layer is used, and a new frame copies the remembered layer:
// with the record left from when the frame came in, going back to that track or making a frame picked its top layer
// instead of the one in use (feedback: "It always resets to the first layer").
void noteCurrentLayer(DocumentState& s) {
    for (const LayerNode& n : s.layers.root().children) {
        if (!contains(n, s.current))
            continue;
        if (const int t = s.animation.trackOfLayer(n.props.id); t >= 0) {
            AnimationTrack& track = s.animation.tracks[static_cast<std::size_t>(t)];
            if (track.current >= 0)
                track.frames[static_cast<std::size_t>(track.current)].current = s.current;
        }
        return;
    }
}

void normalize(DocumentState& s) {
    Animation& a = s.animation;
    if (a.tracks.empty()) {
        a.shared.clear();
        return;
    }
    keepMediaTracks(s);
    a.currentTrack = nearestFramesTrack(a, std::clamp(a.currentTrack, 0, a.trackCount() - 1));
    const std::vector<LayerNode>& kids = topLevel(s);
    const auto isTop = [&](LayerId id) { return std::ranges::any_of(kids, [id](const LayerNode& n) { return n.props.id == id; }); };
    std::erase_if(a.shared, [&](LayerId id) { return !isTop(id); });
    for (AnimationTrack& t : a.tracks)
        std::erase_if(t.layers, [&](LayerId id) { return !isTop(id); });
    for (std::size_t i = 0; i < kids.size(); ++i) {
        const LayerId id = kids[i].props.id;
        if (!a.isShared(id) && a.trackOfLayer(id) < 0)
            a.tracks[static_cast<std::size_t>(trackForNewLayer(s, i))].layers.push_back(id);
    }
    arrange(s);
    reorderTrackLayers(s);
    noteCurrentLayer(s);
}

void followLayer(DocumentState& s, LayerId id) {
    for (const LayerNode& n : s.layers.root().children)
        if (contains(n, id))
            if (const int t = s.animation.trackOfLayer(n.props.id); t >= 0)
                s.animation.currentTrack = t;
    noteCurrentLayer(s);
}

// Whether the current layer is one shared by every frame (or inside one). The playhead moving leaves it current: it is in the
// stack at every beat, and choosing the new frame's layer instead lost it on every frame change (feedback: "If i have
// Layer 2 selected, then move to another frame and back, Layer 2 will no longer be selected").
bool usingSharedLayer(const DocumentState& s) {
    for (const LayerNode& n : s.layers.root().children)
        if (contains(n, s.current))
            return s.animation.isShared(n.props.id);
    return false;
}

EditResult<> goTo(DocumentState& s, int frame) {
    if (!validFrame(s, frame))
        return Failure::failure(EditError::NoSuchFrame);
    if (s.animation.tracks.empty() || frame == s.animation.currentFrame())
        return Failure::failure(EditError::NoChange);
    s.animation.beat = currentTrack(s).startBeat(frame);
    const bool shared = usingSharedLayer(s);
    install(s);
    if (!shared)
        chooseTrackLayer(s);
    return {};
}

EditResult<> goToBeat(DocumentState& s, int beat) {
    beat = std::max(0, beat);
    if (s.animation.tracks.empty() || beat == s.animation.beat)
        return Failure::failure(EditError::NoChange);
    const int before = s.animation.currentFrame();
    const bool shared = usingSharedLayer(s);
    s.animation.beat = beat;
    install(s);
    if (s.animation.currentFrame() != before && !shared)
        chooseTrackLayer(s);
    return {};
}

EditResult<> goToCell(DocumentState& s, int track, int frame) {
    if (!isFramesTrack(s, track) || frame < 0 || frame >= s.animation.tracks[static_cast<std::size_t>(track)].frameCount())
        return Failure::failure(EditError::NoSuchFrame);
    Animation& a = s.animation;
    const LayerId wasLayer = s.current;
    const int wasTrack = a.currentTrack, wasBeat = a.beat;
    a.currentTrack = track;
    if (currentTrack(s).frameAtBeat(a.beat) != frame)
        a.beat = currentTrack(s).startBeat(frame);
    const bool shared = usingSharedLayer(s);
    install(s);
    if (a.trackOfLayer(s.current) != track && !shared)
        chooseTrackLayer(s);
    const bool same = a.currentTrack == wasTrack && a.beat == wasBeat && s.current == wasLayer;
    return same ? Failure::failure(EditError::NoChange) : Failure{};
}

EditResult<int> newFrame(DocumentState& s) {
    ensureFrames(s);
    if (currentTrackLocked(s))
        return EditResult<int>::failure(EditError::TrackLocked);
    Animation& a = s.animation;
    const int from = a.currentFrame();
    AnimationFrame f = makeFrame(a);
    f.layers = copiedLayers(s, from, true, f.current);
    if (f.layers.empty()) {
        f.layers.push_back(s.layers.makeRaster(s.layers.nextLayerName()));
        f.current = f.layers.back().props.id;
    }
    AnimationTrack& t = currentTrack(s);
    stashTrack(s, t);
    t.frames.insert(t.frames.begin() + from + 1, std::move(f));
    a.beat = t.startBeat(from + 1);
    install(s);
    chooseTrackLayer(s);
    return from + 1;
}

EditResult<int> duplicateFrames(DocumentState& s, std::vector<int> frames) {
    ensureFrames(s);
    if (currentTrackLocked(s))
        return EditResult<int>::failure(EditError::TrackLocked);
    frames = cleaned(s, std::move(frames));
    if (frames.empty())
        return EditResult<int>::failure(EditError::NoSuchFrame);
    Animation& a = s.animation;
    const int wasCurrent = a.currentFrame();
    std::vector<AnimationFrame> copies;
    int target = 0;
    for (std::size_t i = 0; i < frames.size(); ++i) {
        AnimationFrame f = makeFrame(a);
        f.hold = currentTrack(s).holdOf(frames[i]);
        f.picture = currentTrack(s).frames[static_cast<std::size_t>(frames[i])].picture;
        f.layers = copiedLayers(s, frames[i], false, f.current);
        if (frames[i] == wasCurrent)
            target = static_cast<int>(i);
        copies.push_back(std::move(f));
    }
    AnimationTrack& t = currentTrack(s);
    stashTrack(s, t);
    const int at = frames.back() + 1;
    t.frames.insert(t.frames.begin() + at, std::make_move_iterator(copies.begin()), std::make_move_iterator(copies.end()));
    a.beat = t.startBeat(at + target);
    install(s);
    chooseTrackLayer(s);
    return at + target;
}

EditResult<> deleteFrames(DocumentState& s, std::vector<int> frames) {
    if (auto locked = unlessLocked(s); !locked)
        return locked;
    frames = cleaned(s, std::move(frames));
    if (frames.empty())
        return Failure::failure(EditError::NoSuchFrame);
    if (static_cast<int>(frames.size()) >= s.animation.frameCount())
        return Failure::failure(EditError::LastFrame);
    Animation& a = s.animation;
    AnimationTrack& t = currentTrack(s);
    const int current = a.currentFrame();
    // The frame to show afterwards: the current one if it stays, else the first one after it that stays, else the last one left.
    int keepId = t.frames[static_cast<std::size_t>(current)].id;
    if (std::ranges::binary_search(frames, current)) {
        keepId = 0;
        for (int i = current + 1; i < t.frameCount() && !keepId; ++i)
            if (!std::ranges::binary_search(frames, i))
                keepId = t.frames[static_cast<std::size_t>(i)].id;
    }
    stashTrack(s, t);
    for (int& frame : std::views::reverse(frames))
        t.frames.erase(t.frames.begin() + frame);
    const int keep = keepId ? t.indexOfFrame(keepId) : t.frameCount() - 1;
    a.beat = t.startBeat(keep);
    install(s);
    chooseTrackLayer(s);
    normalize(s);
    return {};
}

EditResult<> keepOnly(DocumentState& s) {
    Animation& a = s.animation;
    if (a.tracks.empty())
        return Failure::failure(EditError::NoChange);
    // What the playhead shows is in the stack already, above the shared layers, which stay as ordinary layers.
    const Animation old = a;
    a = Animation{};
    a.fps = old.fps;
    a.loop = old.loop;
    a.view = old.view;
    a.nextFrameId = old.nextFrameId;
    a.nextSoundId = old.nextSoundId;
    a.nextTrackId = old.nextTrackId;
    return {};
}

EditResult<> changeHold(DocumentState& s, std::vector<int> frames, int delta) {
    if (auto locked = unlessLocked(s); !locked)
        return locked;
    frames = cleaned(s, std::move(frames));
    if (frames.empty())
        return Failure::failure(EditError::NoSuchFrame);
    ensureFrames(s);
    const int stayOn = s.animation.frameId(s.animation.currentFrame());
    bool changed = false;
    for (int f : frames) {
        int& hold = currentTrack(s).frames[static_cast<std::size_t>(f)].hold;
        const int next = std::clamp(hold + delta, 1, AnimationFrame::kMaxHold);
        changed = changed || next != hold;
        hold = next;
    }
    if (!changed)
        return Failure::failure(EditError::NoChange);
    stayOnFrame(s, stayOn);
    install(s);
    return {};
}

EditResult<> setHold(DocumentState& s, int frame, int hold) {
    return setHolds(s, {frame}, hold);
}

EditResult<> setHolds(DocumentState& s, std::vector<int> frames, int hold) {
    if (auto locked = unlessLocked(s); !locked)
        return locked;
    frames = cleaned(s, std::move(frames));
    if (frames.empty())
        return Failure::failure(EditError::NoSuchFrame);
    ensureFrames(s);
    const int stayOn = s.animation.frameId(s.animation.currentFrame());
    hold = std::clamp(hold, 1, AnimationFrame::kMaxHold);
    bool changed = false;
    for (int f : frames)
        changed = std::exchange(currentTrack(s).frames[static_cast<std::size_t>(f)].hold, hold) != hold || changed;
    if (!changed)
        return Failure::failure(EditError::NoChange);
    stayOnFrame(s, stayOn);
    install(s);
    return {};
}

namespace {

// A track's frames moved, keeping their order, to index `to` among those that stay; its layers out of the stack (installed
// again after). Whether any moved.
bool reorderFrames(DocumentState& s, AnimationTrack& t, const std::vector<int>& frames, int to) {
    std::vector<int> before;
    before.reserve(t.frames.size());
    for (const AnimationFrame& f : t.frames)
        before.push_back(f.id);
    stashTrack(s, t);
    std::vector<AnimationFrame> moving, staying;
    for (int i = 0; i < t.frameCount(); ++i)
        (std::ranges::binary_search(frames, i) ? moving : staying).push_back(std::move(t.frames[static_cast<std::size_t>(i)]));
    to = std::clamp(to, 0, static_cast<int>(staying.size()));
    staying.insert(staying.begin() + to, std::make_move_iterator(moving.begin()), std::make_move_iterator(moving.end()));
    t.frames = std::move(staying);
    return !std::ranges::equal(before, t.frames, [](int id, const AnimationFrame& f) { return id == f.id; });
}

}

EditResult<> moveFrames(DocumentState& s, std::vector<int> frames, int to) {
    if (s.animation.tracks.empty()) {
        if (cleaned(s, std::move(frames)).empty())
            return Failure::failure(EditError::NoSuchFrame);
        return Failure::failure(EditError::NoChange);
    }
    return moveFrameGroups(s, {FrameMove{s.animation.currentTrack, std::move(frames), to}});
}

EditResult<> moveFrameGroups(DocumentState& s, const std::vector<FrameMove>& moves) {
    Animation& a = s.animation;
    std::vector<FrameMove> valid;
    for (FrameMove m : moves) {
        if (!isFramesTrack(s, m.track))
            continue;
        const int count = a.tracks[static_cast<std::size_t>(m.track)].frameCount();
        std::ranges::sort(m.frames);
        m.frames.erase(std::ranges::unique(m.frames).begin(), m.frames.end());
        std::erase_if(m.frames, [count](int f) { return f < 0 || f >= count; });
        if (!m.frames.empty())
            valid.push_back(std::move(m));
    }
    if (valid.empty())
        return Failure::failure(EditError::NoSuchFrame);
    if (std::ranges::any_of(valid, [&](const FrameMove& m) { return a.tracks[static_cast<std::size_t>(m.track)].locked; }))
        return Failure::failure(EditError::TrackLocked);
    const int currentId = a.frameId(a.currentFrame());
    bool changed = false;
    for (const FrameMove& m : valid)
        changed = reorderFrames(s, a.tracks[static_cast<std::size_t>(m.track)], m.frames, m.to) || changed;
    a.beat = currentTrack(s).startBeat(currentTrack(s).indexOfFrame(currentId));
    install(s);
    return changed ? Failure{} : Failure::failure(EditError::NoChange);
}

EditResult<int> addTrack(DocumentState& s) {
    ensureFrames(s);
    Animation& a = s.animation;
    AnimationTrack t = makeTrack(s);
    AnimationFrame f = makeFrame(a);
    f.layers.push_back(s.layers.makeRaster(s.layers.nextLayerName()));
    f.current = f.layers.back().props.id;
    t.frames.push_back(std::move(f));
    const int at = a.currentTrack + 1;
    a.tracks.insert(a.tracks.begin() + at, std::move(t));
    a.currentTrack = at;
    install(s);
    chooseTrackLayer(s);
    return at;
}

EditResult<int> duplicateTrack(DocumentState& s, int track) {
    Animation& a = s.animation;
    if (!validTrack(s, track) || !a.tracks[static_cast<std::size_t>(track)].isDrawn())
        return EditResult<int>::failure(EditError::NoSuchTrack);
    AnimationTrack copy = makeTrack(s);
    const AnimationTrack& from = a.tracks[static_cast<std::size_t>(track)];
    copy.name = QStringLiteral("%1 copy").arg(from.name);
    copy.hidden = from.hidden;
    copy.label = from.label;
    for (int i = 0; i < from.frameCount(); ++i) {
        AnimationFrame f = makeFrame(a);
        f.hold = from.frames[static_cast<std::size_t>(i)].hold;
        std::map<LayerId, LayerId> renamed;
        for (const LayerNode* n : ownLayers(s, track, i))
            f.layers.push_back(deepCopy(s.layers, *n, renamed));
        const LayerId was = from.current == i ? rememberedLayer(s, from) : from.frames[static_cast<std::size_t>(i)].current;
        if (f.layers.empty())
            f.layers.push_back(s.layers.makeRaster(s.layers.nextLayerName()));
        const auto it = renamed.find(was);
        f.current = it != renamed.end() ? it->second : f.layers.back().props.id;
        copy.frames.push_back(std::move(f));
    }
    a.tracks.insert(a.tracks.begin() + track + 1, std::move(copy));
    a.currentTrack = track + 1;
    install(s);
    chooseTrackLayer(s);
    return track + 1;
}

EditResult<> deleteTrack(DocumentState& s, int track) {
    Animation& a = s.animation;
    if (!validTrack(s, track))
        return Failure::failure(EditError::NoSuchTrack);
    AnimationTrack& t = a.tracks[static_cast<std::size_t>(track)];
    if (t.locked)
        return Failure::failure(EditError::TrackLocked);
    if (t.isDrawn() && framesTracks(a) <= 1)
        return Failure::failure(EditError::LastTrack);
    if (t.isDrawn() && !t.hidden && shownTracks(a) <= 1)
        return Failure::failure(EditError::LastShownTrack);
    const int currentId = a.tracks[static_cast<std::size_t>(a.currentTrack)].id;
    stashTrack(s, t);
    if (t.kind == TrackKind::Video)
        a.video.reset();
    std::erase_if(a.sounds, [&](const SoundClip& c) { return c.track == t.id; });
    a.tracks.erase(a.tracks.begin() + track);
    const int stays = a.indexOfTrack(currentId);
    a.currentTrack = nearestFramesTrack(a, stays >= 0 ? stays : std::min(track, a.trackCount() - 1));
    install(s);
    return {};
}

EditResult<> deleteTracks(DocumentState& s, std::vector<int> tracks) {
    std::ranges::sort(tracks, std::greater<>());
    tracks.erase(std::ranges::unique(tracks).begin(), tracks.end());
    if (tracks.empty())
        return Failure::failure(EditError::NoChange);
    for (int track : tracks)
        if (auto done = deleteTrack(s, track); !done)
            return done;
    return {};
}

EditResult<> setTrackLabel(DocumentState& s, int track, int label) {
    if (!validTrack(s, track) || label < 0 || label > AnimationTrack::kLabels)
        return Failure::failure(EditError::NoSuchTrack);
    int& now = s.animation.tracks[static_cast<std::size_t>(track)].label;
    if (now == label)
        return Failure::failure(EditError::NoChange);
    now = label;
    return {};
}

EditResult<> clearTrack(DocumentState& s, int track) {
    Animation& a = s.animation;
    if (!validTrack(s, track))
        return Failure::failure(EditError::NoSuchTrack);
    if (a.tracks[static_cast<std::size_t>(track)].locked)
        return Failure::failure(EditError::TrackLocked);
    if (a.tracks[static_cast<std::size_t>(track)].kind == TrackKind::Sound) {
        const int id = a.tracks[static_cast<std::size_t>(track)].id;
        return std::erase_if(a.sounds, [id](const SoundClip& c) { return c.track == id; }) ? Failure{}
                                                                                           : Failure::failure(EditError::NoChange);
    }
    if (!isFramesTrack(s, track))
        return Failure::failure(EditError::NoChange);
    const int wasCurrent = a.currentTrack;
    a.currentTrack = track;
    AnimationTrack& t = currentTrack(s);
    if (t.kind == TrackKind::Video) {
        // The video's frames stay, each emptied: their pictures and drawings go.
        std::vector<int> every(static_cast<std::size_t>(t.frameCount()));
        std::iota(every.begin(), every.end(), 0);
        EditResult<> cleared = clearFrames(s, every);
        a.currentTrack = wasCurrent;
        return cleared;
    }
    const bool empty = t.frameCount() == 1 && t.holdOf(0) == 1 && !frameHasDrawing(s, track, 0);
    if (empty) {
        a.currentTrack = wasCurrent;
        return Failure::failure(EditError::NoChange);
    }
    AnimationFrame f = makeFrame(a);
    f.layers = copiedLayers(s, t.frameAtBeat(a.beat), true, f.current);
    if (f.layers.empty()) {
        f.layers.push_back(s.layers.makeRaster(s.layers.nextLayerName()));
        f.current = f.layers.back().props.id;
    }
    stashTrack(s, t);
    t.frames.clear();
    t.frames.push_back(std::move(f));
    install(s);
    a.currentTrack = wasCurrent;
    keepCurrentLayer(s);
    return {};
}

EditResult<> clearFrames(DocumentState& s, std::vector<int> frames) {
    frames = cleaned(s, std::move(frames));
    if (frames.empty())
        return Failure::failure(EditError::NoSuchFrame);
    if (auto locked = unlessLocked(s); !locked)
        return locked;
    const bool video = currentTrack(s).kind == TrackKind::Video;
    if (static_cast<int>(frames.size()) == s.animation.frameCount() && !video)
        return clearTrack(s, s.animation.currentTrack);
    const auto clear = [&](const auto& self, LayerNode& n) -> bool {
        bool changed = false;
        if (!n.isFolder() && !n.pixels.isEmpty()) {
            n.pixels = TiledImage(s.layers.size());
            changed = true;
        }
        if (n.props.kind == LayerKind::Text) {
            n.props.kind = LayerKind::Raster;
            changed = true;
        }
        for (LayerNode& c : n.children)
            changed = self(self, c) || changed;
        return changed;
    };
    AnimationTrack& t = currentTrack(s);
    bool changed = false;
    for (int f : frames) {
        if (video)
            changed = std::exchange(t.frames[static_cast<std::size_t>(f)].picture, -1) >= 0 || changed;
        if (f == t.current) {
            for (LayerNode& n : topLevel(s))
                if (t.owns(n.props.id))
                    changed = clear(clear, n) || changed;
        } else {
            for (LayerNode& n : t.frames[static_cast<std::size_t>(f)].layers)
                changed = clear(clear, n) || changed;
        }
    }
    return changed ? Failure{} : Failure::failure(EditError::NoChange);
}

EditResult<> renameTrack(DocumentState& s, int track, const QString& name) {
    if (!validTrack(s, track))
        return Failure::failure(EditError::NoSuchTrack);
    QString& now = s.animation.tracks[static_cast<std::size_t>(track)].name;
    const QString wanted = name.trimmed();
    if (wanted.isEmpty() || wanted == now)
        return Failure::failure(EditError::NoChange);
    now = wanted;
    return {};
}

EditResult<> moveTrack(DocumentState& s, int track, int to) {
    Animation& a = s.animation;
    if (!validTrack(s, track))
        return Failure::failure(EditError::NoSuchTrack);
    to = std::clamp(to, 0, a.trackCount() - 1);
    if (to == track)
        return Failure::failure(EditError::NoChange);
    // The tracks' layers are in the stack in the tracks' order, so moving one moves its layers over or under the others'.
    const int currentId = a.tracks[static_cast<std::size_t>(a.currentTrack)].id;
    AnimationTrack moving = std::move(a.tracks[static_cast<std::size_t>(track)]);
    a.tracks.erase(a.tracks.begin() + track);
    a.tracks.insert(a.tracks.begin() + to, std::move(moving));
    a.currentTrack = a.indexOfTrack(currentId);
    arrange(s);
    return {};
}

EditResult<> setTrackHidden(DocumentState& s, int track, bool hidden) {
    Animation& a = s.animation;
    if (!validTrack(s, track))
        return Failure::failure(EditError::NoSuchTrack);
    AnimationTrack& t = a.tracks[static_cast<std::size_t>(track)];
    if (t.hidden == hidden)
        return Failure::failure(EditError::NoChange);
    if (hidden && t.isDrawn() && shownTracks(a) <= 1)
        return Failure::failure(EditError::LastShownTrack);
    t.hidden = hidden;
    if (hidden && track == a.currentTrack) {
        // The current track goes to the nearest one shown, with its layer.
        for (int d = 1; d < a.trackCount(); ++d)
            for (int other : {track - d, track + d})
                if (isFramesTrack(s, other) && !a.tracks[static_cast<std::size_t>(other)].hidden && a.currentTrack == track)
                    a.currentTrack = other;
    }
    install(s);
    return {};
}

EditResult<> setTrackLocked(DocumentState& s, int track, bool locked) {
    if (!validTrack(s, track))
        return Failure::failure(EditError::NoSuchTrack);
    bool& now = s.animation.tracks[static_cast<std::size_t>(track)].locked;
    if (now == locked)
        return Failure::failure(EditError::NoChange);
    now = locked;
    return {};
}

EditResult<> setTrackVolume(DocumentState& s, int track, int volume) {
    if (!validTrack(s, track))
        return Failure::failure(EditError::NoSuchTrack);
    AnimationTrack& t = s.animation.tracks[static_cast<std::size_t>(track)];
    if (t.kind != TrackKind::Sound)
        return Failure::failure(EditError::NoSuchTrack);
    if (t.locked)
        return Failure::failure(EditError::TrackLocked);
    volume = std::clamp(volume, 0, AnimationTrack::kFullVolume);
    if (t.volume == volume)
        return Failure::failure(EditError::NoChange);
    t.volume = volume;
    return {};
}

EditResult<> setShared(DocumentState& s, LayerId id, bool shared) {
    const LayerNode* node = s.layers.find(id);
    if (!node)
        return Failure::failure(EditError::NoSuchLayer);
    ensureFrames(s);
    Animation& a = s.animation;
    if (a.isShared(id) == shared)
        return Failure::failure(EditError::NoChange);
    if (shared) {
        const int sharedCount = static_cast<int>(a.shared.size());
        if (s.layers.parentOf(id) != &s.layers.root() && !s.layers.move(id, kNoLayer, sharedCount))
            return Failure::failure(EditError::InvalidMove);
        for (AnimationTrack& t : a.tracks)
            std::erase(t.layers, id);
        a.shared.push_back(id);
        normalize(s);
        return {};
    }
    // The current track's other frames get a copy of it, as their own bottom layer, so that no frame changes.
    const int owner = a.tracks[static_cast<std::size_t>(a.currentTrack)].hidden ? trackForNewLayer(s, 0) : a.currentTrack;
    AnimationTrack& t = a.tracks[static_cast<std::size_t>(owner)];
    const LayerNode copyFrom = *node;
    for (int i = 0; i < t.frameCount(); ++i) {
        if (i == t.current)
            continue;
        std::map<LayerId, LayerId> renamed;
        std::vector<LayerNode>& own = t.frames[static_cast<std::size_t>(i)].layers;
        own.insert(own.begin(), deepCopy(s.layers, copyFrom, renamed));
    }
    std::erase(a.shared, id);
    t.layers.insert(t.layers.begin(), id);
    normalize(s);
    return {};
}

EditResult<> setPlayback(DocumentState& s, double fps, bool loop) {
    fps = std::clamp(fps, Animation::kMinFps, Animation::kMaxFps);
    Animation& a = s.animation;
    if (std::abs(a.fps - fps) < 1e-9 && a.loop == loop)
        return Failure::failure(EditError::NoChange);
    a.fps = fps;
    a.loop = loop;
    if (a.video)
        a.video->fps = fps;  // one picture of the video per beat, at the new rate
    separateSounds(a);       // the clips keep their beats, so their lengths in beats change
    return {};
}

EditResult<> setPlayRange(DocumentState& s, int start, int end) {
    Animation& a = s.animation;
    const int last = std::max(0, a.totalBeats() - 1);
    start = std::clamp(start, 0, last);
    end = end < 0 || end >= last ? -1 : std::max(end, start);  // the last beat: follow the animation's end
    if (a.playStart == start && a.playEnd == end)
        return Failure::failure(EditError::NoChange);
    a.playStart = start;
    a.playEnd = end;
    return {};
}

EditResult<int> addSound(DocumentState& s, SoundClip clip) {
    ensureFrames(s);
    Animation& a = s.animation;
    AnimationTrack t;
    t.id = a.nextTrackId++;
    t.kind = TrackKind::Sound;
    t.name = clip.name;
    t.current = -1;
    clip.id = a.nextSoundId++;
    clip.startBeat = std::max(0, clip.startBeat);
    clip.track = t.id;
    a.tracks.insert(a.tracks.begin(), std::move(t));  // imports go to the bottom
    ++a.currentTrack;
    a.sounds.push_back(std::move(clip));
    return a.sounds.back().id;
}

EditResult<> moveSound(DocumentState& s, int id, int startBeat, int track) {
    Animation& a = s.animation;
    SoundClip* c = findSound(a, id);
    if (!c)
        return Failure::failure(EditError::NoChange);
    if (a.lockedSound(*c))
        return Failure::failure(EditError::TrackLocked);
    SoundClip wanted = *c;
    wanted.startBeat = std::max(0, startBeat);
    // Onto another sound track only where it fits as it is dropped: sliding it past a clip there would carry it out of
    // sight (a report). Onto anything else, or where it doesn't fit, it stays on its own track.
    const int to = a.indexOfTrack(track);
    const bool soundTrack = to >= 0 && a.tracks[static_cast<std::size_t>(to)].kind == TrackKind::Sound;
    SoundClip there = wanted;
    there.track = track;
    if (track != c->track && soundTrack && !a.tracks[static_cast<std::size_t>(to)].locked && fits(a, there)) {
        *c = there;
        return {};
    }
    const std::optional<int> beat = nearestFreeBeat(a, wanted, wanted.startBeat);
    if (!beat)
        return Failure::failure(EditError::NoChange);
    wanted.startBeat = *beat;
    if (wanted == *c)
        return Failure::failure(EditError::NoChange);
    *c = wanted;
    return {};
}

EditResult<> placeSounds(DocumentState& s, const std::vector<SoundPlace>& places) {
    Animation& a = s.animation;
    Animation moved = a;
    const auto onOpenSoundTrack = [&](int track) {
        const int t = a.indexOfTrack(track);
        const AnimationTrack* on = t >= 0 ? &a.tracks[static_cast<std::size_t>(t)] : nullptr;
        return on && on->kind == TrackKind::Sound && !on->locked;
    };
    for (const SoundPlace& p : places) {
        SoundClip* c = findSound(moved, p.id);
        if (!c || p.startBeat < 0 || !onOpenSoundTrack(c->track) || !onOpenSoundTrack(p.track))
            return Failure::failure(EditError::NoChange);
        c->startBeat = p.startBeat;
        c->track = p.track;
    }
    const bool apart = std::ranges::all_of(moved.sounds, [&](const SoundClip& c) { return fits(moved, c); });
    if (moved.sounds == a.sounds || !apart)
        return Failure::failure(EditError::NoChange);
    a.sounds = std::move(moved.sounds);
    return {};
}

EditResult<> trimSound(DocumentState& s, int id, int startBeat, double trimStart, double trimEnd) {
    Animation& a = s.animation;
    SoundClip* c = findSound(a, id);
    if (!c)
        return Failure::failure(EditError::NoChange);
    if (a.lockedSound(*c))
        return Failure::failure(EditError::TrackLocked);
    SoundClip wanted = *c;
    wanted.startBeat = startBeat;
    wanted.trimStart = std::max(0.0, trimStart);
    wanted.trimEnd = std::max(0.0, trimEnd);
    const bool playsABeat = wanted.playedSeconds() * a.fps >= 1.0 - 1e-6;
    if (wanted == *c || startBeat < 0 || !playsABeat || !fits(a, wanted))
        return Failure::failure(EditError::NoChange);
    *c = wanted;
    return {};
}

EditResult<> setSoundMuted(DocumentState& s, int id, bool muted) {
    SoundClip* c = findSound(s.animation, id);
    if (!c || c->muted == muted)
        return Failure::failure(EditError::NoChange);
    if (s.animation.lockedSound(*c))
        return Failure::failure(EditError::TrackLocked);
    c->muted = muted;
    return {};
}

EditResult<> removeSound(DocumentState& s, int id) {
    const SoundClip* c = findSound(s.animation, id);
    if (!c)
        return Failure::failure(EditError::NoChange);
    if (s.animation.lockedSound(*c))
        return Failure::failure(EditError::TrackLocked);
    std::erase_if(s.animation.sounds, [id](const SoundClip& clip) { return clip.id == id; });
    return {};
}

EditResult<> removeSounds(DocumentState& s, const std::vector<int>& ids) {
    Animation& a = s.animation;
    const auto chosen = [&](const SoundClip& c) { return std::ranges::find(ids, c.id) != ids.end(); };
    if (std::ranges::none_of(a.sounds, chosen))
        return Failure::failure(EditError::NoChange);
    if (std::ranges::any_of(a.sounds, [&](const SoundClip& c) { return chosen(c) && a.lockedSound(c); }))
        return Failure::failure(EditError::TrackLocked);
    std::erase_if(a.sounds, chosen);
    return {};
}

EditResult<> setVideo(DocumentState& s, std::optional<VideoReference> video) {
    Animation& a = s.animation;
    if (a.video == video)
        return Failure::failure(EditError::NoChange);
    const int t = a.videoTrack();
    if (t >= 0 && a.tracks[static_cast<std::size_t>(t)].locked)
        return Failure::failure(EditError::TrackLocked);
    if (video)
        ensureFrames(s);
    // Another file, or the same at another rate: its frames are made again.
    const bool others = a.video && video && !a.video->samePictures(*video);
    a.video = std::move(video);
    if (others) {
        stashTrack(s, a.tracks[static_cast<std::size_t>(t)]);
        a.tracks[static_cast<std::size_t>(t)].frames.clear();
    }
    normalize(s);  // the video's track comes or goes with it
    install(s);
    return {};
}

EditResult<> followVideo(DocumentState& s, const VideoReference& video) {
    ensureFrames(s);
    Animation& a = s.animation;
    a.fps = std::clamp(video.fps, Animation::kMinFps, Animation::kMaxFps);
    a.video = video;
    keepMediaTracks(s);
    const int wanted = video.startBeat + video.pictureCount();
    AnimationTrack& t = currentTrack(s);
    stashTrack(s, t);
    std::map<LayerId, LayerId> renamed;
    while (t.totalBeats() < wanted) {
        AnimationFrame f = makeFrame(a);
        for (const LayerNode& n : t.frames.back().layers)
            f.layers.push_back(blankCopy(s.layers, n, renamed));
        if (f.layers.empty())
            f.layers.push_back(s.layers.makeRaster(s.layers.nextLayerName()));
        f.current = f.layers.back().props.id;
        t.frames.push_back(std::move(f));
    }
    install(s);
    return {};
}

EditResult<> onEveryFrame(DocumentState& s, const std::function<EditResult<>(DocumentState&)>& op) {
    for (AnimationTrack& track : s.animation.tracks) {
        for (AnimationFrame& f : track.frames) {
            if (f.layers.empty())
                continue;
            DocumentState t;
            t.layers = LayerStack(s.layers.size());
            t.layers.setNextIdValue(s.layers.nextIdValue());
            t.layers.root().children = std::move(f.layers);
            t.current = t.layers.root().children.back().props.id;
            t.selection = s.selection;
            t.background = s.background;
            const EditResult<> done = op(t);
            if (!done && done.error() != EditError::NoChange)
                return done;
            f.layers = std::move(t.layers.root().children);
            s.layers.setNextIdValue(std::max(s.layers.nextIdValue(), t.layers.nextIdValue()));
        }
    }
    return op(s);
}

LayerStack beatStack(const DocumentState& s, int beat) {
    LayerStack out = s.layers;
    const Animation& a = s.animation;
    const bool installed = std::ranges::all_of(
        a.tracks, [&](const AnimationTrack& t) { return t.hidden || !t.hasFrames() || t.current == t.frameAtBeat(beat); });
    if (installed)
        return out;
    std::vector<LayerNode>& kids = out.root().children;
    std::erase_if(kids, [&](const LayerNode& n) { return !a.isShared(n.props.id); });
    for (int i = 0; i < a.trackCount(); ++i) {
        const AnimationTrack& t = a.tracks[static_cast<std::size_t>(i)];
        if (t.hidden || !t.hasFrames())
            continue;
        for (const LayerNode* n : ownLayers(s, i, t.frameAtBeat(beat)))
            kids.push_back(*n);
    }
    return out;
}

LayerStack frameStack(const DocumentState& s, int frame) {
    return beatStack(s, s.animation.startBeat(frame));
}

std::vector<const LayerNode*> ownLayers(const DocumentState& s, int track, int frame) {
    std::vector<const LayerNode*> out;
    const Animation& a = s.animation;
    if (a.tracks.empty()) {
        for (const LayerNode& n : s.layers.root().children)
            if (!a.isShared(n.props.id))
                out.push_back(&n);
        return out;
    }
    if (!validTrack(s, track))
        return out;
    const AnimationTrack& t = a.tracks[static_cast<std::size_t>(track)];
    if (frame < 0 || frame >= t.frameCount())
        return out;
    if (frame == t.current) {
        for (const LayerNode& n : s.layers.root().children)
            if (t.owns(n.props.id))
                out.push_back(&n);
        return out;
    }
    for (const LayerNode& n : t.frames[static_cast<std::size_t>(frame)].layers)
        out.push_back(&n);
    return out;
}

std::vector<const LayerNode*> ownLayers(const DocumentState& s, int frame) {
    return ownLayers(s, s.animation.currentTrack, frame);
}

bool frameHasDrawing(const DocumentState& s, int track, int frame) {
    const auto own = ownLayers(s, track, frame);
    return std::ranges::any_of(own, [](const LayerNode* n) { return hasDrawing(*n); });
}

bool frameHasDrawing(const DocumentState& s, int frame) {
    return frameHasDrawing(s, s.animation.currentTrack, frame);
}

QImage exportedBeat(const DocumentState& s, int beat) {
    const LayerStack stack = beatStack(s, beat);
    QImage out(stack.size(), QImage::Format_RGBA8888_Premultiplied);
    Compositor::renderOpaque(stack, QRect(QPoint(0, 0), stack.size()), s.background.paperColor(), &out);
    return out;
}

std::vector<LayerNode> onionSkins(const DocumentState& s) {
    std::vector<LayerNode> out;
    const Animation& a = s.animation;
    const AnimationTrack* t = a.track();
    if (!t || t->hidden || t->frameCount() < 2 || !a.view.onionSkin)
        return out;
    const int current = t->current;
    const auto fade = [&](int distance, int count) { return std::max(1, a.view.onionOpacity * (count - distance + 1) / count); };
    // Farthest first, so the nearest lie on top.
    for (int d = a.view.onionBefore; d >= 1; --d)
        if (current - d >= 0)
            out.push_back(onionOf(s, current - d, fade(d, a.view.onionBefore), kOnionBefore));
    for (int d = a.view.onionAfter; d >= 1; --d)
        if (current + d < t->frameCount())
            out.push_back(onionOf(s, current + d, fade(d, a.view.onionAfter), kOnionAfter));
    return out;
}

LayerNode videoNode(const TiledImage& picture, int opacity) {
    LayerNode n;
    n.props.name = QStringLiteral("video");
    n.props.opacity = std::clamp(opacity, 0, 100);
    n.pixels = picture;
    return n;
}

LayerNode videoNode(const QImage& picture, int opacity) {
    LayerNode n;
    n.props.name = QStringLiteral("video");
    n.props.opacity = std::clamp(opacity, 0, 100);
    n.pixels = TiledImage(picture.size());
    n.pixels.writeImage(picture.convertToFormat(QImage::Format_RGBA8888_Premultiplied), QPoint(0, 0));
    return n;
}

LayerStack displayStack(const DocumentState& s, const std::optional<LayerNode>& video, const std::vector<LayerNode>& onions) {
    LayerStack out = s.layers;
    std::vector<LayerNode>& kids = out.root().children;
    const Animation& a = s.animation;
    const AnimationTrack* t = a.track();
    // Right under the current track's layers: over the shared layers and the lower tracks.
    const auto under = std::ranges::find_if(kids, [&](const LayerNode& n) {
        return t ? t->owns(n.props.id) || a.trackOfLayer(n.props.id) > a.currentTrack : !a.isShared(n.props.id);
    });
    kids.insert(under, onions.begin(), onions.end());
    if (video) {
        // Where its track lies: over the shared layers and the tracks under it, under the drawings on its own frames.
        const int at = a.videoTrack();
        const auto over = std::ranges::find_if(kids, [&](const LayerNode& n) {
            return !a.isShared(n.props.id) && (at < 0 || a.trackOfLayer(n.props.id) >= at || a.trackOfLayer(n.props.id) < 0);
        });
        kids.insert(at < 0 ? kids.begin() : over, *video);
    }
    return out;
}

}
