#pragma once

#include "core/tools/ToolBox.h"

#include <QPointer>
#include <QStackedWidget>

class QButtonGroup;
class QCheckBox;
class QDoubleSpinBox;
class QListWidget;
class QComboBox;
class QLabel;
class QPushButton;
class QRadioButton;
class QSlider;
class QSpinBox;
class QWidget;

namespace doodlebold {

class ValueBar;

class FixedToolPanels : public QStackedWidget {
    Q_OBJECT
public:
    explicit FixedToolPanels(ToolBox* tools, QWidget* parent = nullptr);

    void setTransformActive(bool active);

signals:
    void transformRequested(doodlebold::TransformMode mode);
    void transformApplied();
    void transformCancelled();
    void reverseRequested(bool horizontal);
    void rotate90Requested(bool clockwise);

private:
    QWidget* buildMarquee();
    QWidget* buildLasso();
    QWidget* buildWand();
    QWidget* buildHint(FixedTool tool);
    QWidget* buildText();
    void showTextColor();
    void sync();
    void edit(const std::function<void(FixedToolOptions&)>& change);
    void showPageFor();

    QPointer<ToolBox> m_tools;
    bool m_syncing = false;
    std::array<QWidget*, kFixedTools.size()> m_pages{};

    QCheckBox* m_cutAndMove = nullptr;
    QButtonGroup* m_modes = nullptr;
    QPushButton* m_ok = nullptr;
    QPushButton* m_cancel = nullptr;
    ValueBar* m_perspectiveBar = nullptr;
    QComboBox* m_resample[3] = {nullptr, nullptr, nullptr};
    QButtonGroup* m_lassoModes = nullptr;
    QCheckBox* m_lassoAa = nullptr;
    QButtonGroup* m_wandModes = nullptr;
    QWidget* m_transparencyRow = nullptr;
    QWidget* m_thresholdRow = nullptr;
    ValueBar* m_transparencyBar = nullptr;
    ValueBar* m_thresholdBar = nullptr;
    ValueBar* m_gapBar = nullptr;
    QButtonGroup* m_wandSource = nullptr;
    QCheckBox* m_wandAa = nullptr;
    QCheckBox* m_textVertical = nullptr;
    QWidget* m_textColor = nullptr;
    QDoubleSpinBox* m_textSize = nullptr;
    QCheckBox* m_textBold = nullptr;
    QCheckBox* m_textItalic = nullptr;
    QCheckBox* m_textFit = nullptr;
    QListWidget* m_fonts = nullptr;
};

}
