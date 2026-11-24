#include "ui/TabletInput.h"

#include <QMouseEvent>
#include <QPointingDevice>
#include <QTabletEvent>

#include <cmath>

namespace doodlebold::TabletInput {

namespace {

PointerKind kindOf(const QPointingDevice* dev) {
    if (!dev)
        return PointerKind::Unknown;
    switch (dev->pointerType()) {
    case QPointingDevice::PointerType::Pen: return PointerKind::Pen;
    case QPointingDevice::PointerType::Eraser: return PointerKind::Eraser;
    case QPointingDevice::PointerType::Cursor: return PointerKind::Cursor;
    case QPointingDevice::PointerType::Finger: return PointerKind::Touch;
    case QPointingDevice::PointerType::Generic:
        return dev->type() == QInputDevice::DeviceType::Mouse ? PointerKind::Mouse : PointerKind::Unknown;
    default: return PointerKind::Unknown;
    }
}

QString flagNames(QInputDevice::Capabilities caps) {
    using C = QInputDevice::Capability;
    static const std::pair<C, const char*> kNames[] = {{C::Position, "Position"},
                                                       {C::Area, "Area"},
                                                       {C::Pressure, "Pressure"},
                                                       {C::Velocity, "Velocity"},
                                                       {C::NormalizedPosition, "NormalizedPosition"},
                                                       {C::MouseEmulation, "MouseEmulation"},
                                                       {C::PixelScroll, "PixelScroll"},
                                                       {C::Scroll, "Scroll"},
                                                       {C::Hover, "Hover"},
                                                       {C::Rotation, "Rotation"},
                                                       {C::XTilt, "XTilt"},
                                                       {C::YTilt, "YTilt"},
                                                       {C::TangentialPressure, "TangentialPressure"},
                                                       {C::ZPosition, "ZPosition"}};
    QStringList names;
    for (const auto& [flag, name] : kNames)
        if (caps.testFlag(flag))
            names << QString::fromLatin1(name);
    return names.join(QLatin1Char(' '));
}

}

QString deviceTypeName(QInputDevice::DeviceType t) {
    using T = QInputDevice::DeviceType;
    switch (t) {
    case T::Mouse: return QStringLiteral("Mouse");
    case T::TouchScreen: return QStringLiteral("TouchScreen");
    case T::TouchPad: return QStringLiteral("TouchPad");
    case T::Puck: return QStringLiteral("Puck");
    case T::Stylus: return QStringLiteral("Stylus");
    case T::Airbrush: return QStringLiteral("Airbrush");
    case T::Keyboard: return QStringLiteral("Keyboard");
    default: return QStringLiteral("Unknown");
    }
}

QString pointerTypeName(QPointingDevice::PointerType t) {
    using P = QPointingDevice::PointerType;
    switch (t) {
    case P::Generic: return QStringLiteral("Generic");
    case P::Finger: return QStringLiteral("Finger");
    case P::Pen: return QStringLiteral("Pen");
    case P::Eraser: return QStringLiteral("Eraser");
    case P::Cursor: return QStringLiteral("Cursor");
    default: return QStringLiteral("Unknown");
    }
}

std::uint8_t buttonsFrom(Qt::MouseButtons b) {
    std::uint8_t m = ButtonNone;
    if (b & Qt::LeftButton)
        m |= ButtonTip;
    if (b & Qt::MiddleButton)
        m |= ButtonBarrel1;
    if (b & Qt::RightButton)
        m |= ButtonBarrel2;
    return m;
}

InputSample fromTablet(const QTabletEvent& e, const QTransform& widgetToDoc) {
    InputSample s;
    s.pos = widgetToDoc.map(e.position());
    s.pressure = e.pressure();
    // The lean in the document's directions, as the position is, so a turned or flipped view turns it too; its size kept.
    const QPointF tilt(e.xTilt(), e.yTilt());
    const QPointF turned = widgetToDoc.map(tilt) - widgetToDoc.map(QPointF());
    const double length = std::hypot(turned.x(), turned.y());
    const QPointF lean = length > 0.0 ? turned * (std::hypot(tilt.x(), tilt.y()) / length) : tilt;
    s.tiltX = lean.x();
    s.tiltY = lean.y();
    s.rotation = e.rotation();
    s.tangentialPressure = e.tangentialPressure();
    s.timestampMs = e.timestamp();
    s.buttons = buttonsFrom(e.buttons());
    s.kind = kindOf(e.pointingDevice());
    return s;
}

InputSample fromMouse(const QMouseEvent& e, const QTransform& widgetToDoc) {
    InputSample s;
    s.pos = widgetToDoc.map(e.position());
    s.pressure = (e.buttons() & Qt::LeftButton) ? 1.0 : 0.0;
    s.timestampMs = e.timestamp();
    s.buttons = buttonsFrom(e.buttons());
    s.kind = PointerKind::Mouse;
    return s;
}

InputDeviceInfo describe(const QPointingDevice* dev) {
    InputDeviceInfo info;
    if (!dev)
        return info;
    // Some back-ends (e.g. X11's core pointer) report names with trailing control characters.
    info.name = dev->name();
    info.name.removeIf([](QChar c) { return c.category() == QChar::Other_Control || c.isNull(); });
    info.name = info.name.trimmed();
    info.type = deviceTypeName(dev->type());
    info.pointerType = pointerTypeName(dev->pointerType());
    info.capabilities = flagNames(dev->capabilities());
    info.uniqueId = dev->uniqueId().numericId();
    info.buttonCount = dev->buttonCount();
    return info;
}

}
