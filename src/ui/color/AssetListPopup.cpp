#include "ui/color/AssetListPopup.h"

#include "ui/ButtonTips.h"
#include "ui/Components.h"
#include "ui/Theme.h"

#include <QHBoxLayout>
#include <QInputDialog>
#include <QListWidget>
#include <QMessageBox>
#include <QToolButton>
#include <QVBoxLayout>

namespace doodlebold {

AssetListPopup::AssetListPopup(const QString& kind, const std::vector<QString>& names, int current, Actions actions, QWidget* parent)
    : QFrame(parent, Qt::Popup), m_kind(kind), m_actions(std::move(actions)) {
    setObjectName(QStringLiteral("assetListPopup"));
    setAttribute(Qt::WA_DeleteOnClose);
    setFrameShape(QFrame::StyledPanel);
    m_list = new QListWidget(this);
    for (const QString& n : names)
        m_list->addItem(n);
    m_list->setCurrentRow(current);
    m_list->setMinimumSize(180, 220);  // px: a dozen names (component metric)

    const auto button = [this](const char* icon, const QString& tip, const char* name) {
        auto* b = new QToolButton(this);
        b->setObjectName(QLatin1String(name));
        b->setIcon(QIcon(QStringLiteral(":/icons/%1.svg").arg(QLatin1String(icon))));
        components::toolButton(b);
        button_tips::setButtonTip(b, tip);
        b->setAutoRaise(true);
        return b;
    };
    QToolButton* add = button("preset-new", tr("New %1").arg(kind), "assetNew");
    QToolButton* remove = button("layer-delete", tr("Delete %1").arg(kind), "assetDelete");
    QToolButton* rename = button("preset-edit", tr("Rename %1").arg(kind), "assetRename");
    remove->setEnabled(names.size() > 1);

    auto* buttons = new QHBoxLayout;
    buttons->addStretch(1);
    buttons->addWidget(add);
    buttons->addWidget(remove);
    buttons->addWidget(rename);
    auto* layout = new QVBoxLayout(this);
    const int pad = Theme::metric(Theme::Metric::Padding);
    layout->setContentsMargins(pad, pad, pad, pad);
    layout->addWidget(m_list);
    layout->addLayout(buttons);

    connect(m_list, &QListWidget::itemClicked, this, [this](QListWidgetItem* item) {
        if (m_actions.choose)
            m_actions.choose(m_list->row(item));
        close();
    });
    connect(add, &QToolButton::clicked, this, [this] {
        if (m_actions.add)
            m_actions.add();
        close();
    });
    connect(remove, &QToolButton::clicked, this, [this] {
        const int row = m_list->currentRow();
        if (row < 0 || !m_actions.remove)
            return;
        const auto answer = QMessageBox::question(this, m_kind, tr("Delete \"%1\"?").arg(m_list->item(row)->text()));
        if (answer == QMessageBox::Yes)
            m_actions.remove(row);
        close();
    });
    connect(rename, &QToolButton::clicked, this, [this] {
        const int row = m_list->currentRow();
        if (row < 0 || !m_actions.rename)
            return;
        bool ok = false;
        const QString name =
            QInputDialog::getText(this, tr("Rename %1").arg(m_kind), tr("Name:"), QLineEdit::Normal, m_list->item(row)->text(), &ok);
        if (ok && !name.trimmed().isEmpty())
            m_actions.rename(row, name);
        close();
    });
}

}
