#pragma once

#include "core/animation/Animation.h"
#include "core/animation/AnimationOps.h"

#include "ui/Theme.h"

#include <QImage>
#include <QPointer>
#include <QRect>
#include <QTimer>
#include <QWidget>

#include <functional>
#include <map>
#include <optional>
#include <set>
#include <thread>
#include <vector>

namespace doodlebold {

class Document;
struct SoundPeaks;

// The timeline's lanes, drawn in one widget inside a scroll area: one lane per track, the top track on top. A frames track's
// frames are cells as wide as their beats, with a thumbnail and their beat numbers under them; a sound track's lane holds its
// clips; the video track's frames are cells showing their video pictures. The playhead's beat is lit across the lanes; past a
// track's end a faint bar shows that its last frame stays.
// - Click a frame (a video frame too) to go to it (its track becomes the current one); Ctrl+click adds or removes a frame of
//   any track, Shift+click selects up to it along its track; Ctrl+A selects every frame of the current track.
// - Drag from anywhere but a frame or clip (the margins, the beat numbers, between the lanes) for a selection box: it picks
//   every frame and clip it touches, on every track. Drag one of the selected frames or clips to move them all: each track's
//   frames to the same place along it, the clips by the same beats.
// - Drag a frame's right edge to hold it for more or fewer beats.
// - Click a sound clip to select it; Ctrl+click adds or removes one, Shift+click adds one. Drag a clip to move it (onto another
//   sound track too); drag a clip's ends to trim it. Del removes the selected clips; with none, it clears the selected
//   frames' drawings (a video frame's picture too). Space plays or stops. Right-click a frame or clip for its menu.
// Only what is on screen is painted, and frames' places are kept between edits, so thousands of frames stay quick.
class FrameStrip : public QWidget {
    Q_OBJECT
public:
    static constexpr int kMargin = Theme::kSpace3;
    static constexpr int kGap = Theme::kSpace2;
    static constexpr int kLaneGap = Theme::kSpace4;
    static constexpr int kTrackGap = Theme::kSpace2;
    // A frame's cell at full height, px. The redesign asked for larger previews: 76, about a third larger than the
    // 58 it was, then 64 after feedback found "alot of empty space"; still larger than before. The Height slider makes it
    // smaller.
    static constexpr int kCellHeight = 64;  // component metric: a frame's cell at full height
    static constexpr int kLabelHeight = 16;  // px, the beats' labels under the frames (20 before the compacting) (component metric)
    static constexpr int kVideoLaneHeight = 44;  // a sound or video track's lane (component metric)
    static constexpr int kClipInset = Theme::kSpace1;  // a clip's or video picture's space from its lane's edges
    static constexpr int kEdgeGrip = 7;  // component metric: the trim handle's width
    static constexpr int kMinBeatWidth = 8;  // component metric: the narrowest zoom
    // Where the zoom and the tracks' height start, at every start of Doodlebold (they aren't kept): a design choice,
    // timeline-default-zoom-height.png, read at its 125 % display scale: cells 73 device px apart (58 px a beat, labels
    // every 2 beats, 0.17 s at 12 fps) and about 52 tall (41.6 px: 65 % of kCellHeight, short enough that the beats' labels
    // under the frames are left out, as in the picture); its sliders' handles agree, Zoom about half way, Height near 65.
    static constexpr int kDefaultBeatWidth = 58;  // component metric: the starting zoom
    static constexpr int kDefaultLaneScale = 65;
    // The widest a beat's cell gets when zoomed in, px: about as wide as a full-height cell (kCellHeight) shows a 4:3 canvas
    // whole. It was 82 for the 58 px cells.
    static constexpr int kMaxBeatWidth = 104;  // component metric: the widest zoom
    // A folded track's lane, px (TimelineView::collapsed): its frames and clips as thin bars, still clickable, so many
    // tracks fit. Chosen by eye: a quarter of a full frames lane, as tall as a line of the headers' smaller signs (14).
    static constexpr int kCollapsedLaneHeight = 14;  // component metric: a folded lane
    // How near, px, a dragged clip's edge, a trimmed end or a held frame's end comes to a frame's start, a clip's edge or the
    // playhead before it jumps onto it. Chosen so that at the narrowest zoom (8 px a beat) it reaches a beat either side;
    // zoomed in, beats are wider than this and a move goes by whole beats as before.
    static constexpr int kSnapDistance = 10;
    static constexpr int kThumbDelayMs = 250;

    using PeaksSource = std::function<const SoundPeaks*(const SoundClip& clip)>;

    // A frame of a track.
    struct Cell {
        int track = -1;
        int frame = -1;
        bool valid() const { return track >= 0 && frame >= 0; }
    };

    explicit FrameStrip(QWidget* parent = nullptr);
    ~FrameStrip() override;

    void setDocument(Document* doc);
    Document* document() const { return m_doc.data(); }
    // A video picture for the video track's cells, from the timeline's cache, kVideoLaneHeight * 2 high (a null image: none, or
    // not read yet).
    void setVideoPictures(std::function<QImage(int index)> pictures);
    // A clip's waveform, when it has been measured (nullptr: not yet, or it can't be).
    void setSoundPeaks(PeaksSource peaks);
    // The video's pictures have been read (or the video changed): the lane asks for them again.
    void videoPicturesChanged();

    int beatWidth() const { return m_beatWidth; }
    // The lanes' heights in percent of their full size (kMinLaneScale … 100): smaller shows more tracks.
    static constexpr int kMinLaneScale = 35;
    int laneScale() const { return m_laneScale; }
    void setLaneScale(int percent);
    int cellHeight() const;
    int labelHeight() const;
    int mediaHeight() const;
    int xOfBeat(int beat) const { return kMargin + beat * m_beatWidth; }
    void setBeatWidth(int px);
    // The beat width that shows every beat in `width` pixels.
    int fitBeatWidth(int width) const;

    // The selected frames of the current track (indices, in order); never empty while there is a document: the current frame
    // at least.
    std::vector<int> selectedFrames() const;
    // Every selected frame, of every track, the lowest track's first.
    std::vector<Cell> selectedCells() const;
    void selectFrames(const std::vector<int>& frames);  // of the current track
    void selectAllFrames();
    // Whether the timeline was used last: a press of the mouse, pen or a finger in `scope` (the timeline), or Ctrl+A there, since
    // the last one anywhere else. Then Delete is the timeline's, even when the canvas has taken the keyboard since. Presses
    // in a menu or dialog leave it as it was: they belong to what opened them. Hiding the timeline or showing another canvas
    // ends it.
    bool selectionActive() const { return m_selectionActive; }
    void setSelectionScope(QWidget* scope) { m_scope = scope; }
    // The selected sound clips' ids, in the order of the clips; empty for none.
    std::vector<int> selectedSounds() const;
    void selectSounds(std::set<int> ids);
    void selectSound(int id) { selectSounds(id ? std::set<int>{id} : std::set<int>{}); }  // that one alone; 0 for none

    // The rows' places, for the track headers beside the lanes. A track's slot is its lane and half the gap on either side,
    // so the slots touch.
    int laneTop(int track) const;
    // Where a dragged edge at `beat` goes: onto the nearest frame start (of any track), clip edge or the playhead within
    // kSnapDistance; else where it is. The edges of `skipClips` and the frames of `skipTrack` don't count (they move with the
    // drag).
    int snapBeat(int beat, const std::set<int>& skipClips = {}, int skipTrack = -1) const;
    // The beat an edge at `beat` (in beats, a fraction for a trimmed end) snaps onto; none when nothing is near.
    std::optional<int> snapTarget(double beat, const std::set<int>& skipClips = {}, int skipTrack = -1) const;
    int laneHeight(int track) const;
    QRect slotRect(int track) const;
    int trackAtY(int y) const;  // the track whose slot is at y; -1 if none
    QRect cellRect(int track, int frame) const;
    int cellHeightOf(int track) const;  // a frames track's cells are taller than the video's
    bool collapsed(int track) const;    // folded to a thin lane (TimelineView::collapsed)
    // The beats' labels under the frames as frames ("12f") or time ("0.50s").
    void setShowTime(bool asTime);
    // The thumbnail already made of what a track shows at the playhead (its frame's, or the video's picture); a null image
    // when there is none yet. For the track headers' rows.
    QImage shownThumbnail(int track) const;
    QRect cellRect(int frame) const;  // the current track's
    QRect labelRect(int track, int frame) const;
    QRect labelRect(int frame) const;
    QRect soundRect(int id) const;
    QRect videoLaneRect() const;  // the video track's lane; empty without a video
    Cell cellAt(const QPoint& pos) const;
    int frameAt(const QPoint& pos) const { return cellAt(pos).frame; }
    bool onRightEdge(int track, int frame, const QPoint& pos) const;
    bool onRightEdge(int frame, const QPoint& pos) const;  // the current track's
    int soundAt(const QPoint& pos) const;
    // Where a track's frames dropped at x go: an index among the frames that stay when `moving` are taken out.
    int dropIndex(int track, int x, const std::vector<int>& moving) const;
    int dropIndex(int x, const std::vector<int>& moving) const { return dropIndex(currentTrack(), x, moving); }
    // Sizes the strip to its lanes, at least as large as the scroll area's view.
    void relayout();
    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

signals:
    void selectionChanged();
    void soundSelectionChanged();
    void layoutChanged();  // rows moved: the track headers follow
    void thumbnailsChanged();
    void frameMenuRequested(int frame, const QPoint& globalPos);
    void soundMenuRequested(int id, const QPoint& globalPos);
    void removeSoundsRequested(const std::vector<int>& ids);
    void playRequested();
    void clearFramesRequested();
    void refused(const QString& why);

protected:
    bool event(QEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;
    void hideEvent(QHideEvent* event) override;

private:
    enum class Drag {
        None,
        Pending,
        Box,
        Move,
        Hold,
        Sound,
        Sounds,
        TrimStart,
        TrimEnd
    };
    enum class SoundPart {
        None,
        Body,
        Start,
        End
    };

    int trackCount() const;
    int currentTrack() const;
    bool hasCells(int track) const;  // a frames track or the video's
    const std::vector<int>& starts(int track) const;
    TrackKind kindOf(int track) const;
    void placeFrames();
    // A track's frames whose cells reach into x0 … x1, as [first, last]; first > last when none do.
    std::pair<int, int> framesBetween(int track, int x0, int x1) const;
    void pruneSelection();
    void clickFrame(Cell cell, Qt::KeyboardModifiers mods);
    void pressFrame(Cell cell, Qt::KeyboardModifiers mods);
    void unpickFrames();
    void makeTrackCurrent(int track);
    SoundPart soundPartAt(int id, const QPoint& pos) const;
    const SoundClip* sound(int id) const;
    void pressSound(int id, SoundPart part, Qt::KeyboardModifiers mods);
    void noteUse(QWidget* w);
    std::vector<int> soundTrackIds() const;  // bottom first
    int groupTrackShift(const QPoint& pos, const std::vector<int>& ids);
    // Whether the selection is more than the one frame or clip pressed: then a drag moves all of it.
    bool groupChosen() const;
    void startClipGroup();
    // Where each track's selected frames go when the drag has gone `dx` from where it started.
    std::vector<animation_ops::FrameMove> frameMoves(int dx) const;
    void finishMove(const QPoint& pos);
    void updateHold(const QPoint& pos);
    int snapClipStart(const SoundClip& clip, int start, const std::set<int>& moving) const;
    void updateSound(const QPoint& pos);
    void updateSounds(const QPoint& pos);
    void updateTrim(const QPoint& pos);
    void updateBox(const QPoint& pos);
    void updateCursor(const QPoint& pos);

    void paintSoundLane(QPainter& p, int track, const QRect& exposed);
    void paintLane(QPainter& p, int track, const QRect& exposed);
    void paintLabelTint(QPainter& p, int track);
    void paintCell(QPainter& p, int track, int frame);
    void paintLabels(QPainter& p, int track, const QRect& exposed);
    void paintCollapsedCell(QPainter& p, const QRect& r, bool current, bool selected);
    void paintHeldEnd(QPainter& p, int track);
    void paintPlayhead(QPainter& p);
    void paintSound(QPainter& p, const SoundClip& clip, const QRect& exposed);
    void paintWaveform(QPainter& p, const SoundClip& clip, const QRect& body, const QRect& exposed, const QColor& ink);
    void paintDropMark(QPainter& p);
    void paintLoading(QPainter& p, const QRect& r);
    QImage thumbnail(int track, int frame);
    QImage videoThumbnail(int index);
    void makeNextThumbnail();
    void makeThumbnailAway(int id, Cell cell);
    void thumbnailMade(int id, Cell cell, quint64 key, const QImage& image, int generation);
    quint64 contentKey(int track, int frame) const;

    QPointer<Document> m_doc;
    std::function<QImage(int)> m_videoPictures;
    PeaksSource m_peaks;
    int m_beatWidth = kDefaultBeatWidth;
    int m_laneScale = kDefaultLaneScale;
    bool m_showTime = false;
    std::vector<std::vector<int>> m_starts;  // per track, each frame's first beat, and the total at the end
    std::vector<int> m_laneTops;             // per track
    std::set<int> m_selected;                // frame ids, of any track
    // Whether the frames selected were picked (a click, Ctrl+click or a box), not only the current frame kept selected after a
    // click on a clip: picked ones move with the clips.
    bool m_framesPicked = true;
    int m_anchor = 0;                        // frame id Shift+click ranges from
    std::set<int> m_selectedSounds;
    bool m_selectionActive = false;
    QPointer<QWidget> m_scope;

    Drag m_drag = Drag::None;
    QPoint m_pressPos;
    QPoint m_dragPos;
    Cell m_dragCell;
    int m_dragSound = 0;
    std::vector<animation_ops::SoundPlace> m_dragGroup;  // the selected clips where a group drag started
    bool m_dragFrames = false;                           // the selected frames go with the drag
    int m_dragTrackShift = 0;                            // sound tracks up (+) or down the group was dragged
    int m_dragStartBeat = 0;
    double m_dragTrimStart = 0.0;
    double m_dragTrimEnd = 0.0;
    int m_mergeKey = 0;
    std::set<int> m_boxBase;        // the frames selected before a Ctrl+box
    std::set<int> m_boxBaseSounds;  // and the clips

    struct Thumb {
        quint64 key = 0;
        QImage image;
    };
    std::map<int, Thumb> m_thumbs;     // by frame id
    std::vector<Cell> m_wanted;        // cells whose thumbnail is out of date, painted last
    std::map<int, QImage> m_videoThumbs;          // by picture, the lane's height
    std::optional<VideoReference> m_thumbsVideo;  // the video they are of
    std::vector<int> m_wantedVideo;    // pictures not read yet
    std::set<int> m_wantedSet;         // their frame ids
    QTimer m_thumbTimer;
    QTimer m_spinTimer;
    // A frame's thumbnail is made on a thread of its own, one at a time; one made for a document no longer shown is let go.
    std::thread m_thumbWorker;
    bool m_makingThumb = false;
    int m_thumbGeneration = 0;
};

}
