#pragma once

#include <QPointer>
#include <QWidget>

class QLabel;
class QToolButton;

namespace doodlebold {

class Document;

// In a canvas's title bar: the Timeline button, which shows or hides this canvas's timeline, and before it, while that is
// hidden and the canvas has frames, a chip with the frame shown ("Frame 09 / 16", in beats) and buttons for the previous and
// next frame.
class TimelineTitleButtons : public QWidget {
    Q_OBJECT
public:
    explicit TimelineTitleButtons(Document* doc, QWidget* parent = nullptr);

    Document* document() const { return m_doc.data(); }
    // Whether its canvas has its timeline open (the button is checked then).
    bool timelineShown() const;
    QToolButton* timelineButton() const { return m_timeline; }
    QWidget* frameChip() const { return m_chip; }
    QString frameText() const;

signals:
    void timelineToggled();
    void previousFrame();
    void nextFrame();
    void changed();  // its width changed: the title bar places it again

private:
    void sync();

    QPointer<Document> m_doc;
    QWidget* m_chip;
    QLabel* m_frame;
    QToolButton* m_timeline;
};

}
