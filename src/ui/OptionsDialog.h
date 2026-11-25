#pragma once

#include "core/options/AppOptions.h"

#include <QDialog>

class QDoubleSpinBox;
class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QSpinBox;
class QStackedWidget;
class QVBoxLayout;

namespace doodlebold {

class BrushResourceLibrary;
class KeysPage;
class ToolBox;

class OptionsDialog : public QDialog {
    Q_OBJECT
public:
    explicit OptionsDialog(const AppOptions::Values& initial, QWidget* parent = nullptr);

    AppOptions::Values values() const;
    // Options > Resources, before the Timelapse page. Its changes take effect at once, not on OK.
    void addResourcesPage(BrushResourceLibrary* library, ToolBox* tools = nullptr);
    // Options › Keys: the caller gives it the keys of the commands and the tray, and reads them back on OK.
    KeysPage* keysPage() const { return m_keys; }
    // What the graphics chose for the current canvas, shown while "Display when zoomed out" is left to them (Smooth if not told).
    void showGraphicsChoice(bool fast);
    // Opens on Options › Performance, scrolled to its Memory section (from the notice of a stroke too large).
    void showMemorySection();

private:
    QWidget* penTabletPage();
    QWidget* environmentPage();
    QWidget* preferencePage();
    QWidget* timelapsePage();
    QWidget* drawingPage();
    QWidget* recoveryPage();
    void enableRecoveryControls(bool on);
    void buildMemorySection(QVBoxLayout* page);
    void showMemoryAmount();
    void showMemoryInUse();
    QWidget* buildKeysPage(const AppOptions::Values& v);

    QListWidget* m_pages = nullptr;
    QStackedWidget* m_stack = nullptr;
    QSpinBox* m_minimumPressure = nullptr;
    QSpinBox* m_fullPressure = nullptr;
    QCheckBox* m_frostedMenus = nullptr;
    QComboBox* m_zoomedOut = nullptr;
    // Left to the graphics, the combo shows what they chose, and only a change of it is the user's pick.
    AppOptions::ZoomedOut m_zoomedOutGiven = AppOptions::ZoomedOut::ByGraphics;
    int m_zoomedOutShown = 0;
    QSpinBox* m_undoSteps = nullptr;
    QCheckBox* m_brushOutline = nullptr;
    QCheckBox* m_strokeTiming = nullptr;
    QWidget* m_memorySection = nullptr;
    QComboBox* m_memory = nullptr;
    QDoubleSpinBox* m_memoryGB = nullptr;
    double m_memoryGBGiven = 0.0;  // the custom amount it was given, kept while Automatic is picked
    double m_memoryGBCustom = 0.0;  // what the box shows for Custom
    QLabel* m_memoryInUse = nullptr;
    QCheckBox* m_keepBrushesInMemory = nullptr;
    QCheckBox* m_fitOnReset = nullptr;
    QCheckBox* m_swapWheel = nullptr;
    QCheckBox* m_wheelAtCentre = nullptr;
    QCheckBox* m_checkForUpdates = nullptr;
    QCheckBox* m_recordNewCanvases = nullptr;
    QCheckBox* m_quickShapes = nullptr;
    QDoubleSpinBox* m_quickShapeHold = nullptr;
    QCheckBox* m_saveRecoveryCopies = nullptr;
    QSpinBox* m_recoveryMinutes = nullptr;
    QSpinBox* m_recoveryCopiesKept = nullptr;
    QLineEdit* m_recoveryFolder = nullptr;
    QPushButton* m_recoveryBrowse = nullptr;
    KeysPage* m_keys = nullptr;
};

}
