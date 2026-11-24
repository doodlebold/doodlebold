#include "ui/ViewSelector.h"

#include "ui/Theme.h"

#include <QHelpEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QToolTip>
#include <QWheelEvent>

#include <algorithm>

namespace doodlebold {

using Theme::Token;

ViewSelector::ViewSelector(QWidget* parent) : QWidget(parent) {
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setMouseTracking(false);
}

void ViewSelector::setItems(std::vector<Item> items) {
    m_items = std::move(items);
    if (m_current >= count())
        m_current = count() - 1;
    relayout();
}

void ViewSelector::setItem(int index, Item item) {
    if (index < 0 || index >= count())
        return;
    m_items[static_cast<std::size_t>(index)] = std::move(item);
    update(itemRect(index));
}

void ViewSelector::setCurrent(int index) {
    m_current = index >= 0 && index < count() ? index : -1;
    showCurrentRow();
    update();
}

int ViewSelector::tabsPerRow() const {
    const int room = width() - kArrowWidth;
    return std::max(1, room / kTabWidth);
}

int ViewSelector::rowCount() const {
    return m_items.empty() ? 1 : (count() + tabsPerRow() - 1) / tabsPerRow();
}

int ViewSelector::visibleRows() const {
    return std::min(rowCount(), kMaxRows);
}

QRect ViewSelector::itemRect(int index) const {
    if (index < 0 || index >= count())
        return {};
    const int row = index / tabsPerRow() - m_firstRow;
    if (row < 0 || row >= visibleRows())
        return {};
    return QRect((index % tabsPerRow()) * kTabWidth, 1 + row * kRowHeight, kTabWidth - 2, kRowHeight - 2);
}

int ViewSelector::itemAt(const QPoint& pos) const {
    for (int i = 0; i < count(); ++i)
        if (itemRect(i).contains(pos))
            return i;
    return -1;
}

QSize ViewSelector::sizeHint() const {
    return {kTabWidth * 3 + kArrowWidth, visibleRows() * kRowHeight + 2};
}

QSize ViewSelector::minimumSizeHint() const {
    return {kTabWidth + kArrowWidth, visibleRows() * kRowHeight + 2};
}

void ViewSelector::scrollTo(int firstRow) {
    const int last = std::max(0, rowCount() - visibleRows());
    firstRow = std::clamp(firstRow, 0, last);
    if (firstRow == m_firstRow)
        return;
    m_firstRow = firstRow;
    update();
}

void ViewSelector::showCurrentRow() {
    if (m_current < 0) {
        scrollTo(m_firstRow);
        return;
    }
    const int row = m_current / tabsPerRow();
    if (row < m_firstRow)
        scrollTo(row);
    else if (row >= m_firstRow + visibleRows())
        scrollTo(row - visibleRows() + 1);
}

void ViewSelector::relayout() {
    // More tabs, or a narrower window, can need another row: the height changes, so the layout above gives it room.
    if (visibleRows() != m_lastRows) {
        m_lastRows = visibleRows();
        updateGeometry();
    }
    scrollTo(m_firstRow);
    showCurrentRow();
    update();
}

void ViewSelector::resizeEvent(QResizeEvent* e) {
    QWidget::resizeEvent(e);
    relayout();
}

void ViewSelector::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.fillRect(rect(), Theme::color(Token::Window));
    const QFontMetrics fm = fontMetrics();
    for (int i = 0; i < count(); ++i) {
        const QRect r = itemRect(i);
        if (r.isEmpty())
            continue;
        const Item& it = m_items[static_cast<std::size_t>(i)];
        const bool current = i == m_current;
        const QRectF pill = QRectF(r).adjusted(1.5, 2.5, -1.5, -2.5);
        if (current) {
            p.setPen(Qt::NoPen);
            p.setBrush(Theme::color(Token::Selected));
            p.drawRoundedRect(pill, pill.height() / 2, pill.height() / 2);
        }
        const QRect icon(r.left() + Theme::kSpace3, r.center().y() - kIconSize / 2, kIconSize, kIconSize);
        if (!it.icon.isNull()) {
            QPainterPath corner;
            corner.addRoundedRect(QRectF(icon), Theme::kRadiusXs, Theme::kRadiusXs);
            p.save();
            p.setClipPath(corner);
            p.drawImage(icon, it.icon);
            p.restore();
        }
        const int zoomWidth = fm.horizontalAdvance(QStringLiteral("3200%")) + 4;
        const QRect zoom(r.right() - zoomWidth - Theme::kSpace3, r.top(), zoomWidth, r.height());
        p.setPen(Theme::color(current ? Token::SelectedText : Token::TextMuted));  // on the selection, on-selection
        p.drawText(zoom, Qt::AlignRight | Qt::AlignVCenter, it.zoom);
        p.setPen(Theme::color(current ? Token::SelectedText : Token::TextMuted));
        const QRect name(icon.right() + Theme::kSpace2, r.top(), zoom.left() - icon.right() - Theme::kSpace3, r.height());
        p.drawText(name, Qt::AlignLeft | Qt::AlignVCenter, fm.elidedText(it.name, Qt::ElideMiddle, name.width()));
    }
    if (scrolls()) {
        const QRect column(width() - kArrowWidth, 0, kArrowWidth, height());
        p.setPen(Qt::NoPen);
        const auto arrow = [&](bool up, bool enabled) {
            const QRect half = up ? column.adjusted(0, 0, 0, -column.height() / 2) : column.adjusted(0, column.height() / 2, 0, 0);
            const QPointF c = QRectF(half).center();
            QPainterPath path;
            const double d = up ? -3.0 : 3.0;
            path.moveTo(c.x() - 4, c.y() - d / 2);
            path.lineTo(c.x() + 4, c.y() - d / 2);
            path.lineTo(c.x(), c.y() + d);
            path.closeSubpath();
            p.setBrush(enabled ? Theme::color(Token::Text) : Theme::color(Token::TextMuted));
            p.drawPath(path);
        };
        arrow(true, m_firstRow > 0);
        arrow(false, m_firstRow + visibleRows() < rowCount());
    }
}

void ViewSelector::mousePressEvent(QMouseEvent* e) {
    const QPoint pos = e->position().toPoint();
    if (e->button() == Qt::LeftButton) {
        if (scrolls() && pos.x() >= width() - kArrowWidth) {
            scrollTo(m_firstRow + (pos.y() < height() / 2 ? -1 : 1));
            return;
        }
        if (const int i = itemAt(pos); i >= 0)
            emit activated(i);
        return;
    }
    QWidget::mousePressEvent(e);
}

void ViewSelector::mouseReleaseEvent(QMouseEvent* e) {
    if (e->button() == Qt::MiddleButton) {
        if (const int i = itemAt(e->position().toPoint()); i >= 0)
            emit closeRequested(i);
        return;
    }
    QWidget::mouseReleaseEvent(e);
}

void ViewSelector::wheelEvent(QWheelEvent* e) {
    if (scrolls() && e->angleDelta().y() != 0)
        scrollTo(m_firstRow + (e->angleDelta().y() > 0 ? -1 : 1));
    e->accept();
}

bool ViewSelector::event(QEvent* e) {
    if (e->type() == QEvent::ToolTip) {
        auto* help = static_cast<QHelpEvent*>(e);
        const int i = itemAt(help->pos());
        if (i >= 0)
            QToolTip::showText(help->globalPos(), m_items[static_cast<std::size_t>(i)].toolTip, this, itemRect(i));
        else
            QToolTip::hideText();
        return true;
    }
    return QWidget::event(e);
}

}
