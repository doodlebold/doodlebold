#pragma once

#include <QImage>
#include <QPointer>
#include <QWidget>

#include <algorithm>
#include <utility>

namespace doodlebold {

class ColorState;

// Hue runs clockwise with red at 10 o'clock and yellow at the top. The disc is a square of chroma (left grey, right the full hue)
// by value (top light, bottom black) mapped smoothly into a circle (the elliptical grid mapping: no seams on the diagonals). Where
// the chroma passes the value the colour is fully saturated, so the lower right is the hue darkened.
class ColorWheelView : public QWidget {
    Q_OBJECT
public:
    explicit ColorWheelView(ColorState* state, QWidget* parent = nullptr);

    // The circle is fixed; smaller only when the panel is narrower.
    static constexpr int kSize = 180;  // component metric: the colour wheel's size

    QSize sizeHint() const override { return {kSize, kSize}; }
    QSize minimumSizeHint() const override { return {100, 100}; }
    bool hasHeightForWidth() const override { return true; }
    int heightForWidth(int w) const override { return std::min(w, kSize); }

    QPointF centre() const;
    double outerRadius() const;
    double innerRadius() const;
    double discRadius() const;
    QPointF ringPoint(double hue) const;
    // Where in the disc a chroma and value sit, and back: chroma and value at a point (one outside the disc: its rim's).
    QPointF discPoint(double chroma, double value) const;
    std::pair<double, double> chromaValueAt(const QPointF& pos) const;
    // The colour the disc shows for a chroma and value.
    static QColor discColor(double hue, double chroma, double value);

protected:
    void paintEvent(QPaintEvent* e) override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;
    void resizeEvent(QResizeEvent* e) override;

private:
    enum class Drag {
        None,
        Ring,
        Disc
    };
    void pick(const QPointF& pos);
    void syncFromState();
    QImage discImage() const;

    QPointer<ColorState> m_state;
    Drag m_drag = Drag::None;
    // The knob's chroma and value: a point in the saturated part keeps where it was clicked.
    double m_chroma = 0.0, m_v = 0.0;
    QColor m_shown;
    QImage m_ring;
    std::pair<double, double> m_ringFor{0.0, 0.0};
};

}
