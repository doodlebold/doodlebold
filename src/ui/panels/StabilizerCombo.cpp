#include "ui/panels/StabilizerCombo.h"

#include <QSignalBlocker>

#include <algorithm>

namespace doodlebold {

StabilizerCombo::StabilizerCombo(bool allowGlobal, QWidget* parent) : QComboBox(parent), m_allowGlobal(allowGlobal) {
    if (allowGlobal)
        addItem(tr("Global"));
    for (int n = 0; n <= StabilizerLevel::kMax; ++n)
        addItem(StabilizerLevel{n}.label());
    setMaxVisibleItems(count());
    connect(this, &QComboBox::currentIndexChanged, this, [this] { emit levelChanged(level()); });
}

std::optional<StabilizerLevel> StabilizerCombo::level() const {
    int i = currentIndex();
    if (m_allowGlobal) {
        if (i <= 0)
            return std::nullopt;
        --i;
    }
    return StabilizerLevel{i};
}

void StabilizerCombo::setLevel(const std::optional<StabilizerLevel>& level) {
    int i = 0;
    if (level) {
        i = std::clamp(level->value, 0, StabilizerLevel::kMax);
        if (m_allowGlobal)
            ++i;
    }
    const QSignalBlocker block(this);
    setCurrentIndex(i);
}

}
