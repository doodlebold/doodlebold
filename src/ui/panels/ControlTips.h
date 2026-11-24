#pragma once

#include <QString>

#include <initializer_list>

class QWidget;

namespace doodlebold::control_tips {

// What a tool panel's control does, in plain words: its name, and under it a sentence or two that
// wraps rather than running across the screen.
QString html(const QString& name, const QString& explanation);

// Gives the control that tip, and its label too where it has one, so the tip shows over either. A button set here should not also
// have a button_tips tip, which would write over it.
void set(std::initializer_list<QWidget*> widgets, const QString& name, const QString& explanation);

}
