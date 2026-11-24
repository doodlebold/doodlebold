#pragma once

#include "ui/Theme.h"

#include <QProxyStyle>

namespace doodlebold {

// Fusion with COSMIC-style canvas windows: the title bar is a header in the window colour with the title
// centred and round minimise / maximise / close buttons on the right; the same buttons stand in the menu bar while the
// canvases are maximized. Everything else is Fusion's.
class DoodleboldStyle : public QProxyStyle {
public:
    // The canvas window's header and its round window buttons (component metrics: the window controls are their own component).
    static constexpr int kTitleBarHeight = 38;  // component metric: the canvas window's header
    static constexpr int kButtonSize = Theme::compact::kControlSm;
    static constexpr int kButtonGap = Theme::kSpace2;  // between the buttons, and from the last to the header's edge
    // A check box's or radio button's indicator, px square (component metric).
    static constexpr int kIndicatorSize = 14;  // component metric, described above
    // The canvas window's frame: as wide as the band in which the pointer resizes the window (2 px, then 6 px, needed
    // aiming).
    static constexpr int kFrameWidth = 10;  // component metric, described above
    static constexpr int kFrameMargin = kFrameWidth - 1;

    // A canvas window's dynamic property: how far from the header's right edge something of the window's own (the REC
    // mark) reaches. The title is centred and keeps this clear on both sides.
    static constexpr const char* kHeaderInsetProperty = "doodleboldHeaderInset";

    using QProxyStyle::QProxyStyle;

    int styleHint(StyleHint hint, const QStyleOption* option = nullptr, const QWidget* widget = nullptr,
                  QStyleHintReturn* data = nullptr) const override;
    int pixelMetric(PixelMetric metric, const QStyleOption* option = nullptr, const QWidget* widget = nullptr) const override;
    QSize sizeFromContents(ContentsType type, const QStyleOption* option, const QSize& contents,
                           const QWidget* widget = nullptr) const override;
    QRect subControlRect(ComplexControl control, const QStyleOptionComplex* option, SubControl sub,
                         const QWidget* widget = nullptr) const override;
    void drawComplexControl(ComplexControl control, const QStyleOptionComplex* option, QPainter* painter,
                            const QWidget* widget = nullptr) const override;
    void drawPrimitive(PrimitiveElement element, const QStyleOption* option, QPainter* painter,
                       const QWidget* widget = nullptr) const override;
    // A disabled icon at a third of its opacity. Fusion greys an icon by its lightness, which leaves light icons on a
    // dark theme looking almost enabled.
    QPixmap generatedIconPixmap(QIcon::Mode mode, const QPixmap& pixmap, const QStyleOption* option) const override;

    static constexpr double kDisabledIconOpacity = 0.35;
};

}
