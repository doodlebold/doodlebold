#pragma once

#include "core/color/ColorModels.h"

#include <QColor>
#include <QJsonObject>
#include <QObject>
#include <QString>

#include <array>
#include <optional>
#include <vector>

namespace doodlebold {

enum class SwatchSize {
    Small,
    Middle,
    Large
};
int swatchCellPixels(SwatchSize size);

struct SwatchSet {
    static constexpr int kCells = 256;
    QString name;
    std::vector<std::optional<QColor>> cells = std::vector<std::optional<QColor>>(kCells);
    bool operator==(const SwatchSet&) const = default;
};

class ColorAssets : public QObject {
    Q_OBJECT
public:
    static constexpr int kHistorySize = 32;

    explicit ColorAssets(QObject* parent = nullptr);

    // History: newest first, no repeats.
    const std::vector<QColor>& history() const { return m_history; }
    void addToHistory(const QColor& color);

    int swatchSetCount() const { return int(m_sets.size()); }
    const SwatchSet& swatchSet(int index) const { return m_sets.at(std::size_t(index)); }
    int currentSwatchSet() const { return m_currentSet; }
    const SwatchSet& currentSet() const { return swatchSet(m_currentSet); }
    void setCurrentSwatchSet(int index);
    void setSwatch(int cell, const std::optional<QColor>& color);
    int addSwatchSet();
    bool removeSwatchSet(int index);                            // the last set stays
    void renameSwatchSet(int index, const QString& name);

    color::SliderMode sliderMode() const { return m_mode; }
    void setSliderMode(color::SliderMode mode);
    SwatchSize swatchSize() const { return m_swatchSize; }
    void setSwatchSize(SwatchSize size);

    QJsonObject toJson() const;
    void fromJson(const QJsonObject& json);

signals:
    void historyChanged();
    void swatchesChanged();
    void optionsChanged();
    void modified();

private:
    QString uniqueName(const QString& stem, const std::vector<QString>& taken) const;
    void touched(void (ColorAssets::*signal)());

    std::vector<QColor> m_history;
    std::vector<SwatchSet> m_sets;
    int m_currentSet = 0;
    color::SliderMode m_mode = color::SliderMode::Hsv;
    SwatchSize m_swatchSize = SwatchSize::Large;
};

}
