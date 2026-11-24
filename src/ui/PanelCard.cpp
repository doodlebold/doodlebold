#include "ui/PanelCard.h"

#include "ui/Theme.h"

#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QVBoxLayout>

#include <algorithm>

namespace doodlebold {

namespace {

// The card's padding past the gap round it: the edge to the content is the compact padding in all.
constexpr int kInset = Theme::compact::kPadding - Theme::kSpace1;

}

PanelCard::PanelCard(const QString& title, QWidget* content, QWidget* parent)
    : QWidget(parent), m_content(content), m_title(new QLabel(title.toUpper(), this)) {
    m_title->setObjectName(QStringLiteral("cardTitle"));
    m_title->setFont(Theme::font(Theme::TextRole::Overline));
    const int edge = Theme::metric(Theme::Metric::CardGap) + kInset;
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(edge, edge, edge, edge);
    layout->setSpacing(Theme::kSpaceHair);
    m_title->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);
    layout->addWidget(m_title);
    // A panel that asks for no more than its size hint (vertical policy Maximum: the Color panel, exactly as tall as its views)
    // takes no stretch, and so makes the card no taller than the panel plus the chrome: no empty space under it.
    const bool fitsItsHint = content->sizePolicy().verticalPolicy() == QSizePolicy::Maximum;
    layout->addWidget(content, fitsItsHint ? 0 : 1);
    layout->setSizeConstraint(QLayout::SetMaximumSize);
    content->setParent(this);
}

QRect PanelCard::cardRect() const {
    const int gap = Theme::metric(Theme::Metric::CardGap);
    return rect().adjusted(gap, gap + m_edgeTop, -gap, -(gap + m_edgeBottom));
}

void PanelCard::setEdgeGap(int top, int bottom) {
    top = std::max(0, top);
    bottom = std::max(0, bottom);
    if (top == m_edgeTop && bottom == m_edgeBottom)
        return;
    m_edgeTop = top;
    m_edgeBottom = bottom;
    const int edge = Theme::metric(Theme::Metric::CardGap) + kInset;
    layout()->setContentsMargins(edge, edge + top, edge, edge + bottom);
    update();
}

QSize PanelCard::chrome() const {
    const QMargins m = layout()->contentsMargins();
    return {m.left() + m.right(), m.top() + m.bottom() + m_title->sizeHint().height() + layout()->spacing()};
}

void PanelCard::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const double radius = Theme::metric(Theme::Metric::CardRadius);
    QPainterPath card;
    card.addRoundedRect(QRectF(cardRect()).adjusted(0.5, 0.5, -0.5, -0.5), radius, radius);
    p.fillPath(card, Theme::color(Theme::Token::Surface));
    p.setPen(Theme::color(Theme::Token::Border));
    p.drawPath(card);
}

}
