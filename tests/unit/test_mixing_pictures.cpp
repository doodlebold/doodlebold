#include "core/brush/BrushSettings.h"
#include "core/raster/Channel.h"
#include "core/raster/TiledImage.h"
#include "core/stroke/RebuiltStroke.h"

#include <QFile>
#include <QImage>
#include <QString>

#include <gtest/gtest.h>

#include <cstdlib>
#include <vector>

using namespace doodlebold;

// The Brush, Water Color and Marker each paint a fixed set of strokes over a red | blue field and over white: Blending, Dilution and
// Persistence at 0, 50 and 100, Blur Pressure on and off, and Density from 25 to 100. The pictures are kept in tests/fixtures/mixing.
// A change that alters them on purpose replaces them, by running this with DOODLEBOLD_UPDATE_PICTURES=1. Anything else has to give
// them again to within one level in 255.

namespace {

constexpr int kPanelWidth = 160;
constexpr int kPanelHeight = 40;

struct Case {
    const char* what;
    int blending, dilution, persistence, blurPressure, density;
    double pressure;  // under 1 is a pen at that pressure, 1 a mouse
};

std::vector<Case> casesFor(ToolType tool) {
    const BrushSettings start = defaultSettings(tool);
    const int b = start.blending, d = start.dilution, p = start.persistence, bp = start.blurPressure;
    std::vector<Case> cases;
    for (const int v : {0, 50, 100})
        cases.push_back({"blending", v, d, p, bp, 100, 1.0});
    if (tool != ToolType::Marker) {
        for (const int v : {0, 50, 100})
            cases.push_back({"dilution", b, v, p, bp, 100, 1.0});
    }
    for (const int v : {0, 50, 100})
        cases.push_back({"persistence", b, d, v, bp, 100, 1.0});
    if (tool != ToolType::Marker) {
        for (const double pressure : {0.35, 0.8}) {
            cases.push_back({"blur pressure on", b, d, p, 50, 100, pressure});
            cases.push_back({"blur pressure off", b, d, p, 0, 100, pressure});
        }
    }
    for (const int v : {25, 50, 75, 100})
        cases.push_back({"density", b, d, p, bp, v, 1.0});
    return cases;
}

TiledImage field(bool redBlue) {
    TiledImage layer(QSize(kPanelWidth, kPanelHeight));
    if (redBlue) {
        layer.fill(QRect(0, 0, kPanelWidth / 2, kPanelHeight), Qt::red);
        layer.fill(QRect(kPanelWidth / 2, 0, kPanelWidth / 2, kPanelHeight), Qt::blue);
    } else {
        layer.fill(Qt::white);
    }
    return layer;
}

InputSample at(double x, double y, const Case& c) {
    InputSample sample;
    sample.pos = QPointF(x, y);
    sample.pressure = c.pressure;
    sample.kind = c.pressure < 1.0 ? PointerKind::Pen : PointerKind::Mouse;
    return sample;
}

// A gentle wave from left to right, in a colour that shows against both red and blue.
void paintCase(TiledImage& layer, ToolType tool, const Case& c) {
    BrushSettings settings = defaultSettings(tool);
    settings.size = 140;
    settings.pressureSize = false;
    settings.blending = c.blending;
    settings.dilution = c.dilution;
    settings.persistence = c.persistence;
    settings.blurPressure = c.blurPressure;
    settings.density = c.density;
    RebuiltStroke stroke;
    stroke.begin(&layer, QColor(250, 220, 40), settings, at(12.0, 20.0, c));
    for (int i = 1; i <= 34; ++i)
        stroke.add(at(12.0 + i * 4.0, 20.0 + 6.0 * std::sin(i * 0.3), c));
    stroke.end();
}

QImage picturesFor(ToolType tool) {
    const std::vector<Case> cases = casesFor(tool);
    QImage sheet(kPanelWidth * 2, kPanelHeight * static_cast<int>(cases.size()), QImage::Format_ARGB32);
    sheet.fill(Qt::transparent);
    for (std::size_t i = 0; i < cases.size(); ++i) {
        for (const bool redBlue : {true, false}) {
            TiledImage layer = field(redBlue);
            paintCase(layer, tool, cases[i]);
            for (int y = 0; y < kPanelHeight; ++y) {
                for (int x = 0; x < kPanelWidth; ++x) {
                    const auto p = toUnorm8(layer.pixel(x, y));
                    sheet.setPixel(x + (redBlue ? 0 : kPanelWidth), y + kPanelHeight * static_cast<int>(i), qRgba(p[0], p[1], p[2], p[3]));
                }
            }
        }
    }
    return sheet;
}

QString pathFor(const char* name) {
    return QStringLiteral(DOODLEBOLD_FIXTURE_DIR "/mixing/%1.png").arg(QLatin1String(name));
}

// Within one level in 255, channel by channel.
void expectTheSame(const QImage& now, const QImage& kept, const char* name) {
    ASSERT_EQ(now.size(), kept.size()) << name;
    int worst = 0;
    QPoint where;
    for (int y = 0; y < now.height(); ++y) {
        for (int x = 0; x < now.width(); ++x) {
            const QRgb a = now.pixel(x, y);
            const QRgb b = kept.pixel(x, y);
            const int most = std::max({std::abs(qRed(a) - qRed(b)), std::abs(qGreen(a) - qGreen(b)), std::abs(qBlue(a) - qBlue(b)),
                                       std::abs(qAlpha(a) - qAlpha(b))});
            if (most > worst) {
                worst = most;
                where = QPoint(x, y);
            }
        }
    }
    EXPECT_LE(worst, 1) << name << ": " << worst << " levels apart at " << where.x() << ", " << where.y() << ", panel "
                        << where.y() / kPanelHeight;
}

void checkPictures(ToolType tool, const char* name) {
    const QImage now = picturesFor(tool);
    if (qEnvironmentVariableIsSet("DOODLEBOLD_UPDATE_PICTURES")) {
        ASSERT_TRUE(now.save(pathFor(name)));
        GTEST_SKIP() << "kept the new pictures for " << name;
    }
    const QImage kept(pathFor(name));
    ASSERT_FALSE(kept.isNull()) << "no kept pictures at " << pathFor(name).toStdString();
    expectTheSame(now, kept.convertToFormat(QImage::Format_ARGB32), name);
}

}

TEST(MixingPictures, TheBrushPaintsItsKeptPictures) {
    checkPictures(ToolType::Brush, "brush");
}

TEST(MixingPictures, WaterColorPaintsItsKeptPictures) {
    checkPictures(ToolType::WaterColor, "water-color");
}

TEST(MixingPictures, TheMarkerPaintsItsKeptPictures) {
    checkPictures(ToolType::Marker, "marker");
}
