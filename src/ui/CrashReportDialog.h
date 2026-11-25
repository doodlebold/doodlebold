#pragma once

#include <QDialog>

class QPushButton;

namespace doodlebold {

class CrashReportDialog : public QDialog {
    Q_OBJECT
public:
    CrashReportDialog(const QString& report, bool afterCrash, QWidget* parent = nullptr);

private:
    void copyReport();

    QString m_report;
    QPushButton* m_copy = nullptr;
};

}
