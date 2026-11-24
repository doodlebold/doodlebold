#include "ui/tools/QuickShapePill.h"

#include "ui/Theme.h"

#include <QAbstractButton>
#include <QEnterEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>

namespace doodlebold {

namespace {

constexpr int kInner = Theme::compact::kControlLg;     // a button's height
constexpr int kHeight = kInner + 2 * Theme::kSpace1;   // the pill round its buttons
constexpr int kStepper = Theme::compact::kControlLg;
constexpr int kMargin = Theme::kSpace2;
constexpr int kIcon = Theme::kIconMd;
constexpr int kIconGap = Theme::kSpace2;

QFont pillFont(const QWidget*) {
    return Theme::font(Theme::TextRole::Body);
}

QPainterPath shapeIcon(QuickShapeKind kind) {
    QPainterPath path;
    switch (kind) {
    case QuickShapeKind::Triangle: path.addPolygon(QPolygonF({{9, 3}, {16, 15}, {2, 15}, {9, 3}})); break;
    case QuickShapeKind::Square: path.addRect(3.5, 3.5, 11, 11); break;
    case QuickShapeKind::Rectangle: path.addRect(1.5, 5, 15, 8); break;
    case QuickShapeKind::Circle: path.addEllipse(2.5, 2.5, 13, 13); break;
    case QuickShapeKind::Oval: path.addEllipse(1.5, 4, 15, 10); break;
    case QuickShapeKind::Polygon: path.addPolygon(QPolygonF({{9, 2}, {16, 7.2}, {13.4, 15.5}, {4.6, 15.5}, {2, 7.2}, {9, 2}})); break;
    }
    return path;
}

QString shapeName(QuickShapeKind kind) {
    switch (kind) {
    case QuickShapeKind::Triangle: return QuickShapePill::tr("Triangle");
    case QuickShapeKind::Square: return QuickShapePill::tr("Square");
    case QuickShapeKind::Rectangle: return QuickShapePill::tr("Rectangle");
    case QuickShapeKind::Circle: return QuickShapePill::tr("Circle");
    case QuickShapeKind::Oval: return QuickShapePill::tr("Oval");
    case QuickShapeKind::Polygon: return QuickShapePill::tr("Polygon");
    }
    return {};
}

}

// A round-ended button of the pill: an icon, with a label when there is room. Filled when checked or `primary`.
class PillButton : public QAbstractButton {
public:
    PillButton(QPainterPath icon, const QString& name, QWidget* parent) : QAbstractButton(parent), m_icon(std::move(icon)) {
        setText(name);
        setAccessibleName(name);
        setFocusPolicy(Qt::NoFocus);
        setAttribute(Qt::WA_Hover);
        setCursor(Qt::ArrowCursor);
    }

    void setPrimary(bool on) { m_primary = on; }
    void setShowsLabel(bool on) {
        m_label = on;
        setToolTip(on ? QString() : text());
        updateGeometry();
    }
    void setSide(int side) {
        m_side = side;
        updateGeometry();
    }

    QSize sizeHint() const override {
        if (!m_label)
            return {m_side, m_side};
        const QFontMetrics fm(pillFont(this));
        return {9 + kIcon + kIconGap + fm.horizontalAdvance(text()) + 12, m_side};
    }

protected:
    void paintEvent(QPaintEvent*) override {
        using Token = Theme::Token;
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const bool filled = isChecked() || m_primary;
        QColor back = Qt::transparent;
        if (filled)
            back = Theme::color(Token::Selected);
        else if (isDown())
            back = Theme::color(Token::Pressed);
        else if (underMouse() && isEnabled())
            back = Theme::color(Token::Hover);
        p.setPen(Qt::NoPen);
        p.setBrush(back);
        p.drawRoundedRect(QRectF(rect()), height() / 2.0, height() / 2.0);
        const QColor ink = !isEnabled() ? Theme::color(Token::TextDisabled)
                           : filled     ? Theme::color(Token::SelectedText)
                                        : Theme::color(Token::Text);
        const double left = m_label ? double(Theme::kSpace2) : (width() - kIcon) / 2.0;
        p.save();
        p.translate(left, (height() - kIcon) / 2.0);
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(ink, Theme::kGlyphStroke, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.drawPath(m_icon);
        p.restore();
        if (m_label) {
            p.setFont(pillFont(this));
            p.setPen(ink);
            p.drawText(QRectF(left + kIcon + kIconGap, 0, width() - left - kIcon - kIconGap, height()), Qt::AlignVCenter | Qt::AlignLeft,
                       text());
        }
    }
    void enterEvent(QEnterEvent* e) override {
        update();
        QAbstractButton::enterEvent(e);
    }
    void leaveEvent(QEvent* e) override {
        update();
        QAbstractButton::leaveEvent(e);
    }

private:
    QPainterPath m_icon;
    bool m_primary = false;
    bool m_label = false;
    int m_side = kInner;
};

QuickShapePill::QuickShapePill(QWidget* parent) : QWidget(parent) {
    setObjectName(QStringLiteral("quickShapePill"));
    setFocusPolicy(Qt::NoFocus);
    setCursor(Qt::ArrowCursor);
    setFixedHeight(kHeight);
    auto* row = new QHBoxLayout(this);
    row->setContentsMargins(kMargin, 0, kMargin, 0);
    row->setSpacing(Theme::kSpaceHair);
    row->setSizeConstraint(QLayout::SetFixedSize);

    for (const QuickShapeKind kind : {QuickShapeKind::Triangle, QuickShapeKind::Square, QuickShapeKind::Rectangle, QuickShapeKind::Circle,
                                      QuickShapeKind::Oval, QuickShapeKind::Polygon}) {
        auto* button = new PillButton(shapeIcon(kind), shapeName(kind), this);
        button->setCheckable(true);
        button->setShowsLabel(true);
        connect(button, &QAbstractButton::clicked, this, [this, kind] { emit kindChosen(kind); });
        m_shapes[std::size_t(kind)] = button;
        row->addWidget(button);
    }

    QPainterPath minus, plus;
    minus.moveTo(5, 9);
    minus.lineTo(13, 9);
    plus = minus;
    plus.moveTo(9, 5);
    plus.lineTo(9, 13);
    m_stepper = new QWidget(this);
    auto* stepper = new QHBoxLayout(m_stepper);
    stepper->setContentsMargins(Theme::kSpace1, 0, 0, 0);
    stepper->setSpacing(Theme::kSpaceHair);
    m_fewer = new PillButton(minus, tr("Fewer corners"), m_stepper);
    m_more = new PillButton(plus, tr("More corners"), m_stepper);
    m_fewer->setSide(kStepper);
    m_more->setSide(kStepper);
    m_fewer->setToolTip(m_fewer->text());
    m_more->setToolTip(m_more->text());
    m_count = new QLabel(m_stepper);
    m_count->setAlignment(Qt::AlignCenter);
    m_count->setFixedWidth(22);  // px: two digits (component metric)
    m_count->setFont(pillFont(this));
    stepper->addWidget(m_fewer);
    stepper->addWidget(m_count);
    stepper->addWidget(m_more);
    connect(m_fewer, &QAbstractButton::clicked, this, [this] { emit cornersChosen(m_corners - 1); });
    connect(m_more, &QAbstractButton::clicked, this, [this] { emit cornersChosen(m_corners + 1); });
    row->addWidget(m_stepper);

    auto* divider = new QWidget(this);
    divider->setObjectName(QStringLiteral("quickShapeDivider"));
    divider->setFixedSize(13, Theme::compact::kControlMd);  // 13 px wide: the line and its room (component metric)
    row->addWidget(divider);

    QPainterPath cross, tick;
    cross.moveTo(4, 4);
    cross.lineTo(14, 14);
    cross.moveTo(14, 4);
    cross.lineTo(4, 14);
    tick.moveTo(3.5, 9.5);
    tick.lineTo(7.5, 13.5);
    tick.lineTo(14.5, 5);
    m_cancel = new PillButton(cross, tr("Back to the stroke as drawn (Esc)"), this);
    m_done = new PillButton(tick, tr("Done (Enter)"), this);
    m_done->setPrimary(true);
    m_cancel->setToolTip(m_cancel->text());
    m_done->setToolTip(m_done->text());
    connect(m_cancel, &QAbstractButton::clicked, this, &QuickShapePill::cancelled);
    connect(m_done, &QAbstractButton::clicked, this, &QuickShapePill::accepted);
    row->addWidget(m_cancel);
    row->addWidget(m_done);
}

void QuickShapePill::setShape(QuickShapeKind kind, int corners) {
    for (std::size_t i = 0; i < m_shapes.size(); ++i)
        m_shapes[i]->setChecked(i == std::size_t(kind));
    m_corners = corners;
    m_stepper->setVisible(kind == QuickShapeKind::Polygon);
    m_count->setText(QString::number(corners));
    m_fewer->setEnabled(corners > QuickShapeEdit::kMinimumCorners);
    m_more->setEnabled(corners < QuickShapeEdit::kMaximumCorners);
    layout()->activate();
}

void QuickShapePill::setCompact(bool compact) {
    for (PillButton* button : m_shapes)
        button->setShowsLabel(!compact);
    layout()->activate();
}

QAbstractButton* QuickShapePill::shapeButton(QuickShapeKind kind) const {
    return m_shapes[std::size_t(kind)];
}

QAbstractButton* QuickShapePill::fewerButton() const {
    return m_fewer;
}

QAbstractButton* QuickShapePill::moreButton() const {
    return m_more;
}

QAbstractButton* QuickShapePill::cancelButton() const {
    return m_cancel;
}

QAbstractButton* QuickShapePill::doneButton() const {
    return m_done;
}

void QuickShapePill::paintEvent(QPaintEvent*) {
    using Token = Theme::Token;
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QPen(Theme::color(Token::BorderStrong), Theme::kBorder));
    p.setBrush(Theme::color(Token::Surface));
    p.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), height() / 2.0 - 0.5, height() / 2.0 - 0.5);
    if (const auto* divider = findChild<QWidget*>(QStringLiteral("quickShapeDivider"))) {
        const QRect r = divider->geometry();
        p.setPen(QPen(Theme::color(Token::BorderStrong), Theme::kBorder));
        p.drawLine(QPointF(r.center().x() + 0.5, r.top()), QPointF(r.center().x() + 0.5, r.bottom()));
    }
}

void QuickShapePill::mousePressEvent(QMouseEvent* e) {
    e->accept();  // a press between the buttons is not for the canvas below
}

}
