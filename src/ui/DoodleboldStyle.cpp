#include "ui/DoodleboldStyle.h"

#include "ui/Theme.h"

#include <QAbstractSpinBox>
#include <QDialog>
#include <QPainter>
#include <QPainterPath>
#include <QStyleOption>

#include <algorithm>
#include <tuple>
#include <vector>

namespace doodlebold {

namespace {

using Theme::Token;

std::vector<QStyle::SubControl> buttonsFromRight(const QStyleOptionTitleBar& bar) {
    const Qt::WindowFlags flags = bar.titleBarFlags;
    const bool minimized = bar.titleBarState & Qt::WindowMinimized;
    const bool maximized = bar.titleBarState & Qt::WindowMaximized;
    std::vector<QStyle::SubControl> out;
    if (flags & Qt::WindowSystemMenuHint)
        out.push_back(QStyle::SC_TitleBarCloseButton);
    if (flags & Qt::WindowMaximizeButtonHint)
        out.push_back(maximized ? QStyle::SC_TitleBarNormalButton : QStyle::SC_TitleBarMaxButton);
    if (flags & Qt::WindowMinimizeButtonHint)
        out.push_back(minimized ? QStyle::SC_TitleBarNormalButton : QStyle::SC_TitleBarMinButton);
    return out;
}

QRect headerRect(const QRect& bar) {
    const int m = DoodleboldStyle::kFrameMargin;
    return bar.adjusted(m, m, -m, 0);
}

QRect buttonRect(const QRect& bar, int slot) {
    const QRect header = headerRect(bar);
    const int right = header.right() - DoodleboldStyle::kButtonGap - slot * (DoodleboldStyle::kButtonSize + DoodleboldStyle::kButtonGap);
    return QRect(right - DoodleboldStyle::kButtonSize + 1, header.center().y() - DoodleboldStyle::kButtonSize / 2 + 1,
                 DoodleboldStyle::kButtonSize, DoodleboldStyle::kButtonSize);
}

void drawButton(QPainter* p, const QRect& r, QStyle::SubControl which, bool hover, bool pressed, bool active) {
    p->save();
    p->setRenderHint(QPainter::Antialiasing);
    p->setPen(Qt::NoPen);
    if (pressed || hover) {
        p->setBrush(Theme::color(pressed ? Token::Pressed : Token::Hover));
        p->drawEllipse(QRectF(r).adjusted(0.5, 0.5, -0.5, -0.5));
    }
    QPen pen(Theme::color(active || hover ? Token::Text : Token::TextMuted), Theme::kGlyphStroke);
    pen.setCapStyle(Qt::RoundCap);
    p->setPen(pen);
    p->setBrush(Qt::NoBrush);
    const QPointF c = QRectF(r).center();
    constexpr double k = 4.0;
    switch (which) {
    case QStyle::SC_TitleBarCloseButton:
        p->drawLine(c + QPointF(-k, -k), c + QPointF(k, k));
        p->drawLine(c + QPointF(-k, k), c + QPointF(k, -k));
        break;
    case QStyle::SC_TitleBarMaxButton: p->drawRect(QRectF(c.x() - k, c.y() - k, 2 * k, 2 * k)); break;
    case QStyle::SC_TitleBarNormalButton:
        p->drawRect(QRectF(c.x() - k, c.y() - k + 2, 2 * k - 2, 2 * k - 2));
        p->drawPolyline(QPolygonF({c + QPointF(-k + 2, -k + 2 - 2), c + QPointF(-k + 2, -k), c + QPointF(k, -k), c + QPointF(k, k - 2),
                                   c + QPointF(k - 2, k - 2)}));
        break;
    case QStyle::SC_TitleBarMinButton: p->drawLine(c + QPointF(-k, 0), c + QPointF(k, 0)); break;
    default: break;
    }
    p->restore();
}

}

int DoodleboldStyle::styleHint(StyleHint hint, const QStyleOption* option, const QWidget* widget, QStyleHintReturn* data) const {
    // Alt is the eyedropper while it is held: letting it go must not put the keyboard in the menu bar.
    if (hint == SH_MenuBar_AltKeyNavigation)
        return 0;
    if (hint == SH_WindowFrame_Mask)
        return 0;  // square corners: Fusion clips two pixels off the title bar's corners, which the outline does not follow
    if (hint == SH_DialogButtonBox_ButtonsHaveIcons)
        return 0;
    if (hint == SH_Workspace_FillSpaceOnMaximize)
        return 1;  // a maximized canvas keeps its header bar: the menu bar's right corner holds the quick bar
    return QProxyStyle::styleHint(hint, option, widget, data);
}

int DoodleboldStyle::pixelMetric(PixelMetric metric, const QStyleOption* option, const QWidget* widget) const {
    switch (metric) {
    case PM_TitleBarHeight: return kTitleBarHeight;
    case PM_MdiSubWindowFrameWidth: return kFrameWidth;
    case PM_TitleBarButtonSize: return kButtonSize;
    case PM_LayoutHorizontalSpacing:
    case PM_LayoutVerticalSpacing:
        if (widget && qobject_cast<const QDialog*>(widget->window()))
            return Theme::metric(metric == PM_LayoutVerticalSpacing ? Theme::Metric::FieldSpacing : Theme::Metric::FieldGap);
        return QProxyStyle::pixelMetric(metric, option, widget);
    case PM_LayoutLeftMargin:
    case PM_LayoutTopMargin:
    case PM_LayoutRightMargin:
    case PM_LayoutBottomMargin:
        if (qobject_cast<const QDialog*>(widget))
            return Theme::metric(Theme::Metric::DialogPadding);
        return QProxyStyle::pixelMetric(metric, option, widget);
    case PM_IndicatorWidth:
    case PM_IndicatorHeight:
    case PM_ExclusiveIndicatorWidth:
    case PM_ExclusiveIndicatorHeight: return kIndicatorSize;
    default: return QProxyStyle::pixelMetric(metric, option, widget);
    }
}

QSize DoodleboldStyle::sizeFromContents(ContentsType type, const QStyleOption* option, const QSize& contents, const QWidget* widget) const {
    QSize size = QProxyStyle::sizeFromContents(type, option, contents, widget);
    if (type == CT_ComboBox || type == CT_SpinBox) {
        // The density's control height: a dialog's fields are comfortable, everything else is compact.
        const bool inDialog = widget && qobject_cast<const QDialog*>(widget->window());
        size.setHeight(std::max(size.height(), inDialog ? Theme::comfortable::kControlMd : Theme::compact::kControlMd));
    }
    return size;
}

QRect DoodleboldStyle::subControlRect(ComplexControl control, const QStyleOptionComplex* option, SubControl sub,
                                      const QWidget* widget) const {
    const auto* bar = qstyleoption_cast<const QStyleOptionTitleBar*>(option);
    if (control != CC_TitleBar || !bar)
        return QProxyStyle::subControlRect(control, option, sub, widget);
    const std::vector<SubControl> buttons = buttonsFromRight(*bar);
    switch (sub) {
    case SC_TitleBarLabel: {
        int reserve = int(buttons.size()) * (kButtonSize + kButtonGap) + kButtonGap;
        if (widget)
            reserve = std::max(reserve, widget->property(kHeaderInsetProperty).toInt());
        return headerRect(bar->rect).adjusted(reserve, 0, -reserve, 0);
    }
    case SC_TitleBarSysMenu: return {};
    default:
        for (std::size_t i = 0; i < buttons.size(); ++i)
            if (buttons[i] == sub)
                return buttonRect(bar->rect, int(i));
        return {};
    }
}

void DoodleboldStyle::drawComplexControl(ComplexControl control, const QStyleOptionComplex* option, QPainter* painter,
                                         const QWidget* widget) const {
    if (control == CC_TitleBar) {
        if (const auto* bar = qstyleoption_cast<const QStyleOptionTitleBar*>(option)) {
            const bool active = bar->state & State_Active;
            // The bar covers the top of the window's frame: the workspace in the frame's margin, then the header as the
            // top of a card and a line under it. The same quiet edge for the active canvas as for the others (no blue
            // outline); its title is brighter.
            const QRect h = headerRect(bar->rect);  // the margin is left unpainted: what is behind shows through
            const double radius = Theme::metric(Theme::Metric::CardRadius);
            QPainterPath card;
            card.addRoundedRect(QRectF(h).adjusted(0.5, 0.5, -0.5, 4 * radius), radius, radius);
            painter->save();
            painter->setClipRect(bar->rect);
            painter->setRenderHint(QPainter::Antialiasing);
            painter->setPen(Theme::color(Token::Border));
            painter->setBrush(Theme::color(Token::Surface));
            painter->drawPath(card);
            painter->restore();
            painter->fillRect(QRect(h.left(), h.bottom(), h.width(), 1), Theme::color(Token::Border));
            const QRect label = subControlRect(CC_TitleBar, bar, SC_TitleBarLabel, widget);
            painter->setPen(Theme::color(active ? Token::Text : Token::TextMuted));
            painter->setFont(widget ? widget->font() : painter->font());
            painter->drawText(label, Qt::AlignCenter, painter->fontMetrics().elidedText(bar->text, Qt::ElideMiddle, label.width()));
            for (const SubControl b : buttonsFromRight(*bar))
                drawButton(painter, subControlRect(CC_TitleBar, bar, b, widget), b,
                           (bar->activeSubControls & b) && (bar->state & State_MouseOver),
                           (bar->activeSubControls & b) && (bar->state & State_Sunken), active);
            return;
        }
    }
    if (control == CC_ComboBox) {
        if (const auto* box = qstyleoption_cast<const QStyleOptionComboBox*>(option)) {
            const bool enabled = box->state & State_Enabled;
            const bool hover = enabled && (box->state & State_MouseOver);
            const bool focus = enabled && (box->state & State_HasFocus);
            painter->save();
            painter->setRenderHint(QPainter::Antialiasing);
            const QRectF r = QRectF(box->rect).adjusted(0.5, 0.5, -0.5, -0.5);
            const double radius = std::min<double>(Theme::metric(Theme::Metric::Radius), r.height() / 2.0);
            painter->setPen(focus ? QPen(Theme::color(Token::FocusRing), Theme::kBorder) : QPen(Qt::NoPen));
            painter->setBrush(Theme::color(!enabled ? Token::AlternateBase : hover ? Token::Hover : Token::Base));
            painter->drawRoundedRect(r, radius, radius);
            if (box->subControls & SC_ComboBoxArrow) {
                const QPointF c = QRectF(subControlRect(CC_ComboBox, box, SC_ComboBoxArrow, widget)).center();
                QPen chevron(Theme::color(enabled ? Token::TextMuted : Token::TextDisabled), Theme::kGlyphStroke);
                chevron.setCapStyle(Qt::RoundCap);
                chevron.setJoinStyle(Qt::RoundJoin);
                painter->setPen(chevron);
                painter->setBrush(Qt::NoBrush);
                painter->drawPolyline(QPolygonF({c + QPointF(-3.5, -1.5), c + QPointF(0, 2), c + QPointF(3.5, -1.5)}));
            }
            painter->restore();
            return;
        }
    }
    if (control == CC_SpinBox) {
        if (const auto* spin = qstyleoption_cast<const QStyleOptionSpinBox*>(option)) {
            const bool enabled = spin->state & State_Enabled;
            const bool focus = enabled && (spin->state & State_HasFocus);
            painter->save();
            painter->setRenderHint(QPainter::Antialiasing);
            const QRectF r = QRectF(spin->rect).adjusted(0.5, 0.5, -0.5, -0.5);
            const double radius = std::min<double>(Theme::metric(Theme::Metric::RadiusSmall), r.height() / 2.0);
            painter->setPen(focus ? QPen(Theme::color(Token::FocusRing), Theme::kBorder) : QPen(Qt::NoPen));
            painter->setBrush(Theme::color(enabled ? Token::Base : Token::AlternateBase));
            painter->drawRoundedRect(r, radius, radius);
            painter->setBrush(Qt::NoBrush);
            for (const auto& [sub, up, step] : {std::tuple{SC_SpinBoxUp, true, QAbstractSpinBox::StepUpEnabled},
                                                std::tuple{SC_SpinBoxDown, false, QAbstractSpinBox::StepDownEnabled}}) {
                if (!(spin->subControls & sub))
                    continue;
                const QRectF area = subControlRect(CC_SpinBox, spin, sub, widget);
                const bool can = enabled && (spin->stepEnabled & step);
                const bool hot = can && (spin->activeSubControls & sub) && (spin->state & State_MouseOver);
                QPen chevron(Theme::color(!can ? Token::TextDisabled : hot ? Token::Text : Token::TextMuted), Theme::kGlyphStroke);
                chevron.setCapStyle(Qt::RoundCap);
                chevron.setJoinStyle(Qt::RoundJoin);
                painter->setPen(chevron);
                const QPointF c = area.center();
                const double d = up ? -1.0 : 1.0;
                painter->drawPolyline(QPolygonF({c + QPointF(-3, -1.5 * d), c + QPointF(0, 1.5 * d), c + QPointF(3, -1.5 * d)}));
            }
            painter->restore();
            return;
        }
    }
    if (control == CC_MdiControls) {
        const struct {
            SubControl sub;
            SubControl as;
        } parts[] = {{SC_MdiMinButton, SC_TitleBarMinButton},
                     {SC_MdiNormalButton, SC_TitleBarNormalButton},
                     {SC_MdiCloseButton, SC_TitleBarCloseButton}};
        for (const auto& part : parts) {
            if (!(option->subControls & part.sub))
                continue;
            const QRect r = QProxyStyle::subControlRect(control, option, part.sub, widget);
            const int d = std::min({r.width(), r.height(), int(kButtonSize)});
            QRect round(0, 0, d, d);
            round.moveCenter(r.center());
            drawButton(painter, round, part.as, (option->activeSubControls & part.sub) && (option->state & State_MouseOver),
                       (option->activeSubControls & part.sub) && (option->state & State_Sunken), true);
        }
        return;
    }
    QProxyStyle::drawComplexControl(control, option, painter, widget);
}

QPixmap DoodleboldStyle::generatedIconPixmap(QIcon::Mode mode, const QPixmap& pixmap, const QStyleOption* option) const {
    if (mode != QIcon::Disabled)
        return QProxyStyle::generatedIconPixmap(mode, pixmap, option);
    QPixmap out(pixmap.size());
    out.setDevicePixelRatio(pixmap.devicePixelRatio());
    out.fill(Qt::transparent);
    QPainter p(&out);
    p.setOpacity(kDisabledIconOpacity);
    p.drawPixmap(0, 0, pixmap);
    return out;
}

void DoodleboldStyle::drawPrimitive(PrimitiveElement element, const QStyleOption* option, QPainter* painter, const QWidget* widget) const {
    if (element == PE_IndicatorDockWidgetResizeHandle)
        return;
    if (element == PE_IndicatorCheckBox || element == PE_IndicatorRadioButton) {
        const bool enabled = option->state & State_Enabled;
        const bool on = option->state & State_On;
        const bool partly = option->state & State_NoChange;
        const bool hover = enabled && (option->state & State_MouseOver);
        const QRectF box = QRectF(option->rect).adjusted(0.5, 0.5, -0.5, -0.5);
        const QColor accent = Theme::color(enabled ? Token::Accent : Token::Border);
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);
        painter->setPen(Theme::color(hover ? Token::TextMuted : enabled ? Token::BorderStrong : Token::Border));
        painter->setBrush(Theme::color(enabled ? Token::Base : Token::AlternateBase));
        if (element == PE_IndicatorRadioButton) {
            painter->drawEllipse(box);
            if (on) {
                painter->setPen(Qt::NoPen);
                painter->setBrush(accent);
                painter->drawEllipse(box.center(), box.width() / 4.0, box.height() / 4.0);
            }
        } else {
            const double radius = Theme::kRadiusXs;  // a rounded square: at the small radius it looked like a radio button
            if (on || partly) {
                painter->setPen(accent);
                painter->setBrush(accent);
            }
            painter->drawRoundedRect(box, radius, radius);
            QPen mark(Theme::color(enabled ? Token::AccentText : Token::TextDisabled), Theme::kGlyphStroke);
            mark.setCapStyle(Qt::RoundCap);
            mark.setJoinStyle(Qt::RoundJoin);
            painter->setPen(mark);
            const auto at = [&box](double x, double y) { return box.topLeft() + QPointF(box.width() * x, box.height() * y); };
            if (on)
                painter->drawPolyline(QPolygonF({at(0.24, 0.52), at(0.43, 0.71), at(0.76, 0.30)}));
            else if (partly)
                painter->drawLine(at(0.27, 0.5), at(0.73, 0.5));
        }
        painter->restore();
        return;
    }
    if (element == PE_FrameWindow) {
        const int m = kFrameMargin;
        const double radius = Theme::metric(Theme::Metric::CardRadius);
        QPainterPath card;
        card.addRoundedRect(QRectF(option->rect).adjusted(m + 0.5, m + 0.5, -m - 0.5, -m - 0.5), radius, radius);
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);
        painter->setPen(Theme::color(Token::Border));
        painter->setBrush(Qt::NoBrush);
        painter->drawPath(card);
        painter->restore();
        return;
    }
    QProxyStyle::drawPrimitive(element, option, painter, widget);
}

}
