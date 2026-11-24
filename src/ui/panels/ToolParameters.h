#pragma once

#include "core/tools/ToolBox.h"

#include <QPointer>
#include <QWidget>

#include <functional>
#include <memory>
#include <vector>

class QButtonGroup;
class QCheckBox;
class QDoubleSpinBox;
class QGridLayout;
class QLabel;
class QPushButton;
class QSlider;
class QSpinBox;
class QToolButton;

namespace doodlebold {

class BrushResourceLibrary;
class BrushResourceSection;
class CollapsibleSection;
class ElidedLabel;
class ValueBar;
class StabilizerCombo;

class ToolParameters : public QWidget {
    Q_OBJECT
public:
    explicit ToolParameters(ToolBox* tools, BrushResourceLibrary* resources = nullptr, QWidget* parent = nullptr);

    // The current canvas's size, for the brush size memory allows there; an empty size shows no limit.
    void setCanvasSize(QSize size);
    // Works the limit out again, as after a change of Options › Performance › Memory.
    void refreshSizeLimit();

signals:
    void refused(const QString& message);

private:
    struct IntRow;
    struct RowUi {
        const IntRow* def = nullptr;
        QToolButton* pressure = nullptr;
        QLabel* label = nullptr;
        ValueBar* bar = nullptr;
    };

    struct Group {
        QWidget* box = nullptr;
        QGridLayout* grid = nullptr;
        int rows = 0;
        std::vector<bool (*)(ToolType)> usedBy;
    };

    Group& addGroup(const QString& title, const char* name);
    void buildSizeRow(Group& group);
    void buildRow(Group& group, const IntRow& def);
    void buildEdgeRow(Group& group);
    void buildRow(QGridLayout* grid, int row, const IntRow& def);
    void buildCloneOptions(Group& group);
    QButtonGroup* addRadioPair(Group& group, const QString& label, const QString& first, const QString& second, const char* name);
    QToolButton* makePressureButton(const QString& name, const QString& explanation, QWidget* parent);
    void sync();
    void showSizeLimit();
    void edit(const std::function<void(BrushSettings&)>& change);

    QPointer<ToolBox> m_tools;
    bool m_syncing = false;
    QSize m_canvas;

    // Size is special: 0.1 … 4000 px, along a logarithmic bar.
    QToolButton* m_sizePressure = nullptr;
    QLabel* m_sizeLabel = nullptr;
    ValueBar* m_sizeBar = nullptr;

    std::vector<std::unique_ptr<Group>> m_groups;
    std::vector<RowUi> m_rows;
    QWidget* m_tipDivider = nullptr;
    CollapsibleSection* m_tipSection = nullptr;
    BrushResourceSection* m_formSection = nullptr;
    BrushResourceSection* m_textureSection = nullptr;
    ElidedLabel* m_formSummary = nullptr;
    ElidedLabel* m_textureSummary = nullptr;
    QButtonGroup* m_edgeGroup = nullptr;
    QCheckBox* m_cloneAligned = nullptr;
    QCheckBox* m_curveInterpolation = nullptr;
    QCheckBox* m_disableDensityAssist = nullptr;
    QCheckBox* m_antiRipple = nullptr;
    QButtonGroup* m_cloneSample = nullptr;  // 0 Current Layer, 1 All Layers
    StabilizerCombo* m_stabilizer = nullptr;
    QPushButton* m_reserve = nullptr;
    QPushButton* m_recall = nullptr;
};

}
