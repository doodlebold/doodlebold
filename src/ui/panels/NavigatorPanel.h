#pragma once

#include <QImage>
#include <QPointF>
#include <QPointer>
#include <QWidget>

#include <vector>

class QToolButton;

namespace doodlebold {

class ValueBar;

class CanvasView;
class NavigatorPreview;

class NavigatorPanel : public QWidget {
    Q_OBJECT
public:
    explicit NavigatorPanel(QWidget* parent = nullptr);

    void setCanvas(CanvasView* canvas);
    CanvasView* canvas() const { return m_canvas; }

    // Slider position (0..1000) of a zoom factor and back: logarithmic on each side of 100 %, 7 halvings to the left
    // and 5 doublings to the right.
    static int zoomToSlider(double zoom);
    static double sliderToZoom(int value);

    QPointF previewToDocument(const QPointF& previewPos) const;
    QPointF documentToPreview(const QPointF& docPos) const;

    const QImage& previewImage();
    void flushPreview();
    int previewRebuilds() const;
    int previewPartialUpdates() const;

private:
    void syncControls();
    QPointF canvasCentre() const;

    QPointer<CanvasView> m_canvas;
    std::vector<QMetaObject::Connection> m_connections;
    NavigatorPreview* m_preview = nullptr;
    QWidget* m_zoomRow = nullptr;
    QWidget* m_angleRow = nullptr;
    ValueBar* m_zoomBar = nullptr;
    ValueBar* m_angleBar = nullptr;
    QToolButton* m_flipButton = nullptr;
};

}
