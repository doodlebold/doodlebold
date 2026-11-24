#pragma once

#include "core/input/InputSample.h"
#include "core/input/TabletStats.h"

#include <QInputDevice>
#include <QPointingDevice>
#include <QTransform>

class QMouseEvent;
class QTabletEvent;

// Uses only the cross-platform Qt 6 pointer API (QTabletEvent / QPointingDevice): Wayland tablet-v2, Windows Ink, macOS
// NSEvent.
namespace doodlebold::TabletInput {

InputSample fromTablet(const QTabletEvent& e, const QTransform& widgetToDoc);
InputSample fromMouse(const QMouseEvent& e, const QTransform& widgetToDoc);
InputDeviceInfo describe(const QPointingDevice* device);
QString deviceTypeName(QInputDevice::DeviceType type);
QString pointerTypeName(QPointingDevice::PointerType type);

std::uint8_t buttonsFrom(Qt::MouseButtons buttons);

}
