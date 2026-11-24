#pragma once

#include "ui/Theme.h"

#include <QGridLayout>
#include <QLabel>

#include <algorithm>
#include <vector>

namespace doodlebold {

// A label column of the same width in every group, box and section, so the value bars start at one x all the way down.
inline constexpr int kParameterColumnGap = Theme::kPanelColumnGap;
inline constexpr int kParameterRowGap = Theme::kPanelRowGap;
inline constexpr int kParameterLabelColumn = 84;

inline void alignLabelColumns(const std::vector<QGridLayout*>& grids, int column) {
    int width = kParameterLabelColumn;
    for (const QGridLayout* grid : grids)
        for (int row = 0; row < grid->rowCount(); ++row)
            if (const QLayoutItem* item = grid->itemAtPosition(row, column))
                if (const auto* label = qobject_cast<const QLabel*>(item->widget()))
                    width = std::max(width, label->sizeHint().width());
    for (QGridLayout* grid : grids)
        grid->setColumnMinimumWidth(column, width);
}

}
