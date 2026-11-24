#pragma once

#include "core/document/EditError.h"
#include "core/effects/EffectJob.h"
#include "core/layers/LayerId.h"

#include <QRect>

#include <chrono>
#include <memory>
#include <vector>

namespace doodlebold {

class Document;

// An effect on the current layer, from its dialog opening to OK or Cancel. While Preview is on the layer shows the effect as
// the job finishes it, tile by tile; the layer itself holds the preview, so the canvas, the Navigator and the layer's
// thumbnail all show it. commit() keeps the result as one undo step; cancel(), or destroying the session uncommitted, puts
// the layer back as it was. The document must outlive the session and must not be edited meanwhile (the dialog is modal).
class EffectSession {
public:
    // Refused where painting is: a folder, a text layer, a locked or hidden layer.
    static EditResult<std::shared_ptr<EffectSession>> begin(Document& doc, const EffectSettings& settings);
    ~EffectSession();
    EffectSession(const EffectSession&) = delete;
    EffectSession& operator=(const EffectSession&) = delete;

    const EffectSettings& settings() const { return m_settings; }
    // New values from the dialog: a job for them starts at once while previewing, and the old one stops.
    void setSettings(const EffectSettings& settings);
    // Off, the layer shows its old pixels and nothing is worked out until OK.
    void setPreview(bool on);
    // The part of the canvas on screen, which a preview fills first.
    void setFocus(const QRect& focus) { m_focus = focus; }

    // Starts the job for the current settings, if it isn't running.
    void compute();
    // Puts on the layer what the job has finished since the last call (while previewing). Call it often, from a timer.
    // Returns isComplete().
    bool update();
    // The job for the current settings has finished every tile.
    bool isComplete() const { return m_complete; }
    // 0 … 1.
    double progress() const;
    // Blocks until the result is complete or the timeout passes, computing it if needed; returns isComplete().
    bool waitForResult(std::chrono::milliseconds timeout);

    // Needs the complete result. One undo step named after the effect; NoChange when no pixel changed (none is recorded).
    EditResult<> commit();
    // The layer back as it was.
    void cancel();
    // The document is gone: stop without touching it.
    void abandon();

private:
    EffectSession(Document& doc, LayerId layer, std::shared_ptr<const EffectSource> source, const EffectSettings& settings);
    void retireJob();
    void show(TileMap& changes, TileCoord c, const TilePtr& pixels);
    void apply(const TileMap& changes);
    void showAll();
    void restoreAll();

    Document& m_doc;
    LayerId m_layer;
    std::shared_ptr<const EffectSource> m_source;
    EffectSettings m_settings;
    bool m_preview = true;
    bool m_done = false;      // committed or cancelled
    bool m_complete = false;  // every tile of the current job is in m_results
    QRect m_focus;
    std::unique_ptr<EffectJob> m_job;
    std::vector<std::unique_ptr<EffectJob>> m_retired;  // cancelled jobs still stopping, so nothing waits for them
    TileMap m_results;  // the current job's finished tiles that differ from the layer's
    TileMap m_shown;    // what the layer shows that differs from its old pixels
};

}
