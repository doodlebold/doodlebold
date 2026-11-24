#pragma once

#include <QKeySequence>
#include <QList>
#include <QString>

#include <cstddef>
#include <functional>
#include <span>
#include <string_view>
#include <vector>

class QAction;
class QMenu;
class QMenuBar;
class QWidget;

namespace doodlebold::menu {

// The menu bar as data: every menu, entry, label, separator and shortcut. The main window builds its menu bar from these tables
// and binds each entry to its action; an entry without a binding is left out, and so is a separator, submenu or menu left with
// nothing.

enum class Kind {
    Command,
    Check,
    Radio,
    Submenu,
    Separator,
};

struct Entry {
    Kind kind = Kind::Command;
    std::string_view id;
    std::string_view label;      // the entry's text; an entry that asks for something more ends in "…"
    std::string_view shortcuts;  // "Ctrl+N"; several are separated by ", " ("F11, Shift+Tab")
    const Entry* children = nullptr;
    std::size_t childCount = 0;

    std::span<const Entry> submenu() const { return {children, childCount}; }
};

struct Menu {
    std::string_view title;
    std::span<const Entry> entries;
};

struct KeyCommand {
    std::string_view id;
    std::string_view label;
    std::string_view shortcuts;
};

std::span<const Menu> allMenus();
std::span<const KeyCommand> allKeyCommands();

// What the main window supplies. `action` returns the (shared, parented) action of an entry, or null if Doodlebold has no such
// feature; the builder sets its text, shortcuts and check state from the entry. `populate` lets a submenu fill itself (the
// recent files); it returns true if the submenu is Doodlebold's, even while it is empty.
struct Binder {
    std::function<QAction*(const Entry&)> action;
    std::function<bool(const Entry&, QMenu*)> populate;
    std::function<QAction*(const KeyCommand&)> keyAction;
};

void build(QMenuBar* bar, QWidget* window, const Binder& binder);

// Every command of the menus and of the keys, once each, in the order Options › Keys lists them: under its menu's title,
// or for a command in no menu under its kind's heading (Tools, Color, Brush; the view and layer keys join their menus). The label
// has no trailing "…", and a submenu's entries are named after it ("Export: PNG (.png)").
struct CommandInfo {
    QString id;
    QString group;
    QString label;
    QList<QKeySequence> defaults;
};
std::vector<CommandInfo> allCommands();
// Gives a built action these keys, and shows them in its menu text. The starting keys show as the table writes them.
void setKeys(QAction* action, const QString& id, const QList<QKeySequence>& shortcuts);

}
