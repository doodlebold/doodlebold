#include "ui/panels/ToolTray.h"

#include "core/options/AppOptions.h"
#include "ui/ButtonTips.h"
#include "ui/Theme.h"
#include "ui/panels/ToolIcons.h"

#include <QApplication>
#include <QContextMenuEvent>
#include <QDrag>
#include <QDragEnterEvent>
#include <QDragLeaveEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QFontMetrics>
#include <QHelpEvent>
#include <QInputDialog>
#include <QMenu>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QScrollArea>
#include <QTabBar>
#include <QToolTip>
#include <QVBoxLayout>

#include <algorithm>
#include <memory>

namespace doodlebold {

namespace {

using Theme::Metric;
using Theme::Token;

constexpr int kIconSize = Theme::kIconMd;
constexpr const char* kSlotMime = "application/x-doodlebold-toolslot";  // "group,index" of the dragged slot

constexpr ToolType kCreatable[] = {ToolType::Pencil,       ToolType::Brush,           ToolType::WaterColor,  ToolType::Marker,
                                   ToolType::Eraser,       ToolType::Bucket,          ToolType::Blur,        ToolType::Smudge,
                                   ToolType::SelectionPen, ToolType::SelectionEraser, ToolType::PixelArtPen, ToolType::Clone};

constexpr int kCellGap = Theme::kSpace2;
constexpr int kSectionGap = Theme::compact::kSectionGap;
constexpr int kPadY = Theme::kSpace2;  // a cell's padding round its name and icon
constexpr int kPadX = Theme::kSpace2;
constexpr int kMostVisibleRows = 30;
const QString kVisibleRowsKey = QStringLiteral("layout/toolTrayRows");

int gap() {
    return kCellGap;
}

// The main window's command that selects a tool of this type (the keys N, V, C, E, A, S; C is shared by Water Color and the
// Pixel Art Pen; the Clone Stamp's has no key until the user binds one; the other tools have no command), or none.
QString toolCommand(ToolType type) {
    switch (type) {
    case ToolType::Pencil: return QStringLiteral("tool.pencil");
    case ToolType::Brush: return QStringLiteral("tool.brush");
    case ToolType::WaterColor:
    case ToolType::PixelArtPen: return QStringLiteral("tool.waterColor");
    case ToolType::Eraser: return QStringLiteral("tool.eraser");
    case ToolType::SelectionPen: return QStringLiteral("tool.selectionPen");
    case ToolType::SelectionEraser: return QStringLiteral("tool.selectionEraser");
    case ToolType::Clone: return QStringLiteral("tool.cloneStamp");
    case ToolType::Bucket: return QStringLiteral("tool.bucket");
    default: return {};
    }
}

}

class ToolGrid : public QWidget {
public:
    ToolGrid(ToolBox* tools, ToolTray* tray) : QWidget(tray), m_tools(tools), m_tray(tray) {
        setObjectName(QStringLiteral("toolGrid"));
        setMouseTracking(true);
        setAcceptDrops(true);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    }

    int heightForRows(int rows) const { return rows * cellHeight() + std::max(0, rows - 1) * gap(); }
    int rows() const { return (cellCount() + ToolBox::kColumns - 1) / ToolBox::kColumns; }
    int pitch() const { return cellHeight() + gap(); }

    void refresh() {
        setFixedHeight(heightForRows(rows()));
        update();
    }

    QRect cellRect(int index) const {
        const int cols = ToolBox::kColumns;
        const int w = (width() - gap() * (cols - 1)) / cols;
        const int h = cellHeight();
        return QRect((index % cols) * (w + gap()), (index / cols) * (h + gap()), w, h);
    }

    int cellAt(const QPoint& pos) const {
        for (int i = 0; i < cellCount(); ++i)
            if (cellRect(i).contains(pos))
                return i;
        return -1;
    }

    // 240 px wide by default, 40 px a column at the least (component metrics: room for a tool's name).
    QSize sizeHint() const override { return QSize(240, heightForRows(rows())); }
    QSize minimumSizeHint() const override { return QSize(ToolBox::kColumns * 40, heightForRows(rows())); }

    QString tipAt(const QPoint& pos) const {
        const int cell = cellAt(pos);
        const ToolSlot* slot = cell < 0 ? nullptr : m_tools->slot({m_tray->group(), cell});
        return slot ? button_tips::commandTip(this, slot->name, toolCommand(slot->type())) : QString();
    }

protected:
    bool event(QEvent* e) override {
        if (e->type() == QEvent::ToolTip) {
            const auto* help = static_cast<QHelpEvent*>(e);
            const QString tip = tipAt(help->pos());
            // An empty text hides the tip at once (hideText waits a moment).
            QToolTip::showText(help->globalPos(), tip, this, tip.isEmpty() ? QRect() : cellRect(cellAt(help->pos())));
            return true;
        }
        return QWidget::event(e);
    }

    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const double radius = Theme::metric(Metric::Radius);
        const SlotRef current = m_tools->currentSlot();
        for (int i = 0; i < cellCount(); ++i) {
            const QRectF r = QRectF(cellRect(i)).adjusted(0.5, 0.5, -0.5, -0.5);
            const ToolSlot* slot = m_tools->slot({m_tray->group(), i});
            const bool isCurrent = slot && current == SlotRef{m_tray->group(), i} && !m_tools->fixedTool();
            if (!slot) {
                p.setPen(QPen(Theme::color(Token::Border), Theme::kBorder, Qt::DotLine));
                p.setBrush(Qt::NoBrush);
                p.drawRoundedRect(r, radius, radius);
            } else {
                p.setPen(Qt::NoPen);
                p.setBrush(Theme::color(isCurrent ? Token::Selected : i == m_hover ? Token::Hover : Token::Base));
                p.setPen(isCurrent ? QPen(Qt::NoPen) : QPen(Theme::color(Token::Border), Theme::kBorder));
                p.drawRoundedRect(r, radius, radius);
                drawContent(p, cellRect(i), *slot, isCurrent);
            }
            if (i == m_dropCell) {
                p.setPen(QPen(Theme::color(Token::Accent), Theme::kBorderStrong));
                p.setBrush(Qt::NoBrush);
                p.drawRoundedRect(r, radius, radius);
            }
        }
    }

    void mousePressEvent(QMouseEvent* e) override {
        if (e->button() != Qt::LeftButton)
            return;
        const int cell = cellAt(e->position().toPoint());
        if (cell < 0 || !m_tools->slot({m_tray->group(), cell}))
            return;
        m_pressCell = cell;
        m_pressPos = e->position().toPoint();
        m_tray->report(m_tools->selectSlot({m_tray->group(), cell}));
    }

    void mouseMoveEvent(QMouseEvent* e) override {
        const int hover = cellAt(e->position().toPoint());
        if (hover != m_hover) {
            m_hover = hover;
            update();
        }
        if ((e->buttons() & Qt::LeftButton) && m_pressCell >= 0 &&
            (e->position().toPoint() - m_pressPos).manhattanLength() >= QApplication::startDragDistance())
            startDrag();
    }

    void mouseReleaseEvent(QMouseEvent*) override { m_pressCell = -1; }

    void leaveEvent(QEvent*) override {
        m_hover = -1;
        update();
    }

    void contextMenuEvent(QContextMenuEvent* e) override {
        const int cell = cellAt(e->pos());
        if (cell < 0)
            return;
        std::unique_ptr<QMenu> menu(m_tray->createContextMenu(cell));
        menu->exec(e->globalPos());
    }

    void dragEnterEvent(QDragEnterEvent* e) override {
        if (e->mimeData()->hasFormat(QLatin1String(kSlotMime)))
            e->acceptProposedAction();
    }

    void dragMoveEvent(QDragMoveEvent* e) override {
        const int cell = cellAt(e->position().toPoint());
        if (cell != m_dropCell) {
            m_dropCell = cell;
            update();
        }
        if (cell >= 0)
            e->acceptProposedAction();
        else
            e->ignore();
    }

    void dragLeaveEvent(QDragLeaveEvent*) override {
        m_dropCell = -1;
        update();
    }

    void dropEvent(QDropEvent* e) override {
        const int cell = cellAt(e->position().toPoint());
        m_dropCell = -1;
        update();
        const QList<QByteArray> parts = e->mimeData()->data(QLatin1String(kSlotMime)).split(',');
        if (cell < 0 || parts.size() != 2)
            return;
        e->acceptProposedAction();
        m_tray->moveSlotTo({parts[0].toInt(), parts[1].toInt()}, cell);
    }

private:
    int cellCount() const { return int(m_tools->group(m_tray->group()).cells.size()); }
    // Each cell has the name at the top-left and the icon at the bottom-right, overlapping rows.
    // Room for a name of two lines ("Water / Color"); the icon sits beside the second line.
    int cellHeight() const { return 2 * fontMetrics().height() + 2 * kPadY; }

    void drawContent(QPainter& p, const QRect& cell, const ToolSlot& slot, bool isCurrent) const {
        const QRect icon(cell.right() - kPadX - kIconSize + 1, cell.bottom() - kPadY - kIconSize + 1, kIconSize, kIconSize);
        toolTypeIcon(slot.type()).paint(&p, icon);
        const QRect text(cell.left() + kPadX, cell.top() + kPadY, cell.width() - 2 * kPadX, 2 * fontMetrics().height());
        p.setPen(Theme::color(isCurrent ? Token::SelectedText : Token::Text));
        if (fontMetrics().horizontalAdvance(slot.name) <= text.width()) {
            p.drawText(text, Qt::AlignLeft | Qt::AlignTop, slot.name);
        } else {
            // Too long for one line: the words wrap onto a second; the second line keeps clear of the icon.
            // As many words on the first line as fit, the rest on the second.
            const QStringList words = slot.name.split(QLatin1Char(' '), Qt::SkipEmptyParts);
            QString first = words.value(0);
            qsizetype used = 1;
            while (used < words.size() && fontMetrics().horizontalAdvance(first + QLatin1Char(' ') + words[used]) <= text.width())
                first += QLatin1Char(' ') + words[used++];
            const QString second = words.mid(used).join(QLatin1Char(' '));
            const int h = fontMetrics().height();
            p.drawText(QRect(text.left(), text.top(), text.width(), h), Qt::AlignLeft | Qt::AlignTop,
                       fontMetrics().elidedText(first, Qt::ElideRight, text.width()));
            p.drawText(QRect(text.left(), text.top() + h, text.width() - kIconSize - gap(), h), Qt::AlignLeft | Qt::AlignTop,
                       fontMetrics().elidedText(second, Qt::ElideRight, text.width() - kIconSize - gap()));
        }
    }

    void startDrag() {
        const SlotRef from{m_tray->group(), m_pressCell};
        auto* mime = new QMimeData;
        mime->setData(QLatin1String(kSlotMime), QByteArray::number(from.group) + ',' + QByteArray::number(from.index));
        auto* drag = new QDrag(this);
        drag->setMimeData(mime);
        drag->setPixmap(grab(cellRect(m_pressCell)));
        m_pressCell = -1;
        drag->exec(Qt::MoveAction);
        m_dropCell = -1;
        update();
    }

    ToolBox* m_tools;
    ToolTray* m_tray;
    int m_hover = -1;
    int m_pressCell = -1;
    QPoint m_pressPos;
    int m_dropCell = -1;
};

ToolTray::ToolTray(ToolBox* tools, QWidget* parent) : QWidget(parent), m_tools(tools) {
    m_tabs = new QTabBar(this);
    m_tabs->setObjectName(QStringLiteral("toolGroupTabs"));
    m_tabs->setExpanding(true);
    m_tabs->setDrawBase(false);
    m_tabs->setAttribute(Qt::WA_StyledBackground);
    m_grid = new ToolGrid(tools, this);
    m_scroll = new QScrollArea(this);
    m_scroll->setObjectName(QStringLiteral("toolGridScroll"));
    m_scroll->setFrameShape(QFrame::NoFrame);
    m_scroll->setWidgetResizable(true);
    m_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_scroll->setWidget(m_grid);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(kSectionGap);
    layout->addWidget(m_tabs);
    layout->addWidget(m_scroll);

    m_visibleRows = std::clamp(AppOptions::settings().value(kVisibleRowsKey, kVisibleRows).toInt(), 1, kMostVisibleRows);
    m_group = std::clamp(tools->currentSlot().group, 0, tools->groupCount() - 1);
    syncTabs();
    refreshGrid();

    connect(m_tabs, &QTabBar::currentChanged, this, [this](int g) {
        if (!m_syncing)
            setGroup(g);
    });
    connect(tools, &ToolBox::layoutChanged, this, [this] {
        m_group = std::clamp(m_group, 0, m_tools->groupCount() - 1);
        syncTabs();
        refreshGrid();
    });
    connect(tools, &ToolBox::currentSlotChanged, this, [this] {
        setGroup(m_tools->currentSlot().group);
        m_grid->update();
        showCurrentCell();
    });
    // The current tool is not highlighted while a fixed tool is active.
    connect(tools, &ToolBox::activeToolChanged, m_grid, qOverload<>(&QWidget::update));
}

void ToolTray::setGroup(int group) {
    group = std::clamp(group, 0, m_tools->groupCount() - 1);
    if (group == m_group)
        return;
    m_group = group;
    syncTabs();
    refreshGrid();
}

void ToolTray::setVisibleRows(int rows) {
    rows = std::clamp(rows, 1, kMostVisibleRows);
    if (rows == m_visibleRows)
        return;
    m_visibleRows = rows;
    AppOptions::settings().setValue(kVisibleRowsKey, rows);
    refreshGrid();
}

int ToolTray::shownRows() const {
    return std::clamp(m_grid->rows(), 1, m_visibleRows);
}

int ToolTray::groupRows() const {
    return m_grid->rows();
}

int ToolTray::rowPitch() const {
    return m_grid->pitch();
}

void ToolTray::refreshGrid() {
    m_grid->refresh();
    m_scroll->setFixedHeight(m_grid->heightForRows(shownRows()));
}

void ToolTray::showCurrentCell() {
    const SlotRef current = m_tools->currentSlot();
    if (current.group != m_group)
        return;
    const QRect cell = m_grid->cellRect(current.index);
    m_scroll->ensureVisible(cell.center().x(), cell.center().y(), 0, cell.height() / 2);
}

void ToolTray::syncTabs() {
    m_syncing = true;
    const int n = m_tools->groupCount();
    while (m_tabs->count() > n)
        m_tabs->removeTab(m_tabs->count() - 1);
    while (m_tabs->count() < n)
        m_tabs->addTab(QString());
    for (int g = 0; g < n; ++g)
        m_tabs->setTabText(g, m_tools->group(g).name);
    m_tabs->setCurrentIndex(m_group);
    m_syncing = false;
}

QRect ToolTray::cellRect(int index) const {
    return m_grid->cellRect(index).translated(m_grid->mapTo(this, QPoint(0, 0)));
}

int ToolTray::cellAt(const QPoint& pos) const {
    return m_grid->cellAt(pos - m_grid->mapTo(this, QPoint(0, 0)));
}

void ToolTray::moveSlotTo(SlotRef from, int cell) {
    report(m_tools->moveSlot(from, {m_group, cell}));
}

void ToolTray::report(const ToolResult<>& result) {
    if (!result)
        emit refused(describe(result.error()));
}

void ToolTray::rename(SlotRef ref) {
    const ToolSlot* slot = m_tools->slot(ref);
    if (!slot)
        return;
    bool accepted = false;
    const QString name = QInputDialog::getText(this, tr("Rename Tool"), tr("Name:"), QLineEdit::Normal, slot->name, &accepted);
    if (accepted)
        report(m_tools->renameSlot(ref, name));
}

QMenu* ToolTray::createContextMenu(int cell) {
    const SlotRef ref{m_group, cell};
    auto* menu = new QMenu(this);
    if (!m_tools->slot(ref)) {
        menu->addAction(tr("Create Tool"))->setEnabled(false);
        menu->addSeparator();
        for (ToolType type : kCreatable)
            menu->addAction(toolTypeIcon(type), toolTypeName(type), this, [this, ref, type] {
                const auto made = m_tools->createSlot(ref, type);
                if (made)
                    report(m_tools->selectSlot(made.value()));
                else
                    emit refused(describe(made.error()));
            });
        return menu;
    }
    menu->addAction(tr("Duplicate"), this, [this, ref] {
        const auto copy = m_tools->duplicateSlot(ref);
        if (copy)
            report(m_tools->selectSlot(copy.value()));
        else
            emit refused(describe(copy.error()));
    });
    menu->addAction(tr("Delete"), this, [this, ref] { report(m_tools->deleteSlot(ref)); })
        ->setEnabled(m_tools->toolCount() > 1);  // the last tool stays
    menu->addAction(tr("Initialize Settings"), this, [this, ref] { report(m_tools->initializeSlot(ref)); });
    menu->addSeparator();
    menu->addAction(tr("Rename…"), this, [this, ref] { rename(ref); });
    return menu;
}

}
