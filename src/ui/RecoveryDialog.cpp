#include "ui/RecoveryDialog.h"

#include "ui/Components.h"
#include "ui/RecoveryController.h"
#include "ui/Theme.h"

#include <QDialogButtonBox>
#include <QDir>
#include <QHBoxLayout>
#include <QLabel>
#include <QLocale>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>

#include <algorithm>

namespace doodlebold {

namespace {

QString detailsOf(const RecoveryManifest& manifest) {
    const QString when = QLocale().toString(manifest.savedAt.toLocalTime(), QLocale::ShortFormat);
    const QString where = manifest.originalPath.isEmpty() ? RecoveryDialog::tr("never saved to a file")
                                                          : QDir::toNativeSeparators(manifest.originalPath);
    return RecoveryDialog::tr("Copied %1 · %2").arg(when, where);
}

}

RecoveryDialog::RecoveryDialog(RecoveryController* recovery, const std::vector<LeftoverCanvas>& canvases, QWidget* parent)
    : QDialog(parent), m_recovery(recovery) {
    setObjectName(QStringLiteral("recoveryDialog"));
    setWindowTitle(tr("Recover Unsaved Work"));

    auto* intro = components::mutedLabel(tr("Doodlebold didn't close normally the last time it ran, and these canvases had "
                                            "changes that weren't saved. Restore opens a canvas as its last recovery copy has it, "
                                            "under a new name: save it to keep it. Discard deletes the copy. Close keeps the rest "
                                            "for the next start."),
                                         this);

    auto* list = new QWidget;
    m_list = new QVBoxLayout(list);
    m_list->setContentsMargins(0, 0, 0, 0);
    m_list->setSpacing(Theme::metric(Theme::Metric::FieldSpacing));
    for (const LeftoverCanvas& canvas : canvases)
        addRow(canvas);
    m_list->addStretch(1);
    // Scrolls once there are more canvases than the screen has room for.
    auto* scroll = new QScrollArea(this);
    scroll->setObjectName(QStringLiteral("recoveryList"));
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setWidget(list);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    QPushButton* restoreAll = buttons->addButton(tr("Restore All"), QDialogButtonBox::ActionRole);
    restoreAll->setObjectName(QStringLiteral("recoveryRestoreAll"));
    restoreAll->setDefault(true);
    QPushButton* discardAll = buttons->addButton(tr("Discard All"), QDialogButtonBox::ActionRole);
    discardAll->setObjectName(QStringLiteral("recoveryDiscardAll"));
    connect(restoreAll, &QPushButton::clicked, this, [this] { settleAll(true); });
    connect(discardAll, &QPushButton::clicked, this, [this] { settleAll(false); });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto* layout = new QVBoxLayout(this);
    Theme::padDialog(layout, true);
    layout->addWidget(intro);
    layout->addWidget(scroll, 1);
    layout->addWidget(buttons);
}

void RecoveryDialog::addRow(const LeftoverCanvas& canvas) {
    const RecoveryManifest& newest = canvas.copies.front().manifest;
    auto* row = new QWidget;
    row->setObjectName(QStringLiteral("recoveryRow"));
    auto* name = new QLabel(newest.name, row);
    name->setObjectName(QStringLiteral("recoveryName"));
    name->setFont(Theme::font(Theme::TextRole::BodyStrong));
    QLabel* details = components::mutedLabel(detailsOf(newest), row);
    details->setObjectName(QStringLiteral("recoveryDetails"));
    auto* text = new QVBoxLayout;
    text->setSpacing(Theme::kSpaceHair);
    text->addWidget(name);
    text->addWidget(details);

    auto* restore = new QPushButton(tr("Restore"), row);
    restore->setObjectName(QStringLiteral("recoveryRestore"));
    auto* discard = new QPushButton(tr("Discard"), row);
    discard->setObjectName(QStringLiteral("recoveryDiscard"));
    connect(restore, &QPushButton::clicked, this, [this, row] { settle(row, true); });
    connect(discard, &QPushButton::clicked, this, [this, row] { settle(row, false); });

    auto* layout = new QHBoxLayout(row);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(Theme::metric(Theme::Metric::FieldGap));
    layout->addLayout(text, 1);
    layout->addWidget(restore);
    layout->addWidget(discard);
    m_list->addWidget(row);
    m_rows.push_back({canvas, row, details, restore});
}

void RecoveryDialog::settle(QWidget* row, bool restore) {
    const auto it = std::ranges::find(m_rows, row, &Row::widget);
    if (it == m_rows.end())
        return;
    if (restore && !m_recovery->restore(it->canvas)) {
        it->details->setText(tr("None of this canvas's copies could be read."));
        it->restore->setEnabled(false);
        return;
    }
    if (!restore)
        m_recovery->discard(it->canvas);
    it->widget->hide();
    it->widget->deleteLater();  // later: its own button may be what asked
    m_rows.erase(it);
    if (m_rows.empty())
        accept();
}

void RecoveryDialog::settleAll(bool restore) {
    std::vector<QWidget*> rows;
    for (const Row& r : m_rows)
        if (!restore || r.restore->isEnabled())
            rows.push_back(r.widget);
    for (QWidget* row : rows)
        settle(row, restore);
}

}
