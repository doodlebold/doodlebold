#include "ui/ValueBar.h"

#include "ui/Theme.h"

#include <QHelpEvent>
#include <QIcon>
#include <QKeyEvent>
#include <QLineEdit>
#include <QLocale>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QToolTip>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>

namespace doodlebold {

namespace {

using Theme::Token;

constexpr int kBarHeight = Theme::compact::kControlMd;
// The view slider (Look::Line), as the style sheet's QSlider: a 4 px track and a 12 px round thumb (component metrics).
constexpr int kTrackHeight = 12;  // component metric, described above
constexpr double kTrackLine = 4;
constexpr int kTextInset = Theme::kSpace1;
constexpr int kLockSize = Theme::kIconSm;
constexpr int kLimitGap = Theme::kSpace1;  // between the value struck through, the limit and the lock

}

ValueBar::ValueBar(QWidget* parent) : QWidget(parent) {
    setFocusPolicy(Qt::StrongFocus);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setCursor(Qt::ArrowCursor);
    // Right-click types a value; a menu from the bar or the panel behind it would only be in the way.
    setContextMenuPolicy(Qt::PreventContextMenu);
}

void ValueBar::setLook(Look look) {
    m_look = look;
    updateGeometry();
    update();
}

void ValueBar::setRange(double minimum, double maximum) {
    m_min = minimum;
    m_max = std::max(minimum, maximum);
    const double clamped = std::clamp(m_value, m_min, m_max);
    if (clamped != m_value)
        setValue(clamped);
    update();
}

void ValueBar::setDecimals(int decimals) {
    m_decimals = std::max(0, decimals);
    update();
}

void ValueBar::setSuffix(const QString& suffix) {
    m_suffix = suffix;
    update();
}

void ValueBar::setLabel(const QString& label) {
    m_label = label;
    update();
}

void ValueBar::setSingleStep(double step) {
    m_step = step;
}

void ValueBar::setSnap(double step) {
    m_snap = step;
}

void ValueBar::setMapping(std::function<double(double)> toPosition, std::function<double(double)> fromPosition) {
    m_toPosition = std::move(toPosition);
    m_fromPosition = std::move(fromPosition);
    update();
}

double ValueBar::rounded(double v) const {
    if (m_snap > 0)
        v = m_min + std::round((std::clamp(v, m_min, m_max) - m_min) / m_snap) * m_snap;
    const double scale = std::pow(10.0, m_decimals);
    return std::clamp(std::round(v * scale) / scale, m_min, m_max);
}

void ValueBar::setValue(double value) {
    const double v = rounded(value);
    if (v == m_value)
        return;
    m_value = v;
    update();
    emit valueChanged(v);
}

QString ValueBar::text() const {
    return QLocale::c().toString(m_value, 'f', m_decimals) + m_suffix;
}

void ValueBar::setLimit(std::optional<double> limit, const QString& why) {
    if (limit == m_limit && why == m_limitWhy)
        return;
    m_limit = limit;
    m_limitWhy = why;
    update();
}

bool ValueBar::event(QEvent* event) {
    if (event->type() == QEvent::ToolTip && showsLimit()) {
        auto* help = static_cast<QHelpEvent*>(event);
        if (lockArea().adjusted(-3, -3, 3, 3).contains(help->pos())) {
            QToolTip::showText(help->globalPos(), m_limitWhy, this, lockArea());
            return true;
        }
    }
    return QWidget::event(event);
}

QRect ValueBar::textArea() const {
    return m_look == Look::Bar ? rect().adjusted(kTextInset + Theme::kSpace2, 0, -kTextInset - Theme::kSpace2, 0)
                               : QRect(0, 0, width(), height() - kTrackHeight).adjusted(kTextInset / 2, 0, -kTextInset / 2, 0);
}

QRect ValueBar::lockArea() const {
    const QRect area = textArea();
    return QRect(area.right() - kLockSize + 1, area.center().y() - kLockSize / 2, kLockSize, kLockSize);
}

// The value at the right, or, kept to a limit, the value struck through, the limit and the lock.
void ValueBar::paintText(QPainter& p, const QRect& area, const QColor& colour) {
    p.setPen(colour);
    if (!showsLimit()) {
        p.drawText(area, Qt::AlignRight | Qt::AlignVCenter, text());
        return;
    }
    QIcon(QStringLiteral(":/icons/lock.svg")).paint(&p, lockArea());
    QRect rest = area.adjusted(0, 0, -kLockSize - kLimitGap, 0);
    const QString limit = QLocale::c().toString(*m_limit, 'f', m_decimals) + m_suffix;
    p.drawText(rest, Qt::AlignRight | Qt::AlignVCenter, limit);
    rest.setRight(rest.right() - p.fontMetrics().horizontalAdvance(limit) - kLimitGap);
    QFont struck = p.font();
    struck.setStrikeOut(true);
    p.save();
    p.setFont(struck);
    p.setPen(Theme::color(Token::TextMuted));
    p.drawText(rest, Qt::AlignRight | Qt::AlignVCenter, QLocale::c().toString(m_value, 'f', m_decimals));
    p.restore();
}

bool ValueBar::isEditing() const {
    return m_editor && m_editor->isVisible();
}

QSize ValueBar::sizeHint() const {
    return m_look == Look::Bar ? QSize(90, kBarHeight) : QSize(120, fontMetrics().height() + kTrackHeight + 2);
}

QSize ValueBar::minimumSizeHint() const {
    const int text = fontMetrics().horizontalAdvance(QLocale::c().toString(m_max, 'f', m_decimals) + m_suffix);
    const bool labelled = m_look == Look::Line && !m_label.isEmpty();
    const int label = labelled ? fontMetrics().horizontalAdvance(m_label) + kTextInset : 0;
    return QSize(text + label + 2 * kTextInset, sizeHint().height());
}

QRect ValueBar::track() const {
    if (m_look == Look::Bar)
        return rect().adjusted(1, 1, -1, -1);
    const int top = height() - kTrackHeight;
    return QRect(kTrackHeight / 2, top, width() - kTrackHeight, kTrackHeight);
}

double ValueBar::position() const {
    if (m_max <= m_min)
        return 0;
    const double p = m_toPosition ? m_toPosition(m_value) : (m_value - m_min) / (m_max - m_min);
    return std::clamp(p, 0.0, 1.0);
}

double ValueBar::valueAt(int x) const {
    const QRect t = track();
    const double p = std::clamp(double(x - t.left()) / std::max(1, t.width() - 1), 0.0, 1.0);
    return m_fromPosition ? m_fromPosition(p) : m_min + p * (m_max - m_min);
}

void ValueBar::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const bool enabled = isEnabled();
    const QColor text = Theme::color(enabled ? Token::Text : Token::TextDisabled);
    const QRect t = track();
    if (m_look == Look::Bar) {
        const double radius = std::min<double>(Theme::metric(Theme::Metric::Radius), height() / 2.0);
        const QRectF frame = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
        QPainterPath clip;
        clip.addRoundedRect(frame, radius, radius);
        p.fillPath(clip, Theme::color(Token::Base));
        p.save();
        p.setClipPath(clip);
        const QColor fill = Theme::color(enabled ? Token::Fill : Token::Border);
        p.fillRect(QRectF(t.left(), t.top(), t.width() * position(), t.height()), fill);
        p.restore();
        if (hasFocus()) {
            p.setPen(QPen(Theme::color(Token::FocusRing), Theme::kBorder));
            p.drawPath(clip);
        }
        p.setFont(Theme::font(Theme::TextRole::Label));
        paintText(p, textArea(), text);
        return;
    }
    const QRect line(0, 0, width(), height() - kTrackHeight);
    p.setPen(Theme::color(enabled ? Token::TextMuted : Token::TextDisabled));
    p.drawText(line.adjusted(kTextInset / 2, 0, -kTextInset / 2, 0), Qt::AlignLeft | Qt::AlignVCenter, m_label);
    paintText(p, textArea(), text);
    const double y = t.top() + t.height() / 2.0;
    const double x = t.left() + (t.width() - 1) * position();
    QPen groove(Theme::color(Token::Border), kTrackLine, Qt::SolidLine, Qt::RoundCap);
    p.setPen(groove);
    p.drawLine(QPointF(t.left(), y), QPointF(t.right(), y));
    groove.setColor(Theme::color(enabled ? Token::TextMuted : Token::BorderStrong));
    p.setPen(groove);
    if (x > t.left())
        p.drawLine(QPointF(t.left(), y), QPointF(x, y));
    const double r = kTrackHeight / 2.0 - 0.5;
    p.setPen(Qt::NoPen);
    p.setBrush(Theme::color(Token::Shadow));
    p.drawEllipse(QPointF(x, y + Theme::kBorder), r + 0.5, r + 0.5);
    p.setBrush(Theme::color(enabled ? (m_dragging ? Token::AccentHover : Token::Accent) : Token::BorderStrong));
    if (hasFocus())
        p.setPen(QPen(Theme::color(Token::FocusRing), Theme::kBorder));
    p.drawEllipse(QPointF(x, y), r, r);
}

void ValueBar::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::RightButton) {
        editValue();
        event->accept();
        return;
    }
    if (event->button() != Qt::LeftButton)
        return;
    m_dragging = true;
    emit dragStarted();
    setValue(valueAt(event->position().toPoint().x()));
    event->accept();
}

void ValueBar::mouseMoveEvent(QMouseEvent* event) {
    if (m_dragging)
        setValue(valueAt(event->position().toPoint().x()));
}

void ValueBar::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() != Qt::LeftButton || !m_dragging)
        return;
    m_dragging = false;
    emit dragFinished();
}

void ValueBar::step(double steps) {
    if (m_toPosition && m_fromPosition) {  // non-linear: one step moves 1 % along the field
        setValue(m_fromPosition(std::clamp(position() + steps * 0.01, 0.0, 1.0)));
        return;
    }
    setValue(m_value + steps * m_step);
}

void ValueBar::wheelEvent(QWheelEvent* event) {
    // Only a bar with the keyboard's focus steps: one the pointer merely lies over lets the panel scroll.
    if (!m_wheelSteps || !hasFocus()) {
        event->ignore();
        return;
    }
    const int notches = event->angleDelta().y() / 120;
    if (notches == 0)
        return;
    step(notches);
    event->accept();
}

void ValueBar::keyPressEvent(QKeyEvent* event) {
    switch (event->key()) {
    case Qt::Key_Right:
    case Qt::Key_Up: step(1); break;
    case Qt::Key_Left:
    case Qt::Key_Down: step(-1); break;
    case Qt::Key_PageUp: step(10); break;
    case Qt::Key_PageDown: step(-10); break;
    case Qt::Key_Home: setValue(m_min); break;
    case Qt::Key_End: setValue(m_max); break;
    case Qt::Key_F2:
    case Qt::Key_Return:
    case Qt::Key_Enter: editValue(); break;
    default: QWidget::keyPressEvent(event); return;
    }
    event->accept();
}

void ValueBar::editValue() {
    if (!isEnabled())
        return;
    if (!m_editor) {
        m_editor = new QLineEdit(this);
        m_editor->setObjectName(QStringLiteral("valueEditor"));
        m_editor->setAlignment(Qt::AlignRight);
        m_editor->setFrame(false);
        // Windows sends the context menu at the right button's release, by when this editor, opened at the press, is under the
        // pointer: its Undo/Copy/Paste menu would open over the value being typed.
        m_editor->setContextMenuPolicy(Qt::PreventContextMenu);
        m_editor->installEventFilter(this);
    }
    const QRect field = m_look == Look::Bar ? rect().adjusted(1, 1, -1, -1) : QRect(0, 0, width(), height() - kTrackHeight);
    m_editor->setGeometry(field);
    m_editor->setText(QLocale::c().toString(m_value, 'f', m_decimals));
    m_editor->show();
    m_editor->selectAll();
    m_editor->setFocus();
}

void ValueBar::closeEditor(bool apply) {
    if (!isEditing())
        return;
    // "43" and "43%" (the suffix as shown) both mean 43.
    QString typed = m_editor->text().trimmed();
    if (const QString suffix = m_suffix.trimmed(); !suffix.isEmpty())
        typed.remove(suffix);
    typed = typed.trimmed();
    m_editor->hide();
    setFocus();
    if (!apply)
        return;
    bool ok = false;
    const double v = QLocale::c().toDouble(QString(typed).replace(QLatin1Char(','), QLatin1Char('.')), &ok);
    if (ok)
        setValue(v);
}

bool ValueBar::eventFilter(QObject* watched, QEvent* event) {
    if (watched == m_editor) {
        if (event->type() == QEvent::KeyPress) {
            // Handled here: an ignored Enter would reach the bar, whose Enter opens the editor again.
            const int key = static_cast<QKeyEvent*>(event)->key();
            if (key == Qt::Key_Escape || key == Qt::Key_Return || key == Qt::Key_Enter) {
                closeEditor(key != Qt::Key_Escape);
                return true;
            }
        }
        if (event->type() == QEvent::FocusOut)
            closeEditor(true);  // clicking elsewhere keeps what was typed, as a spin box does
    }
    return QWidget::eventFilter(watched, event);
}

}
