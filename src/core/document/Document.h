#pragma once

#include "core/animation/AnimationOps.h"
#include "core/document/DocumentContent.h"
#include "core/document/DocumentState.h"
#include "core/document/EditError.h"
#include "core/document/Projection.h"
#include "core/document/Clipboard.h"
#include "core/options/AppOptions.h"
#include "core/selection/SelectionOps.h"
#include "core/transform/Resample.h"
#include "core/view/ViewState.h"

#include <QImage>
#include <QObject>
#include <QSize>
#include <QString>
#include <QUndoStack>

#include <functional>
#include <memory>
#include <optional>

namespace doodlebold {

class TimelapseRecorder;
class Document;

// One open canvas. Document is the coordinator, not the place for rules: layer operations live in LayerOps, compositing in
// Compositor/Projection, brushes in core/brush. It applies an operation to a copy of its state, installs the result,
// invalidates what changed, records one undo step and emits signals.
class Document : public QObject {
    Q_OBJECT
public:
    static constexpr int kUndoLimit = AppOptions::kDefaultUndoSteps;
    // The share of the memory Doodlebold may use (Options › Performance › Memory) that the undo history's pixels may keep, past
    // which the oldest steps' pixels are let go. Each stroke keeps a copy of every 64 × 64 tile it changes (32 KiB each), so a
    // 1000 px brush painting over paint keeps 25 to 60 MB a step (47 MB measured on 2000 × 1500): the default 25 steps keep up
    // to 1.5 GB, and the most, 1000, up to 60 GB. A quarter caps it at about 3.4 GB on a 16 GB machine (about 13.7 GB allowed) and
    // 1.7 GB on an 8 GB one: the default stays under both, so it bites with more steps set or on a smaller machine (0.75 GB on
    // 4 GB). Half was tried on paper and doesn't bite before 100 steps on 16 GB.
    static constexpr double kUndoMemoryShare = 0.25;
    // The undo depth of canvases made or opened from now on: Options > Performance > Undo history. A canvas keeps the
    // depth it was made with, since its history can't be cut short.
    static void setUndoSteps(int steps);
    static int undoSteps();

    // A new canvas has background and one empty layer, "Layer1". resolution (ppi) is only recorded (exports write it).
    Document(const QSize& size, const CanvasBackground& background, double resolution, QObject* parent = nullptr);
    Document(const QSize& size, const QColor& paper, QObject* parent = nullptr);
    // A document from file content (no undo history); a current layer not in the tree is replaced by the first layer. A
    // recording in the content is continued.
    explicit Document(DocumentContent content, QObject* parent = nullptr);
    ~Document() override;

    DocumentContent content() const;

    QSize size() const { return m_state.layers.size(); }
    QRect rect() const { return QRect(QPoint(0, 0), size()); }
    const CanvasBackground& background() const { return m_state.background; }
    double resolution() const { return m_resolution; }
    EditResult<> setBackground(const CanvasBackground& background);

    const LayerStack& layers() const { return m_state.layers; }
    LayerId currentLayerId() const { return m_state.current; }
    const LayerNode* currentLayer() const { return m_state.layers.find(m_state.current); }
    const LayerNode* layer(LayerId id) const { return m_state.layers.find(id); }
    void setCurrentLayer(LayerId id);
    // Whether the current layer's mask is the editing target (the red frame round the mask thumbnail): painting, Erase, Fill
    // and the panel's Delete act on the mask. Not an undo step. A layer without a mask is always its own target; selecting
    // another layer keeps the mask as target if it has one (the last part chosen is remembered), except for folders.
    bool maskTarget() const { return m_state.maskTarget; }
    void setMaskTarget(bool mask);

    const QImage& projection() { return m_projection.image(m_state.layers); }
    // Brings up to `mostTiles` tiles of projection() up to date and says whether any are left (Projection::catchUpSome).
    bool catchUpProjection(std::size_t mostTiles) { return m_projection.catchUpSome(m_state.layers, mostTiles); }
    // What the canvas shows: the projection, or with an animation's onion skins or video under the current frame's own
    // layers, a second composite with them in. Only for showing: picking a colour or anything a tool reads uses projection().
    const QImage& displayImage();
    // The display for a view zoomed out by 2^level, when nothing underlays the layers (Projection::reducedImage).
    const QImage& reducedDisplayImage(int level, const QRect& area = QRect()) {
        return m_projection.reducedImage(m_state.layers, level, area);
    }
    // A running stroke's picture of its layer at a zoomed-out view's scale (Projection::setStrokeBlocks), shown over `area`
    // while the layer waits for the stroke's end; cleared when it ends.
    void showStrokeBlocks(LayerId layer, int level, std::unordered_map<TileCoord, std::vector<float>, TileCoordHash> blocks,
                          const QRect& area);
    void clearStrokeBlocks() { m_projection.clearStrokeBlocks(); }
    bool hasUnderlay() const { return m_videoNode.has_value() || !m_onions.empty(); }

    QUndoStack* undoStack() { return &m_undo; }
    // Undo/redo, refused while a stroke runs (the brush is painting into the current tree).
    EditResult<> undo();
    EditResult<> redo();
    ViewState& view() { return m_view; }
    const ViewState& view() const { return m_view; }

    QString displayName() const;  // file name if saved, else the name of the file it was imported from, else "Untitled-N"
    QString filePath() const { return m_filePath; }
    void setFilePath(const QString& path);
    // The file an image or layered file was opened from. It names the document until it is saved, but is not a place to save to
    // (File → Save asks for an .doodle file instead of overwriting a picture).
    void setSourceName(const QString& fileName);
    bool isModified() const { return !m_undo.isClean(); }
    // After a successful save: the document lives in path, is no longer modified, and lostData says whether the format keeps
    // less than Doodlebold knows (the "!" marker).
    void markSaved(const QString& path, bool lostData);
    bool lostData() const { return m_lostData; }
    QString titleText() const;

    // Timelapse recording. Once started, every finished step that changes the composited picture (each undo step pushed, undo
    // and redo) stores the changed tiles, and while a stroke is drawn a frame every tenth of a second does the same
    // (captureStrokeFrame, a timer that runs only during a stroke); see TimelapseRecorder.
    bool isRecording() const { return m_recorder != nullptr; }
    void startRecording();
    bool isRecordingPaused() const;
    void setRecordingPaused(bool paused);
    void deleteRecording();
    int recordedSteps() const;
    std::int64_t recordedPaintingMs() const;
    std::int64_t recordingStartedMs() const;  // when it began, ms since the epoch; 0: not known
    std::int64_t recordedBytes() const;
    std::shared_ptr<const Timelapse> timelapse() const;
    void recordStep(TimelapseStep::Kind kind);
    static constexpr int kStrokeFrameMs = 100;
    void captureStrokeFrame();
    void dropStrokeFrames();

    // The animation (AnimationOps.h). Tracks, frames, holds, shared layers, sound clips and the video are undo steps; moving
    // the playhead, choosing a track and the timeline's view (onion skins) are not. The frame methods act on the current track.
    const Animation& animation() const { return m_state.animation; }
    int frameCount() const { return m_state.animation.frameCount(); }
    int currentFrame() const { return m_state.animation.currentFrame(); }
    int currentTrack() const { return m_state.animation.currentTrack; }
    int beat() const { return m_state.animation.beat; }
    EditResult<> goToFrame(int frame);
    EditResult<> goToBeat(int beat);
    EditResult<> goToCell(int track, int frame);
    EditResult<int> newFrame();
    EditResult<int> duplicateFrames(const std::vector<int>& frames);
    EditResult<> deleteFrames(const std::vector<int>& frames);
    EditResult<> keepOnlyCurrentFrame();
    EditResult<> changeHold(const std::vector<int>& frames, int delta);
    // Consecutive calls with the same non-zero mergeKey are one undo step (dragging a frame's right edge).
    EditResult<> setFrameHold(int frame, int hold, int mergeKey = 0);
    EditResult<> setFramesHold(const std::vector<int>& frames, int hold);
    EditResult<> moveFrames(const std::vector<int>& frames, int to);
    // Frames of several tracks at once; with the same non-zero mergeKey as the clips moved in the same drag, one undo step.
    EditResult<> moveFrameGroups(const std::vector<animation_ops::FrameMove>& moves, int mergeKey = 0);
    EditResult<int> addTrack();
    EditResult<int> duplicateTrack(int track);
    EditResult<> setPlayRange(int start, int end);
    EditResult<> deleteTrack(int track);
    EditResult<> deleteTracks(const std::vector<int>& tracks);
    EditResult<> setTrackLabel(int track, int label);
    EditResult<> clearTrack(int track);
    EditResult<> renameTrack(int track, const QString& name);
    EditResult<> moveTrack(int track, int to);
    EditResult<> setTrackHidden(int track, bool hidden);
    EditResult<> setTrackLocked(int track, bool locked);
    // Consecutive calls with the same non-zero mergeKey are one undo step (the volume slider's drag).
    EditResult<> setTrackVolume(int track, int volume, int mergeKey = 0);
    EditResult<> clearFrames(const std::vector<int>& frames);
    EditResult<> setLayerShared(LayerId id, bool shared);
    EditResult<> setPlayback(double fps, bool loop);
    EditResult<int> addSound(SoundClip clip);
    EditResult<> moveSound(int id, int startBeat, int track, int mergeKey = 0);
    EditResult<> trimSound(int id, int startBeat, double trimStart, double trimEnd, int mergeKey = 0);
    EditResult<> removeSound(int id);
    EditResult<> placeSounds(const std::vector<animation_ops::SoundPlace>& places, int mergeKey = 0);
    EditResult<> removeSounds(const std::vector<int>& ids);
    EditResult<> setSoundMuted(int id, bool muted);
    // Add Video: with `follow` the drawings follow the video (its rate, and frames up to its length).
    EditResult<> setVideo(const VideoReference& video, bool follow);
    EditResult<> removeVideo();
    // Consecutive calls with the same non-zero mergeKey are one undo step (the opacity slider's drag).
    EditResult<> setVideoOpacity(int opacity, int mergeKey = 0);
    void setTimelineView(const TimelineView& view);
    // While the animation plays, the onion skins are left out.
    void setPlaying(bool playing);
    bool isPlaying() const { return m_playing; }
    // The video picture under the current frame (canvas sized), from the timeline's cache; a null image: none.
    // `tiles`, when given, is the picture as tiles already (read on another thread), so it isn't converted here.
    void setVideoPicture(const QImage& picture, std::shared_ptr<const TiledImage> tiles = {});
    // Frame `frame` of the current track composited as the canvas shows it there without onion skins or video: with the other
    // tracks and the shared layers.
    QImage framePicture(int frame) const;
    // The same, quickly and roughly (each layer's thumbnail drawn at its opacity, blend modes left out), `height` pixels high.
    QImage cellThumbnail(int track, int frame, int height) const;
    // What makes that thumbnail, with a copy of the layers it's made of (their tiles shared, not copied), so it can be made on
    // another thread while the document goes on changing: cellThumbnail is cellThumbnailMaker(...)().
    std::function<QImage()> cellThumbnailMaker(int track, int frame, int height) const;
    LayerStack frameStack(int frame) const;

    EditResult<LayerId> addLayer();
    EditResult<LayerId> addFolder();
    EditResult<LayerId> duplicateLayer(LayerId id);
    EditResult<> deleteLayer(LayerId id);
    EditResult<> deleteLayers(const std::vector<LayerId>& ids);
    EditResult<> mergeDown(LayerId id);
    EditResult<LayerId> mergeLayers(const std::vector<LayerId>& ids);
    EditResult<> transferDown(LayerId id);   // Move Painting to Layer Below
    EditResult<LayerId> mergeVisible();      // Merge Visible Layers
    EditResult<LayerId> flattenImage();      // Flatten Image
    EditResult<> reverseCanvas(bool horizontal);  // Flip Canvas Horizontal / Vertical
    EditResult<> trimCanvas();                    // Trim Canvas by Selection (the canvas size changes: canvasResized)
    EditResult<> rotateCanvas(bool clockwise);    // Rotate Canvas 90deg. CW / CCW (the sides swap)
    EditResult<> moveLayer(LayerId id, LayerId parent, int index);
    EditResult<> moveLayers(const std::vector<LayerId>& ids, LayerId parent, int index);
    EditResult<> moveLayersExactly(const std::vector<LayerId>& ids, const QPoint& offset);
    EditResult<> raiseLayer(LayerId id);
    EditResult<> lowerLayer(LayerId id);
    EditResult<> layerToFront(LayerId id);
    EditResult<> layerToBack(LayerId id);
    // Consecutive calls with the same non-zero mergeKey on the same layer become one undo step (slider drags).
    EditResult<> setLayerProps(LayerId id, const LayerProps& props, int mergeKey = 0);
    EditResult<> clearLayer(LayerId id);
    EditResult<> clearLayers(const std::vector<LayerId>& ids);
    EditResult<> fillLayer(LayerId id, const QColor& color);
    // The Bucket: Fill, weighted by `area`'s coverage in place of the selection's (the caller clips it to the
    // selection), as one undo step. Preserve Opacity, Lock and the mask as target act as they do for Fill. NoChange for an
    // empty area.
    EditResult<> fillArea(LayerId id, const QColor& color, const SelectionMask& area);
    // A new raster layer holding pixels (canvas-sized) above the current layer; it becomes current. SizeMismatch for another
    // canvas size.
    EditResult<LayerId> addImageLayer(TiledImage pixels, const QString& name);
    // Consecutive setText calls with the same non-zero mergeKey on one layer are one undo step (typing).
    EditResult<LayerId> addTextLayer(const TextContent& content);
    EditResult<> setText(LayerId id, const TextContent& content, int mergeKey = 0);
    EditResult<> rasterizeLayer(LayerId id);
    EditResult<> addLayerMask(LayerId id);
    EditResult<> applyLayerMask(LayerId id);
    EditResult<> deleteLayerMask(LayerId id);

    // No value = nothing selected: the whole canvas can be edited. Painting, fill, erase, cut and transform act on the selected
    // part only, weighted by its coverage. An edit that would change nothing fails with EditError::NoChange.
    const std::optional<SelectionMask>& selection() const { return m_state.selection; }
    EditResult<> selectAll();
    EditResult<> deselect();
    EditResult<> invertSelection();
    // Combines shape (built with selection:: operations for this canvas size) with the selection; a result that selects nothing
    // deselects. undoText replaces the default step name.
    EditResult<> setSelection(SelectionMask shape, selection::CombineMode mode = selection::CombineMode::Replace,
                              const QString& undoText = {});
    EditResult<> dilateSelection(int pixels);
    EditResult<> erodeSelection(int pixels);
    // The layer's opacity as a selection (Ctrl+click on a thumbnail); a folder: its composite.
    EditResult<> selectionFromLayer(LayerId id, selection::CombineMode mode = selection::CombineMode::Replace);
    EditResult<> selectionFromMask(LayerId id, selection::CombineMode mode = selection::CombineMode::Replace);
    // The visible layers flattened over transparency: a source of the Magic Wand.
    TiledImage mergedImage() const;

    QRect transformBounds(LayerId id) const;
    EditResult<> transformLayer(LayerId id, const transform::Quad& destination, transform::Interpolation interpolation,
                                double perspective = 0.0);
    EditResult<> moveLayerExactly(LayerId id, transform::Turn turn, const QPoint& offset = {});

    EditResult<ClipboardImage> copy(LayerId id) const;
    EditResult<ClipboardImage> copyBounds(LayerId id) const;
    EditResult<ClipboardImage> cut(LayerId id);
    // Pastes as a new layer above the current one, at the picture's own position; it becomes current. Paste deselects;
    // `keepSelection` is "Paste without Deselecting".
    EditResult<LayerId> paste(const ClipboardImage& picture, bool keepSelection = false);

    EditResult<TiledImage*> paintTarget(LayerId id);
    // While a stroke runs, structural edits and undo/redo are refused (they would replace the tree being painted).
    void setPainting(bool painting);
    bool isPainting() const { return m_painting; }
    void notifyPixelsChanged(LayerId id, const QRect& rect);
    void commitPixelEdit(LayerId id, const TileMap& before, const QString& text);
    void restoreTiles(LayerId id, const TileMap& tiles);
    // Painting the target mask: the brush paints a grey picture of it (each pixel's mask value in all four channels); grey g
    // moves a value toward g and the Eraser toward 0 by the brush's coverage. Full areas share one tile. Every
    // reported change is copied into the mask so the canvas shows it during the stroke; the end records one undo step (or
    // restores the mask).
    EditResult<TiledImage*> maskPaintTarget(LayerId id);
    void notifyMaskPainted(LayerId id, const QRect& rect);
    void endMaskPaint(LayerId id, bool keep, const QString& text);

    const DocumentState& state() const { return m_state; }
    void restoreState(const DocumentState& s);

signals:
    void imageChanged(const QRect& rect);
    void layersChanged();
    void backgroundChanged();              // the canvas background changed (set, undone or redone); imageChanged follows
    void canvasResized(const QSize& size); // the canvas size changed (Trim, Rotate Canvas, or their undo); imageChanged follows
    void selectionChanged(const QRect& area);  // the selection changed; `area` is what a display must redraw
    void layerPixelsChanged(LayerId id);
    void paintingChanged(bool painting);
    void currentLayerChanged(LayerId id);
    void maskTargetChanged(bool mask);
    void modifiedChanged(bool modified);
    void displayNameChanged(const QString& name);
    void recordingChanged();
    void animationChanged();      // frames, holds, shared layers, sounds, video or the timeline's view
    void currentFrameChanged(int frame);  // the current track's frame, the playhead or the current track changed
    // A stroke's brush was kept to `used` instead of `chosen` to fit the memory there is, or it ran out and stopped partway at
    // `size` (StrokeController; sizes in tenths of a pixel, as BrushSettings').
    void strokeLimited(int chosen, int used);
    void strokeStopped(int size);

private:
    template <typename Op> EditResult<> movePlayhead(Op&& op);
    bool trackLocked(LayerId id) const;
    // A layer of a hidden track's frame, where an undone stroke's pixels go back without showing the track.
    LayerNode* restoreHidden(LayerId id);
    // Runs `op` on a copy of the state; on success installs the copy and pushes one undo step.
    template <typename Op> auto apply(const QString& text, Op&& op, int mergeKey = 0, LayerId mergeLayer = kNoLayer);
    void installState(DocumentState s);
    void invalidate(const QRect& rect);
    void pushUndo(QUndoCommand* command);
    void keepUndoWithinMemory();
    bool undoLetGo() const;
    void syncFrameTimer();
    void rebuildUnderlay();
    EditResult<ClipboardImage> copyWith(LayerId id, const SelectionMask& area) const;

    static int s_untitledCounter;
    DocumentState m_state;
    Projection m_projection;
    Projection m_display;                 // with the onion skins and video, while there are any
    std::vector<LayerNode> m_onions;
    std::optional<LayerNode> m_videoNode;
    std::pair<qint64, int> m_videoNodeKey{0, -1};  // the picture and opacity m_videoNode is of
    std::shared_ptr<const TiledImage> m_videoTiles;
    quint64 m_underlayKey = 0;  // the video picture and onion skins m_display was last composited with
    QImage m_videoPicture;
    bool m_playing = false;
    QUndoStack m_undo;
    ViewState m_view;
    QString m_filePath;
    QString m_sourceName;
    double m_resolution = kDefaultResolution;
    int m_untitledNumber = 0;
    bool m_lostData = false;
    bool m_painting = false;
    std::optional<TiledImage> m_maskPicture;  // while a stroke paints a mask
    DocumentState m_maskBefore;
    std::unique_ptr<TimelapseRecorder> m_recorder;
    QTimer* m_frameTimer = nullptr;  // ticks every kStrokeFrameMs while a stroke is drawn on a recording canvas
    int m_undoIndex = 0;     // the undo stack's index after the last change, to tell undo from redo
    bool m_pushing = false;  // a command is being pushed: its index change is not an undo or redo
};

}
