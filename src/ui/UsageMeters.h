#pragma once

#include <QString>
#include <QTimer>
#include <QWidget>

namespace doodlebold {

class UsageMeters : public QWidget {
    Q_OBJECT
public:
    explicit UsageMeters(QWidget* parent = nullptr);

    int memoryPercent() const { return m_memory; }
    int residentPercent() const { return m_resident; }
    int drivePercent() const { return m_drive; }
    QString memoryText() const;
    QString driveText() const;

    QSize sizeHint() const override;

public slots:
    void refresh();

protected:
    void paintEvent(QPaintEvent*) override;

private:
    QTimer m_timer;
    int m_memory = -1, m_resident = -1, m_drive = -1;
};

}
