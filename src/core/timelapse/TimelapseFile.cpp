#include "core/timelapse/TimelapseFile.h"

#include "core/io/ByteIO.h"
#include "core/io/DocumentFormat.h"

#include <unordered_set>

namespace doodlebold::timelapse_file {

namespace {

using io::ByteOrder;
using io::ByteReader;
using io::ByteWriter;

constexpr int kMaxSide = kMaxCanvasSide;

FileError corrupt(const QString& what) {
    return {FileError::Kind::Corrupt, what};
}

}

bool write(QIODevice& device, const Timelapse& recording) {
    ByteWriter head(ByteOrder::Little);
    head.u32(static_cast<std::uint32_t>(recording.steps.size()));
    head.u8(recording.paused ? 1 : 0);
    head.u64(static_cast<std::uint64_t>(std::max<std::int64_t>(0, recording.startedMs)));
    if (!doodle_file::writeChunk(device, kHeaderChunk, head.data()))
        return false;
    for (const TimelapseStepPtr& step : recording.steps) {
        ByteWriter w(ByteOrder::Little);
        w.u8(static_cast<std::uint32_t>(step->kind));
        w.u8((step->full ? 1u : 0u) | (step->resumed ? 2u : 0u) | (step->strokeStart ? 4u : 0u));
        w.u32(step->activeMs);
        w.u32(static_cast<std::uint32_t>(step->size.width()));
        w.u32(static_cast<std::uint32_t>(step->size.height()));
        w.u32(static_cast<std::uint32_t>(step->tiles.size()));
        for (const TimelapseTile& t : step->tiles) {
            w.i32(t.x);
            w.i32(t.y);
            w.u32(static_cast<std::uint32_t>(t.blob.size()));
            w.bytes(t.blob);
        }
        if (!doodle_file::writeChunk(device, kStepChunk, w.data()))
            return false;
    }
    return true;
}

FileResult<Timelapse> read(QIODevice& device, const doodle_file::Chunk& header, const QSize& canvas) {
    using R = FileResult<Timelapse>;
    ByteReader h(header.payload, ByteOrder::Little);
    const std::uint32_t count = h.u32();
    const std::uint8_t flags = h.u8();
    const std::uint64_t started = h.atEnd() ? 0 : h.u64();
    if (!h.ok() || !h.atEnd() || count < 1 || count > kMaxSteps || started > 0x7FFFFFFFFFFFFFFFull)
        return R::failure(corrupt(QStringLiteral("(the recording's header)")));
    Timelapse recording;
    recording.startedMs = static_cast<std::int64_t>(started);
    recording.paused = flags & 1;
    recording.steps.reserve(std::min<std::size_t>(count, 1u << 16));
    std::uint32_t lastTime = 0;
    for (std::uint32_t i = 0; i < count; ++i) {
        auto chunk = doodle_file::readChunk(device);
        if (!chunk)
            return R::failure(chunk.error());
        if (!chunk.value().is(kStepChunk))
            return R::failure(corrupt(QStringLiteral("(the recording has fewer steps than it says)")));
        ByteReader r(chunk.value().payload, ByteOrder::Little);
        auto step = std::make_shared<TimelapseStep>();
        const std::uint8_t kind = r.u8();
        const std::uint8_t stepFlags = r.u8();
        step->activeMs = r.u32();
        const std::uint32_t w = r.u32(), hgt = r.u32(), tiles = r.u32();
        if (!r.ok() || kind > static_cast<std::uint8_t>(TimelapseStep::Kind::Partial) || (stepFlags & ~7u) ||
            ((stepFlags & 4u) && kind != static_cast<std::uint8_t>(TimelapseStep::Kind::Partial)) || w < 1 || hgt < 1 || w > kMaxSide ||
            hgt > kMaxSide || step->activeMs < lastTime)
            return R::failure(corrupt(QStringLiteral("(recorded step %1)").arg(i)));
        lastTime = step->activeMs;
        step->kind = static_cast<TimelapseStep::Kind>(kind);
        step->full = stepFlags & 1;
        step->resumed = stepFlags & 2;
        step->strokeStart = stepFlags & 4;
        step->size = QSize(static_cast<int>(w), static_cast<int>(hgt));
        const bool first = i == 0;
        if (first != (step->kind == TimelapseStep::Kind::First) || (first && !step->full))
            return R::failure(corrupt(QStringLiteral("(the recording's first picture)")));
        const int tilesX = (step->size.width() + kTimelapseTileSize - 1) / kTimelapseTileSize;
        const int tilesY = (step->size.height() + kTimelapseTileSize - 1) / kTimelapseTileSize;
        if (tiles > static_cast<std::uint32_t>(tilesX) * static_cast<std::uint32_t>(tilesY) ||
            (step->full && tiles != static_cast<std::uint32_t>(tilesX) * tilesY))
            return R::failure(corrupt(QStringLiteral("(recorded step %1: tile count)").arg(i)));
        step->tiles.reserve(tiles);
        std::unordered_set<std::uint64_t> seen;
        for (std::uint32_t k = 0; k < tiles; ++k) {
            const std::int32_t x = r.i32(), y = r.i32();
            const std::uint32_t length = r.u32();
            const std::uint8_t* blob = r.span(length);
            if (!r.ok() || x < 0 || y < 0 || x >= tilesX || y >= tilesY ||
                !seen.insert((std::uint64_t(std::uint32_t(x)) << 32) | std::uint32_t(y)).second)
                return R::failure(corrupt(QStringLiteral("(recorded step %1: tile list)").arg(i)));
            QByteArray data(reinterpret_cast<const char*>(blob), static_cast<qsizetype>(length));
            const QSize ts = timelapseTileSize(x, y, step->size);
            if (!isTimelapseBlob(data, ts.width(), ts.height()))
                return R::failure(corrupt(QStringLiteral("(recorded step %1: tile data)").arg(i)));
            step->tiles.push_back({x, y, std::move(data)});
        }
        if (!r.atEnd())
            return R::failure(corrupt(QStringLiteral("(extra data in recorded step %1)").arg(i)));
        recording.steps.push_back(std::move(step));
    }
    if (recording.finalSize() != canvas)
        return Timelapse{};  // not of this picture (the file was put together by hand): left out, the picture still opens
    return recording;
}

}
