#pragma once

#include <QString>
#include <QStringList>
#include <QSurfaceFormat>

namespace doodlebold::platform {

QString sessionDescription();
QStringList inputDeviceReport();
// Desktop OpenGL 3.3 core, vsync. The renderable type must be explicit: Qt's Wayland EGL default picks an ES config
// and NVIDIA then rejects a desktop core context with EGL_BAD_MATCH.
QSurfaceFormat defaultSurfaceFormat();
// On Windows Qt keeps Alt+Space for the window's system menu, so the canvas never sees it: this hands it to the focused widget.
void routeAltSpaceToWidgets();
// Whether a context of this format can run the canvas's shaders, which are GLSL 3.30.
bool canDrawTheCanvas(const QSurfaceFormat& context);
// Whether this processor has AVX2, which the painting code is built with (src/CMakeLists.txt): without it the first stroke would crash.
bool processorIsSupported();
bool padButtonsSupported();

}
