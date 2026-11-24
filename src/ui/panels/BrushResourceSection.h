#pragma once

#include "core/brush/BrushSettings.h"
#include "ui/panels/BrushResourceList.h"

#include <QPointer>
#include <QWidget>

#include <functional>

class QCheckBox;
class QComboBox;
class QLabel;
class QPushButton;

namespace doodlebold {

class ValueBar;

class BrushResourceSection : public QWidget {
    Q_OBJECT
public:
    using Edit = std::function<void(const std::function<void(BrushSettings&)>&)>;

    BrushResourceSection(BrushResourceLibrary* library, BrushResourceList::Content content, Edit edit, QWidget* parent = nullptr);

    void showSettings(const BrushSettings& s);
    BrushResourceList* list() const { return m_list; }

signals:
    void refused(const QString& message);

private:
    void applyEnabled();
    ResourceKind formKind() const;
    void applyKind();
    bool forms() const { return m_content == BrushResourceList::Content::Forms; }
    void askToReset();

    BrushResourceList::Content m_content;
    QPointer<BrushResourceLibrary> m_library;
    Edit m_edit;
    bool m_showing = false;
    BrushResourceList* m_list = nullptr;
    QLabel* m_strengthLabel = nullptr;
    ValueBar* m_strength = nullptr;
    QWidget* m_body = nullptr;
    QWidget* m_mainBody = nullptr;
    QWidget* m_bristleBody = nullptr;
    QWidget* m_shapeBody = nullptr;
    ValueBar* m_scale = nullptr;
    QLabel* m_scaleLabel = nullptr;
    QCheckBox* m_invert = nullptr;
    QCheckBox* m_eraseLow = nullptr;  // textures only
    ValueBar* m_scratch = nullptr;  // Scratch Prs., textures only
    QCheckBox* m_formEraseLow = nullptr;  // the tip shape's Invert for Transparency, forms only
    QCheckBox* m_sharpen = nullptr;  // Sharpen, forms only
    ValueBar* m_bristleMinimum = nullptr;
    ValueBar* m_bristleRandomize = nullptr;
    QComboBox* m_bristleDirection = nullptr;
    QCheckBox* m_bristleDetectTurn = nullptr;
    QComboBox* m_shapeAngleControl = nullptr;
    ValueBar* m_shapeAngle = nullptr;
    ValueBar* m_shapeAngleJitter = nullptr;
    ValueBar* m_shapeScaling = nullptr;
    ValueBar* m_shapeSizeJitter = nullptr;
    ValueBar* m_shapeSpacing = nullptr;
    ValueBar* m_shapeScattering = nullptr;
    QCheckBox* m_shapeHighQuality = nullptr;
    QPushButton* m_reset = nullptr;  // the box's own settings back to the tool's start, after asking
};

}
