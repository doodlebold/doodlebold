// Part of DabTip.cpp, which includes it where it stands: each tile's record of what the stroke laid there: settling, packing it away under
// memory pressure, and the workers.

// A pixel's shape only rises, to 1, and the Marker's opacity and colour strength do too: once a tile's are all 1 they can't change, so the
// array goes and every pixel reads the shared ones. Writes to those are only made where a value would change, which it can't.
void DabPainter::shareSettledColumns(std::vector<Laying>& layings) const {
    if (m_markerCell > 0)
        return;  // a grid's pixels between points keep no record of their own, so none settles
    const bool marker = m_mixing.kind == TipMixing::Kind::Marker && !m_mixing.transparentColour;
    const auto settle = [&](Column<float>& column) {
        if (column.owned() && std::all_of(column.begin(), column.end(), [](float v) { return v == 1.0f; }))
            column.share(allOnes().data());
    };
    const auto settleOne = [&](int i) {
        Reach& reach = *layings[static_cast<std::size_t>(i)].reach;
        settle(reach.shape);
        if (marker)
            settle(reach.value);
    };
    if (m_onWorkers && layings.size() > 1)
        WorkerPool::forStrokes().run(static_cast<int>(layings.size()), settleOne);
    else
        for (int i = 0; i < static_cast<int>(layings.size()); ++i)
            settleOne(i);
}

// A blur of what's under a dab reads what the stroke has laid this far past it. Bounds blurUnder's reach, Gaussian or box, and the coarse blur's.
// Whether the layer had nothing as far as a dab's blur of what's under reads. The Marker melts colour only, so what's under stays
// nothing there and its blur would be nothing: it isn't worked out.
bool DabPainter::nothingUnder(const Dab& dab) const {
    const int far = readsAround(dab);
    const QRect read = dab.area.adjusted(-far, -far, far, far);
    for (int ty = TiledImage::tileOf(read.top()); ty <= TiledImage::tileOf(read.bottom()); ++ty)
        for (int tx = TiledImage::tileOf(read.left()); tx <= TiledImage::tileOf(read.right()); ++tx)
            if (m_before.tile(tx, ty))
                return false;
    return true;
}

int DabPainter::readsAround(const Dab& dab) const {
    if (m_mixing.kind == TipMixing::Kind::Brush)
        return 0;
    const double radii = m_mixing.wetBlurRadii * std::max(blurReachAt(dab.pressure), kWideBlur);
    const double passes = std::max(1, m_mixing.blurPasses);
    const double radius = m_stamps[dab.first].radius;
    // px past the blur's radius (12f989d); 0 to 3 px read the same pixels, as the tile added after covers it. Only the Marker
    // reads this, from 500 px, and off-grid dabs packing their record when memory is short.
    constexpr double kReadSpare = 2.0;
    return static_cast<int>(std::ceil((radius * radii + kReadSpare) * (kGaussianReach * std::sqrt(passes) + passes))) + Tile::kSize;
}

void DabPainter::packIdleReach(const Dab& dab) {
    if (m_gridCell > 0 || m_markerCell > 0 || !m_pressed || !m_pressed())  // a grid's record isn't packed
        return;
    const int far = readsAround(dab) + Tile::kSize;
    const QRect kept = dab.area.adjusted(-far, -far, far, far);
    std::vector<std::pair<TileCoord, const Reach*>> idle;
    for (const auto& [coord, reach] : m_reach) {
        if (m_dabNumber - reach->lastDab > kDabsBeforePacking && !kept.intersects(coord.pixelRect())) {
            idle.emplace_back(coord, reach.get());
            if (idle.size() == kMostPackedPerDab)
                break;
        }
    }
    std::vector<QByteArray> bytes(idle.size());
    WorkerPool::forStrokes().run(static_cast<int>(idle.size()),
                                 [&](int i) { bytes[static_cast<std::size_t>(i)] = packed(*idle[static_cast<std::size_t>(i)].second); });
    for (std::size_t i = 0; i < idle.size(); ++i) {
        m_packed[idle[i].first] = std::move(bytes[i]);
        m_reach.erase(idle[i].first);
    }
#ifdef __GLIBC__
    if (!idle.empty())
        malloc_trim(0);  // what the records freed goes back to the system, or a stroke's memory wouldn't shrink
#endif
}

void DabPainter::unpackAround(const Dab& dab) {
    if (m_packed.empty())
        return;
    const int far = readsAround(dab) + Tile::kSize;
    const QRect kept = dab.area.adjusted(-far, -far, far, far);
    std::vector<TileCoord> wanted;
    for (const auto& [coord, bytes] : m_packed) {
        if (kept.intersects(coord.pixelRect()))
            wanted.push_back(coord);
    }
    std::vector<std::unique_ptr<Reach>> reaches(wanted.size());
    WorkerPool::forStrokes().run(static_cast<int>(wanted.size()), [&](int i) {
        reaches[static_cast<std::size_t>(i)] =
            unpacked(m_packed.at(wanted[static_cast<std::size_t>(i)]), wanted[static_cast<std::size_t>(i)]);
    });
    for (std::size_t i = 0; i < wanted.size(); ++i) {
        m_reach[wanted[i]] = std::move(reaches[i]);
        m_packed.erase(wanted[i]);
    }
}

// The record's numbers as they are, so that unpacking gives the same bits.
QByteArray DabPainter::packed(const Reach& reach) const {
    QByteArray raw;
    const std::size_t floats = Tile::kSize * Tile::kSize * sizeof(float);
    raw.reserve(static_cast<qsizetype>(floats * 12 + 19));
    const auto add = [&raw](const void* data, std::size_t bytes) {
        raw.append(static_cast<const char*>(data), static_cast<qsizetype>(bytes));
    };
    const char valueOwned = reach.value.owned() ? 1 : 0, shapeOwned = reach.shape.owned() ? 1 : 0;
    add(&valueOwned, 1);
    add(&shapeOwned, 1);
    if (reach.value.owned())
        add(reach.value.data(), floats);
    if (reach.shape.owned())
        add(reach.shape.data(), floats);
    add(reach.held.data(), floats);
    if (reach.passed.owned())
        add(reach.passed.data(), floats);
    add(reach.paint.data(), floats * 4);
    const char underOwned = reach.under.owned() ? 1 : 0;
    add(&underOwned, 1);
    if (reach.under.owned())
        add(reach.under.data(), floats * 4);
    add(&reach.lastDab, sizeof reach.lastDab);
    add(&reach.allReached, sizeof reach.allReached);
    return qCompress(raw, kPackLevel);
}

std::unique_ptr<DabPainter::Reach> DabPainter::unpacked(const QByteArray& bytes, TileCoord coord) const {
    auto reach = std::make_unique<Reach>(keepsUnderAsPixels(), m_mixing.kind != TipMixing::Kind::Blur,
                                         m_mixing.passLength > 0.0 || m_mixing.outlineAlongPath, m_before.tile(coord.x, coord.y), false);
    const QByteArray raw = qUncompress(bytes);
    const char* at = raw.constData();
    const auto take = [&at](void* data, std::size_t size) {
        std::memcpy(data, at, size);
        at += size;
    };
    const std::size_t floats = Tile::kSize * Tile::kSize * sizeof(float);
    char valueOwned = 0, shapeOwned = 0;
    take(&valueOwned, 1);
    take(&shapeOwned, 1);
    if (valueOwned)
        take(reach->value.data(), floats);
    else
        reach->value.share(allOnes().data());
    if (shapeOwned) {
        if (!reach->shape.owned())
            reach->shape.own();
        take(reach->shape.data(), floats);
    } else {
        reach->shape.share(allOnes().data());
    }
    take(reach->held.data(), floats);
    if (reach->passed.owned())
        take(reach->passed.data(), floats);
    take(reach->paint.data(), floats * 4);
    char underOwned = 0;
    take(&underOwned, 1);
    if (underOwned) {
        if (!reach->under.owned())
            reach->under.own();
        take(reach->under.data(), floats * 4);
    } else {
        reach->under.share(kNothing.data());
    }
    take(&reach->lastDab, sizeof reach->lastDab);
    take(&reach->allReached, sizeof reach->allReached);
    return reach;
}

// Each pixel's laying reads and writes only that pixel's own values, so the bands can go in any order and give the same pixels.
void DabPainter::layOnWorkers(const Dab& dab, const Premultiplied& paint, std::vector<Laying>& layings) const {
    struct Band {
        Laying* laying;
        int top;
        int bottom;
    };
    std::vector<Band> bands;
    const int rowsPerJob = m_tip.pictureStamps ? kPictureRowsPerJob : kRowsPerJob;
    for (Laying& laying : layings) {
        const QRect area = dab.area.intersected(laying.coord.pixelRect());
        for (int top = area.top(); top <= area.bottom(); top += rowsPerJob)
            bands.push_back({&laying, top, std::min(top + rowsPerJob - 1, area.bottom())});
    }
    const auto layOne = [&](int i) {
        const Band& band = bands[static_cast<std::size_t>(i)];
        layRows(dab, paint, *band.laying, band.top, band.bottom);
    };
    WorkerPool::forStrokes().run(static_cast<int>(bands.size()), layOne);
}
