#pragma once

#include "core/brush/BrushSettings.h"

#include <QComboBox>

#include <optional>

namespace doodlebold {

// With `allowGlobal` the first entry is "Global" (no level of its own), for the per-tool override; without it a level
// is always chosen, for the global setting.
class StabilizerCombo : public QComboBox {
    Q_OBJECT
public:
    explicit StabilizerCombo(bool allowGlobal, QWidget* parent = nullptr);

    std::optional<StabilizerLevel> level() const;
    void setLevel(const std::optional<StabilizerLevel>& level);

signals:
    void levelChanged(const std::optional<StabilizerLevel>& level);

private:
    bool m_allowGlobal;
};

}
