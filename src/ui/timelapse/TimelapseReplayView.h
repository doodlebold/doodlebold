#pragma once

#include "core/timelapse/Timelapse.h"
#include "core/timelapse/TimelapseExport.h"
#include "core/timelapse/TimelapseReplay.h"

#include <QElapsedTimer>
#include <QImage>
#include <QPixmap>
#include <QWidget>

#include <memory>

class QLabel;
class QPushButton;
class QSlider;
class QTimer;

namespace doodlebold {

// Covers the window's canvas (which is not touched) with the Full length frames; Escape is Done, Space is Play / Pause.
class TimelapseReplayView : public QWidget {
    Q_OBJECT
public:
    static constexpr int kBarWidth = 640;  // component metric: the replay bar's width
    static constexpr int kBarBottom = 18;

    TimelapseReplayView(std::shared_ptr<const Timelapse> recording, QWidget* parent);

    int frameCount() const { return int(m_frames.size()); }
    int position() const;
    void setPosition(int frame);
    bool playing() const;
    void play(bool on);
    QImage picture();
    QRect pictureRect() const;
    QRect barRect() const;

signals:
    void exportRequested();
    void doneRequested();

protected:
    void paintEvent(QPaintEvent* e) override;
    void resizeEvent(QResizeEvent* e) override;
    void keyPressEvent(QKeyEvent* e) override;
    void showEvent(QShowEvent* e) override;

private:
    void placeBar();
    void showFrame(int frame);
    void tick();
    void syncClock(int frame);

    std::shared_ptr<const Timelapse> m_recording;
    TimelapseReplay m_replay;
    std::vector<int> m_frames;
    QWidget* m_bar;
    QPushButton* m_play;
    QSlider* m_scrubber;
    QLabel* m_clock;
    QTimer* m_timer;
    QElapsedTimer m_clockStart;
    int m_playFrom = 0;
    int m_shownStep = -1;
    bool m_fastScaling = false;
    QPixmap m_pixmap;
};

}
