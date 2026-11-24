#pragma once

#include "core/animation/AnimationVideo.h"
#include "core/animation/VideoPictureLoader.h"
#include "core/ffmpeg/Ffmpeg.h"
#include "ui/animation/AnimationDialogs.h"
#include "ui/animation/SoundPlayback.h"

#include <QElapsedTimer>
#include <QObject>
#include <QPointer>
#include <QTimer>

#include <atomic>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <vector>
#include <optional>
#include <thread>

class QWidget;

namespace doodlebold {

class Document;
class TimelinePanel;

// Carries out the timeline's and the menus' animation commands on the current canvas: frames, holds, playback, sound clips,
// the video under the frames (reading its pictures with FFmpeg into a cache on disk), New from Video and Export Video. The
// questions go through protected virtuals, so tests can answer without dialogs.
class AnimationController : public QObject {
    Q_OBJECT
public:
    using ToolsSource = std::function<std::optional<FfmpegTools>(bool ask)>;

    AnimationController(TimelinePanel* panel, ToolsSource tools, QWidget* dialogParent, QObject* parent = nullptr);
    ~AnimationController() override;

    void setDocument(Document* doc);
    Document* document() const { return m_doc.data(); }
    TimelinePanel* panel() const { return m_panel; }

    // The frames the commands act on: the timeline's selection while it is shown, else the current frame.
    std::vector<int> targetFrames() const;
    // Whether the current canvas has its timeline open; each canvas keeps its own.
    bool timelineShown() const;
    void setTimelineShown(bool shown);
    void setTimelineShown(Document* doc, bool shown);

    void newFrame();
    void duplicateFrames(bool selection = true);
    void deleteFrames(bool selection = true);
    void changeHold(int delta);
    void addTrack();
    void deleteTrack(int track);
    void clearTrack(int track);
    void setTrackHidden(int track, bool hidden);
    void setHold(int hold);
    void setLoop(bool loop);
    void setSoundStart(int id, double seconds);
    void setSoundEnd(int id, double seconds);
    void setTrackLocked(int track, bool locked);
    void clearFrames();
    void clearSelected();
    bool keepOnlyThisFrame();
    void previousFrame();
    void nextFrame();
    void togglePlay();
    void stop();
    bool isPlaying() const { return m_playTimer.isActive(); }
    const std::vector<int>& playingSounds() const;  // the clips playback is playing
    const SoundPlayback& soundPlayback() const { return *m_sound; }
    bool addSound();
    bool addSoundFile(const QString& file, const QByteArray& bytes, double seconds);
    bool addVideo();
    bool addVideoFile(Document* doc, const QString& file);
    void removeVideo();
    bool newFromVideo();
    bool exportVideo();
    void showSettings();

    // The video's pictures for the current canvas, if it has a video and they're cached; for the timeline's video lane.
    VideoFrames* videoFrames(Document* doc);
    // A clip's waveform once FFmpeg has measured it (it starts measuring the first time it is asked); nullptr until then.
    const SoundPeaks* soundPeaks(const SoundClip& clip);
    void waitForPeaks();  // for tests: until every clip asked for is measured
    void waitForVideo();  // for tests: until the pictures are read

signals:
    void message(const QString& text);
    void refused(const QString& why);
    void timelineShownChanged(bool shown);
    void playingChanged(bool playing);
    void documentCreated(doodlebold::Document* doc);

protected:
    virtual bool confirmKeepOnly(int frameCount, int currentFrame);
    virtual QString askVideoFile();
    virtual std::optional<AddVideoChoice> askAddVideo(const QString& file, const VideoInfo& info, const Document& doc);
    virtual QString askSoundFile();
    virtual QString askExportPath(const QString& suggested);
    virtual void showError(const QString& title, const QString& text);

private:
    struct Reading;

    template <typename T> void report(const T& result);
    void playTick();
    void showBeat(int beat);
    void pictureReady(int index);
    QImage laneVideoPicture(int index);
    void syncVideo();
    void syncSound();
    void startReading(Document* doc);
    void pollReading();
    void forget(Document* doc);

    TimelinePanel* m_panel;
    ToolsSource m_tools;
    QPointer<QWidget> m_parent;
    QPointer<Document> m_doc;

    QTimer m_playTimer;
    QElapsedTimer m_playClock;
    int m_playStartBeat = 0;
    int m_beat = 0;
    std::unique_ptr<SoundPlayback> m_sound;
    QTimer m_prepareTimer;  // the clips are made ready to play a moment after they change, not at Play

    std::vector<QMetaObject::Connection> m_docConnections;
    std::set<Document*> m_known;  // documents whose end is watched
    std::map<Document*, std::unique_ptr<VideoFrames>> m_videos;
    std::unique_ptr<Reading> m_reading;
    std::unique_ptr<VideoPictureLoader> m_loader;  // the current canvas's video pictures, read ahead on a thread
    int m_wantedPicture = -1;
    std::map<std::shared_ptr<const QByteArray>, std::shared_ptr<const SoundPeaks>> m_peaks;  // empty peaks: unreadable
    std::vector<std::thread> m_measurers;
    int m_measuring = 0;
    QTimer m_readTimer;
};

}
