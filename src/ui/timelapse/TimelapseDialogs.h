#pragma once

#include "core/timelapse/Timelapse.h"
#include "core/timelapse/TimelapseExport.h"
#include "core/timelapse/TimelapseReplay.h"

#include <QDialog>
#include <QElapsedTimer>
#include <QStringList>

#include <memory>

class QButtonGroup;
class QCheckBox;
class QFormLayout;
class QAbstractButton;
class QSpinBox;
class QLabel;
class QPushButton;
class QSlider;
class QTimer;

namespace doodlebold {

QWidget* segmentedRow(QButtonGroup* group, const QStringList& labels, int checked, const QString& name, QWidget* parent);

class ExportQualityRows : public QObject {
    Q_OBJECT
public:
    ExportQualityRows(QFormLayout* form, QWidget* parent, const QString& prefix);
    int qualityId() const;
    int crf() const;             // 18, 23, 28
    int maxBitrateKbps() const;  // 0: no cap
    double maxFileMb() const;    // 0: no limit

signals:
    void changed();

private:
    QButtonGroup* m_quality;
    QCheckBox* m_capRate = nullptr;
    QSpinBox* m_maxRate = nullptr;
    QCheckBox* m_capSize = nullptr;
    QSpinBox* m_maxSize = nullptr;
};

class FfmpegMissingDialog : public QDialog {
    Q_OBJECT
public:
    enum class Answer {
        Cancel,
        TryAgain,
        Located
    };

    FfmpegMissingDialog(const QString& problem, QWidget* parent = nullptr);
    Answer answer() const { return m_answer; }
    QString locatedPath() const { return m_located; }
    static QString packageHint();

private:
    Answer m_answer = Answer::Cancel;
    QString m_located;
};

struct TimelapseExportChoice {
    TimelapseExportOptions options;
    int crf = 18;               // High 18, Medium 23, Small file 28
    int maxBitrateKbps = 0;     // 0: no cap
    double maxFileMb = 0.0;     // 0: no limit
    int audioKbps = 128;
    QString soundFile;          // empty: no sound; else the file, cut to the video's length
};

QString timelapseClock(double seconds);

class ExportTimelapseDialog : public QDialog {
    Q_OBJECT
public:
    static constexpr int kPreviewWidth = 560;  // component metric: the preview box
    static constexpr int kPreviewHeight = 400;  // component metric: the preview box

    ExportTimelapseDialog(std::shared_ptr<const Timelapse> recording, const QString& canvasName, QString encoderNote,
                          QWidget* parent = nullptr);

    TimelapseExportChoice choice() const;
    TimelapsePlan plan() const;
    void setSoundFile(const QString& file);
    int scrubberPosition() const;
    bool playing() const { return isPlaying(); }
    void play(bool on) { setPlaying(on); }
    void setScrubberPosition(int position);
    QSize previewedSize() const;

protected:
    // The sound file to add; empty: none chosen. Virtual so tests can answer without a dialog.
    virtual QString askSoundFile();

private:
    void update();
    void showPicture();
    bool isPlaying() const;
    void setPlaying(bool playing);
    void playTick();
    void syncSoundChip();
    void showMoreSettings(bool shown);
    void showTime(int frame);

    std::shared_ptr<const Timelapse> m_recording;
    TimelapseReplay m_replay;
    QString m_encoderNote;
    QString m_soundFile;
    TimelapsePlan m_plan;
    QLabel* m_preview;
    QPushButton* m_play;
    QTimer* m_playTimer;
    QElapsedTimer m_playClock;
    int m_playFrom = 0;
    int m_playShown = -1;
    int m_playShownStep = -1;
    QSlider* m_scrubber;
    QLabel* m_videoTime;
    QLabel* m_videoLength;
    QLabel* m_fullNote;
    QButtonGroup* m_length;
    QAbstractButton* m_more;
    QWidget* m_settings;
    QLabel* m_sizeLabel;
    QSpinBox* m_hold;
    QCheckBox* m_leaveOutUndone;
    QPushButton* m_sound;
    QLabel* m_soundNone;
    QPushButton* m_soundChip;
    ExportQualityRows* m_rows;
    QLabel* m_summary;
};

}
