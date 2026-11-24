#include "ui/KeysPage.h"

#include "ui/Components.h"
#include "ui/Theme.h"

#include <QApplication>
#include <QComboBox>
#include <QFormLayout>
#include <QStyle>
#include <QFrame>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPainter>
#include <QPushButton>
#include <QStyledItemDelegate>
#include <QVBoxLayout>

#include <algorithm>
#include <ranges>

namespace doodlebold {

namespace {

constexpr int kRowRole = Qt::UserRole;       // the row's index in the table; -1 for a heading
constexpr int kKeysRole = Qt::UserRole + 1;  // the row's keys as the key caps show them
constexpr int kListHeight = 300;  // component metric: the shortcut list's least height
constexpr int kCapPadding = Theme::kSpace2;
constexpr int kCapGap = Theme::kSpace1;
constexpr int kRowHeight = 30;  // component metric: a shortcut row, room for its key caps

QColor token(Theme::Token t) {
    return Theme::color(t);
}

QStringList capsOf(const QList<QKeySequence>& keys) {
    QStringList caps;
    for (const QKeySequence& k : keys)
        caps << k.toString(QKeySequence::NativeText);
    return caps;
}

// The rows: a command's name at the left and its keys as key caps at the right; a heading in small capitals with a rule.
class KeyCapDelegate : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override {
        return {QStyledItemDelegate::sizeHint(option, index).width(), kRowHeight};
    }

    void paint(QPainter* p, const QStyleOptionViewItem& option, const QModelIndex& index) const override {
        if (index.data(kRowRole).toInt() < 0) {
            paintHeading(p, option, index.data().toString());
            return;
        }
        // The row's background only (hover, selection): the text and caps are drawn here.
        QStyleOptionViewItem plain = option;
        initStyleOption(&plain, index);
        const QWidget* view = option.widget;
        (view ? view->style() : QApplication::style())->drawPrimitive(QStyle::PE_PanelItemViewItem, &plain, p, view);
        const QStringList caps = index.data(kKeysRole).toStringList();
        const QRect area = option.rect.adjusted(20, 0, -12, 0);
        const int capsLeft = paintCaps(p, option, area, caps);
        p->setPen(option.state & QStyle::State_Selected ? token(Theme::Token::SelectedText) : token(Theme::Token::Text));
        p->drawText(QRect(area.left(), area.top(), capsLeft - area.left() - 8, area.height()), Qt::AlignVCenter | Qt::AlignLeft,
                    option.fontMetrics.elidedText(index.data().toString(), Qt::ElideRight, capsLeft - area.left() - 8));
    }

private:
    static void paintHeading(QPainter* p, const QStyleOptionViewItem& option, const QString& title) {
        p->save();
        p->setFont(Theme::font(Theme::TextRole::Overline));
        p->setPen(token(Theme::Token::TextMuted));
        const QRect r = option.rect.adjusted(20, 4, -12, 0);
        p->drawText(r, Qt::AlignVCenter | Qt::AlignLeft, title);
        p->setPen(token(Theme::Token::Border));
        p->drawLine(r.left(), option.rect.bottom(), r.right(), option.rect.bottom());
        p->restore();
    }

    // Right to left; returns where the leftmost cap starts.
    static int paintCaps(QPainter* p, const QStyleOptionViewItem& option, const QRect& area, const QStringList& caps) {
        int right = area.right();
        p->save();
        p->setRenderHint(QPainter::Antialiasing);
        for (const auto& it : std::views::reverse(caps)) {
            const int width = option.fontMetrics.horizontalAdvance(it) + 2 * kCapPadding;
            const QRectF cap(right - width, area.center().y() - 10.5, width, 21);
            p->setPen(QPen(token(Theme::Token::BorderStrong), Theme::kBorder));
            p->setBrush(token(Theme::Token::Base));
            p->drawRoundedRect(cap.adjusted(0.5, 0.5, -0.5, -0.5), Theme::kRadiusXs, Theme::kRadiusXs);
            p->setPen(token(Theme::Token::Text));
            p->drawText(cap, Qt::AlignCenter, it);
            right -= width + kCapGap;
        }
        p->restore();
        return right;
    }
};

// Zoom and Rotate are partners, as are Pick and Move: the two of a pair take different keys.
Qt::KeyboardModifier HeldKeys::* heldPartner(Qt::KeyboardModifier HeldKeys::* member) {
    if (member == &HeldKeys::zoom)
        return &HeldKeys::rotate;
    if (member == &HeldKeys::rotate)
        return &HeldKeys::zoom;
    return member == &HeldKeys::pick ? &HeldKeys::move : &HeldKeys::pick;
}

QString heldName(Qt::KeyboardModifier HeldKeys::* member) {
    if (member == &HeldKeys::zoom)
        return KeysPage::tr("Zoom");
    if (member == &HeldKeys::rotate)
        return KeysPage::tr("Rotate");
    return member == &HeldKeys::pick ? KeysPage::tr("Pick a colour") : KeysPage::tr("Move the selected layers");
}

bool isModifierKey(int key) {
    return key == Qt::Key_Control || key == Qt::Key_Alt || key == Qt::Key_Shift || key == Qt::Key_Meta || key == Qt::Key_AltGr;
}

}

KeyCaptureEdit::KeyCaptureEdit(QWidget* parent, bool plainOnly) : QLineEdit(parent), m_plainOnly(plainOnly) {
    setReadOnly(true);
    setPlaceholderText(tr("Press shortcut"));
}

void KeyCaptureEdit::focusInEvent(QFocusEvent* e) {
    QLineEdit::focusInEvent(e);
    setListening(true);
}

void KeyCaptureEdit::focusOutEvent(QFocusEvent* e) {
    QLineEdit::focusOutEvent(e);
    setListening(false);
}

// Listening, the field is tinted with the accent and empty, so its placeholder asks for the key.
void KeyCaptureEdit::setListening(bool on) {
    if (on == m_listening || (on && !isEnabled()))
        return;
    m_listening = on;
    if (on) {
        m_shown = text();
        clear();
        setPlaceholderText(tr("Listening… press a key"));
        setProperty("listening", true);  // the style sheet's accent ring and tint
        style()->unpolish(this);
        style()->polish(this);
    } else {
        if (text().isEmpty())
            setText(m_shown);
        setPlaceholderText(tr("Press shortcut"));
        setProperty("listening", false);
        style()->unpolish(this);
        style()->polish(this);
    }
    emit listeningChanged(on);
}

// Tab and Backtab would move the focus before keyPressEvent sees them.
bool KeyCaptureEdit::event(QEvent* e) {
    if (e->type() == QEvent::KeyPress) {
        auto* k = static_cast<QKeyEvent*>(e);
        if (k->key() == Qt::Key_Tab || k->key() == Qt::Key_Backtab) {
            keyPressEvent(k);
            return true;
        }
    }
    if (e->type() == QEvent::ShortcutOverride) {
        e->accept();  // the window's own shortcuts wait while a key is chosen
        return true;
    }
    return QLineEdit::event(e);
}

void KeyCaptureEdit::keyPressEvent(QKeyEvent* e) {
    if (e->key() == Qt::Key_Escape && m_listening) {
        e->accept();
        clearFocus();  // the next Escape reaches the dialog
        setListening(false);
        return;
    }
    if (e->key() == Qt::Key_Escape || isModifierKey(e->key()) || e->key() == 0 || e->key() == Qt::Key_unknown) {
        e->ignore();
        return;
    }
    const Qt::KeyboardModifiers mods = m_plainOnly ? Qt::NoModifier : (e->modifiers() & ~Qt::KeypadModifier);
    const int key = e->key() == Qt::Key_Backtab ? int(Qt::Key_Tab) : e->key();
    e->accept();
    emit captured(QKeySequence(QKeyCombination(mods, Qt::Key(key))));
}

KeysPage::KeysPage(const AppOptions::Values& initial, QWidget* parent) : QWidget(parent), m_held(initial.held) {
    setObjectName(QStringLiteral("keysPage"));
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(Theme::kCardGap);
    layout->addWidget(shortcutSection());
    layout->addWidget(heldSection(initial));
    showRow(-1);
}

QWidget* KeysPage::shortcutSection() {
    QVBoxLayout* body = nullptr;
    QFrame* card = components::sectionCard(tr("Shortcut Keys"), body);
    m_search = new QLineEdit;
    m_search->setObjectName(QStringLiteral("keysSearch"));
    m_search->setPlaceholderText(tr("Search commands and keys"));
    m_search->setClearButtonEnabled(true);
    body->addWidget(m_search);

    m_list = new QListWidget;
    m_list->setObjectName(QStringLiteral("keysList"));
    m_list->setItemDelegate(new KeyCapDelegate(m_list));
    m_list->setMinimumHeight(kListHeight);
    m_list->setUniformItemSizes(true);
    body->addWidget(m_list, 1);

    auto* keyRow = new QHBoxLayout;
    keyRow->addWidget(new QLabel(tr("Key:")));
    m_capture = new KeyCaptureEdit;
    m_capture->setObjectName(QStringLiteral("keysCapture"));
    keyRow->addWidget(m_capture, 1);
    keyRow->addWidget(escapeHint(m_capture, "keysEscapeHint"));
    m_clear = new QPushButton(tr("Clear"));
    m_clear->setObjectName(QStringLiteral("keysClear"));
    m_default = new QPushButton(tr("Default"));
    m_default->setObjectName(QStringLiteral("keysDefault"));
    m_default->setToolTip(tr("Restore this row's starting key"));
    keyRow->addWidget(m_clear);
    keyRow->addWidget(m_default);
    body->addLayout(keyRow);

    auto* footer = new QHBoxLayout;
    m_note = components::mutedLabel(tr("Choose a row and press the new key. A key already in use can move to the row you chose."));
    m_note->setObjectName(QStringLiteral("keysNote"));
    footer->addWidget(m_note, 1);
    auto* all = new QPushButton(tr("Restore All Defaults"));
    all->setObjectName(QStringLiteral("keysRestoreAll"));
    footer->addWidget(all, 0, Qt::AlignTop);
    body->addLayout(footer);

    connect(m_search, &QLineEdit::textChanged, this, &KeysPage::filterRows);
    connect(m_list, &QListWidget::currentItemChanged, this, [this] {
        showRow(currentRow());
        if (currentRow() >= 0)
            m_capture->setFocus(Qt::OtherFocusReason);  // the next key pressed is the row's
    });
    connect(m_capture, &KeyCaptureEdit::captured, this, &KeysPage::assignKey);
    connect(m_clear, &QPushButton::clicked, this, &KeysPage::clearKey);
    connect(m_default, &QPushButton::clicked, this, &KeysPage::restoreDefault);
    connect(all, &QPushButton::clicked, this, &KeysPage::restoreAllDefaults);
    return card;
}

QComboBox* KeysPage::modifierCombo(const char* name, bool canBeOff) {
    auto* combo = new QComboBox;
    combo->setObjectName(QString::fromLatin1(name));
    combo->addItem(tr("Ctrl"), int(Qt::ControlModifier));
    combo->addItem(tr("Alt"), int(Qt::AltModifier));
    combo->addItem(tr("Shift"), int(Qt::ShiftModifier));
    if (canBeOff)
        combo->addItem(tr("Off"), 0);
    return combo;
}

// "(Esc to stop listening)" beside a key field, shown while it listens; its room is kept, so nothing moves when it shows.
QLabel* KeysPage::escapeHint(KeyCaptureEdit* edit, const char* name) {
    QLabel* hint = components::mutedLabel(tr("(Esc to stop listening)"));
    hint->setObjectName(QString::fromLatin1(name));
    hint->setWordWrap(false);
    QSizePolicy policy = hint->sizePolicy();
    policy.setRetainSizeWhenHidden(true);
    hint->setSizePolicy(policy);
    hint->setVisible(edit->listening());
    connect(edit, &KeyCaptureEdit::listeningChanged, hint, &QLabel::setVisible);
    return hint;
}

QWidget* KeysPage::heldSection(const AppOptions::Values& initial) {
    QVBoxLayout* body = nullptr;
    QFrame* card = components::sectionCard(tr("Keys Held on the Canvas"), body);
    auto* form = new QFormLayout;
    form->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
    m_pan = new KeyCaptureEdit(nullptr, true);
    m_pan->setObjectName(QStringLiteral("heldPanKey"));
    m_zoom = modifierCombo("heldZoomKey", false);
    m_rotate = modifierCombo("heldRotateKey", false);
    m_pick = modifierCombo("heldPickKey", true);
    m_move = modifierCombo("heldMoveKey", true);
    auto* pan = new QHBoxLayout;
    pan->addWidget(m_pan, 1);
    pan->addWidget(escapeHint(m_pan, "heldPanEscapeHint"));
    form->addRow(tr("Pan the view:"), pan);
    form->addRow(tr("Zoom (with the pan key):"), m_zoom);
    form->addRow(tr("Rotate (with the pan key):"), m_rotate);
    form->addRow(tr("Pick a colour:"), m_pick);
    form->addRow(tr("Move the selected layers:"), m_move);
    body->addLayout(form);
    body->addWidget(
        components::mutedLabel(tr("Held while the pointer is over the canvas; a key held there doesn't reach the commands that use it. "
                                  "Zoom and Rotate take different keys, as do Pick and Move: choosing a key the other one has "
                                  "swaps the two.")));

    if (!initial.altPicksColour)
        m_pick->setCurrentIndex(m_pick->findData(0));
    if (!initial.ctrlMovesLayers)
        m_move->setCurrentIndex(m_move->findData(0));
    showHeld();
    connect(m_pan, &KeyCaptureEdit::captured, this, [this](const QKeySequence& key) {
        m_held.pan = key[0].key();
        showHeld();
    });
    connect(m_zoom, &QComboBox::activated, this, [this] { chooseModifier(&HeldKeys::zoom, m_zoom); });
    connect(m_rotate, &QComboBox::activated, this, [this] { chooseModifier(&HeldKeys::rotate, m_rotate); });
    connect(m_pick, &QComboBox::activated, this, [this] { chooseModifier(&HeldKeys::pick, m_pick); });
    connect(m_move, &QComboBox::activated, this, [this] { chooseModifier(&HeldKeys::move, m_move); });
    return card;
}

void KeysPage::setTable(KeyTable table) {
    m_table = std::move(table);
    fillList();
}

bool KeysPage::picks() const {
    return m_pick->currentData().toInt() != 0;
}

bool KeysPage::moves() const {
    return m_move->currentData().toInt() != 0;
}

// The rows under their headings, each heading where its first row is.
void KeysPage::fillList() {
    m_list->clear();
    QStringList groups;
    for (const KeyRow& r : m_table.rows())
        if (!groups.contains(r.group))
            groups << r.group;
    for (const QString& group : groups) {
        auto* heading = new QListWidgetItem(group, m_list);
        heading->setData(kRowRole, -1);
        heading->setFlags(Qt::NoItemFlags);
        for (int i = 0; i < m_table.size(); ++i) {
            if (m_table.row(i).group != group)
                continue;
            auto* item = new QListWidgetItem(m_table.row(i).label, m_list);
            item->setData(kRowRole, i);
            item->setData(kKeysRole, capsOf(m_table.row(i).keys));
        }
    }
    filterRows(m_search->text());
}

int KeysPage::currentRow() const {
    const QListWidgetItem* item = m_list->currentItem();
    return item ? item->data(kRowRole).toInt() : -1;
}

void KeysPage::selectRow(const QString& id) {
    const int row = m_table.find(id);
    for (int i = 0; i < m_list->count(); ++i)
        if (m_list->item(i)->data(kRowRole).toInt() == row && row >= 0)
            m_list->setCurrentRow(i);
}

QString KeysPage::note() const {
    return m_note->text();
}

void KeysPage::showRow(int row) {
    m_capture->setEnabled(row >= 0);
    m_clear->setEnabled(row >= 0 && !m_table.row(row).keys.isEmpty());
    m_default->setEnabled(row >= 0 && !m_table.isDefault(row));
    m_capture->setText(row >= 0 ? capsOf(m_table.row(row).keys).join(QStringLiteral(", ")) : QString());
    for (int i = 0; i < m_list->count(); ++i) {
        QListWidgetItem* item = m_list->item(i);
        if (const int r = item->data(kRowRole).toInt(); r >= 0)
            item->setData(kKeysRole, capsOf(m_table.row(r).keys));
    }
}

// A row shows when its name or one of its keys holds the text; a heading, when a row under it shows.
void KeysPage::filterRows(const QString& text) {
    const QString wanted = text.trimmed();
    QListWidgetItem* heading = nullptr;
    bool headingShown = false;
    for (int i = 0; i < m_list->count(); ++i) {
        QListWidgetItem* item = m_list->item(i);
        if (item->data(kRowRole).toInt() < 0) {
            if (heading)
                heading->setHidden(!headingShown);
            heading = item;
            headingShown = false;
            continue;
        }
        const bool shown = wanted.isEmpty() || item->text().contains(wanted, Qt::CaseInsensitive) ||
                           item->data(kKeysRole).toStringList().join(QLatin1Char(' ')).contains(wanted, Qt::CaseInsensitive);
        item->setHidden(!shown);
        headingShown = headingShown || shown;
    }
    if (heading)
        heading->setHidden(!headingShown);
}

void KeysPage::assignKey(const QKeySequence& key) {
    const int row = currentRow();
    if (row < 0)
        return;
    const QString text = key.toString(QKeySequence::NativeText);
    if (const int holder = m_table.rowWithKey(key, row);
        holder >= 0 && !confirmTaking(text, {m_table.row(holder).label}, m_table.row(row).label)) {
        m_note->setText(tr("%1 stays with %2.").arg(text, m_table.row(holder).label));
        showRow(row);
        return;
    }
    const std::optional<int> loser = m_table.assign(row, key);
    tellTaken(key, loser ? std::vector<int>{*loser} : std::vector<int>{});
    showRow(row);
}

void KeysPage::clearKey() {
    if (const int row = currentRow(); row >= 0) {
        m_table.clear(row);
        showRow(row);
    }
}

void KeysPage::restoreDefault() {
    const int row = currentRow();
    if (row < 0)
        return;
    QStringList holders, keys;
    for (const QKeySequence& k : m_table.row(row).defaults)
        if (const int holder = m_table.rowWithKey(k, row); holder >= 0)
            holders << m_table.row(holder).label, keys << k.toString(QKeySequence::NativeText);
    if (!holders.isEmpty() && !confirmTaking(keys.join(QStringLiteral(", ")), holders, m_table.row(row).label)) {
        m_note->setText(tr("%1 stays with %2.").arg(keys.join(QStringLiteral(", ")), holders.join(QStringLiteral(", "))));
        return;
    }
    const std::vector<int> losers = m_table.restoreDefault(row);
    tellTaken(m_table.row(row).keys.value(0), losers);
    showRow(row);
}

void KeysPage::restoreAllDefaults() {
    m_table.restoreAllDefaults();
    m_note->setText(tr("Every key is back to how it started."));
    showRow(currentRow());
}

void KeysPage::tellTaken(const QKeySequence& key, const std::vector<int>& losers) {
    if (losers.empty()) {
        m_note->setText(key.isEmpty() ? QString() : tr("%1 is set.").arg(key.toString(QKeySequence::NativeText)));
        return;
    }
    QStringList names;
    for (const int r : losers)
        names << m_table.row(r).label;
    m_note->setText(tr("%1 was taken from %2, which has no key for it now.")
                        .arg(key.toString(QKeySequence::NativeText), names.join(QStringLiteral(", "))));
}

// Asks before a key leaves the row or held key that has it: Overwrite moves it, Revert (the default) keeps it.
bool KeysPage::confirmTaking(const QString& key, const QStringList& from, const QString& to) {
    QMessageBox box(QMessageBox::Question, tr("Key Already Used"),
                    tr("%1 is already used by %2.").arg(key, from.join(QStringLiteral(", "))), QMessageBox::NoButton, this);
    box.setObjectName(QStringLiteral("keysTakenDialog"));
    box.setInformativeText(
        tr("Overwrite gives it to %1, and %2 loses it. Revert leaves the keys as they were.").arg(to, from.join(QStringLiteral(", "))));
    QPushButton* overwrite = box.addButton(tr("Overwrite"), QMessageBox::AcceptRole);
    overwrite->setObjectName(QStringLiteral("keysOverwrite"));
    QPushButton* revert = box.addButton(tr("Revert"), QMessageBox::RejectRole);
    revert->setObjectName(QStringLiteral("keysRevert"));
    box.setDefaultButton(revert);
    box.setEscapeButton(revert);
    box.exec();
    return box.clickedButton() == overwrite;
}

QComboBox* KeysPage::heldCombo(Qt::KeyboardModifier HeldKeys::* member) const {
    if (member == &HeldKeys::zoom)
        return m_zoom;
    if (member == &HeldKeys::rotate)
        return m_rotate;
    return member == &HeldKeys::pick ? m_pick : m_move;
}

void KeysPage::showHeld() {
    m_pan->setText(QKeySequence(m_held.pan).toString(QKeySequence::NativeText));
    const auto show = [](QComboBox* combo, Qt::KeyboardModifier m) {
        if (combo->currentData().toInt() != 0 || combo->findData(0) < 0)
            combo->setCurrentIndex(combo->findData(int(m)));
    };
    show(m_zoom, m_held.zoom);
    show(m_rotate, m_held.rotate);
    show(m_pick, m_held.pick);
    show(m_move, m_held.move);
}

// A key the partner has (Zoom and Rotate, Pick and Move) swaps the two, once the user says so.
void KeysPage::chooseModifier(Qt::KeyboardModifier HeldKeys::* member, QComboBox* combo) {
    const int chosen = combo->currentData().toInt();
    if (chosen != 0) {
        const HeldKeys swapped = swappedHeldKey(m_held, member, Qt::KeyboardModifier(chosen));
        Qt::KeyboardModifier HeldKeys::* partner = heldPartner(member);
        const bool takes = heldCombo(partner)->currentData().toInt() != 0 && swapped.*partner != m_held.*partner;
        if (!takes || confirmTaking(combo->currentText(), {heldName(partner)}, heldName(member)))
            m_held = swapped;
        else
            combo->setCurrentIndex(combo->findData(int(m_held.*member)));
    }
    showHeld();
}

}
