#include "core/timelapse/TimelapseReplay.h"

#include <algorithm>

namespace doodlebold {

TimelapseReplay::TimelapseReplay(Timelapse recording) : m_recording(std::move(recording)) {
    for (int i = 0; i < stepCount(); ++i) {
        const TimelapseStep& s = *m_recording.steps[static_cast<std::size_t>(i)];
        if (s.full)
            m_fullSteps.push_back(i);
        for (int k = 0; k < static_cast<int>(s.tiles.size()); ++k)
            m_history[TileCoord{s.tiles[static_cast<std::size_t>(k)].x, s.tiles[static_cast<std::size_t>(k)].y}].push_back({i, k});
    }
}

QSize TimelapseReplay::sizeAt(int step) const {
    return m_recording.steps.empty() ? QSize() : m_recording.steps[static_cast<std::size_t>(std::clamp(step, 0, stepCount() - 1))]->size;
}

const QImage& TimelapseReplay::seek(int step) {
    if (m_recording.steps.empty())
        return m_image;
    step = std::clamp(step, 0, stepCount() - 1);
    const QSize size = sizeAt(step);
    if (m_image.size() != size) {
        m_image = QImage(size, QImage::Format_RGBA8888);
        m_image.fill(Qt::black);
        m_shown.clear();
    }
    const auto lastFull = std::ranges::upper_bound(m_fullSteps, step);
    const int from = lastFull == m_fullSteps.begin() ? 0 : *(lastFull - 1);
    for (const auto& [coord, history] : m_history) {
        if (coord.pixelRect().left() >= size.width() || coord.pixelRect().top() >= size.height())
            continue;
        // std::ranges::upper_bound can't compare a step with an entry: it wants the comparison both ways round.
        // NOLINTNEXTLINE(modernize-use-ranges)
        const auto next = std::upper_bound(history.begin(), history.end(), step, [](int s, const Entry& e) { return s < e.step; });
        Shown want;
        if (next != history.begin() && (next - 1)->step >= from)
            want = {(next - 1)->step, (next - 1)->tile};
        Shown& shown = m_shown[coord];
        if (shown.step == want.step && shown.tile == want.tile)
            continue;
        shown = want;
        const QSize ts = timelapseTileSize(coord.x, coord.y, size);
        const QRect r(coord.pixelRect().topLeft(), ts);
        if (want.step < 0) {
            for (int y = r.top(); y <= r.bottom(); ++y)
                std::fill_n(reinterpret_cast<quint32*>(m_image.scanLine(y)) + r.left(), r.width(), 0xFF000000u);
            continue;
        }
        const TimelapseTile& tile = m_recording.steps[static_cast<std::size_t>(want.step)]->tiles[static_cast<std::size_t>(want.tile)];
        if (!decodeTimelapseTile(tile.blob, ts.width(), ts.height(), m_image.scanLine(r.top()) + r.left() * 4,
                                 static_cast<int>(m_image.bytesPerLine()))) {
            for (int y = r.top(); y <= r.bottom(); ++y)
                std::fill_n(reinterpret_cast<quint32*>(m_image.scanLine(y)) + r.left(), r.width(), 0xFF000000u);
        }
    }
    return m_image;
}

}
