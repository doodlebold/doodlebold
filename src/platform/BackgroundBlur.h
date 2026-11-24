#pragma once

#include <QRegion>

class QWindow;

// Blur behind translucent windows: ext-background-effect-v1 on Wayland, acrylic on Windows 11 22H2 and newer. available() is
// false everywhere else.
namespace doodlebold::platform::background_blur {

bool available();
// Blur behind `region` of `window` (window coordinates, dip); an empty region removes it. The window must be shown.
bool setRegion(QWindow* window, const QRegion& region);
// The same for a window about to be shown, for the systems that take it then; a no-op on the others.
void prepare(QWindow* window, const QRegion& region);
void release(QWindow* window);

}
