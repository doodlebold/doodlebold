#pragma once

#include "core/options/AppOptions.h"
#include "core/options/KeyTable.h"

#include <QLineEdit>
#include <QWidget>

class QComboBox;
class QLabel;
class QListWidget;
class QListWidgetItem;
class QPushButton;

namespace doodlebold {

// A field that takes the next key pressed, with its modifiers, as a key sequence (Tab and Backspace too). `plainOnly`: the
// modifiers are dropped, for a key held on its own. While it has the focus it is listening: it turns the accent colour and asks for
// a key, before any key is pressed. Escape stops it listening; one more is left to the dialog.
class KeyCaptureEdit : public QLineEdit {
    Q_OBJECT
public:
    explicit KeyCaptureEdit(QWidget* parent = nullptr, bool plainOnly = false);

    bool listening() const { return m_listening; }

signals:
    void captured(const QKeySequence& key);
    void listeningChanged(bool on);

protected:
    bool event(QEvent* e) override;
    void keyPressEvent(QKeyEvent* e) override;
    void focusInEvent(QFocusEvent* e) override;
    void focusOutEvent(QFocusEvent* e) override;

private:
    void setListening(bool on);

    bool m_plainOnly = false;
    bool m_listening = false;
    QString m_shown;  // the field's text before it started listening, shown again if no key comes
};

// Options › Keys: every command and tray tool with its key, under small headings, and the keys held on the
// canvas. Choosing a row and pressing a key gives the row that key; a key in use moves to it only once the user says so in a
// question (keysOverwrite, keysRevert), and a note says from where.
class KeysPage : public QWidget {
    Q_OBJECT
public:
    explicit KeysPage(const AppOptions::Values& initial, QWidget* parent = nullptr);

    void setTable(KeyTable table);
    const KeyTable& table() const { return m_table; }
    HeldKeys heldKeys() const { return m_held; }
    bool picks() const;
    bool moves() const;

    // Choose a row by id, as a click does (tests and the dialog's first look).
    void selectRow(const QString& id);
    QString note() const;

private:
    QWidget* shortcutSection();
    QWidget* heldSection(const AppOptions::Values& initial);
    QComboBox* modifierCombo(const char* name, bool canBeOff);
    QLabel* escapeHint(KeyCaptureEdit* edit, const char* name);
    void fillList();
    void showRow(int row);
    void filterRows(const QString& text);
    int currentRow() const;
    void assignKey(const QKeySequence& key);
    void clearKey();
    void restoreDefault();
    void restoreAllDefaults();
    void tellTaken(const QKeySequence& key, const std::vector<int>& losers);
    bool confirmTaking(const QString& key, const QStringList& from, const QString& to);
    void showHeld();
    void chooseModifier(Qt::KeyboardModifier HeldKeys::* member, QComboBox* combo);
    QComboBox* heldCombo(Qt::KeyboardModifier HeldKeys::* member) const;

    KeyTable m_table;
    HeldKeys m_held;
    QLineEdit* m_search = nullptr;
    QListWidget* m_list = nullptr;
    KeyCaptureEdit* m_capture = nullptr;
    QPushButton* m_clear = nullptr;
    QPushButton* m_default = nullptr;
    QLabel* m_note = nullptr;
    KeyCaptureEdit* m_pan = nullptr;
    QComboBox* m_zoom = nullptr;
    QComboBox* m_rotate = nullptr;
    QComboBox* m_pick = nullptr;
    QComboBox* m_move = nullptr;
};

}
