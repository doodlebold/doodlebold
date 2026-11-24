#include "ui/color/SwatchesView.h"

#include "core/color/ColorAssets.h"
#include "core/color/ColorState.h"
#include "ui/ButtonTips.h"
#include "ui/Components.h"
#include "ui/Theme.h"
#include "ui/color/AssetListPopup.h"

#include <QContextMenuEvent>
#include <QHBoxLayout>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QScrollArea>
#include <QScrollBar>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>

namespace doodlebold {

namespace {

constexpr int kGridRows = 6;
constexpr int kHistoryCell = 15;  // px: a recent colour's square (component metric)
// An empty cell's checkerboard: lighter than the panels' own (question 5 of the design system keeps each as it was).
constexpr QRgb kEmptyLight = 0xfff0f0f0;
constexpr QRgb kEmptyDark = 0xffe0e0e0;

}

ColorCells::ColorCells(QWidget* parent) : QWidget(parent) {
    QSizePolicy policy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    policy.setHeightForWidth(true);
    setSizePolicy(policy);
}

void ColorCells::setCells(std::vector<std::optional<QColor>> cells) {
    const bool resized = cells.size() != m_cells.size();
    m_cells = std::move(cells);
    if (resized)
        updateGeometry();
    update();
}

void ColorCells::setCellSize(int px) {
    m_cell = std::max(4, px);
    updateGeometry();
    update();
}

int ColorCells::columns() const {
    return std::max(1, width() / m_cell);
}

int ColorCells::heightForWidth(int w) const {
    const int cols = std::max(1, w / m_cell);
    return std::max(1, (int(m_cells.size()) + cols - 1) / cols) * m_cell;
}

QSize ColorCells::sizeHint() const {
    return {12 * m_cell, heightForWidth(12 * m_cell)};
}

QRect ColorCells::cellRect(int index) const {
    const int cols = columns();
    return QRect((index % cols) * m_cell, (index / cols) * m_cell, m_cell, m_cell);
}

int ColorCells::cellAt(const QPoint& pos) const {
    if (pos.x() < 0 || pos.y() < 0 || pos.x() >= columns() * m_cell)
        return -1;
    const int index = (pos.y() / m_cell) * columns() + pos.x() / m_cell;
    return index < int(m_cells.size()) ? index : -1;
}

void ColorCells::paintEvent(QPaintEvent*) {
    QPainter p(this);
    const int half = std::max(1, m_cell / 2);
    for (int i = 0; i < int(m_cells.size()); ++i) {
        const QRect r = cellRect(i);
        if (const auto& c = m_cells[std::size_t(i)]) {
            p.fillRect(r, *c);
            continue;
        }
        components::paintChecker(p, r, half, kEmptyLight, kEmptyDark);
    }
}

void ColorCells::mousePressEvent(QMouseEvent* e) {
    if (e->button() == Qt::LeftButton)
        if (const int i = cellAt(e->position().toPoint()); i >= 0)
            emit clicked(i);
}

void ColorCells::contextMenuEvent(QContextMenuEvent* e) {
    if (const int i = cellAt(e->pos()); i >= 0)
        emit menuRequested(i, e->globalPos());
}

SwatchesView::SwatchesView(ColorState* state, ColorAssets* assets, QWidget* parent) : QWidget(parent), m_state(state), m_assets(assets) {
    setObjectName(QStringLiteral("swatches"));
    m_history = new ColorCells(this);
    m_history->setObjectName(QStringLiteral("colorHistory"));
    m_history->setToolTip(tr("Color history: the colours of your last strokes"));
    m_sets = new QToolButton(this);
    m_sets->setObjectName(QStringLiteral("swatchSetsButton"));
    m_sets->setArrowType(Qt::DownArrow);
    m_sets->setAutoRaise(true);
    button_tips::setButtonTip(m_sets, tr("Swatch Sets"));
    m_grid = new ColorCells;
    m_grid->setObjectName(QStringLiteral("swatchGrid"));
    m_scroll = new QScrollArea(this);
    m_scroll->setWidget(m_grid);
    m_scroll->setWidgetResizable(true);
    m_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
    m_scroll->setFrameShape(QFrame::StyledPanel);

    auto* top = new QHBoxLayout;
    top->setContentsMargins(0, 0, 0, 0);
    top->addWidget(m_history, 1);
    top->addWidget(m_sets);
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(Theme::metric(Theme::Metric::Gap));
    layout->addLayout(top);
    layout->addWidget(m_scroll);

    const auto pick = [this](ColorCells* cells, int i) {
        if (m_state && cells->cells()[std::size_t(i)])
            m_state->setColor(*cells->cells()[std::size_t(i)]);
    };
    connect(m_history, &ColorCells::clicked, this, [pick, this](int i) { pick(m_history, i); });
    connect(m_grid, &ColorCells::clicked, this, [pick, this](int i) { pick(m_grid, i); });
    connect(m_grid, &ColorCells::menuRequested, this, [this](int i, const QPoint& at) {
        std::unique_ptr<QMenu> menu(createCellMenu(i));
        menu->exec(at);
    });
    connect(m_sets, &QToolButton::clicked, this, [this] {
        std::vector<QString> names;
        names.reserve(m_assets->swatchSetCount());
        for (int i = 0; i < m_assets->swatchSetCount(); ++i)
            names.push_back(m_assets->swatchSet(i).name);
        AssetListPopup::Actions actions;
        actions.choose = [this](int i) { m_assets->setCurrentSwatchSet(i); };
        actions.add = [this] { m_assets->addSwatchSet(); };
        actions.remove = [this](int i) { m_assets->removeSwatchSet(i); };
        actions.rename = [this](int i, const QString& name) { m_assets->renameSwatchSet(i, name); };
        auto* popup = new AssetListPopup(tr("Swatch Set"), names, m_assets->currentSwatchSet(), actions, this);
        popup->adjustSize();
        popup->move(m_sets->mapToGlobal(QPoint(m_sets->width() - popup->width(), m_sets->height())));
        popup->show();
    });
    connect(assets, &ColorAssets::historyChanged, this, &SwatchesView::sync);
    connect(assets, &ColorAssets::swatchesChanged, this, &SwatchesView::sync);
    connect(assets, &ColorAssets::optionsChanged, this, &SwatchesView::sync);
    sync();
}

QMenu* SwatchesView::createCellMenu(int index) {
    auto* menu = new QMenu(this);
    QAction* reg = menu->addAction(tr("Register Color"));
    reg->setObjectName(QStringLiteral("registerColor"));
    connect(reg, &QAction::triggered, this, [this, index] {
        if (m_state)
            m_assets->setSwatch(index, m_state->color());
    });
    if (m_assets->currentSet().cells[std::size_t(index)]) {
        QAction* del = menu->addAction(tr("Delete Color"));
        del->setObjectName(QStringLiteral("deleteColor"));
        connect(del, &QAction::triggered, this, [this, index] { m_assets->setSwatch(index, std::nullopt); });
    }
    return menu;
}

void SwatchesView::sync() {
    if (!m_assets)
        return;
    const int cell = swatchCellPixels(m_assets->swatchSize());
    std::vector<std::optional<QColor>> history(static_cast<std::size_t>(ColorAssets::kHistorySize));
    for (std::size_t i = 0; i < m_assets->history().size() && i < history.size(); ++i)
        history[i] = m_assets->history()[i];
    m_history->setCellSize(kHistoryCell);
    m_history->setCells(std::move(history));
    m_history->setFixedHeight(kHistoryCell);
    m_grid->setCellSize(cell);
    m_grid->setCells(m_assets->currentSet().cells);
    m_scroll->setFixedHeight(kGridRows * kHistoryCell + 2 * m_scroll->frameWidth());
}

}
