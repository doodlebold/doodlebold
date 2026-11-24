#include "ui/HoverWheelGuard.h"

#include <QAbstractSpinBox>
#include <QApplication>
#include <QComboBox>
#include <QSlider>
#include <QWheelEvent>

namespace doodlebold {

namespace {

// Whether the wheel over `widget` should pass it on: a closed select box always; a spin box or slider only while it hasn't the
// keyboard's focus (the user clicked into it, so the wheel is meant for it).
bool passesTheWheelOn(const QWidget* widget) {
    if (qobject_cast<const QComboBox*>(widget))
        return true;
    if (qobject_cast<const QAbstractSpinBox*>(widget) || qobject_cast<const QSlider*>(widget))
        return !widget->hasFocus();
    return false;
}

}

void HoverWheelGuard::install(QApplication& app) {
    if (!app.findChild<HoverWheelGuard*>(QString(), Qt::FindDirectChildrenOnly))
        app.installEventFilter(new HoverWheelGuard(&app));
}

// The wheel goes to the control's parent, then on up until one takes it (a scroll area does). Qt 6 doesn't pass a wheel event
// it was handed this way on to the parents by itself, so a select box in a plain row of a scrolled panel stopped the scrolling.
bool HoverWheelGuard::eventFilter(QObject* watched, QEvent* event) {
    if (event->type() != QEvent::Wheel)
        return false;
    auto* control = qobject_cast<QWidget*>(watched);
    if (!control || !passesTheWheelOn(control))
        return false;
    const auto* wheel = static_cast<QWheelEvent*>(event);
    for (QWidget* w = control->parentWidget(); w; w = w->isWindow() ? nullptr : w->parentWidget()) {
        if (passesTheWheelOn(w))
            continue;
        QWheelEvent passed(w->mapFromGlobal(wheel->globalPosition()), wheel->globalPosition(), wheel->pixelDelta(), wheel->angleDelta(),
                           wheel->buttons(), wheel->modifiers(), wheel->phase(), wheel->inverted(), wheel->source());
        QCoreApplication::sendEvent(w, &passed);
        if (passed.isAccepted())
            break;
    }
    return true;
}

}
