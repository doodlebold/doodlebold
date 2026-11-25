#include "platform/Platform.h"

#include "ui/TabletInput.h"

#include <QAbstractNativeEventFilter>
#include <QCoreApplication>
#include <QGuiApplication>
#include <QKeyEvent>

#ifdef Q_OS_WIN
#include <windows.h>
#include <tpcshrd.h>
#endif

#include <QInputDevice>
#include <QPointingDevice>

namespace doodlebold::platform {

QString sessionDescription() {
    QString s = QGuiApplication::platformName();
    const QString desktop = qEnvironmentVariable("XDG_CURRENT_DESKTOP");
    if (!desktop.isEmpty())
        s += QStringLiteral(" (%1)").arg(desktop);
    return s;
}

#ifdef Q_OS_WIN
namespace {

class AltSpaceFilter : public QAbstractNativeEventFilter {
public:
    bool nativeEventFilter(const QByteArray& type, void* message, qintptr*) override {
        if (type != "windows_generic_MSG")
            return false;
        const MSG* msg = static_cast<const MSG*>(message);
        if ((msg->message != WM_SYSKEYDOWN && msg->message != WM_SYSKEYUP) || msg->wParam != VK_SPACE)
            return false;
        QObject* focus = QGuiApplication::focusObject();
        if (!focus)
            return false;
        // Bit 30 of the key message: the key was already down, so this is the keyboard's repeat.
        const bool repeat = msg->message == WM_SYSKEYDOWN && (msg->lParam & (1 << 30));
        QKeyEvent key(msg->message == WM_SYSKEYDOWN ? QEvent::KeyPress : QEvent::KeyRelease, Qt::Key_Space, Qt::AltModifier,
                      QStringLiteral(" "), repeat);
        QCoreApplication::sendEvent(focus, &key);
        return true;
    }
};

}

void routeAltSpaceToWidgets() {
    static AltSpaceFilter filter;
    QCoreApplication::instance()->installNativeEventFilter(&filter);
}

namespace {

// Each of these gestures can take a pen-down or pen-up for itself.
constexpr LRESULT kPenGesturesOff = TABLET_DISABLE_PRESSANDHOLD | TABLET_DISABLE_PENTAPFEEDBACK | TABLET_DISABLE_PENBARRELFEEDBACK |
                                    TABLET_DISABLE_FLICKS | TABLET_DISABLE_FLICKFALLBACKKEYS;
constexpr FEEDBACK_TYPE kPenFeedback[] = {FEEDBACK_PEN_PRESSANDHOLD, FEEDBACK_PEN_TAP, FEEDBACK_PEN_DOUBLETAP, FEEDBACK_PEN_RIGHTTAP,
                                          FEEDBACK_PEN_BARRELVISUALIZATION};

// Set again each time a pen comes over a window, as Qt can make a window's native handle anew.
void turnOffPenFeedback(HWND window) {
    const BOOL off = FALSE;
    for (const FEEDBACK_TYPE feedback : kPenFeedback)
        SetWindowFeedbackSetting(window, feedback, 0, sizeof(off), &off);
}

class PenGestureFilter : public QAbstractNativeEventFilter {
public:
    bool nativeEventFilter(const QByteArray& type, void* message, qintptr* result) override {
        if (type != "windows_generic_MSG")
            return false;
        const MSG* msg = static_cast<const MSG*>(message);
        if (msg->message == WM_POINTERENTER)
            turnOffPenFeedback(msg->hwnd);
        if (msg->message != WM_TABLET_QUERYSYSTEMGESTURESTATUS)
            return false;
        *result = kPenGesturesOff;
        return true;
    }
};

}

void turnOffPenGestures() {
    static PenGestureFilter filter;
    QCoreApplication::instance()->installNativeEventFilter(&filter);
}
#else
void routeAltSpaceToWidgets() {}
void turnOffPenGestures() {}
#endif

QSurfaceFormat defaultSurfaceFormat() {
    QSurfaceFormat fmt;
    fmt.setRenderableType(QSurfaceFormat::OpenGL);
    fmt.setVersion(3, 3);
    fmt.setProfile(QSurfaceFormat::CoreProfile);
    fmt.setSwapInterval(1);
    return fmt;
}

bool canDrawTheCanvas(const QSurfaceFormat& context) {
    const QSurfaceFormat wanted = defaultSurfaceFormat();
    return context.renderableType() != QSurfaceFormat::OpenGLES && context.version() >= wanted.version();
}

bool processorIsSupported() {
#ifdef _MSC_VER
    return IsProcessorFeaturePresent(PF_AVX2_INSTRUCTIONS_AVAILABLE) != 0;  // also false where the system doesn't save AVX registers
#else
    __builtin_cpu_init();
    return __builtin_cpu_supports("avx2");  // checks that the system saves AVX registers, too
#endif
}

QStringList inputDeviceReport() {
    QStringList lines;
    for (const QInputDevice* dev : QInputDevice::devices()) {
        QString line = QStringLiteral("%1 — %2").arg(dev->name(), TabletInput::deviceTypeName(dev->type()));
        if (const auto* p = qobject_cast<const QPointingDevice*>(dev))
            line += QStringLiteral(", pointer %1, %2 buttons").arg(TabletInput::pointerTypeName(p->pointerType())).arg(p->buttonCount());
        lines << line;
    }
    return lines;
}

bool padButtonsSupported() {
    // Qt 6 has no widget-level API for tablet pad buttons; pads at most show up in QInputDevice::devices().
    return false;
}

}
