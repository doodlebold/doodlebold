#include "core/stroke/StampForm.h"
#include "core/stroke/PaperGrain.h"

#include <algorithm>
#include <array>
#include <cmath>

// docs/brush-engine/stamp-form.md says what the named values here are for and how they were chosen.
namespace doodlebold {

namespace {

constexpr float kGreyLevels = 255.0f;  // an 8-bit grey picture's white

// Invert, as the reference's: the plain flip made lighter by twice the picture's darkest grey, capped at white.
float invertedLight(float light, float darkest) {
    return std::min(1.0f - light + 2.0f * darkest, 1.0f);
}

float darkestOf(const QImage& grey) {
    int darkest = static_cast<int>(kGreyLevels);
    for (int y = 0; y < grey.height(); ++y) {
        const uchar* row = grey.constScanLine(y);
        for (int x = 0; x < grey.width(); ++x)
            darkest = std::min(darkest, static_cast<int>(row[x]));
    }
    return static_cast<float>(darkest) / kGreyLevels;
}
// A blot map's stamp is a cone the picture eats into by its lightness; what's left passes each Shape's window (foot to top).
constexpr float kBlotConeReach = 1.5f;  // the cone's reach, in the stamp's nominal radii
constexpr float kBlotDepth = 1.2f;  // how deep the picture eats at Mess. 100 %: its lightness times this
constexpr float kBlotDepthFifth = 1.3f;  // the same on the 5th Shape, for the Pencil, Eraser and Marker
constexpr float kBlotDarkWiden = 0.5f;  // of the way to the darkest of a pixel and its four neighbours
// The stamp tools' window, foot and top, and their depth, per Shape, softest first.
constexpr std::array<float, 5> kBlotWindowFoot{0.07f, 0.11f, 0.15f, 0.1f, 0.05f};
constexpr std::array<float, 5> kBlotWindowTop{1.0f, 0.9f, 0.8f, 0.4f, 0.45f};
constexpr std::array<float, 4> kBlotDepthSoft{0.75f, 0.775f, 0.8f, kBlotDepth};
constexpr float kMixingBlotFoot = 0.05f, kMixingBlotTop = 0.15f;  // a mixing tool's window, taken as its dab's outline
constexpr float kMixingSoftBlotDepth = 1.0f, kMixingSoftBlotTop = 0.3f;  // the Marker's softer Shapes: wider, shallower
constexpr float kBrushBlotDepth = 1.35f;  // the Brush's 5th Shape: its window and a deeper eat
constexpr float kBrushBlotFoot = 0.06f, kBrushBlotTop = 0.25f;
// Doodlebold's own blot maps leave their white ground empty, so no band runs along a stroke's middle between the blots.
constexpr float kGroundFrom = 0.75f;  // the lightness from which a picture counts as ground
constexpr float kGroundEmptyFrom = 0.25f;  // the Mess. from which the ground is wholly empty
// A picture's corner from its centre is at most √2 / 2 of its longest side, rounded up.
constexpr float kHalfDiagonal = 0.7072f;
// How far from a brush shape's lit pixels its sampling can still read one, per picture pixel a canvas pixel spans: past it the
// picture is 0, so at() returns that without sampling.
constexpr float kLitReachPerSpan = 4.0f;
constexpr float kLitSlack = 1.0f;  // the shape's own pixels, for rounding

float lerp(float a, float b, float t) {
    return a + (b - a) * t;
}

}

StampForm::StampForm(const QImage& picture, double scale, double strength, bool invert, bool fixedToCanvas, bool sharpen, bool stamped,
                     bool highQuality, bool turnedAtLightest, bool oneStroke, bool groundEmpty)
    : m_picture(picture.convertToFormat(QImage::Format_Grayscale8)),
      m_width(std::max(m_picture.width(), 1)),
      m_height(std::max(m_picture.height(), 1)),
      m_scale(static_cast<float>(scale)),
      m_strength(static_cast<float>(strength)),
      m_invert(invert),
      m_fixed(fixedToCanvas) {
    m_groundEmpty = groundEmpty && fixedToCanvas;
    m_sharpen = sharpen;
    m_stamped = stamped && !fixedToCanvas;
    m_oneStroke = m_stamped && oneStroke;
    m_highQuality = highQuality;
    if (m_stamped) {
        buildLevels();
        findLit();
    }
    if (m_invert)
        m_darkest = darkestOf(m_picture);
    if (!m_fixed)
        return;
    // The picture at Scale, by box averaging that wraps around its edges, so it repeats without a seam.
    m_tileWidth = std::max(1, static_cast<int>(std::lround(static_cast<float>(m_width) * m_scale)));
    m_tileHeight = std::max(1, static_cast<int>(std::lround(static_cast<float>(m_height) * m_scale)));
    const auto boxed = [](const std::vector<float>& in, int from, int to, int stride, int lines, int lineStride) {
        std::vector<float> out(static_cast<std::size_t>(to) * lines);
        const double step = static_cast<double>(from) / to;  // source pixels a destination pixel covers
        for (int l = 0; l < lines; ++l)
            for (int i = 0; i < to; ++i) {
                const double a = i * step, b = (i + 1) * step;
                double sum = 0.0;
                for (int s = static_cast<int>(std::floor(a)); s < static_cast<int>(std::ceil(b)); ++s) {
                    const double w = std::min(b, s + 1.0) - std::max(a, double(s));
                    sum += w * in[static_cast<std::size_t>(l * lineStride + ((s % from + from) % from) * stride)];
                }
                out[static_cast<std::size_t>(l * to + i)] = static_cast<float>(sum / step);
            }
        return out;
    };
    std::vector<float> light(static_cast<std::size_t>(m_width) * m_height);
    // Erasing, Invert for Transparency turns the picture over about its top and bottom, as a paper is: the reference's own
    // blot maps never reach white (Blots 143 of 255), and turned over as Invert does, about black, every part of them is lighter than its
    // lightest part was, and the Eraser takes next to nothing (Blots at Strength 100, 94 % fewer pixels than the reference's).
    const TurnedRange range = turnedAtLightest ? turnedRangeOf(m_picture) : TurnedRange{};
    for (int y = 0; y < m_height; ++y) {
        const uchar* row = m_picture.constScanLine(y);
        for (int x = 0; x < m_width; ++x) {
            const float v = static_cast<float>(row[x]) / kGreyLevels;
            light[static_cast<std::size_t>(y * m_width + x)] = !m_invert          ? v
                                                               : turnedAtLightest ? range.turned(v)
                                                                                  : invertedLight(v, m_darkest);
        }
    }
    const std::vector<float> across = boxed(light, m_width, m_tileWidth, 1, m_height, m_width);  // rows: m_tileWidth wide
    std::vector<float> transposed(static_cast<std::size_t>(m_tileWidth) * m_height);
    for (int y = 0; y < m_height; ++y)
        for (int x = 0; x < m_tileWidth; ++x)
            transposed[static_cast<std::size_t>(x * m_height + y)] = across[static_cast<std::size_t>(y * m_tileWidth + x)];
    const std::vector<float> down = boxed(transposed, m_height, m_tileHeight, 1, m_tileWidth, m_height);
    m_lightness.resize(static_cast<std::size_t>(m_tileWidth) * m_tileHeight);
    for (int x = 0; x < m_tileWidth; ++x)
        for (int y = 0; y < m_tileHeight; ++y)
            m_lightness[static_cast<std::size_t>(y * m_tileWidth + x)] = down[static_cast<std::size_t>(x * m_tileHeight + y)];
    // Softened as the reference's stamp sees it, drawn at a whole pixel and slid to its place by blending neighbouring ones (Sharpen
    // off): [1 2 1] / 4 each way, wrapping, once. It brings the Pencil at Mess. 100 % from 43.6 to 36.4 levels off; twice, 39.7.
    // With Sharpen on the picture is laid as it is, unsoftened.
    if (!m_sharpen) {
        std::vector<float> was = m_lightness;
        const int w = m_tileWidth, h = m_tileHeight;
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x)
                m_lightness[static_cast<std::size_t>(y * w + x)] = 0.25f * was[static_cast<std::size_t>(y * w + (x + w - 1) % w)] +
                                                                   0.5f * was[static_cast<std::size_t>(y * w + x)] +
                                                                   0.25f * was[static_cast<std::size_t>(y * w + (x + 1) % w)];
        was = m_lightness;
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x)
                m_lightness[static_cast<std::size_t>(y * w + x)] = 0.25f * was[static_cast<std::size_t>(((y + h - 1) % h) * w + x)] +
                                                                   0.5f * was[static_cast<std::size_t>(y * w + x)] +
                                                                   0.25f * was[static_cast<std::size_t>(((y + 1) % h) * w + x)];
    }
    // And its dark parts a little wider: each pixel's lightness kBlotDarkWiden of the way to the darkest of it and its four
    // neighbours. The reference's stamp sees the picture slid to its own place a pixel at a time, and the stroke keeps the most of its
    // stamps, so a picture's dark parts come out about half a pixel wider: where a fine picture (Fine Blots) is light beside dark parts the
    // reference lays 0.19 / 0.09 at lightness 0.8 to 0.9 / 0.9 to 1.0, ours none. Every tool laid Fine Blots 6 to 9 % thin at Strength 100
    // (a held-out fine picture 11 %); now 3 to 5 %. On Fine Blots and Blots at Strength 100, 100 px, the Brush,
    // Water Color, Pencil, Marker and Eraser (ten cases), 103.7 -> 88.3 levels summed; the 175 texture readings -3.8 summed (18 better,
    // 16 worse, by at most 0.67); the Pencil's and Brush's constant-pressure lines 171.1 -> 171.0. The whole way (1) laid every picture 2 to
    // 7 % too wide, and widening into the 3 x 3 square more; softening the picture more thinned it further (2 / 4 / 8 times: 106 / 118 /
    // 141), and leaving it unsoftened cost the Pencil's slow-ramp readings (+26 to +32). With Sharpen the picture is laid as it is.
    if (!m_sharpen) {
        const std::vector<float> was = m_lightness;
        const int w = m_tileWidth, h = m_tileHeight;
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x) {
                const float own = was[static_cast<std::size_t>(y * w + x)];
                float most = own;
                most = std::min(
                    {most, was[static_cast<std::size_t>(y * w + (x + 1) % w)], was[static_cast<std::size_t>(y * w + (x + w - 1) % w)],
                     was[static_cast<std::size_t>(((y + 1) % h) * w + x)], was[static_cast<std::size_t>(((y + h - 1) % h) * w + x)]});
                m_lightness[static_cast<std::size_t>(y * w + x)] = own + (most - own) * kBlotDarkWiden;
            }
    }
}

void StampForm::buildLevels() {
    Level first{m_width, m_height, std::vector<float>(static_cast<std::size_t>(m_width) * m_height)};
    for (int y = 0; y < m_height; ++y) {
        const uchar* row = m_picture.constScanLine(y);
        for (int x = 0; x < m_width; ++x)
            first.light[static_cast<std::size_t>(y * m_width + x)] = static_cast<float>(row[x]);
    }
    m_levels.push_back(std::move(first));
    // Each copy half the last, a pixel the mean of the four it covers; an odd side's last pixel takes its row or column twice.
    while (m_levels.back().width > 1 || m_levels.back().height > 1) {
        const Level& last = m_levels.back();
        Level half{std::max(1, (last.width + 1) / 2), std::max(1, (last.height + 1) / 2), {}};
        half.light.resize(static_cast<std::size_t>(half.width) * half.height);
        const auto value = [&last](int x, int y) {
            return last.light[static_cast<std::size_t>(std::min(y, last.height - 1) * last.width + std::min(x, last.width - 1))];
        };
        for (int y = 0; y < half.height; ++y)
            for (int x = 0; x < half.width; ++x)
                half.light[static_cast<std::size_t>(y * half.width + x)] =
                    (value(2 * x, 2 * y) + value(2 * x + 1, 2 * y) + value(2 * x, 2 * y + 1) + value(2 * x + 1, 2 * y + 1)) / 4.0f;
        m_levels.push_back(std::move(half));
    }
}

void StampForm::findLit() {
    const Level& picture = m_levels.front();
    for (int y = 0; y < picture.height; ++y)
        for (int x = 0; x < picture.width; ++x)
            if (picture.light[static_cast<std::size_t>(y * picture.width + x)] > 0.0f) {
                m_litLeft = std::min(m_litLeft, static_cast<float>(x));
                m_litRight = std::max(m_litRight, static_cast<float>(x));
                m_litTop = std::min(m_litTop, static_cast<float>(y));
                m_litBottom = std::max(m_litBottom, static_cast<float>(y));
            }
}

bool StampForm::pastLit(float px, float py, float picturePerPixel) const {
    const float reach = kLitReachPerSpan * std::max(picturePerPixel, 1.0f) + kLitSlack;
    return px < m_litLeft - reach || px > m_litRight + reach || py < m_litTop - reach || py > m_litBottom + reach;
}

float StampForm::Level::at(float x, float y) const {
    const int x0 = std::clamp(static_cast<int>(std::floor(x)), 0, width - 1),
              y0 = std::clamp(static_cast<int>(std::floor(y)), 0, height - 1);
    const int x1 = std::min(x0 + 1, width - 1), y1 = std::min(y0 + 1, height - 1);
    const float fx = std::clamp(x - static_cast<float>(x0), 0.0f, 1.0f), fy = std::clamp(y - static_cast<float>(y0), 0.0f, 1.0f);
    const float* top = light.data() + static_cast<std::size_t>(y0) * width;
    const float* bottom = light.data() + static_cast<std::size_t>(y1) * width;
    return lerp(lerp(top[x0], top[x1], fx), lerp(bottom[x0], bottom[x1], fx), fy);
}

float StampForm::Level::averageOver(float x0, float y0, float x1, float y1) const {
    // Pixel i covers i - 0.5 to i + 0.5; the box is held inside the picture.
    x0 = std::max(x0, -0.5f), y0 = std::max(y0, -0.5f);
    x1 = std::min(x1, static_cast<float>(width) - 0.5f), y1 = std::min(y1, static_cast<float>(height) - 0.5f);
    if (!(x1 > x0 && y1 > y0))
        return at((x0 + x1) / 2.0f, (y0 + y1) / 2.0f);
    float sum = 0.0f;
    for (int y = static_cast<int>(std::floor(y0 + 0.5f)); y <= std::min(static_cast<int>(std::floor(y1 + 0.5f)), height - 1); ++y) {
        const float py = static_cast<float>(y);
        const float h = std::min(y1, py + 0.5f) - std::max(y0, py - 0.5f);
        for (int x = static_cast<int>(std::floor(x0 + 0.5f)); x <= std::min(static_cast<int>(std::floor(x1 + 0.5f)), width - 1); ++x) {
            const float px = static_cast<float>(x);
            const float w = std::min(x1, px + 0.5f) - std::max(x0, px - 0.5f);
            if (h > 0.0f && w > 0.0f)
                sum += h * w * light[static_cast<std::size_t>(y * width + x)];
        }
    }
    return sum / ((x1 - x0) * (y1 - y0));
}

// A brush shape's lightness, in grey levels, at (px, py) in its own pixels, a canvas pixel spanning `picturePerPixel` of them.
// Up to one, the picture itself, bilinear. Smaller, the two halved copies either side of that size, each bilinear, blended by where the
// size sits between them; with HQ Down Scaling, the finer of the two averaged over the canvas pixel's square.
float StampForm::stampedLight(float px, float py, float picturePerPixel) const {
    if (picturePerPixel <= 1.0f || m_levels.size() == 1)
        return m_levels.front().at(px, py);
    const float level = std::min(std::log2(picturePerPixel), static_cast<float>(m_levels.size() - 1));
    const int finer = static_cast<int>(std::floor(level));
    const auto in = [px, py](int at) {  // the place in a halved copy, its pixels' centres at whole numbers
        const float step = std::ldexp(1.0f, at);
        return std::array<float, 2>{(px + 0.5f) / step - 0.5f, (py + 0.5f) / step - 0.5f};
    };
    const auto [fx, fy] = in(finer);
    if (m_highQuality) {
        const float half = picturePerPixel / std::ldexp(1.0f, finer) / 2.0f;
        return m_levels[static_cast<std::size_t>(finer)].averageOver(fx - half, fy - half, fx + half, fy + half);
    }
    const float a = m_levels[static_cast<std::size_t>(finer)].at(fx, fy);
    if (finer + 1 >= static_cast<int>(m_levels.size()))
        return a;
    const auto [cx, cy] = in(finer + 1);
    return a + (m_levels[static_cast<std::size_t>(finer) + 1].at(cx, cy) - a) * (level - static_cast<float>(finer));
}

float StampForm::reachFor(float radius) const {
    if (m_fixed)
        return radius * kBlotConeReach;
    return radius * 2.0f * m_scale * kHalfDiagonal;
}

float StampForm::blotShape(float distance, float radius, int x, int y, int edge, BlotWindow window) const {
    const float cone = std::max(1.0f - distance / std::max(radius * kBlotConeReach, 1e-3f), 0.0f);
    if (cone <= 0.0f)
        return 0.0f;
    const int tx = ((x % m_tileWidth) + m_tileWidth) % m_tileWidth, ty = ((y % m_tileHeight) + m_tileHeight) % m_tileHeight;
    const float depth = window == BlotWindow::BrushFifth   ? kBrushBlotDepth
                        : edge >= 4                        ? kBlotDepthFifth
                        : window == BlotWindow::Stamp      ? kBlotDepthSoft[static_cast<std::size_t>(std::clamp(edge, 0, 3))]
                        : window == BlotWindow::MarkerSoft ? kMixingSoftBlotDepth
                                                           : kBlotDepth;
    const float light = m_lightness[static_cast<std::size_t>(ty * m_tileWidth + tx)];
    const float left = m_groundEmpty ? cone - groundEaten(depth * m_strength * light, light) : cone - depth * m_strength * light;
    const int e = std::clamp(edge, 0, 4);
    const float foot = window == BlotWindow::Mixing || window == BlotWindow::MarkerSoft ? kMixingBlotFoot
                       : window == BlotWindow::BrushFifth                               ? kBrushBlotFoot
                                                                                        : kBlotWindowFoot[e];
    const float top = window == BlotWindow::MarkerSoft   ? kMixingSoftBlotTop
                      : window == BlotWindow::Mixing     ? kMixingBlotTop
                      : window == BlotWindow::BrushFifth ? kBrushBlotTop
                                                         : kBlotWindowTop[e];
    return std::clamp((left - foot) / (top - foot), 0.0f, 1.0f);
}

// How deep a picture whose ground lays nothing eats at lightness `light`, the rule's eat being `eaten` (kGroundFrom).
float StampForm::groundEaten(float eaten, float light) const {
    const float ground = std::clamp((light - kGroundFrom) / (1.0f - kGroundFrom), 0.0f, 1.0f);
    const float mess = std::min(m_strength / kGroundEmptyFrom, 1.0f);
    return eaten + std::max(1.0f - eaten, 0.0f) * ground * mess;  // never less than the rule's, which may eat past the whole cone
}

float StampForm::at(float dx, float dy, float radius, float turnCos, float turnSin) const {
    const float pixelsPerPicturePixel = radius * 2.0f * m_scale / static_cast<float>(std::max(m_width, m_height));
    // The point in the stamp's own frame, turned back by the stamp's angle.
    const float ux = dx * turnCos + dy * turnSin, uy = dy * turnCos - dx * turnSin;
    // Picture coordinates, its pixels' centres at whole numbers.
    const float px = ux / pixelsPerPicturePixel + static_cast<float>(m_width) / 2.0f - 0.5f;
    const float py = uy / pixelsPerPicturePixel + static_cast<float>(m_height) / 2.0f - 0.5f;
    if (!(px >= -0.5f && py >= -0.5f && px <= static_cast<float>(m_width) - 0.5f && py <= static_cast<float>(m_height) - 0.5f))
        return 0.0f;
    if (m_stamped) {  // a brush shape paints where it's light, a light shape on a dark ground, as the reference's files are
        if (!m_invert && pastLit(px, py, 1.0f / pixelsPerPicturePixel))
            return 0.0f;  // what sampling the dark ground gives, exactly (kLitReachPerSpan)
        const float light = stampedLight(px, py, 1.0f / pixelsPerPicturePixel) / kGreyLevels;
        return m_invert ? 1.0f - light : light;
    }
    const int x0 = std::clamp(static_cast<int>(std::floor(px)), 0, m_width - 1),
              y0 = std::clamp(static_cast<int>(std::floor(py)), 0, m_height - 1);
    const int x1 = std::min(x0 + 1, m_width - 1), y1 = std::min(y0 + 1, m_height - 1);
    const float fx = std::clamp(px - static_cast<float>(x0), 0.0f, 1.0f), fy = std::clamp(py - static_cast<float>(y0), 0.0f, 1.0f);
    const uchar* top = m_picture.constScanLine(y0);
    const uchar* bottom = m_picture.constScanLine(y1);
    const float upper = lerp(static_cast<float>(top[x0]), static_cast<float>(top[x1]), fx);
    const float lower = lerp(static_cast<float>(bottom[x0]), static_cast<float>(bottom[x1]), fx);
    const float light = lerp(upper, lower, fy) / kGreyLevels;
    return 1.0f - (m_invert ? invertedLight(light, m_darkest) : light);
}

}
