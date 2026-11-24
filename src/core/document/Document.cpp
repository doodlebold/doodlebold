#include "core/document/Document.h"

#include "core/animation/AnimationOps.h"

#include "core/document/DocumentCommands.h"
#include "core/document/LayerOps.h"
#include "core/document/SelectionEdits.h"
#include "core/document/TransformEdits.h"
#include "core/layers/Compositor.h"
#include "core/timelapse/TimelapseRecorder.h"

#include <QFileInfo>
#include <QPainter>
#include <QTimer>

#include <algorithm>
#include <tuple>
#include <atomic>
#include <utility>

namespace doodlebold {

int Document::s_untitledCounter = 0;

namespace {

// Each layer's thumbnail, `maxSide` pixels at most, drawn at its opacity over the paper (Document::cellThumbnail).
QImage layerThumbnails(const std::vector<LayerNode>& nodes, const QSize& canvas, int maxSide, int height, const QColor& paper) {
    QImage out;
    for (const LayerNode& n : nodes) {
        const QImage layer = n.isFolder() ? Compositor::flatten({&n}, canvas).thumbnail(maxSide) : n.pixels.thumbnail(maxSide);
        if (out.isNull()) {
            out = QImage(layer.size(), QImage::Format_RGBA8888_Premultiplied);
            out.fill(paper);
        }
        QPainter p(&out);
        p.setOpacity(n.props.opacity / 100.0);
        p.drawImage(0, 0, layer);
    }
    if (out.isNull()) {
        out = QImage(QSize(maxSide, maxSide).scaled(canvas, Qt::KeepAspectRatio).boundedTo(QSize(maxSide, height)),
                     QImage::Format_RGBA8888_Premultiplied);
        out.fill(paper);
    }
    return out;
}

std::atomic<int> s_undoSteps{Document::kUndoLimit};

DocumentContent blankContent(const QSize& size, const CanvasBackground& background, double resolution) {
    DocumentContent c;
    c.state.background = background;
    c.resolution = resolution;
    c.state.layers = LayerStack(size);
    c.state.current = c.state.layers.insert(c.state.layers.makeRaster(c.state.layers.nextLayerName()), kNoLayer, -1);
    return c;
}

}

void Document::setUndoSteps(int steps) {
    s_undoSteps = std::max(1, steps);
}

int Document::undoSteps() {
    return s_undoSteps;
}

Document::Document(const QSize& size, const CanvasBackground& background, double resolution, QObject* parent)
    : Document(blankContent(size, background, resolution), parent) {
    m_untitledNumber = ++s_untitledCounter;
}

Document::Document(const QSize& size, const QColor& paper, QObject* parent)
    : Document(size, CanvasBackground::fromPaper(paper), kDefaultResolution, parent) {}

Document::Document(DocumentContent content, QObject* parent)
    : QObject(parent),
      m_state(std::move(content.state)),
      m_projection(m_state.layers.size(), m_state.background),
      m_display(m_state.layers.size(), m_state.background),
      m_resolution(content.resolution > 0 ? content.resolution : kDefaultResolution) {
    if (!m_state.layers.find(m_state.current)) {
        const auto order = m_state.layers.displayOrder();
        m_state.current = order.empty() ? kNoLayer : order.front()->props.id;
    }
    if (content.view)
        m_view = *content.view;
    animation_ops::normalize(m_state);
    rebuildUnderlay();
    m_undo.setUndoLimit(undoSteps());
    m_frameTimer = new QTimer(this);
    m_frameTimer->setInterval(kStrokeFrameMs);
    connect(m_frameTimer, &QTimer::timeout, this, &Document::captureStrokeFrame);
    connect(&m_undo, &QUndoStack::cleanChanged, this, [this](bool clean) { emit modifiedChanged(!clean); });
    connect(&m_undo, &QUndoStack::indexChanged, this, [this](int index) {
        const bool undone = index < m_undoIndex;
        m_undoIndex = index;
        if (!m_pushing)
            recordStep(undone ? TimelapseStep::Kind::Undo : TimelapseStep::Kind::Redo);
    });
    if (content.timelapse && !content.timelapse->isEmpty() && content.timelapse->finalSize() == size()) {
        m_recorder = std::make_unique<TimelapseRecorder>(*content.timelapse);
        // The picture is the recording's last one, so nothing is dirty until something is drawn.
    }
}

Document::~Document() {
    // The undo stack empties itself when it goes (and says so): by then there must be nothing left to record into.
    disconnect(&m_undo, nullptr, this, nullptr);
    m_recorder.reset();
}

void Document::startRecording() {
    if (m_recorder)
        return;
    m_recorder = std::make_unique<TimelapseRecorder>(m_state.layers, m_state.background);
    syncFrameTimer();
    emit recordingChanged();
}

bool Document::isRecordingPaused() const {
    return m_recorder && m_recorder->isPaused();
}

void Document::setRecordingPaused(bool paused) {
    if (!m_recorder || m_recorder->isPaused() == paused)
        return;
    m_recorder->setPaused(paused);
    syncFrameTimer();
    emit recordingChanged();
}

void Document::deleteRecording() {
    if (!m_recorder)
        return;
    m_recorder.reset();
    syncFrameTimer();
    emit recordingChanged();
}

int Document::recordedSteps() const {
    return m_recorder ? m_recorder->stepCount() : 0;
}

std::int64_t Document::recordedPaintingMs() const {
    return m_recorder ? m_recorder->paintedMs() : 0;
}

std::int64_t Document::recordingStartedMs() const {
    return m_recorder ? m_recorder->startedMs() : 0;
}

std::int64_t Document::recordedBytes() const {
    return m_recorder ? m_recorder->byteSize() : 0;
}

std::shared_ptr<const Timelapse> Document::timelapse() const {
    return m_recorder ? std::make_shared<const Timelapse>(m_recorder->snapshot()) : nullptr;
}

void Document::syncFrameTimer() {
    const bool wanted = m_painting && m_recorder && !m_recorder->isPaused();
    if (wanted == m_frameTimer->isActive())
        return;
    if (wanted)
        m_frameTimer->start();
    else
        m_frameTimer->stop();
}

void Document::captureStrokeFrame() {
    if (!m_painting || !m_recorder || m_recorder->isPaused())
        return;
    // Zoomed out, a stroke shows as the view's reduced blocks and reaches the layer only at its end: its frames are those
    // blocks enlarged (softer than the canvas; the step ending the stroke stores it exactly).
    const TimelapseRecorder::TileImages over = m_projection.takeStrokeFrameTiles(m_state.layers);
    m_recorder->record(TimelapseStep::Kind::Partial, m_projection.image(m_state.layers), over.empty() ? nullptr : &over);
}

void Document::dropStrokeFrames() {
    if (m_recorder)
        m_recorder->abandonStroke();
}

void Document::recordStep(TimelapseStep::Kind kind) {
    if (!m_recorder || m_recorder->isPaused())
        return;
    if (m_recorder->record(kind, m_projection.image(m_state.layers)))
        emit recordingChanged();
}

// Pushes a finished step on the undo stack and records the picture it left (a merged step too: the picture changed).
void Document::pushUndo(QUndoCommand* command) {
    m_pushing = true;
    m_undo.push(command);
    m_pushing = false;
    m_undoIndex = m_undo.index();
    recordStep(TimelapseStep::Kind::Edit);
}

DocumentContent Document::content() const {
    return {m_state, m_resolution, m_view, timelapse()};
}

QString Document::displayName() const {
    if (!m_filePath.isEmpty())
        return QFileInfo(m_filePath).fileName();
    if (!m_sourceName.isEmpty())
        return m_sourceName;
    return QStringLiteral("Untitled-%1").arg(m_untitledNumber);
}

void Document::setFilePath(const QString& path) {
    m_filePath = path;
    emit displayNameChanged(displayName());
}

void Document::setSourceName(const QString& fileName) {
    m_sourceName = fileName;
    emit displayNameChanged(displayName());
}

void Document::markSaved(const QString& path, bool lostData) {
    m_lostData = lostData;
    m_undo.setClean();
    setFilePath(path);  // also refreshes the title, whether or not the clean state changed
}

QString Document::titleText() const {
    QString marks;
    if (isModified())
        marks += QLatin1Char('*');
    if (m_lostData)
        marks += QLatin1Char('!');
    return marks + displayName();
}

void Document::setCurrentLayer(LayerId id) {
    const LayerNode* n = m_state.layers.find(id);
    if (id == m_state.current || !n)
        return;
    const bool wasMask = m_state.maskTarget;
    m_state.current = id;
    m_state.maskTarget = wasMask && n->mask && !n->isFolder();
    const int track = m_state.animation.currentTrack;
    animation_ops::followLayer(m_state, id);
    emit currentLayerChanged(id);
    if (m_state.animation.currentTrack != track)
        emit currentFrameChanged(m_state.animation.currentFrame());
    if (m_state.maskTarget != wasMask)
        emit maskTargetChanged(m_state.maskTarget);
}

void Document::setMaskTarget(bool mask) {
    const LayerNode* n = currentLayer();
    mask = mask && n && n->mask;
    if (mask == m_state.maskTarget || m_painting)
        return;
    m_state.maskTarget = mask;
    emit maskTargetChanged(mask);
}

EditResult<> Document::undo() {
    if (m_painting)
        return EditResult<>::failure(EditError::StrokeInProgress);
    m_undo.undo();
    return {};
}

EditResult<> Document::redo() {
    if (m_painting)
        return EditResult<>::failure(EditError::StrokeInProgress);
    m_undo.redo();
    return {};
}

void Document::invalidate(const QRect& rect) {
    if (rect.isEmpty())
        return;
    m_projection.invalidate(rect);
    m_display.invalidate(rect);
    if (m_recorder)
        m_recorder->markChanged(rect);
    emit imageChanged(rect.intersected(this->rect()));
}

void Document::installState(DocumentState s) {
    const bool resized = s.layers.size() != m_state.layers.size();
    const QRect area = resized ? QRect() : changedArea(m_state.layers, s.layers);
    const QRect selectionArea = selectionChangedArea(m_state.selection, s.selection);
    const LayerId oldCurrent = m_state.current;
    const bool oldMaskTarget = m_state.maskTarget;
    const bool bgChanged = !(s.background == m_state.background);
    const auto playhead = [](const Animation& a) { return std::tuple(a.currentFrame(), a.beat, a.currentTrack); };
    const auto oldPlayhead = playhead(m_state.animation);
    s.animation.view = m_state.animation.view;  // the timeline's view is not part of an undo step
    m_state = std::move(s);
    if (!m_state.layers.find(m_state.current)) {
        const auto order = m_state.layers.displayOrder();
        m_state.current = order.empty() ? kNoLayer : order.front()->props.id;
    }
    if (const LayerNode* n = m_state.layers.find(m_state.current); !n || !n->mask)
        m_state.maskTarget = false;
    if (resized) {
        m_projection = Projection(size(), m_state.background);
        m_display = Projection(size(), m_state.background);
        if (m_recorder)
            m_recorder->markResized(size());
        emit canvasResized(size());
        invalidate(rect());
        if (bgChanged)
            emit backgroundChanged();
    } else if (bgChanged) {
        m_projection.setBackground(m_state.background);
        m_display.setBackground(m_state.background);
        invalidate(rect());
        emit backgroundChanged();
    } else {
        invalidate(area);
    }
    rebuildUnderlay();
    emit layersChanged();
    emit animationChanged();
    if (playhead(m_state.animation) != oldPlayhead)
        emit currentFrameChanged(m_state.animation.currentFrame());
    if (!selectionArea.isEmpty())
        emit selectionChanged(selectionArea);
    if (m_state.current != oldCurrent)
        emit currentLayerChanged(m_state.current);
    if (m_state.maskTarget != oldMaskTarget)
        emit maskTargetChanged(m_state.maskTarget);
}

void Document::restoreState(const DocumentState& s) {
    installState(s);
}

template <typename Op> auto Document::apply(const QString& text, Op&& op, int mergeKey, LayerId mergeLayer) {
    using R = decltype(op(m_state));
    if (m_painting)
        return R::failure(EditError::StrokeInProgress);
    DocumentState next = m_state;
    R result = op(next);
    if (!result)
        return result;  // `next` may be half-modified; it is dropped
    animation_ops::normalize(next);
    DocumentState before = m_state;
    installState(next);
    pushUndo(new StateCommand(this, std::move(before), std::move(next), text, mergeKey, mergeLayer));
    return result;
}

EditResult<> Document::setBackground(const CanvasBackground& background) {
    if (background == m_state.background)
        return EditResult<>::failure(EditError::NoChange);
    return apply(tr("Canvas Background"), [&](DocumentState& s) {
        s.background = background;
        return EditResult<>{};
    });
}

EditResult<LayerId> Document::addLayer() {
    return apply(tr("New Layer"), [](DocumentState& s) { return layer_ops::addLayer(s); });
}

EditResult<LayerId> Document::addFolder() {
    return apply(tr("New Layer Folder"), [](DocumentState& s) { return layer_ops::addFolder(s); });
}

EditResult<LayerId> Document::duplicateLayer(LayerId id) {
    return apply(tr("Duplicate Layer"), [id](DocumentState& s) { return layer_ops::duplicate(s, id); });
}

EditResult<> Document::deleteLayer(LayerId id) {
    return apply(tr("Delete Layer"), [id](DocumentState& s) { return layer_ops::remove(s, id); });
}

EditResult<> Document::deleteLayers(const std::vector<LayerId>& ids) {
    return apply(tr("Delete Layers"), [&ids](DocumentState& s) { return layer_ops::removeLayers(s, ids); });
}

EditResult<LayerId> Document::mergeLayers(const std::vector<LayerId>& ids) {
    return apply(tr("Merge Layers"), [&ids](DocumentState& s) { return layer_ops::mergeLayers(s, ids); });
}

EditResult<> Document::mergeDown(LayerId id) {
    return apply(tr("Merge Down"), [id](DocumentState& s) { return layer_ops::mergeDown(s, id); });
}

EditResult<> Document::transferDown(LayerId id) {
    return apply(tr("Move Painting to Layer Below"), [id](DocumentState& s) { return layer_ops::transferDown(s, id); });
}

EditResult<LayerId> Document::mergeVisible() {
    return apply(tr("Merge Visible Layers"), [](DocumentState& s) { return layer_ops::mergeVisible(s); });
}

EditResult<LayerId> Document::flattenImage() {
    return apply(tr("Flatten Image"), [](DocumentState& s) { return layer_ops::flatten(s); });
}

EditResult<> Document::reverseCanvas(bool horizontal) {
    return apply(horizontal ? tr("Flip Canvas Horizontal") : tr("Flip Canvas Vertical"), [horizontal](DocumentState& s) {
        return animation_ops::onEveryFrame(s, [horizontal](DocumentState& t) { return layer_ops::reverseCanvas(t, horizontal); });
    });
}

EditResult<> Document::trimCanvas() {
    return apply(tr("Trim Canvas by Selection"), [](DocumentState& s) {
        return animation_ops::onEveryFrame(s, [](DocumentState& t) { return layer_ops::trimCanvas(t); });
    });
}

EditResult<> Document::rotateCanvas(bool clockwise) {
    return apply(clockwise ? tr("Rotate Canvas 90deg. CW") : tr("Rotate Canvas 90deg. CCW"), [clockwise](DocumentState& s) {
        return animation_ops::onEveryFrame(s, [clockwise](DocumentState& t) { return layer_ops::rotateCanvas(t, clockwise); });
    });
}

EditResult<> Document::moveLayer(LayerId id, LayerId parent, int index) {
    return apply(tr("Move Layer"), [=](DocumentState& s) { return layer_ops::move(s, id, parent, index); });
}

EditResult<> Document::raiseLayer(LayerId id) {
    return apply(tr("Move Layer"), [id](DocumentState& s) { return layer_ops::raise(s, id); });
}

EditResult<> Document::lowerLayer(LayerId id) {
    return apply(tr("Move Layer"), [id](DocumentState& s) { return layer_ops::lower(s, id); });
}

EditResult<> Document::layerToFront(LayerId id) {
    return apply(tr("Move Layer"), [id](DocumentState& s) { return layer_ops::toFront(s, id); });
}

EditResult<> Document::layerToBack(LayerId id) {
    return apply(tr("Move Layer"), [id](DocumentState& s) { return layer_ops::toBack(s, id); });
}

EditResult<> Document::setLayerProps(LayerId id, const LayerProps& props, int mergeKey) {
    return apply(tr("Layer Properties"), [&](DocumentState& s) { return layer_ops::setProps(s, id, props); }, mergeKey, id);
}

EditResult<LayerId> Document::addTextLayer(const TextContent& content) {
    return apply(tr("New Text Layer"), [&](DocumentState& s) { return layer_ops::addTextLayer(s, content); });
}

EditResult<> Document::setText(LayerId id, const TextContent& content, int mergeKey) {
    return apply(tr("Edit Text"), [&](DocumentState& s) { return layer_ops::setText(s, id, content); }, mergeKey, id);
}

EditResult<> Document::rasterizeLayer(LayerId id) {
    return apply(tr("Rasterize"), [id](DocumentState& s) { return layer_ops::rasterize(s, id); });
}

EditResult<> Document::clearLayers(const std::vector<LayerId>& ids) {
    return apply(tr("Clear Layers"), [&ids](DocumentState& s) { return layer_ops::clearLayers(s, ids); });
}

EditResult<> Document::clearLayer(LayerId id) {
    return apply(tr("Clear Layer"), [id](DocumentState& s) {
        return s.maskTarget && id == s.current ? layer_ops::clearMask(s, id) : layer_ops::clear(s, id);
    });
}

EditResult<> Document::fillLayer(LayerId id, const QColor& color) {
    return apply(tr("Fill"), [&](DocumentState& s) {
        return s.maskTarget && id == s.current ? layer_ops::fillMask(s, id, color) : layer_ops::fill(s, id, color);
    });
}

EditResult<> Document::fillArea(LayerId id, const QColor& color, const SelectionMask& area) {
    if (area.isEmpty())
        return EditResult<>::failure(EditError::NoChange);
    return apply(tr("Bucket"), [&](DocumentState& s) {
        std::optional<SelectionMask> kept = std::exchange(s.selection, area);
        EditResult<> filled = s.maskTarget && id == s.current ? layer_ops::fillMask(s, id, color) : layer_ops::fill(s, id, color);
        s.selection = std::move(kept);
        return filled;
    });
}

EditResult<> Document::addLayerMask(LayerId id) {
    return apply(tr("New Layer Mask"), [id](DocumentState& s) { return layer_ops::addMask(s, id); });
}

EditResult<> Document::applyLayerMask(LayerId id) {
    return apply(tr("Apply Layer Mask"), [id](DocumentState& s) { return layer_ops::applyMask(s, id); });
}

EditResult<> Document::deleteLayerMask(LayerId id) {
    return apply(tr("Delete Layer Mask"), [id](DocumentState& s) { return layer_ops::removeMask(s, id); });
}

namespace {

QString selectionText(selection::CombineMode mode) {
    switch (mode) {
    case selection::CombineMode::Add: return Document::tr("Add to Selection");
    case selection::CombineMode::Subtract: return Document::tr("Subtract from Selection");
    case selection::CombineMode::Intersect: return Document::tr("Intersect Selection");
    case selection::CombineMode::Replace: break;
    }
    return Document::tr("Select");
}

}

EditResult<> Document::selectAll() {
    return apply(tr("Select All"), [](DocumentState& s) { return selection_edits::selectAll(s); });
}

EditResult<> Document::deselect() {
    return apply(tr("Deselect"), [](DocumentState& s) { return selection_edits::deselect(s); });
}

EditResult<> Document::invertSelection() {
    return apply(tr("Invert Selection"), [](DocumentState& s) { return selection_edits::invert(s); });
}

EditResult<> Document::setSelection(SelectionMask shape, selection::CombineMode mode, const QString& undoText) {
    return apply(undoText.isEmpty() ? selectionText(mode) : undoText,
                 [&](DocumentState& s) { return selection_edits::set(s, shape, mode); });
}

EditResult<> Document::dilateSelection(int pixels) {
    return apply(tr("Grow Selection"), [pixels](DocumentState& s) { return selection_edits::dilate(s, pixels); });
}

EditResult<> Document::erodeSelection(int pixels) {
    return apply(tr("Shrink Selection"), [pixels](DocumentState& s) { return selection_edits::erode(s, pixels); });
}

EditResult<> Document::selectionFromLayer(LayerId id, selection::CombineMode mode) {
    return apply(tr("Select Layer Region"), [=](DocumentState& s) { return selection_edits::fromLayer(s, id, mode); });
}

EditResult<> Document::selectionFromMask(LayerId id, selection::CombineMode mode) {
    return apply(tr("Select Layer Region"), [=](DocumentState& s) { return selection_edits::fromMask(s, id, mode); });
}

TiledImage Document::mergedImage() const {
    std::vector<const LayerNode*> top;
    for (const LayerNode& n : m_state.layers.root().children)
        top.push_back(&n);
    return Compositor::flatten(top, size());
}

QRect Document::transformBounds(LayerId id) const {
    return transform_edits::transformBounds(m_state, id);
}

EditResult<> Document::transformLayer(LayerId id, const transform::Quad& destination, transform::Interpolation interpolation,
                                      double perspective) {
    return apply(tr("Transform Layer"),
                 [&](DocumentState& s) { return transform_edits::transformLayer(s, id, destination, interpolation, perspective); });
}

EditResult<> Document::moveLayers(const std::vector<LayerId>& ids, LayerId parent, int index) {
    return apply(tr("Move Layers"), [&](DocumentState& s) { return layer_ops::moveLayers(s, ids, parent, index); });
}

EditResult<> Document::moveLayersExactly(const std::vector<LayerId>& ids, const QPoint& offset) {
    return apply(tr("Move Layer Pixels"), [&](DocumentState& s) { return transform_edits::moveLayersExactly(s, ids, offset); });
}

EditResult<> Document::moveLayerExactly(LayerId id, transform::Turn turn, const QPoint& offset) {
    QString text = tr("Move Layer Pixels");
    switch (turn) {
    case transform::Turn::FlipHorizontal: text = tr("Flip Horizontal"); break;
    case transform::Turn::FlipVertical: text = tr("Flip Vertical"); break;
    case transform::Turn::Clockwise: text = tr("Rotate 90° CW"); break;
    case transform::Turn::CounterClockwise: text = tr("Rotate 90° CCW"); break;
    case transform::Turn::Rotate180: text = tr("Rotate 180°"); break;
    case transform::Turn::None: break;
    }
    return apply(text, [&](DocumentState& s) { return transform_edits::moveLayerExactly(s, id, turn, offset); });
}

EditResult<ClipboardImage> Document::copy(LayerId id) const {
    using R = EditResult<ClipboardImage>;
    const LayerNode* n = m_state.layers.find(id);
    if (!n)
        return R::failure(EditError::NoSuchLayer);
    if (n->isFolder())
        return R::failure(EditError::NotARasterLayer);
    auto picture = clipboard::copy(n->pixels, m_state.selection ? &*m_state.selection : nullptr);
    if (!picture)
        return R::failure(EditError::NoChange);
    return *picture;
}

EditResult<ClipboardImage> Document::copyBounds(LayerId id) const {
    using R = EditResult<ClipboardImage>;
    if (!m_state.selection)
        return R::failure(EditError::NoChange);
    const QRect bounds = m_state.selection->bounds(true);
    return copyWith(id, selection::rectangle(size(), QRectF(bounds), false));
}

EditResult<ClipboardImage> Document::copyWith(LayerId id, const SelectionMask& area) const {
    using R = EditResult<ClipboardImage>;
    const LayerNode* n = m_state.layers.find(id);
    if (!n)
        return R::failure(EditError::NoSuchLayer);
    if (n->isFolder())
        return R::failure(EditError::NotARasterLayer);
    auto picture = clipboard::copy(n->pixels, &area);
    if (!picture)
        return R::failure(EditError::NoChange);
    return *picture;
}

EditResult<ClipboardImage> Document::cut(LayerId id) {
    return apply(tr("Cut"), [&](DocumentState& s) -> EditResult<ClipboardImage> {
        using R = EditResult<ClipboardImage>;
        const LayerNode* n = s.layers.find(id);
        if (!n)
            return R::failure(EditError::NoSuchLayer);
        if (n->isFolder())
            return R::failure(EditError::NotARasterLayer);
        if (n->isText())
            return R::failure(EditError::NotForThisLayer);
        auto picture = clipboard::copy(n->pixels, s.selection ? &*s.selection : nullptr);
        if (!picture)
            return R::failure(EditError::NoChange);
        if (const auto cleared = layer_ops::clear(s, id); !cleared)
            return R::failure(cleared.error());
        return *picture;
    });
}

EditResult<LayerId> Document::paste(const ClipboardImage& picture, bool keepSelection) {
    return apply(tr("Paste"), [&](DocumentState& s) -> EditResult<LayerId> {
        if (picture.isNull())
            return EditResult<LayerId>::failure(EditError::NoChange);
        const auto added = layer_ops::addLayer(s);
        s.layers.find(added.value())->pixels = clipboard::toPixels(picture, s.layers.size());
        if (!keepSelection)
            s.selection.reset();
        return added;
    });
}

EditResult<LayerId> Document::addImageLayer(TiledImage pixels, const QString& name) {
    if (pixels.size() != size())
        return EditResult<LayerId>::failure(EditError::SizeMismatch);
    return apply(tr("Import Layer"), [&](DocumentState& s) -> EditResult<LayerId> {
        const auto added = layer_ops::addLayer(s);
        LayerNode* node = s.layers.find(added.value());
        node->pixels = std::move(pixels);
        if (!name.isEmpty())
            node->props.name = name;
        return added;
    });
}

// Whether the layer is one of a locked track's (by its top-level layer).
bool Document::trackLocked(LayerId id) const {
    for (const LayerNode& top : m_state.layers.root().children) {
        const auto holds = [id](const auto& self, const LayerNode& n) -> bool {
            return n.props.id == id || std::any_of(n.children.begin(), n.children.end(), [&](const LayerNode& c) { return self(self, c); });
        };
        if (holds(holds, top))
            return m_state.animation.lockedLayer(top.props.id);
    }
    return false;
}

EditResult<TiledImage*> Document::paintTarget(LayerId id) {
    LayerNode* n = m_state.layers.find(id);
    if (!n)
        return EditResult<TiledImage*>::failure(EditError::NoSuchLayer);
    if (n->isFolder())
        return EditResult<TiledImage*>::failure(EditError::NotARasterLayer);
    if (n->isText())
        return EditResult<TiledImage*>::failure(EditError::NotForThisLayer);
    if (n->props.locked)
        return EditResult<TiledImage*>::failure(EditError::LayerLocked);
    if (!n->props.visible)
        return EditResult<TiledImage*>::failure(EditError::LayerHidden);
    if (trackLocked(id))
        return EditResult<TiledImage*>::failure(EditError::TrackLocked);
    return &n->pixels;
}

void Document::setPainting(bool painting) {
    if (painting == m_painting)
        return;
    m_painting = painting;
    if (painting && m_recorder)
        m_recorder->beginStroke();
    syncFrameTimer();
    emit paintingChanged(painting);
}

void Document::showStrokeBlocks(LayerId layer, int level, std::unordered_map<TileCoord, std::vector<float>, TileCoordHash> blocks,
                                const QRect& area) {
    m_projection.setStrokeBlocks(layer, level, std::move(blocks));
    emit imageChanged(area.intersected(rect()));
}

void Document::notifyPixelsChanged(LayerId id, const QRect& rect) {
    invalidate(rect);
    emit layerPixelsChanged(id);
}

void Document::commitPixelEdit(LayerId id, const TileMap& before, const QString& text) {
    const LayerNode* n = m_state.layers.find(id);
    if (!n || before.empty())
        return;
    TileMap after;
    for (const auto& [coord, tile] : before)
        after[coord] = n->pixels.tileShared(coord.x, coord.y);
    pushUndo(new TileEditCommand(this, id, before, std::move(after), text));
}

EditResult<TiledImage*> Document::maskPaintTarget(LayerId id) {
    using R = EditResult<TiledImage*>;
    const LayerNode* n = m_state.layers.find(id);
    if (!n)
        return R::failure(EditError::NoSuchLayer);
    if (!n->mask)
        return R::failure(EditError::NoLayerMask);
    if (n->props.locked)
        return R::failure(EditError::LayerLocked);
    if (!n->props.visible)
        return R::failure(EditError::LayerHidden);
    if (!n->props.maskEnabled)
        return R::failure(EditError::MaskDisabled);
    m_maskBefore = m_state;
    TiledImage picture(size());
    TilePtr full;
    for (const auto& [coord, maskTile] : n->mask->tiles()) {
        if (SelectionMask::isFull(maskTile.get())) {
            if (!full) {
                auto t = std::make_shared<Tile>();
                std::fill(std::begin(t->px), std::end(t->px), Channel(kChannelOne));
                full = std::move(t);
            }
            picture.setTile(coord.x, coord.y, full);
            continue;
        }
        Tile* t = picture.writableTile(coord.x, coord.y);
        for (std::size_t i = 0; i < std::size_t(SelectionTile::kValues); ++i)
            for (std::size_t c = 0; c < 4; ++c)
                t->px[i * 4 + c] = Channel(maskTile->v[i]);
    }
    m_maskPicture = std::move(picture);
    return &*m_maskPicture;
}

void Document::notifyMaskPainted(LayerId id, const QRect& rect) {
    LayerNode* n = m_state.layers.find(id);
    if (!n || !n->mask || !m_maskPicture || rect.isEmpty())
        return;
    const QRect area = rect.intersected(this->rect());
    std::array<std::uint16_t, Tile::kSize> row{};
    for (int y = area.top(); y <= area.bottom(); ++y) {
        for (int x0 = area.left(); x0 <= area.right(); x0 += Tile::kSize - x0 % Tile::kSize) {
            const int n0 = std::min(area.right() + 1, x0 - x0 % Tile::kSize + Tile::kSize) - x0;
            const Tile* t = m_maskPicture->tile(x0 / Tile::kSize, y / Tile::kSize);
            for (int i = 0; i < n0; ++i)
                row[std::size_t(i)] = t ? t->at((x0 + i) % Tile::kSize, y % Tile::kSize)[0] : 0;
            n->mask->writeRow(y, x0, n0, row.data());
        }
    }
    invalidate(area);
    emit layerPixelsChanged(id);
}

void Document::endMaskPaint(LayerId id, bool keep, const QString& text) {
    m_maskPicture.reset();
    LayerNode* n = m_state.layers.find(id);
    if (!n || !n->mask)
        return;
    n->mask->squeeze();
    if (!keep) {
        dropStrokeFrames();
        installState(std::exchange(m_maskBefore, {}));
        return;
    }
    if (const LayerNode* before = m_maskBefore.layers.find(id); before && before->mask && *before->mask == *n->mask) {
        m_maskBefore = {};
        return;
    }
    pushUndo(new StateCommand(this, std::exchange(m_maskBefore, {}), m_state, text, 0, kNoLayer));
}

LayerNode* Document::restoreHidden(LayerId id) {
    const auto find = [id](const auto& self, std::vector<LayerNode>& nodes) -> LayerNode* {
        for (LayerNode& n : nodes) {
            if (n.props.id == id)
                return &n;
            if (LayerNode* inside = self(self, n.children))
                return inside;
        }
        return nullptr;
    };
    for (AnimationTrack& track : m_state.animation.tracks)
        if (track.hidden)
            for (AnimationFrame& f : track.frames)
                if (LayerNode* n = find(find, f.layers))
                    return n;
    return nullptr;
}

void Document::restoreTiles(LayerId id, const TileMap& tiles) {
    if (!m_state.layers.find(id)) {
        // Painted on another frame: undo and redo go back to it. On a hidden track's frame the pixels change where they are.
        if (LayerNode* stashed = restoreHidden(id)) {
            for (const auto& [coord, tile] : tiles)
                stashed->pixels.setTile(coord.x, coord.y, tile);
            emit animationChanged();
            return;
        }
        const Animation& a = m_state.animation;
        const auto holds = [id](const auto& self, const std::vector<LayerNode>& nodes) -> bool {
            return std::any_of(nodes.begin(), nodes.end(), [&](const LayerNode& n) { return n.props.id == id || self(self, n.children); });
        };
        for (int t = 0; t < a.trackCount(); ++t) {
            const AnimationTrack& track = a.tracks[static_cast<std::size_t>(t)];
            for (int f = 0; f < track.frameCount(); ++f) {
                if (holds(holds, track.frames[static_cast<std::size_t>(f)].layers)) {
                    (void)goToCell(t, f);
                    break;
                }
            }
        }
    }
    LayerNode* n = m_state.layers.find(id);
    if (!n)
        return;
    QRect area;
    for (const auto& [coord, tile] : tiles) {
        n->pixels.setTile(coord.x, coord.y, tile);
        area |= coord.pixelRect();
    }
    invalidate(area);
    emit layerPixelsChanged(id);
}

const QImage& Document::displayImage() {
    if (!hasUnderlay())
        return projection();
    return m_display.image(animation_ops::displayStack(m_state, m_videoNode, m_onions));
}

namespace {

// What a node shows: its settings, its tiles (by address: tiles are shared until painted) and its children's.
quint64 nodeKey(quint64 h, const LayerNode& n) {
    const auto mix = [&h](quint64 v) { h = (h ^ v) * 0x100000001b3ULL; };
    mix(n.props.id.value());
    mix(quint64(n.props.opacity) << 8 | quint64(n.props.visible) << 1 | quint64(n.props.clipping));
    mix(quint64(n.props.mode));
    for (const auto& [coord, tile] : n.pixels.tiles())
        mix(reinterpret_cast<quintptr>(tile.get()) ^ (quint64(quint32(coord.x)) << 32 | quint32(coord.y)));
    for (const LayerNode& c : n.children)
        h = nodeKey(h, c);
    return h;
}

}

void Document::rebuildUnderlay() {
    const bool had = hasUnderlay();
    const quint64 before = m_underlayKey;
    m_onions = m_playing ? std::vector<LayerNode>{} : animation_ops::onionSkins(m_state);
    // The video picture becomes a layer once per picture and opacity, not on every change of the frames.
    const bool video = !m_videoPicture.isNull() && m_videoPicture.size() == size() && m_state.animation.videoShown();
    const auto key = std::pair(m_videoPicture.cacheKey(), video ? m_state.animation.video->opacity : -1);
    if (!video)
        m_videoNode.reset();
    else if (!m_videoNode || key != m_videoNodeKey)
        m_videoNode = m_videoTiles ? animation_ops::videoNode(*m_videoTiles, m_state.animation.video->opacity)
                                   : animation_ops::videoNode(m_videoPicture, m_state.animation.video->opacity);
    m_videoNodeKey = key;
    // The whole canvas is composited again only when what lies under the frames changed (not on every edit of the frames).
    m_underlayKey = 0xcbf29ce484222325ULL ^ quint64(key.first) ^ (quint64(key.second) << 48);
    for (const LayerNode& n : m_onions)
        m_underlayKey = nodeKey(m_underlayKey, n);
    if ((hasUnderlay() || had) && (m_underlayKey != before || hasUnderlay() != had)) {
        m_display.invalidate(rect());
        emit imageChanged(rect());
    }
    if (!hasUnderlay())
        m_display.release();
}

void Document::setPlaying(bool playing) {
    if (playing == m_playing)
        return;
    m_playing = playing;
    rebuildUnderlay();
}

void Document::setVideoPicture(const QImage& picture, std::shared_ptr<const TiledImage> tiles) {
    if (picture.cacheKey() == m_videoPicture.cacheKey() || (picture.isNull() && m_videoPicture.isNull()))
        return;
    m_videoPicture = picture;
    m_videoTiles = tiles && tiles->size() == picture.size() ? std::move(tiles) : nullptr;
    rebuildUnderlay();
}

void Document::setTimelineView(const TimelineView& view) {
    if (view == m_state.animation.view)
        return;
    m_state.animation.view = view;
    rebuildUnderlay();
    emit animationChanged();
}

template <typename Op> EditResult<> Document::movePlayhead(Op&& op) {
    if (m_painting)
        return EditResult<>::failure(EditError::StrokeInProgress);
    DocumentState next = m_state;
    if (auto done = op(next); !done)
        return done;
    installState(std::move(next));
    return {};
}

EditResult<> Document::goToFrame(int frame) {
    return movePlayhead([=](DocumentState& s) { return animation_ops::goTo(s, frame); });
}

EditResult<> Document::goToBeat(int beat) {
    return movePlayhead([=](DocumentState& s) { return animation_ops::goToBeat(s, beat); });
}

EditResult<> Document::goToCell(int track, int frame) {
    return movePlayhead([=](DocumentState& s) { return animation_ops::goToCell(s, track, frame); });
}

EditResult<int> Document::addTrack() {
    return apply(tr("New Track"), [](DocumentState& s) { return animation_ops::addTrack(s); });
}

EditResult<int> Document::duplicateTrack(int track) {
    return apply(tr("Duplicate Track"), [=](DocumentState& s) { return animation_ops::duplicateTrack(s, track); });
}

EditResult<> Document::setPlayRange(int start, int end) {
    return apply(tr("Play Range"), [=](DocumentState& s) { return animation_ops::setPlayRange(s, start, end); });
}

EditResult<> Document::deleteTrack(int track) {
    return apply(tr("Delete Track"), [=](DocumentState& s) { return animation_ops::deleteTrack(s, track); });
}

EditResult<> Document::deleteTracks(const std::vector<int>& tracks) {
    return apply(tr("Delete Tracks"), [&](DocumentState& s) { return animation_ops::deleteTracks(s, tracks); });
}

EditResult<> Document::setTrackLabel(int track, int label) {
    return apply(tr("Track Colour"), [=](DocumentState& s) { return animation_ops::setTrackLabel(s, track, label); });
}

EditResult<> Document::clearTrack(int track) {
    return apply(tr("Clear Track"), [=](DocumentState& s) { return animation_ops::clearTrack(s, track); });
}

EditResult<> Document::renameTrack(int track, const QString& name) {
    return apply(tr("Rename Track"), [&](DocumentState& s) { return animation_ops::renameTrack(s, track, name); });
}

EditResult<> Document::moveTrack(int track, int to) {
    return apply(tr("Move Track"), [=](DocumentState& s) { return animation_ops::moveTrack(s, track, to); });
}

EditResult<> Document::setTrackHidden(int track, bool hidden) {
    return apply(hidden ? tr("Hide Track") : tr("Show Track"),
                 [=](DocumentState& s) { return animation_ops::setTrackHidden(s, track, hidden); });
}

EditResult<> Document::setTrackLocked(int track, bool locked) {
    return apply(locked ? tr("Lock Track") : tr("Unlock Track"),
                 [=](DocumentState& s) { return animation_ops::setTrackLocked(s, track, locked); });
}

EditResult<> Document::clearFrames(const std::vector<int>& frames) {
    return apply(tr("Clear Frames"), [&](DocumentState& s) { return animation_ops::clearFrames(s, frames); });
}

EditResult<> Document::setSoundMuted(int id, bool muted) {
    return apply(muted ? tr("Mute Sound") : tr("Unmute Sound"),
                 [=](DocumentState& s) { return animation_ops::setSoundMuted(s, id, muted); });
}

EditResult<int> Document::newFrame() {
    return apply(tr("New Frame"), [](DocumentState& s) { return animation_ops::newFrame(s); });
}

EditResult<int> Document::duplicateFrames(const std::vector<int>& frames) {
    return apply(tr("Duplicate Frame"), [&](DocumentState& s) { return animation_ops::duplicateFrames(s, frames); });
}

EditResult<> Document::deleteFrames(const std::vector<int>& frames) {
    return apply(tr("Delete Frame"), [&](DocumentState& s) { return animation_ops::deleteFrames(s, frames); });
}

EditResult<> Document::keepOnlyCurrentFrame() {
    return apply(tr("Keep Only This Frame"), [](DocumentState& s) { return animation_ops::keepOnly(s); });
}

EditResult<> Document::changeHold(const std::vector<int>& frames, int delta) {
    return apply(delta > 0 ? tr("Hold More") : tr("Hold Less"),
                 [&](DocumentState& s) { return animation_ops::changeHold(s, frames, delta); });
}

EditResult<> Document::setFrameHold(int frame, int hold, int mergeKey) {
    return apply(tr("Hold"), [=](DocumentState& s) { return animation_ops::setHold(s, frame, hold); }, mergeKey);
}

EditResult<> Document::setFramesHold(const std::vector<int>& frames, int hold) {
    return apply(tr("Hold"), [&](DocumentState& s) { return animation_ops::setHolds(s, frames, hold); });
}

EditResult<> Document::moveFrames(const std::vector<int>& frames, int to) {
    return apply(tr("Move Frames"), [&](DocumentState& s) { return animation_ops::moveFrames(s, frames, to); });
}

EditResult<> Document::moveFrameGroups(const std::vector<animation_ops::FrameMove>& moves, int mergeKey) {
    return apply(tr("Move Frames"), [&](DocumentState& s) { return animation_ops::moveFrameGroups(s, moves); }, mergeKey);
}

EditResult<> Document::setLayerShared(LayerId id, bool shared) {
    return apply(shared ? tr("Share Layer With Every Frame") : tr("Make Per-Frame"),
                 [=](DocumentState& s) { return animation_ops::setShared(s, id, shared); });
}

EditResult<> Document::setPlayback(double fps, bool loop) {
    return apply(tr("Timeline Settings"), [=](DocumentState& s) { return animation_ops::setPlayback(s, fps, loop); });
}

EditResult<int> Document::addSound(SoundClip clip) {
    return apply(tr("Add Sound"), [&](DocumentState& s) { return animation_ops::addSound(s, clip); });
}

EditResult<> Document::moveSound(int id, int startBeat, int track, int mergeKey) {
    return apply(tr("Move Sound"), [=](DocumentState& s) { return animation_ops::moveSound(s, id, startBeat, track); }, mergeKey);
}

EditResult<> Document::trimSound(int id, int startBeat, double trimStart, double trimEnd, int mergeKey) {
    return apply(
        tr("Trim Sound"), [=](DocumentState& s) { return animation_ops::trimSound(s, id, startBeat, trimStart, trimEnd); }, mergeKey);
}

EditResult<> Document::removeSound(int id) {
    return apply(tr("Remove Sound"), [=](DocumentState& s) { return animation_ops::removeSound(s, id); });
}

EditResult<> Document::placeSounds(const std::vector<animation_ops::SoundPlace>& places, int mergeKey) {
    return apply(tr("Move Sounds"), [places](DocumentState& s) { return animation_ops::placeSounds(s, places); }, mergeKey);
}

EditResult<> Document::removeSounds(const std::vector<int>& ids) {
    return apply(ids.size() == 1 ? tr("Remove Sound") : tr("Remove Sounds"),
                 [ids](DocumentState& s) { return animation_ops::removeSounds(s, ids); });
}

EditResult<> Document::setVideo(const VideoReference& video, bool follow) {
    return apply(tr("Add Video"),
                 [&](DocumentState& s) { return follow ? animation_ops::followVideo(s, video) : animation_ops::setVideo(s, video); });
}

EditResult<> Document::removeVideo() {
    return apply(tr("Remove Video"), [](DocumentState& s) { return animation_ops::setVideo(s, std::nullopt); });
}

EditResult<> Document::setTrackVolume(int track, int volume, int mergeKey) {
    return apply(tr("Track Volume"), [=](DocumentState& s) { return animation_ops::setTrackVolume(s, track, volume); }, mergeKey);
}

EditResult<> Document::setVideoOpacity(int opacity, int mergeKey) {
    if (!m_state.animation.video)
        return EditResult<>::failure(EditError::NoChange);
    VideoReference v = *m_state.animation.video;
    v.opacity = std::clamp(opacity, 0, 100);
    return apply(tr("Video Opacity"), [&](DocumentState& s) { return animation_ops::setVideo(s, v); }, mergeKey);
}

LayerStack Document::frameStack(int frame) const {
    return animation_ops::frameStack(m_state, frame);
}

QImage Document::framePicture(int frame) const {
    QImage out(size(), QImage::Format_RGBA8888_Premultiplied);
    Compositor::renderDisplay(frameStack(frame), rect(), m_state.background, &out);
    return out;
}

QImage Document::cellThumbnail(int track, int frame, int height) const {
    return cellThumbnailMaker(track, frame, height)();
}

std::function<QImage()> Document::cellThumbnailMaker(int track, int frame, int height) const {
    const QSize canvas = size();
    const int maxSide = std::max(1, height * std::max(canvas.width(), canvas.height()) / std::max(1, canvas.height()));
    std::vector<LayerNode> nodes;
    const auto take = [&nodes](const LayerNode& n) {
        if (n.props.visible)
            nodes.push_back(n);
    };
    if (track == 0)
        for (const LayerNode& n : m_state.layers.root().children)
            if (m_state.animation.isShared(n.props.id))
                take(n);
    for (const LayerNode* n : animation_ops::ownLayers(m_state, track, frame))
        take(*n);
    return [nodes = std::move(nodes), canvas, maxSide, height, paper = m_state.background.paperColor()] {
        return layerThumbnails(nodes, canvas, maxSide, height, paper);
    };
}

}
