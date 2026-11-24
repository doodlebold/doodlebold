#pragma once

#include <QColor>
#include <QRgb>
#include <QString>

#include <optional>

namespace doodlebold {

// What is seen behind a transparent canvas: one flat colour, or a checkerboard of square-pixel squares alternating between a
// and b (a at (0, 0)), anchored to canvas pixels.
struct Backdrop {
    QRgb a = 0xffffffff;
    QRgb b = 0xffffffff;
    int square = 0;  // 0: flat colour `a`

    QRgb at(int x, int y) const {
        if (square <= 0)
            return a;
        return (((x / square) + (y / square)) & 1) == 0 ? a : b;
    }
    bool operator==(const Backdrop&) const = default;
};

enum class BackgroundMode {
    White,
    Black,
    Color,
    TransparencyBrightChecker,
    TransparencyDarkChecker,
    TransparencyWhite,
    TransparencyBlack,
    TransparencyColor,
};
inline constexpr int kBackgroundModeCount = 8;

// The mode and the colour of the "Color" modes (kept while another mode is chosen). An opaque mode (White, Black, Color) is the
// colour the layers are blended onto, so every blend mode sees an opaque backdrop. A Transparency mode composites
// the layers over transparency and only shows the result over a checkerboard or flat colour; exports keep the alpha.
struct CanvasBackground {
    BackgroundMode mode = BackgroundMode::White;
    QColor color = QColor(128, 128, 128);  // swatch default (#808080); always opaque

    // The background of a canvas that has just `paper` as its colour: transparent paper (alpha 0) is Transparent (White), white
    // and black are those modes, anything else is Color.
    static CanvasBackground fromPaper(const QColor& paper);

    bool isTransparent() const { return mode >= BackgroundMode::TransparencyBrightChecker; }
    QColor paperColor() const;
    Backdrop backdrop() const;

    bool operator==(const CanvasBackground&) const = default;
};

QString backgroundModeKey(BackgroundMode mode);
std::optional<BackgroundMode> backgroundModeFromKey(const QString& key);
QString backgroundModeLabel(BackgroundMode mode);

}
