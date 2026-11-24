#pragma once

#include "core/tools/ToolBox.h"

#include <QPointer>
#include <QWidget>

class QMenu;
class QScrollArea;
class QTabBar;

namespace doodlebold {

class ToolGrid;

class ToolTray : public QWidget {
    Q_OBJECT
public:
    explicit ToolTray(ToolBox* tools, QWidget* parent = nullptr);

    int group() const { return m_group; }
    void setGroup(int group);

    QRect cellRect(int index) const;
    int cellAt(const QPoint& pos) const;

    void moveSlotTo(SlotRef from, int cell);

    QMenu* createContextMenu(int cell);

    QTabBar* tabBar() const { return m_tabs; }
    // How many rows of tools show at once; the rest scroll. The user sets it with the handle under the tray, and it is
    // remembered. A group with fewer rows shows them all.
    static constexpr int kVisibleRows = 3;
    int visibleRows() const { return m_visibleRows; }
    void setVisibleRows(int rows);
    int shownRows() const;
    int groupRows() const;  // the rows of the group shown
    int rowPitch() const;   // from one row's top to the next one's

signals:
    void refused(const QString& message);

private:
    void syncTabs();
    void refreshGrid();
    void showCurrentCell();
    void report(const ToolResult<>& result);
    void rename(SlotRef ref);

    QPointer<ToolBox> m_tools;
    QTabBar* m_tabs = nullptr;
    ToolGrid* m_grid = nullptr;
    QScrollArea* m_scroll = nullptr;
    int m_group = 0;
    int m_visibleRows = kVisibleRows;
    bool m_syncing = false;

    friend class ToolGrid;
};

}
