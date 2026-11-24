#pragma once

#include <QColor>
#include <QObject>

namespace doodlebold {

// A hue that survives colours without one: the hue is kept when the colour becomes grey, white or black.
class ColorState : public QObject {
    Q_OBJECT
public:
    explicit ColorState(QObject* parent = nullptr);

    QColor color() const { return m_color; }
    double hue() const { return m_hue; }

    void setColor(const QColor& color);
    void setColor(const QColor& color, double hue);

signals:
    void changed();

private:
    QColor m_color = Qt::black;
    double m_hue = 0.0;
};

}
