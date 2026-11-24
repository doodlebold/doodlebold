#pragma once

#include <QPointer>
#include <QToolBar>

#include <vector>

class QComboBox;
class QLabel;

namespace doodlebold {

class CanvasView;
class StabilizerCombo;
class ToolBox;

class QuickBar : public QToolBar {
    Q_OBJECT
public:
    struct Commands {
        QAction *undo = nullptr, *redo = nullptr;
        QAction *deselect = nullptr, *invertSelection = nullptr, *showSelection = nullptr;
        QAction *zoomOut = nullptr, *zoomIn = nullptr, *resetView = nullptr;
        QAction *rotateCcw = nullptr, *rotateCw = nullptr, *resetAngle = nullptr;
        QAction* flip = nullptr;
    };

    static const std::vector<double>& zoomFactors();
    static const std::vector<int>& angleSteps();

    explicit QuickBar(ToolBox* tools, QWidget* parent = nullptr);

    void populate(const Commands& commands);
    void setCanvas(CanvasView* canvas);

    QComboBox* zoomCombo() const { return m_zoom; }
    QComboBox* angleCombo() const { return m_angle; }
    StabilizerCombo* stabilizerCombo() const { return m_stabilizer; }
    QString zoomReadout() const;
    QString angleReadout() const;

private:
    void syncReadouts();
    void addButton(QAction* action, const QString& icon, const QString& tip);

    QPointer<CanvasView> m_canvas;
    QPointer<ToolBox> m_tools;
    QComboBox* m_zoom = nullptr;
    QComboBox* m_angle = nullptr;
    StabilizerCombo* m_stabilizer = nullptr;
    QMetaObject::Connection m_viewConnection;
};

}
