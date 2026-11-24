#include "ui/SelectionGrowDialog.h"

#include "ui/Theme.h"

#include <QDialogButtonBox>
#include <QFormLayout>
#include <QSpinBox>
#include <QVBoxLayout>

namespace doodlebold {

SelectionGrowDialog::SelectionGrowDialog(bool dilate, QWidget* parent) : QDialog(parent), m_dilate(dilate) {
    setObjectName(QStringLiteral("selectionGrowDialog"));
    setWindowTitle(dilate ? tr("Grow Selection") : tr("Shrink Selection"));
    m_width = new QSpinBox(this);
    m_width->setObjectName(QStringLiteral("widthSpin"));
    m_width->setRange(1, kMaxWidth);
    m_width->setValue(3);
    m_width->setSuffix(tr(" px"));
    auto* form = new QFormLayout;
    form->addRow(tr("Width:"), m_width);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    auto* layout = new QVBoxLayout(this);
    Theme::padDialog(layout);
    layout->addLayout(form);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

int SelectionGrowDialog::growWidth() const {
    return m_width->value();
}

void SelectionGrowDialog::setGrowWidth(int pixels) {
    m_width->setValue(pixels);
}

}
