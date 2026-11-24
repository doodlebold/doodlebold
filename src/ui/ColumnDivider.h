#pragma once

#include <QWidget>

namespace doodlebold {

// Lies over the gap between two panel columns, so that all of the gap takes the pointer; paints nothing. MainWindow
// places it and does what a drag means.
class ColumnDivider : public QWidget {
    Q_OBJECT
public:
    explicit ColumnDivider(QWidget* parent);

signals:
    void dragStarted();
    void dragged(int dx);  // from where the drag started
    void dragFinished();

protected:
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;

private:
    bool m_dragging = false;
    double m_startX = 0;  // on the screen: the divider moves under the pointer
};

}
