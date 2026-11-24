#include "core/options/AppOptions.h"
#include "ui/ButtonTips.h"
#include "ui/NewCanvasDialog.h"

#include "core/io/DocumentFormat.h"
#include "ui/Theme.h"

#include <QColorDialog>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleValidator>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QListWidget>
#include <QLocale>
#include <QApplication>
#include <QPainter>
#include <QProgressBar>
#include <QPushButton>
#include <QStyledItemDelegate>
#include <QToolButton>
#include <QVBoxLayout>
#include <utility>

namespace doodlebold {

namespace {
constexpr const char* kLinkSizeKey = "newCanvas/linkSize";
}

using Theme::Token;

namespace {

constexpr int kSeparatorRole = Qt::UserRole + 1;

QLabel* rightLabel(const QString& text, QWidget* parent) {
    auto* l = new QLabel(text, parent);
    l->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    return l;
}

QLineEdit* numberEdit(const QString& name, QWidget* parent) {
    auto* e = new QLineEdit(parent);
    e->setObjectName(name);
    auto* v = new QDoubleValidator(0.0, 1e7, 4, e);
    v->setNotation(QDoubleValidator::StandardNotation);
    v->setLocale(QLocale::c());
    e->setValidator(v);
    return e;
}

std::optional<double> parseNumber(const QString& text) {
    bool ok = false;
    const double v = QLocale::c().toDouble(text, &ok);
    if (!ok || !(v > 0))
        return std::nullopt;
    return v;
}

class PresetDelegate : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    void paint(QPainter* p, const QStyleOptionViewItem& option, const QModelIndex& index) const override {
        if (!index.data(kSeparatorRole).toBool()) {
            QStyledItemDelegate::paint(p, option, index);
            return;
        }
        p->setPen(Theme::color(Token::Border));
        const int y = option.rect.center().y();
        p->drawLine(option.rect.left() + 2, y, option.rect.right() - 2, y);
    }
    QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override {
        QSize s = QStyledItemDelegate::sizeHint(option, index);
        if (index.data(kSeparatorRole).toBool())
            s.setHeight(10);
        return s;
    }
};

QString presetLabel(const CanvasPreset& p) {
    return p.separator ? QString() : p.name;
}

}

CanvasFieldsEditor::CanvasFieldsEditor(const NewCanvasValues& values, bool allowNoBackground, QObject* parent)
    : QObject(parent), m_model(values), m_allowNone(allowNoBackground) {
    pickColor = [](const QColor& current) -> std::optional<QColor> {
        const QColor c = QColorDialog::getColor(current, nullptr, tr("Canvas Background Color"));
        if (!c.isValid())
            return std::nullopt;
        return c;
    };
    if (m_allowNone)
        m_choice = std::nullopt;
    else
        m_choice = m_model.background();
}

int CanvasFieldsEditor::build(QGridLayout* grid, int row, QWidget* parent) {
    m_width = numberEdit(QStringLiteral("widthEdit"), parent);
    m_height = numberEdit(QStringLiteral("heightEdit"), parent);
    m_sizeUnit = new QComboBox(parent);
    m_sizeUnit->setObjectName(QStringLiteral("sizeUnitCombo"));
    for (const SizeUnit u : {SizeUnit::Pixels, SizeUnit::Inches, SizeUnit::Centimeters, SizeUnit::Millimeters})
        m_sizeUnit->addItem(sizeUnitLabel(u), static_cast<int>(u));
    grid->addWidget(rightLabel(tr("Width :"), parent), row, 0);
    grid->addWidget(m_width, row, 1);
    m_linkSize = new QToolButton(parent);
    m_linkSize->setObjectName(QStringLiteral("linkSizeButton"));
    m_linkSize->setCheckable(true);
    m_linkSize->setAutoRaise(true);
    m_linkSize->setIconSize(QSize(Theme::kIconMd, Theme::kIconMd));
    QIcon linkIcon;
    linkIcon.addFile(QStringLiteral(":/icons/mask-link.svg"), {}, QIcon::Normal, QIcon::On);
    linkIcon.addFile(QStringLiteral(":/icons/link-off.svg"), {}, QIcon::Normal, QIcon::Off);
    m_linkSize->setIcon(linkIcon);
    button_tips::setButtonTip(m_linkSize, tr("Keep the width and height in proportion"));
    m_linkSize->setChecked(AppOptions::settings().value(QLatin1String(kLinkSizeKey), false).toBool());
    noteProportion();
    connect(m_linkSize, &QToolButton::toggled, this, [this](bool on) {
        AppOptions::settings().setValue(QLatin1String(kLinkSizeKey), on);
        noteProportion();
    });
    auto* sizeRow = new QHBoxLayout;
    sizeRow->setContentsMargins(0, 0, 0, 0);
    sizeRow->setSpacing(Theme::metric(Theme::Metric::Gap));
    sizeRow->addWidget(m_linkSize);
    sizeRow->addWidget(m_sizeUnit, 1);
    grid->addLayout(sizeRow, row, 2, 2, 1, Qt::AlignVCenter);
    grid->addWidget(rightLabel(tr("Height :"), parent), row + 1, 0);
    grid->addWidget(m_height, row + 1, 1);
    row += 2;

    m_resolution = numberEdit(QStringLiteral("resolutionEdit"), parent);
    m_resolutionUnit = new QComboBox(parent);
    m_resolutionUnit->setObjectName(QStringLiteral("resolutionUnitCombo"));
    for (const ResolutionUnit u : {ResolutionUnit::PerInch, ResolutionUnit::PerCentimeter})
        m_resolutionUnit->addItem(resolutionUnitLabel(u), static_cast<int>(u));
    grid->addWidget(rightLabel(tr("Printing Resolution :"), parent), row, 0);
    grid->addWidget(m_resolution, row, 1);
    grid->addWidget(m_resolutionUnit, row, 2);
    ++row;

    // Background: the list is White, Black, Color, a separator, then the five Transparency entries.
    m_background = new QComboBox(parent);
    m_background->setObjectName(QStringLiteral("backgroundCombo"));
    if (m_allowNone)
        m_background->addItem(QStringLiteral("[None]"), -1);
    for (int i = 0; i < kBackgroundModeCount; ++i) {
        if (i == 3)
            m_background->insertSeparator(m_background->count());
        m_background->addItem(backgroundModeLabel(static_cast<BackgroundMode>(i)), i);
    }
    m_swatch = new QPushButton(parent);
    m_swatch->setObjectName(QStringLiteral("backgroundSwatch"));
    m_swatch->setFixedSize(40, Theme::comfortable::kControlMd);  // as tall as the select box beside it; 40 px wide (component metric)
    m_swatch->setToolTip(tr("Canvas Background Color"));
    auto* bgRow = new QHBoxLayout;
    bgRow->addWidget(m_background, 1);
    bgRow->addWidget(m_swatch);
    grid->addWidget(rightLabel(tr("Background :"), parent), row, 0);
    grid->addLayout(bgRow, row, 1, 1, 2);
    ++row;

    m_pixels = new QLabel(parent);
    m_pixels->setObjectName(QStringLiteral("pixelsLabel"));
    m_metrics = new QLabel(parent);
    m_metrics->setObjectName(QStringLiteral("metricsLabel"));
    grid->addWidget(rightLabel(tr("Pixels :"), parent), row, 0);
    grid->addWidget(m_pixels, row, 1, 1, 2);
    grid->addWidget(rightLabel(tr("Printing Metrics :"), parent), row + 1, 0);
    grid->addWidget(m_metrics, row + 1, 1, 1, 2);
    // Shown while the size asked for was limited.
    m_limit = new QLabel(tr("Canvases are at most %1 × %1 pixels.").arg(kMaxCanvasSide), parent);
    m_limit->setObjectName(QStringLiteral("limitLabel"));
    m_limit->setWordWrap(true);
    m_limit->setVisible(false);
    grid->addWidget(m_limit, row + 2, 1, 1, 2);
    row += 3;

    connect(m_width, &QLineEdit::textEdited, this, [this] { editNumber(m_width, &NewCanvasModel::setWidth); });
    connect(m_height, &QLineEdit::textEdited, this, [this] { editNumber(m_height, &NewCanvasModel::setHeight); });
    connect(m_resolution, &QLineEdit::textEdited, this, [this] { editNumber(m_resolution, &NewCanvasModel::setResolutionValue); });
    for (QLineEdit* e : {m_width, m_height, m_resolution})
        connect(e, &QLineEdit::editingFinished, this, &CanvasFieldsEditor::refreshFields);
    connect(m_sizeUnit, &QComboBox::currentIndexChanged, this, [this](int i) {
        if (m_syncing || i < 0)
            return;
        m_model.setSizeUnit(static_cast<SizeUnit>(m_sizeUnit->itemData(i).toInt()));
        refreshFields();
        emit edited();
        emit changed();
    });
    connect(m_resolutionUnit, &QComboBox::currentIndexChanged, this, [this](int i) {
        if (m_syncing || i < 0)
            return;
        m_model.setResolutionUnit(static_cast<ResolutionUnit>(m_resolutionUnit->itemData(i).toInt()));
        refreshFields();
        emit edited();
        emit changed();
    });
    connect(m_background, &QComboBox::currentIndexChanged, this, [this] { onBackgroundComboChanged(); });
    connect(m_swatch, &QPushButton::clicked, this, [this] {
        const auto picked = pickColor ? pickColor(m_model.background().color) : std::nullopt;
        if (!picked)
            return;
        CanvasBackground b = m_model.background();
        b.color = QColor(picked->red(), picked->green(), picked->blue());
        m_model.setBackground(b);
        if (m_choice)
            m_choice = b;
        refreshSwatch();
        emit changed();
    });

    m_syncing = true;
    m_background->setCurrentIndex(comboIndexOf(m_choice ? std::optional(m_choice->mode) : std::nullopt));
    m_syncing = false;
    refreshFields();
    return row;
}

int CanvasFieldsEditor::comboIndexOf(std::optional<BackgroundMode> mode) const {
    const int wanted = mode ? static_cast<int>(*mode) : -1;
    for (int i = 0; i < m_background->count(); ++i)
        if (m_background->itemData(i).isValid() && m_background->itemData(i).toInt() == wanted)
            return i;
    return 0;
}

std::optional<CanvasBackground> CanvasFieldsEditor::backgroundChoice() const {
    return m_choice;
}

void CanvasFieldsEditor::setBackgroundChoice(const std::optional<CanvasBackground>& choice) {
    m_choice = m_allowNone ? choice : std::optional<CanvasBackground>(choice.value_or(m_model.background()));
    if (m_choice)
        m_model.setBackground(*m_choice);
    m_syncing = true;
    m_background->setCurrentIndex(comboIndexOf(m_choice ? std::optional(m_choice->mode) : std::nullopt));
    m_syncing = false;
    refreshSwatch();
    emit changed();
}

void CanvasFieldsEditor::onBackgroundComboChanged() {
    if (m_syncing)
        return;
    const int data = m_background->currentData().isValid() ? m_background->currentData().toInt() : -1;
    if (data < 0) {
        m_choice = std::nullopt;
    } else {
        CanvasBackground b = m_model.background();
        b.mode = static_cast<BackgroundMode>(data);
        m_model.setBackground(b);
        m_choice = b;
    }
    refreshSwatch();
    emit changed();
}

void CanvasFieldsEditor::applyPreset(const CanvasPreset& preset, int index) {
    m_model.applyPreset(preset, index);
    noteProportion();
    if (preset.background)
        m_choice = m_model.background();
    m_syncing = true;
    m_background->setCurrentIndex(comboIndexOf(m_choice ? std::optional(m_choice->mode) : std::nullopt));
    m_syncing = false;
    refreshFields();
    emit changed();
}

void CanvasFieldsEditor::noteProportion() {
    if (m_model.width() > 0 && m_model.height() > 0)
        m_proportion = m_model.width() / m_model.height();
}

void CanvasFieldsEditor::keepProportion(const QLineEdit* edited) {
    if (!m_linkSize || !m_linkSize->isChecked() || m_proportion <= 0 || m_syncing)
        return;
    m_syncing = true;
    if (edited == m_width && m_model.width() > 0) {
        m_model.setHeight(m_model.width() / m_proportion);
        m_height->setText(formatLength(m_model.height(), m_model.sizeUnit()));
    } else if (edited == m_height && m_model.height() > 0) {
        m_model.setWidth(m_model.height() * m_proportion);
        m_width->setText(formatLength(m_model.width(), m_model.sizeUnit()));
    }
    m_syncing = false;
}

void CanvasFieldsEditor::editNumber(QLineEdit* edit, void (NewCanvasModel::*setter)(double)) {
    if (const auto v = parseNumber(edit->text())) {
        (m_model.*setter)(*v);
        keepProportion(edit);
        refreshReadouts();
        emit edited();
        emit changed();
    }
}

void CanvasFieldsEditor::refreshFields() {
    m_syncing = true;
    m_sizeUnit->setCurrentIndex(m_sizeUnit->findData(static_cast<int>(m_model.sizeUnit())));
    m_resolutionUnit->setCurrentIndex(m_resolutionUnit->findData(static_cast<int>(m_model.resolutionUnit())));
    m_width->setText(formatLength(m_model.width(), m_model.sizeUnit()));
    m_height->setText(formatLength(m_model.height(), m_model.sizeUnit()));
    m_resolution->setText(formatResolution(m_model.resolutionValue()));
    m_syncing = false;
    refreshSwatch();
    refreshReadouts();
}

void CanvasFieldsEditor::refreshReadouts() {
    m_pixels->setText(m_model.pixelsText());
    m_metrics->setText(m_model.metricsText());
    m_limit->setVisible(m_model.limited());
}

void CanvasFieldsEditor::refreshSwatch() {
    const QColor c = m_model.background().color;
    // The user's colour, so an inline sheet; its edge and corners are the theme's.
    m_swatch->setStyleSheet(QStringLiteral("background:%1; border:%2px solid %3; border-radius:%4px;")
                                .arg(c.name())
                                .arg(Theme::kBorder)
                                .arg(Theme::color(Token::Border).name())
                                .arg(Theme::kRadiusXs));
}

NewCanvasDialog::NewCanvasDialog(const QString& fileName, const NewCanvasValues& values, CanvasPresetList presets, QWidget* parent)
    : QDialog(parent), m_defaultName(fileName), m_presets(std::move(presets)), m_fields(new CanvasFieldsEditor(values, false, this)) {
    setWindowTitle(tr("New Canvas"));
    setMinimumWidth(480);  // component metric: the dialog's least width
    registerPreset = [this](const CanvasPreset& initial) -> std::optional<CanvasPreset> {
        PresetEditDialog dlg(initial, this);
        if (dlg.exec() != QDialog::Accepted)
            return std::nullopt;
        return dlg.preset();
    };
    configurePresets = [this](const CanvasPresetList& current, const NewCanvasValues& v) -> std::optional<CanvasPresetList> {
        PresetListDialog dlg(current, v, this);
        if (dlg.exec() != QDialog::Accepted)
            return std::nullopt;
        return dlg.presets();
    };

    auto* grid = new QGridLayout;
    Theme::spaceFields(grid);
    grid->setColumnStretch(1, 1);
    m_fileName = new QLineEdit(fileName, this);
    m_fileName->setObjectName(QStringLiteral("fileNameEdit"));
    grid->addWidget(rightLabel(tr("File Name :"), this), 0, 0);
    grid->addWidget(m_fileName, 0, 1, 1, 2);

    m_presetCombo = new QComboBox(this);
    m_presetCombo->setObjectName(QStringLiteral("presetCombo"));
    m_configure = new QToolButton(this);
    m_configure->setObjectName(QStringLiteral("presetConfigureButton"));
    m_configure->setIcon(QIcon(QStringLiteral(":/icons/wrench.svg")));
    m_configure->setToolTip(tr("Configure Canvas Presets"));
    m_register = new QToolButton(this);
    m_register->setObjectName(QStringLiteral("presetRegisterButton"));
    m_register->setIcon(QIcon(QStringLiteral(":/icons/plus.svg")));
    m_register->setToolTip(tr("Register New Canvas Preset"));
    auto* presetRow = new QHBoxLayout;
    presetRow->addWidget(m_presetCombo, 1);
    presetRow->addWidget(m_configure);
    presetRow->addWidget(m_register);
    grid->addWidget(rightLabel(tr("Preset :"), this), 1, 0);
    grid->addLayout(presetRow, 1, 1, 1, 2);

    const int next = m_fields->build(grid, 2, this);
    Q_UNUSED(next)
    fillPresetCombo(-1);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    auto* layout = new QVBoxLayout(this);
    Theme::padDialog(layout);
    layout->addLayout(grid);
    layout->addWidget(buttons);

    connect(m_presetCombo, &QComboBox::activated, this, [this](int i) { onPresetChosen(i); });
    connect(m_fields, &CanvasFieldsEditor::edited, this, [this] {
        const QSignalBlocker block(m_presetCombo);
        m_presetCombo->setCurrentIndex(0);
    });
    connect(m_configure, &QToolButton::clicked, this, &NewCanvasDialog::openConfigureDialog);
    connect(m_register, &QToolButton::clicked, this, &NewCanvasDialog::openRegisterDialog);

    m_fields->widthEdit()->setFocus();  // opens with the Width field active and its text selected
    m_fields->widthEdit()->selectAll();
}

// About where a new canvas starts to take a noticeable while: 4000 px a side is 222 ms from OK to its first picture, 8000 is 570.
bool NewCanvasDialog::takesAWhile(QSize pixels) {
    constexpr qint64 kFromPixels = 30'000'000;
    return qint64(pixels.width()) * pixels.height() >= kFromPixels;
}

void NewCanvasDialog::accept() {
    if (m_creating || !createWhileShowing || !takesAWhile(answer().values.pixels)) {
        if (!m_creating)
            QDialog::accept();
        return;
    }
    showCreating();
    // The picture on the screen is made on this thread, so the dialog says what's happening before that starts.
    QMetaObject::invokeMethod(
        this,
        [this] {
            createWhileShowing(answer(), [this] {
                QApplication::restoreOverrideCursor();
                m_creating = false;
                QDialog::accept();
            });
        },
        Qt::QueuedConnection);
}

void NewCanvasDialog::reject() {
    if (!m_creating)
        QDialog::reject();
}

// A bar that moves when it can, and words and a wait cursor for while the first picture blocks the window.
void NewCanvasDialog::showCreating() {
    m_creating = true;
    for (QWidget* child : findChildren<QWidget*>())
        child->setEnabled(false);
    auto* working = new QLabel(tr("Creating the canvas…"), this);
    working->setObjectName(QStringLiteral("newCanvasWorkingLabel"));
    auto* bar = new QProgressBar(this);
    bar->setObjectName(QStringLiteral("newCanvasWorking"));
    bar->setRange(0, 0);
    bar->setTextVisible(false);
    auto* column = qobject_cast<QVBoxLayout*>(layout());
    column->addWidget(working);
    column->addWidget(bar);
    QApplication::setOverrideCursor(Qt::WaitCursor);
    repaint();
}

NewCanvasAnswer NewCanvasDialog::answer() const {
    const QString name = m_fileName->text().trimmed();
    return {name.isEmpty() ? m_defaultName : name, m_fields->model().values()};
}

void NewCanvasDialog::fillPresetCombo(int selected) {
    const QSignalBlocker block(m_presetCombo);
    m_presetCombo->clear();
    m_presetCombo->addItem(QStringLiteral("----"));
    for (const CanvasPreset& p : m_presets) {
        if (p.separator)
            m_presetCombo->insertSeparator(m_presetCombo->count());
        else
            m_presetCombo->addItem(presetLabel(p));
    }
    // "----" is what the combo shows while no preset applies; it is not an entry of the list.
    if (auto* view = qobject_cast<QListView*>(m_presetCombo->view()))
        view->setRowHidden(0, true);
    m_presetCombo->setCurrentIndex(selected >= 0 ? selected + 1 : 0);
}

void NewCanvasDialog::onPresetChosen(int comboIndex) {
    const int index = comboIndex - 1;
    if (index < 0 || index >= static_cast<int>(m_presets.size()) || m_presets[static_cast<std::size_t>(index)].separator) {
        const QSignalBlocker block(m_presetCombo);
        m_presetCombo->setCurrentIndex(m_fields->model().activePreset() + 1);
        return;
    }
    m_fields->applyPreset(m_presets[static_cast<std::size_t>(index)], index);
    const QSignalBlocker block(m_presetCombo);
    m_presetCombo->setCurrentIndex(comboIndex);
}

CanvasPreset NewCanvasDialog::presetFromFields() const {
    const NewCanvasModel& m = m_fields->model();
    CanvasPreset p;
    p.width = m.width();
    p.height = m.height();
    p.sizeUnit = m.sizeUnit();
    p.resolution = m.resolutionValue();
    p.resolutionUnit = m.resolutionUnit();
    return p;
}

void NewCanvasDialog::openRegisterDialog() {
    const auto made = registerPreset(presetFromFields());
    if (!made)
        return;
    m_presets.push_back(*made);
    const int index = static_cast<int>(m_presets.size()) - 1;
    fillPresetCombo(index);
    m_fields->applyPreset(*made, index);
    emit presetsChanged(m_presets);
}

void NewCanvasDialog::openConfigureDialog() {
    const auto edited = configurePresets(m_presets, m_fields->model().values());
    if (!edited)
        return;
    m_presets = *edited;
    fillPresetCombo(-1);
    emit presetsChanged(m_presets);
}

PresetEditDialog::PresetEditDialog(const CanvasPreset& initial, QWidget* parent)
    : QDialog(parent), m_fields(new CanvasFieldsEditor(NewCanvasValues{}, true, this)) {
    setWindowTitle(tr("Register New Canvas Preset"));
    setMinimumWidth(440);  // component metric: the preset dialog's least width
    auto* grid = new QGridLayout;
    grid->setColumnStretch(1, 1);
    m_name = new QLineEdit(initial.name, this);
    m_name->setObjectName(QStringLiteral("presetNameEdit"));
    grid->addWidget(rightLabel(tr("Preset Name :"), this), 0, 0);
    grid->addWidget(m_name, 0, 1, 1, 2);
    m_fields->build(grid, 1, this);
    CanvasPreset start = initial;
    if (start.width <= 0 || start.height <= 0) {
        start.width = 1000;
        start.height = 1000;
        start.sizeUnit = SizeUnit::Pixels;
    }
    m_fields->applyPreset(start, -1);
    m_fields->setBackgroundChoice(initial.background);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    m_ok = buttons->button(QDialogButtonBox::Ok);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(m_name, &QLineEdit::textChanged, this, [this](const QString& t) { m_ok->setEnabled(!t.trimmed().isEmpty()); });
    m_ok->setEnabled(!initial.name.trimmed().isEmpty());
    auto* layout = new QVBoxLayout(this);
    Theme::padDialog(layout);
    layout->addLayout(grid);
    layout->addWidget(buttons);
    m_name->setFocus();
}

CanvasPreset PresetEditDialog::preset() const {
    const NewCanvasModel& m = m_fields->model();
    CanvasPreset p;
    p.name = m_name->text().trimmed();
    p.width = m.width();
    p.height = m.height();
    p.sizeUnit = m.sizeUnit();
    p.resolution = m.resolutionValue();
    p.resolutionUnit = m.resolutionUnit();
    p.background = m_fields->backgroundChoice();
    return p;
}

PresetListDialog::PresetListDialog(const CanvasPresetList& presets, const NewCanvasValues& current, QWidget* parent)
    : QDialog(parent), m_current(current), m_list(new QListWidget(this)) {
    setWindowTitle(tr("Configure Canvas Presets"));
    resize(400, 400);
    editPreset = [this](const CanvasPreset& initial) -> std::optional<CanvasPreset> {
        PresetEditDialog dlg(initial, this);
        if (dlg.exec() != QDialog::Accepted)
            return std::nullopt;
        return dlg.preset();
    };

    const auto tool = [this](const char* name, const char* icon, const QString& tip) {
        auto* b = new QToolButton(this);
        b->setObjectName(QString::fromLatin1(name));
        b->setIcon(QIcon(QString::fromLatin1(icon)));
        b->setToolTip(tip);
        return b;
    };
    m_create = tool("presetCreateButton", ":/icons/preset-new.svg", tr("Create a new preset and insert it to the selected position."));
    m_separator = tool("presetSeparatorButton", ":/icons/preset-separator.svg", tr("Insert a separator to the selected position."));
    m_modify = tool("presetModifyButton", ":/icons/preset-edit.svg", tr("Modify the selected preset."));
    m_delete = tool("presetDeleteButton", ":/icons/layer-delete.svg", tr("Delete the selected preset."));
    auto* bar = new QHBoxLayout;
    for (QToolButton* b : {m_create, m_separator, m_modify, m_delete})
        bar->addWidget(b);
    bar->addStretch(1);

    m_list->setObjectName(QStringLiteral("presetList"));
    m_list->setItemDelegate(new PresetDelegate(m_list));
    m_list->setSelectionMode(QAbstractItemView::SingleSelection);
    for (const CanvasPreset& p : presets)
        addItem(p, static_cast<int>(m_data.size()));
    m_list->setCurrentRow(-1);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    auto* layout = new QVBoxLayout(this);
    Theme::padDialog(layout, true);
    layout->addLayout(bar);
    layout->addWidget(m_list, 1);
    layout->addWidget(buttons);

    connect(m_list, &QListWidget::currentRowChanged, this, [this] { updateButtons(); });
    connect(m_create, &QToolButton::clicked, this, [this] {
        CanvasPreset start;
        start.width = m_current.pixels.width();
        start.height = m_current.pixels.height();
        const auto made = editPreset(start);
        if (made)
            addItem(*made, insertionRow());
    });
    connect(m_separator, &QToolButton::clicked, this, [this] { addItem(CanvasPreset::makeSeparator(), insertionRow()); });
    connect(m_modify, &QToolButton::clicked, this, [this] {
        const int row = m_list->currentRow();
        if (row < 0 || m_data[static_cast<std::size_t>(row)].separator)
            return;
        if (const auto made = editPreset(m_data[static_cast<std::size_t>(row)])) {
            m_data[static_cast<std::size_t>(row)] = *made;
            m_list->item(row)->setText(presetLabel(*made));
        }
    });
    connect(m_delete, &QToolButton::clicked, this, [this] {
        const int row = m_list->currentRow();
        if (row < 0)
            return;
        m_data.erase(m_data.begin() + row);
        delete m_list->takeItem(row);
    });
    updateButtons();
}

CanvasPresetList PresetListDialog::presets() const {
    return m_data;
}

int PresetListDialog::insertionRow() const {
    const int row = m_list->currentRow();
    return row < 0 ? static_cast<int>(m_data.size()) : row;
}

void PresetListDialog::addItem(const CanvasPreset& preset, int row) {
    m_data.insert(m_data.begin() + row, preset);
    auto* item = new QListWidgetItem(presetLabel(preset));
    item->setData(kSeparatorRole, preset.separator);
    m_list->insertItem(row, item);
    m_list->setCurrentRow(row);
}

void PresetListDialog::updateButtons() {
    const int row = m_list->currentRow();
    m_delete->setEnabled(row >= 0);
    m_modify->setEnabled(row >= 0 && !m_data[static_cast<std::size_t>(row)].separator);
}

}
