#pragma once

#include <QObject>

class QApplication;

namespace doodlebold {

// The mouse wheel over a control that only lies under the pointer doesn't change it: over a closed select box (QComboBox), and
// over a spin box or slider without the keyboard's focus, the wheel goes on to what's around it, so a panel, page or list still
// scrolls (feedback, 2026-11-22: \"Dropdowns can stop mouse scrolling events even when they are not focused, but hovered
// on"). An open list, a focused spin box or slider, and scroll bars take the wheel as usual. The value bars (ValueBar,
// the JPEG quality bar) follow the same rule in their own wheel handlers.
class HoverWheelGuard : public QObject {
    Q_OBJECT
public:
    using QObject::QObject;

    // Once per application; again does nothing.
    static void install(QApplication& app);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
};

}
