#pragma once

#include "core/brush/BrushSettings.h"

#include <QColor>
#include <QImage>
#include <QTimer>
#include <QWidget>

namespace doodlebold {

class BrushPreviewStrip : public QWidget {
    Q_OBJECT
public:
    static constexpr int kHeight = 64;
    static constexpr int kRedrawDelayMs = 30;

    explicit BrushPreviewStrip(QWidget* parent = nullptr);

    // `hue` is the colour wheel's, in degrees, which black, white and grey keep; below 0 it's the colour's own, or red.
    void setBrush(const BrushSettings& settings, const QColor& color, double hue = -1.0);

    const QImage& image();
    int renderCount() const { return m_renderCount; }
    bool redrawPending() const { return m_dirty; }

    QSize sizeHint() const override { return QSize(240, kHeight); }
    QSize minimumSizeHint() const override { return QSize(60, kHeight); }

protected:
    void paintEvent(QPaintEvent*) override;
    void resizeEvent(QResizeEvent*) override;

private:
    void scheduleRedraw();
    void render();
    QSize pixelSize() const;

    BrushSettings m_settings;
    QColor m_color = Qt::black;
    double m_hue = -1.0;
    QImage m_image;
    ToolType m_shownTool = ToolType::Pencil;  // the tool m_image was made for
    QTimer m_timer;
    bool m_dirty = true;
    int m_renderCount = 0;
};

}
