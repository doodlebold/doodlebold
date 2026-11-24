#include "core/timelapse/TimelapseRecorder.h"

#include "core/layers/Compositor.h"

#include <QDateTime>
#include <QElapsedTimer>
#include <QRunnable>

#include <algorithm>
#include <cstring>

namespace doodlebold {

namespace {

// A hash of the pixels of a tile (rows of `width` RGBA pixels); never 0, which stands for "not known".
std::uint64_t hashTile(const QImage& picture, const QRect& r) {
    std::uint64_t h = 0x9E3779B97F4A7C15ull ^ (std::uint64_t(r.width()) << 32 | std::uint64_t(r.height()));
    const std::size_t rowBytes = static_cast<std::size_t>(r.width()) * 4;
    for (int y = r.top(); y <= r.bottom(); ++y) {
        const std::uint8_t* p = picture.constScanLine(y) + static_cast<std::ptrdiff_t>(r.left()) * 4;
        std::size_t i = 0;
        for (; i + 8 <= rowBytes; i += 8) {
            std::uint64_t w;
            std::memcpy(&w, p + i, 8);
            h = (h ^ w) * 0xFF51AFD7ED558CCDull;
            h ^= h >> 32;
        }
        for (; i < rowBytes; i += 4) {
            std::uint32_t w;
            std::memcpy(&w, p + i, 4);
            h = (h ^ w) * 0xFF51AFD7ED558CCDull;
            h ^= h >> 32;
        }
    }
    return h ? h : 1;
}

QByteArray copyTile(const QImage& picture, const QRect& r) {
    const int rowBytes = r.width() * 4;
    QByteArray out(rowBytes * r.height(), Qt::Uninitialized);
    for (int y = 0; y < r.height(); ++y)
        std::memcpy(out.data() + static_cast<std::ptrdiff_t>(y) * rowBytes, picture.constScanLine(r.top() + y) + r.left() * 4,
                    static_cast<std::size_t>(rowBytes));
    return out;
}

class SteadyClock {
public:
    SteadyClock() { m_timer.start(); }
    std::int64_t operator()() const { return m_timer.elapsed(); }

private:
    QElapsedTimer m_timer;
};

}

class TimelapseRecorder::EncodeJob : public QRunnable {
public:
    EncodeJob(std::shared_ptr<Shared> shared, std::shared_ptr<TimelapseStep> step, std::vector<QByteArray> raw)
        : m_shared(std::move(shared)), m_step(std::move(step)), m_raw(std::move(raw)) {
        setAutoDelete(true);
    }
    EncodeJob(std::shared_ptr<Shared> shared, std::shared_ptr<TimelapseStep> step, LayerStack layers, CanvasBackground background)
        : m_shared(std::move(shared)), m_step(std::move(step)), m_layers(std::move(layers)), m_background(background), m_first(true) {
        setAutoDelete(true);
    }

    void run() override {
        std::int64_t bytes = 0;
        if (m_first) {
            QImage picture(m_layers.size(), QImage::Format_RGBA8888_Premultiplied);
            Compositor::renderDisplay(m_layers, QRect(QPoint(0, 0), m_layers.size()), m_background, &picture);
            for (TimelapseTile& t : m_step->tiles) {
                const QRect r =
                    QRect(QPoint(t.x * kTimelapseTileSize, t.y * kTimelapseTileSize), timelapseTileSize(t.x, t.y, m_layers.size()));
                t.blob = encodeTimelapseTile(picture.constScanLine(r.top()) + r.left() * 4, static_cast<int>(picture.bytesPerLine()),
                                             r.width(), r.height());
                bytes += t.blob.size() + 8;
            }
        } else {
            for (std::size_t i = 0; i < m_step->tiles.size(); ++i) {
                TimelapseTile& t = m_step->tiles[i];
                const QSize s = timelapseTileSize(t.x, t.y, m_step->size);
                t.blob =
                    encodeTimelapseTile(reinterpret_cast<const std::uint8_t*>(m_raw[i].constData()), s.width() * 4, s.width(), s.height());
                bytes += t.blob.size() + 8;
            }
        }
        m_shared->bytes += bytes;
        std::scoped_lock lock(m_shared->mutex);
        --m_shared->pending;
        m_shared->done.notify_all();
    }

private:
    std::shared_ptr<Shared> m_shared;
    std::shared_ptr<TimelapseStep> m_step;
    std::vector<QByteArray> m_raw;
    LayerStack m_layers;
    CanvasBackground m_background;
    bool m_first = false;
};

TimelapseRecorder::TimelapseRecorder(const LayerStack& layers, const CanvasBackground& background) {
    m_pool.setMaxThreadCount(1);
    m_startedMs = QDateTime::currentMSecsSinceEpoch();
    m_clock = SteadyClock();
    m_lastTime = now();
    resizeGrids(layers.size());
    auto first = std::make_shared<TimelapseStep>();
    first->kind = TimelapseStep::Kind::First;
    first->full = true;
    first->size = layers.size();
    for (int ty = 0; ty < m_tilesY; ++ty)
        for (int tx = 0; tx < m_tilesX; ++tx)
            first->tiles.push_back({tx, ty, {}});
    m_steps.push_back(first);
    m_stepCount = 0;
    ++m_shared->pending;
    m_pool.start(new EncodeJob(m_shared, first, layers, background));
}

TimelapseRecorder::TimelapseRecorder(const Timelapse& recording) {
    m_pool.setMaxThreadCount(1);
    m_clock = SteadyClock();
    m_lastTime = now();
    for (const TimelapseStepPtr& s : recording.steps) {
        m_steps.push_back(std::make_shared<TimelapseStep>(*s));
        for (const TimelapseTile& t : s->tiles)
            m_shared->bytes += t.blob.size() + 8;
    }
    m_stepCount = recording.stepCount();
    m_paused = recording.paused;
    m_startedMs = recording.startedMs;
    m_activeMs = recording.paintedMs();
    m_resumed = true;
    resizeGrids(recording.finalSize());  // the tile hashes are not known: the first tiles that change are all stored
}

TimelapseRecorder::~TimelapseRecorder() {
    flush();
}

void TimelapseRecorder::setClock(std::function<std::int64_t()> clock) {
    m_clock = std::move(clock);
    m_lastTime = now();
}

std::int64_t TimelapseRecorder::now() const {
    return m_clock();
}

void TimelapseRecorder::resizeGrids(const QSize& size) {
    m_size = size;
    m_tilesX = (size.width() + kTimelapseTileSize - 1) / kTimelapseTileSize;
    m_tilesY = (size.height() + kTimelapseTileSize - 1) / kTimelapseTileSize;
    m_hash.assign(static_cast<std::size_t>(m_tilesX) * m_tilesY, 0);
    m_dirty.assign(m_hash.size(), 0);
    m_dirtyTiles = QRect();
}

void TimelapseRecorder::markChanged(const QRect& area) {
    const QRect c = area.intersected(QRect(QPoint(0, 0), m_size));
    if (c.isEmpty())
        return;
    const int x0 = c.left() / kTimelapseTileSize, x1 = c.right() / kTimelapseTileSize;
    const int y0 = c.top() / kTimelapseTileSize, y1 = c.bottom() / kTimelapseTileSize;
    for (int ty = y0; ty <= y1; ++ty)
        std::memset(m_dirty.data() + static_cast<std::ptrdiff_t>(ty) * m_tilesX + x0, 1, static_cast<std::size_t>(x1 - x0 + 1));
    m_dirtyTiles |= QRect(QPoint(x0, y0), QPoint(x1, y1));
}

void TimelapseRecorder::markResized(const QSize& size) {
    resizeGrids(size);
    m_needFull = true;
}

void TimelapseRecorder::spendTime() {
    const std::int64_t t = now();
    m_activeMs += std::clamp<std::int64_t>(t - m_lastTime, 0, kIdleCapMs);
    m_lastTime = t;
}

void TimelapseRecorder::enqueue(std::shared_ptr<TimelapseStep> step, std::vector<QByteArray> raw) {
    ++m_shared->pending;
    m_pool.start(new EncodeJob(m_shared, std::move(step), std::move(raw)));
}

void TimelapseRecorder::beginStroke() {
    m_partials = 0;
    m_hashBeforeStroke.clear();
    m_strokeFirst = true;
}

void TimelapseRecorder::abandonStroke() {
    // The frames are the last steps stored (nothing else is recorded while a stroke runs).
    for (; m_partials > 0 && m_steps.size() > 1; --m_partials) {
        m_steps.pop_back();
    }
    for (const auto& [tile, hash] : m_hashBeforeStroke)
        m_hash[tile] = hash;  // the last picture that stays
    m_hashBeforeStroke.clear();
    m_partials = 0;
}

static_assert(kTimelapseTileSize == Tile::kSize, "a stroke's frame tiles (TileImages) are the layers' tiles");

bool TimelapseRecorder::record(TimelapseStep::Kind kind, const QImage& picture, const TileImages* over) {
    if (picture.isNull() || picture.format() != QImage::Format_RGBA8888_Premultiplied)
        return false;
    if (over)
        for (const auto& [c, image] : *over)
            markChanged(c.pixelRect());
    const bool full = m_needFull || picture.size() != m_size;
    if (picture.size() != m_size)
        resizeGrids(picture.size());
    auto step = std::make_shared<TimelapseStep>();
    step->kind = kind;
    step->size = picture.size();
    std::vector<QByteArray> raw;
    const auto take = [&](int tx, int ty) {
        const QRect r(QPoint(tx * kTimelapseTileSize, ty * kTimelapseTileSize), timelapseTileSize(tx, ty, m_size));
        std::uint64_t& known = m_hash[static_cast<std::size_t>(ty) * m_tilesX + tx];
        const QImage* source = &picture;
        QRect at = r;
        if (over)
            if (const auto it = over->find(TileCoord{tx, ty});
                it != over->end() && it->second.size() == r.size() && it->second.format() == QImage::Format_RGBA8888_Premultiplied) {
                source = &it->second;
                at = QRect(QPoint(0, 0), r.size());
            }
        const std::uint64_t h = hashTile(*source, at);
        if (!full && known == h)
            return;
        if (kind == TimelapseStep::Kind::Partial)
            m_hashBeforeStroke.emplace(static_cast<std::size_t>(ty) * m_tilesX + tx, known);  // the first frame to store the tile keeps it
        known = h;
        step->tiles.push_back({tx, ty, {}});
        raw.push_back(copyTile(*source, at));
    };
    if (full) {
        step->full = true;
        for (int ty = 0; ty < m_tilesY; ++ty)
            for (int tx = 0; tx < m_tilesX; ++tx)
                take(tx, ty);
    } else if (!m_dirtyTiles.isNull()) {
        for (int ty = m_dirtyTiles.top(); ty <= m_dirtyTiles.bottom(); ++ty)
            for (int tx = m_dirtyTiles.left(); tx <= m_dirtyTiles.right(); ++tx)
                if (m_dirty[static_cast<std::size_t>(ty) * m_tilesX + tx])
                    take(tx, ty);
    }
    if (full) {
        std::ranges::fill(m_dirty, 0);
    } else if (!m_dirtyTiles.isNull()) {  // only the rows the dirty tiles lie in: a frame every tenth of a second must stay cheap
        for (int ty = m_dirtyTiles.top(); ty <= m_dirtyTiles.bottom(); ++ty)
            std::memset(m_dirty.data() + static_cast<std::ptrdiff_t>(ty) * m_tilesX + m_dirtyTiles.left(), 0,
                        static_cast<std::size_t>(m_dirtyTiles.width()));
    }
    m_dirtyTiles = QRect();
    m_needFull = false;
    const bool partial = kind == TimelapseStep::Kind::Partial;
    const bool endsStroke = kind == TimelapseStep::Kind::Edit && m_partials > 0;
    if (!partial)
        m_hashBeforeStroke.clear();
    if (!partial && !endsStroke)
        m_partials = 0;  // another kind of step: the frames of a stroke that did not become a step are not claimed by it
    if (step->tiles.empty() && !endsStroke)
        return false;
    spendTime();
    step->activeMs = static_cast<std::uint32_t>(std::min<std::int64_t>(m_activeMs, 0xFFFFFFFFll));
    step->resumed = m_resumed;
    m_resumed = false;
    if (partial) {
        step->strokeStart = m_strokeFirst;
        m_strokeFirst = false;
        ++m_partials;
    } else {
        m_partials = 0;
        ++m_stepCount;
    }
    m_steps.push_back(step);
    enqueue(std::move(step), std::move(raw));
    return true;
}

void TimelapseRecorder::flush() {
    std::unique_lock lock(m_shared->mutex);
    m_shared->done.wait(lock, [this] { return m_shared->pending.load() == 0; });
}

Timelapse TimelapseRecorder::snapshot() {
    flush();
    Timelapse t;
    t.paused = m_paused;
    t.startedMs = m_startedMs;
    t.steps.assign(m_steps.begin(), m_steps.end());
    return t;
}

}
