#include "ui/panels/RowHandle.h"

#include "ui/Theme.h"

#include <QMouseEvent>
#include <QPainter>

#include <cmath>

namespace doodlebold {

namespace {

constexpr int kGripWidth = 36;     // px: the grip's length (component metric)
constexpr int kGripThickness = 4;  // px: the grip's thickness (component metric)

}

RowHandle::RowHandle(QWidget* parent) : QWidget(parent) {
    setCursor(Qt::SplitVCursor);
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
}

QSize RowHandle::sizeHint() const {
    return {kGripWidth, Theme::kPanelSectionGap};
}

void RowHandle::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen);
    p.setBrush(Theme::color(m_hover || m_dragging ? Theme::Token::Accent : Theme::Token::BorderStrong));
    const QRectF grip((width() - kGripWidth) / 2.0, (height() - kGripThickness) / 2.0, kGripWidth, kGripThickness);
    p.drawRoundedRect(grip, kGripThickness / 2.0, kGripThickness / 2.0);
}

void RowHandle::enterEvent(QEnterEvent*) {
    m_hover = true;
    update();
}

void RowHandle::leaveEvent(QEvent*) {
    m_hover = false;
    update();
}

void RowHandle::mousePressEvent(QMouseEvent* event) {
    if (event->button() != Qt::LeftButton) {
        event->ignore();
        return;
    }
    m_dragging = true;
    m_startY = event->globalPosition().y();
    update();
    emit dragStarted();
}

void RowHandle::mouseMoveEvent(QMouseEvent* event) {
    if (m_dragging)
        emit dragged(int(std::lround(event->globalPosition().y() - m_startY)));
}

void RowHandle::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton && m_dragging) {
        m_dragging = false;
        update();
        emit dragFinished();
    }
}

}
