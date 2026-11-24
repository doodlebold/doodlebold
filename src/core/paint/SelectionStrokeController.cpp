#include "core/paint/SelectionStrokeController.h"

#include "core/stroke/TimingLog.h"

#include <QCoreApplication>
#include <QElapsedTimer>

#include <utility>

namespace doodlebold {

EditResult<QRect> SelectionStrokeController::begin(const InputSample& first, SelectionBrush brush, const BrushSettings& settings) {
    if (isActive() || m_doc->isPainting())
        return EditResult<QRect>::failure(EditError::StrokeInProgress);
    m_brush = brush;
    m_proxy = TiledImage(m_doc->size());
    m_mask = m_doc->selection() ? *m_doc->selection() : SelectionMask(m_doc->size());
    for (const auto& [coord, tile] : m_mask.tiles()) {
        Tile* t = m_proxy.writableTile(coord.x, coord.y);
        for (std::size_t i = 0; i < std::size_t(SelectionTile::kValues); ++i)
            for (std::size_t c = 0; c < 4; ++c)
                t->px[i * 4 + c] = tile->v[i];
    }
    BrushSettings s = settings;
    s.tool = brush == SelectionBrush::Pen ? ToolType::SelectionPen : ToolType::SelectionEraser;
    s.stabilizer = StabilizerLevel{0};  // the pens have none, so their path is straight from sample to sample
    // The Pen lays white, coverage as alpha; the Eraser a colour with no alpha, which erases on either engine.
    const QColor colour = brush == SelectionBrush::Pen ? QColor(Qt::white) : QColor(Qt::transparent);
    m_doc->setPainting(true);
    m_active = true;
    m_finishing = false;
    m_takenWhileCatchingUp = QRect();
    return sync(m_painter.begin(&m_proxy, colour, s, first));
}

void SelectionStrokeController::paintInBackground(std::function<void()> onFrame) {
    m_inBackground = true;
    // While a lift waits for the painting to catch up, each frame is taken into the mask as it comes, and reported with the rest.
    m_painter.paintInBackground(std::move(onFrame), [this] { m_takenWhileCatchingUp |= takeFrame(); });
}

QRect SelectionStrokeController::add(const InputSample& sample) {
    if (!isActive() || m_finishing)
        return {};
    return sync(m_painter.add(sample));
}

// The painter's newest frame into the proxy, then into the mask.
QRect SelectionStrokeController::takeFrame() {
    QElapsedTimer timed;
    timed.start();
    const QRect area = sync(m_painter.showNow());
    timing_log::note("selection take", timed.nsecsElapsed() / 1000);
    return area;
}

QRect SelectionStrokeController::takeChanges() {
    if (!isActive() || !m_inBackground)
        return {};
    return std::exchange(m_takenWhileCatchingUp, QRect()) | takeFrame();
}

QRect SelectionStrokeController::catchUp() {
    if (!isActive() || !m_inBackground)
        return {};
    QRect area;
    do {  // a frame published after the wait is taken too
        m_painter.catchUp();
        area |= takeChanges();
    } while (!m_painter.isPainted() || m_painter.hasUnshown());
    return area;
}

void SelectionStrokeController::finishInBackground() {
    if (!isActive() || m_finishing)
        return;
    if (!m_inBackground) {
        finish();
        return;
    }
    m_finishing = true;
    m_painter.liftInBackground();
}

QRect SelectionStrokeController::finish() {
    if (!isActive())
        return {};
    QRect last;
    if (m_inBackground) {
        if (!m_finishing)
            m_painter.liftInBackground();
        last = catchUp();
    } else {
        last = sync(m_painter.lift());
    }
    end(true);
    return last;
}

void SelectionStrokeController::cancel() {
    end(false);
}

QRect SelectionStrokeController::sync(const QRect& dirty) {
    const QRect tiles = m_proxy.tileRange(dirty.intersected(m_proxy.rect()));  // a selection ends at the canvas
    for (int ty = tiles.top(); !tiles.isEmpty() && ty <= tiles.bottom(); ++ty)
        for (int tx = tiles.left(); tx <= tiles.right(); ++tx) {
            const Tile* t = m_proxy.tile(tx, ty);
            if (!t) {
                m_mask.setTile(tx, ty, nullptr);
                continue;
            }
            SelectionTile* o = m_mask.writableTile(tx, ty);
            for (std::size_t i = 0; i < std::size_t(SelectionTile::kValues); ++i)
                o->v[i] = t->px[i * 4 + 3];
        }
    return dirty;
}

void SelectionStrokeController::end(bool keep) {
    if (!isActive())
        return;
    if (keep)
        m_painter.end();  // finish() has taken the stroke's last paint into the mask
    else
        m_painter.abandon();
    m_active = false;
    m_finishing = false;
    m_doc->setPainting(false);
    if (keep) {
        m_mask.squeeze();
        const QString text = m_brush == SelectionBrush::Pen ? QCoreApplication::translate("SelectionStrokeController", "Selection Pen")
                                                            : QCoreApplication::translate("SelectionStrokeController", "Selection Eraser");
        (void)m_doc->setSelection(m_mask, selection::CombineMode::Replace, text);
    }
    m_proxy = TiledImage();
}

}
