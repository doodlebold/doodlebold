#include "ui/panels/BrushResourceList.h"

#include "ui/Theme.h"

#include <QApplication>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QImageReader>
#include <QMimeData>
#include <QPainter>
#include <QPainterPath>
#include <QStandardItemModel>
#include <QStyledItemDelegate>

#include <algorithm>

namespace doodlebold {

namespace {

constexpr int kThumbnail = 28;  // px: a tip or texture picture in the list (component metric, the user's pictures)
constexpr int kNameRole = Qt::UserRole + 1;    // the entry's resource name ("none" names included)
constexpr int kImportRole = Qt::UserRole + 2;  // an import entry: its ResourceKind

QString noneName(BrushResourceList::Content content) {
    return content == BrushResourceList::Content::Forms ? BrushResourceLibrary::kSimpleCircle : BrushResourceLibrary::kNoTexture;
}

// A tip picture's row in the open list: its name, and under it, smaller and dimmer, its kind (kKindRole); a row without a kind
// ("Round", the separator, the Import items) is left to the list's own delegate, `rest`, as before. The name and the kind sit together
// in the middle of the row, each cut short with "..." if the list is too narrow.
// The kind's type is this share of the name's: a step smaller, as a caption, chosen by eye on the open list on the private Xvfb
// beside the panel's names; no other share was compared.
QFont kindFont(const QFont&) {
    return Theme::font(Theme::TextRole::Caption);
}

class KindDelegate : public QStyledItemDelegate {
public:
    KindDelegate(QAbstractItemDelegate* rest, QObject* parent) : QStyledItemDelegate(parent), m_rest(rest) {}

    void paint(QPainter* p, const QStyleOptionViewItem& option, const QModelIndex& index) const override {
        const QString kind = index.data(BrushResourceList::kKindRole).toString();
        if (kind.isEmpty()) {
            m_rest ? m_rest->paint(p, option, index) : QStyledItemDelegate::paint(p, option, index);
            return;
        }
        QStyleOptionViewItem item(option);
        initStyleOption(&item, index);
        const QStyle* style = item.widget ? item.widget->style() : QApplication::style();
        const QRect text = style->subElementRect(QStyle::SE_ItemViewItemText, &item, item.widget);
        const QString name = item.text;
        item.text.clear();  // the row's background and picture as the style draws them; the two lines are drawn here
        style->drawControl(QStyle::CE_ItemViewItem, &item, p, item.widget);
        const QFont small = kindFont(item.font);
        const QFontMetrics nameLine(item.font), kindLine(small);
        const int top = text.top() + (text.height() - nameLine.height() - kindLine.height()) / 2;
        const bool selected = item.state & QStyle::State_Selected;
        p->save();
        p->setFont(item.font);
        p->setPen(item.palette.color(selected ? QPalette::HighlightedText : QPalette::Text));
        p->drawText(QRect(text.left(), top, text.width(), nameLine.height()), Qt::AlignLeft | Qt::AlignVCenter,
                    nameLine.elidedText(name, Qt::ElideRight, text.width()));
        p->setFont(small);
        p->setPen(selected ? item.palette.color(QPalette::HighlightedText) : Theme::color(Theme::Token::TextMuted));
        p->drawText(QRect(text.left(), top + nameLine.height(), text.width(), kindLine.height()), Qt::AlignLeft | Qt::AlignVCenter,
                    kindLine.elidedText(kind, Qt::ElideRight, text.width()));
        p->restore();
    }

    QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override {
        const QString kind = index.data(BrushResourceList::kKindRole).toString();
        if (kind.isEmpty())
            return m_rest ? m_rest->sizeHint(option, index) : QStyledItemDelegate::sizeHint(option, index);
        QSize size = QStyledItemDelegate::sizeHint(option, index);
        QStyleOptionViewItem item(option);
        initStyleOption(&item, index);
        const QFontMetrics nameLine(item.font), kindLine(kindFont(item.font));
        size.setHeight(std::max(size.height(), nameLine.height() + kindLine.height()));
        return size;
    }

private:
    QPointer<QAbstractItemDelegate> m_rest;  // the list's own, which it keeps (QComboBox doesn't delete a replaced delegate)
};

QString droppedImage(const QMimeData* mime) {
    if (!mime || !mime->hasUrls())
        return {};
    for (const QUrl& url : mime->urls())
        if (url.isLocalFile() && !QImageReader::imageFormat(url.toLocalFile()).isEmpty())
            return url.toLocalFile();
    return {};
}

}

QPixmap resourceThumbnail(const BrushResource& resource, int side, qreal devicePixelRatio) {
    const int px = qRound(side * devicePixelRatio);
    QPixmap out(px, px);
    out.fill(Qt::transparent);
    QPainter p(&out);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    QPainterPath clip;
    clip.addRoundedRect(QRectF(0, 0, px, px), Theme::kRadiusXs * devicePixelRatio, Theme::kRadiusXs * devicePixelRatio);
    p.setClipPath(clip);
    p.fillRect(QRect(0, 0, px, px), Qt::white);
    const QImage fitted = resource.image.scaled(px, px, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    p.drawImage(QPoint((px - fitted.width()) / 2, (px - fitted.height()) / 2), fitted);
    p.end();
    out.setDevicePixelRatio(devicePixelRatio);
    return out;
}

BrushResourceList::BrushResourceList(BrushResourceLibrary* library, Content content, QWidget* parent)
    : QComboBox(parent), m_library(library), m_content(content), m_current(noneName(content)) {
    switch (content) {
    case Content::Forms: m_importKinds = {ResourceKind::Shape, ResourceKind::BlotMap, ResourceKind::Bristle}; break;
    case Content::Textures: m_importKinds = {ResourceKind::Texture}; break;
    case Content::PaperTextures: m_importKinds = {ResourceKind::PaperTexture}; break;
    }
    setIconSize(QSize(kThumbnail, kThumbnail));
    if (content == Content::Forms)
        setItemDelegate(new KindDelegate(itemDelegate(), this));
    setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    setMinimumContentsLength(8);
    setAcceptDrops(true);
    switch (content) {
    case Content::Forms: setToolTip(tr("The brush form. Drop an image here to import it.")); break;
    case Content::Textures: setToolTip(tr("The brush texture. Drop an image here to import it.")); break;
    case Content::PaperTextures: setToolTip(tr("The layer's paper texture. Drop an image here to import it.")); break;
    }
    rebuild();
    connect(this, &QComboBox::activated, this, &BrushResourceList::onActivated);
    if (m_library)
        connect(m_library, &BrushResourceLibrary::changed, this, &BrushResourceList::rebuild);
}

// The kinds in the words the Resources page's lists use (Blot Maps, Bristles, Brush Shapes), so a picture's row, its Import item and its
// list there agree.
QString BrushResourceList::kindLabel(ResourceKind kind) {
    switch (kind) {
    case ResourceKind::Shape: return tr("Brush shape");
    case ResourceKind::BlotMap: return tr("Blot map");
    case ResourceKind::Bristle: return tr("Bristle");
    case ResourceKind::Texture: return tr("Texture");
    case ResourceKind::PaperTexture: return tr("Paper texture");
    }
    return {};
}

QString BrushResourceList::importLabel(ResourceKind kind) {
    switch (kind) {
    case ResourceKind::Shape: return tr("Import Brush Shape Picture ...");
    case ResourceKind::BlotMap: return tr("Import Blot Map Picture ...");
    case ResourceKind::Bristle: return tr("Import Bristle Picture ...");
    case ResourceKind::Texture: return tr("Import Texture ...");
    case ResourceKind::PaperTexture: return tr("Import Paper Texture ...");
    }
    return {};
}

void BrushResourceList::rebuild() {
    m_rebuilding = true;
    clear();
    const qreal dpr = devicePixelRatioF();
    addItem(noneName(m_content));
    setItemData(0, noneName(m_content), kNameRole);
    if (m_content == Content::Textures) {
        addItem(BrushResourceLibrary::kLayerPaper);
        setItemData(count() - 1, BrushResourceLibrary::kLayerPaper, kNameRole);
        setItemData(count() - 1, tr("The paper texture of the layer being painted on (Layer Effect > Paper)"), Qt::ToolTipRole);
    }
    if (m_library) {
        auto list = m_content == Content::Forms      ? m_library->forms()
                    : m_content == Content::Textures ? m_library->textures()
                                                     : m_library->paperTextures();
        // Grouped by kind in the Import items' order (brush shapes, blot maps, bristles), each group by name A to Z as the library gives
        // them; an imported picture goes into its kind's group (a design choice).
        const auto group = [this](const BrushResource* r) { return std::ranges::find(m_importKinds, r->kind) - m_importKinds.begin(); };
        std::ranges::stable_sort(list, [&group](const BrushResource* a, const BrushResource* b) { return group(a) < group(b); });
        for (const BrushResource* r : list) {
            addItem(QIcon(resourceThumbnail(*r, kThumbnail, dpr)), r->name);
            setItemData(count() - 1, r->name, kNameRole);
            if (m_content == Content::Forms)
                setItemData(count() - 1, kindLabel(r->kind), kKindRole);
        }
    }
    if (findData(m_current, kNameRole) < 0) {  // a name the library lacks: kept, greyed
        addItem(m_current);
        setItemData(count() - 1, m_current, kNameRole);
        setItemData(count() - 1, tr("Not in the brush library"), Qt::ToolTipRole);
        if (auto* model = qobject_cast<QStandardItemModel*>(this->model()))
            model->item(count() - 1)->setForeground(Theme::color(Theme::Token::TextDisabled));
    }
    if (m_library) {
        insertSeparator(count());
        for (ResourceKind kind : m_importKinds) {
            addItem(importLabel(kind));
            setItemData(count() - 1, static_cast<int>(kind), kImportRole);
        }
    }
    setCurrentIndex(findData(m_current, kNameRole));
    m_rebuilding = false;
}

void BrushResourceList::setCurrentName(const QString& name) {
    const QString shown = name.isEmpty() ? noneName(m_content) : name;
    if (shown == m_current && findData(shown, kNameRole) == currentIndex())
        return;
    m_current = shown;
    rebuild();
}

QString BrushResourceList::currentName() const {
    return m_current;
}

void BrushResourceList::onActivated(int index) {
    if (m_rebuilding)
        return;
    if (const QVariant kind = itemData(index, kImportRole); kind.isValid()) {
        setCurrentIndex(findData(m_current, kNameRole));
        const auto k = static_cast<ResourceKind>(kind.toInt());
        if (const QString path = askFile(k); !path.isEmpty())
            importFile(k, path);
        return;
    }
    const QString name = itemData(index, kNameRole).toString();
    if (name.isEmpty() || name == m_current)
        return;
    m_current = name;
    emit chosen(name);
}

void BrushResourceList::importFile(ResourceKind kind, const QString& path) {
    if (!m_library)
        return;
    const auto imported = m_library->importImage(kind, path);
    if (!imported.ok()) {
        emit refused(tr("%1 could not be imported: %2").arg(QFileInfo(path).fileName(), describe(imported.error())));
        return;
    }
    setCurrentName(imported.value());
    emit chosen(imported.value());
}

QString BrushResourceList::askFile(ResourceKind kind) {
    QStringList patterns;
    for (const QByteArray& format : QImageReader::supportedImageFormats())
        patterns << QStringLiteral("*.") + QString::fromLatin1(format);
    return QFileDialog::getOpenFileName(this, importLabel(kind).remove(QStringLiteral(" ...")), QString(),
                                        tr("Images (%1)").arg(patterns.join(QLatin1Char(' '))));
}

void BrushResourceList::dragEnterEvent(QDragEnterEvent* event) {
    if (m_library && !droppedImage(event->mimeData()).isEmpty())
        event->acceptProposedAction();
    else
        QComboBox::dragEnterEvent(event);
}

void BrushResourceList::dropEvent(QDropEvent* event) {
    const QString path = droppedImage(event->mimeData());
    if (!m_library || path.isEmpty()) {
        QComboBox::dropEvent(event);
        return;
    }
    event->acceptProposedAction();
    importFile(m_importKinds.front(), path);  // a form dropped is a tip shape, the plainest kind
}

}
