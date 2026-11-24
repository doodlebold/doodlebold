#include "core/effects/EffectSession.h"

#include "core/document/Document.h"

#include <algorithm>

namespace doodlebold {

EditResult<std::shared_ptr<EffectSession>> EffectSession::begin(Document& doc, const EffectSettings& settings) {
    using R = EditResult<std::shared_ptr<EffectSession>>;
    const LayerId id = doc.currentLayerId();
    const EditResult<TiledImage*> target = doc.paintTarget(id);
    if (!target)
        return R::failure(target.error());
    auto source = std::make_shared<EffectSource>();
    source->pixels = *target.value();
    source->selection = doc.selection();
    source->keepAlpha = doc.layer(id)->props.preserveOpacity;
    source->canvas = doc.size();
    return std::shared_ptr<EffectSession>(new EffectSession(doc, id, std::move(source), settings));
}

EffectSession::EffectSession(Document& doc, LayerId layer, std::shared_ptr<const EffectSource> source, const EffectSettings& settings)
    : m_doc(doc), m_layer(layer), m_source(std::move(source)), m_settings(settings) {}

EffectSession::~EffectSession() {
    if (!m_done)
        cancel();
}

void EffectSession::setSettings(const EffectSettings& settings) {
    if (settings == m_settings)
        return;
    m_settings = settings;
    retireJob();
    if (m_preview)
        compute();
}

void EffectSession::setPreview(bool on) {
    if (on == m_preview)
        return;
    m_preview = on;
    if (on) {
        showAll();
        compute();
    } else {
        restoreAll();
    }
}

void EffectSession::compute() {
    if (!m_job && !m_done)
        m_job = std::make_unique<EffectJob>(m_source, m_settings, m_focus);
}

// The old job stops in its own time; the tiles it showed stay until the new job's replace them, so the canvas never flashes
// back to the old pixels between two slider positions.
void EffectSession::retireJob() {
    if (m_job) {
        m_job->cancel();
        m_retired.push_back(std::move(m_job));
    }
    m_results.clear();
    m_complete = false;
}

void EffectSession::show(TileMap& changes, TileCoord c, const TilePtr& pixels) {
    const TilePtr old = m_source->pixels.tileShared(c.x, c.y);
    const auto shown = m_shown.find(c);
    const TilePtr current = shown == m_shown.end() ? old : shown->second;
    if (pixels == current)
        return;
    changes[c] = pixels;
    if (pixels == old)
        m_shown.erase(c);
    else
        m_shown[c] = pixels;
}

void EffectSession::apply(const TileMap& changes) {
    if (!changes.empty())
        m_doc.restoreTiles(m_layer, changes);
}

bool EffectSession::update() {
    std::erase_if(m_retired, [](const std::unique_ptr<EffectJob>& j) { return j->isFinished(); });
    if (!m_job)
        return false;
    const bool finished = m_job->isFinished();  // before take(): every tile taken after it is in
    TileMap changes;
    for (const EffectTile& t : m_job->take()) {
        if (t.changed)
            m_results[t.coord] = t.pixels;
        if (m_preview)
            show(changes, t.coord, t.pixels);
    }
    apply(changes);
    m_complete = finished && !m_job->isCancelled();
    if (m_complete && m_preview)
        showAll();  // also puts back the tiles an earlier job changed and this one doesn't
    return m_complete;
}

double EffectSession::progress() const {
    return m_job ? m_job->progress() : 0.0;
}

bool EffectSession::waitForResult(std::chrono::milliseconds timeout) {
    compute();
    if (m_job)
        m_job->wait(timeout);
    return update();
}

// The layer shows the current job's results and its old pixels everywhere else.
void EffectSession::showAll() {
    TileMap changes;
    const TileMap shown = m_shown;
    for (const auto& [c, pixels] : shown)
        if (!m_results.contains(c))
            show(changes, c, m_source->pixels.tileShared(c.x, c.y));
    for (const auto& [c, pixels] : m_results)
        show(changes, c, pixels);
    apply(changes);
}

void EffectSession::restoreAll() {
    TileMap changes;
    const TileMap shown = m_shown;
    for (const auto& [c, pixels] : shown)
        show(changes, c, m_source->pixels.tileShared(c.x, c.y));
    apply(changes);
}

EditResult<> EffectSession::commit() {
    if (m_done || !update())
        return EditResult<>::failure(EditError::NoChange);
    showAll();
    m_done = true;
    if (m_shown.empty())
        return EditResult<>::failure(EditError::NoChange);
    TileMap before;
    for (const auto& [c, pixels] : m_shown)
        before[c] = m_source->pixels.tileShared(c.x, c.y);
    m_doc.commitPixelEdit(m_layer, before, effectTitle(m_settings.kind));
    return {};
}

void EffectSession::cancel() {
    if (m_job)
        m_job->cancel();
    restoreAll();
    m_done = true;
}

}

namespace doodlebold {

void EffectSession::abandon() {
    if (m_job)
        m_job->cancel();
    m_done = true;
}

}
