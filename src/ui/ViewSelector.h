#pragma once

#include "ui/Theme.h"

#include <QImage>
#include <QString>
#include <QWidget>

#include <vector>

namespace doodlebold {

class ViewSelector : public QWidget {
    Q_OBJECT
public:
    struct Item {
        QString name;
        QString zoom;
        QImage icon;
        QString toolTip;
    };

    static constexpr int kTabWidth = 182;  // px: a canvas's tab (component metric)
    static constexpr int kRowHeight = 30;  // px: a row of tabs (component metric)
    static constexpr int kMaxRows = 3;  // rows before scrolling
    static constexpr int kArrowWidth = Theme::kIconMd;
    static constexpr int kIconSize = Theme::kIconMd;

    explicit ViewSelector(QWidget* parent = nullptr);

    void setItems(std::vector<Item> items);
    void setItem(int index, Item item);
    const Item& item(int index) const { return m_items[static_cast<std::size_t>(index)]; }
    int count() const { return static_cast<int>(m_items.size()); }
    void setCurrent(int index);
    int current() const { return m_current; }

    int tabsPerRow() const;
    int rowCount() const;
    int visibleRows() const;
    int firstRow() const { return m_firstRow; }
    int itemAt(const QPoint& pos) const;
    QRect itemRect(int index) const;

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

signals:
    void activated(int index);
    void closeRequested(int index);

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;
    void wheelEvent(QWheelEvent* e) override;
    void resizeEvent(QResizeEvent* e) override;
    bool event(QEvent* e) override;

private:
    bool scrolls() const { return rowCount() > kMaxRows; }
    void scrollTo(int firstRow);
    void showCurrentRow();
    void relayout();

    std::vector<Item> m_items;
    int m_current = -1;
    int m_firstRow = 0;
    int m_lastRows = 0;
};

}
