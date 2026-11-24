#include "core/options/KeyTable.h"

#include "core/util/AppIdentity.h"

#include <QSettings>
#include <QStringList>

#include <algorithm>

namespace doodlebold {

namespace {

constexpr const char* kGroup = "keys";
constexpr const char* kTrayPrefix = "tray:";

}

int KeyTable::find(const QString& id) const {
    const auto it = std::ranges::find_if(m_rows, [&id](const KeyRow& r) { return r.id == id; });
    return it == m_rows.end() ? -1 : int(it - m_rows.begin());
}

int KeyTable::rowWithKey(const QKeySequence& key, int except) const {
    if (key.isEmpty())
        return -1;
    for (int i = 0; i < size(); ++i)
        if (i != except && m_rows[std::size_t(i)].keys.contains(key))
            return i;
    return -1;
}

std::optional<int> KeyTable::assign(int row, const QKeySequence& key) {
    if (key.isEmpty())
        return std::nullopt;
    const int holder = rowWithKey(key, row);
    if (holder >= 0)
        m_rows[std::size_t(holder)].keys.removeAll(key);
    m_rows.at(std::size_t(row)).keys = {key};
    return holder >= 0 ? std::optional<int>(holder) : std::nullopt;
}

void KeyTable::clear(int row) {
    m_rows.at(std::size_t(row)).keys.clear();
}

std::vector<int> KeyTable::restoreDefault(int row) {
    std::vector<int> losers;
    KeyRow& r = m_rows.at(std::size_t(row));
    for (const QKeySequence& key : r.defaults) {
        const int holder = rowWithKey(key, row);
        if (holder < 0)
            continue;
        m_rows[std::size_t(holder)].keys.removeAll(key);
        if (std::ranges::find(losers, holder) == losers.end())
            losers.push_back(holder);
    }
    r.keys = r.defaults;
    return losers;
}

void KeyTable::restoreAllDefaults() {
    for (KeyRow& r : m_rows)
        r.keys = r.defaults;
}

QString trayRowId(int group, int index) {
    return QStringLiteral("%1%2:%3").arg(QLatin1String(kTrayPrefix)).arg(group).arg(index);
}

std::optional<std::pair<int, int>> trayPlace(const QString& id) {
    if (!id.startsWith(QLatin1String(kTrayPrefix)))
        return std::nullopt;
    const QStringList parts = id.mid(int(qstrlen(kTrayPrefix))).split(QLatin1Char(':'));
    bool groupOk = false, indexOk = false;
    const int group = parts.value(0).toInt(&groupOk);
    const int index = parts.value(1).toInt(&indexOk);
    if (parts.size() != 2 || !groupOk || !indexOk)
        return std::nullopt;
    return std::pair{group, index};
}

QString keysText(const QList<QKeySequence>& keys) {
    QStringList texts;
    for (const QKeySequence& k : keys)
        texts << k.toString(QKeySequence::PortableText);
    return texts.join(QStringLiteral(", "));
}

QList<QKeySequence> keysFromText(const QString& text) {
    QList<QKeySequence> keys;
    for (const QString& one : text.split(QStringLiteral(", "), Qt::SkipEmptyParts))
        if (const QKeySequence k = QKeySequence::fromString(one, QKeySequence::PortableText); !k.isEmpty())
            keys << k;
    return keys;
}

namespace key_settings {

QMap<QString, QList<QKeySequence>> load() {
    QSettings s = app_identity::settings();
    s.beginGroup(QLatin1String(kGroup));
    QMap<QString, QList<QKeySequence>> changed;
    for (const QString& id : s.childKeys()) {
        QList<QKeySequence> keys;
        for (const QString& one : s.value(id).toStringList())
            if (const QKeySequence k = QKeySequence::fromString(one, QKeySequence::PortableText); !k.isEmpty())
                keys << k;
        changed.insert(id, keys);
    }
    return changed;
}

void save(const KeyTable& table) {
    QSettings s = app_identity::settings();
    s.remove(QLatin1String(kGroup));
    s.beginGroup(QLatin1String(kGroup));
    for (const KeyRow& r : table.rows()) {
        if (trayPlace(r.id) || r.keys == r.defaults)
            continue;
        QStringList texts;
        for (const QKeySequence& k : r.keys)
            texts << k.toString(QKeySequence::PortableText);
        s.setValue(r.id, texts);  // an empty list: the user took the key away
    }
}

}

}
