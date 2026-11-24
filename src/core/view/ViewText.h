#pragma once

#include <QString>

namespace doodlebold::view_text {

// The text shown for a view's zoom and angle, rounded.
QString zoomText(double zoom);

QString angleText(double degrees);

QString angleListText(int degrees);

}
