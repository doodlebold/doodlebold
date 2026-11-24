#pragma once

#include <QWidget>

namespace doodlebold {

// A small, thick grip across a panel, dragged up and down to make the part above it taller or shorter (the tool tray).
// It fills the gap between the two parts, so all of the gap takes the pointer, and draws its grip in the middle. Its owner
// does what a drag means.
class RowHandle : public QWidget {
    Q_OBJECT
public:
    explicit RowHandle(QWidget* parent);

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override { return sizeHint(); }

signals:
    void dragStarted();
    void dragged(int dy);  // from where the drag started
    void dragFinished();

protected:
    void paintEvent(QPaintEvent* event) override;
    void enterEvent(QEnterEvent* event) override;
    void leaveEvent(QEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;

private:
    bool m_dragging = false;
    bool m_hover = false;
    double m_startY = 0;  // on the screen: the handle moves under the pointer
};

}
