#pragma once

#include "core/brush/BrushSettings.h"

#include <QObject>
#include <QPointer>
#include <QSize>

#include <optional>

class QMessageBox;
class QWidget;

namespace doodlebold {

class Document;
class ToolBox;

// What the user is told when a stroke doesn't fit in memory (the decision of 2026-11-08: "The dialogue should appear
// once then limit the brushes"). The first time a brush is limited as a stroke starts, or a stroke stops partway, a notice explains
// it once the stroke ends, and never again. After that brushes are kept within memory silently. With brushes not kept within it, a
// stroke that runs out still stops partway, and a note in the corner says so each time.
class MemoryNotice : public QObject {
    Q_OBJECT
public:
    // What happened to the stroke, sizes in tenths of a pixel.
    struct Event {
        bool stopped = false;  // it ran out partway; otherwise its brush was kept to `used` as it started
        int chosen = 0;
        int used = 0;
        ToolType tool = ToolType::Brush;
        int largestThatFits = 0;  // on that canvas, for that tool, now
    };

    explicit MemoryNotice(ToolBox* tools, QObject* parent = nullptr);

    // Listens to `doc`'s strokes; nullptr stops listening.
    void watch(Document* doc);

    // Whether the notice has been shown, ever (kept in the settings).
    static bool wasShown();
    static void setShown(bool shown);

    static QString title();
    static QString body(const Event& event);
    // The note shown, every time, for a stroke that stopped with brushes not kept within memory.
    static QString stoppedNote();

    // The notice as a message box over `parent`: "Open Performance options" and OK, the default.
    static QMessageBox* makeBox(const Event& event, QWidget* parent);

signals:
    // The notice is due: the stroke that needed it has ended, and it has never been shown. The receiver shows it.
    void noticeWanted(const Event& event);
    void noteWanted(const QString& text);

private:
    void heard(bool stopped, int chosen, int used);
    void strokeEnded();

    QPointer<ToolBox> m_tools;
    QPointer<Document> m_doc;
    std::optional<Event> m_pending;
    QSize m_canvas;
};

}
