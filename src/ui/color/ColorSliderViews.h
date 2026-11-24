#pragma once

#include "core/color/ColorModels.h"

#include <QPointer>
#include <QWidget>

#include <array>
#include <functional>

namespace doodlebold {

class ColorAssets;
class ColorState;

class ColorSliderRow : public QWidget {
    Q_OBJECT
public:
    using Gradient = std::function<QColor(double)>;

    ColorSliderRow(QString letter, int maximum, QWidget* parent = nullptr);

    void setLetter(const QString& letter);
    void setGradient(Gradient gradient);
    // The position 0…1; the number shows round(position · maximum).
    void setPosition(double position);
    double position() const { return m_position; }
    int number() const;
    QString letter() const { return m_letter; }
    QRect track() const;

    QSize sizeHint() const override { return {200, 23}; }
    QSize minimumSizeHint() const override { return {120, 23}; }

signals:
    void moved(double position);

protected:
    void paintEvent(QPaintEvent* e) override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;

private:
    void dragTo(double x);

    QString m_letter;
    int m_maximum;
    Gradient m_gradient;
    double m_position = 0.0;
};

class RgbSlidersView : public QWidget {
    Q_OBJECT
public:
    explicit RgbSlidersView(ColorState* state, QWidget* parent = nullptr);
    ColorSliderRow* row(int channel) const { return m_rows[std::size_t(channel)]; }

private:
    void sync();
    QPointer<ColorState> m_state;
    std::array<ColorSliderRow*, 3> m_rows{};
};

class HsvSlidersView : public QWidget {
    Q_OBJECT
public:
    HsvSlidersView(ColorState* state, ColorAssets* assets, QWidget* parent = nullptr);
    ColorSliderRow* row(int index) const { return m_rows[std::size_t(index)]; }

private:
    void sync();
    void edit(int index, double position);

    QPointer<ColorState> m_state;
    QPointer<ColorAssets> m_assets;
    std::array<ColorSliderRow*, 3> m_rows{};
    // The view's own values: dragging S down to 0 keeps the hue.
    color::SliderValues m_values;
    QColor m_shown;
};

}
