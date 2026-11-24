#include "core/animation/VideoPictureLoader.h"

#include <algorithm>

namespace doodlebold {

VideoPictureLoader::VideoPictureLoader(VideoFrames frames, std::function<void(int index)> ready)
    : m_frames(std::move(frames)), m_ready(std::move(ready)), m_worker([this] { run(); }) {}

VideoPictureLoader::~VideoPictureLoader() {
    {
        const std::scoped_lock lock(m_mutex);
        m_quit = true;
    }
    m_wake.notify_all();
    m_worker.join();
}

std::optional<VideoPictureLoader::Picture> VideoPictureLoader::picture(int index) {
    const std::scoped_lock lock(m_mutex);
    const auto it = m_pictures.find(index);
    if (it == m_pictures.end())
        return std::nullopt;
    m_used[index] = ++m_clock;
    return it->second;
}

void VideoPictureLoader::want(int index) {
    {
        const std::scoped_lock lock(m_mutex);
        m_wanted = index;
    }
    m_wake.notify_all();
}

void VideoPictureLoader::waitIdle() {
    std::unique_lock lock(m_mutex);
    m_idle.wait(lock, [this] { return !m_busy && !nextPicture(); });
}

// The picture asked for if it isn't read, else the first of the ones after it that isn't.
std::optional<int> VideoPictureLoader::nextPicture() {
    if (m_wanted < 0)
        return std::nullopt;
    const int last = std::min(m_wanted + kAhead, m_frames.video().pictureCount() - 1);
    for (int index = m_wanted; index <= std::max(m_wanted, last); ++index)
        if (!m_pictures.contains(index))
            return index;
    return std::nullopt;
}

void VideoPictureLoader::keep(int index, Picture picture) {
    m_pictures[index] = std::move(picture);
    m_used[index] = ++m_clock;
    while (m_pictures.size() > std::size_t(kKept)) {
        const auto oldest = std::ranges::min_element(m_used, [](const auto& a, const auto& b) { return a.second < b.second; });
        m_pictures.erase(oldest->first);
        m_used.erase(oldest);
    }
}

void VideoPictureLoader::run() {
    std::unique_lock lock(m_mutex);
    for (;;) {
        m_wake.wait(lock, [this] { return m_quit || nextPicture(); });
        if (m_quit)
            return;
        const int index = *nextPicture();
        m_busy = true;
        lock.unlock();
        Picture read;
        read.image = m_frames.picture(index);
        if (!read.image.isNull()) {
            auto tiles = std::make_shared<TiledImage>(read.image.size());
            tiles->writeImage(read.image, QPoint(0, 0));
            read.tiles = std::move(tiles);
        }
        lock.lock();
        keep(index, std::move(read));
        m_busy = false;
        const bool asked = index == m_wanted;
        if (asked) {
            lock.unlock();
            m_ready(index);
            lock.lock();
        }
        m_idle.notify_all();
    }
}

}
