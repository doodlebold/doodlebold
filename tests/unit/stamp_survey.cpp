// Paints straight strokes through the app's own input path and measures what they leave along their length, to find stamps a painter
// could see. The layer under a stroke changes only from top to bottom, and the pen moves
// left to right at a steady pressure, so anything that changes along a row is the stroke's own doing. Prints one line per case, and
// writes each case's middle at actual pixels, beside the same picture with what changes along the row multiplied by 16.
//
//   doodlebold_stamp_survey --out DIR --tool Brush [--sizes 1,2,3] [--speeds 0.5,3,12,60] [--pressures full,p70,ramp,off,mouse]
//                           [--stabilizer 3] [--edge 1..5] [--sharpness N] [--density N] [--blending N] [--persistence N] [--layer empty|bands|solid|edge|ramp]
//
// A tool that moves paint along the stroke leaves bands as they were whether or not its stamps show, so the Smudge starts from "ramp",
// colours changing evenly from left to right, which a running average takes out exactly, and the Clone Stamp copies bands from further
// down than it paints. "edge" is a colour boundary across the stroke's path, for looking at.
//
// Sizes are in px and speeds in px per pen event, 8 ms apart. The line it prints gives, in levels of 255 and over the middle of the
// stroke, how far a row strays from its own running average: inside the stroke, and on its rim, with the row and the period of the
// worst case. Then the same over the fade at the stroke's start and at its end, along three rows through its middle, leaving out
// the outer rim: "pen" pressure rises over four events, holds, and falls over four, as a pen's does.

#include "core/brush/BrushSettings.h"
#include "core/document/Document.h"
#include "core/paint/StrokeController.h"

#include <QColor>
#include <QDir>
#include <QGuiApplication>
#include <QImage>
#include <QPainter>
#include <QStringList>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <vector>

using namespace doodlebold;

namespace {

constexpr double kEventMs = 8.0;
constexpr int kShownWide = 420;
constexpr int kShownTall = 180;
constexpr double kShownGain = 16.0;

[[noreturn]] void fail(const QString& message) {
    std::fprintf(stderr, "doodlebold_stamp_survey: %s\n", qPrintable(message));
    std::exit(2);
}

struct Options {
    QString out;
    ToolType tool = ToolType::Brush;
    QString toolName = "Brush";
    std::vector<double> sizes{1, 2, 3, 4, 6, 8, 10, 14, 20, 30, 50, 80, 120, 170, 250, 400, 700, 1200, 2000, 4000};
    std::vector<double> speeds{0.5, 3, 12, 60};
    QStringList pressures{"full", "p70", "ramp"};
    int stabilizer = 3;
    int edge = 0;  // 1 to 5, or 0 for the tool's own
    int sharpness = -1;
    int density = -1;
    int blending = -1;
    int persistence = -1;
    QString layer = "auto";
};

ToolType toolFrom(const QString& text) {
    const QString wanted = text.trimmed().toLower().remove(' ');
    for (int i = 0; i <= static_cast<int>(ToolType::Clone); ++i) {
        const auto t = static_cast<ToolType>(i);
        if (toolTypeName(t).toLower().remove(' ') == wanted)
            return t;
    }
    fail(QStringLiteral("no tool called %1").arg(text));
}

std::vector<double> numbersFrom(const QString& text) {
    std::vector<double> out;
    for (const QString& part : text.split(','))
        out.push_back(part.toDouble());
    return out;
}

Options optionsFrom(const QStringList& args) {
    Options o;
    for (int i = 1; i + 1 < args.size(); i += 2) {
        const QString& flag = args[i];
        const QString& value = args[i + 1];
        if (flag == "--out") {
            o.out = value;
        } else if (flag == "--tool") {
            o.tool = toolFrom(value);
            o.toolName = value;
        } else if (flag == "--sizes") {
            o.sizes = numbersFrom(value);
        } else if (flag == "--speeds") {
            o.speeds = numbersFrom(value);
        } else if (flag == "--pressures") {
            o.pressures = value.split(',');
        } else if (flag == "--stabilizer") {
            o.stabilizer = value.toInt();
        } else if (flag == "--edge") {
            o.edge = value.toInt();
        } else if (flag == "--sharpness") {
            o.sharpness = value.toInt();
        } else if (flag == "--density") {
            o.density = value.toInt();
        } else if (flag == "--blending") {
            o.blending = value.toInt();
        } else if (flag == "--persistence") {
            o.persistence = value.toInt();
        } else if (flag == "--layer") {
            o.layer = value;
        } else {
            fail(QStringLiteral("unknown argument %1").arg(flag));
        }
    }
    if (o.out.isEmpty())
        fail("--out is needed");
    return o;
}

struct Case {
    double size = 0.0;
    double speed = 0.0;
    QString pressure;
};

// Where the stroke runs and which part of it counts: past each end by a radius and what the stabilizer and the first pressures take.
struct Geometry {
    QSize canvas;
    double y = 0.0;
    double firstX = 0.0;
    int events = 0;
    int left = 0, right = 0, top = 0, bottom = 0;
    int window = 0;
};

Geometry geometryOf(const Case& c, int stabilizer) {
    const double radius = std::ceil(c.size / 2.0);
    const double margin = radius * 1.3 + 2.0 * std::max(stabilizer, 1) * c.speed + 24.0;
    const double middle = std::max(160.0, 1.2 * c.size);
    const double length = 2.0 * margin + middle;
    const int pad = static_cast<int>(radius * 1.4) + 12;
    Geometry g;
    g.canvas = QSize(static_cast<int>(length) + 2 * pad, static_cast<int>(radius * 2.8) + 2 * pad);
    g.y = g.canvas.height() / 2 + 0.37;
    g.firstX = pad + 0.21;
    g.events = static_cast<int>(length / c.speed) + 1;
    g.left = static_cast<int>(g.firstX + margin);
    g.right = static_cast<int>(g.firstX + length - margin);
    g.top = std::max(0, static_cast<int>(g.y - radius * 1.35 - 6));
    g.bottom = std::min(g.canvas.height(), static_cast<int>(g.y + radius * 1.35 + 7));
    g.window = 2 * std::max(31, static_cast<int>(0.2 * c.size)) + 1;
    return g;
}

int bandOf(const Case& c) {
    return std::max(6, static_cast<int>(c.size / 6.0));
}

// Tools that blend or take paint away need something there. The bands change only from top to bottom.
QImage startingLayer(const Options& o, const Case& c, const Geometry& g) {
    const QSize canvas = g.canvas;
    QString kind = o.layer;
    if (kind == "auto") {
        kind = "empty";
        if (o.tool == ToolType::Eraser)
            kind = "solid";
        if (o.tool == ToolType::Blur || o.tool == ToolType::Clone)
            kind = "bands";
        if (o.tool == ToolType::Smudge)
            kind = "ramp";
    }
    if (kind == "empty")
        return {};
    QImage image(canvas, QImage::Format_RGBA8888);
    if (kind == "solid") {
        image.fill(QColor(0, 0, 0));
        return image;
    }
    if (kind == "ramp") {
        for (int y = 0; y < canvas.height(); ++y) {
            auto* line = image.scanLine(y);
            for (int x = 0; x < canvas.width(); ++x) {
                const double t = static_cast<double>(x) / (canvas.width() - 1);
                line[x * 4] = static_cast<uchar>(std::lround(230.0 - 190.0 * t));
                line[x * 4 + 1] = static_cast<uchar>(std::lround(40.0 + 160.0 * t));
                line[x * 4 + 2] = static_cast<uchar>(std::lround(40.0 + 160.0 * t));
                line[x * 4 + 3] = 255;
            }
        }
        return image;
    }
    if (kind == "edge") {
        image.fill(QColor(40, 90, 200));
        QPainter(&image).fillRect(QRectF(0.0, 0.0, g.left - c.size * 0.25, canvas.height()), QColor(214, 40, 40));
        return image;
    }
    const std::array<QColor, 4> colours{QColor(214, 40, 40), QColor(247, 200, 60), QColor(40, 90, 200), QColor(250, 250, 245)};
    const int band = bandOf(c);
    for (int y = 0; y < canvas.height(); ++y) {
        const QColor colour = colours[static_cast<std::size_t>(y / band) % colours.size()];
        auto* line = reinterpret_cast<QRgb*>(image.scanLine(y));
        std::fill_n(line, canvas.width(),
                    qToBigEndian(static_cast<quint32>(colour.red()) << 24 | colour.green() << 16 | colour.blue() << 8 | 255u));
    }
    return image;
}

struct Along {
    double share = 0.0;  // of the stroke's events
    double first = 0.0;  // events since the first
    double last = 0.0;  // events until the last
};

double pressureAt(const QString& mode, const Along& along) {
    if (mode == "pen") {
        const double firstEvents = along.first / 4.0;
        const double lastEvents = along.last / 4.0;
        return 0.9 * std::clamp(std::min(firstEvents, lastEvents), 0.0, 1.0);
    }
    if (mode == "p70")
        return 0.7;
    if (mode == "ramp")
        return 0.15 + 0.85 * along.share;
    return 1.0;
}

BrushSettings settingsFor(const Options& o, const Case& c) {
    BrushSettings s = defaultSettings(o.tool);
    s.size = static_cast<int>(std::lround(c.size * 10.0));
    if (o.edge > 0)
        s.edge = o.edge - 1;
    if (o.sharpness >= 0)
        s.sharpness = o.sharpness;
    if (o.density >= 0)
        s.density = o.density;
    if (o.blending >= 0)
        s.blending = o.blending;
    if (o.persistence >= 0)
        s.persistence = o.persistence;
    if (c.pressure == "off") {
        s.pressureSize = false;
        s.pressureDensity = false;
        s.pressureBlending = false;
    }
    s.stabilizer = StabilizerLevel{o.stabilizer};
    return s.clamped();
}

void paint(Document& doc, const Options& o, const Case& c, const Geometry& g) {
    StrokeController stroke(&doc);
    if (o.tool == ToolType::Clone)
        stroke.cloneStamp().setSource(QPointF(g.firstX - 40.0, g.y + 1.5 * bandOf(c)));
    const PointerKind kind = c.pressure == "mouse" ? PointerKind::Mouse : PointerKind::Pen;
    const BrushSettings settings = settingsFor(o, c);
    const auto sample = [&](int i) {
        InputSample s;
        s.pos = QPointF(g.firstX + i * c.speed, g.y);
        s.pressure = pressureAt(c.pressure, {static_cast<double>(i) / std::max(g.events - 1, 1), static_cast<double>(i),
                                             static_cast<double>(g.events - 1 - i)});
        s.timestampMs = static_cast<std::uint64_t>(1000.0 + i * kEventMs);
        s.kind = kind;
        return s;
    };
    if (const auto began = stroke.begin(sample(0), QColor(20, 30, 70), settings, o.stabilizer); !began)
        fail(QStringLiteral("the stroke was refused"));
    for (int i = 1; i < g.events; ++i)
        stroke.add(sample(i));
    stroke.finish();
}

// One row's three channels, in levels of 255, over white.
struct Picture {
    int width = 0, height = 0;
    std::vector<float> levels;
    float* at(int x, int y) { return levels.data() + (static_cast<std::size_t>(y) * width + x) * 3; }
    const float* at(int x, int y) const { return levels.data() + (static_cast<std::size_t>(y) * width + x) * 3; }
};

Picture middleOf(const TiledImage& layer, const Geometry& g) {
    Picture p;
    p.width = g.right - g.left;
    p.height = g.bottom - g.top;
    p.levels.resize(static_cast<std::size_t>(p.width) * p.height * 3);
    for (int y = 0; y < p.height; ++y) {
        for (int x = 0; x < p.width; ++x) {
            const std::array<Channel, 4> c = layer.pixel(x + g.left, y + g.top);
            const double white = kChannelOne - c[3];
            float* out = p.at(x, y);
            for (int i = 0; i < 3; ++i)
                out[i] = static_cast<float>((c[i] + white) * 255.0 / kChannelOne);
        }
    }
    return p;
}

std::vector<float> alongRow(const std::vector<float>& in, int window);

// The largest swing along a row's stretch, less its running average, leaving out half a window at each end. Below 0 if it's too short.
double ringsAlong(const std::vector<float>& row, int window) {
    if (static_cast<int>(row.size()) < 3 * window)
        return -1.0;
    const std::vector<float> rest = alongRow(row, window);
    const auto [low, high] = std::minmax_element(rest.begin() + window / 2, rest.end() - window / 2);
    return *high - *low;
}

struct Rings {
    double start = 0.0;
    double end = 0.0;
};

// Over the start's and the end's fade: from 0.8 of a radius outside the stroke's first and last place to 0.8 of a radius inside it, so past
// the outer rim, along three rows.
Rings ringsOf(const TiledImage& layer, const Geometry& g, const Case& c) {
    const double radius = c.size / 2.0;
    const int reach = static_cast<int>(radius * 0.8);
    const int window = std::max(9, static_cast<int>(c.size * 0.15)) | 1;
    Rings rings{-1.0, -1.0};
    const double lastX = g.firstX + (g.events - 1) * c.speed;
    for (const double share : {-0.3, 0.0, 0.3}) {
        const int y = static_cast<int>(g.y + share * radius);
        const auto rowAround = [&](double centre) {
            const int left = static_cast<int>(centre) - reach;
            std::vector<float> row(static_cast<std::size_t>(2 * reach));
            for (int x = 0; x < 2 * reach; ++x) {
                const std::array<Channel, 4> p = layer.pixel(left + x, y);
                row[x] = static_cast<float>((p[0] + p[1] + p[2] + 3.0 * (kChannelOne - p[3])) * 85.0 / kChannelOne);
            }
            return row;
        };
        rings.start = std::max(rings.start, ringsAlong(rowAround(g.firstX), window));
        rings.end = std::max(rings.end, ringsAlong(rowAround(lastX), window));
    }
    return rings;
}

// `in` less its centred running average, over `in`'s whole length: the first and last half windows count as if the row ran on evenly.
std::vector<float> alongRow(const std::vector<float>& in, int window) {
    const int n = static_cast<int>(in.size());
    std::vector<double> sums(n + 1, 0.0);
    for (int i = 0; i < n; ++i)
        sums[i + 1] = sums[i] + in[i];
    std::vector<float> out(in.size());
    for (int i = 0; i < n; ++i) {
        const int a = std::max(0, i - window / 2);
        const int b = std::min(n, i + window / 2 + 1);
        out[i] = in[i] - static_cast<float>((sums[b] - sums[a]) / (b - a));
    }
    return out;
}

struct Finding {
    double inside = 0.0, rim = 0.0;
    int insideRow = -1, rimRow = -1;
    int period = 0;
    std::vector<float> worst;
};

int periodOf(const std::vector<float>& row) {
    int best = 0;
    double bestScore = 0.0;
    const int n = static_cast<int>(row.size());
    for (int lag = 2; lag < n / 3; ++lag) {
        double score = 0.0;
        for (int i = lag; i < n; ++i)
            score += row[i] * row[i - lag];
        score /= n - lag;
        if (score > bestScore * 1.02) {
            bestScore = score;
            best = lag;
        }
    }
    return best;
}

Finding measure(const Picture& p, int window, std::vector<float>& shown) {
    Finding f;
    shown.assign(static_cast<std::size_t>(p.width) * p.height * 3, 0.0f);
    std::vector<double> rowMean(p.height);
    for (int y = 0; y < p.height; ++y) {
        double sum = 0.0;
        for (int x = 0; x < p.width; ++x)
            sum += p.at(x, y)[1];
        rowMean[y] = sum / p.width;
    }
    std::vector<float> row(p.width);
    for (int y = 0; y < p.height; ++y) {
        const bool steep =
            (y > 0 && std::abs(rowMean[y] - rowMean[y - 1]) > 1.5) || (y + 1 < p.height && std::abs(rowMean[y] - rowMean[y + 1]) > 1.5);
        for (int channel = 0; channel < 3; ++channel) {
            for (int x = 0; x < p.width; ++x)
                row[x] = p.at(x, y)[channel];
            const std::vector<float> rest = alongRow(row, window);
            const auto [low, high] =
                std::minmax_element(rest.begin() + window / 2, rest.end() - window / 2);  // the running average is cut short at the ends
            const double spread = *high - *low;
            double& worst = steep ? f.rim : f.inside;
            int& worstRow = steep ? f.rimRow : f.insideRow;
            if (spread > worst) {
                worst = spread;
                worstRow = y;
                f.worst = rest;
            }
            for (int x = 0; x < p.width; ++x)
                shown[(static_cast<std::size_t>(y) * p.width + x) * 3 + channel] = rest[x];
        }
    }
    if (!f.worst.empty())
        f.period = periodOf(f.worst);
    return f;
}

// The stroke's upper rim and a little of what's inside it, at actual pixels: the plain picture and what changes along the row, times 16.
// The most the stroke changed any channel in the middle, in levels: 0 means the case shows nothing about the tool.
double movedBy(const Picture& p, const QImage& start, const Geometry& g) {
    double most = 0.0;
    if (start.isNull())
        return -1.0;
    for (int y = 0; y < p.height; ++y) {
        const uchar* line = start.constScanLine(y + g.top);
        for (int x = 0; x < p.width; ++x)
            for (int i = 0; i < 3; ++i)
                most = std::max(most, std::abs(double(p.at(x, y)[i]) - line[(x + g.left) * 4 + i]));
    }
    return most;
}

QImage shownPair(const Picture& p, const std::vector<float>& rest, const Finding& f) {
    const int wide = std::min(kShownWide, p.width);
    const int tall = std::min(kShownTall, p.height);
    int firstY = 0;
    if (p.height > tall) {
        const int interesting = f.rim >= f.inside && f.rimRow >= 0 ? f.rimRow : std::max(f.insideRow, 0);
        firstY = std::clamp(interesting - tall / 2, 0, p.height - tall);
    }
    QImage out(wide * 2 + 6, tall, QImage::Format_RGB888);
    out.fill(Qt::black);
    for (int y = 0; y < tall; ++y) {
        uchar* line = out.scanLine(y);
        for (int x = 0; x < wide; ++x) {
            const float* plain = p.at(x, y + firstY);
            for (int i = 0; i < 3; ++i) {
                line[x * 3 + i] = static_cast<uchar>(std::clamp(std::lround(plain[i]), 0L, 255L));
                const float r = rest[(static_cast<std::size_t>(y + firstY) * p.width + x) * 3 + i];
                line[(x + wide + 6) * 3 + i] = static_cast<uchar>(std::clamp(std::lround(128.0 + r * kShownGain), 0L, 255L));
            }
        }
    }
    return out;
}

QImage sheetOf(const std::vector<std::pair<QString, QImage>>& rows) {
    int width = 0, height = 0;
    for (const auto& [label, image] : rows) {
        width = std::max(width, image.width());
        height += image.height() + 16;
    }
    QImage sheet(width, height, QImage::Format_RGB888);
    sheet.fill(QColor(60, 60, 60));
    QPainter painter(&sheet);
    painter.setPen(Qt::white);
    QFont font = painter.font();
    font.setPixelSize(11);
    painter.setFont(font);
    int y = 0;
    for (const auto& [label, image] : rows) {
        painter.drawText(4, y + 12, label);
        painter.drawImage(0, y + 16, image);
        y += image.height() + 16;
    }
    return sheet;
}

}

int main(int argc, char** argv) {
    QGuiApplication app(argc, argv);
    const Options o = optionsFrom(QGuiApplication::arguments());
    QDir().mkpath(o.out);
    std::printf(
        "tool,size,speed,pressure,stabilizer,edge,sharpness,density,inside,insideRow,rim,rimRow,period,ringsStart,ringsEnd,moved\n");
    for (const double speed : o.speeds) {
        for (const QString& pressure : o.pressures) {
            std::vector<std::pair<QString, QImage>> rows;
            for (const double size : o.sizes) {
                const Case c{size, speed, pressure};
                const Geometry g = geometryOf(c, o.stabilizer);
                Document doc(g.canvas, Qt::white);
                const QImage start = startingLayer(o, c, g);
                if (!start.isNull())
                    doc.paintTarget(doc.currentLayerId()).value()->writeImage(start, {0, 0});
                paint(doc, o, c, g);
                const Picture picture = middleOf(doc.layer(doc.currentLayerId())->pixels, g);
                std::vector<float> rest;
                const Finding f = measure(picture, g.window, rest);
                const Rings rings = ringsOf(doc.layer(doc.currentLayerId())->pixels, g, c);
                std::printf("%s,%g,%g,%s,%d,%d,%d,%d,%.2f,%d,%.2f,%d,%d,%.2f,%.2f,%.2f\n", qPrintable(o.toolName), size, speed,
                            qPrintable(pressure), o.stabilizer, o.edge, o.sharpness, o.density, f.inside, f.insideRow, f.rim, f.rimRow,
                            f.period, rings.start, rings.end, movedBy(picture, start, g));
                std::fflush(stdout);
                rows.emplace_back(QStringLiteral("size %1  inside %2  rim %3  period %4")
                                      .arg(size)
                                      .arg(f.inside, 0, 'f', 1)
                                      .arg(f.rim, 0, 'f', 1)
                                      .arg(f.period),
                                  shownPair(picture, rest, f));
            }
            const QString name = QStringLiteral("%1/%2-v%3-%4-s%5.png")
                                     .arg(o.out, QString(o.toolName).remove(' '))
                                     .arg(speed)
                                     .arg(pressure)
                                     .arg(o.stabilizer);
            sheetOf(rows).save(name);
        }
    }
    return 0;
}
