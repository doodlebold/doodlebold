#include "core/layers/CanvasBackground.h"

#include <array>

namespace doodlebold {

namespace {

struct ModeNames {
    BackgroundMode mode;
    const char* key;
    const char* label;
};

constexpr std::array<ModeNames, kBackgroundModeCount> kNames{{
    {BackgroundMode::White, "white", "White"},
    {BackgroundMode::Black, "black", "Black"},
    {BackgroundMode::Color, "color", "Color"},
    {BackgroundMode::TransparencyBrightChecker, "transparency-bright-checker", "Transparent (Light Checker)"},
    {BackgroundMode::TransparencyDarkChecker, "transparency-dark-checker", "Transparent (Dark Checker)"},
    {BackgroundMode::TransparencyWhite, "transparency-white", "Transparent (White)"},
    {BackgroundMode::TransparencyBlack, "transparency-black", "Transparent (Black)"},
    {BackgroundMode::TransparencyColor, "transparency-color", "Transparent (Color)"},
}};

// TODO(reimplement): choose how the checkerboard behind a transparent canvas should look.
constexpr int kCheckerSquare = 10;
constexpr QRgb kBrightA = 0xffffffff, kBrightB = 0xffcccccc;
constexpr QRgb kDarkA = 0xff666666, kDarkB = 0xff444444;

}

CanvasBackground CanvasBackground::fromPaper(const QColor& paper) {
    CanvasBackground b;
    if (paper.alpha() == 0) {
        b.mode = BackgroundMode::TransparencyWhite;
        return b;
    }
    const QColor opaque(paper.red(), paper.green(), paper.blue());
    if (opaque == QColor(Qt::white))
        return b;
    b.mode = opaque == QColor(Qt::black) ? BackgroundMode::Black : BackgroundMode::Color;
    if (b.mode == BackgroundMode::Color)
        b.color = opaque;
    return b;
}

QColor CanvasBackground::paperColor() const {
    switch (mode) {
    case BackgroundMode::White:
    case BackgroundMode::TransparencyWhite:
    case BackgroundMode::TransparencyBrightChecker: return QColor(Qt::white);
    case BackgroundMode::Black:
    case BackgroundMode::TransparencyBlack: return QColor(Qt::black);
    case BackgroundMode::TransparencyDarkChecker: return QColor::fromRgb(kDarkA);
    case BackgroundMode::Color:
    case BackgroundMode::TransparencyColor: break;
    }
    return QColor(color.red(), color.green(), color.blue());
}

Backdrop CanvasBackground::backdrop() const {
    switch (mode) {
    case BackgroundMode::TransparencyBrightChecker: return {kBrightA, kBrightB, kCheckerSquare};
    case BackgroundMode::TransparencyDarkChecker: return {kDarkA, kDarkB, kCheckerSquare};
    default: break;
    }
    const QRgb flat = paperColor().rgb();
    return {flat, flat, 0};
}

QString backgroundModeKey(BackgroundMode mode) {
    return QString::fromLatin1(kNames[static_cast<std::size_t>(mode)].key);
}

std::optional<BackgroundMode> backgroundModeFromKey(const QString& key) {
    for (const ModeNames& n : kNames)
        if (key == QLatin1String(n.key))
            return n.mode;
    return std::nullopt;
}

QString backgroundModeLabel(BackgroundMode mode) {
    return QString::fromLatin1(kNames[static_cast<std::size_t>(mode)].label);
}

}
