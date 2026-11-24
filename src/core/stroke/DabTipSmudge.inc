// Part of DabTip.cpp, which includes it where it stands: the Smudge.

// Smudge moves what's under each stamp along the stamp's step, by a share v of it that falls off from the middle, read back with
// bilinear sampling. In the middle v is near 1, so paint rides along with the tip a way and smears; toward the edge of its reach it
// only bends what's there, a few pixels, keeping most of its contrast; past it nothing moves. v follows the density share, so a
// firm touch drags further. It reads and writes the layer as it stands, so dragged paint keeps its colour and transparency.
void DabPainter::smudgeDab(const Dab& dab) {
    std::vector<Laying> layings = layingsFor(dab.area.adjusted(-4, -4, 4, 4));
    for (const Laying& laying : layings)
        m_changed.insert(laying.coord);
    // The tiles by where they are in the dab's grid of them; a place off the grid reads as nothing, as one off the layings always has.
    const int tx0 = layings.empty() ? 0 : layings.front().coord.x, ty0 = layings.empty() ? 0 : layings.front().coord.y;
    const int across = layings.empty() ? 0 : layings.back().coord.x - tx0 + 1,
              down = layings.empty() ? 0 : layings.back().coord.y - ty0 + 1;
    const auto pixelAt = [&](int x, int y) -> Channel* {
        const TileCoord coord{TiledImage::tileOf(x), TiledImage::tileOf(y)};
        const int gx = coord.x - tx0, gy = coord.y - ty0;
        if (gx < 0 || gy < 0 || gx >= across || gy >= down)
            return nullptr;
        return layings[static_cast<std::size_t>(gy * across + gx)].tile->px.data() + static_cast<std::size_t>(indexIn(coord, x, y)) * 4;
    };
    const auto read = [&](int x, int y) -> Premultiplied {
        const Channel* px = pixelAt(x, y);
        return px ? readPixel(px) : Premultiplied{};
    };
    const double share = std::clamp(dab.pressure, 0.0, 1.0);
    const float vmax = static_cast<float>(std::min(kSmudgeMostShare, m_mixing.smudgeShare * share / kSmudgeShareAt));
    const double reachShare = m_mixing.smudgeReach;
    const float power = static_cast<float>(m_mixing.smudgeFalloff);
    for (std::size_t i = dab.first; i < dab.first + dab.count; ++i) {
        const Stamp& stamp = m_stamps[i];
        if (!m_hasLastCentre) {
            m_lastCentre = stamp.centre;
            m_hasLastCentre = true;
            continue;
        }
        const QPointF step = stamp.centre - m_lastCentre;
        m_lastCentre = stamp.centre;
        const double reach = reachShare * stamp.radius;
        const int x0 = static_cast<int>(std::floor(stamp.centre.x() - reach)) - 1,
                  x1 = static_cast<int>(std::ceil(stamp.centre.x() + reach)) + 1;
        const int y0 = static_cast<int>(std::floor(stamp.centre.y() - reach)) - 1,
                  y1 = static_cast<int>(std::ceil(stamp.centre.y() + reach)) + 1;
        const int w = x1 - x0 + 1, h = y1 - y0 + 1;
        // The blocks are the last stamp's, grown if need be: `moved` is all 0 between stamps, as each row clears what it wrote.
        const std::size_t size = static_cast<std::size_t>(w) * static_cast<std::size_t>(h);
        if (m_smudgeNext.size() < size) {
            m_smudgeNext.resize(size);
            m_smudgeMoved.resize(size, 0);
        }
        Premultiplied* next = m_smudgeNext.data();
        char* moved = m_smudgeMoved.data();
        const QPointF warpCentre(stamp.centre.x() + kSmudgeCentreShift, stamp.centre.y() + kSmudgeCentreShift);
        const PixelFrame warp(warpCentre);
        const float reachInFloat = static_cast<float>(reach);
        const float stepX = static_cast<float>(step.x()), stepY = static_cast<float>(step.y());
        // How far back each pixel reads, worked out on a grid at large sizes.
        const int cell = markCellFor(2.0 * stamp.radius);
        const int gridLeft = cellOfPixel(x0, cell), gridTop = cellOfPixel(y0, cell);
        const int gridAcross = cellOfPixel(x1, cell) - gridLeft + 2, gridDown = cellOfPixel(y1, cell) - gridTop + 2;
        if (cell > 1) {
            m_smudgeGrid.resize(static_cast<std::size_t>(gridAcross) * static_cast<std::size_t>(gridDown));
            const auto gridRow = [&](int j) {
                for (int gx = 0; gx < gridAcross; ++gx) {
                    const float d = std::hypot(warp.dx((gridLeft + gx) * cell), warp.dy((gridTop + j) * cell));
                    m_smudgeGrid[static_cast<std::size_t>(j) * gridAcross + gx] =
                        d >= reachInFloat ? 0.0f : vmax * std::pow(1.0f - d / reachInFloat, power);
                }
            };
            if (m_onWorkers && gridDown > 16)
                WorkerPool::forStrokes().run(gridDown, gridRow);
            else
                for (int j = 0; j < gridDown; ++j)
                    gridRow(j);
        }
        const float exactFromSquared = (kExactFromReach * reachInFloat) * (kExactFromReach * reachInFloat);
        const auto readFromGrid = [&](int x, int y) {
            const int cx = cellOfPixel(x, cell) - gridLeft, cy = cellOfPixel(y, cell) - gridTop;
            const float tx = static_cast<float>(x - (cx + gridLeft) * cell) / static_cast<float>(cell),
                        ty = static_cast<float>(y - (cy + gridTop) * cell) / static_cast<float>(cell);
            const float* top = m_smudgeGrid.data() + static_cast<std::size_t>(cy) * gridAcross + cx;
            const float* bottom = top + gridAcross;
            return lerp(lerp(top[0], top[1], tx), lerp(bottom[0], bottom[1], tx), ty);
        };
        // The part of a row the circle reaches, a pixel to spare each way; the exact test is made at each pixel of it.
        const auto spanOf = [&](int y) -> std::pair<int, int> {
            const double dy = y + 0.5 - warpCentre.y();
            const double half = std::sqrt(std::max(reach * reach - dy * dy, 0.0));
            return {std::max(x0, static_cast<int>(std::floor(warpCentre.x() - 0.5 - half)) - 1),
                    std::min(x1, static_cast<int>(std::ceil(warpCentre.x() - 0.5 + half)) + 1)};
        };
        // A stamp reads what's there before it writes anything, so its rows can be worked out, and then written, in any order.
        const auto readRow = [&](int row) {
            const int y = y0 + row;
            const auto [left, right] = spanOf(y);
            for (int x = left; x <= right; ++x) {
                const float dx = warp.dx(x), dy = warp.dy(y);
                const float distanceSquared = dx * dx + dy * dy;
                if (distanceSquared >= reachInFloat * reachInFloat)
                    continue;
                // How far back along the step it reads from: vmax in the middle, falling off to nothing at the reach.
                float v = cell > 1 && distanceSquared < exactFromSquared
                              ? readFromGrid(x, y)
                              : vmax * std::pow(1.0f - std::sqrt(distanceSquared) / reachInFloat, power);
                if (m_tip.form)  // the form shapes where it smudges
                    v *= formShapeAt(stamp, PixelFrame(stamp.centre).dx(x), PixelFrame(stamp.centre).dy(y));
                // Kept as how far from the pixel it reads, so the place loses nothing to a float however far across the canvas.
                const float backX = -v * stepX, backY = -v * stepY;
                const float wholeX = std::floor(backX), wholeY = std::floor(backY);
                const int ix = x + static_cast<int>(wholeX), iy = y + static_cast<int>(wholeY);
                const float fx = backX - wholeX, fy = backY - wholeY;
                const Premultiplied a = lerp(read(ix, iy), read(ix + 1, iy), fx), b = lerp(read(ix, iy + 1), read(ix + 1, iy + 1), fx);
                const std::size_t k = static_cast<std::size_t>((y - y0) * w + (x - x0));
                next[k] = lerp(a, b, fy);
                moved[k] = 1;
            }
        };
        const auto writeRow = [&](int row) {
            const int y = y0 + row;
            const auto [left, right] = spanOf(y);
            for (int x = left; x <= right; ++x) {
                const std::size_t k = static_cast<std::size_t>((y - y0) * w + (x - x0));
                Channel* px = moved[k] ? pixelAt(x, y) : nullptr;
                if (px)
                    writePixel(px, next[k]);
                moved[k] = 0;
            }
        };
        const bool onWorkers = m_onWorkers && static_cast<std::int64_t>(w) * h >= kParallelPixels && h > 1;
        for (const auto& pass : {std::function<void(int)>(readRow), std::function<void(int)>(writeRow)}) {
            if (onWorkers)
                WorkerPool::forStrokes().run(h, pass);
            else
                for (int row = 0; row < h; ++row)
                    pass(row);
        }
    }
}
