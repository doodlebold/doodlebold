#include "ui/GlassPopups.h"

#include "platform/BackgroundBlur.h"
#include "ui/Theme.h"

#include <QApplication>
#include <QDialog>
#include <QEvent>
#include <QMenu>
#include <QPainter>
#include <QPainterPath>
#include <QPointer>
#include <QStyle>
#include <QStyleOption>
#include <QTimer>
#include <QWindow>

namespace doodlebold {

namespace {

constexpr char kGlass[] = "glass";

bool s_frosted = true;
int s_installed = 0;  // GlassPopups filters alive: without one the desktop can't blur, and nothing is made glass

// The corner radius the style sheet gives the popup (QMenu: @radius, QToolTip: @radiusSmall); a dialog is a window of
// the desktop's, which rounds its corners itself.
int radiusOf(const QWidget* popup) {
    if (qobject_cast<const QDialog*>(popup))
        return 0;
    return Theme::metric(qobject_cast<const QMenu*>(popup) ? Theme::Metric::Radius : Theme::Metric::RadiusSmall);
}

// Windows can hand a new dialog's window a size of its own while Qt is still making it, and the dialog's layout polishes the
// dialog on that resize: destroying the window there crashes Qt, which goes on using it. Qt gives a window its backing store
// once it is made.
bool windowBeingMade(const QWidget* popup) {
    const QWindow* window = popup->windowHandle();
    return window && window->handle() && !popup->backingStore();
}

}

GlassPopups::GlassPopups(Blur blur, QObject* parent) : QObject(parent), m_blur(std::move(blur)) {
    ++s_installed;
}

GlassPopups::~GlassPopups() {
    --s_installed;
}

bool GlassPopups::frosted() {
    return s_frosted;
}

void GlassPopups::setFrosted(bool on) {
    if (on == s_frosted)
        return;
    s_frosted = on;
    if (s_installed == 0)
        return;
    for (QWidget* w : QApplication::allWidgets())
        if (isGlassKind(w) && !w->isVisible() && w->testAttribute(Qt::WA_WState_Polished))
            follow(w);
}

// A popup made glass, or plain, as the option now says. Only while it's hidden: its window is made again.
void GlassPopups::follow(QWidget* popup) {
    const bool glass = popup->property(kGlass).toBool();
    if (s_frosted && !glass)
        makeGlass(popup);
    else if (!s_frosted && glass)
        makePlain(popup);
}

bool GlassPopups::install(QApplication& app) {
    static bool installed = false;
    if (installed)
        return true;
    if (!platform::background_blur::available())
        return false;
    app.installEventFilter(new GlassPopups(&platform::background_blur::setRegion, &app));
    installed = true;
    return true;
}

bool GlassPopups::isGlassKind(const QWidget* widget) {
    return widget && (qobject_cast<const QMenu*>(widget) || qobject_cast<const QDialog*>(widget) ||
                      widget->objectName() == QLatin1String("qtooltip_label"));
}

QRegion GlassPopups::shape(const QSize& size, int radius) {
    QPainterPath path;
    path.addRoundedRect(QRectF(QPointF(0, 0), QSizeF(size)), radius, radius);
    return QRegion(path.toFillPolygon().toPolygon());
}

bool GlassPopups::eventFilter(QObject* watched, QEvent* event) {
    if (event->type() == QEvent::Paint) {
        // A translucent window is not painted its style sheet's background by Qt (menus and tool tips paint their own).
        auto* dialog = qobject_cast<QDialog*>(watched);
        if (dialog && dialog->property(kGlass).toBool()) {
            QPainter painter(dialog);
            QStyleOption option;
            option.initFrom(dialog);
            dialog->style()->drawPrimitive(QStyle::PE_Widget, &option, &painter, dialog);
        }
        return false;
    }
    switch (event->type()) {
    case QEvent::Polish:
    case QEvent::Show:
    case QEvent::Resize:
    case QEvent::Hide: break;
    default: return false;
    }
    auto* popup = qobject_cast<QWidget*>(watched);
    if (!isGlassKind(popup))
        return false;
    if (event->type() == QEvent::Polish) {
        // A popup polished while its window is being made stays plain this time, and turns glass once hidden.
        if (s_frosted && !popup->property(kGlass).toBool() && !windowBeingMade(popup))
            makeGlass(popup);
    } else if (event->type() == QEvent::Hide) {
        // A popup that was on screen when the option changed follows it once it's closed.
        if (popup->property(kGlass).toBool() != s_frosted)
            QTimer::singleShot(0, popup, [p = QPointer<QWidget>(popup)] {
                if (p && !p->isVisible())
                    follow(p);
            });
    } else if (popup->property(kGlass).toBool() && popup->isVisible()) {
        // The window exists by now and is not on screen yet.
        if (event->type() == QEvent::Show)
            platform::background_blur::prepare(popup->windowHandle(), shape(popup->size(), radiusOf(popup)));
        // After the show has run its course: the window and its surface exist then.
        QTimer::singleShot(0, popup, [this, p = QPointer<QWidget>(popup)] {
            if (p && p->isVisible())
                blurBehind(p);
        });
    }
    return false;
}

void GlassPopups::makeGlass(QWidget* popup) {
    // The window needs an alpha channel, which is chosen when it is created; Qt's menus and tool tips are polished
    // (for their size hints) before that.
    popup->setAttribute(Qt::WA_TranslucentBackground);
    // A window made before this (a dialog whose window was asked for early, as on Wayland) has no alpha channel: it is made
    // again, with one, when it is next shown.
    if (QWindow* window = popup->windowHandle();
        window && window->handle() && !popup->isVisible() && window->format().alphaBufferSize() <= 0)
        window->destroy();
    popup->setProperty(kGlass, true);
    popup->style()->unpolish(popup);
    popup->style()->polish(popup);
}

void GlassPopups::makePlain(QWidget* popup) {
    popup->setAttribute(Qt::WA_TranslucentBackground, false);
    if (QWindow* window = popup->windowHandle(); window && window->handle() && !popup->isVisible())
        window->destroy();
    popup->setProperty(kGlass, false);
    popup->style()->unpolish(popup);
    popup->style()->polish(popup);
}

void GlassPopups::blurBehind(QWidget* popup) {
    if (m_blur(popup->windowHandle(), shape(popup->size(), radiusOf(popup))))
        popup->update();  // the blur region takes effect with the window's next frame
}

}
