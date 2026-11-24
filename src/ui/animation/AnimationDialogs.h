#pragma once

#include "core/animation/Animation.h"
#include "core/animation/AnimationVideo.h"

#include <QDialog>

class QButtonGroup;
class QCheckBox;
class QDoubleSpinBox;
class QLabel;
class QPushButton;
class QRadioButton;
class QSlider;
class QSpinBox;

namespace doodlebold {

// What Add Video … settles.
struct AddVideoChoice {
    bool follow = true;       // Drawings follow the video: frames grow to its length at `fps`
    double fps = 12.0;
    bool atCurrentFrame = false;
    int opacity = 40;
    bool keepSound = true;
    bool lossless = false;
};

// Add Video …: how the video lines up with the frames, its rate, where it starts, its opacity, whether its sound becomes a
// sound clip, and how its pictures are cached.
class AddVideoDialog : public QDialog {
    Q_OBJECT
public:
    static constexpr double kRates[] = {12.0, 15.0, 16.0, 24.0};

    AddVideoDialog(const QString& fileName, const VideoInfo& info, const Animation& animation, const QSize& canvas,
                   QWidget* parent = nullptr);

    AddVideoChoice choice() const;
    void setFollow(bool follow);
    void setRate(double fps);  // one of the buttons, the source's rate, or a custom one
    int pictureCount() const;  // at the chosen rate
    QString summary() const;

private:
    // px: its width, as its least width was before it was fixed; room for the video's name and the choices on a line.
    static constexpr int kAddVideoWidth = 600;  // component metric: the dialog's fixed width
    void fitHeight();
    void update();

    VideoInfo m_info;
    int m_frames;
    double m_fps;
    int m_currentFrame;
    QRadioButton* m_follow;
    QRadioButton* m_keep;
    QButtonGroup* m_rates;
    QPushButton* m_custom;
    QDoubleSpinBox* m_customRate;
    QPushButton* m_fromFirst;
    QPushButton* m_fromCurrent;
    QSlider* m_opacity;
    QLabel* m_opacityText;
    QCheckBox* m_sound;
    QPushButton* m_jpeg;
    QPushButton* m_lossless;
    QLabel* m_summary;
};

// Timeline Settings: frame rate, and the onion skins' frames and opacity. (Loop is on the timeline itself.)
class TimelineSettingsDialog : public QDialog {
    Q_OBJECT
public:
    TimelineSettingsDialog(const Animation& animation, QWidget* parent = nullptr);

    double fps() const;
    TimelineView view() const;

private:
    QDoubleSpinBox* m_fps;
    QSpinBox* m_before;
    QSpinBox* m_after;
    QSlider* m_opacity;
    TimelineView m_view;
};

// "Delete the other 3 frames and keep frame 03 as an ordinary picture?"
QString keepOnlyQuestion(int frameCount, int currentFrame);
QString keepOnlyButtonText(int currentFrame);

}
