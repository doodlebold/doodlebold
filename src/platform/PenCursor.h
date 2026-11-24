#pragma once

#include <Qt>

// The tablet pen's cursor, set by Doodlebold itself where Qt cannot on Wayland.
namespace doodlebold::platform::pen_cursor {

bool available();
// Qt::BlankCursor hides the pen cursor (the canvas draws its own); other shapes show by name (cursor-shape-v1).
void set(Qt::CursorShape shape);

}
