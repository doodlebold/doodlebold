#include "core/tools/ToolBox.h"

#include "core/tools/SizeList.h"

#include <QCoreApplication>

#include <algorithm>
#include <iterator>
#include <utility>

namespace doodlebold {

namespace {

// The picture a tool paints with from one of the two lists: the forms, or the textures.
QString& resourceName(BrushSettings& s, bool form) {
    return form ? s.form : s.texture;
}

const QString& resourceName(const BrushSettings& s, bool form) {
    return form ? s.form : s.texture;
}

// The tray always ends with a whole row of empty cells (so there is somewhere to create a tool) and has at least
constexpr int kMinRows = 2;

QString tr(const char* text) {
    return QCoreApplication::translate("ToolBox", text);
}

ToolSlot makeSlot(ToolType type) {
    return ToolSlot{toolTypeName(type), defaultSettings(type), std::nullopt, {}};
}

void normalizeGroup(ToolGroup& g) {
    int last = -1;
    for (int i = 0; i < int(g.cells.size()); ++i)
        if (g.cells[std::size_t(i)])
            last = i;
    const int rows = std::max(kMinRows, last / ToolBox::kColumns + 2);
    g.cells.resize(std::size_t(rows * ToolBox::kColumns));
}

bool sameOptions(const BrushOptions& a, const BrushOptions& b) {
    return a.minimumPressure == b.minimumPressure && a.stabilizer == b.stabilizer;
}

StabilizerLevel clampedLevel(StabilizerLevel l) {
    l.value = std::clamp(l.value, 0, StabilizerLevel::kMax);
    return l;
}

template <typename F> void forEachSlot(const ToolBoxState& s, F&& f) {
    for (int g = 0; g < int(s.groups.size()); ++g)
        for (int i = 0; i < int(s.groups[std::size_t(g)].cells.size()); ++i)
            if (s.groups[std::size_t(g)].cells[std::size_t(i)])
                f(SlotRef{g, i});
}

SlotRef firstSlot(const ToolBoxState& s) {
    SlotRef first{-1, -1};
    forEachSlot(s, [&first](SlotRef r) {
        if (first.group < 0)
            first = r;
    });
    return first;
}

int slotCount(const ToolBoxState& s) {
    int n = 0;
    forEachSlot(s, [&n](SlotRef) { ++n; });
    return n;
}

}

QString describe(ToolBoxError e) {
    switch (e) {
    case ToolBoxError::NoSuchSlot: return tr("There is no such tool slot.");
    case ToolBoxError::SlotOccupied: return tr("That slot already holds a tool.");
    case ToolBoxError::SlotEmpty: return tr("That slot is empty.");
    case ToolBoxError::LastTool: return tr("The last tool cannot be deleted.");
    case ToolBoxError::InvalidName: return tr("A tool needs a name.");
    case ToolBoxError::NothingReserved: return tr("No settings have been reserved for this tool.");
    }
    return {};
}

ToolBoxState defaultToolBoxState() {
    ToolBoxState s;
    ToolGroup basic{tr("Basic"), {}};
    // The Bucket sits where the Blur was, and the Blur comes after the Clone Stamp.
    for (ToolType t : {ToolType::Pencil, ToolType::Brush, ToolType::WaterColor, ToolType::Marker, ToolType::Eraser, ToolType::Bucket,
                       ToolType::Smudge, ToolType::PixelArtPen, ToolType::Clone, ToolType::Blur})
        basic.cells.emplace_back(makeSlot(t));
    normalizeGroup(basic);
    ToolGroup custom{tr("Custom"), {}};
    normalizeGroup(custom);
    s.groups = {std::move(basic), std::move(custom)};
    s.current = {0, 0};
    return s;
}

namespace {

std::optional<ToolSlot>& cellAt(ToolBoxState& s, SlotRef r) {
    return s.groups[std::size_t(r.group)].cells[std::size_t(r.index)];
}

std::optional<SlotRef> firstOfType(const ToolBoxState& s, ToolType type) {
    std::optional<SlotRef> found;
    forEachSlot(s, [&](SlotRef r) {
        if (!found && s.groups[std::size_t(r.group)].cells[std::size_t(r.index)]->type() == type)
            found = r;
    });
    return found;
}

// The cell after the group's last tool, made if the group has none spare.
SlotRef cellAfterLastTool(ToolBoxState& s, int group) {
    auto& cells = s.groups[std::size_t(group)].cells;
    int last = -1;
    for (int i = 0; i < int(cells.size()); ++i)
        if (cells[std::size_t(i)])
            last = i;
    if (last + 1 >= int(cells.size()))
        cells.resize(std::size_t(last + 2));
    return {group, last + 1};
}

// Exchanges two cells' tools, and the current tool follows its own.
void swapCells(ToolBoxState& s, SlotRef a, SlotRef b) {
    std::swap(cellAt(s, a), cellAt(s, b));
    if (s.current == a)
        s.current = b;
    else if (s.current == b)
        s.current = a;
}

}

void placeBucket(ToolBoxState& state) {
    if (slotCount(state) == 0)
        return;  // setState gives it the default tools, the Bucket among them
    const std::optional<SlotRef> blur = firstOfType(state, ToolType::Blur);
    const std::optional<SlotRef> bucket = firstOfType(state, ToolType::Bucket);
    if (bucket && blur) {
        swapCells(state, *bucket, *blur);
    } else if (!bucket && blur) {
        const SlotRef after = cellAfterLastTool(state, blur->group);
        cellAt(state, after) = makeSlot(ToolType::Bucket);
        swapCells(state, after, *blur);
    } else if (!bucket) {
        const auto& cells = state.groups.front().cells;
        const auto empty = std::ranges::find(cells, std::optional<ToolSlot>{});
        const SlotRef cell = empty != cells.end() ? SlotRef{0, int(empty - cells.begin())} : cellAfterLastTool(state, 0);
        cellAt(state, cell) = makeSlot(ToolType::Bucket);
    }
}

ToolBox::ToolBox(QObject* parent) : QObject(parent), m_state(defaultToolBoxState()) {}

void ToolBox::setSettings(const BrushSettings& s) {
    ToolSlot& slot = *slotMutable(m_state.current);
    BrushSettings c = s;
    c.tool = slot.type();
    c = c.clamped();
    if (c == slot.settings)
        return;
    slot.settings = c;
    emit settingsChanged();
    emit modified();
}

void ToolBox::setColor(const QColor& c) {
    if (!c.isValid())
        return;
    setTransparent(false);
    if (c == m_state.color)
        return;
    m_state.color = c;
    emit colorChanged(c);
    emit modified();
}

void ToolBox::setSecondaryColor(const QColor& c) {
    if (!c.isValid() || c == m_state.secondaryColor)
        return;
    m_state.secondaryColor = c;
    emit secondaryColorChanged(c);
    emit modified();
}

void ToolBox::setTransparent(bool on) {
    if (on == m_transparent)
        return;
    m_transparent = on;
    emit transparentChanged(on);
}

void ToolBox::exchangeColors() {
    setTransparent(false);
    if (m_state.color == m_state.secondaryColor)
        return;
    std::swap(m_state.color, m_state.secondaryColor);
    emit colorChanged(m_state.color);
    emit secondaryColorChanged(m_state.secondaryColor);
    emit modified();
}

void ToolBox::setOptions(const BrushOptions& o) {
    BrushOptions fixed = o;
    fixed.minimumPressure = std::clamp(o.minimumPressure, 0.0, 1.0);
    fixed.stabilizer = clampedLevel(o.stabilizer);
    if (sameOptions(fixed, m_state.options))
        return;
    m_state.options = fixed;
    emit optionsChanged();
    emit modified();
}

void ToolBox::setFixedTool(std::optional<FixedTool> tool) {
    if (tool == m_fixedTool)
        return;
    m_fixedTool = tool;
    emit activeToolChanged();
}

void ToolBox::setFixedOptions(const FixedToolOptions& o) {
    const FixedToolOptions c = o.clamped();
    if (c == m_state.fixed)
        return;
    m_state.fixed = c;
    emit fixedOptionsChanged();
    emit modified();
}

const ToolSlot* ToolBox::slot(SlotRef ref) const {
    if (ref.group < 0 || ref.group >= groupCount())
        return nullptr;
    const auto& cells = m_state.groups[std::size_t(ref.group)].cells;
    if (ref.index < 0 || ref.index >= int(cells.size()))
        return nullptr;
    const auto& cell = cells[std::size_t(ref.index)];
    return cell ? &*cell : nullptr;
}

int ToolBox::toolCount() const {
    return slotCount(m_state);
}

ToolSlot* ToolBox::slotMutable(SlotRef ref) {
    return const_cast<ToolSlot*>(std::as_const(*this).slot(ref));
}

void ToolBox::setCurrent(SlotRef ref) {
    const bool wasFixed = m_fixedTool.has_value();
    m_fixedTool.reset();  // picking a tray tool ends the fixed tool, even when it is the slot already current
    if (ref == m_state.current) {
        if (wasFixed)
            emit activeToolChanged();
        return;
    }
    const BrushSettings before = settings();
    m_state.current = ref;
    emit currentSlotChanged();
    if (!(settings() == before))
        emit settingsChanged();
    if (wasFixed)
        emit activeToolChanged();
    emit modified();
}

ToolResult<> ToolBox::selectSlot(SlotRef ref) {
    if (ref.group < 0 || ref.group >= groupCount() || ref.index < 0 ||
        ref.index >= int(m_state.groups[std::size_t(ref.group)].cells.size()))
        return ToolResult<>::failure(ToolBoxError::NoSuchSlot);
    if (!slot(ref))
        return ToolResult<>::failure(ToolBoxError::SlotEmpty);
    setCurrent(ref);
    return {};
}

bool ToolBox::selectToolOfType(ToolType type) {
    return selectToolOfTypes({type});
}

bool ToolBox::selectToolOfTypes(std::initializer_list<ToolType> types) {
    const auto wanted = [&types](ToolType t) { return std::ranges::find(types, t) != types.end(); };
    std::vector<SlotRef> matches;
    forEachSlot(m_state, [&](SlotRef r) {
        if (wanted(slot(r)->type()))
            matches.push_back(r);
    });
    if (matches.empty())
        return false;
    SlotRef target = matches.front();
    if (wanted(currentTool().type())) {
        const auto it = std::ranges::find(matches, m_state.current);
        // With a fixed tool active the key first returns to the current tray tool; pressed again it cycles.
        target = m_fixedTool ? m_state.current : *(std::next(it) != matches.end() ? std::next(it) : matches.begin());
    }
    setCurrent(target);
    return true;
}

void ToolBox::selectOrCreateToolOfType(ToolType type) {
    if (selectToolOfType(type))
        return;
    const ToolGroup& g = m_state.groups[std::size_t(m_state.current.group)];
    const auto empty = std::ranges::find_if(g.cells, [](const auto& c) { return !c; });
    if (empty == g.cells.end())
        return;  // cannot happen: a group always ends with an empty row
    if (const auto made = createSlot({m_state.current.group, int(empty - g.cells.begin())}, type))
        (void)selectSlot(made.value());
}

ToolResult<SlotRef> ToolBox::createSlot(SlotRef at, ToolType type) {
    if (at.group < 0 || at.group >= groupCount() || at.index < 0 || at.index >= int(m_state.groups[std::size_t(at.group)].cells.size()))
        return ToolResult<SlotRef>::failure(ToolBoxError::NoSuchSlot);
    if (slot(at))
        return ToolResult<SlotRef>::failure(ToolBoxError::SlotOccupied);
    ToolGroup& g = m_state.groups[std::size_t(at.group)];
    g.cells[std::size_t(at.index)] = makeSlot(type);
    normalizeGroup(g);
    emit layoutChanged();
    emit modified();
    return at;
}

ToolResult<SlotRef> ToolBox::duplicateSlot(SlotRef ref) {
    const ToolSlot* source = slot(ref);
    if (!source)
        return ToolResult<SlotRef>::failure(ref.group >= 0 && ref.group < groupCount() ? ToolBoxError::SlotEmpty
                                                                                       : ToolBoxError::NoSuchSlot);
    ToolSlot copy = *source;
    copy.name = tr("%1 copy").arg(source->name);
    copy.key.clear();  // a key picks one tool
    ToolGroup& g = m_state.groups[std::size_t(ref.group)];
    // The tray always ends with an empty row, so an empty cell exists; search from the source, then from the start.
    auto empty = std::find_if(g.cells.begin() + ref.index, g.cells.end(), [](const auto& c) { return !c; });
    if (empty == g.cells.end())
        empty = std::ranges::find_if(g.cells, [](const auto& c) { return !c; });
    const SlotRef at{ref.group, int(empty - g.cells.begin())};
    *empty = std::move(copy);
    normalizeGroup(g);
    emit layoutChanged();
    emit modified();
    return at;
}

ToolResult<> ToolBox::deleteSlot(SlotRef ref) {
    if (!slot(ref))
        return ToolResult<>::failure(ref.group >= 0 && ref.group < groupCount() ? ToolBoxError::SlotEmpty : ToolBoxError::NoSuchSlot);
    if (slotCount(m_state) <= 1)
        return ToolResult<>::failure(ToolBoxError::LastTool);
    const BrushSettings before = settings();
    const bool wasCurrent = ref == m_state.current;
    ToolGroup& g = m_state.groups[std::size_t(ref.group)];
    g.cells[std::size_t(ref.index)].reset();
    normalizeGroup(g);

    if (wasCurrent) {
        // The nearest tool after the deleted one in its group, else before it, else the first tool of the tray.
        SlotRef next{-1, -1};
        for (int i = ref.index + 1; i < int(g.cells.size()) && next.group < 0; ++i)
            if (g.cells[std::size_t(i)])
                next = {ref.group, i};
        for (int i = ref.index - 1; i >= 0 && next.group < 0; --i)
            if (g.cells[std::size_t(i)])
                next = {ref.group, i};
        if (next.group < 0)
            next = firstSlot(m_state);
        m_state.current = next;
        emit currentSlotChanged();
        if (!(settings() == before))
            emit settingsChanged();
    }
    emit layoutChanged();
    emit modified();
    return {};
}

ToolResult<> ToolBox::renameSlot(SlotRef ref, const QString& name) {
    ToolSlot* s = slotMutable(ref);
    if (!s)
        return ToolResult<>::failure(ref.group >= 0 && ref.group < groupCount() ? ToolBoxError::SlotEmpty : ToolBoxError::NoSuchSlot);
    const QString trimmed = name.trimmed();
    if (trimmed.isEmpty())
        return ToolResult<>::failure(ToolBoxError::InvalidName);
    if (trimmed == s->name)
        return {};
    s->name = trimmed;
    emit layoutChanged();
    emit modified();
    return {};
}

int ToolBox::renameResource(bool form, const QString& from, const QString& to) {
    return replaceResource(form, from, [&to](ToolType) { return to; });
}

QStringList ToolBox::toolsUsingResource(bool form, const QString& name) const {
    QStringList tools;
    for (const ToolGroup& g : m_state.groups)
        for (const std::optional<ToolSlot>& slot : g.cells)
            if (slot && resourceName(slot->settings, form) == name)
                tools << slot->name;
    return tools;
}

int ToolBox::resetResource(bool form, const QString& name) {
    return replaceResource(form, name, [form](ToolType type) { return resourceName(defaultSettings(type), form); });
}

int ToolBox::replaceResource(bool form, const QString& from, const std::function<QString(ToolType)>& to) {
    const auto follow = [&](BrushSettings& s) {
        QString& name = resourceName(s, form);
        if (name != from)
            return false;
        name = to(s.tool);
        return true;
    };
    int tools = 0;
    bool current = false, any = false;
    for (int g = 0; g < groupCount(); ++g)
        for (int i = 0; i < int(group(g).cells.size()); ++i) {
            ToolSlot* slot = slotMutable({g, i});
            if (!slot)
                continue;
            const bool moved = follow(slot->settings);
            const bool kept = slot->reserved && follow(*slot->reserved);
            tools += moved ? 1 : 0;
            any = any || moved || kept;
            current = current || (moved && SlotRef{g, i} == m_state.current);
        }
    if (current)
        emit settingsChanged();
    if (any)
        emit modified();
    return tools;
}

ToolResult<> ToolBox::setSlotKey(SlotRef ref, const QString& key) {
    ToolSlot* s = slotMutable(ref);
    if (!s)
        return ToolResult<>::failure(ref.group >= 0 && ref.group < groupCount() ? ToolBoxError::SlotEmpty : ToolBoxError::NoSuchSlot);
    if (key == s->key)
        return {};
    s->key = key;
    emit layoutChanged();
    emit modified();
    return {};
}

ToolResult<> ToolBox::initializeSlot(SlotRef ref) {
    ToolSlot* s = slotMutable(ref);
    if (!s)
        return ToolResult<>::failure(ref.group >= 0 && ref.group < groupCount() ? ToolBoxError::SlotEmpty : ToolBoxError::NoSuchSlot);
    s->settings = defaultSettings(s->type());
    s->reserved.reset();
    if (ref == m_state.current)
        emit settingsChanged();
    emit modified();
    return {};
}

ToolResult<> ToolBox::moveSlot(SlotRef from, SlotRef to) {
    for (SlotRef r : {from, to})
        if (r.group < 0 || r.group >= groupCount() || r.index < 0 || r.index >= int(m_state.groups[std::size_t(r.group)].cells.size()))
            return ToolResult<>::failure(ToolBoxError::NoSuchSlot);
    if (!slot(from))
        return ToolResult<>::failure(ToolBoxError::SlotEmpty);
    if (from == to)
        return {};
    std::swap(m_state.groups[std::size_t(from.group)].cells[std::size_t(from.index)],
              m_state.groups[std::size_t(to.group)].cells[std::size_t(to.index)]);
    if (m_state.current == from)
        m_state.current = to;
    else if (m_state.current == to)
        m_state.current = from;
    normalizeGroup(m_state.groups[std::size_t(from.group)]);
    normalizeGroup(m_state.groups[std::size_t(to.group)]);
    emit layoutChanged();
    emit modified();
    return {};
}

void ToolBox::reserveSettings() {
    ToolSlot& s = *slotMutable(m_state.current);
    s.reserved = s.settings;
    emit modified();
}

ToolResult<> ToolBox::recallSettings() {
    const ToolSlot& s = currentTool();
    if (!s.reserved)
        return ToolResult<>::failure(ToolBoxError::NothingReserved);
    setSettings(*s.reserved);
    return {};
}

void ToolBox::stepSize(int direction) {
    BrushSettings s = settings();
    s.size = steppedSize(s.size, direction);
    setSettings(s);
}

void ToolBox::setState(ToolBoxState state) {
    // A state without tools gets the default tool set (into its first group, so custom group names survive).
    if (slotCount(state) == 0) {
        const ToolBoxState defaults = defaultToolBoxState();
        if (state.groups.empty())
            state.groups = defaults.groups;
        else
            state.groups.front().cells = defaults.groups.front().cells;
        state.current = {0, 0};
    }
    for (std::size_t g = 0; g < state.groups.size(); ++g) {
        ToolGroup& group = state.groups[g];
        if (group.name.trimmed().isEmpty())
            group.name = tr("Group %1").arg(int(g) + 1);
        for (auto& cell : group.cells) {
            if (!cell)
                continue;
            cell->settings = cell->settings.clamped();
            if (cell->reserved) {
                cell->reserved->tool = cell->type();
                cell->reserved = cell->reserved->clamped();
            }
            if (cell->name.trimmed().isEmpty())
                cell->name = toolTypeName(cell->type());
        }
        normalizeGroup(group);
    }
    if (!state.color.isValid())
        state.color = Qt::black;
    state.options.minimumPressure = std::clamp(state.options.minimumPressure, 0.0, 1.0);
    state.options.stabilizer = clampedLevel(state.options.stabilizer);
    state.fixed = state.fixed.clamped();

    m_state = std::move(state);
    if (!slot(m_state.current))
        m_state.current = firstSlot(m_state);
    emit layoutChanged();
    emit currentSlotChanged();
    emit settingsChanged();
    emit colorChanged(m_state.color);
    emit secondaryColorChanged(m_state.secondaryColor);
    emit optionsChanged();
    emit fixedOptionsChanged();
    emit modified();
}

}
