#pragma once

#include "core/brush/BrushSettings.h"
#include "core/tools/FixedTool.h"
#include "core/util/Result.h"

#include <QColor>
#include <QObject>
#include <QString>
#include <QStringList>

#include <functional>
#include <initializer_list>
#include <optional>
#include <vector>

namespace doodlebold {

enum class ToolBoxError {
    NoSuchSlot,       // group or cell index out of range
    SlotOccupied,     // the target cell already holds a tool
    SlotEmpty,        // the source cell holds no tool
    LastTool,         // the tool box keeps at least one tool
    InvalidName,      // names are not empty
    NothingReserved,  // Recall Settings without a Reserve Settings before
};

QString describe(ToolBoxError e);

template <typename T = void> using ToolResult = Result<T, ToolBoxError>;

struct SlotRef {
    int group = 0;
    int index = 0;
    bool operator==(const SlotRef&) const = default;
};

struct ToolSlot {
    QString name;
    BrushSettings settings;
    std::optional<BrushSettings> reserved;
    // The key that picks this tool, as QKeySequence's portable text, or empty (Options › Keys). It belongs to the slot, so
    // it keeps to the tool through renames and moves, and is saved with the tray.
    QString key;
    ToolType type() const { return settings.tool; }
    bool operator==(const ToolSlot&) const = default;
};

struct ToolGroup {
    QString name;
    std::vector<std::optional<ToolSlot>> cells;
    bool operator==(const ToolGroup&) const = default;
};

struct ToolBoxState {
    std::vector<ToolGroup> groups;
    SlotRef current;
    QColor color = Qt::black;
    QColor secondaryColor = Qt::white;  // the second colour, exchanged with the first by X
    BrushOptions options;
    FixedToolOptions fixed;  // Marquee / Lasso / Magic Wand panels (the active fixed tool itself is not persisted)
};

ToolBoxState defaultToolBoxState();

// A tray saved before the Bucket existed gets one where its Blur is. A Bucket the user already made swaps places with the
// Blur; without one, a new Bucket takes the Blur's cell and the Blur moves after its group's last tool. A tray without a Blur gets
// the Bucket in its first group's first empty cell, and one without tools is left to setState. The current tool stays the same
// tool.
void placeBucket(ToolBoxState& state);

// The tray is a list of groups of tool slots; the current slot's settings are what the canvas paints with. One instance is
// shared by every canvas (tools and colour are global, not per document).
class ToolBox : public QObject {
    Q_OBJECT
public:
    static constexpr int kColumns = 3;  // wider cells than four

    explicit ToolBox(QObject* parent = nullptr);

    // setSettings clamps them and keeps the slot's tool type (a slot never changes type).
    const BrushSettings& settings() const { return currentTool().settings; }
    void setSettings(const BrushSettings& s);
    QColor color() const { return m_state.color; }
    void setColor(const QColor& c);
    QColor secondaryColor() const { return m_state.secondaryColor; }
    void setSecondaryColor(const QColor& c);
    void exchangeColors();
    // The transparent colour ("-"): the brush paints transparent, taking away what is under the stroke. Picking a colour
    // leaves it. Not saved.
    bool transparent() const { return m_transparent; }
    void setTransparent(bool on);
    QColor paintColor() const { return m_transparent ? QColor(Qt::transparent) : m_state.color; }
    const BrushOptions& options() const { return m_state.options; }
    // The current tool's own stabilizer level, or the global one if it has none.
    int stabilizerLevel() const { return settings().stabilizer.value_or(options().stabilizer).value; }
    void setOptions(const BrushOptions& o);

    // While a fixed tool is active fixedTool() holds it; picking a tray slot (selectSlot, selectToolOfType, deleting the
    // current tool …) makes the tray tool active again.
    std::optional<FixedTool> fixedTool() const { return m_fixedTool; }
    void setFixedTool(std::optional<FixedTool> tool);
    const FixedToolOptions& fixedOptions() const { return m_state.fixed; }
    void setFixedOptions(const FixedToolOptions& o);

    int groupCount() const { return int(m_state.groups.size()); }
    const ToolGroup& group(int g) const { return m_state.groups.at(std::size_t(g)); }
    const ToolSlot* slot(SlotRef ref) const;
    int toolCount() const;
    SlotRef currentSlot() const { return m_state.current; }
    const ToolSlot& currentTool() const { return *slot(m_state.current); }

    ToolResult<> selectSlot(SlotRef ref);
    // N / V / E: selects the first tool of this type, or the next of its type when the current one already is one (the key
    // cycles). False when there is none.
    bool selectToolOfType(ToolType type);
    // The same for a key shared by several tool types (C: Water Color and Pixel Art Pen): the tools of all of them, in
    bool selectToolOfTypes(std::initializer_list<ToolType> types);
    // Also creates one in the first empty cell of the current group when the tray has none: the default set has no Selection
    // Pen / Eraser, and the A / S keys make them.
    void selectOrCreateToolOfType(ToolType type);

    ToolResult<SlotRef> createSlot(SlotRef at, ToolType type);  // `at` must be an empty cell
    ToolResult<SlotRef> duplicateSlot(SlotRef ref);             // the copy goes to the next empty cell of the group
    ToolResult<> deleteSlot(SlotRef ref);
    ToolResult<> renameSlot(SlotRef ref, const QString& name);
    // The slot's key (portable text; empty removes it). Not checked against other keys: Options › Keys does that.
    ToolResult<> setSlotKey(SlotRef ref, const QString& key);
    ToolResult<> initializeSlot(SlotRef ref);                   // back to defaultSettings, reserved copy dropped
    // Moves a tool to another cell (any group); onto an occupied cell the two swap places.
    ToolResult<> moveSlot(SlotRef from, SlotRef to);
    void reserveSettings();
    ToolResult<> recallSettings();

    void stepSize(int direction);

    // A brush picture was renamed: every tool that uses it, and every reserved copy, follows. `form` says which list it's in,
    // the forms or the textures. Returns how many tools changed.
    int renameResource(bool form, const QString& from, const QString& to);
    // The names of the tools that paint with the picture, in tray order (a reserved copy isn't counted).
    QStringList toolsUsingResource(bool form, const QString& name) const;
    // A brush picture was removed: every tool that used it, and every reserved copy, goes back to its tool's default form or
    // texture. Returns how many tools changed.
    int resetResource(bool form, const QString& name);

    const ToolBoxState& state() const { return m_state; }
    // The state is repaired first: ranges clamped, grids resized, a missing tool or invalid current slot replaced (an empty
    // state gives the default tool set).
    void setState(ToolBoxState state);

signals:
    void settingsChanged();     // the current settings (value) changed
    void currentSlotChanged();  // another slot is current
    void activeToolChanged();   // a fixed tool became active, or the tray tool took over again
    void fixedOptionsChanged(); // the options of the fixed tools changed
    void layoutChanged();       // tools were created, deleted, moved or renamed
    void colorChanged(const QColor& color);
    void secondaryColorChanged(const QColor& color);
    void transparentChanged(bool on);
    void optionsChanged();
    void modified();

private:
    ToolSlot* slotMutable(SlotRef ref);
    // Every use of the picture `from`, in tools and reserved copies, takes the name `to` gives for its tool type.
    int replaceResource(bool form, const QString& from, const std::function<QString(ToolType)>& to);
    void setCurrent(SlotRef ref);

    ToolBoxState m_state;
    bool m_transparent = false;
    std::optional<FixedTool> m_fixedTool;
};

}
