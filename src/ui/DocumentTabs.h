#pragma once

#include "ui/timelapse/RecordingPill.h"
#include "ui/timelapse/TimelapseReplayView.h"

#include <QPoint>
#include <QPointer>
#include <QTimer>
#include <QWidget>

#include <functional>
#include <vector>

class QLabel;
class QMdiArea;
class QSplitter;
class QMdiSubWindow;

namespace doodlebold {

class TimelineTitleButtons;
class CanvasView;
class Document;
class TabletStats;
class ViewSelector;

class DocumentTabs : public QWidget {
    Q_OBJECT
public:
    static constexpr int kCascadeStep = 26;
    // A canvas window is never smaller than its title bar with the title, its buttons and a little canvas.
    static constexpr int kMinimumTitleWidth = 100;  // component metric: a canvas title's least width
    static constexpr int kMinimumCanvasHeight = 60;  // component metric: a canvas window's least height
    static QSize minimumWindowSize();

    DocumentTabs(TabletStats* stats, QWidget* parent = nullptr);
    ~DocumentTabs() override;

    // The gap between the top and the bottom of the area and a canvas card (the frame's invisible margin counts: the
    // card is drawn `kFrameMargin` px inside its window), so that the card lines up with the panels' cards.
    void setOuterGap(int gap);

    CanvasView* addDocument(Document* doc);

    int count() const { return static_cast<int>(m_entries.size()); }
    int currentIndex() const;
    void setCurrentIndex(int index);
    int indexOf(const CanvasView* view) const;
    CanvasView* canvasAt(int index) const;
    QMdiSubWindow* windowAt(int index) const;
    CanvasView* currentCanvas() const;
    Document* currentDocument() const;
    QMdiArea* area() const { return m_area; }
    ViewSelector* viewSelector() const { return m_selector; }

    enum class UnsavedChoice {
        Save,
        Discard,
        Cancel
    };
    // Without a handler Save is refused.
    using SaveHandler = std::function<bool(Document*)>;
    void setSaveHandler(SaveHandler handler) { m_save = std::move(handler); }

    bool requestCloseTab(int index);
    bool requestCloseAll();
    void selectNextTab();
    void selectPreviousTab();

    RecordingPill* pillOf(const Document* doc) const;
    TimelineTitleButtons* timelineButtonsOf(const Document* doc) const;
    // The timeline, under the canvases and above the view selector, with a handle between them to make it taller or shorter;
    // shown or hidden by setTimelineShown (for the current canvas).
    void setTimelinePanel(QWidget* panel);
    QWidget* timelinePanel() const { return m_timelinePanel; }
    void setTimelineShown(bool shown);
    int timelineHeight() const;
    void setTimelineHeight(int height);
    bool timelineShown() const { return m_timelineShown; }
    enum class TimelineCommand {
        Toggle,
        Previous,
        Next
    };

    TimelapseReplayView* showReplay(Document* doc, std::shared_ptr<const Timelapse> recording);
    void endReplay(Document* doc);
    TimelapseReplayView* replayOf(const Document* doc) const;
    QLabel* badgeOf(const Document* doc) const;

    static QString windowTitleFor(const Document* doc, double zoom);
    static QString nameOf(const Document* doc);
    static bool isModified(const Document* doc);
    static QString titleTextOf(const Document* doc);

signals:
    void currentDocumentChanged(doodlebold::Document* doc);
    void recordingCommand(doodlebold::Document* doc, doodlebold::RecordingPill::Command command);
    void timelineCommand(doodlebold::Document* doc, doodlebold::DocumentTabs::TimelineCommand command);
    void timelineResized(int height);  // by its handle
    void documentTitleChanged();

protected:
    // Asks what to do with a modified document. Virtual so tests can answer without a dialog.
    virtual UnsavedChoice askUnsaved(const Document* doc);
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    struct Entry {
        CanvasView* view = nullptr;
        QMdiSubWindow* window = nullptr;
        RecordingPill* pill = nullptr;
        QPointer<TimelapseReplayView> replay{};
        QLabel* badge = nullptr;
        TimelineTitleButtons* timeline = nullptr;
    };

    void addView(CanvasView* view, const QSize& canvas, Document* recorded);
    void watchDocument(CanvasView* view, Document* doc);
    Entry* entryOf(const CanvasView* view);
    const Entry* entryOf(const CanvasView* view) const;
    void updateHeader(Entry& entry);
    void placePill(const Entry& entry);
    void placeReplay(const Entry& entry);
    void announceCurrent();
    void refresh(CanvasView* view);
    void refreshIcons();
    void rebuildSelector();
    void onWindowActivated(QMdiSubWindow* window);
    void place(QMdiSubWindow* window, const QSize& canvas);
    void keepInside(QMdiSubWindow* window);
    int indexOfWindow(const QMdiSubWindow* window) const;

    TabletStats* m_stats;
    QMdiArea* m_area;
    ViewSelector* m_selector;
    std::vector<Entry> m_entries;
    SaveHandler m_save;
    QPointer<Document> m_lastCurrent;
    QPoint m_cascade{-1, -1};
    bool m_maximized = true;
    QTimer m_iconTimer;
    QWidget* m_timelinePanel = nullptr;
    QSplitter* m_timelineSplitter = nullptr;
    int m_timelineHeight = 300;
    bool m_timelineShown = false;
};

}
