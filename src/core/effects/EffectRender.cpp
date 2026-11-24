#include "core/effects/EffectRender.h"

#include "core/effects/ColorEffect.h"
#include "core/effects/EffectSampling.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>

namespace doodlebold {

namespace {

constexpr int kS = Tile::kSize;
constexpr int kMaxRadialSamples = 128;
constexpr float kMaxAberrationScale = 0.5f;

using Patch = std::array<float, kTilePixels * 4>;

Px at(const float* patch, int i) {
    const float* p = patch + i * 4;
    return {p[0], p[1], p[2], p[3]};
}

void put(float* patch, int i, const Px& px) {
    float* p = patch + i * 4;
    p[0] = px.r;
    p[1] = px.g;
    p[2] = px.b;
    p[3] = px.a;
}

Rgb straight(const Px& p) {
    if (p.a <= 0)
        return {};
    return {p.r / p.a, p.g / p.a, p.b / p.a};
}

Px premultiplied(Rgb c, float a) {
    return {c.r * a, c.g * a, c.b * a, a};
}

// The pixel centre's position in the layer.
float centreOf(int pixel) {
    return static_cast<float>(pixel) + 0.5f;
}

double farthestCorner(const QRect& r, QPointF from) {
    double far = 0;
    for (const QPointF corner : {QPointF(r.left(), r.top()), QPointF(r.right() + 1, r.top()), QPointF(r.left(), r.bottom() + 1),
                                 QPointF(r.right() + 1, r.bottom() + 1)})
        far = std::max(far, std::hypot(corner.x() - from.x(), corner.y() - from.y()));
    return far;
}

Channel quantized(float v, float ceiling) {
    return static_cast<Channel>(std::clamp(v, 0.0f, ceiling) * kChannelOne + 0.5f);
}

QPointF radialCentre(const EffectSettings& s, QSize canvas) {
    return {canvas.width() * s.value(1) / 100, canvas.height() * s.value(2) / 100};
}

bool isRadialAberration(const EffectSettings& s) {
    return s.mode == chromatic::kRadial;
}

QPointF aberrationCentre(const EffectSettings& s) {
    return {s.value(chromatic::kCenterX), s.value(chromatic::kCenterY)};
}

// Radial Chromatic Aberration's spread: red moves outwards from the Centre and blue inwards by this share of their distance
// from it, so that at the canvas's corner farthest from the Centre each moves Amount pixels.
float aberrationScale(const EffectSettings& s, QSize canvas) {
    const double farthest = std::max(1.0, farthestCorner(QRect(QPoint(0, 0), canvas), aberrationCentre(s)));
    return std::min(kMaxAberrationScale, static_cast<float>(s.value(chromatic::kAmount) / farthest));
}

}

EffectRenderer::EffectRenderer(const EffectSource& source, const EffectSettings& settings)
    : m_source(source),
      m_settings(settings),
      m_canvasOnly(!isPointEffect(settings.kind)),
      m_grid(std::make_unique<TileGrid>(source.pixels, m_canvasOnly ? QRect(QPoint(0, 0), source.canvas) : QRect())) {
    if (settings.kind == EffectKind::GaussianBlur)
        m_blur = std::make_unique<BlurField>(*m_grid, settings.value(0));
    else if (settings.kind == EffectKind::UnsharpMask)
        m_blur = std::make_unique<BlurField>(*m_grid, settings.value(1));
    planTiles();
    if (m_blur)
        m_blur->setArea(reachArea());
}

EffectRenderer::~EffectRenderer() = default;

void EffectRenderer::prepare(const ParallelRun& parallel) {
    if (m_blur)
        m_blur->prepare(parallel);
}

// Where the effect can leave something: the layer's content and as far round it as the effect reads.
QRect EffectRenderer::reachArea() const {
    const QRect content = m_source.pixels.contentBounds();
    if (content.isEmpty())
        return {};
    double reach = 0;
    switch (m_settings.kind) {
    case EffectKind::GaussianBlur: reach = m_blur->reach(); break;
    case EffectKind::UnsharpMask: return content & QRect(QPoint(0, 0), m_source.canvas);  // alpha stays: nothing appears round it
    case EffectKind::MotionBlur: reach = m_settings.value(1) / 2 + 2; break;
    case EffectKind::RadialBlur: {
        const double half = m_settings.value(0) / 200;
        reach = farthestCorner(content, radialCentre(m_settings, m_source.canvas)) * half / (1 - half) + 2;
        break;
    }
    case EffectKind::ChromaticAberration: {
        if (!isRadialAberration(m_settings)) {
            reach = m_settings.value(chromatic::kAmount) + 2;
            break;
        }
        const double k = aberrationScale(m_settings, m_source.canvas);
        reach = farthestCorner(content, aberrationCentre(m_settings)) * k / (1 - k) + 2;
        break;
    }
    default: return content;
    }
    const int r = static_cast<int>(std::ceil(reach));
    return content.adjusted(-r, -r, r, r) & QRect(QPoint(0, 0), m_source.canvas);
}

void EffectRenderer::planTiles() {
    if (isIdentity(m_settings))
        return;
    const SelectionMask* selection = m_source.selection ? &*m_source.selection : nullptr;
    const auto selected = [&](int tx, int ty) { return !selection || selection->tile(tx, ty); };
    if (isPointEffect(m_settings.kind)) {
        for (const auto& [coord, tile] : m_source.pixels.tiles())
            if (selected(coord.x, coord.y))
                m_tiles.push_back(coord);
        return;
    }
    const QRect area = reachArea();
    if (area.isEmpty())
        return;
    for (int ty = TiledImage::tileOf(area.top()); ty <= TiledImage::tileOf(area.bottom()); ++ty)
        for (int tx = TiledImage::tileOf(area.left()); tx <= TiledImage::tileOf(area.right()); ++tx)
            if (selected(tx, ty))
                m_tiles.push_back({tx, ty});
}

TilePtr EffectRenderer::render(TileCoord c) const {
    TilePtr before = m_source.pixels.tileShared(c.x, c.y);
    if (!before && (isPointEffect(m_settings.kind) || m_source.keepAlpha))
        return before;  // these leave transparent pixels as they are
    auto effect = std::make_unique<Patch>();
    effectPatch(c, effect->data());
    return finish(c, effect->data());
}

void EffectRenderer::effectPatch(TileCoord c, float* out) const {
    switch (m_settings.kind) {
    case EffectKind::GaussianBlur: m_blur->tile(c, out); break;
    case EffectKind::MotionBlur: motionPatch(c, out); break;
    case EffectKind::RadialBlur: radialPatch(c, out); break;
    case EffectKind::ChromaticAberration: chromaticPatch(c, out); break;
    case EffectKind::UnsharpMask: unsharpPatch(c, out); break;
    default: pointPatch(c, out); break;
    }
}

void EffectRenderer::pointPatch(TileCoord c, float* out) const {
    const ColorEffect effect(m_settings);
    const QRect r = c.pixelRect();
    m_grid->read(r, out);
    for (int i = 0; i < kTilePixels; ++i) {
        const Px p = at(out, i);
        if (p.a > 0)
            put(out, i, premultiplied(effect(straight(p), r.left() + i % kS, r.top() + i / kS), p.a));
    }
}

// The mean of the layer along a line Distance long through the pixel, at Angle (0° to the right, counterclockwise).
void EffectRenderer::motionPatch(TileCoord c, float* out) const {
    const double angle = m_settings.value(0) * std::numbers::pi / 180;
    const float length = static_cast<float>(m_settings.value(1));
    const float dx = static_cast<float>(std::cos(angle)), dy = static_cast<float>(-std::sin(angle));
    const int samples = static_cast<int>(std::ceil(length)) + 1;
    const float step = length / static_cast<float>(samples - 1), weight = 1.0f / static_cast<float>(samples);
    const QRect r = c.pixelRect();
    for (int i = 0; i < kTilePixels; ++i) {
        const float x = centreOf(r.left() + i % kS), y = centreOf(r.top() + i / kS);
        Px sum;
        for (int s = 0; s < samples; ++s) {
            const float t = -length / 2 + step * static_cast<float>(s);
            sum += m_grid->bilinear(x + dx * t, y + dy * t) * weight;
        }
        put(out, i, sum);
    }
}

// Zoom streaks: the mean of the layer along the line from the centre through the pixel, Amount of the pixel's distance from
// the centre long, half towards the centre and half away.
void EffectRenderer::radialPatch(TileCoord c, float* out) const {
    const QPointF centre = radialCentre(m_settings, m_source.canvas);
    const float cx = static_cast<float>(centre.x()), cy = static_cast<float>(centre.y());
    const float amount = static_cast<float>(m_settings.value(0) / 100);
    const QRect r = c.pixelRect();
    for (int i = 0; i < kTilePixels; ++i) {
        const float x = centreOf(r.left() + i % kS) - cx, y = centreOf(r.top() + i / kS) - cy;
        const float streak = std::hypot(x, y) * amount;
        const int samples = std::clamp(static_cast<int>(std::ceil(streak)) + 1, 2, kMaxRadialSamples);
        const float weight = 1.0f / static_cast<float>(samples);
        Px sum;
        for (int s = 0; s < samples; ++s) {
            const float scale = 1 - amount / 2 + amount * static_cast<float>(s) / static_cast<float>(samples - 1);
            sum += m_grid->bilinear(cx + x * scale, cy + y * scale) * weight;
        }
        put(out, i, sum);
    }
}

// Green stays where it is; red and blue move apart, as a lens that bends colours apart (the reference's Chromatic Aberration).
// Linear: red moves Amount along Angle (0° to the right, 90° up the screen) and blue as far the other way. Radial: red
// moves outwards from the Centre and blue inwards, by their distance from it times the spread. A channel that would come from
// off the canvas is missing there, so a filled layer's borders show a band without it. Each channel keeps its own coverage:
// the pixel's alpha is the largest of the three it took its colours from.
void EffectRenderer::chromaticPatch(TileCoord c, float* out) const {
    const float w = static_cast<float>(m_source.canvas.width()), h = static_cast<float>(m_source.canvas.height());
    // Off the canvas counts as nothing in the mix of the four pixels round a point: within half a pixel of the border the
    // channel fades out, beyond it it is gone. (The grid carries the border on outwards; scaling it by the share of the mix
    // that falls inside gives the same.)
    const auto read = [&](float x, float y) {
        const float inside = std::clamp(x + 0.5f, 0.0f, 1.0f) * std::clamp(w + 0.5f - x, 0.0f, 1.0f) * std::clamp(y + 0.5f, 0.0f, 1.0f) *
                             std::clamp(h + 0.5f - y, 0.0f, 1.0f);
        return inside <= 0 ? Px{} : m_grid->bilinear(x, y) * inside;
    };
    const QRect r = c.pixelRect();
    if (!isRadialAberration(m_settings)) {
        const double angle = m_settings.value(chromatic::kAngle) * std::numbers::pi / 180;
        const float amount = static_cast<float>(m_settings.value(chromatic::kAmount));
        const float dx = amount * static_cast<float>(std::cos(angle)), dy = -amount * static_cast<float>(std::sin(angle));
        for (int i = 0; i < kTilePixels; ++i) {
            const float x = centreOf(r.left() + i % kS), y = centreOf(r.top() + i / kS);
            const Px red = read(x - dx, y - dy), green = read(x, y), blue = read(x + dx, y + dy);
            put(out, i, {red.r, green.g, blue.b, std::max({red.a, green.a, blue.a})});
        }
        return;
    }
    const QPointF centre = aberrationCentre(m_settings);
    const float cx = static_cast<float>(centre.x()), cy = static_cast<float>(centre.y());
    const float k = aberrationScale(m_settings, m_source.canvas);
    for (int i = 0; i < kTilePixels; ++i) {
        const float x = centreOf(r.left() + i % kS), y = centreOf(r.top() + i / kS);
        const float ox = (x - cx) * k, oy = (y - cy) * k;
        const Px red = read(x - ox, y - oy), green = read(x, y), blue = read(x + ox, y + oy);
        put(out, i, {red.r, green.g, blue.b, std::max({red.a, green.a, blue.a})});
    }
}

// The colour moves away from the blurred colour by Amount where they differ by more than Threshold (of 255); alpha stays.
void EffectRenderer::unsharpPatch(TileCoord c, float* out) const {
    const float amount = static_cast<float>(m_settings.value(0) / 100);
    const float threshold = static_cast<float>(m_settings.value(2) / 255);
    auto blurred = std::make_unique<Patch>();
    m_blur->tile(c, blurred->data());
    m_grid->read(c.pixelRect(), out);
    const auto sharpen = [&](float v, float soft) {
        const float d = v - soft;
        return std::abs(d) > threshold || threshold <= 0 ? std::clamp(v + d * amount, 0.0f, 1.0f) : v;
    };
    for (int i = 0; i < kTilePixels; ++i) {
        const Px p = at(out, i);
        if (p.a <= 0)
            continue;
        const Px b = at(blurred->data(), i);
        const Rgb v = straight(p), soft = b.a > 0 ? straight(b) : v;
        put(out, i, premultiplied({sharpen(v.r, soft.r), sharpen(v.g, soft.g), sharpen(v.b, soft.b)}, p.a));
    }
}

namespace {

// One row of a tile: the old pixels moved towards the effect's by the selection's coverage. Returns whether any of it is
// left (not fully transparent).
bool mixRow(const float* old, const float* effect, const std::uint16_t* coverage, Channel* dst) {
    bool left = false;
    for (int x = 0; x < kS; ++x, old += 4, effect += 4, dst += 4) {
        const float w = static_cast<float>(coverage[x]) / SelectionMask::kOne;
        dst[3] = quantized(old[3] + (effect[3] - old[3]) * w, 1.0f);
        const float ceiling = static_cast<float>(dst[3]) / kChannelOne;
        for (int ch = 0; ch < 3; ++ch)
            dst[ch] = quantized(old[ch] + (effect[ch] - old[ch]) * w, ceiling);
        left = left || dst[3] != 0;
    }
    return left;
}

}

// The effect's pixels as the layer keeps them: with its alpha under Preserve Opacity, mixed with the old pixels by the
// selection's coverage, in 16 bits. The old tile when nothing changed, null when nothing is left.
// How much of the effect each pixel of a row takes: the selection's coverage, or all of it; none off the canvas for the
// effects that read their neighbours.
void EffectRenderer::coverageRow(int y, int x0, std::uint16_t* out) const {
    if (m_source.selection) {
        m_source.selection->row(y, x0, kS, out);
        return;
    }
    std::fill(out, out + kS, static_cast<std::uint16_t>(SelectionMask::kOne));
    if (!m_canvasOnly)
        return;
    const QRect canvas(QPoint(0, 0), m_source.canvas);
    for (int x = 0; x < kS; ++x)
        if (!canvas.contains(x0 + x, y))
            out[x] = 0;
}

TilePtr EffectRenderer::finish(TileCoord c, float* effect) const {
    const TilePtr before = m_source.pixels.tileShared(c.x, c.y);
    auto old = std::make_unique<Patch>();
    m_grid->read(c.pixelRect(), old->data());
    if (m_source.keepAlpha)
        for (int i = 0; i < kTilePixels; ++i)
            put(effect, i, premultiplied(straight(at(effect, i)), (*old)[static_cast<std::size_t>(i * 4 + 3)]));
    std::array<std::uint16_t, kS> coverage;
    auto tile = std::make_shared<Tile>();
    bool left = false;
    for (int y = 0; y < kS; ++y) {
        coverageRow(c.y * kS + y, c.x * kS, coverage.data());
        left = mixRow(old->data() + y * kS * 4, effect + y * kS * 4, coverage.data(), tile->at(0, y)) || left;
    }
    if (!left)
        return nullptr;
    const bool same = before && std::equal(tile->px.begin(), tile->px.end(), before->px.begin());
    return same ? before : TilePtr(std::move(tile));
}

void orderTiles(std::vector<TileCoord>& tiles, const QRect& focus) {
    const QPoint centre = focus.isEmpty() ? QPoint() : focus.center();
    const auto key = [&](TileCoord t) {
        const QRect r = t.pixelRect();
        const QPoint d = r.center() - centre;
        const bool outside = !focus.isEmpty() && !focus.intersects(r);
        return std::pair(outside, static_cast<std::int64_t>(d.x()) * d.x() + static_cast<std::int64_t>(d.y()) * d.y());
    };
    std::ranges::stable_sort(tiles, [&](TileCoord a, TileCoord b) { return key(a) < key(b); });
}

TiledImage renderEffect(const EffectSource& source, const EffectSettings& settings) {
    EffectRenderer renderer(source, settings);
    renderer.prepare(nullptr);
    TiledImage out = source.pixels;
    for (const TileCoord c : renderer.tiles())
        out.setTile(c.x, c.y, renderer.render(c));
    return out;
}

}
