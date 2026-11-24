#pragma once

#include "core/animation/Animation.h"

#include <memory>
#include <vector>

namespace doodlebold {

// Plays the sound clips with the animation, each from its place, through Qt Multimedia. A build without Qt Multimedia plays
// the animation silently; the clips are still saved and exported.
class SoundPlayback {
public:
    SoundPlayback();
    ~SoundPlayback();
    SoundPlayback(const SoundPlayback&) = delete;
    SoundPlayback& operator=(const SoundPlayback&) = delete;

    static bool isAvailable();
    // Gets the clips ready ahead of Play: each one's file written and a player opened on it (the first one starts Qt
    // Multimedia, which took half a second to a second on the window's thread at the first
    // Play), so that Play only starts them. The players of clips not given any more go.
    void prepare(const std::vector<SoundClip>& clips);
    // Starts every clip that is heard from `fromBeat` on, at its place (one not prepared is prepared first), each as loud as
    // its gain (0 … 1 of its file, Animation::gainOf; the same count as the clips), `speed` times as fast as the animation's
    // rate (TimelineView::speed; the players' pitch follows Qt's playback rate).
    void play(const std::vector<SoundClip>& clips, const std::vector<double>& gains, double fps, int fromBeat, double speed = 1.0);
    // A track's volume changed while playing: the clips' players follow at once.
    void setGains(const std::vector<SoundClip>& clips, const std::vector<double>& gains);
    void stop();
    // The clips the last play() was given, until stop() (with or without Qt Multimedia).
    const std::vector<int>& playing() const { return m_playing; }
    // How many players have been opened, for tests (none without Qt Multimedia).
    int playersMade() const { return m_playersMade; }

private:
    std::vector<int> m_playing;
    int m_playersMade = 0;
    struct Players;
    std::unique_ptr<Players> m_players;
};

}
