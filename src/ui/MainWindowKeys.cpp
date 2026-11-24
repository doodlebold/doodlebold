#include "ui/MainWindow.h"

#include "core/tools/ToolBox.h"
#include "ui/MenuSpec.h"

#include <QAction>

namespace doodlebold {

// The keys the user gave the commands, over the tables', and a key for each tray tool that has one.
void MainWindow::bindKeys() {
    const QMap<QString, QList<QKeySequence>> changed = key_settings::load();
    for (auto it = changed.cbegin(); it != changed.cend(); ++it)
        if (QAction* a = command(it.key()))
            menu::setKeys(a, it.key(), it.value());
    connect(m_tools, &ToolBox::layoutChanged, this, &MainWindow::rebuildTrayKeys);
    rebuildTrayKeys();
}

// Every command the window has, then every tray tool, each group of the tray under its own heading.
KeyTable MainWindow::keyTable() const {
    std::vector<KeyRow> rows;
    for (const menu::CommandInfo& c : menu::allCommands())
        if (const QAction* a = command(c.id))
            rows.push_back({c.id, c.group, c.label, c.defaults, a->shortcuts()});
    for (int g = 0; g < m_tools->groupCount(); ++g) {
        const ToolGroup& group = m_tools->group(g);
        for (int i = 0; i < int(group.cells.size()); ++i)
            if (const auto& cell = group.cells[std::size_t(i)])
                rows.push_back({trayRowId(g, i), tr("Tray: %1").arg(group.name), cell->name, {}, keysFromText(cell->key)});
    }
    return KeyTable(std::move(rows));
}

void MainWindow::applyKeyTable(const KeyTable& table) {
    for (const KeyRow& r : table.rows()) {
        if (const auto place = trayPlace(r.id)) {
            (void)m_tools->setSlotKey({place->first, place->second}, keysText(r.keys));
        } else if (QAction* a = command(r.id); a && a->shortcuts() != r.keys) {
            menu::setKeys(a, r.id, r.keys);
        }
    }
    key_settings::save(table);
}

void MainWindow::rebuildTrayKeys() {
    for (QAction* a : m_trayKeyActions)
        delete a;
    m_trayKeyActions.clear();
    for (int g = 0; g < m_tools->groupCount(); ++g) {
        const ToolGroup& group = m_tools->group(g);
        for (int i = 0; i < int(group.cells.size()); ++i) {
            const auto& cell = group.cells[std::size_t(i)];
            if (!cell || cell->key.isEmpty())
                continue;
            auto* a = new QAction(cell->name, this);
            a->setObjectName(trayRowId(g, i));
            a->setShortcuts(keysFromText(cell->key));
            connect(a, &QAction::triggered, this, [this, g, i] { (void)m_tools->selectSlot({g, i}); });
            addAction(a);
            m_trayKeyActions.push_back(a);
        }
    }
}

}
