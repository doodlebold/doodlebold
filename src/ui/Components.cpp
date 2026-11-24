#include "ui/Components.h"

#include "ui/Theme.h"

#include <QAbstractButton>
#include <QFrame>
#include <QLabel>
#include <QPainter>
#include <QVBoxLayout>

namespace doodlebold::components {

QLabel* sectionHeading(const QString& text, QWidget* parent) {
    auto* label = new QLabel(text, parent);
    label->setProperty("role", "sectionTitle");  // the style sheet's overline: size, weight, letter spacing, colour
    label->setFont(Theme::font(Theme::TextRole::Overline));
    return label;
}

QFrame* sectionCard(const QString& title, QVBoxLayout*& body) {
    auto* card = new QFrame;
    card->setObjectName(QStringLiteral("optionsSection"));
    body = new QVBoxLayout(card);
    body->setContentsMargins(Theme::kCardPadding, Theme::kCardPadding - Theme::kCardTitleLift, Theme::kCardPadding, Theme::kCardPadding);
    body->setSpacing(Theme::kCardRowGap);
    body->addWidget(sectionHeading(title));
    return card;
}

QLabel* mutedLabel(const QString& text, QWidget* parent) {
    auto* label = new QLabel(text, parent);
    label->setProperty("role", "hint");  // the style sheet's TextMuted, so it follows the theme
    label->setWordWrap(true);
    return label;
}

void toolButton(QAbstractButton* button) {
    button->setFixedSize(Theme::compact::kControlMd, Theme::compact::kControlMd);
    button->setIconSize(QSize(Theme::kIconMd, Theme::kIconMd));
}

QString tipHtml(const QString& name, const QString& keys, const QString& explanation) {
    // Qt's rich text takes a point size and a colour; the label's own padding is the style sheet's.
    QString html = QStringLiteral("<div style=\"font-weight:600;\">%1</div>").arg(name.toHtmlEscaped());
    if (!keys.isEmpty())
        html +=
            QStringLiteral(
                "<div style=\"font-size:%1pt; color:%2;\">%3</div>")  // ui-tokens: allow: rich text takes its size and colour inline, from the tokens
                .arg(Theme::font(Theme::TextRole::Caption).pointSizeF())
                .arg(Theme::color(Theme::Token::TextMuted).name(), keys.toHtmlEscaped());
    if (!explanation.isEmpty())
        html += QStringLiteral("<div>%1</div>").arg(explanation.toHtmlEscaped());
    return html;
}

void paintChecker(QPainter& painter, const QRect& area, int cell, QRgb light, QRgb dark) {
    painter.save();
    painter.setClipRect(area, painter.hasClipping() ? Qt::IntersectClip : Qt::ReplaceClip);
    for (int y = area.top(); y <= area.bottom(); y += cell)
        for (int x = area.left(); x <= area.right(); x += cell) {
            const int index = (x - area.left()) / cell + (y - area.top()) / cell;
            painter.fillRect(x, y, cell, cell, QColor::fromRgba(index % 2 ? dark : light));
        }
    painter.restore();
}

}
