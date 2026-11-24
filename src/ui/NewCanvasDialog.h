#pragma once

#include "core/document/NewCanvas.h"

#include <QDialog>

#include <functional>
#include <optional>

class QComboBox;
class QGridLayout;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QToolButton;

namespace doodlebold {

class CanvasFieldsEditor : public QObject {
    Q_OBJECT
public:
    // With `allowNoBackground` the Background list starts with "[None]" (the Register New Canvas Preset dialog).
    CanvasFieldsEditor(const NewCanvasValues& values, bool allowNoBackground, QObject* parent = nullptr);

    int build(QGridLayout* grid, int firstRow, QWidget* parent);

    const NewCanvasModel& model() const { return m_model; }
    // The Background combo: nullopt while "[None]" is chosen (only with allowNoBackground).
    std::optional<CanvasBackground> backgroundChoice() const;
    void setBackgroundChoice(const std::optional<CanvasBackground>& choice);
    void applyPreset(const CanvasPreset& preset, int index);

    std::function<std::optional<QColor>(const QColor& current)> pickColor;

    QLineEdit* widthEdit() const { return m_width; }
    QLineEdit* heightEdit() const { return m_height; }
    QComboBox* sizeUnitCombo() const { return m_sizeUnit; }
    QToolButton* linkSizeButton() const { return m_linkSize; }
    QLineEdit* resolutionEdit() const { return m_resolution; }
    QComboBox* resolutionUnitCombo() const { return m_resolutionUnit; }
    QComboBox* backgroundCombo() const { return m_background; }
    QPushButton* swatch() const { return m_swatch; }
    QLabel* limitLabel() const { return m_limit; }

signals:
    // The user edited a field (not a preset choice): the preset combo goes back to "----".
    void edited();
    void changed();

private:
    void refreshFields();
    void refreshReadouts();
    void refreshSwatch();
    void onBackgroundComboChanged();
    void editNumber(QLineEdit* edit, void (NewCanvasModel::*setter)(double));
    void keepProportion(const QLineEdit* edited);
    void noteProportion();
    int comboIndexOf(std::optional<BackgroundMode> mode) const;

    NewCanvasModel m_model;
    bool m_allowNone;
    std::optional<CanvasBackground> m_choice;
    bool m_syncing = false;
    QLineEdit* m_width = nullptr;
    QLineEdit* m_height = nullptr;
    QComboBox* m_sizeUnit = nullptr;
    QToolButton* m_linkSize = nullptr;
    double m_proportion = 1.0;  // width / height while the link is on
    QLineEdit* m_resolution = nullptr;
    QComboBox* m_resolutionUnit = nullptr;
    QComboBox* m_background = nullptr;
    QPushButton* m_swatch = nullptr;
    QLabel* m_pixels = nullptr;
    QLabel* m_metrics = nullptr;
    QLabel* m_limit = nullptr;
};

class NewCanvasDialog : public QDialog {
    Q_OBJECT
public:
    NewCanvasDialog(const QString& fileName, const NewCanvasValues& values, CanvasPresetList presets, QWidget* parent = nullptr);

    NewCanvasAnswer answer() const;
    const CanvasPresetList& presets() const { return m_presets; }
    CanvasFieldsEditor* fields() const { return m_fields; }
    QComboBox* presetCombo() const { return m_presetCombo; }
    QToolButton* configureButton() const { return m_configure; }
    QToolButton* registerButton() const { return m_register; }
    QLineEdit* fileNameEdit() const { return m_fileName; }

    // Given, a canvas large enough to take a noticeable while is made by this while the dialog stays up saying so; it calls `done` once
    // the canvas's first picture is on screen, and the dialog then closes as accepted.
    std::function<void(const NewCanvasAnswer& answer, std::function<void()> done)> createWhileShowing;
    void accept() override;
    void reject() override;
    static bool takesAWhile(QSize pixels);

    std::function<std::optional<CanvasPreset>(const CanvasPreset& initial)> registerPreset;
    std::function<std::optional<CanvasPresetList>(const CanvasPresetList& current, const NewCanvasValues& values)> configurePresets;

signals:
    void presetsChanged(const doodlebold::CanvasPresetList& presets);

private:
    void showCreating();
    void fillPresetCombo(int selected);
    void onPresetChosen(int comboIndex);
    CanvasPreset presetFromFields() const;
    void openRegisterDialog();
    void openConfigureDialog();

    QString m_defaultName;
    CanvasPresetList m_presets;
    CanvasFieldsEditor* m_fields;
    bool m_creating = false;
    QLineEdit* m_fileName = nullptr;
    QComboBox* m_presetCombo = nullptr;
    QToolButton* m_configure = nullptr;
    QToolButton* m_register = nullptr;
};

class PresetEditDialog : public QDialog {
    Q_OBJECT
public:
    explicit PresetEditDialog(const CanvasPreset& initial, QWidget* parent = nullptr);

    CanvasPreset preset() const;
    QLineEdit* nameEdit() const { return m_name; }
    CanvasFieldsEditor* fields() const { return m_fields; }
    QPushButton* okButton() const { return m_ok; }

private:
    CanvasFieldsEditor* m_fields;
    QLineEdit* m_name = nullptr;
    QPushButton* m_ok = nullptr;
};

class PresetListDialog : public QDialog {
    Q_OBJECT
public:
    PresetListDialog(const CanvasPresetList& presets, const NewCanvasValues& current, QWidget* parent = nullptr);

    CanvasPresetList presets() const;
    QListWidget* list() const { return m_list; }
    QToolButton* createButton() const { return m_create; }
    QToolButton* separatorButton() const { return m_separator; }
    QToolButton* modifyButton() const { return m_modify; }
    QToolButton* deleteButton() const { return m_delete; }

    std::function<std::optional<CanvasPreset>(const CanvasPreset& initial)> editPreset;

private:
    void addItem(const CanvasPreset& preset, int row);
    int insertionRow() const;
    void updateButtons();

    NewCanvasValues m_current;
    CanvasPresetList m_data;
    QListWidget* m_list;
    QToolButton* m_create;
    QToolButton* m_separator;
    QToolButton* m_modify;
    QToolButton* m_delete;
};

}
