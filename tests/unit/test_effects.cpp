#include "core/document/Document.h"
#include "core/effects/ColorEffect.h"
#include "core/effects/EffectRender.h"
#include "core/effects/EffectSession.h"
#include "core/selection/SelectionOps.h"

#include <chrono>
#include <cmath>
#include <numbers>

#include <gtest/gtest.h>

using namespace doodlebold;
using namespace std::chrono_literals;

namespace {

EffectSettings settings(EffectKind kind, std::initializer_list<double> values = {}, bool option = false) {
    EffectSettings s = EffectSettings::start(kind);
    std::size_t i = 0;
    for (const double v : values)
        s.values[i++] = v;
    s.option = option;
    return s;
}

Rgb apply(const EffectSettings& s, Rgb c) {
    return ColorEffect(s)(c, 0, 0);
}

void expectRgb(Rgb got, Rgb want, float tolerance = 0.002f) {
    EXPECT_NEAR(got.r, want.r, tolerance);
    EXPECT_NEAR(got.g, want.g, tolerance);
    EXPECT_NEAR(got.b, want.b, tolerance);
}

// A straight colour and alpha, 0 … 1, as the layer stores them.
std::array<float, 4> px(const TiledImage& image, int x, int y) {
    const auto p = image.pixel(x, y);
    const float a = p[3] / 65535.0f;
    if (a <= 0)
        return {0, 0, 0, 0};
    return {p[0] / 65535.0f / a, p[1] / 65535.0f / a, p[2] / 65535.0f / a, a};
}

float alpha(const TiledImage& image, int x, int y) {
    return image.pixel(x, y)[3] / 65535.0f;
}

EffectSource source(const QSize& size) {
    EffectSource s;
    s.pixels = TiledImage(size);
    s.canvas = size;
    return s;
}

void dot(TiledImage& image, int x, int y, const QColor& c = Qt::black) {
    image.fill(QRect(x, y, 1, 1), c);
}

void must(const EditResult<>& r) {
    ASSERT_TRUE(r.ok()) << qPrintable(describe(r.error()));
}

}

TEST(Effects, EveryEntryIsInTheMenusOrderWithItsSliders) {
    const auto all = allEffects();
    ASSERT_EQ(all.size(), 13u);
    for (std::size_t i = 0; i < all.size(); ++i) {
        EXPECT_EQ(static_cast<std::size_t>(all[i].kind), i) << all[i].title;
        EXPECT_LE(all[i].sliders.size(), static_cast<std::size_t>(EffectSettings::kMaxSliders));
        for (const EffectSlider& s : all[i].sliders)
            EXPECT_TRUE(s.minimum <= s.start && s.start <= s.maximum) << all[i].title << " " << s.label;
    }
    EXPECT_TRUE(effectSpec(EffectKind::Invert).sliders.empty()) << "Invert has no dialog";
    EXPECT_TRUE(effectSpec(EffectKind::Desaturate).sliders.empty());
    EXPECT_STREQ(effectSpec(EffectKind::HueSaturation).option, "Colorize");
    EXPECT_EQ(effectSpec(EffectKind::Levels).sliders.size(), 5u);
}

TEST(Effects, TheStartingValuesOfTheAdjustmentsChangeNothing) {
    for (const EffectKind k : {EffectKind::BrightnessContrast, EffectKind::HueSaturation, EffectKind::Levels, EffectKind::ColorBalance})
        EXPECT_TRUE(isIdentity(EffectSettings::start(k))) << effectSpec(k).title;
    EXPECT_FALSE(isIdentity(EffectSettings::start(EffectKind::GaussianBlur)));
    EXPECT_FALSE(isIdentity(settings(EffectKind::HueSaturation, {0, 0, 0}, true))) << "Colorize alone tints";
}

TEST(ColorEffects, InvertTurnsEachChannelOver) {
    expectRgb(apply(settings(EffectKind::Invert), {1, 0, 0.25f}), {0, 1, 0.75f});
}

TEST(ColorEffects, DesaturateKeepsTheLuma) {
    expectRgb(apply(settings(EffectKind::Desaturate), {1, 0, 0}), {0.299f, 0.299f, 0.299f});
    expectRgb(apply(settings(EffectKind::Desaturate), {0, 0, 1}), {0.114f, 0.114f, 0.114f});
}

TEST(ColorEffects, BrightnessReachesWhiteAndBlackAtItsEnds) {
    expectRgb(apply(settings(EffectKind::BrightnessContrast, {100, 0}), {0.2f, 0.5f, 0.9f}), {1, 1, 1});
    expectRgb(apply(settings(EffectKind::BrightnessContrast, {-100, 0}), {0.2f, 0.5f, 0.9f}), {0, 0, 0});
    const Rgb lighter = apply(settings(EffectKind::BrightnessContrast, {50, 0}), {0.2f, 0.5f, 0.9f});
    EXPECT_NEAR(lighter.r, 0.6f, 0.002f);
    EXPECT_GT(lighter.b, 0.9f);
}

TEST(ColorEffects, ContrastTurnsAboutMiddleGrey) {
    const Rgb more = apply(settings(EffectKind::BrightnessContrast, {0, 50}), {0.4f, 0.5f, 0.6f});
    EXPECT_LT(more.r, 0.4f);
    EXPECT_NEAR(more.g, 0.5f, 0.001f) << "middle grey stays";
    EXPECT_GT(more.b, 0.6f);
    expectRgb(apply(settings(EffectKind::BrightnessContrast, {0, -100}), {0.1f, 0.5f, 0.9f}), {0.5f, 0.5f, 0.5f});
    expectRgb(apply(settings(EffectKind::BrightnessContrast, {0, 100}), {0.45f, 0.5f, 0.55f}), {0, 0.5f, 1});
}

TEST(ColorEffects, HueTurnsTheColourWheel) {
    expectRgb(apply(settings(EffectKind::HueSaturation, {120, 0, 0}), {1, 0, 0}), {0, 1, 0});
    expectRgb(apply(settings(EffectKind::HueSaturation, {-120, 0, 0}), {1, 0, 0}), {0, 0, 1});
    expectRgb(apply(settings(EffectKind::HueSaturation, {180, 0, 0}), {1, 0.5f, 0}), {0, 0.5f, 1});
}

TEST(ColorEffects, SaturationAndLightnessReachGreyWhiteAndBlack) {
    expectRgb(apply(settings(EffectKind::HueSaturation, {0, -100, 0}), {1, 0, 0}), {0.5f, 0.5f, 0.5f});
    expectRgb(apply(settings(EffectKind::HueSaturation, {0, 100, 0}), {0.6f, 0.4f, 0.4f}), {1, 0, 0});
    expectRgb(apply(settings(EffectKind::HueSaturation, {0, 0, 100}), {0.3f, 0.6f, 0.2f}), {1, 1, 1});
    expectRgb(apply(settings(EffectKind::HueSaturation, {0, 0, -100}), {0.3f, 0.6f, 0.2f}), {0, 0, 0});
}

TEST(ColorEffects, ColorizeGivesEveryPixelTheHueAndKeepsItsLuma) {
    const EffectSettings blue = settings(EffectKind::HueSaturation, {-120, 0, 0}, true);  // 240°
    const Rgb a = apply(blue, {1, 0, 0}), b = apply(blue, {0.2f, 0.8f, 0.2f});
    EXPECT_NEAR(toHsl(a).h, 240, 0.5f);
    EXPECT_NEAR(toHsl(b).h, 240, 0.5f);
    EXPECT_NEAR(toHsl(a).s, 0.25f, 0.01f);
    EXPECT_NEAR(toHsl(a).l, luma({1, 0, 0}), 0.01f);
    expectRgb(apply(blue, {0.5f, 0.5f, 0.5f}), fromHsl({240, 0.25f, 0.5f}));
}

TEST(ColorEffects, HslRoundTrips) {
    for (const Rgb c : {Rgb{0.1f, 0.7f, 0.3f}, Rgb{0.9f, 0.9f, 0.1f}, Rgb{0.5f, 0.5f, 0.5f}, Rgb{0.0f, 0.2f, 0.9f}})
        expectRgb(fromHsl(toHsl(c)), c, 0.0005f);
}

TEST(ColorEffects, LevelsMapsInputToOutput) {
    expectRgb(apply(settings(EffectKind::Levels, {64, 191, 1, 0, 255}), {0.2f, 0.5f, 0.8f}), {0, 0.5f, 1}, 0.01f);
    const Rgb gamma = apply(settings(EffectKind::Levels, {0, 255, 2, 0, 255}), {0.25f, 0, 1});
    expectRgb(gamma, {0.5f, 0, 1});
    expectRgb(apply(settings(EffectKind::Levels, {0, 255, 1, 51, 204}), {0, 0.5f, 1}), {0.2f, 0.5f, 0.8f});
}

TEST(ColorEffects, ColorBalanceBendsTheMiddleAndKeepsBlackAndWhite) {
    const EffectSettings red = settings(EffectKind::ColorBalance, {100, 0, 0}, false);
    expectRgb(apply(red, {0.5f, 0.5f, 0.5f}), {std::sqrt(0.5f), 0.5f, 0.5f});
    expectRgb(apply(red, {0, 0, 0}), {0, 0, 0});
    expectRgb(apply(red, {1, 1, 1}), {1, 1, 1});
    const Rgb kept = apply(settings(EffectKind::ColorBalance, {100, -50, 30}, true), {0.4f, 0.5f, 0.6f});
    EXPECT_NEAR(luma(kept), luma({0.4f, 0.5f, 0.6f}), 0.002f) << "Preserve Luminosity";
    EXPECT_GT(kept.r - kept.g, 0.4f - 0.5f) << "and still redder";
}

TEST(ColorEffects, PosterizeLeavesThatManyLevels) {
    expectRgb(apply(settings(EffectKind::Posterize, {2}), {0.4f, 0.6f, 1}), {0, 1, 1});
    expectRgb(apply(settings(EffectKind::Posterize, {3}), {0.2f, 0.4f, 0.8f}), {0, 0.5f, 1});
}

TEST(ColorEffects, NoiseIsGrainOfTheAmountsStrength) {
    const EffectSettings s = settings(EffectKind::AddNoise, {40});
    const ColorEffect noise(s);
    double sum = 0, squares = 0;
    constexpr int kN = 20000;
    for (int i = 0; i < kN; ++i) {
        const float d = noise({0.5f, 0.5f, 0.5f}, i % 200, i / 200).r - 0.5f;
        sum += d;
        squares += d * d;
    }
    EXPECT_NEAR(sum / kN, 0, 0.01);
    EXPECT_NEAR(std::sqrt(squares / kN), 0.2, 0.02) << "40 % is a standard deviation of 0.2";
    const Rgb a = noise({0.5f, 0.5f, 0.5f}, 3, 4), b = noise({0.5f, 0.5f, 0.5f}, 3, 4);
    expectRgb(a, b, 0);
    EXPECT_NE(a.r, a.g) << "each channel its own grain";
    const Rgb mono = ColorEffect(settings(EffectKind::AddNoise, {40}, true))({0.5f, 0.5f, 0.5f}, 3, 4);
    EXPECT_EQ(mono.r, mono.g);
    EXPECT_EQ(mono.g, mono.b);
}

TEST(EffectRender, ColourEffectsKeepAlphaAndLeaveTransparencyAlone) {
    EffectSource s = source(QSize(200, 100));
    s.pixels.fill(QRect(10, 10, 50, 50), QColor(255, 0, 0, 128));
    const TiledImage out = renderEffect(s, settings(EffectKind::Invert));
    const auto p = px(out, 20, 20);
    EXPECT_NEAR(p[3], 128 / 255.0f, 0.001f);
    EXPECT_NEAR(p[0], 0, 0.01f);
    EXPECT_NEAR(p[1], 1, 0.01f);
    EXPECT_EQ(out.pixel(100, 50)[3], 0);
    EXPECT_EQ(out.tileCount(), s.pixels.tileCount()) << "no tile added where nothing was";
}

TEST(EffectRender, OnlyTheSelectionChangesByItsCoverage) {
    EffectSource s = source(QSize(200, 200));
    s.pixels.fill(QColor(200, 40, 40));
    s.selection = selection::rectangle(s.canvas, QRectF(50, 50, 100.5, 100), true);
    const TiledImage out = renderEffect(s, settings(EffectKind::Invert));
    EXPECT_EQ(out.pixel(10, 10), s.pixels.pixel(10, 10)) << "outside: exactly as before";
    EXPECT_EQ(out.pixel(160, 100), s.pixels.pixel(160, 100));
    EXPECT_NEAR(px(out, 100, 100)[0], 55 / 255.0f, 0.01f) << "inside: inverted";
    EXPECT_NEAR(px(out, 150, 100)[0], (200 + 55) / 2 / 255.0f, 0.02f) << "half-covered edge pixel: half way";
    // A blur inside the selection reads what is outside it.
    EffectSource edge = source(QSize(200, 200));
    edge.pixels.fill(QRect(0, 0, 100, 200), Qt::black);
    edge.selection = selection::rectangle(edge.canvas, QRectF(100, 0, 100, 200), false);
    const TiledImage blurred = renderEffect(edge, settings(EffectKind::GaussianBlur, {4}));
    EXPECT_GT(alpha(blurred, 101, 100), 0.2f);
    EXPECT_EQ(blurred.pixel(98, 100), edge.pixels.pixel(98, 100));
}

TEST(EffectRender, PreserveOpacityKeepsEveryAlpha) {
    EffectSource s = source(QSize(128, 128));
    s.pixels.fill(QRect(0, 0, 64, 128), Qt::black);
    s.pixels.fill(QRect(64, 0, 64, 128), Qt::white);
    s.pixels.fill(QRect(0, 100, 128, 28), Qt::transparent);
    s.keepAlpha = true;
    const TiledImage out = renderEffect(s, settings(EffectKind::GaussianBlur, {5}));
    EXPECT_EQ(out.pixel(64, 110)[3], 0) << "the empty part stays empty";
    EXPECT_EQ(out.pixel(64, 50)[3], 65535);
    EXPECT_NEAR(px(out, 64, 50)[0], 0.5f, 0.1f) << "the colours mix";
}

TEST(EffectRender, AGaussianBlurSpreadsADotEvenlyAndKeepsItsInk) {
    for (const double radius : {1.5, 6.0}) {
        EffectSource s = source(QSize(256, 256));
        dot(s.pixels, 128, 128);
        const TiledImage out = renderEffect(s, settings(EffectKind::GaussianBlur, {radius}));
        double ink = 0;
        for (int y = 64; y < 192; ++y)
            for (int x = 64; x < 192; ++x)
                ink += alpha(out, x, y);
        EXPECT_NEAR(ink, 1.0, 0.01) << radius;
        EXPECT_NEAR(alpha(out, 128 + 3, 128), alpha(out, 128, 128 - 3), 1e-4) << "round";
        EXPECT_NEAR(alpha(out, 128 - 3, 128), alpha(out, 128 + 3, 128), 1e-4);
        const double peak = 1 / (2 * std::numbers::pi * radius * radius);
        EXPECT_NEAR(alpha(out, 128, 128), peak, peak * 0.1) << "a Gaussian with the radius as its deviation";
    }
}

// A wide blur works on a reduced picture; across an edge it should still follow the Gaussian's curve.
TEST(EffectRender, AWideBlurFollowsTheGaussiansEdge) {
    for (const double radius : {8.0, 40.0}) {
        EffectSource s = source(QSize(1024, 256));
        s.pixels.fill(QRect(0, 0, 512, 256), Qt::black);
        s.selection = std::nullopt;
        const TiledImage out = renderEffect(s, settings(EffectKind::GaussianBlur, {radius}));
        for (const double d : {-2.0, -1.0, 0.0, 0.5, 1.0, 2.0}) {
            const int x = 512 + static_cast<int>(std::lround(d * radius));
            const double expected = 0.5 * std::erfc((x + 0.5 - 512) / (radius * std::numbers::sqrt2));
            EXPECT_NEAR(alpha(out, x, 128), expected, 0.02) << "radius " << radius << " at " << d << " deviations";
        }
    }
}

// The canvas's border carries on outwards for the effects that read their neighbours, so a painted edge doesn't fade; what the
// layer holds off the canvas stays as it is.
TEST(EffectRender, ABlurKeepsAPaintedEdgeAndLeavesWhatIsOffTheCanvas) {
    EffectSource s = source(QSize(200, 100));
    s.pixels.fill(QRect(0, 0, 200, 100), QColor(20, 120, 220));
    s.pixels.fill(QRect(-40, 0, 40, 100), Qt::red);  // off the canvas, to the left
    for (const EffectSettings& e : {settings(EffectKind::GaussianBlur, {8}), settings(EffectKind::GaussianBlur, {40}),
                                    settings(EffectKind::MotionBlur, {0, 30}), settings(EffectKind::RadialBlur, {30, 50, 50})}) {
        const TiledImage out = renderEffect(s, e);
        for (const QPoint p : {QPoint(0, 0), QPoint(199, 99), QPoint(100, 0), QPoint(0, 50)}) {
            const auto got = out.pixel(p.x(), p.y()), was = s.pixels.pixel(p.x(), p.y());
            for (int ch = 0; ch < 4; ++ch)
                EXPECT_NEAR(got[ch], was[ch], 300) << effectSpec(e.kind).title << " at " << p.x() << ", " << p.y();
        }
        EXPECT_EQ(out.pixel(-1, 50), s.pixels.pixel(-1, 50)) << effectSpec(e.kind).title << ": off the canvas stays";
    }
}

TEST(EffectRender, MotionBlurStreaksAlongItsAngleOnly) {
    EffectSource s = source(QSize(128, 128));
    dot(s.pixels, 64, 64);
    const TiledImage across = renderEffect(s, settings(EffectKind::MotionBlur, {0, 20}));
    EXPECT_GT(alpha(across, 72, 64), 0.03f);
    EXPECT_GT(alpha(across, 56, 64), 0.03f) << "centred on the pixel";
    EXPECT_EQ(across.pixel(64, 66)[3], 0) << "nothing above or below";
    EXPECT_EQ(across.pixel(80, 64)[3], 0) << "no further than half the distance";
    const TiledImage up = renderEffect(s, settings(EffectKind::MotionBlur, {90, 20}));
    EXPECT_GT(alpha(up, 64, 72), 0.03f);
    EXPECT_EQ(up.pixel(72, 64)[3], 0);
}

TEST(EffectRender, RadialBlurStreaksTowardsTheCentre) {
    EffectSource s = source(QSize(256, 256));
    dot(s.pixels, 228, 128);  // 100 px right of the centre
    const TiledImage out = renderEffect(s, settings(EffectKind::RadialBlur, {20, 50, 50}));
    EXPECT_GT(alpha(out, 220, 128), 0.02f) << "along the line to the centre";
    EXPECT_GT(alpha(out, 236, 128), 0.02f);
    EXPECT_EQ(out.pixel(228, 132)[3], 0) << "not across it";
    EffectSource middle = source(QSize(256, 256));
    dot(middle.pixels, 128, 128);
    EXPECT_NEAR(alpha(renderEffect(middle, settings(EffectKind::RadialBlur, {50, 50, 50})), 128, 128), 1.0f, 0.3f)
        << "the centre barely moves";
}

TEST(EffectRender, UnsharpMaskSharpensEdgesAndLeavesFlatAreas) {
    EffectSource s = source(QSize(128, 64));
    s.pixels.fill(QRect(0, 0, 64, 64), QColor(100, 100, 100));
    s.pixels.fill(QRect(64, 0, 64, 64), QColor(150, 150, 150));
    const TiledImage out = renderEffect(s, settings(EffectKind::UnsharpMask, {100, 2, 0}));
    EXPECT_LT(px(out, 63, 32)[0], 100 / 255.0f - 0.02f) << "darker by the edge";
    EXPECT_GT(px(out, 64, 32)[0], 150 / 255.0f + 0.02f) << "lighter by the edge";
    EXPECT_NEAR(px(out, 20, 32)[0], 100 / 255.0f, 0.002f) << "flat stays";
    EXPECT_EQ(out.pixel(63, 32)[3], 65535) << "alpha stays";
    const TiledImage held = renderEffect(s, settings(EffectKind::UnsharpMask, {100, 2, 60}));
    EXPECT_EQ(held.pixel(63, 32), s.pixels.pixel(63, 32)) << "differences under the threshold are left";
}

TEST(EffectRender, AddNoiseLeavesTransparencyAndRepeatsWithItsSeed) {
    EffectSource s = source(QSize(128, 128));
    s.pixels.fill(QRect(0, 0, 64, 128), QColor(128, 128, 128));
    EffectSettings noise = settings(EffectKind::AddNoise, {30});
    const TiledImage a = renderEffect(s, noise), b = renderEffect(s, noise);
    EXPECT_EQ(a.toImage(), b.toImage());
    EXPECT_EQ(a.pixel(100, 10)[3], 0);
    EXPECT_NE(a.pixel(10, 10), s.pixels.pixel(10, 10));
    noise.seed = 2;
    EXPECT_NE(renderEffect(s, noise).toImage(), a.toImage());
}

// Chromatic Aberration, against the reference's readings.

namespace {

constexpr QSize kChart(1200, 900);

// The reference's light chart without its discs: 2 px black lines every 100 px (at x 99–100, 199–200 …) on opaque white.
EffectSource lightChart() {
    EffectSource s = source(kChart);
    s.pixels.fill(Qt::white);
    for (int at = 99; at < kChart.width(); at += 100)
        s.pixels.fill(QRect(at, 0, 2, kChart.height()), Qt::black);
    for (int at = 99; at < kChart.height(); at += 100)
        s.pixels.fill(QRect(0, at, kChart.width(), 2), Qt::black);
    return s;
}

EffectSettings aberration(int mode, double amount, double angle = 0, QPointF centre = QPointF(600, 450)) {
    EffectSettings s = EffectSettings::start(EffectKind::ChromaticAberration, kChart);
    s.mode = mode;
    s.values[chromatic::kAmount] = amount;
    s.values[chromatic::kAngle] = angle;
    s.values[chromatic::kCenterX] = centre.x();
    s.values[chromatic::kCenterY] = centre.y();
    return s;
}

// Where channel ch's line near `near` sits, as the reading does it: the centre of its ink (1 − the channel) across the
// line, in pixel indices (a line at 599–600 reads 599.5). Along row `fixed` for a vertical line, column `fixed` otherwise.
double inkCentre(const TiledImage& out, int ch, int near, int fixed, bool vertical) {
    double sum = 0, weight = 0;
    for (int i = near - 30; i <= near + 30; ++i) {
        const auto p = vertical ? px(out, i, fixed) : px(out, fixed, i);
        const double ink = 1 - p[static_cast<std::size_t>(ch)];
        sum += ink * i;
        weight += ink;
    }
    return weight > 0 ? sum / weight : -1;
}

struct Reading {
    int near;        // the line, 100, 600 …
    bool vertical;   // read along row 250; a horizontal line along column 650
    double r, g, b;  // the reference's readings
};

void expectReadings(const EffectSettings& e, std::initializer_list<Reading> readings) {
    const TiledImage out = renderEffect(lightChart(), e);
    for (const Reading& want : readings) {
        const int fixed = want.vertical ? 250 : 650;
        const double tolerance = 0.15;  // the readings carry one decimal
        const auto where = [&](int ch) { return inkCentre(out, ch, want.near, fixed, want.vertical); };
        EXPECT_NEAR(where(0), want.r, tolerance) << "red, line " << want.near << (want.vertical ? " |" : " -");
        EXPECT_NEAR(where(1), want.g, tolerance) << "green, line " << want.near;
        EXPECT_NEAR(where(2), want.b, tolerance) << "blue, line " << want.near;
    }
}

}

TEST(ChromaticAberration, StartsLinearAtFourPixelsWithTheCentreInTheMiddleOfTheCanvas) {
    const EffectSettings s = EffectSettings::start(EffectKind::ChromaticAberration, kChart);
    EXPECT_EQ(s.mode, chromatic::kLinear);
    EXPECT_EQ(s.value(chromatic::kAmount), 4);
    EXPECT_EQ(s.value(chromatic::kAngle), 0);
    EXPECT_EQ(s.value(chromatic::kCenterX), 600);
    EXPECT_EQ(s.value(chromatic::kCenterY), 450);
    const EffectSpec& spec = effectSpec(EffectKind::ChromaticAberration);
    ASSERT_EQ(spec.modes.size(), 2u);
    EXPECT_STREQ(spec.modes[0], "Linear");
    EXPECT_STREQ(spec.modes[1], "Radial");
    EXPECT_EQ(spec.sliders[chromatic::kAmount].maximum, 100);
    EXPECT_EQ(spec.sliders[chromatic::kAngle].onlyInMode, chromatic::kLinear);
    EXPECT_EQ(spec.sliders[chromatic::kCenterX].onlyInMode, chromatic::kRadial);
    EXPECT_EQ(spec.sliders[chromatic::kCenterY].span, SliderSpan::CanvasHeight);
    EXPECT_TRUE(isIdentity(aberration(chromatic::kLinear, 0)));
    EXPECT_TRUE(EffectRenderer(lightChart(), aberration(chromatic::kRadial, 0)).tiles().empty()) << "Amount 0 changes nothing";
}

TEST(ChromaticAberration, LinearMovesRedAmountAlongTheAngleAndBlueTheOtherWay) {
    expectReadings(aberration(chromatic::kLinear, 20, 0),
                   {{600, true, 619.5, 599.5, 579.5}, {100, true, 119.5, 99.5, 79.5}, {300, false, 299.5, 299.5, 299.5}});
    expectReadings(aberration(chromatic::kLinear, 4, 90), {{300, false, 295.5, 299.5, 303.5},  // red moves up the screen
                                                           {600, true, 599.5, 599.5, 599.5}});
    expectReadings(aberration(chromatic::kLinear, 20, 30), {{600, true, 616.8, 599.5, 582.2}, {300, false, 289.5, 299.5, 309.5}});
    expectReadings(aberration(chromatic::kLinear, 20, -135), {{1100, true, 1085.4, 1099.5, 1113.6}, {100, false, 113.6, 99.5, 85.4}});
    expectReadings(aberration(chromatic::kLinear, 4, 45), {{600, true, 602.3, 599.5, 596.7}, {300, false, 296.7, 299.5, 302.3}});
}

TEST(ChromaticAberration, RadialMovesRedOutAndBlueInByTheDistanceFromTheCentre) {
    expectReadings(aberration(chromatic::kRadial, 20), {{600, true, 599.5, 599.5, 599.5},
                                                        {100, true, 85.8, 99.5, 112.5},
                                                        {1100, true, 1113.2, 1099.5, 1086.5},
                                                        {300, false, 295.4, 299.5, 303.4},
                                                        {100, false, 89.9, 99.5, 108.6}});
    expectReadings(aberration(chromatic::kRadial, 4), {{100, true, 96.8, 99.5, 102.2}, {1100, true, 1102.2, 1099.5, 1096.8}});
    // Away from the middle the shift is Amount at the corner farthest from the Centre: smaller near it, larger beyond.
    expectReadings(aberration(chromatic::kRadial, 20, 0, QPointF(250, 250)), {{600, true, 605.7, 599.5, 593.5},
                                                                              {100, true, 96.8, 99.5, 102.1},
                                                                              {1100, true, 1114.5, 1099.5, 1085.0},
                                                                              {300, false, 300.4, 299.5, 298.6}});
}

// A channel read from off the canvas is missing: over white, a cyan band where red would come from beyond the left border and
// a yellow one where blue would come from beyond the right, each Amount wide; the layer stays opaque. (Against the reference's
// 16 layers, pixel by pixel, the largest difference is half a step of 8 bits.)
TEST(ChromaticAberration, AChannelFromOffTheCanvasIsMissingAlongTheBorder) {
    EffectSource s = source(QSize(200, 100));
    s.pixels.fill(Qt::white);
    s.pixels.fill(QRect(-40, 0, 40, 100), Qt::red);  // off the canvas: neither read nor changed
    const TiledImage out = renderEffect(s, aberration(chromatic::kLinear, 20));
    for (const int x : {0, 19}) {
        const auto p = px(out, x, 50);
        EXPECT_NEAR(p[0], 0, 0.002) << "no red at " << x;
        EXPECT_NEAR(p[1], 1, 0.002);
        EXPECT_NEAR(p[2], 1, 0.002);
        EXPECT_EQ(p[3], 1);
    }
    for (const int x : {180, 199}) {
        const auto p = px(out, x, 50);
        EXPECT_NEAR(p[0], 1, 0.002) << "no blue at " << x;
        EXPECT_NEAR(p[2], 0, 0.002);
    }
    EXPECT_EQ(out.pixel(20, 50), s.pixels.pixel(20, 50)) << "white within";
    EXPECT_EQ(out.pixel(-1, 50), s.pixels.pixel(-1, 50)) << "off the canvas stays";
    // Off the canvas counts as nothing in the mix round a point, so a shift of part of a pixel fades the band's last pixel:
    // at 45°, 4 px, red at x 2 comes from x −0.33, 0.17 of it from inside (the reference: 44 of 255).
    EXPECT_NEAR(px(renderEffect(s, aberration(chromatic::kLinear, 4, 45)), 2, 50)[0], 44 / 255.0, 0.004);
}

// On a transparent layer each channel's copy carries its own coverage: a shape shows three times, and its painted area grows.
TEST(ChromaticAberration, OnATransparentLayerEachChannelKeepsItsOwnCoverage) {
    EffectSource s = source(QSize(400, 300));
    s.pixels.fill(QRect(190, 140, 20, 20), QColor(20, 20, 20));
    const TiledImage out = renderEffect(s, aberration(chromatic::kLinear, 30));
    const auto red = px(out, 225, 150), green = px(out, 200, 150), blue = px(out, 165, 150);
    EXPECT_EQ(red[3], 1) << "red's copy, 30 px right";
    EXPECT_NEAR(red[0] * 255, 20, 0.5);
    EXPECT_NEAR(red[1], 0, 0.002);
    EXPECT_EQ(green[3], 1);
    EXPECT_NEAR(green[1] * 255, 20, 0.5);
    EXPECT_EQ(blue[3], 1) << "blue's copy, 30 px left";
    EXPECT_NEAR(blue[2] * 255, 20, 0.5);
    EXPECT_NEAR(blue[0], 0, 0.002);
    EXPECT_EQ(alpha(out, 250, 150), 0) << "nothing beyond the copies";
    EXPECT_EQ(alpha(out, 200, 130), 0) << "nothing above: the shift is sideways";
}

TEST(EffectRender, AnEffectThatChangesNothingTouchesNoTile) {
    EffectSource s = source(QSize(128, 128));
    s.pixels.fill(Qt::red);
    EXPECT_TRUE(EffectRenderer(s, EffectSettings::start(EffectKind::Levels)).tiles().empty());
    EffectSource empty = source(QSize(128, 128));
    EXPECT_TRUE(EffectRenderer(empty, settings(EffectKind::GaussianBlur, {10})).tiles().empty());
}

TEST(EffectRender, APreviewFillsTheTilesOnScreenFirst) {
    std::vector<TileCoord> tiles = {{0, 0}, {6, 6}, {3, 3}, {9, 0}, {4, 4}};
    orderTiles(tiles, QRect(256, 256, 64, 64));  // tile (4, 4)
    EXPECT_EQ(tiles.front(), (TileCoord{4, 4}));
    EXPECT_EQ(tiles[1], (TileCoord{3, 3}));
}

// The session on a document.

TEST(EffectSession, PreviewShowsOnTheLayerAndCancelPutsItBack) {
    Document doc(QSize(300, 200), Qt::white);
    const LayerId id = doc.currentLayerId();
    must(doc.fillLayer(id, QColor(200, 50, 50)));
    const QImage before = doc.layer(id)->pixels.toImage();
    const int steps = doc.undoStack()->count();
    {
        auto session = EffectSession::begin(doc, settings(EffectKind::Invert)).value();
        session->compute();
        ASSERT_TRUE(session->waitForResult(10s));
        EXPECT_NEAR(px(doc.layer(id)->pixels, 10, 10)[0], 55 / 255.0f, 0.01f) << "the layer shows the preview";
        session->setPreview(false);
        EXPECT_EQ(doc.layer(id)->pixels.toImage(), before) << "Preview off: the old pixels";
        session->setPreview(true);
        EXPECT_NE(doc.layer(id)->pixels.toImage(), before);
        session->cancel();
    }
    EXPECT_EQ(doc.layer(id)->pixels.toImage(), before);
    EXPECT_EQ(doc.undoStack()->count(), steps) << "Cancel records nothing";
}

TEST(EffectSession, OkIsOneUndoStepOfTheLastSettings) {
    Document doc(QSize(300, 200), Qt::white);
    const LayerId id = doc.currentLayerId();
    must(doc.fillLayer(id, QColor(200, 50, 50)));
    const QImage before = doc.layer(id)->pixels.toImage();
    const int steps = doc.undoStack()->count();
    auto session = EffectSession::begin(doc, settings(EffectKind::GaussianBlur, {30})).value();
    session->compute();
    for (const double v : {10.0, 40.0, 0.0, 30.0})
        session->setSettings(settings(EffectKind::HueSaturation, {v, 0, 0}));
    ASSERT_TRUE(session->waitForResult(10s));
    must(session->commit());
    EffectSource expected = source(doc.size());
    expected.pixels = TiledImage(doc.size());
    expected.pixels.fill(QColor(200, 50, 50));
    EXPECT_EQ(doc.layer(id)->pixels.toImage(), renderEffect(expected, settings(EffectKind::HueSaturation, {30, 0, 0})).toImage());
    EXPECT_EQ(doc.undoStack()->count(), steps + 1);
    EXPECT_EQ(doc.undoStack()->undoText(), QStringLiteral("Hue/Saturation/Lightness"));
    must(doc.undo());
    EXPECT_EQ(doc.layer(id)->pixels.toImage(), before);
    must(doc.redo());
    EXPECT_NE(doc.layer(id)->pixels.toImage(), before);
}

TEST(EffectSession, OkWorksWithPreviewOffAndSkipsAnEffectThatChangesNothing) {
    Document doc(QSize(100, 100), Qt::white);
    const LayerId id = doc.currentLayerId();
    must(doc.fillLayer(id, Qt::red));
    auto session = EffectSession::begin(doc, settings(EffectKind::Invert)).value();
    session->setPreview(false);
    ASSERT_TRUE(session->waitForResult(10s));
    EXPECT_EQ(px(doc.layer(id)->pixels, 5, 5)[0], 1.0f) << "nothing shown while Preview is off";
    must(session->commit());
    EXPECT_NEAR(px(doc.layer(id)->pixels, 5, 5)[0], 0.0f, 0.001f);
    const int steps = doc.undoStack()->count();
    auto nothing = EffectSession::begin(doc, EffectSettings::start(EffectKind::BrightnessContrast)).value();
    ASSERT_TRUE(nothing->waitForResult(10s));
    EXPECT_EQ(nothing->commit().error(), EditError::NoChange);
    EXPECT_EQ(doc.undoStack()->count(), steps);
}

TEST(EffectSession, RefusedWherePaintingIs) {
    Document doc(QSize(100, 100), Qt::white);
    const LayerId id = doc.currentLayerId();
    LayerProps p = doc.layer(id)->props;
    p.locked = true;
    must(doc.setLayerProps(id, p));
    EXPECT_EQ(EffectSession::begin(doc, settings(EffectKind::Invert)).error(), EditError::LayerLocked);
    const LayerId folder = doc.addFolder().value();
    doc.setCurrentLayer(folder);
    EXPECT_EQ(EffectSession::begin(doc, settings(EffectKind::Invert)).error(), EditError::NotARasterLayer);
}

// The job runs on threads of its own: starting it and showing what is done returns at once, even for a wide blur of a large
// layer, and the result comes in on its own.
TEST(EffectSession, ALargeLayerDoesNotHoldUpTheCaller) {
    Document doc(QSize(4000, 3000), Qt::white);
    const LayerId id = doc.currentLayerId();
    must(doc.fillLayer(id, QColor(30, 90, 200)));
    must(doc.setSelection(selection::rectangle(doc.size(), QRectF(0, 0, 2000, 3000), false)));
    auto session = EffectSession::begin(doc, settings(EffectKind::GaussianBlur, {60})).value();
    const auto start = std::chrono::steady_clock::now();
    session->compute();
    const bool doneAtOnce = session->update();
    const auto took = std::chrono::steady_clock::now() - start;
    EXPECT_FALSE(doneAtOnce);
    EXPECT_LT(took, 100ms);
    ASSERT_TRUE(session->waitForResult(120s));
    EXPECT_DOUBLE_EQ(session->progress(), 1.0);
    must(session->commit());
}
