#include "ui/Toast.h"

#include "ui/DoodleboldStyle.h"
#include "ui/Theme.h"

#include <QEvent>
#include <QFontMetrics>
#include <QPainter>
#include <QPainterPath>

#include <algorithm>
#include <cmath>
#include <numbers>
#include <utility>

namespace doodlebold {

namespace {

constexpr int kFadeMs = Theme::kMotionBase;
constexpr int kMoveMs = Theme::kMotionFast;
constexpr int kShakeMs = Theme::kMotionSlow;
constexpr int kShakeWidth = 6;    // px to each side, dying away (component metric)
constexpr int kShakes = 3;
constexpr QPoint kEnterFrom(Theme::kSpace5, -Theme::kSpace3);  // it comes from the top right
// One card gap and one padding below a canvas window's title bar.
constexpr int kFromTop = DoodleboldStyle::kTitleBarHeight + Theme::kSpace1 + Theme::kSpace4;
constexpr int kMaxWidth = 420;    // px: about 60 characters a line (component metric)
constexpr int kMaxShown = 5;      // an older one leaves for a sixth
constexpr int kAccentWidth = 5;   // px: the danger stripe at its left edge (component metric)
constexpr int kAlpha = Theme::kFloatingAlpha;

int padding() {
    return Theme::metric(Theme::Metric::DialogPadding);
}

}

Toast::Toast(QWidget* window, QString text) : QWidget(window), m_text(std::move(text)) {
    setObjectName(QStringLiteral("errorToast"));
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setFocusPolicy(Qt::NoFocus);
    m_hold.setSingleShot(true);
    connect(&m_hold, &QTimer::timeout, this, &Toast::leave);
    m_fade.setDuration(kFadeMs);
    m_fade.setEasingCurve(QEasingCurve::OutCubic);
    connect(&m_fade, &QVariantAnimation::valueChanged, this, [this](const QVariant& value) {
        m_opacity = value.toDouble();
        applyPosition();
        update();
    });
    connect(&m_fade, &QVariantAnimation::finished, this, [this] {
        if (m_leaving)
            emit gone();
    });
    m_move.setDuration(kMoveMs);
    m_move.setEasingCurve(QEasingCurve::OutCubic);
    connect(&m_move, &QVariantAnimation::valueChanged, this, [this](const QVariant& value) {
        m_rest = value.toPoint();
        applyPosition();
    });
    m_shake.setDuration(kShakeMs);
    m_shake.setStartValue(0.0);
    m_shake.setEndValue(1.0);
    connect(&m_shake, &QVariantAnimation::valueChanged, this, [this](const QVariant& value) {
        const double t = value.toDouble();
        m_shakeOffset = static_cast<int>(std::lround(kShakeWidth * (1 - t) * std::sin(t * kShakes * 2 * std::numbers::pi)));
        applyPosition();
    });
    connect(&m_shake, &QVariantAnimation::finished, this, [this] {
        m_shakeOffset = 0;
        applyPosition();
    });
}

void Toast::setMaxWidth(int width) {
    const int inner = std::max(1, std::min(kMaxWidth, width) - 2 * padding());
    const QRect box = QFontMetrics(font()).boundingRect(QRect(0, 0, inner, 10000), Qt::TextWordWrap | Qt::AlignLeft, m_text);
    resize(box.width() + 2 * padding(), box.height() + 2 * padding());
}

void Toast::placeAt(const QPoint& rest, bool animated) {
    m_move.stop();
    if (!animated) {
        m_rest = rest;
        applyPosition();
        return;
    }
    m_move.setStartValue(m_rest);
    m_move.setEndValue(rest);
    m_move.start();
}

void Toast::appear(const QPoint& rest, int holdMs) {
    placeAt(rest, false);
    raise();
    show();
    fadeTo(1);
    m_hold.start(holdMs);
}

void Toast::repeat(int holdMs) {
    m_leaving = false;
    raise();
    fadeTo(1);
    m_hold.start(holdMs);
    m_shake.stop();
    m_shake.start();
}

void Toast::leave() {
    m_hold.stop();
    m_leaving = true;
    fadeTo(0);
}

void Toast::fadeTo(double opacity) {
    m_fade.stop();
    m_fade.setStartValue(m_opacity);
    m_fade.setEndValue(opacity);
    m_fade.start();
}

void Toast::applyPosition() {
    move(m_rest + kEnterFrom * (1 - m_opacity) + QPoint(m_shakeOffset, 0));
}

void Toast::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setOpacity(m_opacity);
    const double radius = Theme::metric(Theme::Metric::Radius);
    QPainterPath shape;
    shape.addRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), radius, radius);
    QColor fill = Theme::color(Theme::Token::Surface);
    fill.setAlpha(kAlpha);
    p.fillPath(shape, fill);
    p.setClipPath(shape);
    p.fillRect(0, 0, kAccentWidth, height(), Theme::color(Theme::Token::Danger));
    p.setClipping(false);
    p.setPen(Theme::color(Theme::Token::Border));
    p.drawPath(shape);
    p.setPen(Theme::color(Theme::Token::Text));
    p.drawText(rect().adjusted(padding(), padding(), -padding(), -padding()), Qt::TextWordWrap | Qt::AlignLeft | Qt::AlignTop, m_text);
}

ToastStack::ToastStack(QWidget* window, QWidget* anchor) : QObject(window), m_window(window), m_anchor(anchor) {
    anchor->installEventFilter(this);
}

int ToastStack::holdTimeFor(const QString& text) const {
    return m_holdOverride >= 0 ? m_holdOverride : std::clamp(2500 + 40 * int(text.size()), 3000, 7000);
}

void ToastStack::showMessage(const QString& text) {
    const auto same = std::ranges::find_if(m_toasts, [&text](const Toast* t) { return t->text() == text; });
    if (same != m_toasts.end()) {
        (*same)->repeat(holdTimeFor(text));
        return;
    }
    int staying = 0;
    for (Toast* toast : std::as_const(m_toasts))
        staying += toast->isLeaving() ? 0 : 1;
    for (Toast* toast : std::as_const(m_toasts)) {
        if (staying < kMaxShown)
            break;
        if (!toast->isLeaving()) {
            toast->leave();
            --staying;
        }
    }

    auto* toast = new Toast(m_window, text);
    toast->setMaxWidth(m_anchor->width() - 2 * (padding() + Theme::metric(Theme::Metric::CardGap)));
    connect(toast, &Toast::gone, this, [this, toast] {
        m_toasts.removeOne(toast);
        toast->deleteLater();
        arrange(true);
    });
    m_toasts.append(toast);
    toast->appear(restOf(static_cast<int>(m_toasts.size()) - 1), holdTimeFor(text));
}

QPoint ToastStack::restOf(int index) const {
    // The canvas card sits one CardGap inside the anchor. Right edges line up.
    const int right = m_anchor->width() - Theme::metric(Theme::Metric::CardGap) - padding();
    const QPoint corner = m_anchor->mapTo(m_window, QPoint(right, kFromTop));
    int y = corner.y();
    for (int i = 0; i < index; ++i)
        y += m_toasts.at(i)->height() + Theme::metric(Theme::Metric::FieldSpacing);
    return {corner.x() - m_toasts.at(index)->width(), y};
}

void ToastStack::arrange(bool animated) {
    for (int i = 0; i < m_toasts.size(); ++i)
        m_toasts.at(i)->placeAt(restOf(i), animated);
}

bool ToastStack::eventFilter(QObject* watched, QEvent* event) {
    if (watched == m_anchor && !m_toasts.isEmpty() && (event->type() == QEvent::Resize || event->type() == QEvent::Move)) {
        for (Toast* toast : std::as_const(m_toasts))
            toast->setMaxWidth(m_anchor->width() - 2 * (padding() + Theme::metric(Theme::Metric::CardGap)));
        arrange(false);
    }
    return QObject::eventFilter(watched, event);
}

}
