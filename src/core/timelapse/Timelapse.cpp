#include "core/timelapse/Timelapse.h"

#include <QSize>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace doodlebold {

namespace {

enum class Codec : std::uint8_t {
    Solid = 0,         // the whole tile is one colour: 3 bytes
    RowDelta = 1,      // each value less the one 3 bytes before it (the pixel to the left), then qCompress
};

constexpr int kCompressionLevel = 5;

}

std::int64_t Timelapse::byteSize() const {
    std::int64_t bytes = 0;
    for (const TimelapseStepPtr& s : steps)
        for (const TimelapseTile& t : s->tiles)
            bytes += t.blob.size() + 8;
    return bytes;
}

int Timelapse::stepCount() const {
    int n = 0;
    for (std::size_t i = 1; i < steps.size(); ++i)
        n += steps[i]->kind != TimelapseStep::Kind::Partial;
    return n;
}

int Timelapse::editCount() const {
    int n = 0;
    for (std::size_t i = 1; i < steps.size(); ++i)
        n += steps[i]->kind == TimelapseStep::Kind::Edit;
    return n;
}

std::vector<int> Timelapse::path(bool leaveOutUndone) const {
    std::vector<int> out;
    if (steps.empty())
        return out;
    out.push_back(0);
    // The frames of a stroke belong to the step that ends it: `from[i]` is the first of step i's frames (i itself without any).
    const int count = static_cast<int>(steps.size());
    std::vector<int> from(steps.size());
    std::vector<bool> owned(steps.size(), false);
    int open = -1;
    for (int i = 1; i < count; ++i) {
        const TimelapseStep& s = *steps[static_cast<std::size_t>(i)];
        from[static_cast<std::size_t>(i)] = i;
        if (s.kind == TimelapseStep::Kind::Partial) {
            if (s.strokeStart || open < 0)
                open = i;
            continue;
        }
        if (s.kind == TimelapseStep::Kind::Edit && open >= 0)
            from[static_cast<std::size_t>(i)] = open;
        for (int j = from[static_cast<std::size_t>(i)]; j <= i; ++j)
            owned[static_cast<std::size_t>(j)] = true;
        open = -1;
    }
    if (!leaveOutUndone) {
        for (int i = 1; i < count; ++i)
            if (owned[static_cast<std::size_t>(i)])
                out.push_back(i);
        return out;
    }
    // The undo history, replayed: `stack` holds the edits that can still be undone or redone, `at` how many are in force.
    std::vector<int> stack;
    std::size_t at = 0;
    std::size_t floor = 0;  // a file opened again has lost the history before: what was in force then is final
    for (int i = 1; i < count; ++i) {
        const TimelapseStep& s = *steps[static_cast<std::size_t>(i)];
        if (s.resumed) {
            stack.resize(at);
            floor = at;
        }
        switch (s.kind) {
        case TimelapseStep::Kind::Edit:
            stack.resize(at);
            stack.push_back(i);
            ++at;
            break;
        case TimelapseStep::Kind::Undo:
            if (at > floor)
                --at;
            break;
        case TimelapseStep::Kind::Redo:
            if (at < stack.size())
                ++at;
            break;
        case TimelapseStep::Kind::First:
        case TimelapseStep::Kind::Partial: break;
        }
    }
    for (std::size_t k = 0; k < at; ++k)
        for (int j = from[static_cast<std::size_t>(stack[k])]; j <= stack[k]; ++j)
            out.push_back(j);
    return out;
}

QSize timelapseTileSize(int tx, int ty, const QSize& canvas) {
    return {std::clamp(canvas.width() - tx * kTimelapseTileSize, 0, kTimelapseTileSize),
            std::clamp(canvas.height() - ty * kTimelapseTileSize, 0, kTimelapseTileSize)};
}

QByteArray encodeTimelapseTile(const std::uint8_t* rgba, int stride, int width, int height) {
    bool solid = true;
    for (int y = 0; y < height && solid; ++y) {
        const std::uint8_t* row = rgba + static_cast<std::ptrdiff_t>(y) * stride;
        for (int x = 0; x < width; ++x)
            if (std::memcmp(row + x * 4, rgba, 3) != 0) {
                solid = false;
                break;
            }
    }
    if (solid) {
        QByteArray out(4, Qt::Uninitialized);
        out[0] = static_cast<char>(Codec::Solid);
        std::memcpy(out.data() + 1, rgba, 3);
        return out;
    }
    QByteArray filtered(width * height * 3, Qt::Uninitialized);
    auto* f = reinterpret_cast<std::uint8_t*>(filtered.data());
    for (int y = 0; y < height; ++y) {
        const std::uint8_t* row = rgba + static_cast<std::ptrdiff_t>(y) * stride;
        for (int x = 0; x < width; ++x)
            for (int c = 0; c < 3; ++c)
                *f++ = static_cast<std::uint8_t>(row[x * 4 + c] - (x ? row[(x - 1) * 4 + c] : 0));
    }
    QByteArray out = qCompress(filtered, kCompressionLevel);
    out.prepend(static_cast<char>(Codec::RowDelta));
    return out;
}

bool isTimelapseBlob(const QByteArray& blob, int width, int height) {
    if (blob.isEmpty())
        return false;
    switch (static_cast<Codec>(static_cast<std::uint8_t>(blob[0]))) {
    case Codec::Solid: return blob.size() == 4;
    case Codec::RowDelta: {
        if (blob.size() < 1 + 4)
            return false;
        // qCompress prefixes the unpacked size (big endian).
        const auto* z = reinterpret_cast<const std::uint8_t*>(blob.constData()) + 1;
        const std::uint32_t claimed = (std::uint32_t(z[0]) << 24) | (std::uint32_t(z[1]) << 16) | (std::uint32_t(z[2]) << 8) | z[3];
        return claimed == std::uint32_t(width) * std::uint32_t(height) * 3u;
    }
    }
    return false;
}

bool decodeTimelapseTile(const QByteArray& blob, int width, int height, std::uint8_t* rgba, int stride) {
    if (!isTimelapseBlob(blob, width, height))
        return false;
    if (static_cast<Codec>(static_cast<std::uint8_t>(blob[0])) == Codec::Solid) {
        const auto* c = reinterpret_cast<const std::uint8_t*>(blob.constData()) + 1;
        for (int y = 0; y < height; ++y) {
            std::uint8_t* row = rgba + static_cast<std::ptrdiff_t>(y) * stride;
            for (int x = 0; x < width; ++x) {
                row[x * 4] = c[0];
                row[x * 4 + 1] = c[1];
                row[x * 4 + 2] = c[2];
                row[x * 4 + 3] = 255;
            }
        }
        return true;
    }
    const QByteArray data = qUncompress(reinterpret_cast<const std::uint8_t*>(blob.constData()) + 1, blob.size() - 1);
    if (data.size() != width * height * 3)
        return false;
    const auto* f = reinterpret_cast<const std::uint8_t*>(data.constData());
    for (int y = 0; y < height; ++y) {
        std::uint8_t* row = rgba + static_cast<std::ptrdiff_t>(y) * stride;
        for (int x = 0; x < width; ++x) {
            for (int c = 0; c < 3; ++c)
                row[x * 4 + c] = static_cast<std::uint8_t>(*f++ + (x ? row[(x - 1) * 4 + c] : 0));
            row[x * 4 + 3] = 255;
        }
    }
    return true;
}

int timelapseFrameCount(TimelapseLength length, int pathSteps, int fps) {
    return length == TimelapseLength::Seconds30 ? kTimelapseShortSeconds * std::max(1, fps) : std::max(1, pathSteps);
}

QSize fitTimelapseSize(const QSize& canvas, const QSize& box) {
    QSize s = canvas;
    if (canvas.width() > box.width() || canvas.height() > box.height())
        s = canvas.scaled(box, Qt::KeepAspectRatio);
    return {std::max(2, s.width() & ~1), std::max(2, s.height() & ~1)};
}

std::vector<int> resampleTimelapse(const std::vector<int>& path, int frames) {
    std::vector<int> out;
    if (path.empty() || frames <= 0)
        return out;
    out.reserve(static_cast<std::size_t>(frames));
    const std::int64_t last = static_cast<std::int64_t>(path.size()) - 1;
    for (int f = 0; f < frames; ++f) {
        // Frame f of N shows state round(f · (M − 1) / (N − 1)): the first frame the first state, the last the last.
        const std::int64_t i = frames == 1 ? last : (std::int64_t(f) * last + (frames - 1) / 2) / (frames - 1);
        out.push_back(path[static_cast<std::size_t>(i)]);
    }
    return out;
}

}
