#include "core/brush/BrushSettings.h"
#include "core/raster/Channel.h"
#include "core/raster/TiledImage.h"
#include "core/stroke/RebuiltStroke.h"

#include <QImage>
#include <QString>

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <vector>

using namespace doodlebold;

// The three checks on Water Color: strokes are continuous washes, a colour over itself leaves no trace, and
// translucent paint keeps its hue. Each is measured, and the strokes are kept as pictures in tests/fixtures/mixing, replaced only on
// purpose, by running with DOODLEBOLD_UPDATE_PICTURES=1.

namespace {

BrushSettings water(int size) {
    BrushSettings settings = defaultSettings(ToolType::WaterColor);
    settings.size = size;
    return settings;
}

// A pen moving at `step` px an event, with its pressure wavering around `pressure`.
void line(TiledImage& layer, const BrushSettings& settings, const QColor& ink, double y, double from, double to, double step,
          double pressure, double waver = 0.0) {
    RebuiltStroke stroke;
    InputSample sample;
    sample.kind = PointerKind::Pen;
    sample.pressure = pressure;
    sample.pos = QPointF(from, y);
    stroke.begin(&layer, ink, settings, sample);
    int i = 0;
    for (double x = from + step; x <= to; x += step) {
        ++i;
        sample.pos = QPointF(x, y);
        sample.pressure = std::clamp(pressure + waver * std::sin(i * 2.3) * std::cos(i * 0.7), 0.0, 1.0);
        stroke.add(sample);
    }
    stroke.end();
}

double brightness(const TiledImage& layer, int x, int y) {
    const auto p = layer.pixel(x, y);
    return (p[0] + p[1] + p[2]) / 3.0 / 257.0;
}

QImage pictureOf(const TiledImage& layer) {
    QImage picture(layer.size(), QImage::Format_ARGB32);
    for (int y = 0; y < layer.size().height(); ++y) {
        for (int x = 0; x < layer.size().width(); ++x) {
            const auto p = toUnorm8(layer.pixel(x, y));
            picture.setPixel(x, y, qRgba(p[0], p[1], p[2], p[3]));
        }
    }
    return picture;
}

void expectKept(const QImage& now, const char* name) {
    const QString path = QStringLiteral(DOODLEBOLD_FIXTURE_DIR "/mixing/%1.png").arg(QLatin1String(name));
    if (qEnvironmentVariableIsSet("DOODLEBOLD_UPDATE_PICTURES")) {
        ASSERT_TRUE(now.save(path));
        return;
    }
    const QImage kept = QImage(path).convertToFormat(QImage::Format_ARGB32);
    ASSERT_EQ(kept.size(), now.size()) << name;
    int worst = 0;
    for (int y = 0; y < now.height(); ++y) {
        for (int x = 0; x < now.width(); ++x) {
            const QRgb a = now.pixel(x, y);
            const QRgb b = kept.pixel(x, y);
            worst = std::max({worst, std::abs(qRed(a) - qRed(b)), std::abs(qGreen(a) - qGreen(b)), std::abs(qBlue(a) - qBlue(b)),
                              std::abs(qAlpha(a) - qAlpha(b))});
        }
    }
    EXPECT_LE(worst, 1) << name;
}

// Blue across flat opaque cyan, at three event spacings, its pressure wavering unless `waver` is 0, at Water Color's starting Shape
// or at `edge`.
TiledImage blueAcrossCyan(double step, double waver = 0.1, int edge = -1) {
    TiledImage layer(QSize(900, 300));
    layer.fill(QColor(0, 220, 230));
    BrushSettings settings = water(600);
    if (edge >= 0)
        settings.edge = edge;
    line(layer, settings, QColor(20, 40, 220), 150.5, 60.5, 840.5, step, 0.8, waver);
    return layer;
}

}

TEST(WaterColorChecks, AStrokeIsAContinuousWashWithNoBumpsOneDabApart) {
    for (const double step : {4.0, 12.0, 24.0}) {
        const TiledImage layer = blueAcrossCyan(step);
        double worst = 0.0;
        for (int x = 200; x < 700; ++x)
            worst = std::max(worst,
                             std::abs(brightness(layer, x, 150) - (brightness(layer, x - 6, 150) + brightness(layer, x + 6, 150)) / 2.0));
        EXPECT_LT(worst, 1.0) << "levels, events " << step << " px apart";
        if (step == 12.0)
            expectKept(pictureOf(layer), "water-color-wash");
    }
}

// At steady pressure Water Color's edge is crisp, so wavering pressure shows in the
// outline as it should. Arcs from its dabs would show at steady pressure too. On the 4th Shape: the 5th lays whole stamps, whose outline
// scallops by a pixel as the reference's does.
TEST(WaterColorChecks, ItsOutlineIsASmoothCurveNotARowOfArcs) {
    constexpr int kSmoothShape = kEdgePresets - 2;
    for (const double step : {4.0, 12.0, 24.0}) {
        const TiledImage layer = blueAcrossCyan(step, 0.0, kSmoothShape);
        std::vector<int> edge;
        for (int x = 200; x < 700; ++x) {
            const double centre = brightness(layer, x, 150);
            const double base = brightness(layer, x, 20);
            int y = 150;
            while (y > 90 && std::abs(brightness(layer, x, y) - base) > std::abs(centre - base) / 2.0)
                --y;
            edge.push_back(y);
        }
        double worst = 0.0;
        for (std::size_t i = 6; i + 6 < edge.size(); ++i)
            worst = std::max(worst, std::abs(edge[i] - (edge[i - 6] + edge[i + 6]) / 2.0));
        EXPECT_LE(worst, 0.5) << "px, events " << step << " px apart";
    }
}

TEST(WaterColorChecks, AColourOverItselfLeavesNoTrace) {
    const QColor colour(0, 200, 220);
    for (const int blending : {0, 50, 100}) {
        for (const int dilution : {0, 50, 100}) {
            for (const int persistence : {0, 50, 100}) {
                for (const double pressure : {0.3, 0.6, 1.0}) {
                    TiledImage layer(QSize(300, 200));
                    layer.fill(colour);
                    BrushSettings settings = water(400);
                    settings.blending = blending;
                    settings.dilution = dilution;
                    settings.persistence = persistence;
                    line(layer, settings, colour, 100.5, 40.5, 260.5, 6.0, pressure);
                    for (int y = 60; y < 140; y += 2) {
                        for (int x = 20; x < 280; x += 2) {
                            const auto p = toUnorm8(layer.pixel(x, y));
                            ASSERT_LE(std::max({p[0] - 0, std::abs(p[1] - 200), std::abs(p[2] - 220), 255 - p[3]}), 1)
                                << blending << ", " << dilution << ", " << persistence << " at " << pressure;
                        }
                    }
                }
            }
        }
    }
}

// A tap half over an opaque circle of its own colour and half over empty canvas. The reference, painted
// 2026-11-19: Water Color at its starting values, Size 40 px, no stabilizer, a 300 px canvas with an opaque (0, 200, 220) disc
// of radius 80 px at (150, 150), a pen tap at (230.5, 150.5) at pressure 0.6. On row 150 it leaves x 200 to 210 as they were, and
// x 214 / 218 / 222 / 226 at alpha 253.7 / 252.2 / 249.3 / 238.0 of 255, its colour unchanged throughout: its wet blur softens the
// opaque rim a little toward the bare paper. Ours must do the same, its alphas within kTapAlphaLevels of the reference's.
TEST(WaterColorChecks, ATapAtTheEdgeOfTheSameColourSoftensItAsTheReferences) {
    // Of 255. Ours at x 214 / 218 / 222 / 226: 252.7 / 252.6 / 249.9 / 239.0, all within a level of the reference's.
    constexpr double kTapAlphaLevels = 1.5;
    const QColor colour(0, 200, 220);
    TiledImage layer(QSize(300, 300));
    for (int y = 0; y < 300; ++y) {
        for (int x = 0; x < 300; ++x) {
            if (std::hypot(x - 150.0, y - 150.0) < 80.0)
                layer.fill(QRect(x, y, 1, 1), colour);
        }
    }
    RebuiltStroke stroke;
    InputSample sample;
    sample.kind = PointerKind::Pen;
    sample.pressure = 0.6;
    sample.pos = QPointF(230.5, 150.5);
    stroke.begin(&layer, colour, water(400), sample);
    stroke.end();
    for (const int x : {200, 205, 210}) {
        const auto p = toUnorm8(layer.pixel(x, 150));
        EXPECT_LE(std::max({std::abs(p[1] - 200), std::abs(p[2] - 220), 255 - p[3]}), 1) << x;
    }
    const std::pair<int, double> rim[] = {{214, 253.7}, {218, 252.2}, {222, 249.3}, {226, 238.0}};
    for (const auto& [x, alpha] : rim) {
        const auto p = layer.pixel(x, 150);
        const double a = p[3] / double(kChannelOne) * 255.0;
        EXPECT_NEAR(a, alpha, kTapAlphaLevels) << x;
        // its colour, straight, unchanged
        EXPECT_NEAR(p[1] / double(p[3]) * 255.0, 200.0, 1.0) << x;
        EXPECT_NEAR(p[2] / double(p[3]) * 255.0, 220.0, 1.0) << x;
    }
    expectKept(pictureOf(layer), "water-color-edge-tap");
}

TEST(WaterColorChecks, TranslucentPaintKeepsItsHue) {
    TiledImage layer(QSize(400, 200));
    line(layer, water(500), QColor(0, 0, 255), 100.5, 40.5, 360.5, 6.0, 0.8, 0.1);
    int worst = 0;
    int translucent = 0;
    for (int y = 60; y < 140; ++y) {
        for (int x = 20; x < 380; ++x) {
            const auto p = layer.pixel(x, y);
            if (p[3] < 256 || p[3] > kChannelOne - 256)
                continue;
            ++translucent;
            const double alpha = p[3] / double(kChannelOne);
            const int red = static_cast<int>(std::lround(p[0] / double(kChannelOne) / alpha * 255.0));
            const int green = static_cast<int>(std::lround(p[1] / double(kChannelOne) / alpha * 255.0));
            const int blue = static_cast<int>(std::lround(p[2] / double(kChannelOne) / alpha * 255.0));
            worst = std::max({worst, red, green, 255 - blue});
        }
    }
    EXPECT_GT(translucent, 100);
    EXPECT_LE(worst, 1);
    expectKept(pictureOf(layer), "water-color-translucent");
}
