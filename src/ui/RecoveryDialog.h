#pragma once

#include "core/io/RecoveryStore.h"

#include <QDialog>

#include <vector>

class QLabel;
class QPushButton;
class QVBoxLayout;

namespace doodlebold {

class RecoveryController;

// At start, the canvases a run that didn't close left recovery copies of: each restored or discarded, or all at once. Closing it
// keeps what is left for the next start.
class RecoveryDialog : public QDialog {
    Q_OBJECT
public:
    RecoveryDialog(RecoveryController* recovery, const std::vector<LeftoverCanvas>& canvases, QWidget* parent = nullptr);

private:
    struct Row {
        LeftoverCanvas canvas;
        QWidget* widget = nullptr;
        QLabel* details = nullptr;
        QPushButton* restore = nullptr;
    };

    void addRow(const LeftoverCanvas& canvas);
    // Restores or discards a row's canvas and takes its row away; the dialog closes with the last.
    void settle(QWidget* row, bool restore);
    void settleAll(bool restore);

    RecoveryController* m_recovery;
    QVBoxLayout* m_list = nullptr;
    std::vector<Row> m_rows;
};

}
