#include "ui/panels/ControlTips.h"

#include "ui/Components.h"

#include <QWidget>

namespace doodlebold::control_tips {

QString html(const QString& name, const QString& explanation) {
    return components::tipHtml(name, {}, explanation);
}

void set(std::initializer_list<QWidget*> widgets, const QString& name, const QString& explanation) {
    const QString tip = html(name, explanation);
    for (QWidget* w : widgets)
        if (w)
            w->setToolTip(tip);
}

}
