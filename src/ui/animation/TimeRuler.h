#pragma once

#include <QPointer>
#include <QWidget>

class QPainter;

namespace doodlebold {

class Document;
class FrameStrip;

// Over the timeline's lanes, scrolling with them: the beats counted from 0 ("0f, 6f, 12f …")
// with a long tick at each label, a middle one at each second and a short one at each beat, and the playhead: a chip with
// its beat ("12f") over a line that the lanes carry on. Clicking or dragging on it moves the playhead. The labels come every
// few beats, a step that divides a second (or a number of seconds), wider as the frames are zoomed out.
class TimeRuler : public QWidget {
    Q_OBJECT
public:
    // px: the labels' line over the ticks, with the playhead's chip on the labels' line. It was 26, with a marker under the
    // ticks that was hard to find (the redesign: "a more visible playhead"), then 32, then 28 when less empty
    // space was asked for.
    static constexpr int kHeight = 28;  // component metric: the ruler's height
    static constexpr int kLabelSpacing = 72;  // the least room between two labels (component metric)

    TimeRuler(FrameStrip* strip, QWidget* parent = nullptr);

    void setDocument(Document* doc);
    void setOffset(int x);  // the lanes' horizontal scroll

    // The beats between the ruler's labels: the least that leaves kLabelSpacing between them, among the steps that divide
    // a second of `perSecond` beats, then whole seconds.
    static int beatLabelStep(int beatWidth, int perSecond);
    // A beat as the ruler writes it: "12f", or as time passed at `fps`, "0.50s" (TimelinePanel::setShowTime).
    static QString beatLabel(int beat);
    static QString beatLabel(int beat, double fps, bool asTime);
    void setShowTime(bool asTime);
    int beatAt(int x) const;
    int xOfBeat(int beat) const;

    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;

private:
    void scrub(int x);
    void paintPlayhead(QPainter& p);
    QString label(int beat) const;
    bool m_showTime = false;
    QRect chipRect() const;  // the playhead's chip

    FrameStrip* m_strip;
    QPointer<Document> m_doc;
    int m_offset = 0;
};

}
