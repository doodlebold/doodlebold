#include "ui/ResourcesPage.h"

#include "core/brush/BrushPack.h"
#include "core/tools/ToolBox.h"
#include "ui/PackImportDialog.h"
#include "ui/Components.h"
#include "ui/Theme.h"
#include "ui/panels/BrushResourceList.h"

#include <QComboBox>
#include <QDesktopServices>
#include <QDir>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QHBoxLayout>
#include <QImageReader>
#include <QInputDialog>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QMimeData>
#include <QPushButton>
#include <QUrl>
#include <QVBoxLayout>

namespace doodlebold {

namespace {

constexpr int kThumbnail = 72;  // px: a picture in the grid (component metric, the user's pictures)
constexpr QSize kCell(112, 108);  // px: a picture and its name (component metric)
constexpr int kNameRole = Qt::UserRole + 1;

QString kindLabel(ResourceKind kind) {
    switch (kind) {
    case ResourceKind::BlotMap: return ResourcesPage::tr("Blot Maps");
    case ResourceKind::Bristle: return ResourcesPage::tr("Bristles");
    case ResourceKind::Shape: return ResourcesPage::tr("Brush Shapes");
    case ResourceKind::Texture: return ResourcesPage::tr("Brush Textures");
    case ResourceKind::PaperTexture: return ResourcesPage::tr("Paper Textures");
    }
    return {};
}

QStringList droppedImages(const QMimeData* mime) {
    QStringList out;
    if (!mime || !mime->hasUrls())
        return out;
    for (const QUrl& url : mime->urls())
        if (url.isLocalFile() && !QImageReader::imageFormat(url.toLocalFile()).isEmpty())
            out << url.toLocalFile();
    return out;
}

QPushButton* button(const QString& text, const char* name) {
    auto* b = new QPushButton(text);
    b->setObjectName(QString::fromLatin1(name));
    return b;
}

}

ResourcesPage::ResourcesPage(BrushResourceLibrary* library, ToolBox* tools, QWidget* parent)
    : QWidget(parent), m_library(library), m_tools(tools) {
    setObjectName(QStringLiteral("resourcesPage"));
    setAcceptDrops(true);

    QVBoxLayout* body = nullptr;
    QFrame* card = components::sectionCard(tr("Brush Forms and Textures"), body);

    auto* showRow = new QHBoxLayout;
    showRow->addWidget(new QLabel(tr("Show:")));
    m_kinds = new QComboBox;
    m_kinds->setObjectName(QStringLiteral("resourceKindCombo"));
    for (ResourceKind kind : kResourceKinds)
        m_kinds->addItem(kindLabel(kind), static_cast<int>(kind));
    showRow->addWidget(m_kinds);
    showRow->addStretch(1);
    body->addLayout(showRow);

    m_grid = new QListWidget;
    m_grid->setObjectName(QStringLiteral("resourceGrid"));
    m_grid->setViewMode(QListView::IconMode);
    m_grid->setMovement(QListView::Static);
    m_grid->setResizeMode(QListView::Adjust);
    m_grid->setWrapping(true);
    m_grid->setUniformItemSizes(true);
    m_grid->setIconSize(QSize(kThumbnail, kThumbnail));
    m_grid->setGridSize(kCell);
    m_grid->setWordWrap(true);
    m_grid->setSelectionMode(QAbstractItemView::SingleSelection);
    m_grid->setDragDropMode(QAbstractItemView::NoDragDrop);  // a drop on the grid goes on to the page, which adds the files
    body->addWidget(m_grid, 1);

    auto* buttons = new QHBoxLayout;
    m_add = button(tr("Add ..."), "resourceAddButton");
    m_remove = button(tr("Remove"), "resourceRemoveButton");
    m_rename = button(tr("Rename ..."), "resourceRenameButton");
    m_pack = button(tr("Import Pack ..."), "resourcePackButton");
    m_folder = button(tr("Open Folder"), "resourceFolderButton");
    auto* packMenu = new QMenu(m_pack);
    packMenu->setObjectName(QStringLiteral("resourcePackMenu"));
    packMenu->addAction(tr("From a Zip File ..."), this, [this] { importPack(askPack(false)); });
    packMenu->addAction(tr("From a Folder ..."), this, [this] { importPack(askPack(true)); });
    m_pack->setMenu(packMenu);
    for (QPushButton* b : {m_add, m_remove, m_rename})
        buttons->addWidget(b);
    buttons->addStretch(1);
    buttons->addWidget(m_pack);
    buttons->addWidget(m_folder);
    body->addLayout(buttons);

    m_message = new QLabel;
    m_message->setObjectName(QStringLiteral("resourceMessage"));
    m_message->setWordWrap(true);
    m_message->hide();
    body->addWidget(m_message);
    auto* help = new QLabel(tr("Drop image files here to add them. Adding, removing and renaming take effect at once. The "
                               "built-in pictures, drawn for Doodlebold, cannot be removed or renamed; a file of the same name "
                               "takes the place of one. Tools that use a picture follow it when it is renamed."));
    help->setWordWrap(true);
    help->setProperty("role", "hint");
    body->addWidget(help);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(card);

    connect(m_kinds, &QComboBox::currentIndexChanged, this, [this] {
        report({});
        rebuild();
    });
    connect(m_grid, &QListWidget::itemSelectionChanged, this, &ResourcesPage::syncButtons);
    connect(m_grid, &QListWidget::itemDoubleClicked, this, [this] { renameSelected(); });
    connect(m_add, &QPushButton::clicked, this, [this] { addFiles(askFiles()); });
    connect(m_remove, &QPushButton::clicked, this, &ResourcesPage::removeSelected);
    connect(m_rename, &QPushButton::clicked, this, &ResourcesPage::renameSelected);
    connect(m_folder, &QPushButton::clicked, this, [this] {
        if (!m_library)
            return;
        const QString path = m_library->kindFolder(kind());
        QDir().mkpath(path);
        openFolder(path);
    });
    if (m_library)
        connect(m_library, &BrushResourceLibrary::changed, this, &ResourcesPage::rebuild);
    rebuild();
}

ResourceKind ResourcesPage::kind() const {
    return static_cast<ResourceKind>(m_kinds->currentData().toInt());
}

void ResourcesPage::showKind(ResourceKind kind) {
    m_kinds->setCurrentIndex(m_kinds->findData(static_cast<int>(kind)));
}

QString ResourcesPage::selectedName() const {
    const QList<QListWidgetItem*> items = m_grid->selectedItems();
    return items.isEmpty() ? QString() : items.first()->data(kNameRole).toString();
}

void ResourcesPage::select(const QString& name) {
    for (int i = 0; i < m_grid->count(); ++i)
        if (m_grid->item(i)->data(kNameRole).toString() == name) {
            m_grid->setCurrentRow(i);
            return;
        }
    m_grid->clearSelection();
}

const BrushResource* ResourcesPage::selected() const {
    const QString name = selectedName();
    if (!m_library || name.isEmpty())
        return nullptr;
    for (const BrushResource* r : m_library->ofKind(kind()))
        if (r->name == name)
            return r;
    return nullptr;
}

QString ResourcesPage::message() const {
    return m_message->text();
}

void ResourcesPage::report(const QString& text) {
    m_message->setText(text);
    m_message->setVisible(!text.isEmpty());
}

void ResourcesPage::rebuild() {
    const QString keep = selectedName();
    m_grid->clear();
    if (m_library)
        for (const BrushResource* r : m_library->ofKind(kind())) {
            auto* item = new QListWidgetItem(QIcon(resourceThumbnail(*r, kThumbnail, devicePixelRatioF())), r->name);
            item->setData(kNameRole, r->name);
            item->setToolTip(r->builtIn ? tr("%1, drawn for Doodlebold").arg(r->name) : QDir::toNativeSeparators(r->file));
            m_grid->addItem(item);
        }
    select(keep);
    syncButtons();
}

void ResourcesPage::syncButtons() {
    const BrushResource* r = selected();
    const bool own = r && !r->builtIn;
    m_remove->setEnabled(own);
    m_rename->setEnabled(own);
    m_add->setEnabled(m_library);
    m_pack->setEnabled(m_library);
    m_folder->setEnabled(m_library);
}

void ResourcesPage::addFiles(const QStringList& paths) {
    if (!m_library || paths.isEmpty())
        return;
    QStringList added, refused;
    for (const QString& path : paths) {
        const auto r = m_library->importImage(kind(), path, BrushResourceLibrary::SameName::Replace);
        if (r)
            added << r.value();
        else
            refused << tr("%1: %2").arg(QFileInfo(path).fileName(), describe(r.error()));
    }
    if (!added.isEmpty())
        select(added.last());
    QStringList lines;
    if (!added.isEmpty())
        lines << tr("Added %1.").arg(added.join(QStringLiteral(", ")));
    report((lines + refused).join(QLatin1Char('\n')));
}

void ResourcesPage::removeSelected() {
    const BrushResource* r = selected();
    if (!r || r->builtIn || !askRemove(r->name, toolsUsing(r->name)))
        return;
    const QString name = r->name;
    if (!m_library->remove(kind(), name)) {
        report(tr("%1 could not be removed: its file could not be deleted.").arg(name));
        return;
    }
    const bool toolsCan = m_tools && kind() != ResourceKind::PaperTexture;
    const int reset = toolsCan ? m_tools->resetResource(isForm(kind()), name) : 0;
    QString text = tr("Removed %1.").arg(name);
    if (reset)
        text += QLatin1Char(' ') +
                (reset == 1 ? tr("1 tool went back to its default.") : tr("%1 tools went back to their default.").arg(reset));
    report(text);
}

// Paper textures belong to layers, not tools.
QStringList ResourcesPage::toolsUsing(const QString& name) const {
    if (!m_tools || kind() == ResourceKind::PaperTexture)
        return {};
    return m_tools->toolsUsingResource(isForm(kind()), name);
}

void ResourcesPage::renameSelected() {
    const BrushResource* r = selected();
    if (!r || r->builtIn)
        return;
    const QString name = r->name;
    const std::optional<QString> wanted = askNewName(name);
    if (!wanted)
        return;
    const auto renamed = m_library->rename(kind(), name, *wanted);
    if (!renamed) {
        report(describe(renamed.error()));
        return;
    }
    select(renamed.value());
    report(renamed.value() == name ? QString() : tr("Renamed %1 to %2.").arg(name, renamed.value()));
}

void ResourcesPage::importPack(const QString& path) {
    if (!m_library || path.isEmpty())
        return;
    const BrushPack pack = readBrushPack(path);
    if (!pack.error.isEmpty()) {
        report(tr("%1 could not be imported. %2").arg(QFileInfo(path).fileName(), pack.error));
        return;
    }
    PackImportDialog dialog(pack, this);
    if (!askPackChoices(dialog))
        return;
    const PackInstall installed = installBrushPack(*m_library, pack, dialog.uses());
    QString text = installed.written ? tr("Added %n picture(s) from %1.", nullptr, installed.added).arg(pack.name)
                                     : describe(ResourceError::WriteFailed);
    if (!installed.unreadable.isEmpty())
        text += QLatin1Char(' ') + tr("%n picture(s) could not be read.", nullptr, int(installed.unreadable.size()));
    report(text);
}

QStringList ResourcesPage::askFiles() {
    QStringList patterns;
    for (const QByteArray& format : QImageReader::supportedImageFormats())
        patterns << QStringLiteral("*.") + QString::fromLatin1(format);
    return QFileDialog::getOpenFileNames(this, tr("Add %1").arg(kindLabel(kind())), QString(),
                                         tr("Images (%1)").arg(patterns.join(QLatin1Char(' '))));
}

bool ResourcesPage::askRemove(const QString& name, const QStringList& tools) {
    QString text = tr("Remove %1? Its file is deleted.").arg(name);
    if (!tools.isEmpty())
        text = (tools.size() == 1 ? tr("This tool paints with %1:") : tr("These tools paint with %1:")).arg(name) +
               QStringLiteral("\n\n• ") + tools.join(QStringLiteral("\n• ")) + QStringLiteral("\n\n") +
               (isForm(kind()) ? tr("Removing it puts them back to their default form.")
                               : tr("Removing it puts them back to their default texture.")) +
               QStringLiteral("\n\n") + text;
    return QMessageBox::question(this, tr("Remove Picture"), text, QMessageBox::Yes | QMessageBox::No, QMessageBox::No) == QMessageBox::Yes;
}

std::optional<QString> ResourcesPage::askNewName(const QString& name) {
    bool accepted = false;
    const QString wanted = QInputDialog::getText(this, tr("Rename Picture"), tr("Name:"), QLineEdit::Normal, name, &accepted);
    return accepted ? std::optional<QString>(wanted) : std::nullopt;
}

void ResourcesPage::openFolder(const QString& path) {
    QDesktopServices::openUrl(QUrl::fromLocalFile(path));
}

QString ResourcesPage::askPack(bool folder) {
    return folder ? QFileDialog::getExistingDirectory(this, tr("Import a Brush Pack Folder"))
                  : QFileDialog::getOpenFileName(this, tr("Import a Brush Pack"), QString(), tr("Brush packs (*.zip)"));
}

bool ResourcesPage::askPackChoices(PackImportDialog& dialog) {
    return dialog.exec() == QDialog::Accepted;
}

void ResourcesPage::dragEnterEvent(QDragEnterEvent* event) {
    if (m_library && !droppedImages(event->mimeData()).isEmpty())
        event->acceptProposedAction();
}

void ResourcesPage::dropEvent(QDropEvent* event) {
    const QStringList files = droppedImages(event->mimeData());
    if (files.isEmpty())
        return;
    event->acceptProposedAction();
    addFiles(files);
}

}
