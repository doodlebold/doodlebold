#include "core/animation/Animation.h"

#include <algorithm>
#include <cmath>

namespace doodlebold {

int VideoReference::pictureCount() const {
    return fps > 0.0 ? std::max(1, static_cast<int>(std::floor(seconds * fps + 1e-6))) : 0;
}

int AnimationTrack::holdOf(int frame) const {
    if (frames.empty())
        return 1;
    return frames[static_cast<std::size_t>(std::clamp(frame, 0, frameCount() - 1))].hold;
}

int AnimationTrack::startBeat(int frame) const {
    int beat = 0;
    for (int i = 0; i < std::min(frame, frameCount()); ++i)
        beat += frames[static_cast<std::size_t>(i)].hold;
    return beat;
}

int AnimationTrack::totalBeats() const {
    return std::max(1, startBeat(frameCount()));
}

int AnimationTrack::frameAtBeat(int beat) const {
    int end = 0;
    for (int i = 0; i < frameCount(); ++i) {
        end += frames[static_cast<std::size_t>(i)].hold;
        if (beat < end)
            return i;
    }
    return std::max(0, frameCount() - 1);
}

int AnimationTrack::indexOfFrame(int frameId) const {
    for (std::size_t i = 0; i < frames.size(); ++i)
        if (frames[i].id == frameId)
            return static_cast<int>(i);
    return -1;
}

int Animation::totalBeats() const {
    int beats = 1;
    for (const AnimationTrack& t : tracks)
        if (t.hasFrames())
            beats = std::max(beats, t.totalBeats());
    return beats;
}

int Animation::frameId(int frame) const {
    const AnimationTrack* t = track();
    return t && frame >= 0 && frame < t->frameCount() ? t->frames[static_cast<std::size_t>(frame)].id : 0;
}

int Animation::trackOfLayer(LayerId id) const {
    for (std::size_t i = 0; i < tracks.size(); ++i)
        if (tracks[i].owns(id))
            return static_cast<int>(i);
    return -1;
}

int Animation::indexOfTrack(int id) const {
    for (std::size_t i = 0; i < tracks.size(); ++i)
        if (tracks[i].id == id)
            return static_cast<int>(i);
    return -1;
}

int Animation::videoTrack() const {
    for (std::size_t i = 0; i < tracks.size(); ++i)
        if (tracks[i].kind == TrackKind::Video)
            return static_cast<int>(i);
    return -1;
}

int Animation::videoPictureAt(int atBeat) const {
    const int t = videoTrack();
    if (t < 0 || tracks[static_cast<std::size_t>(t)].frames.empty())
        return -1;
    const AnimationTrack& track = tracks[static_cast<std::size_t>(t)];
    return track.frames[static_cast<std::size_t>(track.frameAtBeat(atBeat))].picture;
}

bool Animation::heard(const SoundClip& clip) const {
    const int t = indexOfTrack(clip.track);
    return !clip.muted && (t < 0 || !tracks[static_cast<std::size_t>(t)].hidden);
}

int Animation::playLast() const {
    const int last = std::max(0, totalBeats() - 1);
    return playEnd < 0 ? last : std::clamp(playEnd, playFirst(), last);
}

int Animation::playFirst() const {
    return std::clamp(playStart, 0, std::max(0, totalBeats() - 1));
}

double Animation::gainOf(const SoundClip& clip) const {
    const int t = indexOfTrack(clip.track);
    return t < 0 ? 1.0 : double(tracks[static_cast<std::size_t>(t)].volume) / AnimationTrack::kFullVolume;
}

bool Animation::videoShown() const {
    const int t = videoTrack();
    return video && (t < 0 || !tracks[static_cast<std::size_t>(t)].hidden);
}

bool Animation::lockedLayer(LayerId topLevel) const {
    const int t = trackOfLayer(topLevel);
    return t >= 0 && tracks[static_cast<std::size_t>(t)].locked;
}

bool Animation::lockedSound(const SoundClip& clip) const {
    const int t = indexOfTrack(clip.track);
    return t >= 0 && tracks[static_cast<std::size_t>(t)].locked;
}

bool Animation::isShared(LayerId id) const {
    return std::ranges::find(shared, id) != shared.end();
}

}
