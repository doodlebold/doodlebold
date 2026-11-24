#include "ui/PackImportDialog.h"

#include "ui/Theme.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QTableWidget>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <map>

namespace doodlebold {

namespace {

QString kindsPlural(PackUse use, int count) {
    switch (use) {
    case PackUse::BlotMap: return PackImportDialog::tr("%n blot map(s)", nullptr, count);
    case PackUse::Bristle: return PackImportDialog::tr("%n bristle(s)", nullptr, count);
    case PackUse::Shape: return PackImportDialog::tr("%n brush shape(s)", nullptr, count);
    case PackUse::Texture: return PackImportDialog::tr("%n brush texture(s)", nullptr, count);
    case PackUse::PaperTexture: return PackImportDialog::tr("%n paper texture(s)", nullptr, count);
    case PackUse::Skip:
    case PackUse::TextureAndPaper: break;
    }
    return {};
}

}

PackImportDialog::PackImportDialog(const BrushPack& pack, QWidget* parent) : QDialog(parent), m_pack(pack) {
    setObjectName(QStringLiteral("packImportDialog"));
    setWindowTitle(tr("Install \"%1\"").arg(pack.name));

    auto* intro = new QLabel(
        tr("%n picture(s) in %1. Tick the folders to keep and choose what each one's pictures become.", nullptr, pack.pictureCount())
            .arg(tr("%n folder(s)", nullptr, int(pack.folders.size()))));
    intro->setWordWrap(true);
    intro->setProperty("role", "hint");

    m_table = new QTableWidget(int(pack.folders.size()), 5);
    m_table->setObjectName(QStringLiteral("packFolders"));
    m_table->setHorizontalHeaderLabels({tr("Folder"), tr("Pictures"), tr("Install as"), tr("Result"), tr("Why")});
    m_table->verticalHeader()->hide();
    m_table->setSelectionMode(QAbstractItemView::NoSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    for (int column : {FolderColumn, PicturesColumn, UseColumn, ResultColumn})
        m_table->horizontalHeader()->setSectionResizeMode(column, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(WhyColumn, QHeaderView::Stretch);
    for (int row = 0; row < int(pack.folders.size()); ++row) {
        const PackFolder& folder = pack.folders[std::size_t(row)];
        auto* name = new QTableWidgetItem(folder.name.isEmpty() ? tr("(top of the pack)") : folder.name);
        name->setFlags(Qt::ItemIsEnabled | Qt::ItemIsUserCheckable);
        name->setCheckState(folder.guess == PackUse::Skip ? Qt::Unchecked : Qt::Checked);
        m_table->setItem(row, FolderColumn, name);
        auto* count = new QTableWidgetItem(QString::number(folder.pictures.size()));
        count->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        m_table->setItem(row, PicturesColumn, count);
        auto* use = new QComboBox;
        for (PackUse u : kPackUses)
            use->addItem(packUseName(u), static_cast<int>(u));
        use->setCurrentIndex(use->findData(static_cast<int>(folder.guess)));
        m_table->setCellWidget(row, UseColumn, use);
        m_table->setItem(row, ResultColumn, new QTableWidgetItem);
        m_table->setItem(row, WhyColumn, new QTableWidgetItem(folder.why));
        connect(use, &QComboBox::currentIndexChanged, this, [this, row, use] {
            if (m_refreshing)
                return;
            setUse(row, static_cast<PackUse>(use->currentData().toInt()));
        });
    }
    connect(m_table, &QTableWidget::itemChanged, this, [this](QTableWidgetItem* item) {
        if (!m_refreshing && item->column() == FolderColumn)
            setKept(item->row(), item->checkState() == Qt::Checked);
    });

    m_summary = new QLabel;
    m_summary->setObjectName(QStringLiteral("packSummary"));
    m_summary->setWordWrap(true);

    m_leftOut = new QTreeWidget;
    m_leftOut->setObjectName(QStringLiteral("packLeftOut"));
    m_leftOut->setHeaderHidden(true);
    if (!pack.leftOut.isEmpty()) {
        auto* group = new QTreeWidgetItem(m_leftOut, {tr("Not a picture: %n file(s)", nullptr, int(pack.leftOut.size()))});
        for (const QString& file : pack.leftOut)
            new QTreeWidgetItem(group, {file});
    }

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("Install"));
    buttons->button(QDialogButtonBox::Ok)->setDefault(true);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto* layout = new QVBoxLayout(this);
    Theme::padDialog(layout, true);
    layout->addWidget(intro);
    layout->addWidget(m_table, 2);
    layout->addWidget(m_summary);
    auto* leftOutTitle = new QLabel(tr("Left out"));
    leftOutTitle->setProperty("role", "sectionTitle");
    layout->addWidget(leftOutTitle);
    layout->addWidget(m_leftOut, 1);
    layout->addWidget(buttons);
    resize(840, 560);
    refresh();
}

PackUse PackImportDialog::chosen(int folder) const {
    const auto* use = qobject_cast<QComboBox*>(m_table->cellWidget(folder, UseColumn));
    return static_cast<PackUse>(use->currentData().toInt());
}

std::vector<PackUse> PackImportDialog::uses() const {
    std::vector<PackUse> out;
    out.reserve(m_table->rowCount());
    for (int row = 0; row < m_table->rowCount(); ++row)
        out.push_back(m_table->item(row, FolderColumn)->checkState() == Qt::Checked ? chosen(row) : PackUse::Skip);
    return out;
}

void PackImportDialog::setUse(int folder, PackUse use) {
    m_refreshing = true;
    auto* combo = qobject_cast<QComboBox*>(m_table->cellWidget(folder, UseColumn));
    combo->setCurrentIndex(combo->findData(static_cast<int>(use)));
    m_table->item(folder, FolderColumn)->setCheckState(use == PackUse::Skip ? Qt::Unchecked : Qt::Checked);
    m_refreshing = false;
    refresh();
}

// Ticking a folder that was set not to install makes its pictures brush shapes, the commonest kind in a pack.
void PackImportDialog::setKept(int folder, bool kept) {
    if (kept && chosen(folder) == PackUse::Skip) {
        setUse(folder, PackUse::Shape);
        return;
    }
    m_refreshing = true;
    m_table->item(folder, FolderColumn)->setCheckState(kept ? Qt::Checked : Qt::Unchecked);
    m_refreshing = false;
    refresh();
}

QString PackImportDialog::summary() const {
    return m_summary->text();
}

void PackImportDialog::refresh() {
    m_refreshing = true;
    const std::vector<PackUse> chosenUses = uses();
    std::map<PackUse, int> counts;
    for (int row = 0; row < m_table->rowCount(); ++row) {
        const PackUse use = chosenUses[std::size_t(row)];
        const int pictures = int(m_pack.folders[std::size_t(row)].pictures.size());
        m_table->item(row, ResultColumn)
            ->setText(use == PackUse::Skip ? tr("%n left out", nullptr, pictures) : tr("%n new", nullptr, pictures * picturesPerFile(use)));
        if (use == PackUse::TextureAndPaper) {
            counts[PackUse::Texture] += pictures;
            counts[PackUse::PaperTexture] += pictures;
        } else if (use != PackUse::Skip) {
            counts[use] += pictures;
        }
    }
    QStringList parts;
    for (const auto& [use, count] : counts)
        parts << kindsPlural(use, count);
    m_summary->setText(parts.isEmpty() ? tr("Nothing is ticked: nothing will be installed.")
                                       : tr("Will install %1.").arg(parts.join(QStringLiteral(", "))));
    if (auto* ok = findChild<QDialogButtonBox*>())
        ok->button(QDialogButtonBox::Ok)->setEnabled(!parts.isEmpty());
    m_refreshing = false;
}

}
