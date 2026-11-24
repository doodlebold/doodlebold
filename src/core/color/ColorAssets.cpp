#include "core/color/ColorAssets.h"

#include <QCoreApplication>
#include <QJsonArray>

#include <algorithm>

namespace doodlebold {

namespace {

const char* modeKey(color::SliderMode m) {
    switch (m) {
    case color::SliderMode::Hsv: return "hsv";
    case color::SliderMode::Hsl: return "hsl";
    }
    return "hsv";
}

const char* sizeKey(SwatchSize s) {
    switch (s) {
    case SwatchSize::Small: return "small";
    case SwatchSize::Middle: return "middle";
    case SwatchSize::Large: return "large";
    }
    return "large";
}

QJsonValue colourJson(const std::optional<QColor>& c) {
    return c ? QJsonValue(c->name()) : QJsonValue();
}

std::optional<QColor> colourFrom(const QJsonValue& v) {
    if (!v.isString())
        return std::nullopt;
    const QColor c(v.toString());
    return c.isValid() ? std::optional<QColor>(c) : std::nullopt;
}

}

int swatchCellPixels(SwatchSize size) {
    switch (size) {
    case SwatchSize::Small: return 9;
    case SwatchSize::Middle: return 12;
    case SwatchSize::Large: return 15;
    }
    return 15;
}

ColorAssets::ColorAssets(QObject* parent) : QObject(parent) {
    m_sets.push_back(SwatchSet{tr("Swatch1")});
}

void ColorAssets::touched(void (ColorAssets::*signal)()) {
    emit(this->*signal)();
    emit modified();
}

void ColorAssets::addToHistory(const QColor& color) {
    if (!color.isValid() || color.alpha() == 0)
        return;
    const QColor c = QColor(color.red(), color.green(), color.blue());
    if (!m_history.empty() && m_history.front() == c)
        return;
    std::erase(m_history, c);
    m_history.insert(m_history.begin(), c);
    if (int(m_history.size()) > kHistorySize)
        m_history.resize(kHistorySize);
    touched(&ColorAssets::historyChanged);
}

void ColorAssets::setCurrentSwatchSet(int index) {
    if (index < 0 || index >= swatchSetCount() || index == m_currentSet)
        return;
    m_currentSet = index;
    touched(&ColorAssets::swatchesChanged);
}

void ColorAssets::setSwatch(int cell, const std::optional<QColor>& color) {
    auto& cells = m_sets[std::size_t(m_currentSet)].cells;
    if (cell < 0 || cell >= int(cells.size()) || cells[std::size_t(cell)] == color)
        return;
    cells[std::size_t(cell)] = color ? std::optional<QColor>(QColor(color->red(), color->green(), color->blue())) : color;
    touched(&ColorAssets::swatchesChanged);
}

QString ColorAssets::uniqueName(const QString& stem, const std::vector<QString>& taken) const {
    for (int n = 1;; ++n) {
        const QString name = stem + QString::number(n);
        if (std::ranges::find(taken, name) == taken.end())
            return name;
    }
}

int ColorAssets::addSwatchSet() {
    std::vector<QString> names;
    names.reserve(m_sets.size());
    for (const SwatchSet& s : m_sets)
        names.push_back(s.name);
    m_sets.push_back(SwatchSet{uniqueName(tr("Swatch"), names)});
    m_currentSet = swatchSetCount() - 1;
    touched(&ColorAssets::swatchesChanged);
    return m_currentSet;
}

bool ColorAssets::removeSwatchSet(int index) {
    if (swatchSetCount() <= 1 || index < 0 || index >= swatchSetCount())
        return false;
    m_sets.erase(m_sets.begin() + index);
    m_currentSet = std::clamp(m_currentSet > index ? m_currentSet - 1 : m_currentSet, 0, swatchSetCount() - 1);
    touched(&ColorAssets::swatchesChanged);
    return true;
}

void ColorAssets::renameSwatchSet(int index, const QString& name) {
    if (index < 0 || index >= swatchSetCount() || name.trimmed().isEmpty())
        return;
    m_sets[std::size_t(index)].name = name.trimmed();
    touched(&ColorAssets::swatchesChanged);
}

void ColorAssets::setSliderMode(color::SliderMode mode) {
    if (mode == m_mode)
        return;
    m_mode = mode;
    touched(&ColorAssets::optionsChanged);
}

void ColorAssets::setSwatchSize(SwatchSize size) {
    if (size == m_swatchSize)
        return;
    m_swatchSize = size;
    touched(&ColorAssets::optionsChanged);
}

QJsonObject ColorAssets::toJson() const {
    QJsonArray history;
    for (const QColor& c : m_history)
        history.append(c.name());
    QJsonArray sets;
    for (const SwatchSet& s : m_sets) {
        QJsonArray cells;
        for (const auto& c : s.cells)
            cells.append(colourJson(c));
        sets.append(QJsonObject{{"name", s.name}, {"cells", cells}});
    }
    return {{"version", 1},
            {"history", history},
            {"swatchSets", sets},
            {"currentSwatchSet", m_currentSet},
            {"sliderMode", QLatin1String(modeKey(m_mode))},
            {"swatchSize", QLatin1String(sizeKey(m_swatchSize))}};
}

void ColorAssets::fromJson(const QJsonObject& json) {
    m_history.clear();
    for (const auto& v : json.value("history").toArray())
        if (const auto c = colourFrom(v); c && int(m_history.size()) < kHistorySize)
            m_history.push_back(*c);

    std::vector<SwatchSet> sets;
    for (const auto& v : json.value("swatchSets").toArray()) {
        const QJsonObject o = v.toObject();
        SwatchSet s{o.value("name").toString()};
        const QJsonArray cells = o.value("cells").toArray();
        for (int i = 0; i < std::min(int(cells.size()), SwatchSet::kCells); ++i)
            s.cells[std::size_t(i)] = colourFrom(cells[i]);
        if (s.name.trimmed().isEmpty())
            s.name = tr("Swatch%1").arg(sets.size() + 1);
        sets.push_back(std::move(s));
    }
    if (!sets.empty())
        m_sets = std::move(sets);
    m_currentSet = std::clamp(json.value("currentSwatchSet").toInt(), 0, swatchSetCount() - 1);

    // Settings from before the scratch pad was removed still hold its entries ("scratchPads", "padBrushSize", …): they
    // are passed over, and dropped when the settings are next saved. The pads' pictures stay on disk, untouched.

    const QString mode = json.value("sliderMode").toString();
    for (color::SliderMode m : {color::SliderMode::Hsv, color::SliderMode::Hsl})
        if (mode == QLatin1String(modeKey(m)))
            m_mode = m;
    const QString size = json.value("swatchSize").toString();
    for (SwatchSize s : {SwatchSize::Small, SwatchSize::Middle, SwatchSize::Large})
        if (size == QLatin1String(sizeKey(s)))
            m_swatchSize = s;

    emit historyChanged();
    emit swatchesChanged();
    emit optionsChanged();
}

}
