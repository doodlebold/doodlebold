#include "ui/EffectDialog.h"

#include "ui/Theme.h"
#include "ui/ValueBar.h"

#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QProgressBar>
#include <QPushButton>
#include <QRandomGenerator>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace doodlebold {

namespace {

constexpr int kSliderWidth = 240;  // component metric: an effect's value bar

QString translated(const char* text) {
    return QCoreApplication::translate("Effects", text);
}

}

EffectDialog::EffectDialog(EffectKind kind, QSize canvas, QWidget* parent)
    : QDialog(parent), m_settings(EffectSettings::start(kind, canvas)) {
    const EffectSpec& spec = effectSpec(kind);
    setObjectName(QStringLiteral("effectDialog"));
    setWindowTitle(effectTitle(kind));
    m_settings.seed = QRandomGenerator::global()->generate();  // fresh grain each time Add Noise opens

    auto* form = new QFormLayout;
    form->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
    if (!spec.modes.empty()) {
        m_mode = new QComboBox(this);
        m_mode->setObjectName(QStringLiteral("effectMode"));
        for (const char* mode : spec.modes)
            m_mode->addItem(translated(mode));
        connect(m_mode, &QComboBox::currentIndexChanged, this, [this](int mode) {
            m_settings.mode = mode;
            enableForMode();
            emit settingsChanged(m_settings);
        });
        form->addRow(translated("Mode :"), m_mode);
    }
    for (std::size_t i = 0; i < spec.sliders.size(); ++i)
        addSlider(form, static_cast<int>(i), spec.sliders[i], canvas);
    enableForMode();
    if (spec.option) {
        m_option = new QCheckBox(translated(spec.option), this);
        m_option->setObjectName(QStringLiteral("effectOption"));
        m_option->setChecked(spec.optionStart);
        connect(m_option, &QCheckBox::toggled, this, [this](bool on) {
            m_settings.option = on;
            emit settingsChanged(m_settings);
        });
        form->addRow(QString(), m_option);
    }
    m_preview = new QCheckBox(tr("Preview"), this);
    m_preview->setObjectName(QStringLiteral("effectPreview"));
    m_preview->setChecked(true);
    connect(m_preview, &QCheckBox::toggled, this, &EffectDialog::previewToggled);
    form->addRow(QString(), m_preview);

    m_progress = new QProgressBar(this);
    m_progress->setObjectName(QStringLiteral("effectProgress"));
    m_progress->setRange(0, 100);
    m_progress->setTextVisible(false);
    m_progress->hide();

    m_buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(m_buttons, &QDialogButtonBox::accepted, this, &EffectDialog::okRequested);
    connect(m_buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto* layout = new QVBoxLayout(this);
    Theme::padDialog(layout);
    layout->addLayout(form);
    layout->addWidget(m_progress);
    layout->addWidget(m_buttons);
}

void EffectDialog::addSlider(QFormLayout* form, int i, const EffectSlider& spec, QSize canvas) {
    auto* bar = new ValueBar(this);
    bar->setObjectName(QStringLiteral("effectSlider%1").arg(i));
    switch (spec.span) {
    case SliderSpan::Fixed: bar->setRange(spec.minimum, spec.maximum); break;
    case SliderSpan::CanvasWidth: bar->setRange(0, std::max(1, canvas.width())); break;
    case SliderSpan::CanvasHeight: bar->setRange(0, std::max(1, canvas.height())); break;
    }
    bar->setDecimals(spec.decimals);
    bar->setSuffix(QString::fromUtf8(spec.suffix));
    bar->setValue(m_settings.value(i));
    m_settings.values[static_cast<std::size_t>(i)] = bar->value();  // as the bar rounds it (an odd canvas's centre)
    bar->setMinimumWidth(kSliderWidth);
    if (spec.logarithmic) {
        const double low = std::log(spec.minimum), span = std::log(spec.maximum) - low;
        bar->setMapping([low, span](double v) { return (std::log(v) - low) / span; },
                        [low, span](double p) { return std::exp(low + p * span); });
    }
    connect(bar, &ValueBar::valueChanged, this, [this, i](double v) {
        m_settings.values[static_cast<std::size_t>(i)] = v;
        emit settingsChanged(m_settings);
    });
    m_sliders.push_back(bar);
    form->addRow(translated(spec.label), bar);
}

void EffectDialog::enableForMode() {
    const EffectSpec& spec = effectSpec(m_settings.kind);
    for (std::size_t i = 0; i < m_sliders.size(); ++i) {
        const int only = spec.sliders[i].onlyInMode;
        m_sliders[i]->setEnabled(!m_applying && (only < 0 || only == m_settings.mode));
    }
}

bool EffectDialog::isPreviewOn() const {
    return m_preview->isChecked();
}

void EffectDialog::setValue(int i, double value) {
    slider(i)->setValue(value);
}

void EffectDialog::showApplying(double progress) {
    if (!m_applying) {
        m_applying = true;
        for (ValueBar* bar : m_sliders)
            bar->setEnabled(false);
        if (m_option)
            m_option->setEnabled(false);
        if (m_mode)
            m_mode->setEnabled(false);
        m_preview->setEnabled(false);
        m_buttons->button(QDialogButtonBox::Ok)->setEnabled(false);
        m_progress->show();
    }
    m_progress->setValue(static_cast<int>(progress * 100));
}

}
