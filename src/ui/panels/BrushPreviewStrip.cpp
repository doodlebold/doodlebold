#include "ui/panels/BrushPreviewStrip.h"

#include "core/color/ColorModels.h"
#include "core/layers/CanvasBackground.h"
#include "core/stroke/RebuiltStroke.h"
#include "core/stroke/StrokePainter.h"
#include "ui/Theme.h"

#include <QPainter>
#include <QPainterPath>

#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>

namespace doodlebold {

using Theme::Token;

namespace {

// As many pen events as the stroke takes: with 48 the Brush took on the right side's colour sooner than the reference's preview;
// more than 96 change nothing.
constexpr int kStrokeEvents = 96;
// The stroke's shape is the reference's preview.
constexpr double kStrokeMargin = 0.163;  // of the strip's width, at each end
constexpr double kStrokeWave = 0.065;    // how far the stroke swings up and down, of the strip's height
constexpr int kLargestTip = 490;  // tenths of a px: larger brushes show at this size, as from Size 50 px up in the mocks' preview
constexpr double kLargestShare = 0.8;  // of the strip's height, on a strip too low for kLargestTip
constexpr double kPi = 3.14159265358979;
// The background is the same whatever the colour, as the reference's is.
constexpr QRgb kLeftGround = qRgb(0, 0, 0);  // the Pencil's: a light stroke shows on the left, a dark one on the right
constexpr QRgb kRightGround = qRgb(64, 64, 64);
constexpr double kSideTurn = 120.0;  // degrees round the wheel, from its hue to each side's colour
// The transparency checker behind the strip, the reference's preview's: lighter and larger than the canvas's.
constexpr Backdrop kPreviewChecker{qRgb(255, 255, 255), qRgb(243, 243, 243), 17};

// The Eraser's strip is one plain grey layer over the transparency checker, so where it erases the checker shows, as much as it
// erased, as through the other previews' empty half. The grey is the
// reference's Eraser strip.
constexpr QRgb kEraserLayer = qRgb(138, 143, 152);
// The Blur, the Smudge and the Clone Stamp don't depend on the colour, so they show over the same vertical stripes, light from the
// left edge, as the reference's Blur preview does. The
// Clone Stamp copies from half a stripe to the right.
constexpr int kStripeWidth = 10;  // px
constexpr QRgb kLightStripe = qRgb(216, 221, 230);
constexpr QRgb kDarkStripe = qRgb(108, 122, 150);
constexpr QPoint kCloneOffset(-kStripeWidth / 2, 0);
// The Blur's pressure reaches full this many times sooner from each end, so the middle of its stroke, at full, runs longer.
constexpr double kBlurPlateau = 2.5;

// What a tool's stroke is shown over.
enum class Strip {
    Halves,
    Sides,
    Grey,
    Stripes
};

Strip stripOf(const BrushSettings& settings) {
    switch (settings.tool) {
    case ToolType::Pencil:
    case ToolType::PixelArtPen: return Strip::Halves;  // they don't mix colours, so they have nothing to show over the sides
    case ToolType::Eraser: return Strip::Grey;
    case ToolType::Blur:
    case ToolType::Smudge:  // feedback: "Smudge should also have the same background as clone or blur"
    case ToolType::Clone: return Strip::Stripes;
    default: return Strip::Sides;
    }
}

void paintStripes(TiledImage& picture) {
    const QSize px = picture.size();
    for (int x = 0; x < px.width(); x += kStripeWidth)
        picture.fill(QRect(x, 0, kStripeWidth, px.height()), QColor::fromRgb((x / kStripeWidth) % 2 == 0 ? kLightStripe : kDarkStripe));
}

// The two pure, fully vivid colours that make a triad with the wheel's hue, so the stroke has something to mix with on each side:
// the one kSideTurn back on the left, the one kSideTurn on on the right. Only the hue counts, not how dark, pale or grey the colour is.
std::pair<QColor, QColor> sideColours(double hue) {
    const auto turned = [hue](double turn) {
        return QColor::fromHsvF(static_cast<float>(std::fmod(hue + turn + 360.0, 360.0) / 360.0), 1.0f, 1.0f);
    };
    return {turned(-kSideTurn), turned(kSideTurn)};
}

// The sides fill the lower half; the upper half is left empty, so a stroke shows how it picks up and lays over transparency. The
// stroke's first tip, on the middle, picks up the left side's colour, as asked: the Brush's faint start lays it
// thinned, as the reference's does. Before, the sides started a little above the middle instead.
void paintSides(TiledImage& picture, double hue) {
    const auto [left, right] = sideColours(hue);
    const QSize px = picture.size();
    const int half = px.width() / 2;
    const int top = px.height() / 2;
    picture.fill(QRect(0, top, half, px.height() - top), left);
    picture.fill(QRect(half, top, px.width() - half, px.height() - top), right);
}

// What shows behind the picture: for the Pencil, black and grey halves; elsewhere the reference's light transparency checker, under the
// sides through their empty upper half and under the Eraser's grey where it erases. The stripes cover it.
QImage groundFor(const BrushSettings& settings, QSize px) {
    QImage ground(px, QImage::Format_ARGB32_Premultiplied);
    const Strip strip = stripOf(settings);
    const Backdrop& checker = kPreviewChecker;
    for (int y = 0; y < px.height(); ++y) {
        auto* line = reinterpret_cast<QRgb*>(ground.scanLine(y));
        for (int x = 0; x < px.width(); ++x) {
            if (strip == Strip::Halves)
                line[x] = x < px.width() / 2 ? kLeftGround : kRightGround;
            else
                line[x] = checker.at(x, y);
        }
    }
    return ground;
}

// A pen moving along a gentle wave, with the given touch at each end and full pressure in the middle.
InputSample samplePoint(const QSize& px, double t, double touch, double plateau) {
    const double margin = px.width() * kStrokeMargin;
    InputSample sample;
    sample.pos = QPointF(margin + t * (px.width() - 2.0 * margin), px.height() * (0.5 - kStrokeWave * std::sin(t * 2.0 * kPi)));
    sample.pressure = touch + (1.0 - touch) * std::min(1.0, plateau * std::sin(t * kPi));
    return sample;
}

std::vector<InputSample> penStroke(const QSize& px, double touch, double plateau = 1.0) {
    std::vector<InputSample> stroke;
    for (int i = 0; i <= kStrokeEvents; ++i)
        stroke.push_back(samplePoint(px, double(i) / kStrokeEvents, touch, plateau));
    return stroke;
}

// The preview's pressure rises from 0 at the left end and falls back to 0 at the right, so both ends come to the same point.
// It doesn't go through the stabilizer: a real stroke's start depends on how the pen lands, which the preview doesn't show.
std::vector<InputSample> previewStroke(const QSize& px, const BrushSettings& settings) {
    return penStroke(px, 0.0, settings.tool == ToolType::Blur ? kBlurPlateau : 1.0);
}

// The stroke runs across both sides, so it shows how it looks over each of them.
void paintSample(TiledImage& picture, BrushSettings settings, const QColor& color) {
    settings.size = std::min({settings.size, kLargestTip, int(picture.size().height() * kLargestShare) * 10});
    if (settings.tool == ToolType::Eraser)
        settings.pressureSize = true;  // the Pencil's stroke, pointed at both ends, though the Eraser's size starts without pressure
    // Its path is the pen's samples as they are, whatever the level: it never goes through the stabilizer, and at 0 every tool's
    // stamps go straight from sample to sample, which the preview's close samples don't show.
    settings.stabilizer = StabilizerLevel{0};
    const std::vector<InputSample> stroke = previewStroke(picture.size(), settings);
    StrokePainter engine;
    if (settings.tool == ToolType::Clone)
        engine.setCloneSource(std::make_shared<const TiledImage>(picture), kCloneOffset);
    engine.begin(&picture, color, settings, stroke.front());
    for (std::size_t i = 1; i < stroke.size(); ++i)
        engine.add(stroke[i]);
    engine.end();
}

}

BrushPreviewStrip::BrushPreviewStrip(QWidget* parent) : QWidget(parent) {
    setObjectName(QStringLiteral("brushPreview"));
    setFixedHeight(kHeight);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setToolTip(tr("Brush preview"));
    m_timer.setSingleShot(true);
    m_timer.setInterval(kRedrawDelayMs);
    connect(&m_timer, &QTimer::timeout, this, [this] {
        render();
        update();
    });
}

void BrushPreviewStrip::setBrush(const BrushSettings& settings, const QColor& color, double hue) {
    if (settings == m_settings && color == m_color && hue == m_hue && !m_dirty)
        return;
    m_settings = settings;
    m_color = color;
    m_hue = hue;
    scheduleRedraw();
}

const QImage& BrushPreviewStrip::image() {
    if (m_dirty || m_image.size() != pixelSize())
        render();
    return m_image;
}

void BrushPreviewStrip::scheduleRedraw() {
    m_dirty = true;
    // Not restarted while changes keep coming: a drag redraws at most once per kRedrawDelayMs.
    if (!m_timer.isActive())
        m_timer.start();
}

QSize BrushPreviewStrip::pixelSize() const {
    return size() * devicePixelRatioF();
}

void BrushPreviewStrip::render() {
    m_timer.stop();
    const QSize px = pixelSize();
    TiledImage picture(px);
    switch (stripOf(m_settings)) {
    case Strip::Sides: paintSides(picture, m_hue >= 0.0 ? m_hue : color::hueOf(m_color, 0.0)); break;
    case Strip::Grey: picture.fill(QRect(QPoint(0, 0), px), QColor::fromRgb(kEraserLayer)); break;
    case Strip::Stripes: paintStripes(picture); break;
    case Strip::Halves: break;
    }
    paintSample(picture, m_settings, m_color);
    m_image = groundFor(m_settings, px);
    QPainter onGround(&m_image);
    onGround.drawImage(0, 0, picture.toImage());
    onGround.end();
    m_image.setDevicePixelRatio(devicePixelRatioF());
    m_shownTool = m_settings.tool;
    m_dirty = false;
    ++m_renderCount;
}

void BrushPreviewStrip::resizeEvent(QResizeEvent*) {
    scheduleRedraw();
}

void BrushPreviewStrip::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QRectF frame = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    const double radius = Theme::metric(Theme::Metric::Radius) + 2;
    QPainterPath clip;
    clip.addRoundedRect(frame, radius, radius);
    p.setClipPath(clip);
    if (pixelSize().isEmpty()) {
        p.fillRect(rect(), Theme::color(Token::Base));
        return;
    }
    // A picture made for another tool or another size is never shown, not even until the timer comes: the last tool's showed,
    // or a blank strip, for a few frames after choosing a tool. Changes within one tool still wait for the timer.
    if (m_image.isNull() || m_image.size() != pixelSize() || m_shownTool != m_settings.tool)
        render();
    p.drawImage(QPointF(0, 0), m_image);
}

}
