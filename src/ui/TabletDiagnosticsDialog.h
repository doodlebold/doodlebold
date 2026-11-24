#pragma once

#include <QDialog>
#include <QElapsedTimer>

class QLabel;
class QPlainTextEdit;
class QPushButton;
class QTimer;

namespace doodlebold {

class TabletStats;
class DiagBar;
class DiagHistory;
class DiagIndicator;
class DiagTestPad;
class DiagTilt;

class TabletDiagnosticsDialog : public QDialog {
    Q_OBJECT
public:
    TabletDiagnosticsDialog(TabletStats* stats, QWidget* parent = nullptr);

    QString reportText() const;

private:
    void buildUi();
    void onSample();
    void onProximity(bool inProximity);
    void refresh();
    void copyReport();
    bool idle() const;
    double eventRateHz() const;
    double medianIntervalMs() const;

    TabletStats* m_stats;
    QTimer* m_refreshTimer = nullptr;
    QElapsedTimer m_clock;
    qint64 m_lastEventAt = -1;

    QLabel* m_proximity = nullptr;
    QLabel* m_deviceName = nullptr;
    QLabel* m_deviceType = nullptr;
    QLabel* m_pointerType = nullptr;
    QLabel* m_capabilities = nullptr;
    QLabel* m_uniqueId = nullptr;
    QLabel* m_buttonCount = nullptr;

    QLabel* m_pressure = nullptr;
    QLabel* m_peak = nullptr;
    QLabel* m_tiltX = nullptr;
    QLabel* m_tiltY = nullptr;
    QLabel* m_rotation = nullptr;
    QLabel* m_tangential = nullptr;
    QLabel* m_position = nullptr;
    QLabel* m_pointerKind = nullptr;
    QLabel* m_rate = nullptr;
    QLabel* m_interval = nullptr;
    QLabel* m_total = nullptr;
    DiagBar* m_pressureBar = nullptr;
    DiagTilt* m_tiltView = nullptr;
    DiagIndicator* m_tipIndicator = nullptr;
    DiagIndicator* m_barrel1Indicator = nullptr;
    DiagIndicator* m_barrel2Indicator = nullptr;
    DiagHistory* m_history = nullptr;
    DiagTestPad* m_pad = nullptr;
    QPushButton* m_copyButton = nullptr;
    QPlainTextEdit* m_deviceList = nullptr;
};

}
