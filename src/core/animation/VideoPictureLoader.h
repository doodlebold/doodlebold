#pragma once

#include "core/animation/AnimationVideo.h"
#include "core/raster/TiledImage.h"

#include <QImage>

#include <condition_variable>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>

namespace doodlebold {

// The video's pictures for the canvas, read on a thread of their own so that going to a frame or playing never waits for the
// disk: the picture asked for first, then the next few, each ready as the canvas-sized picture and the tiles it becomes as the
// layer under the frames. Pictures go by their index in the video (AnimationFrame::picture).
class VideoPictureLoader {
public:
    struct Picture {
        QImage image;                              // null: the video has no such picture
        std::shared_ptr<const TiledImage> tiles;
    };
    static constexpr int kAhead = 6;  // pictures read after the one asked for
    static constexpr int kKept = 24;

    // `ready` is called on the loader's thread when the picture last asked for is ready.
    VideoPictureLoader(VideoFrames frames, std::function<void(int index)> ready);
    ~VideoPictureLoader();
    VideoPictureLoader(const VideoPictureLoader&) = delete;
    VideoPictureLoader& operator=(const VideoPictureLoader&) = delete;

    const VideoReference& video() const { return m_frames.video(); }
    QSize canvas() const { return m_frames.canvas(); }
    // The picture if it has been read.
    std::optional<Picture> picture(int index);
    // Reads picture `index` next (dropping what was asked before), then the ones after it.
    void want(int index);
    // For tests: until nothing is left to read.
    void waitIdle();

private:
    void run();
    std::optional<int> nextPicture();  // under the lock
    void keep(int index, Picture picture);

    VideoFrames m_frames;  // used on the loader's thread only
    std::function<void(int)> m_ready;
    std::mutex m_mutex;
    std::condition_variable m_wake;
    std::condition_variable m_idle;
    std::map<int, Picture> m_pictures;
    std::map<int, unsigned> m_used;  // when each picture was last read or asked for, to drop the oldest
    unsigned m_clock = 0;
    int m_wanted = -1;
    bool m_busy = false;
    bool m_quit = false;
    std::thread m_worker;
};

}
