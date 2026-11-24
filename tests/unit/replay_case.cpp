// Renders one reference case: a stroke file painted with one tool, its settings named as the tool panel shows them, a colour
// and an optional starting layer, into a PNG of the layer flattened over white. It paints through the app's own input path: each
// stroke goes to a StrokeController as the canvas hands it over, the press to begin(), each move to add() and the release to
// finish(), with the stabilizer.
//
//   doodlebold_replay_case <strokes.jsonl> <out.png> [--tool Pencil] [--color R,G,B] [--layer start.png]
//                          [--set "Name=value"]... [--variant name] [--pressure-range low,high] [--clone-source X,Y] [--pointer mouse]
//
// Settings start at the tool's starting values. Names: Size (px), Size pressure (on/off), Min Size, Density, Density pressure,
// Min Density, Edge (button 1 to 5), Sharpness, Size sensitivity, Density sensitivity, Blending, Blending pressure,
// Blending sensitivity, Dilution, Persistence, Blur Pressure, Blur Width, Aligned (on/off), Sample (Current Layer or All Layers),
// Stabilizer (Global or a level), Tip Shape and Texture (a built-in picture's name), Tip Shape Strength / Scale / Invert, Texture Strength / Scale
// (%), Texture Invert and Texture Invert for Transparency, Curve Interpolation
// (on/off), Bristle Size, Hair Min, Direction (None, Auto or Pen Direction), Detect Turn (on/off), Randomize, a brush shape's Shape Density, Angle Control (0 to 2), Angle, Angle Jitter, Scaling, Size Jitter, Spacing,
// Scattering and HQ Down Scaling (on/off). REPLAY_PICTURES=<folder> adds a library folder's own pictures (s_library). --clone-source is the Clone Stamp's source point, as an Alt+click there. --variant picks a candidate of a
// round by its name; without it the tool paints its own behaviour. --pressure-range is the range in Options, in %: the stroke
// files hold the pressure the tool gets, so it starts at 0,100.

#include "core/brush/BrushResources.h"
#include "core/brush/BrushSettings.h"
#include "core/document/Document.h"
#include "core/paint/StrokeController.h"
#include "core/stroke/RebuiltStroke.h"
#include "core/stroke/StrokeRecording.h"
#include "core/stroke/Variant.h"

#include <QBuffer>
#include <QCoreApplication>
#include <QFile>

#include <array>
#include <QImage>
#include <QImageReader>
#include <QStringList>
#include <QTemporaryDir>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <map>
#include <optional>

using namespace doodlebold;

namespace {

constexpr QSize kCanvas(768, 512);
constexpr int kGlobalStabilizer = 3;  // the reference cases start the Stabilizer at Global, at 3

[[noreturn]] void fail(const QString& message) {
    std::fprintf(stderr, "doodlebold_replay_case: %s\n", qPrintable(message));
    std::exit(2);
}

bool onOff(const QString& value) {
    const QString v = value.trimmed().toLower();
    if (v == "on" || v == "true" || v == "1")
        return true;
    if (v == "off" || v == "false" || v == "0")
        return false;
    fail(QStringLiteral("expected on or off, not %1").arg(value));
}

double decimal(const QString& value) {
    bool ok = false;
    const double v = QString(value).remove('%').remove(QStringLiteral("px")).trimmed().toDouble(&ok);
    if (!ok)
        fail(QStringLiteral("expected a number, not %1").arg(value));
    return v;
}

int number(const QString& value) {
    return static_cast<int>(std::lround(decimal(value)));
}

struct Case {
    BrushSettings settings;
    int stabilizer = kGlobalStabilizer;
};

void setStabilizer(Case& c, const QString& value) {
    if (value.trimmed().toLower() == "global") {
        c.settings.stabilizer.reset();
        c.stabilizer = kGlobalStabilizer;
        return;
    }
    const std::optional<StabilizerLevel> level = StabilizerLevel::fromLabel(value);
    c.settings.stabilizer = level ? *level : StabilizerLevel{number(value)};
    c.stabilizer = c.settings.stabilizer->value;
}

// The pictures a tip shape or texture is named from: the built-ins, and with REPLAY_PICTURES=<folder> a library folder's own as well
// (forms/blots, textures, ... as the app's data folder holds them), which take the built-ins' place where their names meet.
const BrushResourceLibrary* s_library = nullptr;

// The bristle box's Direction by the name the panel shows.
int bristleDirectionFrom(const QString& text) {
    const QString v = text.trimmed().toLower();
    if (v == "none")
        return kBristleDirectionNone;
    if (v == "auto")
        return kBristleDirectionAuto;
    if (v == "pen direction")
        return kBristleDirectionPen;
    fail(QStringLiteral("a Direction is None, Auto or Pen Direction, not %1").arg(text));
}

void apply(Case& c, const QString& name, const QString& value) {
    BrushSettings& s = c.settings;
    const std::map<QString, std::function<void()>> setters = {
        {"size", [&] { s.size = static_cast<int>(std::lround(decimal(value) * 10.0)); }},
        {"size pressure", [&] { s.pressureSize = onOff(value); }},
        {"min size", [&] { s.minSize = number(value); }},
        {"density", [&] { s.density = number(value); }},
        {"density pressure", [&] { s.pressureDensity = onOff(value); }},
        {"min density", [&] { s.minDensity = number(value); }},
        {"edge", [&] { s.edge = number(value) - 1; }},
        {"sharpness", [&] { s.sharpness = number(value); }},
        {"size sensitivity", [&] { s.sensitivitySize = number(value); }},
        {"density sensitivity", [&] { s.sensitivityDensity = number(value); }},
        {"blending", [&] { s.blending = number(value); }},
        {"blending pressure", [&] { s.pressureBlending = onOff(value); }},
        {"blending sensitivity", [&] { s.sensitivityBlending = number(value); }},
        {"dilution", [&] { s.dilution = number(value); }},
        {"persistence", [&] { s.persistence = number(value); }},
        {"blur pressure", [&] { s.blurPressure = number(value); }},
        {"blur width", [&] { s.blurWidth = number(value); }},
        {"aligned", [&] { s.cloneAligned = onOff(value); }},
        {"sample", [&] { s.cloneAllLayers = value.trimmed().toLower() == QStringLiteral("all layers"); }},
        {"stabilizer", [&] { setStabilizer(c, value); }},
        {"tip shape",
         [&] {  // a brush shape brings its settings file's settings, as choosing it in the panel does
             s.form = value.trimmed();
             if (const BrushResource* r = s_library->form(s.form); r && r->shapeSettings)
                 applyShapeSettings(s, *r->shapeSettings);
         }},
        {"bristle size", [&] { s.bristleSize = number(value); }},
        {"hair", [&] { s.bristleSize = number(value); }},
        {"hair min", [&] { s.bristleMinimum = number(value); }},
        {"direction", [&] { s.bristleDirection = bristleDirectionFrom(value); }},
        {"detect turn", [&] { s.bristleDetectTurn = onOff(value); }},
        {"randomize", [&] { s.bristleRandomize = number(value); }},
        {"scaling", [&] { s.shapeScaling = number(value); }},
        {"spacing", [&] { s.shapeSpacing = number(value); }},
        {"angle jitter", [&] { s.shapeAngleJitter = number(value); }},
        {"size jitter", [&] { s.shapeSizeJitter = number(value); }},
        {"scattering", [&] { s.shapeScattering = number(value); }},
        {"hq down scaling", [&] { s.shapeHighQuality = onOff(value); }},
        {"tip shape strength", [&] { s.formStrength = number(value); }},
        {"tip shape scale", [&] { s.formScale = number(value); }},
        {"tip shape invert", [&] { s.formInvert = onOff(value); }},
        {"shape density", [&] { s.shapeDensity = number(value); }},
        {"angle control", [&] { s.shapeAngleControl = number(value); }},
        {"angle", [&] { s.shapeAngle = number(value); }},
        {"texture", [&] { s.texture = value.trimmed(); }},
        {"texture strength", [&] { s.textureStrength = number(value); }},
        {"texture scale", [&] { s.textureScale = number(value); }},
        {"texture invert for transparency", [&] { s.textureEraseLow = onOff(value); }},
        {"texture invert", [&] { s.textureInvert = onOff(value); }},
        {"curve interpolation", [&] { s.curveInterpolation = onOff(value); }},
    };
    const auto setter = setters.find(name.trimmed().toLower());
    if (setter == setters.end())
        fail(QStringLiteral("no setting called %1").arg(name));
    setter->second();
}

QColor colourFrom(const QString& text) {
    const QStringList parts = text.split(',');
    if (parts.size() != 3)
        fail(QStringLiteral("a colour is R,G,B, not %1").arg(text));
    return QColor(number(parts[0]), number(parts[1]), number(parts[2]));
}

QPointF pointFrom(const QString& text) {
    const QStringList parts = text.split(',');
    if (parts.size() != 2)
        fail(QStringLiteral("a point is X,Y in px, not %1").arg(text));
    return {decimal(parts[0]), decimal(parts[1])};
}

PressureRange pressureRangeFrom(const QString& text) {
    const QStringList parts = text.split(',');
    if (parts.size() != 2)
        fail(QStringLiteral("a pressure range is low,high in %, not %1").arg(text));
    return {decimal(parts[0]) / 100.0, decimal(parts[1]) / 100.0};
}

// By its key in tools.json or the name the tool bar shows.
ToolType toolFrom(const QString& text) {
    const QString wanted = text.trimmed().toLower().remove(' ');
    if (const auto t = toolTypeFromKey(wanted))
        return *t;
    for (int i = 0; i <= static_cast<int>(ToolType::SelectionEraser); ++i) {
        const auto t = static_cast<ToolType>(i);
        if (toolTypeName(t).toLower().remove(' ') == wanted)
            return t;
    }
    fail(QStringLiteral("no tool called %1").arg(text));
}

// The layer over white, at 1:1. Layer pixels are premultiplied, so white shows through by what alpha leaves.
QImage flattenedOverWhite(const TiledImage& layer, QSize size) {
    QImage out(size, QImage::Format_RGBA8888);
    for (int y = 0; y < size.height(); ++y) {
        std::uint8_t* line = out.scanLine(y);
        for (int x = 0; x < size.width(); ++x) {
            const std::array<Channel, 4> p = layer.pixel(x, y);
            const double a = p[3] * double(kChannelToFloat);
            for (int i = 0; i < 3; ++i)
                line[x * 4 + i] =
                    static_cast<std::uint8_t>(std::lround(std::clamp(p[i] * double(kChannelToFloat) + 1.0 - a, 0.0, 1.0) * 255.0));
            line[x * 4 + 3] = 255;
        }
    }
    return out;
}

// Keeps only the chunks a PNG needs to be read, so the file carries no metadata.
QByteArray withoutAncillaryChunks(const QByteArray& png) {
    QByteArray out = png.left(8);
    qsizetype at = 8;
    while (at + 12 <= png.size()) {
        const auto* b = reinterpret_cast<const unsigned char*>(png.constData() + at);
        const qsizetype length = (qsizetype(b[0]) << 24) | (b[1] << 16) | (b[2] << 8) | b[3];
        const QByteArray type = png.mid(at + 4, 4);
        if (type == "IHDR" || type == "IDAT" || type == "IEND")
            out += png.mid(at, length + 12);
        at += length + 12;
    }
    return out;
}

void writePng(const QImage& image, const QString& path) {
    QByteArray png;
    QBuffer buffer(&png);
    buffer.open(QIODevice::WriteOnly);
    if (!image.save(&buffer, "PNG"))
        fail(QStringLiteral("can't encode %1").arg(path));
    QFile out(path);
    if (!out.open(QIODevice::WriteOnly) || out.write(withoutAncillaryChunks(png)) < 0)
        fail(QStringLiteral("can't write %1").arg(path));
}

struct Arguments {
    ToolType tool = ToolType::Pencil;
    QColor colour{0, 0, 0};
    QString layerPath;
    QStringList sets;
    QString variant;
    PressureRange pressure{0.0, 1.0};
    std::optional<QPointF> cloneSource;
    bool mouse = false;  // --pointer mouse: every sample as the mouse's, for the reference's mouse readings
};

Arguments argumentsFrom(const QStringList& args) {
    Arguments a;
    for (int i = 3; i < args.size(); ++i) {
        const QString& flag = args[i];
        if (i + 1 >= args.size())
            fail(QStringLiteral("%1 needs a value").arg(flag));
        const QString& value = args[++i];
        if (flag == "--tool")
            a.tool = toolFrom(value);
        else if (flag == "--color")
            a.colour = colourFrom(value);
        else if (flag == "--layer")
            a.layerPath = value;
        else if (flag == "--set")
            a.sets << value;
        else if (flag == "--variant")
            a.variant = value;
        else if (flag == "--pressure-range")
            a.pressure = pressureRangeFrom(value);
        else if (flag == "--clone-source")
            a.cloneSource = pointFrom(value);
        else if (flag == "--pointer")
            a.mouse = value == QLatin1String("mouse");
        else
            fail(QStringLiteral("unknown argument %1").arg(flag));
    }
    return a;
}

Case caseFrom(const Arguments& a) {
    Case c{defaultSettings(a.tool)};
    if (c.settings.stabilizer)
        c.stabilizer = c.settings.stabilizer->value;
    for (const QString& set : a.sets) {
        const qsizetype eq = set.indexOf('=');
        if (eq < 0)
            fail(QStringLiteral("a setting is Name=value, not %1").arg(set));
        apply(c, set.left(eq), set.mid(eq + 1));
    }
    return c;
}

void loadLayer(Document& doc, const QString& path) {
    if (path.isEmpty())
        return;
    const QImage start(path);
    if (start.isNull())
        fail(QStringLiteral("can't read %1").arg(path));
    doc.paintTarget(doc.currentLayerId()).value()->writeImage(start, {0, 0});
}

// As CanvasView hands a pen stroke over: the press begins it, each move adds to it, and the release only ends it.
void paintStrokes(Document& doc, const std::vector<RecordedStroke>& strokes, const Arguments& a, const Case& c) {
    StrokeController stroke(&doc);
    stroke.setPressureRange(a.pressure);
    if (a.cloneSource)
        stroke.cloneStamp().setSource(*a.cloneSource);  // as an Alt+click there
    for (const RecordedStroke& recorded : strokes) {
        const std::vector<InputSample>& samples = recorded.samples;
        if (const auto began = stroke.begin(samples.front(), a.colour, c.settings.clamped(), c.stabilizer); !began)
            fail(QStringLiteral("the stroke was refused"));
        for (std::size_t i = 1; i + 1 < samples.size(); ++i)
            stroke.add(samples[i]);
        stroke.finish();
    }
}

}

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    const QStringList args = QCoreApplication::arguments();
    if (args.size() < 3)
        fail(QStringLiteral(
            "usage: doodlebold_replay_case <strokes.jsonl> <out.png> [--tool Pencil] [--color R,G,B] "
            "[--layer start.png] [--set \"Name=value\"]... [--variant name] [--pressure-range low,high] [--clone-source X,Y]"));
    const Arguments a = argumentsFrom(args);
    // The pictures that come with Doodlebold, for a tip shape or texture by name, and REPLAY_PICTURES's (s_library).
    QTemporaryDir pictures;
    const BrushResourceLibrary library(qEnvironmentVariableIsSet("REPLAY_PICTURES") ? qEnvironmentVariable("REPLAY_PICTURES")
                                                                                    : pictures.path());
    s_library = &library;
    RebuiltStroke::setResources(&library);
    const Case c = caseFrom(a);
    variant::set(a.variant);

    std::vector<RecordedStroke> strokes = readRecording(args[1]);
    if (strokes.empty())
        fail(QStringLiteral("no pen strokes in %1").arg(args[1]));
    if (a.mouse)
        for (RecordedStroke& stroke : strokes)
            for (InputSample& sample : stroke.samples)
                sample.kind = PointerKind::Mouse;
    // The canvas is the starting layer's size, as the 3000 px cases need; without one, 768 x 512.
    const QSize canvas = a.layerPath.isEmpty() ? kCanvas : QImageReader(a.layerPath).size();
    Document doc(canvas.isValid() ? canvas : kCanvas, Qt::white);
    loadLayer(doc, a.layerPath);
    paintStrokes(doc, strokes, a, c);
    writePng(flattenedOverWhite(doc.layer(doc.currentLayerId())->pixels, doc.size()), args[2]);
    // REPLAY_RAW_OUT=<file>: the layer itself as well, for measuring: x, y, w, h (int32), then its 16-bit premultiplied RGBA, row by row.
    if (const QString raw = qEnvironmentVariable("REPLAY_RAW_OUT"); !raw.isEmpty()) {
        const int rect[4] = {0, 0, doc.size().width(), doc.size().height()};
        QFile out(raw);
        if (!out.open(QIODevice::WriteOnly))
            fail(QStringLiteral("can't write %1").arg(raw));
        out.write(reinterpret_cast<const char*>(rect), sizeof rect);
        const TiledImage& layer = doc.layer(doc.currentLayerId())->pixels;
        for (int y = 0; y < rect[3]; ++y)
            for (int x = 0; x < rect[2]; ++x) {
                const std::array<Channel, 4> p = layer.pixel(x, y);
                out.write(reinterpret_cast<const char*>(p.data()), sizeof(Channel) * 4);
            }
    }
    return 0;
}
