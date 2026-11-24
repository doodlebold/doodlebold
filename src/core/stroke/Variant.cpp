#include "core/stroke/Variant.h"

namespace doodlebold::variant {

namespace {
QString& chosen() {
    static QString name;
    return name;
}
}

void set(const QString& name) {
    chosen() = name;
}

const QString& current() {
    return chosen();
}

}
