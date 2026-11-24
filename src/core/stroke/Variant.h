#pragma once

#include <QString>
#include <QStringList>

namespace doodlebold::variant {

// The candidate a comparison round is rendering, by its name, for the headless replay tool only. The app never sets it, so it always
// paints the tool's own behaviour, which is the empty name. A tool reads it while a round has more than one candidate for it.
void set(const QString& name);
const QString& current();
// Whether the name, or one of its parts between hyphens, is `part`: "blurred-tight" has "blurred" and "tight".
inline bool has(const char* part) {
    return current().split(QLatin1Char('-')).contains(QLatin1String(part));
}

// The number after a part that starts with `part`, or `otherwise`: "firm0.5-ahead0.3" has 0.5 for "firm".
inline double number(const char* part, double otherwise) {
    for (const QString& piece : current().split(QLatin1Char('-'))) {
        bool ok = false;
        const double value = piece.startsWith(QLatin1String(part)) ? piece.mid(int(qstrlen(part))).toDouble(&ok) : 0.0;
        if (ok)
            return value;
    }
    return otherwise;
}

}
