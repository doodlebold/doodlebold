#pragma once

#include <QKeySequence>
#include <QList>
#include <QMap>
#include <QString>

#include <optional>
#include <vector>

namespace doodlebold {

// One row of Options › Keys: a command of the menus or the keys, or a tool of the tray.
struct KeyRow {
    QString id;     // the command's id ("file.new"), or trayRowId() for a tray tool
    QString group;  // the heading it's listed under
    QString label;
    QList<QKeySequence> defaults;
    QList<QKeySequence> keys;
    bool operator==(const KeyRow&) const = default;
};

// The keys of every row, and the rule that a key reaches one row only: a key given to a row moves there from the row that had
// it.
class KeyTable {
public:
    KeyTable() = default;
    explicit KeyTable(std::vector<KeyRow> rows) : m_rows(std::move(rows)) {}

    const std::vector<KeyRow>& rows() const { return m_rows; }
    int size() const { return int(m_rows.size()); }
    const KeyRow& row(int i) const { return m_rows.at(std::size_t(i)); }
    int find(const QString& id) const;
    // The row holding `key`, other than `except`; -1 for none.
    int rowWithKey(const QKeySequence& key, int except = -1) const;

    // Row `row` gets `key` as its only key. The row that had it loses it, and is returned so the page can say so.
    std::optional<int> assign(int row, const QKeySequence& key);
    void clear(int row);
    // The row's starting keys again; the rows they're taken from are returned.
    std::vector<int> restoreDefault(int row);
    void restoreAllDefaults();
    bool isDefault(int row) const { return m_rows.at(std::size_t(row)).keys == m_rows.at(std::size_t(row)).defaults; }

private:
    std::vector<KeyRow> m_rows;
};

// A tray tool's row id: "tray:<group>:<index>", its place in the tray while Options is open.
QString trayRowId(int group, int index);
// The place a tray row id names, or nullopt for a command's id.
std::optional<std::pair<int, int>> trayPlace(const QString& id);

// The keys as text: QKeySequence's portable text, several separated by ", ".
QString keysText(const QList<QKeySequence>& keys);
QList<QKeySequence> keysFromText(const QString& text);

namespace key_settings {

// The commands whose keys the user changed, by id, from the program's settings ("keys/<id>"). An empty list: no key.
QMap<QString, QList<QKeySequence>> load();
// Keeps every command row that differs from its starting keys, and forgets the others. Tray rows are saved with the tray.
void save(const KeyTable& table);

}

}
