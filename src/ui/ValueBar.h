#pragma once

#include <QWidget>

#include <functional>
#include <optional>

class QLineEdit;

namespace doodlebold {

// A value control one compact field instead of a slider and a spin box. Like QSlider /
// QSpinBox, setValue() emits valueChanged when the value changes; owners block signals while syncing.
class ValueBar : public QWidget {
    Q_OBJECT
public:
    enum class Look {
        Bar,
        Line
    };

    explicit ValueBar(QWidget* parent = nullptr);

    void setLook(Look look);
    void setRange(double minimum, double maximum);
    double minimum() const { return m_min; }
    double maximum() const { return m_max; }
    void setDecimals(int decimals);
    int decimals() const { return m_decimals; }
    void setSuffix(const QString& suffix);
    void setLabel(const QString& label);
    void setSingleStep(double step);
    // Only the minimum plus whole multiples of `step` can be set (the layer Scale moves in fives: 10, 15 … 500); 0 allows
    // every value the decimals show.
    void setSnap(double step);
    // Whether the mouse wheel steps the value (default). Off, the wheel passes to the parent, so a panel of fields
    // scrolls instead.
    void setWheelSteps(bool on) { m_wheelSteps = on; }
    bool wheelSteps() const { return m_wheelSteps; }
    void setMapping(std::function<double(double)> toPosition, std::function<double(double)> fromPosition);

    double value() const { return m_value; }
    void setValue(double value);
    QString text() const;
    // A value it's kept to, below the one set (the brush size memory allows): shown struck through, then this, then a lock
    // whose tooltip is `why`. std::nullopt shows the value alone.
    void setLimit(std::optional<double> limit, const QString& why = {});
    bool showsLimit() const { return m_limit && *m_limit < m_value; }
    bool isDragging() const { return m_dragging; }
    bool isEditing() const;
    void editValue();

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

signals:
    void valueChanged(double value);
    void dragStarted();
    void dragFinished();

protected:
    bool event(QEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    QRect track() const;
    QRect textArea() const;
    QRect lockArea() const;
    void paintText(QPainter& p, const QRect& area, const QColor& colour);
    double position() const;
    double valueAt(int x) const;
    double rounded(double v) const;
    void step(double steps);
    void closeEditor(bool apply);

    Look m_look = Look::Bar;
    double m_min = 0, m_max = 100, m_value = 0, m_step = 1, m_snap = 0;
    int m_decimals = 0;
    QString m_suffix, m_label;
    std::function<double(double)> m_toPosition, m_fromPosition;
    bool m_dragging = false;
    bool m_wheelSteps = true;
    QLineEdit* m_editor = nullptr;
    std::optional<double> m_limit;
    QString m_limitWhy;
};

}
