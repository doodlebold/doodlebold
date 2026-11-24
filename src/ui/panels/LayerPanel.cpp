#include "ui/panels/LayerPanel.h"

#include "core/document/Document.h"
#include "ui/ButtonTips.h"
#include "ui/Components.h"
#include "ui/Theme.h"
#include "ui/panels/LayerTreeModel.h"
#include "ui/tools/CanvasTool.h"
#include "ui/ValueBar.h"

#include <QApplication>
#include <QCheckBox>
#include <QContextMenuEvent>
#include <QMenu>
#include <QComboBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QHelpEvent>
#include <QIcon>
#include <QItemSelectionModel>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QPixmap>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QSlider>
#include <QSpinBox>
#include <QStyleOption>
#include <QStyledItemDelegate>
#include <QTabletEvent>
#include <QTimer>
#include <QToolButton>
#include <QToolTip>
#include <QTreeView>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <memory>
#include <utility>

namespace doodlebold {

namespace {

using Theme::Token;

// A layer row's geometry (component metrics where not a token): its height, room for a 40 px thumbnail.
constexpr int kRowHeight = 44;  // component metric, described above
constexpr int kMargin = Theme::kSpace1;
constexpr int kEyeSize = Theme::kIconMd;
constexpr int kLockSize = Theme::kIconSm;
constexpr int kClipBarWidth = 3;  // px, fits in kMargin (component metric)
constexpr int kArrowWidth = Theme::kIconSm;
constexpr int kDepthIndent = Theme::kSpace3;
constexpr int kTextGap = Theme::kSpace2;
constexpr int kChecker = 5;  // px: a thumbnail checkerboard's square (component metric)
constexpr int kTargetSize = Theme::kIconSm;
constexpr int kMaskSwitch = Theme::kIconSm;
constexpr int kMaskGap = Theme::kSpace1;
constexpr int kShareSize = Theme::kIconMd;
constexpr int kFolderIcon = 26;  // px: a folder in the thumbnail's place (component metric)

struct RowRects {
    QRect eye;
    QRect target;
    QRect arrow;
    QRect thumb;
    QRect maskSwitch;
    QRect maskLink;
    QRect mask;
    QRect text;
    QRect lock;
    QRect share;  // the shared / per-frame mark of an animation's top-level layers
};

RowRects rowRects(const QRect& row, int depth, bool hasMask, bool shareable = false) {
    RowRects r;
    const int cy = row.top() + row.height() / 2;
    int x = row.left() + kMargin;
    r.eye = QRect(x, cy - kEyeSize, kEyeSize, kEyeSize);
    r.target = QRect(x + (kEyeSize - kTargetSize) / 2, cy + 2, kTargetSize, kTargetSize);
    x = r.eye.right() + 1 + kTextGap + depth * kDepthIndent;
    r.arrow = QRect(x, row.top(), kArrowWidth, row.height());
    x += kArrowWidth;
    r.thumb = QRect(x, cy - LayerTreeModel::kThumbSize / 2, LayerTreeModel::kThumbSize, LayerTreeModel::kThumbSize);
    x = r.thumb.right() + 1;
    if (hasMask) {
        x += kMaskGap;
        r.maskSwitch = QRect(x, cy - kMaskSwitch - 1, kMaskSwitch, kMaskSwitch);
        r.maskLink = QRect(x, cy + 1, kMaskSwitch, kMaskSwitch);
        x += kMaskSwitch + kMaskGap;
        r.mask = QRect(x, r.thumb.top(), LayerTreeModel::kThumbSize, LayerTreeModel::kThumbSize);
        x = r.mask.right() + 1;
    }
    x += kTextGap;
    const int right = row.right() - kMargin;
    r.lock = QRect(right - kLockSize + 1, cy - kLockSize / 2, kLockSize, kLockSize);
    int textRight = r.lock.left();
    if (shareable) {
        r.share = QRect(r.lock.left() - kTextGap / 2 - kShareSize, cy - kShareSize / 2, kShareSize, kShareSize);
        textRight = r.share.left();
    }
    r.text = QRect(x, row.top(), std::max(0, textRight - kTextGap - x), row.height());
    return r;
}

int depthOf(const QModelIndex& index) {
    int depth = 0;
    for (QModelIndex p = index.parent(); p.isValid(); p = p.parent())
        ++depth;
    return depth;
}

RowRects rowRectsFor(const QRect& row, const QModelIndex& index) {
    return rowRects(row, depthOf(index), index.data(LayerTreeModel::HasMaskRole).toBool(),
                    index.data(LayerTreeModel::ShareableRole).toBool());
}

void drawChecker(QPainter* p, const QRect& r) {
    components::paintChecker(*p, r, kChecker, Theme::kCheckerLight, Theme::kCheckerDark);
}

}

class LayerItemDelegate : public QStyledItemDelegate {
public:
    explicit LayerItemDelegate(QObject* parent = nullptr)
        : QStyledItemDelegate(parent),
          m_visible(QStringLiteral(":/icons/visible.svg")),
          m_hidden(QStringLiteral(":/icons/hidden.svg")),
          m_lock(QStringLiteral(":/icons/lock.svg")),
          m_folder(QStringLiteral(":/icons/folder.svg")),
          m_folderOpen(QStringLiteral(":/icons/folder-open.svg")),
          m_targetLayer(QStringLiteral(":/icons/tool-pencil.svg")),
          m_targetMask(QStringLiteral(":/icons/target-mask.svg")),
          m_maskOn(QStringLiteral(":/icons/mask-on.svg")),
          m_maskLink(QStringLiteral(":/icons/mask-link.svg")),
          m_linkOff(QStringLiteral(":/icons/link-off.svg")) {}

    QSize sizeHint(const QStyleOptionViewItem&, const QModelIndex&) const override { return {160, kRowHeight}; }

    void paint(QPainter* p, const QStyleOptionViewItem& option, const QModelIndex& index) const override {
        const bool folder = index.data(LayerTreeModel::IsFolderRole).toBool();
        const bool highlighted = index.data(LayerTreeModel::IsCurrentRole).toBool() || (option.state & QStyle::State_Selected);
        const bool visible = index.data(LayerTreeModel::VisibleRole).toBool();
        const RowRects r = rowRectsFor(option.rect, index);

        p->save();
        if (highlighted || (option.state & QStyle::State_MouseOver)) {
            const double radius = Theme::metric(Theme::Metric::Radius);
            p->setRenderHint(QPainter::Antialiasing);
            p->setPen(Qt::NoPen);
            p->setBrush(Theme::color(highlighted ? Token::Selected : Token::Hover));
            p->drawRoundedRect(QRectF(option.rect).adjusted(1, 1, -1, -1), radius, radius);
            p->setRenderHint(QPainter::Antialiasing, false);
        }

        if (index.data(LayerTreeModel::ClippingRole).toBool())
            p->fillRect(option.rect.left() + 1, option.rect.top() + 1, kClipBarWidth, option.rect.height() - 2,
                        QColor::fromRgba(Theme::kClipBar));

        (visible ? m_visible : m_hidden).paint(p, r.eye);
        const bool maskTarget = index.data(LayerTreeModel::MaskTargetRole).toBool();
        (maskTarget ? m_targetMask : m_targetLayer).paint(p, r.target);

        if (!visible)
            p->setOpacity(0.5);
        if (folder)
            drawArrow(p, r.arrow, option.state & QStyle::State_Open, highlighted);

        if (folder) {
            const QRect icon =
                QRect(QPoint(), QSize(kFolderIcon, kFolderIcon)).translated(r.thumb.center() - QPoint(kFolderIcon / 2, kFolderIcon / 2));
            (option.state & QStyle::State_Open ? m_folderOpen : m_folder).paint(p, icon);
        } else {
            drawThumbnail(p, r.thumb, qvariant_cast<QImage>(index.data(Qt::DecorationRole)));
        }
        if (index.data(LayerTreeModel::SharedRole).toBool())
            drawSharedBadge(p, r.thumb);
        if (index.data(LayerTreeModel::HasMaskRole).toBool())
            drawMask(p, r, index, maskTarget);

        drawTexts(p, option, index, r.text, highlighted);
        if (index.data(LayerTreeModel::LockedRole).toBool())
            m_lock.paint(p, r.lock);
        if (!r.share.isEmpty())
            drawShareMark(p, r.share, index.data(LayerTreeModel::SharedRole).toBool());
        p->restore();
    }

    bool helpEvent(QHelpEvent* event, QAbstractItemView* view, const QStyleOptionViewItem& option, const QModelIndex& index) override {
        const RowRects r = rowRectsFor(option.rect, index);
        if (event->type() != QEvent::ToolTip || r.share.isEmpty() || !r.share.contains(event->pos()))
            return QStyledItemDelegate::helpEvent(event, view, option, index);
        const bool shared = index.data(LayerTreeModel::SharedRole).toBool();
        QToolTip::showText(event->globalPos(),
                           shared ? LayerPanel::tr("Shared with every frame. Click to make it this frame's own again.")
                                  : LayerPanel::tr("This frame's own layer. Click to share it with every frame."),
                           view, r.share);
        return true;
    }

    void setEditorData(QWidget* editor, const QModelIndex& index) const override {
        QStyledItemDelegate::setEditorData(editor, index);
        if (auto* edit = qobject_cast<QLineEdit*>(editor))
            edit->selectAll();
    }

    void updateEditorGeometry(QWidget* editor, const QStyleOptionViewItem& option, const QModelIndex& index) const override {
        const QRect text = rowRectsFor(option.rect, index).text;
        const int h = editor->sizeHint().height();
        editor->setGeometry(text.left(), text.top() + (text.height() - h) / 2, text.width(), h);
    }

private:
    // The share button: a chain on an accent-coloured button for a layer shared by every frame; a broken chain for a layer of
    // this frame only.
    void drawShareMark(QPainter* p, const QRect& box, bool shared) const {
        p->save();
        p->setOpacity(1.0);
        p->setRenderHint(QPainter::Antialiasing);
        if (shared) {
            p->setPen(Qt::NoPen);
            p->setBrush(Theme::color(Token::Accent));
            p->drawRoundedRect(QRectF(box).adjusted(-1, -1, 1, 1), Theme::kRadiusXs, Theme::kRadiusXs);
        }
        (shared ? m_maskLink : m_linkOff).paint(p, box.adjusted(2, 2, -2, -2));
        p->restore();
    }

    // A shared layer's thumbnail carries a small chain in its corner.
    void drawSharedBadge(QPainter* p, const QRect& thumb) const {
        p->save();
        p->setOpacity(1.0);
        p->setRenderHint(QPainter::Antialiasing);
        const QRect badge(thumb.right() - 11, thumb.bottom() - 11, 14, 14);
        p->setPen(QPen(Theme::color(Token::Base), Theme::kGlyphStroke));
        p->setBrush(Theme::color(Token::Accent));
        p->drawEllipse(QRectF(badge));
        m_maskLink.paint(p, badge.adjusted(2, 2, -2, -2));
        p->restore();
    }

    static void drawArrow(QPainter* p, const QRect& area, bool open, bool onSelected) {
        const QPoint c = area.center();
        p->setRenderHint(QPainter::Antialiasing);
        p->setPen(Qt::NoPen);
        p->setBrush(Theme::color(onSelected ? Token::SelectedText : Token::TextMuted));
        if (open)
            p->drawPolygon(QPolygon({c + QPoint(-4, -2), c + QPoint(4, -2), c + QPoint(0, 3)}));
        else
            p->drawPolygon(QPolygon({c + QPoint(-2, -4), c + QPoint(-2, 4), c + QPoint(3, 0)}));
        p->setRenderHint(QPainter::Antialiasing, false);
    }

    static void drawThumbnail(QPainter* p, const QRect& box, const QImage& image) {
        if (image.isNull())
            return;
        QRect img(QPoint(), image.size());
        img.moveCenter(box.center());
        drawChecker(p, img);
        p->drawImage(img.topLeft(), image);
        p->setPen(Theme::color(Token::Border));
        p->setBrush(Qt::NoBrush);
        p->drawRect(img.adjusted(-1, -1, 0, 0));
    }

    void drawMask(QPainter* p, const RowRects& r, const QModelIndex& index, bool target) const {
        const auto box = [p](const QRect& rect) {
            p->setPen(Theme::color(Token::Border));
            p->setBrush(Qt::NoBrush);
            p->drawRect(rect.adjusted(0, 0, -1, -1));
        };
        if (index.data(LayerTreeModel::MaskEnabledRole).toBool())
            m_maskOn.paint(p, r.maskSwitch);
        else
            box(r.maskSwitch);
        if (index.data(LayerTreeModel::MaskLinkedRole).toBool())
            m_maskLink.paint(p, r.maskLink);
        else
            box(r.maskLink);
        const QImage image = qvariant_cast<QImage>(index.data(LayerTreeModel::MaskThumbRole));
        if (image.isNull())
            return;
        QRect img(QPoint(), image.size());
        img.moveCenter(r.mask.center());
        p->drawImage(img.topLeft(), image);
        p->setBrush(Qt::NoBrush);
        if (target) {
            p->setPen(QPen(QColor::fromRgba(Theme::kMaskFrame), Theme::kBorderStrong));
            p->drawRect(img.adjusted(-1, -1, 0, 0));
        } else {
            p->setPen(Theme::color(Token::Border));
            p->drawRect(img.adjusted(-1, -1, 0, 0));
        }
    }

    static void drawTexts(QPainter* p, const QStyleOptionViewItem& option, const QModelIndex& index, const QRect& area, bool onSelected) {
        const QFont modeFont = Theme::font(Theme::TextRole::Caption);
        const QFontMetrics nameMetrics(option.font);
        const QFontMetrics modeMetrics(modeFont);
        const int top = area.top() + (area.height() - nameMetrics.height() - modeMetrics.height()) / 2;
        const QRect nameRect(area.left(), top, area.width(), nameMetrics.height());
        const QRect modeRect(area.left(), nameRect.bottom() + 1, area.width(), modeMetrics.height());

        p->setPen(Theme::color(onSelected ? Token::SelectedText : Token::Text));
        QRect name = nameRect;
        if (index.data(LayerTreeModel::IsTextRole).toBool()) {
            QFont t = option.font;
            t.setWeight(QFont::DemiBold);
            t.setStyleHint(QFont::Serif);
            t.setFamily(QStringLiteral("serif"));
            p->setFont(t);
            const int w = QFontMetrics(t).horizontalAdvance(QLatin1Char('T')) + 3;
            p->drawText(QRect(name.left(), name.top(), w, name.height()), Qt::AlignLeft | Qt::AlignVCenter, QStringLiteral("T"));
            name.setLeft(name.left() + w);
        }
        p->setFont(option.font);
        p->drawText(name, Qt::AlignLeft | Qt::AlignVCenter,
                    nameMetrics.elidedText(index.data(Qt::DisplayRole).toString(), Qt::ElideRight, name.width()));
        const QColor muted = Theme::color(onSelected ? Token::SelectedText : Token::TextMuted);
        p->setFont(modeFont);
        p->setPen(muted);
        const QString mode = modeMetrics.elidedText(index.data(LayerTreeModel::ModeTextRole).toString(), Qt::ElideRight, modeRect.width());
        p->drawText(modeRect, Qt::AlignLeft | Qt::AlignVCenter, mode);
        if (index.data(LayerTreeModel::PaperRole).toBool()) {
            const int x = modeRect.left() + modeMetrics.horizontalAdvance(mode) + modeMetrics.horizontalAdvance(QLatin1Char(' '));
            p->setPen(Theme::color(onSelected ? Token::SelectedText : Token::Success));
            p->drawText(QRect(x, modeRect.top(), std::max(0, modeRect.right() - x + 1), modeRect.height()),
                        Qt::AlignLeft | Qt::AlignVCenter, tr("Pap"));
        }
    }

    QIcon m_visible, m_hidden, m_lock, m_folder, m_folderOpen, m_targetLayer, m_targetMask, m_maskOn, m_maskLink, m_linkOff;
};

// Where a row dragged in the layer list lands, as Qt's item views place a drop: within this many px of a row's top or bottom edge, its
// height divided by kDropEdgeShare and held between kDropEdgeLeast and kDropEdgeMost, above or below it; between, onto it when it is a
// folder. The same numbers as Qt's, so the layers land where the mouse's drag put them before it moved into the list.
constexpr double kDropEdgeShare = 5.5;
constexpr int kDropEdgeLeast = 2;  // px
constexpr int kDropEdgeMost = 12;  // px
// While rows are dragged, a picture of them follows the pointer, and the list scrolls when the pointer is within its autoScrollMargin of
// the top or bottom edge, as Qt's drag did (feedback: "Add both back"). The picture is drawn at this opacity so the rows and the
// drop line under it show through while its own name still reads: chosen by eye on the test's screenshot; no other value was compared.
constexpr qreal kDragPictureOpacity = 0.7;
// How often the list scrolls a step while the pointer is held at its edge: Qt's own item views' intervals, a row a step when the list
// scrolls by rows and a pixel step when it scrolls by pixels, so it scrolls as fast as the mouse's drag did.
constexpr int kAutoScrollByRowMs = 150;
constexpr int kAutoScrollByPixelMs = 50;

// The layer list moves its rows itself, with the mouse and with the pen alike, rather than through Qt's drag and drop (QDrag): that
// goes through the window system, and on Wayland a pen, which speaks the tablet protocol rather than the pointer's, can't start one
// (feedback: "When using the pen, i cant drag the layers up and down"). The drop is the model's own, as before: one undo step,
// several rows together, into and out of folders, refused where a folder would go inside itself.
class LayerTreeView : public QTreeView {
public:
    explicit LayerTreeView(QWidget* parent) : QTreeView(parent) {
        m_autoScroll.setSingleShot(false);
        connect(&m_autoScroll, &QTimer::timeout, this, [this] { scrollTowardsPointer(); });
    }

    std::function<void(LayerId, Qt::KeyboardModifiers, bool mask)> selectFromLayer;
    std::function<void(LayerId, bool mask)> chooseTarget;

    bool moving() const { return m_move != nullptr; }

protected:
    // The mouse: Qt's item view decides when a press has become a drag, past the drag distance on a selected row, as before.
    void startDrag(Qt::DropActions) override { beginMove(m_mousePressed); }

    void mouseMoveEvent(QMouseEvent* event) override {
        if (m_move) {
            trackMove(event->position().toPoint());
            event->accept();
            return;
        }
        if (m_pressTaken) {  // Qt's view never saw the press, and would start a drag of the row pressed before it from its moves
            event->accept();
            return;
        }
        QTreeView::mouseMoveEvent(event);
    }

    void mouseReleaseEvent(QMouseEvent* event) override {
        if (m_move && event->button() == Qt::LeftButton) {
            finishMove(event->position().toPoint());
            event->accept();
            return;
        }
        if (m_penMoved || m_pressTaken) {  // the release Qt makes from the pen's after its move landed, or of a press a switch took
            m_penMoved = m_pressTaken = false;
            setState(NoState);
            event->accept();
            return;
        }
        QTreeView::mouseReleaseEvent(event);
    }

    void keyPressEvent(QKeyEvent* event) override {
        if (m_move && event->key() == Qt::Key_Escape) {  // as Qt's drag did: Escape puts the rows back
            endMove();
            event->accept();
            return;
        }
        QTreeView::keyPressEvent(event);
    }

    // The pen: its press and release go on to make the mouse's (selecting the row, the eye's switch), but once it has moved the drag
    // distance on a row, its moves are the list's own, so no mouse move is made from them and nothing starts Qt's drag.
    bool viewportEvent(QEvent* event) override {
        switch (event->type()) {
        case QEvent::TabletPress:
        case QEvent::TabletMove:
        case QEvent::TabletRelease:
            if (penMove(static_cast<QTabletEvent*>(event)))
                return true;
            break;
        default: break;
        }
        return QTreeView::viewportEvent(event);
    }

    void paintEvent(QPaintEvent* event) override {
        QTreeView::paintEvent(event);
        if (!m_move)
            return;
        QPainter painter(viewport());
        if (m_move->drop.allowed) {
            QStyleOption option;
            option.initFrom(this);
            option.rect = m_move->drop.indicator;
            style()->drawPrimitive(QStyle::PE_IndicatorItemViewItemDrop, &option, &painter, this);
        }
        if (m_move->tracked) {
            painter.setOpacity(kDragPictureOpacity);
            painter.drawPixmap(m_move->pointer - m_move->grip, m_move->picture);
        }
    }

    void mousePressEvent(QMouseEvent* event) override {
        m_mousePressed = event->position().toPoint();
        m_penMoved = false;
        m_pressTaken = toggleAt(event);
        if (m_pressTaken)
            return;
        const QModelIndex index = indexAt(event->position().toPoint());
        if (event->button() == Qt::LeftButton && event->modifiers() == Qt::ShiftModifier && index.isValid()) {
            selectRowsTo(index);
            event->accept();
            return;
        }
        if (!(event->modifiers() & Qt::ShiftModifier))
            m_rangeStart = index;
        QTreeView::mousePressEvent(event);
    }

    void mouseDoubleClickEvent(QMouseEvent* event) override {
        m_pressTaken = toggleAt(event);
        if (m_pressTaken)
            return;
        const QModelIndex index = indexAt(event->position().toPoint());
        if (event->button() == Qt::LeftButton && index.isValid() &&
            rowRectsFor(visualRect(index), index).text.contains(event->position().toPoint())) {
            edit(index);
            return;
        }
        QTreeView::mouseDoubleClickEvent(event);
    }

private:
    struct Drop {
        int row = -1;  // among the parent's rows; -1 onto the parent itself
        QModelIndex parent;
        QRect indicator;  // in the viewport: a line between rows, or the folder's row
        bool allowed = false;
    };
    struct Move {
        std::unique_ptr<QMimeData> layers;
        Drop drop;
        QPixmap picture;  // the dragged rows as the list shows them, each at its place among them
        QPoint grip;  // where in the picture the drag took hold of it
        QPoint pointer;  // in the viewport
        bool tracked = false;  // the pointer has moved since the drag began
    };

    Drop dropAt(const QPoint& pos) const {
        Drop drop;
        const QModelIndex index = indexAt(pos);
        if (!index.isValid()) {  // below the rows: the bottom of the top level
            drop.indicator = viewport()->rect();
        } else {
            const QRect rect = visualRect(index);
            const int edge = std::clamp(static_cast<int>(std::lround(rect.height() / kDropEdgeShare)), kDropEdgeLeast, kDropEdgeMost);
            const bool folder = model()->flags(index) & Qt::ItemIsDropEnabled;
            const bool above = pos.y() - rect.top() < edge || (!folder && pos.y() < rect.center().y());
            const bool below = !above && (rect.bottom() - pos.y() < edge || !folder);
            if (above || below) {
                drop.row = index.row() + (below ? 1 : 0);
                drop.parent = index.parent();
                drop.indicator = QRect(rect.left(), below ? rect.bottom() + 1 : rect.top(), rect.width(), 0);
            } else {
                drop.parent = index;
                drop.indicator = rect;
            }
        }
        drop.allowed = model()->canDropMimeData(m_move->layers.get(), Qt::MoveAction, drop.row, 0, drop.parent);
        return drop;
    }

    void beginMove(const QPoint& pressed) {
        QModelIndexList rows;
        for (const QModelIndex& index : selectedIndexes())
            if (model()->flags(index) & Qt::ItemIsDragEnabled)
                rows << index;
        // In the list's order, top first, so the rows keep their order where they land, whichever was chosen first.
        std::ranges::sort(rows, [this](const QModelIndex& a, const QModelIndex& b) { return visualRect(a).top() < visualRect(b).top(); });
        std::unique_ptr<QMimeData> layers(rows.isEmpty() ? nullptr : model()->mimeData(rows));
        if (!layers)
            return;
        m_move = std::make_unique<Move>();
        m_move->layers = std::move(layers);
        pictureOf(rows, pressed);
    }

    // The rows shown in the viewport, grabbed as drawn, in one picture as tall as from the first to the last of them.
    void pictureOf(const QModelIndexList& rows, const QPoint& pressed) {
        QRect around;
        for (const QModelIndex& row : rows)
            around |= visualRect(row) & viewport()->rect();
        if (around.isEmpty())
            return;
        const qreal ratio = viewport()->devicePixelRatioF();
        QPixmap picture(around.size() * ratio);
        picture.setDevicePixelRatio(ratio);
        picture.fill(Qt::transparent);
        QPainter painter(&picture);
        for (const QModelIndex& row : rows)
            if (const QRect r = visualRect(row) & viewport()->rect(); !r.isEmpty())
                painter.drawPixmap(r.topLeft() - around.topLeft(), viewport()->grab(r));
        painter.end();
        m_move->picture = picture;
        m_move->grip = pressed - around.topLeft();
    }

    void trackMove(const QPoint& pos) {
        m_move->pointer = pos;
        m_move->tracked = true;
        m_move->drop = dropAt(pos);
        const bool atEdge = pos.y() < autoScrollMargin() || pos.y() >= viewport()->height() - autoScrollMargin();
        if (atEdge && !m_autoScroll.isActive())
            m_autoScroll.start(verticalScrollMode() == ScrollPerItem ? kAutoScrollByRowMs : kAutoScrollByPixelMs);
        else if (!atEdge)
            m_autoScroll.stop();
        viewport()->update();
    }

    // A step towards the edge the pointer is held at, and the drop under the pointer again, as the rows moved beneath it.
    void scrollTowardsPointer() {
        if (!m_move) {
            m_autoScroll.stop();
            return;
        }
        QScrollBar* bar = verticalScrollBar();
        const bool up = m_move->pointer.y() < autoScrollMargin();
        bar->setValue(bar->value() + (up ? -bar->singleStep() : bar->singleStep()));
        trackMove(m_move->pointer);
    }

    void finishMove(const QPoint& pos) {
        const Drop drop = dropAt(pos);
        const std::unique_ptr<Move> move = std::move(m_move);
        endMove();
        if (drop.allowed)
            model()->dropMimeData(move->layers.get(), Qt::MoveAction, drop.row, 0, drop.parent);
    }

    void endMove() {
        m_autoScroll.stop();
        m_move.reset();
        setState(NoState);
        viewport()->update();
    }

    // True when the pen's event is the list's move and taken here.
    bool penMove(QTabletEvent* event) {
        const QPoint pos = event->position().toPoint();
        if (event->type() == QEvent::TabletPress) {
            m_penDown = event->button() == Qt::LeftButton;
            m_penPressed = pos;
            m_penRow = indexAt(pos);
            return false;
        }
        if (event->type() == QEvent::TabletRelease) {
            m_penDown = false;
            if (!m_move)
                return false;
            finishMove(pos);
            m_penMoved = true;
            event->ignore();  // Qt still makes the mouse's release from it, which ends the press it began
            return false;
        }
        if (!m_move && m_penDown && !m_pressTaken && m_penRow.isValid() &&
            (pos - m_penPressed).manhattanLength() > QApplication::startDragDistance() &&
            (model()->flags(m_penRow) & Qt::ItemIsDragEnabled)) {
            if (!selectionModel()->isSelected(m_penRow))  // the mouse's press selects it; a pen's press without one, here
                selectionModel()->setCurrentIndex(m_penRow, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
            beginMove(m_penPressed);
        }
        if (!m_move)
            return false;
        trackMove(pos);
        event->accept();
        return true;
    }

    std::unique_ptr<Move> m_move;  // a drag of rows under way
    QTimer m_autoScroll;  // runs while the pointer is held at the list's top or bottom edge during a drag
    QPoint m_mousePressed;
    bool m_penDown = false;
    QPoint m_penPressed;
    QPersistentModelIndex m_penRow;
    bool m_penMoved = false;  // the pen's move landed; the mouse release made from its release is the move's, not a click
    bool m_pressTaken = false;  // a switch in the row (the eye, the mask's) took the press: no drag comes of it

    // Every row shown from the last row clicked without Shift to `index`. Qt's own Shift+click selected only the clicked row
    // when the range went into a folder here.
    void selectRowsTo(const QModelIndex& index) {
        QModelIndex first =
            m_rangeStart.isValid() && selectionModel()->isSelected(m_rangeStart) ? QModelIndex(m_rangeStart) : currentIndex();
        QModelIndex last = index;
        if (!first.isValid())
            first = index;
        if (visualRect(first).top() > visualRect(last).top())
            std::swap(first, last);
        QItemSelection rows;
        for (QModelIndex row = first; row.isValid(); row = indexBelow(row)) {
            rows.select(row, row);
            if (row == last)
                break;
        }
        selectionModel()->setCurrentIndex(index, QItemSelectionModel::NoUpdate);
        selectionModel()->select(rows, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
    }

    QPersistentModelIndex m_rangeStart;

    bool toggleAt(QMouseEvent* event) {
        const QPoint pos = event->position().toPoint();
        const QModelIndex index = indexAt(pos);
        if (event->button() != Qt::LeftButton || !index.isValid())
            return false;
        const RowRects r = rowRectsFor(visualRect(index), index);
        const LayerId id(index.data(LayerTreeModel::LayerIdRole).toUInt());
        const bool onThumb = r.thumb.contains(pos), onMask = r.mask.contains(pos);
        if ((event->modifiers() & Qt::ControlModifier) && (onThumb || onMask) && selectFromLayer) {
            selectFromLayer(id, event->modifiers(), onMask);
        } else if (r.maskSwitch.contains(pos)) {
            model()->setData(index, !index.data(LayerTreeModel::MaskEnabledRole).toBool(), LayerTreeModel::MaskEnabledRole);
        } else if (r.maskLink.contains(pos)) {
            model()->setData(index, !index.data(LayerTreeModel::MaskLinkedRole).toBool(), LayerTreeModel::MaskLinkedRole);
        } else if ((onMask || (onThumb && index.data(LayerTreeModel::HasMaskRole).toBool())) && chooseTarget &&
                   event->type() == QEvent::MouseButtonPress) {
            chooseTarget(id, onMask);
            return false;  // the stock press still selects the row (and may start a drag)
        } else if (r.share.contains(pos)) {
            model()->setData(index, !index.data(LayerTreeModel::SharedRole).toBool(), LayerTreeModel::SharedRole);
        } else if (r.eye.contains(pos)) {
            model()->setData(index, !index.data(LayerTreeModel::VisibleRole).toBool(), LayerTreeModel::VisibleRole);
        } else if (index.data(LayerTreeModel::IsFolderRole).toBool() && r.arrow.contains(pos)) {
            setExpanded(index, !isExpanded(index));
        } else {
            return false;
        }
        event->accept();
        return true;
    }
};

LayerPanel::LayerPanel(QWidget* parent) : QWidget(parent) {
    m_model = new LayerTreeModel(this);

    m_controls = new QWidget(this);

    // The Layer Effect, a placeholder until it is made again as layer styles (shadows and glows, bevels and borders,
    // overlays): greyed, it does nothing. Feedback (2026-11-22): \"Remove the Layer effect for now with a greyed out
    // mock". A layer that has a paper texture from a file keeps it, saved and shown, though it can no longer be changed here.
    m_effectExpander = new QToolButton(m_controls);
    m_effectExpander->setObjectName(QStringLiteral("layerEffectExpander"));
    m_effectExpander->setProperty("role", "fold");
    m_effectExpander->setText(tr("Layer Effect"));
    m_effectExpander->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_effectExpander->setArrowType(Qt::RightArrow);
    m_effectExpander->setAutoRaise(true);
    m_effectExpander->setEnabled(false);
    m_effectExpander->setToolTip(QStringLiteral("TODO: Implement"));

    m_modeCombo = new QComboBox(m_controls);
    m_modeCombo->setObjectName(QStringLiteral("blendModeCombo"));
    m_modeCombo->setMaxVisibleItems(40);

    m_opacityBar = new ValueBar(m_controls);
    m_opacityBar->setObjectName(QStringLiteral("opacityBar"));
    m_opacityBar->setRange(0, 100);
    m_opacityBar->setSuffix(QStringLiteral("%"));

    m_preserveCheck = new QCheckBox(tr("Preserve Opacity"), m_controls);
    m_preserveCheck->setObjectName(QStringLiteral("preserveOpacityCheck"));
    m_clippingCheck = new QCheckBox(tr("Clipping Group"), m_controls);
    m_clippingCheck->setObjectName(QStringLiteral("clippingCheck"));
    m_lockCheck = new QCheckBox(tr("Lock"), m_controls);
    m_lockCheck->setObjectName(QStringLiteral("lockCheck"));

    auto* opacityRow = new QHBoxLayout;
    opacityRow->setSpacing(Theme::kPanelColumnGap);
    opacityRow->addWidget(new QLabel(tr("Opacity"), m_controls));
    opacityRow->addWidget(m_opacityBar, 1);
    auto* switches = new QGridLayout;
    switches->addWidget(m_preserveCheck, 0, 0);
    switches->addWidget(m_lockCheck, 0, 1);
    switches->addWidget(m_clippingCheck, 1, 0);
    auto* controlsLayout = new QVBoxLayout(m_controls);
    controlsLayout->setContentsMargins(0, 0, 0, 0);
    controlsLayout->setSpacing(Theme::compact::kRowGap);
    controlsLayout->addWidget(m_effectExpander);
    controlsLayout->addWidget(m_modeCombo);
    controlsLayout->addLayout(opacityRow);
    controlsLayout->addLayout(switches);

    m_buttons = new QWidget(this);
    // Two rows: nine buttons do not fit the column's width. Making things, and masks; merging, and emptying or removing.
    auto* buttonRows = new QGridLayout(m_buttons);
    buttonRows->setContentsMargins(0, 0, 0, 0);
    buttonRows->setSpacing(Theme::compact::kControlGap);
    buttonRows->setColumnStretch(3, 1);
    const auto addButton = [this, buttonRows](int row, int column, const char* icon, const QString& tip, const char* name,
                                              const char* command) {
        auto* b = new QToolButton(m_buttons);
        b->setObjectName(QString::fromLatin1(name));
        b->setIcon(QIcon(QStringLiteral(":/icons/%1.svg").arg(QString::fromLatin1(icon))));
        components::toolButton(b);
        if (command)
            button_tips::setCommandTip(b, tip, QString::fromLatin1(command));
        else
            button_tips::setButtonTip(b, tip);
        b->setAutoRaise(true);
        buttonRows->addWidget(b, row, column);
        return b;
    };
    auto* newLayer = addButton(0, 0, "layer-new", tr("New Layer"), "newLayerButton", "layer.newNormal");
    auto* newFolder = addButton(0, 1, "folder-new", tr("New Layer Folder"), "newFolderButton", "layer.newFolder");
    auto* duplicate = addButton(0, 2, "layer-duplicate", tr("Duplicate Layer"), "duplicateButton", "layer.copy");
    m_newMaskButton = addButton(0, 4, "mask-new", tr("New Layer Mask"), "newMaskButton", nullptr);
    m_applyMaskButton = addButton(0, 5, "mask-apply", tr("Apply Layer Mask"), "applyMaskButton", nullptr);
    auto* mergeDown = addButton(1, 0, "layer-merge-down", tr("Merge Down"), "mergeDownButton", "layer.merge");
    m_mergeButton = addButton(1, 1, "layer-merge", tr("Merge Layers"), "mergeLayersButton", "layer.merge");
    auto* clear = addButton(1, 4, "layer-clear", tr("Clear Layer"), "clearLayerButton", "layer.erase");
    m_deleteButton = addButton(1, 5, "layer-delete", tr("Delete Layer"), "deleteButton", "layer.delete");
    m_oneLayerButtons = {newLayer, newFolder, duplicate, mergeDown};

    m_view = new LayerTreeView(this);
    m_view->setObjectName(QStringLiteral("layerTree"));
    m_view->setModel(m_model);
    m_view->setItemDelegate(new LayerItemDelegate(m_view));
    m_view->setHeaderHidden(true);
    m_view->setRootIsDecorated(false);
    m_view->setIndentation(0);
    m_view->setUniformRowHeights(true);
    m_view->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_view->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_view->selectFromLayer = [this](LayerId id, Qt::KeyboardModifiers mods, bool mask) { selectionFromLayer(id, mods, mask); };
    m_view->chooseTarget = [this](LayerId id, bool mask) {
        if (!m_doc)
            return;
        m_doc->setCurrentLayer(id);
        m_doc->setMaskTarget(mask);
    };
    m_view->setEditTriggers(QAbstractItemView::NoEditTriggers);
    // In an animation a top-level layer's menu shares it with every frame or makes it the current frame's own again.
    m_view->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_view, &QWidget::customContextMenuRequested, this, [this](const QPoint& pos) {
        const QModelIndex index = m_view->indexAt(pos);
        if (!m_doc || !index.isValid() || !index.data(LayerTreeModel::ShareableRole).toBool())
            return;
        const bool shared = index.data(LayerTreeModel::SharedRole).toBool();
        QMenu menu(m_view);
        QAction* toggle = menu.addAction(shared ? tr("Make Per-Frame") : tr("Share With Every Frame"));
        toggle->setObjectName(QStringLiteral("layerShareAction"));
        if (menu.exec(m_view->viewport()->mapToGlobal(pos)) == toggle)
            m_model->setData(index, !shared, LayerTreeModel::SharedRole);
    });
    m_view->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_view->setMouseTracking(true);
    m_view->setDragEnabled(true);
    m_view->setAcceptDrops(true);
    m_view->setDragDropMode(QAbstractItemView::InternalMove);
    m_view->setDefaultDropAction(Qt::MoveAction);
    m_view->setDropIndicatorShown(true);

    auto* layout = new QVBoxLayout(this);
    const int pad = Theme::metric(Theme::Metric::Padding);
    layout->setContentsMargins(pad, pad, pad, pad);
    layout->setSpacing(pad);
    layout->addWidget(m_controls);
    layout->addWidget(m_buttons);
    layout->addWidget(m_view, 1);

    // Controls → document. Syncing from the document blocks these signals, so it never creates an undo step.
    connect(m_modeCombo, &QComboBox::currentIndexChanged, this, [this] {
        const QVariant mode = m_modeCombo->currentData();
        if (mode.isValid())
            editCurrent([m = static_cast<BlendMode>(mode.toInt())](LayerProps& p) { p.mode = m; });
    });
    // One drag is one undo step: all its changes carry the merge key made when the handle was pressed.
    connect(m_opacityBar, &ValueBar::dragStarted, this, [this] {
        m_dragging = true;
        ++m_dragKey;
    });
    connect(m_opacityBar, &ValueBar::dragFinished, this, [this] { m_dragging = false; });
    connect(m_opacityBar, &ValueBar::valueChanged, this, [this](double value) {
        editCurrent([v = int(std::lround(value))](LayerProps& p) { p.opacity = v; }, m_dragging ? m_dragKey : 0);
    });
    connect(m_preserveCheck, &QCheckBox::toggled, this, [this](bool on) { editCurrent([on](LayerProps& p) { p.preserveOpacity = on; }); });
    connect(m_clippingCheck, &QCheckBox::toggled, this, [this](bool on) { editCurrent([on](LayerProps& p) { p.clipping = on; }); });
    connect(m_lockCheck, &QCheckBox::toggled, this, [this](bool on) { editCurrent([on](LayerProps& p) { p.locked = on; }); });

    const auto refuseIfFailed = [this](const auto& result) {
        if (!result)
            refuse(result.error());
    };
    connect(newLayer, &QToolButton::clicked, this, [this, refuseIfFailed] {
        if (m_doc)
            refuseIfFailed(m_doc->addLayer());
    });
    connect(newFolder, &QToolButton::clicked, this, [this, refuseIfFailed] {
        if (m_doc)
            refuseIfFailed(m_doc->addFolder());
    });
    connect(duplicate, &QToolButton::clicked, this, [this, refuseIfFailed] {
        if (m_doc)
            refuseIfFailed(m_doc->duplicateLayer(m_doc->currentLayerId()));
    });
    connect(mergeDown, &QToolButton::clicked, this, [this, refuseIfFailed] {
        if (m_doc)
            refuseIfFailed(m_doc->mergeDown(m_doc->currentLayerId()));
    });
    connect(m_mergeButton, &QToolButton::clicked, this, [this, refuseIfFailed] {
        if (m_doc)
            refuseIfFailed(m_doc->mergeLayers(m_selected));
    });
    connect(clear, &QToolButton::clicked, this, [this, refuseIfFailed] {
        if (m_doc)
            refuseIfFailed(m_selected.size() > 1 ? m_doc->clearLayers(m_selected) : m_doc->clearLayer(m_doc->currentLayerId()));
    });
    connect(m_deleteButton, &QToolButton::clicked, this, [this, refuseIfFailed] {
        if (!m_doc)
            return;
        // With the mask as the editing target, Delete removes the mask only (no question asked).
        if (m_selected.size() > 1)
            refuseIfFailed(m_doc->deleteLayers(m_selected));
        else
            refuseIfFailed(m_doc->maskTarget() ? m_doc->deleteLayerMask(m_doc->currentLayerId())
                                               : m_doc->deleteLayer(m_doc->currentLayerId()));
    });
    connect(m_newMaskButton, &QToolButton::clicked, this, [this, refuseIfFailed] {
        if (m_doc)
            refuseIfFailed(m_doc->addLayerMask(m_doc->currentLayerId()));
    });
    connect(m_applyMaskButton, &QToolButton::clicked, this, [this, refuseIfFailed] {
        if (m_doc)
            refuseIfFailed(m_doc->applyLayerMask(m_doc->currentLayerId()));
    });

    connect(m_view->selectionModel(), &QItemSelectionModel::currentChanged, this,
            [this](const QModelIndex& current) { onTreeSelectionChanged(current); });
    connect(m_view->selectionModel(), &QItemSelectionModel::selectionChanged, this, [this] { onTreeRowsSelected(); });
    connect(m_view, &QTreeView::expanded, this, [this](const QModelIndex& i) { rememberExpanded(i, true); });
    connect(m_view, &QTreeView::collapsed, this, [this](const QModelIndex& i) { rememberExpanded(i, false); });
    connect(m_model, &QAbstractItemModel::modelReset, this, &LayerPanel::onModelReset);
    connect(m_model, &LayerTreeModel::editRefused, this, &LayerPanel::editRefused);

    syncControls();
}

void LayerPanel::setDocument(Document* doc) {
    if (doc && doc == m_doc.data())
        return;
    if (m_doc)
        m_doc->disconnect(this);
    m_doc = doc;
    m_expanded.clear();
    m_selected.clear();
    m_dragging = false;
    m_model->setDocument(doc);
    if (doc) {
        connect(doc, &Document::layersChanged, this, &LayerPanel::onLayersChanged);
        connect(doc, &Document::currentLayerChanged, this, &LayerPanel::onCurrentLayerChanged);
        connect(doc, &Document::maskTargetChanged, this, &LayerPanel::syncControls);
        connect(doc, &QObject::destroyed, this, [this] { setDocument(nullptr); });
    }
    selectCurrent(true);
    syncControls();
}

void LayerPanel::onLayersChanged() {
    selectCurrent(false);
    syncControls();
}

void LayerPanel::onCurrentLayerChanged() {
    selectCurrent(true);
    syncControls();
}

void LayerPanel::onModelReset() {
    m_syncing = true;
    restoreExpansion({});
    m_syncing = false;
    selectCurrent(false);
    syncControls();
}

void LayerPanel::onTreeSelectionChanged(const QModelIndex& current) {
    // Ctrl+click on a selected row makes it the view's current index while taking it out of the selection.
    if (m_syncing || !m_doc || !m_view->selectionModel()->isSelected(current))
        return;
    const LayerId id = m_model->layerAt(current);
    if (id)
        m_doc->setCurrentLayer(id);
}

void LayerPanel::onTreeRowsSelected() {
    if (m_syncing || !m_doc)
        return;
    const QModelIndexList rows = m_view->selectionModel()->selectedRows();
    if (rows.isEmpty()) {  // the last selected row was Ctrl+clicked: the current layer stays selected
        m_selected.clear();
        selectCurrent(false);
        syncControls();
        return;
    }
    m_selected.clear();
    for (const QModelIndex& row : rows)
        m_selected.push_back(m_model->layerAt(row));
    // A click moves the view's current index first and selects after: the row last clicked is the current layer.
    const QModelIndex clicked = m_view->currentIndex();
    if (m_view->selectionModel()->isSelected(clicked))
        m_doc->setCurrentLayer(m_model->layerAt(clicked));
    else if (std::ranges::find(m_selected, m_doc->currentLayerId()) == m_selected.end())
        m_doc->setCurrentLayer(m_selected.back());
    syncControls();
}

void LayerPanel::syncControls() {
    const LayerNode* layer = m_doc ? m_doc->currentLayer() : nullptr;
    const bool several = m_selected.size() > 1;
    m_controls->setEnabled(layer != nullptr && !several);
    m_buttons->setEnabled(layer != nullptr);
    m_view->setEnabled(m_doc != nullptr);

    for (QToolButton* button : std::as_const(m_oneLayerButtons))
        button->setEnabled(!several);
    m_mergeButton->setEnabled(several);
    m_deleteButton->setEnabled(m_doc && (m_doc->layers().layerCount() > 1 || m_doc->maskTarget()));  // the mask goes, not the layer
    m_newMaskButton->setEnabled(layer && !several && !layer->mask);
    m_applyMaskButton->setEnabled(layer && !several && layer->mask && !layer->isFolder() && !layer->isText());

    const QSignalBlocker b1(m_modeCombo), b3(m_opacityBar), b4(m_preserveCheck), b5(m_clippingCheck), b6(m_lockCheck);
    if (!layer) {
        m_modeCombo->clear();
        m_modesKey = -1;
        m_opacityBar->setValue(100);
        for (QCheckBox* box : {m_preserveCheck, m_clippingCheck, m_lockCheck})
            box->setChecked(false);
        return;
    }
    const LayerProps& p = layer->props;
    rebuildModes(*layer);
    m_modeCombo->setCurrentIndex(m_modeCombo->findData(static_cast<int>(p.mode)));
    m_opacityBar->setValue(p.opacity);
    m_preserveCheck->setChecked(p.preserveOpacity);
    m_preserveCheck->setEnabled(!layer->isFolder());  // folders have no pixels to protect
    m_clippingCheck->setChecked(p.clipping);
    m_lockCheck->setChecked(p.locked);
}

void LayerPanel::rebuildModes(const LayerNode& layer) {
    const bool isFolder = layer.isFolder();
    const int key = isFolder ? 1 : 0;
    if (key == m_modesKey)
        return;
    m_modesKey = key;

    m_modeCombo->clear();
    bool lastWasSeparator = true;
    const auto addMode = [&](BlendMode mode) {
        m_modeCombo->addItem(blendModeName(mode), static_cast<int>(mode));
        lastWasSeparator = false;
    };
    const auto addSeparator = [&] {
        if (!lastWasSeparator)
            m_modeCombo->insertSeparator(m_modeCombo->count());
        lastWasSeparator = true;
    };
    if (isFolder) {
        addMode(BlendMode::PassThrough);
        addSeparator();
    }
    for (const std::optional<BlendMode>& entry : blendModeMenuOrder()) {
        if (entry)
            addMode(*entry);
        else
            addSeparator();
    }
    // The eight modes that apply opacity as a PSD layer does are one group below the others.
    addSeparator();
    for (const BlendMode mode : {BlendMode::LinearBurn, BlendMode::LinearDodge, BlendMode::LinearLight, BlendMode::ColorBurn,
                                 BlendMode::ColorDodge, BlendMode::VividLight, BlendMode::HardMixShape, BlendMode::DifferenceShape})
        addMode(mode);
}

void LayerPanel::selectCurrent(bool reveal) {
    if (!m_doc)
        return;
    const LayerId currentId = m_doc->currentLayerId();
    const QModelIndex index = m_model->indexOf(currentId);
    const bool wasSyncing = std::exchange(m_syncing, true);
    // The selection outlives edits and model resets as long as the current layer stays in it; another current layer ends it.
    std::erase_if(m_selected, [this](LayerId id) { return !m_doc->layer(id); });
    if (std::ranges::find(m_selected, currentId) == m_selected.end())
        m_selected = {currentId};
    if (!index.isValid()) {
        m_selected.clear();
        m_view->selectionModel()->clear();
    } else {
        if (reveal) {
            for (QModelIndex a = index.parent(); a.isValid(); a = a.parent()) {
                if (!m_view->isExpanded(a)) {
                    m_view->setExpanded(a, true);
                    m_expanded[m_model->layerAt(a)] = true;
                }
            }
        }
        QItemSelection rows;
        for (const LayerId id : m_selected) {
            const QModelIndex row = m_model->indexOf(id);
            rows.select(row, row);
        }
        m_view->selectionModel()->select(rows, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
        m_view->selectionModel()->setCurrentIndex(index, QItemSelectionModel::NoUpdate);
        if (reveal)
            m_view->scrollTo(index);
    }
    m_syncing = wasSyncing;
}

// A model reset forgets the view's expansion; folders keep what the user last chose, and start as the file says.
void LayerPanel::restoreExpansion(const QModelIndex& parent) {
    for (int row = 0; row < m_model->rowCount(parent); ++row) {
        const QModelIndex index = m_model->index(row, 0, parent);
        if (!m_model->data(index, LayerTreeModel::IsFolderRole).toBool())
            continue;
        const LayerId id = m_model->layerAt(index);
        if (!m_expanded.contains(id)) {
            const LayerNode* node = m_doc ? m_doc->layer(id) : nullptr;
            m_expanded[id] = node ? node->props.expanded : true;
        }
        m_view->setExpanded(index, m_expanded[id]);
        restoreExpansion(index);
    }
}

void LayerPanel::rememberExpanded(const QModelIndex& folder, bool expanded) {
    if (!m_syncing)
        m_expanded[m_model->layerAt(folder)] = expanded;
}

void LayerPanel::editCurrent(const std::function<void(LayerProps&)>& change, int mergeKey) {
    const LayerNode* layer = m_doc ? m_doc->currentLayer() : nullptr;
    if (!layer)
        return;
    LayerProps props = layer->props;
    change(props);
    const auto result = m_doc->setLayerProps(props.id, props, mergeKey);
    if (!result) {
        refuse(result.error());
        syncControls();  // put the control back to what the layer really has
    }
}

void LayerPanel::selectionFromLayer(LayerId id, Qt::KeyboardModifiers mods, bool mask) {
    if (!m_doc)
        return;
    // Ctrl replaces, Shift+Ctrl adds, Alt+Ctrl subtracts, Alt+Shift+Ctrl intersects.
    const auto result = mask ? m_doc->selectionFromMask(id, combineModeFor(mods)) : m_doc->selectionFromLayer(id, combineModeFor(mods));
    if (!result && result.error() != EditError::NoChange)
        refuse(result.error());
}

void LayerPanel::refuse(EditError error) {
    if (error != EditError::NoChange)
        emit editRefused(describe(error));
}

}
