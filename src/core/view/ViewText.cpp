#include "core/view/ViewText.h"

#include <cmath>

namespace doodlebold::view_text {

namespace {

QString rounded(double value, int decimals) {
    QString s = QString::number(value, 'f', decimals);
    if (decimals > 0) {
        while (s.endsWith(QLatin1Char('0')))
            s.chop(1);
        if (s.endsWith(QLatin1Char('.')))
            s.chop(1);
    }
    return s;
}

}

QString zoomText(double zoom) {
    const double percent = zoom * 100.0;
    const int decimals = percent >= 100.0 ? 0 : percent >= 10.0 ? 1 : 2;
    return rounded(percent, decimals) + QLatin1Char('%');
}

QString angleText(double degrees) {
    if (degrees <= -180.0)
        degrees += 360.0;  // −180 and 180 are the same view; 180 is shown
    const double shown = std::round(degrees * 10.0) / 10.0;
    return QString::number(shown == 0.0 ? 0.0 : shown, 'f', 1) + QStringLiteral("°");
}

QString angleListText(int degrees) {
    return QString::number(degrees) + QStringLiteral("°");
}

}
