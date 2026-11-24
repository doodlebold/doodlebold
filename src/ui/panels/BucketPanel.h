#pragma once

#include "core/brush/BrushSettings.h"

#include <QPointer>
#include <QWidget>

#include <functional>

class QButtonGroup;
class QCheckBox;

namespace doodlebold {

class ToolBox;
class ValueBar;

// The Bucket's panel: how it finds the area (Selection Mode, Color Threshold, Gap Tolerance), where it looks (Selection
// Source), Anti-Aliasing and Ignore Existing Selection. It edits the current tray tool's own settings.
class BucketPanel : public QWidget {
    Q_OBJECT
public:
    explicit BucketPanel(ToolBox* tools, QWidget* parent = nullptr);

private:
    void sync();
    void edit(const std::function<void(BucketSettings&)>& change);

    QPointer<ToolBox> m_tools;
    bool m_syncing = false;
    QButtonGroup* m_modes = nullptr;
    ValueBar* m_threshold = nullptr;
    ValueBar* m_gap = nullptr;
    QButtonGroup* m_source = nullptr;
    QCheckBox* m_antialias = nullptr;
    QCheckBox* m_ignoreSelection = nullptr;
};

}
