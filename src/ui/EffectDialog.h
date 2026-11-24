#pragma once

#include "core/effects/EffectSettings.h"

#include <QDialog>

#include <vector>

class QCheckBox;
class QComboBox;
class QDialogButtonBox;
class QFormLayout;
class QProgressBar;

namespace doodlebold {

class ValueBar;

// An Effects menu entry's dialog: its Mode choice if it has one, a slider for each of the effect's values (a slider that
// belongs to one mode is greyed out in the others), its check box if it has one, Preview, Cancel and OK. It only collects the values; EffectController shows them on the layer and applies them. OK asks for the result
// (okRequested); while it is still being worked out the dialog locks its controls and shows how far it is.
class EffectDialog : public QDialog {
    Q_OBJECT
public:
    // `canvas`: the size of the canvas the effect is for; the sliders measured against it run from 0 to its width or height.
    EffectDialog(EffectKind kind, QSize canvas, QWidget* parent = nullptr);

    EffectSettings settings() const { return m_settings; }
    ValueBar* slider(int i) const { return m_sliders[static_cast<std::size_t>(i)]; }
    int sliderCount() const { return static_cast<int>(m_sliders.size()); }
    QCheckBox* optionBox() const { return m_option; }
    QComboBox* modeBox() const { return m_mode; }
    QCheckBox* previewBox() const { return m_preview; }
    bool isPreviewOn() const;
    // Sets slider i as dragging it would.
    void setValue(int i, double value);

    bool isApplying() const { return m_applying; }
    // progress 0 … 1; the first call locks the controls.
    void showApplying(double progress);

signals:
    void settingsChanged(const EffectSettings& settings);
    void previewToggled(bool on);
    void okRequested();

private:
    void addSlider(QFormLayout* form, int i, const EffectSlider& spec, QSize canvas);
    void enableForMode();

    EffectSettings m_settings;
    std::vector<ValueBar*> m_sliders;
    QCheckBox* m_option = nullptr;
    QComboBox* m_mode = nullptr;
    QCheckBox* m_preview = nullptr;
    QProgressBar* m_progress = nullptr;
    QDialogButtonBox* m_buttons = nullptr;
    bool m_applying = false;
};

}
