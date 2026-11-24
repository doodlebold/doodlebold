#include "ui/ColumnDivider.h"

#include <QMouseEvent>

#include <cmath>

namespace doodlebold {

ColumnDivider::ColumnDivider(QWidget* parent) : QWidget(parent) {
    setCursor(Qt::SplitHCursor);
}

void ColumnDivider::mousePressEvent(QMouseEvent* event) {
    if (event->button() != Qt::LeftButton) {
        event->ignore();
        return;
    }
    m_dragging = true;
    m_startX = event->globalPosition().x();
    emit dragStarted();
}

void ColumnDivider::mouseMoveEvent(QMouseEvent* event) {
    if (m_dragging)
        emit dragged(int(std::lround(event->globalPosition().x() - m_startX)));
}

void ColumnDivider::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton && m_dragging) {
        m_dragging = false;
        emit dragFinished();
    }
}

}
