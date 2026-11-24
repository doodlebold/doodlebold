#pragma once

#include "core/brush/BrushPack.h"

#include <QDialog>

#include <vector>

class QLabel;
class QTableWidget;
class QTreeWidget;

namespace doodlebold {

// Install "<pack>": the pack's folders, each ticked to keep or not, with what its pictures go into the library as; then
// what will be installed, and the files left out.
class PackImportDialog : public QDialog {
    Q_OBJECT
public:
    enum Column {
        FolderColumn,
        PicturesColumn,
        UseColumn,
        ResultColumn,
        WhyColumn
    };

    explicit PackImportDialog(const BrushPack& pack, QWidget* parent = nullptr);

    // One entry per folder of the pack: Skip for a folder not ticked.
    std::vector<PackUse> uses() const;
    // Ticks or unticks a folder, and sets what it installs as (Skip unticks it).
    void setUse(int folder, PackUse use);
    void setKept(int folder, bool kept);
    QString summary() const;

private:
    void refresh();
    PackUse chosen(int folder) const;

    BrushPack m_pack;
    QTableWidget* m_table = nullptr;
    QLabel* m_summary = nullptr;
    QTreeWidget* m_leftOut = nullptr;
    bool m_refreshing = false;
};

}
