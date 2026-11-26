#pragma once

#include "core/layers/LayerId.h"
#include "core/document/Document.h"
#include "core/input/TabletStats.h"
#include "core/options/AppOptions.h"
#include "core/paint/StrokeController.h"
#include "core/tools/ToolBox.h"
#include "ui/tools/CanvasCursor.h"
#include "core/view/ViewNavigator.h"
#include "ui/FloatingLayerRenderer.h"
#include "ui/StrokeTimingReadout.h"
#include "ui/tools/CanvasTool.h"
#include "ui/tools/SelectionOverlay.h"

#include <vector>
#include <functional>
#include <QColor>
#include <QOpenGLBuffer>
#include <QOpenGLFunctions>
#include <QOpenGLShaderProgram>
#include <QOpenGLVertexArrayObject>
#include <deque>

#include <QElapsedTimer>
#include <QOpenGLWidget>
#include <QPointer>
#include <QWindow>
#include <QPolygonF>
#include <QRegion>
#include <QTimer>

#include <map>
#include <memory>
#include <optional>

namespace doodlebold {

class QuickShapePill;
class QuickShapeTool;

enum class PenButtonAction {
    None,
    Pan,
    Eyedropper
};

struct PenButtonMap {
    PenButtonAction barrel1 = PenButtonAction::Pan;
    PenButtonAction barrel2 = PenButtonAction::Eyedropper;
};

// GPU-displayed view of one Document: an OpenGL texture that receives only changed rectangles. Every tablet event
// becomes one InputSample for the brush (no coalescing).
class CanvasView : public QOpenGLWidget, protected QOpenGLFunctions, private ToolHost {
    Q_OBJECT
public:
    // Rotation step of Shift+PageUp/PageDown.
    static constexpr double kRotateStepDeg = 15.0;
    // Stride of Shift+arrow nudges in pixels.
    static constexpr int kLargeNudge = 10;
    static constexpr int kSettleMs = 250;
    // How long the pointer stays down and within kShapeStillPx for a stroke to become a shape.
    static constexpr int kShapeHoldMs = 1000;
    static constexpr double kShapeStillPx = 4.0;

    CanvasView(Document* doc, TabletStats* stats, QWidget* parent = nullptr);
    ~CanvasView() override;

    Document* document() const { return m_doc; }

    void setToolBox(ToolBox* tools);
    ToolBox* toolBox() const { return m_tools; }
    CanvasGlyph drawnCursor() const { return m_drawnGlyph; }
    // Whether the pen's own cursor is hidden, where the platform lets Doodlebold set it: while the pen is over this canvas.
    bool penCursorHidden() const { return m_penCursorHidden; }
    bool isPainting() const { return m_stroke.isActive(); }
    // Lets a stroke that's still painting after the pen lifted finish now, waiting if need be, before something needs the picture.
    void settleStroke();
    // Whether a stroke the pen has lifted from is still painting, for the view bench to wait for.
    bool strokeFinishing() const { return m_stroke.isFinishing(); }
    static void settleAllStrokes();
    bool isStrokeFinishing() const { return m_stroke.isFinishing(); }
    const CloneStamp& cloneStamp() const { return m_stroke.cloneStamp(); }

    bool selectionVisible() const { return m_selectionVisible; }
    void setSelectionVisible(bool visible);
    bool beginTransform(std::optional<TransformMode> mode = std::nullopt);
    bool hasTransform() const;
    TransformSession* transformSession() const { return m_session.get(); }
    void commitTransform();
    void cancelTransform();
    // Turns the running stroke into the shape it looks like, as the end of the hold does; false if it looks like none.
    bool snapHeldStroke();
    QuickShapeTool* shapeTool() const { return m_shapeTool.get(); }
    QuickShapePill* shapePill() const { return m_shapePill; }
    void acceptShape();
    void cancelShape();  // back to the stroke as drawn
    void nudgeLayer(const QPoint& offset);
    void nudgeSelection(const QPoint& offset);

    double zoom() const;
    double rotation() const;
    bool isFlipped() const;
    void setZoom(double zoom, const QPointF& anchor);
    void zoomBy(double factor, const QPointF& anchor);
    void zoomStep(int direction, const QPointF& anchor);
    void setRotation(double degrees);
    void rotateBy(double degrees);
    void toggleFlip();
    void resetPosition(bool fit);
    void resetAngle();
    void fitToWindow();
    void actualPixels();
    void scrollBy(const QPointF& widgetDelta);
    void resetView();
    void centreOn(const QPointF& docPoint);
    // The layers a held Ctrl or the Move tool moves together: those selected in the layer panel.
    void setSelectedLayers(std::function<std::vector<LayerId>()> selected);
    void setWheelZoom(bool swapDirection, bool atViewCentre);
    void setQuickShapes(bool enabled, int holdMs);
    void setPressureRange(PressureRange range) {
        m_pressureRange = range;
        m_stroke.setPressureRange(range);
    }
    // The keys held on the canvas (Options › Keys); `picks` and `moves` switch the pick and move keys on.
    void setHeldKeys(const HeldKeys& keys, bool picks, bool moves);
    // Options > Performance. None of them changes the picture, only how the canvas shows it.
    void setZoomedOut(AppOptions::ZoomedOut display);
    void setFastZoomedOut(bool fast) { setZoomedOut(fast ? AppOptions::ZoomedOut::Fast : AppOptions::ZoomedOut::Smooth); }
    bool fastZoomedOut() const { return m_fastZoomedOut; }  // as shown: by the graphics, where the choice is theirs
    // Whether the graphics a canvas found are software (llvmpipe and the like); unknown until a canvas has been shown.
    static std::optional<bool> softwareGraphics();
    static bool isSoftwareRenderer(const QString& renderer);
    // Left to the graphics, software ones show a canvas of at least this many pixels Fast, and smaller ones Smooth.
    static constexpr qint64 kSoftwareFastFromPixels = 64000000;  // 8000 × 8000
    static bool startsFast(bool software, const QSize& canvas);
    void setBrushOutline(bool shown);
    bool brushOutline() const { return m_brushOutline; }
    void setStrokeTiming(bool shown);
    bool strokeTiming() const { return m_strokeTiming; }
    // A shape that takes longer than this to repaint shows as a plain line while it changes; the brush paints it when it rests.
    static constexpr qint64 kShapeLiveRepaintMs = 12;
    void setShapeLiveRepaintLimit(qint64 ms) { m_shapeLiveRepaintLimit = ms; }
    bool isPreviewingShape() const { return m_shapePreviewing; }
    QPolygonF visibleDocumentArea() const;
    QPointF zoomAnchor() const;
    QPointF viewCentre() const;

    // One texture of the canvas's picture. A canvas wider or taller than the graphics allow in one texture is shown as several,
    // each holding the part it draws and a margin round it, so that sampling at a cut sees the pixels on both sides as one
    // texture would.
    struct TexturePart {
        GLuint id = 0;
        QRect held;   // the canvas pixels the texture holds
        QRect shown;  // those it draws
        bool mipmapsStale = true;
    };
    static constexpr int kTextureMargin = 256;
    static constexpr int kTextureAlign = 512;  // a cut falls on a multiple of this, so mipmap pixels line up across it
    // How a canvas is cut for textures at most `largest` pixels on a side: a single part where it fits.
    static std::vector<TexturePart> textureLayout(const QSize& canvas, int largest);
    // Takes `px` as the graphics' largest texture where it is smaller (0: the graphics' own), so tests can cut a canvas.
    static void setLargestTextureForTesting(int px);

signals:
    void strokeHasNews();  // from the stroke's own thread
    void viewChanged();
    void paintRefused(const QString& reason);
    void strokePainted(const QColor& colour);
    void message(const QString& text);
    void transformChanged();

protected:
    void initializeGL() override;
    void paintGL() override;
    void resizeGL(int w, int h) override;

    void tabletEvent(QTabletEvent* e) override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;
    void mouseDoubleClickEvent(QMouseEvent* e) override;
    void wheelEvent(QWheelEvent* e) override;
    void keyPressEvent(QKeyEvent* e) override;
    void keyReleaseEvent(QKeyEvent* e) override;
    void resizeEvent(QResizeEvent* e) override;
    void enterEvent(QEnterEvent* e) override;
    void focusOutEvent(QFocusEvent* e) override;
    bool eventFilter(QObject* watched, QEvent* e) override;
    void leaveEvent(QEvent* e) override;
    bool event(QEvent* e) override;

private:
    enum class Mode {
        Idle,
        Painting,
        Panning,
        Picking,
        Zooming,
        Rotating,
        Tool
    };
    enum class HeldGesture {
        None,
        Pick,
        Move,
        Navigation
    };  // what a held key is doing, which ends with the key

    Document* toolDocument() override { return m_doc; }
    ToolBox* toolBox() override { return m_tools; }
    double docPerPixel() const override { return 1.0 / zoom(); }
    void toolChanged() override;
    void toolMessage(const QString& text) override { emit message(text); }
    TransformSession* session() override { return m_session.get(); }
    bool startMoveSession() override;
    void finishSession(bool commit) override;
    PressureRange pressureRange() const override { return m_pressureRange; }

    ToolHost& asHost() { return *this; }
    CanvasTool* activeTool() const;
    void connectDocument(Document* doc);
    void syncTool();
    bool beginTool(const InputSample& s, Qt::KeyboardModifiers mods, const QPointF& widgetPos);
    ToolPointer pointer(const InputSample& s, const QPointF& widgetPos, Qt::KeyboardModifiers mods) const;
    void endSession();
    void pullSessionDirty();
    // The GPU draws a transform's lifted pixels from one texture of its box, so it can while the box fits in one.
    bool canFloat(const TransformSession& session) const;
    const SelectionMask* displayedSelection() const;
    bool selectionShownAsArea() const;
    void takeKeyboardFocus();
    // In the workspace a canvas window is made current only by clicking it: hovering one that is not current
    // leaves it alone, and the click that makes it current does not paint.
    bool isCurrentView() const;
    void makeCurrentView();
    void trackModifiers(Qt::KeyboardModifiers mods, int key, bool pressed);
    void showCursor(const QCursor& system, CanvasGlyph glyph);
    void setPenOverride(bool on, Qt::CursorShape shape);
    void setPenActive(bool pen);
    void syncAnts();
    bool showsBrushOutline() const;
    bool isSelectionToolActive() const;
    bool isCloneToolActive() const;
    void paintCloneCrosshair(QPainter& painter);
    void paintStrokeTiming(QPainter& painter);

    ViewNavigator navigator() const;
    QTransform docToWidget() const { return navigator().docToWidget(); }
    QTransform widgetToDoc() const { return navigator().widgetToDoc(); }
    void viewWasChanged();
    void beginStroke(const InputSample& s);
    void drawIfOverdue();
    void askForFrameIfUnshown();
    void noteStrokeFrameCost();
    void strokeProgress();
    void completeStroke();
    void startNextHeldStroke();
    void startFinishing(const BrushSettings& settings);
    void showStrokePaint();
    void continueStroke(const InputSample& s);
    void endStroke();
    void endPenPress(const InputSample& s, const QTabletEvent& e);
    void notePaintedColour(bool painted);
    void moveHeldShape(const InputSample& s);
    void repaintShape();
    void shapeChanged();
    void paintShapePreview(QPainter& painter);
    bool snapsToShapes() const;
    void syncShapePill();
    void placeShapePill();
    void endShape();
    void pickColor(const QPointF& docPos);
    bool beginGesture(Qt::MouseButton button, Qt::KeyboardModifiers mods, const QPointF& widgetPos, const InputSample& s);
    void startGesture(PenButtonAction action, const QPointF& widgetPos, const InputSample& s);
    void moveGesture(const QPointF& widgetPos, const InputSample& s);
    void movePointer(const InputSample& s, QPointF position, Qt::KeyboardModifiers modifiers);
    void onImageChanged(const QRect& rect);
    void onCanvasResized();
    // Brings the full texture up to date, over `area` only if it's valid.
    void uploadDirty(const QRect& area = QRect());
    // Zoomed out by 2^level or more, the canvas shows a texture of the picture at that scale, kept up to date apart from the full
    // one, which catches up when it's needed again.
    int reducedLevel() const;
    bool reducedFits(int level) const;  // the canvas at that scale fits one texture
    // While nothing else goes on, the textures not shown, the full one and every reduced level, catch up a band at a time, so a zoom
    // only switches between pictures already there.
    void catchUpView();
    std::optional<int> levelBehind() const;  // the one nearest the shown level that has catching up to do; 0 is the full texture
public:
    // For the view bench: turns the reduced texture off, to compare with it, or on again.
    static void setReducedViewForTesting(bool on);

private:
    void uploadReduced(int level, const QRect& area = QRect());
    // Mipmaps of a picture drawn zoomed out: the display texture's are rebuilt when stale, the floating preview's are
    // built at upload, the selection tint has none (it changes with every pen event).
    enum class Mipmaps {
        Display,
        Prebuilt,
        None
    };
    void drawPicture(std::vector<TexturePart>& parts, const QMatrix4x4& docToClip, Mipmaps mipmaps);
    void drawPicture(GLuint texture, const QMatrix4x4& docToClip, Mipmaps mipmaps);
    // Limits drawing to the screen pixels round `docRect`, the viewport being `viewport`.
    void scissorTo(const QRect& docRect, const QMatrix4x4& docToClip, const GLint viewport[4]);
    // With `mipmapped`, every mipmap level is made at once, so building them later only fills them in.
    std::vector<TexturePart> makeTextures(bool mipmapped);
    void deleteTextures(std::vector<TexturePart>& parts);
    // Puts the pixels of `area` into every texture holding some of it; `bits` is the canvas pixel at `origin`.
    void uploadToParts(std::vector<TexturePart>& parts, const QRect& area, const uchar* bits, qsizetype bytesPerLine, const QPoint& origin);
    void drawSelectionTint(const QMatrix4x4& docToClip);
    void releaseGL();
    void settleSessionPreview();
    void startSession(std::shared_ptr<TransformSession> session);
    void updateCursor();
    bool holdsMove(Qt::KeyboardModifiers mods) const;
    bool holdsPick(Qt::KeyboardModifiers mods) const;
    void endHeldGesture(int key);
    Qt::KeyboardModifiers heldModifiers() const;
    static bool isHeldModifierKey(int key);
    void forgetHeldGesture();

    QPointer<Document> m_doc;
    TabletStats* m_stats;
    StrokeController m_stroke;
    ToolBox m_ownTools;
    QPointer<ToolBox> m_tools = &m_ownTools;
    PenButtonMap m_buttons;

    std::unique_ptr<CanvasTool> m_tool;
    std::unique_ptr<CanvasTool> m_transformTool;
    std::shared_ptr<TransformSession> m_session;
    QRect m_sessionTouched;
    bool m_committing = false;
    // While a handle is dragged the lifted pixels are drawn by the GPU; the CPU preview catches up when the drag pauses
    // for kSettleMs or ends.
    FloatingLayerRenderer m_floating;
    QTimer m_settleTimer;
    bool m_unloadFloating = false;
    SelectionOverlay m_overlay;
    SelectionTint m_tint;
    std::unique_ptr<QuickShapeTool> m_shapeTool;
    QuickShapePill* m_shapePill = nullptr;
    QTimer m_shapeHold;
    QTimer m_shapeRepaint;  // repaints of the shape wait for the next frame, so a slow brush does not hold up the pointer
    QPointF m_stillAt;
    bool m_shapeMoved = false;
    QTimer m_shapeSettle;
    bool m_shapePreviewing = false;
    qint64 m_shapeRepaintCost = 0;
    qint64 m_shapeLiveRepaintLimit = kShapeLiveRepaintMs;
    bool m_quickShapes = true;
    HeldKeys m_held;
    bool m_altPicks = true;   // m_held.pick picks a colour
    bool m_ctrlMoves = true;  // m_held.move moves the selected layers
    bool m_selectionVisible = true;
    Qt::KeyboardModifiers m_modifiers;
    bool m_penActive = false;
    bool m_overrideCursor = false;
    Qt::CursorShape m_overrideShape = Qt::ArrowCursor;
    bool m_penCursorHidden = false;
    AppOptions::ZoomedOut m_zoomedOut = AppOptions::ZoomedOut::ByGraphics;
    std::optional<bool> m_graphicsChoseFast;  // decided when the canvas first shows, by its size then; kept if it's resized
    bool m_fastZoomedOut = false;
    PressureRange m_pressureRange;  // Options', for the tools that paint strokes of their own
    bool m_brushOutline = true;
    bool m_strokeTiming = false;
    StrokeTimingReadout m_timing;
    std::int64_t m_timedStampUs = 0;  // the pen event the readout last timed a frame for
    CanvasGlyph m_drawnGlyph = CanvasGlyph::None;
    QTimer m_antsTimer;
    int m_antPhase = 0;

    Mode m_mode = Mode::Idle;
    HeldGesture m_heldGesture = HeldGesture::None;
    Qt::KeyboardModifiers m_heldKeys;
    std::unique_ptr<CanvasTool> m_heldTool;
    std::function<std::vector<LayerId>()> m_selectedLayers;
    bool m_spaceHeld = false;  // the pan key (m_held.pan, Space at the start) is held
    bool m_penInProximity = false;
    // Tablet hover does not make the widget "under the mouse" on every platform (Wayland), so the brush outline tracks
    // the pen separately.
    bool m_penOver = false;
    int m_wheelRemainder = 0;  // wheel delta short of a whole notch (1/8 degree units)
    bool m_wheelSwap = false;
    bool m_wheelAtCentre = false;
    bool pointerOver() const { return underMouse() || (m_penInProximity && m_penOver); }
    bool m_tabletStrokeActive = false;  // suppress synthesized mouse events while a tablet stroke runs
    QPointF m_lastWidgetPos;
    QPointF m_gestureStartPos;
    double m_gestureStartZoom = 1.0;
    double m_gestureStartAngle = 0.0;
    QPointF m_hoverWidgetPos;
    double m_hoverPressure = 0.0;

    std::unique_ptr<QOpenGLShaderProgram> m_program;
    QOpenGLVertexArrayObject m_vao;
    QOpenGLBuffer m_vbo{QOpenGLBuffer::VertexBuffer};
    std::vector<TexturePart> m_texture;
    struct ReducedTexture {
        std::vector<TexturePart> parts;  // one part: the whole canvas, held at 1 / 2^level
        QRegion dirty;  // canvas pixels changed since it last had them
    };
    // By level: each one shown so far, kept and brought up to date when it's shown again, so a zoom only switches between them.
    std::map<int, ReducedTexture> m_reducedTextures;
    QTimer m_viewCatchUp;
    void reducedChanged(const QRegion& area);
    std::vector<TexturePart> m_tintTexture;
    int m_largestTexture = 0;
    QElapsedTimer m_waitingForFrame;  // since a change that hasn't been drawn yet
    bool m_showingStroke = false;  // inside a frame, which doesn't need to ask for another
    // A stroke begun while the last one is still painting waits here, with its pen events, so nothing the pen does is lost.
    struct HeldStroke {
        QColor color;
        BrushSettings settings;
        int stabilizerLevel = 0;
        std::vector<InputSample> samples;
        bool lifted = false;
    };
    std::deque<HeldStroke> m_heldStrokes;
    BrushSettings m_finishingSettings;
    QElapsedTimer m_finishingFor;
    QElapsedTimer m_frameClock;
    QTimer m_strokeFramePacer;
    QTimer m_timingHeartbeat;
    qint64 m_nextStrokeFrameMs = 0;  // by m_frameClock
    qint64 m_strokeFrameStartedNs = -1;
    QRegion m_dirty;
    bool m_glReady = false;
    QString m_glError;  // set when the canvas cannot be displayed; shown in place of the canvas
};

}
