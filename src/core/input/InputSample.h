#pragma once

#include <QPointF>
#include <QString>

#include <cstdint>

namespace doodlebold {

enum class PointerKind : std::uint8_t {
    Mouse,
    Pen,
    Eraser,
    Cursor,
    Touch,
    Unknown
};

enum InputButton : std::uint8_t {
    ButtonNone = 0,
    ButtonTip = 1 << 0,
    ButtonBarrel1 = 1 << 1,
    ButtonBarrel2 = 1 << 2,
};

// One pointer event; never coalesced or resampled before it reaches the stroke.
struct InputSample {
    QPointF pos;               // document coordinates, pixel centres at x + 0.5
    double pressure = 1.0;
    double widthShare = 1.0;  // what's left of the width as a stroke's end closes
    double tiltX = 0.0;
    double tiltY = 0.0;
    double rotation = 0.0;
    double tangentialPressure = 0.0;
    std::uint64_t timestampMs = 0;
    std::uint8_t buttons = ButtonNone;
    PointerKind kind = PointerKind::Unknown;
};

// A mouse's button and a finger give no real pressure.
inline bool sensesPressure(PointerKind kind) {
    return kind == PointerKind::Pen || kind == PointerKind::Eraser;
}

}
