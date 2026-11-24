#pragma once

#include <QColor>
#include <QPointer>
#include <QWidget>

#include <optional>
#include <vector>

class QMenu;
class QScrollArea;
class QToolButton;

namespace doodlebold {

class ColorAssets;
class ColorState;

class ColorCells : public QWidget {
    Q_OBJECT
public:
    ColorCells(QWidget* parent = nullptr);
    void setCells(std::vector<std::optional<QColor>> cells);
    void setCellSize(int px);
    int cellSize() const { return m_cell; }
    int columns() const;
    int cellAt(const QPoint& pos) const;  // -1 outside the cells
    QRect cellRect(int index) const;
    const std::vector<std::optional<QColor>>& cells() const { return m_cells; }

    bool hasHeightForWidth() const override { return true; }
    int heightForWidth(int w) const override;
    QSize sizeHint() const override;

signals:
    void clicked(int index);
    void menuRequested(int index, const QPoint& globalPos);

protected:
    void paintEvent(QPaintEvent* e) override;
    void mousePressEvent(QMouseEvent* e) override;
    void contextMenuEvent(QContextMenuEvent* e) override;

private:
    std::vector<std::optional<QColor>> m_cells;
    int m_cell = 15;
};

class SwatchesView : public QWidget {
    Q_OBJECT
public:
    SwatchesView(ColorState* state, ColorAssets* assets, QWidget* parent = nullptr);

    ColorCells* history() const { return m_history; }
    ColorCells* grid() const { return m_grid; }
    QMenu* createCellMenu(int index);

private:
    void sync();

    QPointer<ColorState> m_state;
    QPointer<ColorAssets> m_assets;
    ColorCells* m_history;
    ColorCells* m_grid;
    QScrollArea* m_scroll;
    QToolButton* m_sets;
};

}
