#pragma once

#include "core/tools/FixedTool.h"

#include <QPointer>
#include <QWidget>

#include <map>

class QScrollArea;
class QStackedWidget;
class QToolButton;

namespace doodlebold {

class BrushPreviewStrip;
class BucketPanel;
class BrushResourceLibrary;
class ColorState;
class FixedToolPanels;
class RowHandle;
class ToolBox;
class ToolParameters;
class ToolTray;

class ToolPanel : public QWidget {
    Q_OBJECT
public:
    explicit ToolPanel(ToolBox* tools, BrushResourceLibrary* resources = nullptr, QWidget* parent = nullptr);

    ToolTray* tray() const { return m_tray; }
    BrushPreviewStrip* preview() const { return m_preview; }
    ToolParameters* parameters() const { return m_parameters; }
    FixedToolPanels* fixedPanels() const { return m_fixedPanels; }
    BucketPanel* bucketPanel() const { return m_bucket; }
    QToolButton* fixedToolButton(FixedTool tool) const { return m_fixedButtons.at(tool); }
    // The colour wheel's state, whose hue the preview's sides follow, black, white and grey included.
    void setColorState(ColorState* state);

signals:
    void refused(const QString& message);

private:
    void updatePreview();
    void syncActiveTool();
    void startTrayDrag();
    void dragTray(int dy);

    QPointer<ToolBox> m_tools;
    QPointer<ColorState> m_colorState;
    ToolTray* m_tray = nullptr;
    BrushPreviewStrip* m_preview = nullptr;
    ToolParameters* m_parameters = nullptr;
    FixedToolPanels* m_fixedPanels = nullptr;
    BucketPanel* m_bucket = nullptr;
    QStackedWidget* m_options = nullptr;
    QScrollArea* m_optionsScroll = nullptr;
    RowHandle* m_trayHandle = nullptr;
    struct {
        int rows = 0;  // shown when the drag started
        int most = 0;
    } m_trayDrag;
    std::map<FixedTool, QToolButton*> m_fixedButtons;
};

}
