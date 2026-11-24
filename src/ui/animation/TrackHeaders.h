#pragma once

#include "core/animation/Animation.h"

#include <QIcon>
#include <QPointer>
#include <QWidget>

#include <set>
#include <vector>

namespace doodlebold {

class Document;
class FrameStrip;

// The column left of the timeline's lanes, row for row with them as they scroll: each track's
// row (frames, sound or video), touching the next, the current one filled with the accent. Along it: a tile with what the
// track shows at the playhead (its frame's thumbnail; a sound's or the video's sign), its name, and on the right the
// lock (faint while unlocked), an eye that hides or shows it (a speaker that mutes a sound track, with its volume before
// it) and a chevron, › open, ⌄ folded to a thin row, that folds or opens it. Clicking a frames track's row makes it the
// current track; double-clicking renames it; right-clicking opens its menu; dragging a row up or down moves the track.
class TrackHeaders : public QWidget {
    Q_OBJECT
public:
    // px: room for the chevron, kind, a name of about a dozen letters, a volume bar and three buttons in one row. The old 156
    // held only the eye, lock and name.
    static constexpr int kWidth = 210;  // component metric: the track column's width

    TrackHeaders(FrameStrip* strip, QWidget* parent = nullptr);

    void setDocument(Document* doc);
    // The lanes' vertical scroll, so the rows stay beside them.
    void setOffset(int y);

    // A row and its parts, in this widget's coordinates; empty when the row isn't there.
    QRect trackRow(int track) const;
    QRect eyeRect(const QRect& row) const;
    QRect lockRect(const QRect& row) const;
    QRect chevronRect(const QRect& row) const;
    QRect thumbRect(const QRect& row) const;
    // A sound track's volume bar; empty for other tracks.
    QRect volumeRect(int track) const;
    int trackAt(const QPoint& pos) const;

    QSize sizeHint() const override;
    // The selected tracks' indices, bottom first (Ctrl+click and Shift+click add to it).
    std::vector<int> selectedTracks() const;

signals:
    void trackClicked(int track);
    void trackHiddenToggled(int track, bool hidden);
    void trackLockToggled(int track, bool locked);
    void renameTrackRequested(int track);
    void trackMenuRequested(int track, const QPoint& globalPos);
    void moveTrackRequested(int track, int to);
    void collapseToggled(int track);
    // While dragging, `dragging` is true; the release sends the last value with it false.
    void volumeChanged(int track, int volume, bool dragging);
    // Del with tracks selected (indices, bottom first).
    void deleteTracksRequested(const std::vector<int>& tracks);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    bool event(QEvent* event) override;

private:
    int trackCount() const;
    // Where a row dragged to y goes: the index of the track it is over (0: the bottom one).
    int dropIndex(int y) const;
    void paintRow(QPainter& p, int track);
    void paintChevron(QPainter& p, const QRect& r, bool collapsed, const QColor& ink) const;
    void paintThumb(QPainter& p, const QRect& r, int track, TrackKind kind) const;
    void paintKind(QPainter& p, const QRect& r, TrackKind kind) const;
    void paintSpeaker(QPainter& p, const QRect& r, bool muted, const QColor& ink) const;
    void paintVolume(QPainter& p, const QRect& r, int volume) const;
    int controlsTop(const QRect& row) const;
    int volumeAt(int track, int x) const;
    void selectByClick(int track, Qt::KeyboardModifiers mods);
    void pruneSelection();

    FrameStrip* m_strip;
    QPointer<Document> m_doc;
    int m_offset = 0;
    int m_pressTrack = -1;
    QPoint m_pressPos;
    bool m_dragging = false;
    int m_dropTrack = -1;
    int m_volumeTrack = -1;  // the track whose volume is being dragged
    std::set<int> m_selected;  // the selected tracks, by id
    int m_anchor = 0;          // the track Shift+click selects from, by id
    QIcon m_visible, m_hidden, m_lock;
};

}
