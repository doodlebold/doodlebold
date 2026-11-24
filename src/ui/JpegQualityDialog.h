#pragma once

#include "core/io/DocumentFormat.h"
#include "core/io/JpegPreview.h"

#include <QAbstractScrollArea>
#include <QDialog>
#include <QImage>
#include <QTimer>

#include <future>

class QCheckBox;
class QComboBox;
class QLabel;
class QProgressBar;
class QRadioButton;

namespace doodlebold {

class QualityBar : public QWidget {
    Q_OBJECT
public:
    static constexpr int kMin = 1, kMax = 100;
    explicit QualityBar(int value = 90, QWidget* parent = nullptr);

    int value() const { return m_value; }
    void setValue(int value);
    QSize sizeHint() const override { return {190, 20}; }

signals:
    void valueChanged(int value);

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void keyPressEvent(QKeyEvent* e) override;
    void wheelEvent(QWheelEvent* e) override;

private:
    void setFromX(int x);
    int m_value;
};

class JpegPreviewView : public QAbstractScrollArea {
    Q_OBJECT
public:
    explicit JpegPreviewView(QWidget* parent = nullptr);

    void setImage(const QImage& image);
    void setScalePercent(double percent);
    double scalePercent() const { return m_scale; }
    QSize scaledSize() const;
    QSize deviceViewportSize() const;
    const QImage& image() const { return m_image; }

protected:
    bool event(QEvent* e) override;
    void paintEvent(QPaintEvent*) override;
    void resizeEvent(QResizeEvent*) override;

private:
    void updateScrollBars();
    void rebuildScaled();

    QImage m_image;
    QImage m_scaled;  // below 100 % the picture is scaled down once (smoothly) instead of on every paint
    double m_scale = 100.0;
};

class JpegQualityDialog : public QDialog {
    Q_OBJECT
public:
    static bool hasOptions(const DocumentFormat::Features& features);

    JpegQualityDialog(const QImage& canvas, const ExportOptions& initial, double resolution, QWidget* parent = nullptr);
    ~JpegQualityDialog() override;

    ExportOptions options() const;
    // No change pending and nothing being encoded: the Compression Result and the preview are up to date.
    bool isIdle() const;
    const io::CompressionResult& compression() const { return m_result; }
    void setDebounceMs(int ms) { m_debounce.setInterval(ms); }

    JpegPreviewView* preview() const { return m_view; }
    QComboBox* scaleCombo() const { return m_scale; }
    QRadioButton* originalRadio() const { return m_original; }
    QRadioButton* jpegRadio() const { return m_jpeg; }
    QualityBar* qualityBar() const { return m_quality; }
    QCheckBox* horizontalBox() const { return m_horizontal; }
    QCheckBox* verticalBox() const { return m_vertical; }
    QProgressBar* progress() const { return m_progress; }

private:
    void requestEncode();
    void startEncode();
    void onEncoded(const io::JpegPreview& preview, bool ok);
    void showResult(bool ok);
    void updatePreviewImage();
    void stepScale(int delta);
    void fitScale();

    QImage m_canvas;
    QImage m_decoded;
    double m_resolution;
    io::CompressionResult m_result;
    bool m_resultValid = false;
    bool m_dirty = false;
    std::future<void> m_job;
    bool m_running = false;

    QTimer m_debounce;
    JpegPreviewView* m_view;
    QComboBox* m_scale;
    QRadioButton* m_original;
    QRadioButton* m_jpeg;
    QualityBar* m_quality;
    QCheckBox* m_horizontal;
    QCheckBox* m_vertical;
    QLabel* m_horzPixels;
    QLabel* m_vertPixels;
    QLabel* m_fileSize;
    QLabel* m_rate;
    QProgressBar* m_progress;
};

}
