#pragma once

#include <QPointer>
#include <QWidget>

#include <QList>

#include <functional>
#include <optional>

#include <vector>

class QAbstractButton;
class QBoxLayout;
class QAction;
class QLabel;
class QMenu;
class QPushButton;
class QScrollArea;
class QSlider;
class QSpinBox;
class QToolButton;
class QWheelEvent;

namespace doodlebold {

class Document;
class FrameStrip;
class TimeRuler;
class TrackHeaders;

// The timeline under the canvases: it shows the current canvas's frames.
// - Top, left to right: the Timeline menu (Hide Timeline, Keep Only This Frame), undo and redo, the Frame menu (+ Frame,
//   Duplicate, Hold, and Delete apart at its foot), Play (its menu: Loop and Settings); on the right the playback speed,
//   Export, + Add (+ Track, + Sound, + Video) and ⋯ (the onion skin, its strength, the tracks' height, Fit).
// - A video bar with the video's name and its opacity while the canvas has a video (Canvas ▸ Remove Video takes it away).
// - The track headers' bar (eye, lock, duplicate and delete for the current track) beside the ruler (TimeRuler); the track
//   headers (TrackHeaders) beside the lanes (FrameStrip); + Add Track under the headers.
// - Bottom: first, previous, play, next and last; the play range's Start and End and its duration (a click on it shows frames
//   or time); on the right the tracks' Height, the zoom with − and +, and a toggle that makes the timeline as tall as the
//   window allows. Where the playhead is shows on the ruler's chip.
// Ctrl+wheel over the lanes zooms, Ctrl+Shift+wheel changes the tracks' height. The buttons and menu items ask for commands
// by signal; AnimationController carries them out.
class TimelinePanel : public QWidget {
    Q_OBJECT
public:
    explicit TimelinePanel(QWidget* parent = nullptr);

    void setDocument(Document* doc);
    Document* document() const { return m_doc.data(); }
    FrameStrip* strip() const { return m_strip; }
    TrackHeaders* headers() const { return m_headers; }
    TimeRuler* ruler() const { return m_ruler; }
    std::vector<int> selectedFrames() const;
    void setPlaying(bool playing);
    // Shown while the video's pictures are being read, empty when done.
    void setVideoStatus(const QString& text);

    // A button, or an item of one of the toolbars' menus, by its object name; nullptr if there is none.
    QAbstractButton* button(const QString& objectName) const;
    QAction* action(const QString& objectName) const;
    // Frames ("12f") or time ("0.50s") on the ruler, the frames' labels and the play range; kept in the settings.
    void setShowTime(bool asTime);
    bool showsTime() const;
    // Custom …'s question; tests answer it without a dialog.
    std::function<std::optional<double>(double now)> askSpeed;

signals:
    void settingsRequested();
    void previousRequested();
    void nextRequested();
    void playRequested();
    void newFrameRequested();
    void duplicateRequested();
    void holdSetRequested(int hold);
    void loopRequested(bool loop);
    void deleteRequested();
    void addSoundRequested();
    void addVideoRequested();
    void firstRequested();
    void lastRequested();
    void exportRequested();
    void hideRequested();
    void keepOnlyRequested();
    void duplicateTrackRequested(int track);
    // The maximise toggle: the timeline as tall as the window allows, or back to its height.
    void maximizeToggled(bool maximized);
    void removeSoundRequested(int id);
    void removeSoundsRequested(const std::vector<int>& ids);
    void addTrackRequested();
    void deleteTrackRequested(int track);
    void clearTrackRequested(int track);
    void trackHiddenRequested(int track, bool hidden);
    void trackLockedRequested(int track, bool locked);
    void clearFramesRequested();
    void soundEndRequested(int id, double seconds);
    void soundStartRequested(int id, double seconds);
    void selectionChanged();
    void refused(const QString& why);

protected:
    void resizeEvent(QResizeEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    QToolButton* makeToolButton(const QString& text, const char* name, const QString& tip);
    QAction* makeAction(QMenu* menu, const QString& text, const char* name);
    QAction* makeSwitch(QMenu* menu, const QString& text, const char* name, const QString& tip);
    QWidget* makeHeight();
    void setSpeed(double speed);
    void syncSwitches();
    QWidget* makeTopBar();
    QWidget* makeTimelineMenu();
    QWidget* makeFrameMenu();
    QWidget* makePlayButton();
    QWidget* makeSpeedMenu();
    QWidget* makeAddMenu();
    QWidget* makeMoreMenu();
    QWidget* makeTrackBar();
    QWidget* makeBottomBar();
    QWidget* makeTransport();
    QWidget* makeRange();
    QWidget* makeZoom();
    QWidget* makeVideoGroup();
    QWidget* sliderRow(const QString& label, QSlider* slider);
    static double wheelNotches(const QWheelEvent* event);
    bool zoomByWheel(QObject* over, QWheelEvent* event);
    void toggleCollapsed(int track);
    bool scrollLanes(QObject* over, QWheelEvent* event);
    QPushButton* makeButton(const QString& text, const char* name, const QString& tip);
    void syncButtons();
    void syncTrackBar();
    void syncRange();
    void syncVideoBar();
    void setPlayRange();
    void showFrameMenu(int frame, const QPoint& globalPos);
    void showSoundMenu(int id, const QPoint& globalPos);
    void showTrackMenu(int track, const QPoint& globalPos);
    void askTrackName(int track);
    template <typename T> void report(const T& result);
    void askHold();

    QPointer<Document> m_doc;
    FrameStrip* m_strip;
    TrackHeaders* m_headers;
    TimeRuler* m_ruler = nullptr;
    QScrollArea* m_scroll;
    QWidget* m_videoBar;
    QLabel* m_videoText;
    QLabel* m_videoStatus;
    QWidget* m_videoGroup = nullptr;
    QSlider* m_videoOpacity = nullptr;
    QToolButton* m_undo = nullptr;
    QToolButton* m_redo = nullptr;
    QToolButton* m_play = nullptr;      // the top bar's Play, with its menu
    QToolButton* m_playBottom = nullptr;
    QToolButton* m_speed = nullptr;
    QAction* m_duplicate = nullptr;
    QAction* m_holdFrame = nullptr;
    QAction* m_delete = nullptr;
    QAction* m_loop = nullptr;
    QAction* m_onion = nullptr;
    QAction* m_addVideo = nullptr;
    QList<QAction*> m_speeds;
    QAction* m_customSpeed = nullptr;
    QList<QAction*> m_switches;  // the menus' switch items (makeSwitch)
    QAction* m_showTime = nullptr;
    QSlider* m_onionStrength = nullptr;
    QSlider* m_laneHeight = nullptr;
    QSlider* m_zoom = nullptr;
    QWidget* m_side = nullptr;      // + Add Track
    QBoxLayout* m_bottomRows = nullptr;  // the bottom bar: its halves side by side, or one over the other
    QWidget* m_bottomLeft = nullptr;
    QWidget* m_bottomRight = nullptr;
    QList<QWidget*> m_secondary;    // hidden in a narrow timeline (resizeEvent)
    QWidget* m_trackBar = nullptr;
    QToolButton* m_trackEye = nullptr;
    QToolButton* m_trackLock = nullptr;
    QToolButton* m_trackDuplicate = nullptr;
    QToolButton* m_trackDelete = nullptr;
    QSpinBox* m_rangeStart = nullptr;
    QSpinBox* m_rangeEnd = nullptr;
    QToolButton* m_duration = nullptr;
    int m_opacityKey = 0;
    int m_volumeKey = 0;  // while a sound track's volume is dragged
};

}
