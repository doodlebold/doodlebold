#include "ui/CanvasView.h"

#include "platform/PenCursor.h"

#include "core/document/Document.h"
#include "core/document/TransformSession.h"
#include "core/input/InputLog.h"
#include "core/stroke/TimingLog.h"
#include "core/paint/QuickShape.h"
#include "core/util/WorkerPool.h"
#include "ui/tools/BucketTool.h"
#include "ui/tools/CanvasCursor.h"
#include "ui/tools/QuickShapePill.h"
#include "ui/tools/QuickShapeTool.h"
#include "ui/TabletInput.h"
#include "ui/Theme.h"
#include "ui/tools/SelectionPenTool.h"
#include "ui/tools/SelectionTools.h"
#include "ui/tools/TextTool.h"
#include "ui/tools/TransformTools.h"

#include <QElapsedTimer>
#include <QApplication>
#include <QLineEdit>
#include <QMdiArea>
#include <QMdiSubWindow>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QLineF>
#include <QMatrix4x4>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPointingDevice>
#include <QShortcutEvent>
#include <QTabletEvent>
#include <QWheelEvent>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <limits>
#include <numbers>
#include <optional>
#include <utility>

namespace doodlebold {

namespace {

constexpr qint64 kLongestFrameWaitMs = 50;  // past this, a change is drawn at once, with pen events still waiting
constexpr qint64 kStrokeFrameRest = 2;  // a frame showing a stroke leaves this many times its own cost free before the next
// The view's catch-up (catchUpView) starts this long after the last frame, so it waits while the view is busy, and goes on a band
// at a time, kCatchUpNextMs apart, each band kCatchUpRows rows of the full texture, or as many output rows at a reduced level.
constexpr int kCatchUpAfterMs = 100;
constexpr int kCatchUpNextMs = 10;
constexpr int kCatchUpRows = 512;  // texture rows, about 30 MB at 16000 px across
constexpr int kMostReducedLevel = 6;  // a tile is 2^6 px a side: no block past it
// A texture of w x h, with room for all its mipmaps if `mipmapped`: a driver otherwise makes room for them when they're first built,
// copying the whole texture.
void allocateTexture(int width, int height, bool mipmapped) {
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    for (int level = 1; mipmapped && (width > 1 || height > 1); ++level) {
        width = std::max(1, width / 2);
        height = std::max(1, height / 2);
        glTexImage2D(GL_TEXTURE_2D, level, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    }
}

std::vector<CanvasView*> s_viewsFinishing;  // those with a stroke still painting after the pen lifted

// A quad over the whole canvas, whichever part of it a texture holds, so its edges fall on the same screen pixels however the
// canvas is cut; the fragment shader keeps a texture's draw to the part it shows.
const char* kVertexShader = R"(#version 330 core
layout(location = 0) in vec2 corner;
uniform mat4 docToClip;
uniform vec2 docSize;
uniform vec2 heldOrigin;
uniform vec2 heldSize;
out vec2 uv;
out vec2 docPos;
void main() {
    docPos = corner * docSize;
    uv = (docPos - heldOrigin) / heldSize;
    gl_Position = docToClip * vec4(docPos, 0.0, 1.0);
})";

// Premultiplied RGBA, blended with ONE / ONE_MINUS_SRC_ALPHA. Magnified (zoom >= 100 %), a screen pixel shows the
// average of the canvas pixels it covers: the sample point is moved so that linear filtering blends
// two texels only across the fraction of the screen pixel that straddles their border.
const char* kFragmentShader = R"(#version 330 core
in vec2 uv;
in vec2 docPos;  // the same in every texture's draw, so each screen pixel at a cut is drawn by one texture
uniform sampler2D image;
uniform vec2 heldSize;
uniform vec2 shownFrom;
uniform vec2 shownTo;
uniform bool magnify;
out vec4 color;
void main() {
    if (any(lessThan(docPos, shownFrom)) || any(greaterThanEqual(docPos, shownTo)))
        discard;
    if (!magnify) {
        color = texture(image, uv);
        return;
    }
    vec2 t = uv * heldSize - 0.5;
    vec2 perPixel = max(fwidth(t), vec2(1e-6));  // canvas pixels per screen pixel
    vec2 c = floor(t) + 0.5 + clamp((fract(t) - 0.5) / perPixel + 0.5, 0.0, 1.0);
    color = texture(image, c / heldSize);
})";

int s_largestTextureForTesting = 0;
std::optional<bool> s_softwareGraphics;  // what the last canvas to start its graphics found

// Where a side `length` pixels long is cut, as [from, to) spans, so that each span with a margin either side fits in `largest`.
std::vector<std::pair<int, int>> textureSpans(int length, int largest) {
    if (length <= largest)
        return {{0, length}};
    constexpr int align = CanvasView::kTextureAlign;
    const int most = std::max(align, (largest - 2 * CanvasView::kTextureMargin) / align * align);
    const int count = (length + most - 1) / most;
    const int each = ((length + count - 1) / count + align - 1) / align * align;
    std::vector<std::pair<int, int>> spans;
    for (int from = 0; from < length; from += each)
        spans.emplace_back(from, std::min(length, from + each));
    return spans;
}

QColor workspace() {
    return Theme::color(Theme::Token::Window);
}

// The view fills the window below its header, so it rounds its own bottom corners to match the frame's outline
// (DoodleboldStyle): the workspace outside the curve, the outline along it, 1 px outside the view as the frame draws
// it.
void paintCardCorners(QPainter& painter, const QSize& size) {
    const double radius = Theme::metric(Theme::Metric::CardRadius);
    const QRectF outline(-0.5, -2 * radius, size.width() + 1.0, size.height() + 2 * radius + 1.0);
    QPainterPath card;
    card.addRoundedRect(outline, radius, radius);
    QPainterPath corners;
    corners.addRect(0, size.height() - radius - 1, radius + 1, radius + 1);
    corners.addRect(size.width() - radius - 1, size.height() - radius - 1, radius + 1, radius + 1);
    painter.save();
    painter.resetTransform();
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setClipPath(corners);
    painter.fillPath(corners.subtracted(card), Theme::color(Theme::Token::Window));
    painter.setPen(Theme::color(Theme::Token::Border));
    painter.setBrush(Qt::NoBrush);
    painter.drawPath(card);
    painter.restore();
}

QMdiSubWindow* subWindowOf(const QWidget* w) {
    for (QWidget* p = w->parentWidget(); p; p = p->parentWidget())
        if (auto* window = qobject_cast<QMdiSubWindow*>(p))
            return window;
    return nullptr;
}

// Tools that draw a line can snap it to a shape.
bool drawsLines(ToolType tool) {
    switch (tool) {
    case ToolType::Pencil:
    case ToolType::Brush:
    case ToolType::WaterColor:
    case ToolType::Marker:
    case ToolType::Eraser:
    case ToolType::PixelArtPen: return true;
    default: return false;
    }
}

bool isRealMouse(const QMouseEvent* e) {
    const QPointingDevice* d = e->pointingDevice();
    return !d || d->type() == QInputDevice::DeviceType::Mouse || d->type() == QInputDevice::DeviceType::TouchPad;
}

}

CanvasView::CanvasView(Document* doc, TabletStats* stats, QWidget* parent)
    : QOpenGLWidget(parent), m_doc(doc), m_stats(stats), m_stroke(doc) {
    setAttribute(Qt::WA_TabletTracking);
    qApp->installEventFilter(this);
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    connectDocument(doc);
    // A canvas opened while the pen is already in range knows it is there.
    m_penInProximity = stats && stats->inProximity();
    connect(stats, &TabletStats::proximityChanged, this, [this](bool in) {
        m_penInProximity = in;
        updateCursor();
        update();
    });
    m_antsTimer.setInterval(100);
    connect(&m_antsTimer, &QTimer::timeout, this, [this] {
        m_antPhase = (m_antPhase + 1) % kAntPeriod;
        update();
    });
    m_settleTimer.setSingleShot(true);
    m_settleTimer.setInterval(kSettleMs);
    connect(&m_settleTimer, &QTimer::timeout, this, &CanvasView::settleSessionPreview);
    m_shapeHold.setSingleShot(true);
    m_shapeHold.setInterval(kShapeHoldMs);
    connect(&m_shapeHold, &QTimer::timeout, this, &CanvasView::snapHeldStroke);
    m_shapeRepaint.setSingleShot(true);
    m_shapeRepaint.setInterval(16);
    connect(&m_shapeRepaint, &QTimer::timeout, this, &CanvasView::repaintShape);
    m_shapeSettle.setSingleShot(true);
    m_shapeSettle.setInterval(150);
    connect(&m_shapeSettle, &QTimer::timeout, this, &CanvasView::repaintShape);
    m_transformTool = std::make_unique<TransformTool>(asHost());
    m_dirty = QRegion(doc->rect());
    m_viewCatchUp.setSingleShot(true);
    connect(&m_viewCatchUp, &QTimer::timeout, this, &CanvasView::catchUpView);
    syncAnts();
}

void CanvasView::connectDocument(Document* doc) {
    connect(doc, &Document::imageChanged, this, &CanvasView::onImageChanged);
    connect(this, &QOpenGLWidget::frameSwapped, this, [] { timing_log::frameSwapped(); });
    connect(this, &CanvasView::strokeHasNews, this, &CanvasView::strokeProgress, Qt::QueuedConnection);
    m_frameClock.start();
    m_strokeFramePacer.setSingleShot(true);
    connect(&m_strokeFramePacer, &QTimer::timeout, this, &CanvasView::askForFrameIfUnshown);
    if (timing_log::isOn()) {
        m_timingHeartbeat.setInterval(5);
        connect(&m_timingHeartbeat, &QTimer::timeout, this, [] { timing_log::mainThreadTick(); });
        m_timingHeartbeat.start();
    }
    connect(doc, &Document::canvasResized, this, &CanvasView::onCanvasResized);
    connect(doc, &Document::selectionChanged, this, [this] {
        m_overlay.invalidate();
        m_tint.invalidate();
        syncAnts();
        update();
    });
    const auto structureChanged = [this] {
        if (m_session && !m_committing)
            cancelTransform();
    };
    connect(doc, &Document::layersChanged, this, structureChanged);
    connect(doc, &Document::currentLayerChanged, this, structureChanged);
}

CanvasView::~CanvasView() {
    setPenOverride(false, Qt::ArrowCursor);
    if (m_penCursorHidden)
        platform::pen_cursor::set(Qt::ArrowCursor);
    if (m_tool)
        m_tool->cancel();
    settleStroke();
    if (m_shapeTool) {
        repaintShape();
        m_stroke.finish();
        m_shapeTool.reset();
    }
    m_session.reset();
    releaseGL();
    // ~QOpenGLWidget deletes the context after this; its aboutToBeDestroyed must not reach releaseGL on a gone CanvasView.
    if (QOpenGLContext* c = context())
        c->disconnect(this);
}

// Trim / Rotate Canvas (or their undo): the display texture, the quad and the tint are sized by the canvas, so they are
// made afresh; the view keeps its zoom and angle and centres the new canvas.
void CanvasView::onCanvasResized() {
    if (m_session && !m_committing)
        cancelTransform();
    m_stroke.cloneStamp().reset();  // the source point belonged to the old canvas
    if (m_glReady || m_program) {
        releaseGL();
        if (context()) {
            makeCurrent();
            initializeGL();
            doneCurrent();
        }
    }
    m_dirty = QRegion(m_doc->rect());
    m_reducedTextures.clear();
    m_overlay.invalidate();
    m_tint.invalidate();
    syncAnts();
    m_doc->view().pan = QPointF();
    emit viewChanged();
    update();
}

void CanvasView::releaseGL() {
    if (!m_program && !m_glReady)
        return;
    makeCurrent();
    // A canvas that was hidden can have lost its context already (the widget releases it); then there is nothing to
    // delete and calling GL would crash. Another canvas's context may still be current, so check it is ours.
    if (context() && QOpenGLContext::currentContext() == context()) {
        deleteTextures(m_texture);
        for (auto& [level, reduced] : m_reducedTextures)
            deleteTextures(reduced.parts);
        deleteTextures(m_tintTexture);
        m_vbo.destroy();
        m_vao.destroy();
        m_floating.release();
    } else {
        m_floating.release(false);
    }
    m_program.reset();
    m_texture.clear();
    m_reducedTextures.clear();
    m_tintTexture.clear();
    m_glReady = false;
    doneCurrent();
}

ViewNavigator CanvasView::navigator() const {
    return ViewNavigator(m_doc->view(), QSizeF(size()), QSizeF(m_doc->size()));
}

void CanvasView::viewWasChanged() {
    emit viewChanged();
    update();
}

double CanvasView::zoom() const {
    return m_doc->view().zoom;
}

double CanvasView::rotation() const {
    return m_doc->view().rotationDeg;
}

bool CanvasView::isFlipped() const {
    return m_doc->view().flipH;
}

void CanvasView::setZoom(double z, const QPointF& anchor) {
    navigator().zoomTo(z, anchor);
    viewWasChanged();
}

void CanvasView::zoomBy(double factor, const QPointF& anchor) {
    navigator().zoomBy(factor, anchor);
    viewWasChanged();
}

void CanvasView::zoomStep(int direction, const QPointF& anchor) {
    setZoom(ViewNavigator::nextLadderZoom(zoom(), direction), anchor);
}

void CanvasView::setRotation(double degrees) {
    ViewNavigator nav = navigator();
    nav.rotateTo(degrees, nav.centre());
    viewWasChanged();
}

void CanvasView::rotateBy(double degrees) {
    setRotation(rotation() + degrees);
}

void CanvasView::toggleFlip() {
    ViewNavigator nav = navigator();
    nav.flip(nav.centre());
    viewWasChanged();
}

void CanvasView::resetPosition(bool fit) {
    navigator().home(fit);
    viewWasChanged();
}

void CanvasView::resetAngle() {
    setRotation(0.0);
}

void CanvasView::fitToWindow() {
    ViewNavigator nav = navigator();
    nav.resetOrientation();
    nav.fit();
    viewWasChanged();
}

void CanvasView::actualPixels() {
    ViewNavigator nav = navigator();
    nav.zoomTo(1.0, nav.centre());
    viewWasChanged();
}

void CanvasView::scrollBy(const QPointF& widgetDelta) {
    navigator().scrollBy(widgetDelta);
    viewWasChanged();
}

void CanvasView::resetView() {
    m_doc->view() = ViewState{};
    ViewNavigator nav = navigator();
    nav.fit();
    if (zoom() > 1.0)
        nav.zoomTo(1.0, nav.centre());
    viewWasChanged();
}

void CanvasView::centreOn(const QPointF& docPoint) {
    navigator().centreOn(docPoint);
    viewWasChanged();
}

QPolygonF CanvasView::visibleDocumentArea() const {
    return navigator().visibleDocumentArea();
}

QPointF CanvasView::zoomAnchor() const {
    return pointerOver() ? m_hoverWidgetPos : viewCentre();
}

void CanvasView::setWheelZoom(bool swapDirection, bool atViewCentre) {
    m_wheelSwap = swapDirection;
    m_wheelAtCentre = atViewCentre;
    m_wheelRemainder = 0;
}

QPointF CanvasView::viewCentre() const {
    return navigator().centre();
}

void CanvasView::initializeGL() {
    // Qt calls this again when it gives the widget a new context (it moved to another window): every GL object is made
    // afresh; the old ones were freed with the old context (releaseGL).
    initializeOpenGLFunctions();
    m_glReady = false;
    m_glError.clear();
    connect(context(), &QOpenGLContext::aboutToBeDestroyed, this, &CanvasView::releaseGL,
            Qt::ConnectionType(Qt::UniqueConnection | Qt::DirectConnection));
    m_program = std::make_unique<QOpenGLShaderProgram>();
    if (!m_program->addShaderFromSourceCode(QOpenGLShader::Vertex, kVertexShader) ||
        !m_program->addShaderFromSourceCode(QOpenGLShader::Fragment, kFragmentShader) || !m_program->link()) {
        m_glError = tr("The canvas could not be displayed: OpenGL shader error.\n%1").arg(m_program->log());
        qWarning("CanvasView: %s", qPrintable(m_glError));
        return;
    }
    s_softwareGraphics = isSoftwareRenderer(QString::fromLatin1(reinterpret_cast<const char*>(glGetString(GL_RENDERER))));
    if (!m_graphicsChoseFast)
        m_graphicsChoseFast = startsFast(*s_softwareGraphics, m_doc->size());
    setZoomedOut(m_zoomedOut);
    GLint maxTexture = 0;
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maxTexture);
    m_largestTexture = s_largestTextureForTesting > 0 ? std::min<int>(maxTexture, s_largestTextureForTesting) : maxTexture;
    // The transform's preview on the graphics holds the whole canvas in one texture; past the limit the CPU's preview shows it.
    const bool oneTexture = textureLayout(m_doc->size(), m_largestTexture).size() == 1;
    if (oneTexture && !m_floating.initialize())
        qWarning("CanvasView: floating layer shader: %s", qPrintable(m_floating.log()));

    const float quad[] = {0, 0, 1, 0, 0, 1, 1, 1};
    m_vao.create();
    m_vao.bind();
    m_vbo.create();
    m_vbo.bind();
    m_vbo.allocate(quad, sizeof(quad));
    m_program->enableAttributeArray(0);
    m_program->setAttributeBuffer(0, GL_FLOAT, 0, 2);
    m_vao.release();

    m_tintTexture.clear();
    m_texture = makeTextures(true);
    m_dirty = QRegion(m_doc->rect());
    m_reducedTextures.clear();
    m_glReady = true;
    // From here on there are frames to show it. The first runs on the stroke's thread, so it only leaves a message for this one.
    m_stroke.paintInBackground([this] { emit strokeHasNews(); },
                               [this] {
                                   if (m_stroke.hasUnshownPaint())
                                       repaint();
                               });
}

void CanvasView::resizeGL(int, int) {}

std::vector<CanvasView::TexturePart> CanvasView::textureLayout(const QSize& canvas, int largest) {
    std::vector<TexturePart> parts;
    const QRect whole(QPoint(0, 0), canvas);
    for (const auto& [top, bottom] : textureSpans(canvas.height(), largest))
        for (const auto& [left, right] : textureSpans(canvas.width(), largest)) {
            TexturePart part;
            part.shown = QRect(QPoint(left, top), QPoint(right - 1, bottom - 1));
            part.held = part.shown.adjusted(-kTextureMargin, -kTextureMargin, kTextureMargin, kTextureMargin) & whole;
            parts.push_back(part);
        }
    return parts;
}

void CanvasView::setLargestTextureForTesting(int px) {
    s_largestTextureForTesting = px;
}

std::vector<CanvasView::TexturePart> CanvasView::makeTextures(bool mipmapped) {
    std::vector<TexturePart> parts = textureLayout(m_doc->size(), m_largestTexture);
    for (TexturePart& part : parts) {
        glGenTextures(1, &part.id);
        glBindTexture(GL_TEXTURE_2D, part.id);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        allocateTexture(part.held.width(), part.held.height(), mipmapped);
    }
    return parts;
}

void CanvasView::deleteTextures(std::vector<TexturePart>& parts) {
    for (TexturePart& part : parts)
        glDeleteTextures(1, &part.id);
    parts.clear();
}

void CanvasView::uploadToParts(std::vector<TexturePart>& parts, const QRect& area, const uchar* bits, qsizetype bytesPerLine,
                               const QPoint& origin) {
    for (TexturePart& part : parts) {
        const QRect c = area & part.held;
        if (c.isEmpty())
            continue;
        glBindTexture(GL_TEXTURE_2D, part.id);
        glTexSubImage2D(GL_TEXTURE_2D, 0, c.x() - part.held.x(), c.y() - part.held.y(), c.width(), c.height(), GL_RGBA, GL_UNSIGNED_BYTE,
                        bits + (c.y() - origin.y()) * bytesPerLine + (c.x() - origin.x()) * 4);
        part.mipmapsStale = true;
    }
}

void CanvasView::uploadDirty(const QRect& area) {
    const QRegion dirty = area.isValid() ? m_dirty.intersected(area) : m_dirty;
    if (dirty.isEmpty())
        return;
    pullSessionDirty();
    QElapsedTimer timed;
    timed.start();
    const QImage& img = m_session ? m_session->image() : m_doc->displayImage();
    timing_log::note("composite", timed.nsecsElapsed() / 1000);
    timed.restart();
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, static_cast<GLint>(img.bytesPerLine() / 4));
    for (const QRect& r : dirty)
        uploadToParts(m_texture, r & img.rect(), img.constBits(), img.bytesPerLine(), QPoint(0, 0));
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    timing_log::note("upload", timed.nsecsElapsed() / 1000);
    m_dirty -= dirty;
}

// How far out the view is, as 2^level with at least 2 canvas pixels to a screen pixel, where the reduced texture can show it;
// 0 when it's not that far out, or the picture is more than the layers (a transform, an underlay, the reduced texture too big).
namespace {
bool s_reducedView = true;
}

void CanvasView::setReducedViewForTesting(bool on) {
    s_reducedView = on;
}

int CanvasView::reducedLevel() const {
    constexpr double kReducedFromZoom = 0.5;  // at this zoom or further out, at least 2 canvas pixels to a screen pixel
    const double zoom = m_doc->view().zoom;
    if (zoom <= 0.0 || zoom > kReducedFromZoom)
        return 0;
    const int level = std::min(kMostReducedLevel, static_cast<int>(std::floor(std::log2(1.0 / zoom))));
    return reducedFits(level) ? level : 0;
}

bool CanvasView::reducedFits(int level) const {
    if (!s_reducedView || m_session || m_doc->hasUnderlay())
        return false;
    const int block = 1 << level;
    return (m_doc->size().width() + block - 1) / block <= m_largestTexture &&
           (m_doc->size().height() + block - 1) / block <= m_largestTexture;
}

std::optional<int> CanvasView::levelBehind() const {
    const int shown = reducedLevel();
    std::optional<int> nearest;
    for (int level = 0; level <= kMostReducedLevel; ++level) {
        if (level == shown || (level > 0 && !reducedFits(level)))
            continue;
        const std::vector<TexturePart>* parts = &m_texture;
        bool dirty = !m_dirty.isEmpty();
        if (level > 0) {
            const auto reduced = m_reducedTextures.find(level);
            parts = reduced == m_reducedTextures.end() ? nullptr : &reduced->second.parts;
            dirty = !parts || !reduced->second.dirty.isEmpty();
        }
        const bool stale = parts && std::ranges::any_of(*parts, [](const TexturePart& part) { return part.mipmapsStale; });
        if ((dirty || stale) && (!nearest || std::abs(level - shown) < std::abs(*nearest - shown)))
            nearest = level;
    }
    return nearest;
}

// One band of the texture nearest the shown one that's behind, and once it's all there, its mipmaps; then again soon if more is behind.
// It waits for a stroke, a transform and a hidden canvas.
void CanvasView::catchUpView() {
    if (!m_glReady || !isVisible() || m_stroke.isActive() || m_session)
        return;
    const std::optional<int> level = levelBehind();
    if (!level)
        return;
    makeCurrent();
    QElapsedTimer timed;
    timed.start();
    std::vector<TexturePart>* parts = &m_texture;
    if (*level == 0) {
        if (!m_dirty.isEmpty()) {
            const QRect next = m_dirty.boundingRect();
            uploadDirty(QRect(next.left(), next.top(), next.width(), kCatchUpRows));
        }
    } else {
        ReducedTexture& reduced = m_reducedTextures[*level];
        const QRect next = reduced.parts.empty() ? m_doc->rect() : reduced.dirty.boundingRect();
        if (!next.isEmpty())
            uploadReduced(*level, QRect(next.left(), next.top(), next.width(), kCatchUpRows << *level));
        parts = &m_reducedTextures[*level].parts;
    }
    const bool caughtUp = *level == 0 ? m_dirty.isEmpty() : m_reducedTextures[*level].dirty.isEmpty();
    if (caughtUp)
        for (TexturePart& part : *parts)
            if (part.mipmapsStale) {
                glBindTexture(GL_TEXTURE_2D, part.id);
                glGenerateMipmap(GL_TEXTURE_2D);
                part.mipmapsStale = false;
            }
    doneCurrent();
    timing_log::note("catch-up", timed.nsecsElapsed() / 1000);
    if (levelBehind())
        m_viewCatchUp.start(kCatchUpNextMs);
}

void CanvasView::reducedChanged(const QRegion& area) {
    for (auto& [level, reduced] : m_reducedTextures)
        reduced.dirty += area;
}

void CanvasView::uploadReduced(int level, const QRect& area) {
    const int block = 1 << level;
    ReducedTexture& reduced = m_reducedTextures[level];
    if (reduced.parts.empty()) {
        TexturePart part;
        part.held = part.shown = m_doc->rect();  // in canvas pixels: the shader reads the texture by its share of these
        glGenTextures(1, &part.id);
        glBindTexture(GL_TEXTURE_2D, part.id);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        allocateTexture((m_doc->size().width() + block - 1) / block, (m_doc->size().height() + block - 1) / block, true);
        reduced.parts.push_back(part);
        reduced.dirty = QRegion(m_doc->rect());
    }
    const QRegion dirty = area.isValid() ? reduced.dirty.intersected(area) : reduced.dirty;
    if (dirty.isEmpty())
        return;
    QElapsedTimer timed;
    timed.start();
    const QImage& img = m_doc->reducedDisplayImage(level, area);
    timing_log::note("composite", timed.nsecsElapsed() / 1000);
    timed.restart();
    TexturePart& part = reduced.parts.front();
    glBindTexture(GL_TEXTURE_2D, part.id);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, static_cast<GLint>(img.bytesPerLine() / 4));
    for (const QRect& changed : dirty) {
        // The changed canvas pixels' blocks, in the reduced picture.
        const QRect r =
            QRect(QPoint(changed.left() / block, changed.top() / block), QPoint(changed.right() / block, changed.bottom() / block)) &
            img.rect();
        if (r.isEmpty())
            continue;
        glTexSubImage2D(GL_TEXTURE_2D, 0, r.x(), r.y(), r.width(), r.height(), GL_RGBA, GL_UNSIGNED_BYTE,
                        img.constBits() + r.y() * img.bytesPerLine() + r.x() * 4);
    }
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    part.mipmapsStale = true;
    reduced.dirty -= dirty;
    timing_log::note("upload", timed.nsecsElapsed() / 1000);
}

void CanvasView::drawPicture(GLuint texture, const QMatrix4x4& docToClip, Mipmaps mipmaps) {
    std::vector<TexturePart> whole{{texture, m_doc->rect(), m_doc->rect(), false}};
    drawPicture(whole, docToClip, mipmaps);
}

void CanvasView::drawPicture(std::vector<TexturePart>& parts, const QMatrix4x4& docToClip, Mipmaps mipmaps) {
    const ViewState& v = m_doc->view();
    const bool minify = v.zoom < 1.0;
    // Fast: zoomed out, each screen pixel takes the canvas's nearest four, with no mipmaps to build after every change.
    if (m_fastZoomedOut)
        mipmaps = Mipmaps::None;
    const bool cut = parts.size() > 1;
    GLint viewport[4] = {};
    if (cut) {
        glGetIntegerv(GL_VIEWPORT, viewport);
        glEnable(GL_SCISSOR_TEST);
    }
    const auto vec = [](const QPoint& p) { return QVector2D(float(p.x()), float(p.y())); };
    for (TexturePart& part : parts) {
        glBindTexture(GL_TEXTURE_2D, part.id);
        if (minify && mipmaps == Mipmaps::Display && part.mipmapsStale) {
            QElapsedTimer timed;
            timed.start();
            glGenerateMipmap(GL_TEXTURE_2D);
            part.mipmapsStale = false;
            timing_log::note("mipmaps", timed.nsecsElapsed() / 1000);
        }
        // Magnified, the shader averages over each screen pixel's footprint (sharp at whole zoom factors, softened block
        // edges in between, also when rotated); it needs linear filtering.
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, minify && mipmaps != Mipmaps::None ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR);
        // Each texture draws only the screen round its part: the quad covers the whole canvas.
        if (cut)
            scissorTo(part.shown, docToClip, viewport);
        m_program->bind();
        m_program->setUniformValue("magnify", !minify);
        m_program->setUniformValue("docToClip", docToClip);
        m_program->setUniformValue("docSize", QVector2D(float(m_doc->size().width()), float(m_doc->size().height())));
        m_program->setUniformValue("heldOrigin", vec(part.held.topLeft()));
        m_program->setUniformValue("heldSize", QVector2D(float(part.held.width()), float(part.held.height())));
        // Where the part meets the canvas's edge, the quad's own edge decides, as with one texture.
        constexpr float kNoCut = 1e9f;
        const QRect canvas = m_doc->rect();
        m_program->setUniformValue("shownFrom", QVector2D(part.shown.left() > canvas.left() ? float(part.shown.left()) : -kNoCut,
                                                          part.shown.top() > canvas.top() ? float(part.shown.top()) : -kNoCut));
        m_program->setUniformValue("shownTo", QVector2D(part.shown.right() < canvas.right() ? float(part.shown.right() + 1) : kNoCut,
                                                        part.shown.bottom() < canvas.bottom() ? float(part.shown.bottom() + 1) : kNoCut));
        m_program->setUniformValue("image", 0);
        m_vao.bind();
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        m_vao.release();
        m_program->release();
    }
    if (cut)
        glDisable(GL_SCISSOR_TEST);
}

void CanvasView::scissorTo(const QRect& docRect, const QMatrix4x4& docToClip, const GLint viewport[4]) {
    float left = std::numeric_limits<float>::max(), bottom = left, right = -left, top = -left;
    for (const QPoint& corner : {docRect.topLeft(), docRect.topRight() + QPoint(1, 0), docRect.bottomLeft() + QPoint(0, 1),
                                 docRect.bottomRight() + QPoint(1, 1)}) {
        const QVector4D clip = docToClip * QVector4D(float(corner.x()), float(corner.y()), 0.0f, 1.0f);
        const float x = static_cast<float>(viewport[0]) + (clip.x() / clip.w() + 1.0f) * 0.5f * static_cast<float>(viewport[2]);
        const float y = static_cast<float>(viewport[1]) + (clip.y() / clip.w() + 1.0f) * 0.5f * static_cast<float>(viewport[3]);
        left = std::min(left, x), right = std::max(right, x), bottom = std::min(bottom, y), top = std::max(top, y);
    }
    // A pixel more each side than the corners reach, so no pixel the part covers is cut off.
    const int x0 = std::clamp(int(std::floor(left)) - 1, viewport[0], viewport[0] + viewport[2]);
    const int x1 = std::clamp(int(std::ceil(right)) + 1, viewport[0], viewport[0] + viewport[2]);
    const int y0 = std::clamp(int(std::floor(bottom)) - 1, viewport[1], viewport[1] + viewport[3]);
    const int y1 = std::clamp(int(std::ceil(top)) + 1, viewport[1], viewport[1] + viewport[3]);
    glScissor(x0, y0, x1 - x0, y1 - y0);
}

void CanvasView::drawSelectionTint(const QMatrix4x4& docToClip) {
    const SelectionMask* mask = displayedSelection();
    if (m_tintTexture.empty()) {
        if (!mask)
            return;
        m_tintTexture = makeTextures(false);
        m_tint.reset(m_doc->rect());
    }
    const QRegion dirty = m_tint.takeDirty(mask).intersected(m_doc->rect());
    if (!dirty.isEmpty()) {
        // In bands, so a large change never needs a picture of the whole canvas in memory: kBandsAtOnce of them made on the workers at
        // a time, then uploaded in order.
        constexpr int kBand = 128;
        constexpr std::size_t kBandsAtOnce = 64;
        std::vector<QRect> bands;
        for (const QRect& r : dirty)
            for (int y = r.top(); y <= r.bottom(); y += kBand)
                bands.emplace_back(r.left(), y, r.width(), std::min(kBand, r.bottom() + 1 - y));
        glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
        const QColor tint = Theme::color(Theme::Token::SelectionTint);
        std::vector<QImage> pixels;
        for (std::size_t first = 0; first < bands.size(); first += kBandsAtOnce) {
            const std::size_t count = std::min(kBandsAtOnce, bands.size() - first);
            pixels.assign(count, QImage());
            WorkerPool::shared().run(static_cast<int>(count), [&](int i) {
                pixels[static_cast<std::size_t>(i)] = SelectionTint::pixels(mask, bands[first + static_cast<std::size_t>(i)], tint);
            });
            for (std::size_t i = 0; i < count; ++i) {
                const QImage& px = pixels[i];
                glPixelStorei(GL_UNPACK_ROW_LENGTH, static_cast<GLint>(px.bytesPerLine() / 4));
                uploadToParts(m_tintTexture, bands[first + i], px.constBits(), px.bytesPerLine(), bands[first + i].topLeft());
            }
        }
        glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    }
    if (mask)
        drawPicture(m_tintTexture, docToClip, Mipmaps::None);
}

void CanvasView::paintGL() {
    timing_log::frameStarted();
    QElapsedTimer paintTimed;
    paintTimed.start();
    m_stroke.setViewLevel(reducedLevel());  // a zoomed-out view takes the stroke's frames at its scale
    showStrokePaint();
    timing_log::note("take", paintTimed.nsecsElapsed() / 1000);
    QPainter painter(this);
    if (!m_glReady) {
        painter.fillRect(rect(), workspace());
        painter.setPen(palette().color(QPalette::Text));
        painter.drawText(rect().adjusted(24, 24, -24, -24), Qt::AlignCenter | Qt::TextWordWrap,
                         m_glError.isEmpty() ? tr("OpenGL is not available.") : m_glError);
        return;
    }
    painter.beginNativePainting();
    const QColor back = workspace();
    glClearColor(back.redF(), back.greenF(), back.blueF(), 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    if (m_unloadFloating) {
        m_floating.unload();
        m_unloadFloating = false;
    }
    if (m_session && m_session->gpuPreview() && !m_floating.isReady()) {
        m_session->setGpuPreview(false);
        pullSessionDirty();
    }
    const bool floating = m_session && m_session->gpuPreview() && (m_session->floatingPreviewReady() || m_session->gpuPreviewActive());
    if (floating && !m_floating.isLoadedFor(m_session.get()))
        m_floating.load(m_session.get(), m_session->floatingPreview());
    const bool dragPreview = floating && m_session->gpuPreviewActive();
    const int reduced = dragPreview ? 0 : reducedLevel();
    if (reduced > 0)
        uploadReduced(reduced);  // the full texture's changes wait until it's shown again
    else if (!dragPreview)
        uploadDirty();  // while dragging the display texture is not shown; its changes wait

    QMatrix4x4 proj;
    proj.ortho(0, float(width()), float(height()), 0, -1, 1);
    const QMatrix4x4 docToClip = proj * QMatrix4x4(docToWidget());
    const ViewState& v = m_doc->view();
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    if (dragPreview) {
        const MarqueeOptions& o = m_session->options();
        drawPicture(m_floating.belowTexture(), docToClip, Mipmaps::Prebuilt);
        m_floating.drawFloating(docToClip, m_session->floatingQuad(), m_session->perspective(),
                                transform::interactiveQuality(o.final, o.preview, o.dragging, true));
        if (m_floating.aboveTexture())
            drawPicture(m_floating.aboveTexture(), docToClip, Mipmaps::Prebuilt);
    } else {
        drawPicture(reduced > 0 ? m_reducedTextures[reduced].parts : m_texture, docToClip, Mipmaps::Display);
    }
    const bool tint = m_selectionVisible && selectionShownAsArea();
    if (tint)
        drawSelectionTint(docToClip);
    painter.endNativePainting();

    if (m_selectionVisible && !tint)
        m_overlay.paint(painter, docToWidget(), m_antPhase, displayedSelection());
    if (m_shapePreviewing && m_shapeTool)
        paintShapePreview(painter);
    if (CanvasTool* tool = activeTool())
        tool->paintOverlay(painter, docToWidget());
    if (m_drawnGlyph != CanvasGlyph::None && pointerOver())
        paintCanvasCursor(painter, m_drawnGlyph, m_hoverWidgetPos);

    if (pointerOver() && m_brushOutline && showsBrushOutline() && !m_spaceHeld && !holdsPick(m_modifiers) && !holdsMove(m_modifiers)) {
        const double dia = m_tools->settings().clamped().sizePx() * v.zoom;
        painter.setRenderHint(QPainter::Antialiasing);
        const double r = std::max(1.5, dia / 2.0);
        painter.setPen(QPen(QColor(0, 0, 0, 160), 1.0));
        painter.drawEllipse(m_hoverWidgetPos, r + 0.5, r + 0.5);
        painter.setPen(QPen(QColor(255, 255, 255, 200), 1.0));
        painter.drawEllipse(m_hoverWidgetPos, r - 0.5, r - 0.5);
    }
    if (isCloneToolActive())
        paintCloneCrosshair(painter);
    if (m_strokeTiming && !m_timing.isEmpty())
        paintStrokeTiming(painter);
    if (subWindowOf(this))
        paintCardCorners(painter, size());
    m_waitingForFrame.invalidate();
    noteStrokeFrameCost();
    m_viewCatchUp.start(kCatchUpAfterMs);
    timing_log::frameFinished();
    timing_log::note("frame", paintTimed.nsecsElapsed() / 1000);
}

void CanvasView::onImageChanged(const QRect& rect) {
    timing_log::pixelsChanged();
    if (!m_waitingForFrame.isValid())
        m_waitingForFrame.start();
    m_dirty += rect;
    reducedChanged(rect);
    if (!m_showingStroke)
        update();
}

void CanvasView::beginStroke(const InputSample& s) {
    if (m_strokeTiming)
        m_timing.startStroke();
    if (m_stroke.isFinishing() || !m_heldStrokes.empty()) {
        m_heldStrokes.push_back({m_tools->paintColor(), m_tools->settings(), m_tools->stabilizerLevel(), {s}, false});
        m_mode = Mode::Painting;
        return;
    }
    if (const auto started = m_stroke.begin(s, m_tools->paintColor(), m_tools->settings(), m_tools->stabilizerLevel()); !started) {
        emit paintRefused(describe(started.error()));
        return;
    }
    m_mode = Mode::Painting;
    m_stillAt = docToWidget().map(s.pos);
    if (snapsToShapes())
        m_shapeHold.start();
}

// The system can hand over pen events in a batch, and the event loop only gets back to drawing once they've all been handled. On a
// tip too large to keep up with them, that would leave the picture frozen until the pen stops.
void CanvasView::drawIfOverdue() {
    if (!m_stroke.isInBackground() && m_waitingForFrame.isValid() && m_waitingForFrame.elapsed() >= kLongestFrameWaitMs)
        repaint();
}

// A stroke paints on its own thread, and its finished tiles are put on the layer at the start of the frame that shows them.
void CanvasView::askForFrameIfUnshown() {
    if (!m_stroke.hasUnshownPaint())
        return;
    timing_log::pixelsChanged();
    if (!m_waitingForFrame.isValid())
        m_waitingForFrame.start();
    const qint64 rest = m_nextStrokeFrameMs - m_frameClock.elapsed();
    if (rest <= 0)
        update();
    else if (!m_strokeFramePacer.isActive())
        m_strokeFramePacer.start(static_cast<int>(rest));
}

// The interface comes first: however far behind a big stroke is, its frames only take a share of the main thread, and it can show up
// late. A frame that isn't showing a stroke, such as the cursor moving, doesn't wait for this.
void CanvasView::showStrokePaint() {
    if (!m_stroke.hasUnshownPaint() || m_frameClock.elapsed() < m_nextStrokeFrameMs)
        return;
    m_strokeFrameStartedNs = m_frameClock.nsecsElapsed();
    m_showingStroke = true;
    m_stroke.showPaint();
    m_showingStroke = false;
    timing_log::inkShown(m_stroke.shownStampUs());
    if (const std::int64_t stamp = m_stroke.shownStampUs(); m_strokeTiming && stamp > 0 && stamp != m_timedStampUs) {
        // The stroke stamps its pen events on this clock (StrokeWorker), as DOODLEBOLD_TIMING_LOG reads them.
        const auto now = std::chrono::steady_clock::now().time_since_epoch();
        m_timing.inkShown(std::chrono::duration_cast<std::chrono::microseconds>(now).count() - stamp);
        m_timedStampUs = stamp;
    }
}

void CanvasView::paintStrokeTiming(QPainter& painter) {
    const QString text = m_timing.text();
    QFont font = painter.font();
    font.setPointSizeF(font.pointSizeF() * 0.9);
    painter.setFont(font);
    const QRect bounds = painter.fontMetrics().boundingRect(QRect(0, 0, width(), height()), Qt::AlignLeft | Qt::AlignTop, text);
    const QRect box = bounds.adjusted(-8, -6, 8, 6).translated(16 - bounds.left() + 8, height() - bounds.height() - 28);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(0, 0, 0, 170));
    painter.drawRoundedRect(box, 6, 6);
    painter.setPen(QColor(235, 235, 235));
    painter.drawText(box.adjusted(8, 6, -8, -6), Qt::AlignLeft | Qt::AlignTop, text);
}

void CanvasView::setZoomedOut(AppOptions::ZoomedOut display) {
    m_zoomedOut = display;
    const bool fast =
        display == AppOptions::ZoomedOut::ByGraphics ? m_graphicsChoseFast.value_or(false) : display == AppOptions::ZoomedOut::Fast;
    if (fast == m_fastZoomedOut)
        return;
    m_fastZoomedOut = fast;
    for (TexturePart& part : m_texture)
        part.mipmapsStale = true;
    update();
}

std::optional<bool> CanvasView::softwareGraphics() {
    return s_softwareGraphics;
}

bool CanvasView::startsFast(bool software, const QSize& canvas) {
    return software && qint64(canvas.width()) * canvas.height() >= kSoftwareFastFromPixels;
}

bool CanvasView::isSoftwareRenderer(const QString& renderer) {
    // Mesa's llvmpipe and softpipe (also under Zink), SwiftShader, Apple's and Windows' own.
    static constexpr std::array kNames = {"llvmpipe", "softpipe", "swiftshader", "software", "basic render", "gdi generic"};
    return std::ranges::any_of(kNames, [&](const char* name) { return renderer.contains(QLatin1String(name), Qt::CaseInsensitive); });
}

void CanvasView::setBrushOutline(bool shown) {
    if (shown == m_brushOutline)
        return;
    m_brushOutline = shown;
    updateCursor();
    update();
}

void CanvasView::setStrokeTiming(bool shown) {
    if (shown == m_strokeTiming)
        return;
    m_strokeTiming = shown;
    m_timing.startStroke();
    update();
}

void CanvasView::continueStroke(const InputSample& s) {
    if (!m_heldStrokes.empty() && !m_heldStrokes.back().lifted) {
        m_heldStrokes.back().samples.push_back(s);
        return;
    }
    if (m_shapeTool) {
        moveHeldShape(s);
        return;
    }
    m_stroke.add(s);
    askForFrameIfUnshown();
    drawIfOverdue();
    const QPointF at = docToWidget().map(s.pos);
    if (snapsToShapes() && QLineF(at, m_stillAt).length() > kShapeStillPx) {
        m_stillAt = at;
        m_shapeHold.start();
    }
}

bool CanvasView::snapHeldStroke() {
    m_shapeHold.stop();
    if (m_mode != Mode::Painting || m_shapeTool || !m_stroke.isActive() || m_stroke.isFinishing() || !snapsToShapes())
        return false;
    const std::vector<InputSample>& samples = m_stroke.samples();
    std::vector<QPointF> line;
    line.reserve(samples.size());
    for (const InputSample& s : samples)
        line.push_back(s.pos);
    const std::optional<QuickShape> shape = recognizeShape(line);
    if (!shape)
        return false;
    m_shapeTool = std::make_unique<QuickShapeTool>(asHost(), *shape, [this] { shapeChanged(); });
    m_shapeTool->edit().beginHold(samples.back().pos);
    m_shapeMoved = false;
    repaintShape();
    updateCursor();
    return true;
}

// The first few pixels are the hand trembling, not a wish to resize.
void CanvasView::moveHeldShape(const InputSample& s) {
    if (!m_shapeMoved && QLineF(docToWidget().map(s.pos), m_stillAt).length() <= kShapeStillPx)
        return;
    m_shapeMoved = true;
    m_shapeTool->edit().holdTo(s.pos);
    shapeChanged();
}

bool CanvasView::snapsToShapes() const {
    return m_quickShapes && drawsLines(m_tools->settings().tool);
}

void CanvasView::setQuickShapes(bool enabled, int holdMs) {
    m_quickShapes = enabled;
    m_shapeHold.setInterval(holdMs);
    if (!enabled)
        m_shapeHold.stop();
}

void CanvasView::setHeldKeys(const HeldKeys& keys, bool picks, bool moves) {
    m_held = keys;
    m_altPicks = picks;
    m_ctrlMoves = moves;
    updateCursor();
}

// A big shape can take a large part of a second to go through the brush: while it changes it is a plain line in the brush's
// width and colour, and the brush paints it when it rests.
void CanvasView::shapeChanged() {
    if (m_shapeRepaintCost <= m_shapeLiveRepaintLimit) {
        if (!m_shapeRepaint.isActive())
            m_shapeRepaint.start();
        return;
    }
    if (!m_shapePreviewing) {
        m_stroke.clearPainted();
        m_shapePreviewing = true;
    }
    m_shapeRepaint.stop();
    m_shapeSettle.start();
    update();
}

void CanvasView::paintShapePreview(QPainter& painter) {
    QPolygonF line;
    for (const QPointF& point : shapeOutline(m_shapeTool->edit().shape()))
        line << docToWidget().map(point);
    const BrushSettings& settings = m_tools->settings();
    QColor colour = m_tools->paintColor();
    if (settings.tool == ToolType::Eraser || colour.alpha() == 0)
        colour = Theme::color(Theme::Token::Accent);  // what would be erased shows only once the brush has been through
    colour.setAlphaF(static_cast<float>(std::clamp(settings.density / 100.0, 0.2, 1.0)));
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(QPen(colour, std::max(1.0, settings.clamped().sizePx() * zoom()), Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.setBrush(Qt::NoBrush);
    painter.drawPolyline(line);
    painter.restore();
}

void CanvasView::repaintShape() {
    m_shapeRepaint.stop();
    m_shapeSettle.stop();
    m_shapePreviewing = false;
    if (m_shapeTool) {
        QElapsedTimer took;
        took.start();
        m_stroke.repaintAlong(shapeSamples(m_shapeTool->edit().shape(), m_stroke.samples()));
        m_shapeRepaintCost = took.elapsed();
    }
    update();
}

void CanvasView::syncShapePill() {
    if (!m_shapeTool)
        return;
    const QuickShapeEdit& edit = m_shapeTool->edit();
    if (!m_shapePill) {
        m_shapePill = new QuickShapePill(this);
        connect(m_shapePill, &QuickShapePill::kindChosen, this, [this](QuickShapeKind kind) {
            if (!m_shapeTool)
                return;
            m_shapeTool->edit().setKind(kind);
            repaintShape();
            syncShapePill();
        });
        connect(m_shapePill, &QuickShapePill::cornersChosen, this, [this](int corners) {
            if (!m_shapeTool)
                return;
            m_shapeTool->edit().setCornerCount(corners);
            repaintShape();
            syncShapePill();
        });
        connect(m_shapePill, &QuickShapePill::accepted, this, &CanvasView::acceptShape);
        connect(m_shapePill, &QuickShapePill::cancelled, this, &CanvasView::cancelShape);
    }
    m_shapePill->setShape(edit.shape().kind, edit.cornerCount());
    placeShapePill();
    m_shapePill->show();
    m_shapePill->raise();
}

void CanvasView::placeShapePill() {
    if (!m_shapePill)
        return;
    constexpr int kTop = 12;
    m_shapePill->setCompact(false);
    if (m_shapePill->sizeHint().width() > width() - 2 * kTop)
        m_shapePill->setCompact(true);
    const QSize size = m_shapePill->sizeHint();
    m_shapePill->setGeometry(std::max(0, (width() - size.width()) / 2), kTop, size.width(), size.height());
}

void CanvasView::resizeEvent(QResizeEvent* e) {
    QOpenGLWidget::resizeEvent(e);
    placeShapePill();
}

void CanvasView::acceptShape() {
    if (!m_shapeTool)
        return;
    repaintShape();
    m_stroke.finish();
    endShape();
}

void CanvasView::cancelShape() {
    if (!m_shapeTool)
        return;
    m_shapeRepaint.stop();
    m_shapeSettle.stop();
    m_shapePreviewing = false;
    m_stroke.finishAsDrawn();
    endShape();
}

void CanvasView::endShape() {
    m_shapeTool.reset();
    if (m_shapePill)
        m_shapePill->hide();
    m_mode = Mode::Idle;
    notePaintedColour(true);
    updateCursor();
    update();
}

void CanvasView::endStroke() {
    timing_log::lifted();
    m_shapeHold.stop();
    if (m_shapeTool) {
        repaintShape();
        m_shapeTool->setAdjusting(true);
        syncShapePill();
        m_mode = Mode::Idle;
        updateCursor();
        return;
    }
    if (!m_heldStrokes.empty() && !m_heldStrokes.back().lifted) {
        m_heldStrokes.back().lifted = true;
        m_mode = Mode::Idle;
        return;
    }
    const bool painted = m_stroke.isActive();
    startFinishing(m_tools->settings());
    m_mode = Mode::Idle;
    notePaintedColour(painted);
}

// The pen's up. Unless the stroke's still painting, it's over now.
void CanvasView::startFinishing(const BrushSettings& settings) {
    m_finishingSettings = settings;
    m_finishingFor.start();
    m_stroke.finishInBackground();
    if (m_stroke.isFinishing())
        s_viewsFinishing.push_back(this);
    else
        completeStroke();
}

void CanvasView::noteStrokeFrameCost() {
    if (m_strokeFrameStartedNs < 0)
        return;
    const qint64 now = m_frameClock.nsecsElapsed();
    m_nextStrokeFrameMs = (now + (now - m_strokeFrameStartedNs) * kStrokeFrameRest) / 1000000;
    m_strokeFrameStartedNs = -1;
}

void CanvasView::strokeProgress() {
    if (m_stroke.isFinishing() && m_stroke.isPainted())
        completeStroke();
    else
        askForFrameIfUnshown();
}

void CanvasView::completeStroke() {
    const long long caughtUpIn = m_finishingFor.isValid() ? m_finishingFor.nsecsElapsed() / 1000 : 0;
    std::erase(s_viewsFinishing, this);
    QElapsedTimer finishing;
    finishing.start();
    m_stroke.finish();
    timing_log::strokeFinished({static_cast<int>(m_finishingSettings.tool), m_finishingSettings.sizePx(), m_doc->view().zoom,
                                m_doc->size().width(), m_doc->size().height(), caughtUpIn, finishing.nsecsElapsed() / 1000});
    startNextHeldStroke();
}

void CanvasView::startNextHeldStroke() {
    if (m_heldStrokes.empty() || m_stroke.isActive())
        return;
    const HeldStroke held = std::move(m_heldStrokes.front());
    m_heldStrokes.pop_front();
    if (const auto started = m_stroke.begin(held.samples.front(), held.color, held.settings, held.stabilizerLevel); !started) {
        emit paintRefused(describe(started.error()));
        if (!held.lifted)
            m_mode = Mode::Idle;
        startNextHeldStroke();
        return;
    }
    for (std::size_t i = 1; i < held.samples.size(); ++i)
        m_stroke.add(held.samples[i]);
    askForFrameIfUnshown();
    if (held.lifted)
        startFinishing(held.settings);
}

void CanvasView::settleStroke() {
    while (m_stroke.isFinishing())
        completeStroke();
}

void CanvasView::settleAllStrokes() {
    const std::vector<CanvasView*> views = s_viewsFinishing;
    for (CanvasView* view : views)
        view->settleStroke();
}

void CanvasView::notePaintedColour(bool painted) {
    // The colour history takes the colours strokes are painted with; the eraser, Blur and Smudge add nothing.
    switch (m_tools->settings().tool) {
    case ToolType::Pencil:
    case ToolType::Brush:
    case ToolType::WaterColor:
    case ToolType::Marker:
    case ToolType::PixelArtPen:
        if (painted && !m_tools->transparent())
            emit strokePainted(m_tools->color());
        break;
    default: break;
    }
}

void CanvasView::pickColor(const QPointF& docPos) {
    const QPoint p(int(std::floor(docPos.x())), int(std::floor(docPos.y())));
    const QImage& img = m_doc->projection();
    if (!img.rect().contains(p))
        return;
    m_tools->setColor(QColor::fromRgba(img.pixel(p)));
}

void CanvasView::setToolBox(ToolBox* tools) {
    if (m_tools)
        disconnect(m_tools, nullptr, this, nullptr);
    m_tools = tools ? tools : &m_ownTools;
    connect(m_tools, &ToolBox::settingsChanged, this, qOverload<>(&QWidget::update));
    connect(m_tools, &ToolBox::activeToolChanged, this, &CanvasView::syncTool);
    connect(m_tools, &ToolBox::currentSlotChanged, this, &CanvasView::syncTool);
    connect(m_tools, &ToolBox::fixedOptionsChanged, this, [this] {
        if (m_session)
            m_session->setOptions(m_tools->fixedOptions().marquee);
        toolChanged();
    });
    syncTool();
    update();
}

bool CanvasView::beginGesture(Qt::MouseButton button, Qt::KeyboardModifiers mods, const QPointF& widgetPos, const InputSample& s) {
    m_lastWidgetPos = m_gestureStartPos = widgetPos;
    m_gestureStartZoom = zoom();
    m_gestureStartAngle = rotation();
    if (m_spaceHeld && (button == Qt::LeftButton || button == Qt::NoButton)) {
        m_mode = (mods & m_held.zoom) ? Mode::Zooming : (mods & m_held.rotate) ? Mode::Rotating : Mode::Panning;
        m_heldGesture = HeldGesture::Navigation;
        updateCursor();
        return true;
    }
    // Alt-click picks a colour, except with a selection tool, where Alt subtracts, and with the Clone Stamp, where it
    // sets the source point (Photoshop's).
    if (button == Qt::LeftButton && (mods & Qt::AltModifier) && isCloneToolActive()) {
        m_stroke.cloneStamp().setSource(s.pos);
        update();
        return true;
    }
    if (button == Qt::LeftButton && (mods & m_held.pick) && m_altPicks && !isSelectionToolActive()) {
        startGesture(PenButtonAction::Eyedropper, widgetPos, s);
        m_heldGesture = HeldGesture::Pick;
        return true;
    }
    if (button == Qt::LeftButton && holdsMove(mods)) {
        m_heldTool = std::make_unique<MoveTool>(asHost());
        m_heldGesture = HeldGesture::Move;
        beginTool(s, mods, widgetPos);
        if (m_mode != Mode::Tool)
            forgetHeldGesture();
        return true;
    }
    if (button == Qt::LeftButton && m_tools->fixedTool() && !hasTransform()) {
        switch (*m_tools->fixedTool()) {
        case FixedTool::Hand: m_mode = Mode::Panning; break;
        case FixedTool::Zoom: m_mode = Mode::Zooming; break;
        case FixedTool::Rotate: m_mode = Mode::Rotating; break;
        case FixedTool::Eyedropper: startGesture(PenButtonAction::Eyedropper, widgetPos, s); return true;
        default: return false;
        }
        updateCursor();
        return true;
    }
    if (button == Qt::MiddleButton) {
        startGesture(m_buttons.barrel1, widgetPos, s);
        return true;
    }
    if (button == Qt::RightButton) {
        startGesture(m_buttons.barrel2, widgetPos, s);
        return true;
    }
    return false;
}

void CanvasView::startGesture(PenButtonAction action, const QPointF& widgetPos, const InputSample& s) {
    settleStroke();  // a gesture can pick a colour from the picture
    m_lastWidgetPos = widgetPos;
    switch (action) {
    case PenButtonAction::Pan: m_mode = Mode::Panning; break;
    case PenButtonAction::Eyedropper:
        m_mode = Mode::Picking;
        pickColor(s.pos);
        break;
    case PenButtonAction::None: m_mode = Mode::Idle; break;
    }
    updateCursor();
}

void CanvasView::moveGesture(const QPointF& pos, const InputSample& s) {
    switch (m_mode) {
    case Mode::Panning: scrollBy(pos - m_lastWidgetPos); break;
    case Mode::Picking: pickColor(s.pos); break;
    case Mode::Zooming: {
        const QPointF d = pos - m_gestureStartPos;
        setZoom(m_gestureStartZoom * std::pow(2.0, (d.x() - d.y()) / 200.0), m_gestureStartPos);
        break;
    }
    case Mode::Rotating: {
        const QPointF c = navigator().centre();
        const QPointF a = m_gestureStartPos - c, b = pos - c;
        if (std::hypot(a.x(), a.y()) > 4.0 && std::hypot(b.x(), b.y()) > 4.0) {
            const double delta = (std::atan2(b.y(), b.x()) - std::atan2(a.y(), a.x())) * 180.0 / std::numbers::pi;
            setRotation(m_gestureStartAngle + delta);
        }
        break;
    }
    default: break;
    }
    m_lastWidgetPos = pos;
}

void CanvasView::tabletEvent(QTabletEvent* e) {
    const bool partOfStroke = e->type() == QEvent::TabletPress || m_mode == Mode::Painting;
    const timing_log::EventScope handling(partOfStroke);
    const StrokeTimingReadout::EventScope timed(m_strokeTiming && partOfStroke ? &m_timing : nullptr);
    // Counted as the stroke's only once one is painting: a press that starts a gesture, a tool or only makes the view current isn't one
    // (a press that starts a stroke is counted once it has, below).
    if (m_mode == Mode::Painting) {
        timing_log::eventReceived();
        timing_log::eventStamped(e->timestamp());
    }
    // Left unaccepted, the pen reaches the pill as a mouse.
    if (m_mode == Mode::Idle && m_shapePill && !m_shapePill->isHidden() && m_shapePill->geometry().contains(e->position().toPoint())) {
        m_penOver = false;
        updateCursor();
        e->ignore();
        return;
    }
    const InputSample s = TabletInput::fromTablet(*e, widgetToDoc());
    // A pen that was in range before the program started sends no proximity event: its first event says it's here.
    if (!m_stats->inProximity())
        m_stats->setProximity(true, TabletInput::describe(e->pointingDevice()));
    m_stats->record(s, TabletInput::describe(e->pointingDevice()));
    m_hoverWidgetPos = e->position();
    m_hoverPressure = s.pressure;
    const bool wasOver = std::exchange(m_penOver, rect().contains(e->position().toPoint()));
    m_modifiers = e->modifiers() | m_heldKeys;  // a pen event may not know of keys held on the keyboard
    setPenActive(true);
    if (m_penOver)
        takeKeyboardFocus();  // a hovering pen does not always send an enter event
    if (m_penOver != wasOver)
        updateCursor();  // hovering onto the canvas, or off it, without a tap

    input_log::write(e->type() == QEvent::TabletPress ? "press" : e->type() == QEvent::TabletRelease ? "release" : "move", s);
    switch (e->type()) {
    case QEvent::TabletPress:
        m_tabletStrokeActive = true;
        if (!isCurrentView()) {
            makeCurrentView();  // the rest of this pen-down only moves the pointer
            break;
        }
        if (m_mode == Mode::Idle && !beginGesture(e->button(), m_modifiers, e->position(), s) && e->button() == Qt::LeftButton &&
            !beginTool(s, m_modifiers, e->position())) {
            beginStroke(s);
            if (m_mode == Mode::Painting) {
                timing_log::eventReceived();
                timing_log::eventStamped(e->timestamp());
            }
        }
        break;
    case QEvent::TabletMove: movePointer(s, e->position(), e->modifiers()); break;
    case QEvent::TabletRelease:
        if (m_mode == Mode::Painting)
            endStroke();
        else if (m_mode == Mode::Tool && activeTool())
            activeTool()->release(pointer(s, e->position(), e->modifiers()));
        m_mode = Mode::Idle;
        forgetHeldGesture();
        m_tabletStrokeActive = false;
        updateCursor();
        break;
    default: break;
    }
    e->accept();  // accepted tablet events are not re-sent as synthesized mouse events
}

void CanvasView::mousePressEvent(QMouseEvent* e) {
    if (m_tabletStrokeActive || !isRealMouse(e) || m_mode != Mode::Idle)
        return;
    if (!isCurrentView()) {
        makeCurrentView();
        return;
    }
    const InputSample s = TabletInput::fromMouse(*e, widgetToDoc());
    m_stats->record(s, TabletInput::describe(e->pointingDevice()));
    input_log::write("press", s);
    if (!beginGesture(e->button(), e->modifiers(), e->position(), s) && e->button() == Qt::LeftButton &&
        !beginTool(s, e->modifiers(), e->position()))
        beginStroke(s);
}

void CanvasView::mouseMoveEvent(QMouseEvent* e) {
    m_modifiers = e->modifiers();
    m_heldKeys = e->modifiers() & heldModifiers();
    if (isRealMouse(e))
        setPenActive(false);
    takeKeyboardFocus();
    m_hoverWidgetPos = e->position();
    if (m_tabletStrokeActive || !isRealMouse(e)) {
        update();
        return;
    }
    const InputSample s = TabletInput::fromMouse(*e, widgetToDoc());
    m_stats->record(s, TabletInput::describe(e->pointingDevice()));
    input_log::write("move", s);
    movePointer(s, e->position(), e->modifiers());
}

// A pen's or the mouse's move: on with the stroke, the tool or the gesture its press started.
void CanvasView::movePointer(const InputSample& s, QPointF position, Qt::KeyboardModifiers modifiers) {
    if (m_mode == Mode::Painting)
        continueStroke(s);
    else if (m_mode == Mode::Tool && activeTool())
        activeTool()->move(pointer(s, position, modifiers));
    else
        moveGesture(position, s);
    if (m_mode == Mode::Idle && activeTool())
        updateCursor();
    update();
}

void CanvasView::mouseReleaseEvent(QMouseEvent* e) {
    if (m_tabletStrokeActive || !isRealMouse(e))
        return;
    const InputSample s = TabletInput::fromMouse(*e, widgetToDoc());
    input_log::write("release", s);
    if (m_mode == Mode::Painting)
        endStroke();
    else if (m_mode == Mode::Tool && activeTool())
        activeTool()->release(pointer(s, e->position(), e->modifiers()));
    m_mode = Mode::Idle;
    forgetHeldGesture();
    updateCursor();
}

void CanvasView::mouseDoubleClickEvent(QMouseEvent* e) {
    CanvasTool* tool = activeTool();
    if (tool && tool->isActive() && !m_tabletStrokeActive && isRealMouse(e) && e->button() == Qt::LeftButton) {
        tool->doubleClick(pointer(TabletInput::fromMouse(*e, widgetToDoc()), e->position(), e->modifiers()));
        update();
        return;
    }
    mousePressEvent(e);
}

void CanvasView::wheelEvent(QWheelEvent* e) {
    // Each notch moves to the next level of 2^(k/4) about the pointer (100, 118.9, 141.4 … %), whatever modifier is
    // held. Partial deltas (smooth-scrolling devices) add up to whole notches.
    m_wheelRemainder += m_wheelSwap ? -e->angleDelta().y() : e->angleDelta().y();
    const int notches = m_wheelRemainder / 120;
    m_wheelRemainder -= notches * 120;
    if (notches != 0) {
        double z = zoom();
        for (int i = 0; i < std::abs(notches); ++i)
            z = ViewNavigator::nextWheelZoom(z, notches);
        setZoom(z, m_wheelAtCentre ? viewCentre() : e->position());
    }
    e->accept();
}

// The move key (Ctrl at the start) alone, on the canvas, for as long as it is held: nothing else has a use for it there.
bool CanvasView::holdsMove(Qt::KeyboardModifiers mods) const {
    const Qt::KeyboardModifiers keys = mods & (Qt::ControlModifier | Qt::AltModifier | Qt::ShiftModifier);
    return m_ctrlMoves && keys == m_held.move && !m_spaceHeld && !m_session && !m_shapeTool && !m_doc->isPainting() &&
           m_tools->fixedTool() != FixedTool::Text;
}

// The pick key (Alt at the start) without the move key, for as long as it is held; a selection tool's Alt subtracts and the
// Clone Stamp's sets its source.
bool CanvasView::holdsPick(Qt::KeyboardModifiers mods) const {
    return m_altPicks && (mods & m_held.pick) && !(mods & m_held.move) && !m_spaceHeld && !hasTransform() && !m_shapeTool &&
           !isSelectionToolActive() && !isCloneToolActive();
}

void CanvasView::forgetHeldGesture() {
    m_heldGesture = HeldGesture::None;
    m_heldTool.reset();
}

// A tool held with a key stops with the key, even with the button or pen still down.
void CanvasView::endHeldGesture(int key) {
    const bool ends = (m_heldGesture == HeldGesture::Move && key == HeldKeys::keyOf(m_held.move)) ||
                      (m_heldGesture == HeldGesture::Pick && key == HeldKeys::keyOf(m_held.pick)) ||
                      (m_heldGesture == HeldGesture::Navigation &&
                       (key == m_held.pan || (key == HeldKeys::keyOf(m_held.rotate) && m_mode == Mode::Rotating) ||
                        (key == HeldKeys::keyOf(m_held.zoom) && m_mode == Mode::Zooming)));
    if (!ends)
        return;
    if (m_heldTool && m_mode == Mode::Tool)
        m_heldTool->release(ToolPointer{});
    m_mode = Mode::Idle;
    forgetHeldGesture();
}

void CanvasView::setSelectedLayers(std::function<std::vector<LayerId>()> selected) {
    m_selectedLayers = std::move(selected);
}

void CanvasView::trackModifiers(Qt::KeyboardModifiers mods, int key, bool pressed) {
    // A key event for a modifier key does not reliably carry that key's own state; set it from the key itself.
    const Qt::KeyboardModifier own = key == Qt::Key_Control ? Qt::ControlModifier
                                     : key == Qt::Key_Alt   ? Qt::AltModifier
                                     : key == Qt::Key_Shift ? Qt::ShiftModifier
                                     : key == Qt::Key_Meta  ? Qt::MetaModifier
                                                            : Qt::NoModifier;
    if (own != Qt::NoModifier)
        mods.setFlag(own, pressed);
    m_modifiers = mods;
    m_heldKeys = mods & heldModifiers();
}

// The modifiers a pen event may not know are held: those the held keys use (Ctrl and Alt at the start).
Qt::KeyboardModifiers CanvasView::heldModifiers() const {
    return m_held.modifiers();
}

bool CanvasView::isHeldModifierKey(int key) {
    return key == Qt::Key_Control || key == Qt::Key_Alt || key == Qt::Key_Shift;
}

void CanvasView::keyPressEvent(QKeyEvent* e) {
    trackModifiers(e->modifiers(), e->key(), true);
    if (m_shapeTool) {
        if (e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter) {
            acceptShape();
            return;
        }
        if (e->key() == Qt::Key_Escape || e->matches(QKeySequence::Undo)) {
            cancelShape();
            return;
        }
    }
    if (CanvasTool* tool = activeTool(); tool && tool->keyPress(e)) {
        update();
        return;
    }
    const QPoint dir = e->key() == Qt::Key_Left    ? QPoint(-1, 0)
                       : e->key() == Qt::Key_Right ? QPoint(1, 0)
                       : e->key() == Qt::Key_Up    ? QPoint(0, -1)
                       : e->key() == Qt::Key_Down  ? QPoint(0, 1)
                                                   : QPoint();
    if (!dir.isNull()) {
        const Qt::KeyboardModifiers mods = e->modifiers() & ~Qt::KeypadModifier;
        const int stride = (mods & Qt::ShiftModifier) ? kLargeNudge : 1;
        const Qt::KeyboardModifiers base = mods & ~Qt::ShiftModifier;
        if (base & Qt::AltModifier) {
            nudgeSelection(dir * stride);
            return;
        }
        if (base == Qt::ControlModifier || (base == Qt::NoModifier && m_tools->fixedTool() == FixedTool::Move)) {
            nudgeLayer(dir * stride);
            return;
        }
    }
    if (e->key() == m_held.pan && !e->isAutoRepeat()) {
        m_spaceHeld = true;
        updateCursor();
        update();
        return;
    }
    if (isHeldModifierKey(e->key())) {
        updateCursor();
        update();
    }
    if (e->modifiers() == Qt::NoModifier || e->modifiers() == Qt::KeypadModifier) {
        constexpr double kStep = 64.0;
        switch (e->key()) {
        case Qt::Key_Left: scrollBy({kStep, 0}); return;
        case Qt::Key_Right: scrollBy({-kStep, 0}); return;
        case Qt::Key_Up: scrollBy({0, kStep}); return;
        case Qt::Key_Down: scrollBy({0, -kStep}); return;
        default: break;
        }
    }
    QOpenGLWidget::keyPressEvent(e);
}

void CanvasView::keyReleaseEvent(QKeyEvent* e) {
    trackModifiers(e->modifiers(), e->key(), false);
    if (!e->isAutoRepeat())
        endHeldGesture(e->key());
    if (e->key() == m_held.pan && !e->isAutoRepeat()) {
        m_spaceHeld = false;
        updateCursor();
        update();
        return;
    }
    if (isHeldModifierKey(e->key())) {
        updateCursor();
        update();
    }
    QOpenGLWidget::keyReleaseEvent(e);
}

void CanvasView::takeKeyboardFocus() {
    // The keys (Space pans, Ctrl+Space zooms, …) work whenever the pointer is over the canvas. A slider, box or button
    // clicked in a panel would otherwise keep the keyboard, and Space would press it instead. A text field being typed in
    // (e.g. a layer name) keeps it.
    if (hasFocus() || !isActiveWindow() || !isCurrentView())
        return;
    if (qobject_cast<QLineEdit*>(QApplication::focusWidget()))
        return;
    setFocus(Qt::MouseFocusReason);
}

bool CanvasView::isCurrentView() const {
    QMdiSubWindow* window = subWindowOf(this);
    return !window || !window->mdiArea() || window->mdiArea()->currentSubWindow() == window;
}

void CanvasView::makeCurrentView() {
    if (QMdiSubWindow* window = subWindowOf(this); window && window->mdiArea())
        window->mdiArea()->setActiveSubWindow(window);
    setFocus(Qt::MouseFocusReason);
}

void CanvasView::focusOutEvent(QFocusEvent* e) {
    // The Space release would go to the new focus widget: forget the held key so the next press starts clean.
    m_modifiers = Qt::NoModifier;
    m_heldKeys = Qt::NoModifier;
    for (const int key : {int(Qt::Key_Control), int(Qt::Key_Alt), int(Qt::Key_Shift), m_held.pan})
        endHeldGesture(key);
    m_spaceHeld = false;
    updateCursor();
    update();
    QOpenGLWidget::focusOutEvent(e);
}

void CanvasView::enterEvent(QEnterEvent* e) {
    takeKeyboardFocus();
    m_hoverWidgetPos = e->position();
    updateCursor();
    update();
}

void CanvasView::leaveEvent(QEvent*) {
    m_penOver = false;
    update();
}

bool CanvasView::event(QEvent* e) {
    if (e->type() == QEvent::ShortcutOverride) {
        auto* k = static_cast<QKeyEvent*>(e);
        if (CanvasTool* tool = activeTool(); tool && tool->wantsKey(k)) {
            e->accept();
            return true;
        }
        if (k->key() == m_held.pan) {
            e->accept();
            return true;
        }
    }
    return QOpenGLWidget::event(e);
}

void CanvasView::updateCursor() {
    // The modifiers of the last key, mouse or pen event this canvas saw. Not QGuiApplication::queryKeyboardModifiers():
    // on Wayland that is only the last state Qt saw, which can be stale (e.g. right after start), and showed the zoom
    // cursor for Space.
    const Qt::KeyboardModifiers mods = m_modifiers;
    switch (m_mode) {
    case Mode::Panning: showCursor(Qt::ClosedHandCursor, CanvasGlyph::ClosedHand); return;
    case Mode::Picking: showCursor(Qt::CrossCursor, CanvasGlyph::Cross); return;
    case Mode::Zooming: showCursor(Qt::SizeHorCursor, CanvasGlyph::Zoom); return;
    case Mode::Rotating: showCursor(canvasCursor(CanvasGlyph::Rotate), CanvasGlyph::Rotate); return;
    default: break;
    }
    if (m_spaceHeld) {
        if (mods & m_held.zoom)
            showCursor(Qt::SizeHorCursor, CanvasGlyph::Zoom);
        else if (mods & m_held.rotate)
            showCursor(canvasCursor(CanvasGlyph::Rotate), CanvasGlyph::Rotate);
        else
            showCursor(Qt::OpenHandCursor, CanvasGlyph::OpenHand);
        return;
    }
    if (m_mode == Mode::Idle && holdsPick(mods)) {
        showCursor(Qt::CrossCursor, CanvasGlyph::Cross);
        return;
    }
    if (m_mode == Mode::Idle && holdsMove(mods)) {
        showCursor(Qt::SizeAllCursor, glyphForToolCursor(Qt::SizeAllCursor));
        return;
    }
    if (CanvasTool* tool = activeTool()) {
        const Qt::CursorShape shape = tool->cursor(widgetToDoc().map(m_hoverWidgetPos));
        showCursor(shape, glyphForToolCursor(shape));
        return;
    }
    if (const auto fixed = m_tools->fixedTool()) {
        switch (*fixed) {
        case FixedTool::Hand: showCursor(Qt::OpenHandCursor, CanvasGlyph::OpenHand); return;
        case FixedTool::Zoom: showCursor(Qt::SizeHorCursor, CanvasGlyph::Zoom); return;
        case FixedTool::Rotate: showCursor(canvasCursor(CanvasGlyph::Rotate), CanvasGlyph::Rotate); return;
        default: showCursor(Qt::CrossCursor, CanvasGlyph::Cross); return;
        }
    }
    // A painting tool: its outline is drawn under the pen, or a cross where the outline is turned off.
    if (!m_brushOutline) {
        showCursor(Qt::CrossCursor, CanvasGlyph::Cross);
        return;
    }
    showCursor(Qt::BlankCursor, CanvasGlyph::None);
}

// With the pen over the canvas only the drawn glyph shows: the canvas's own cursor is blank too, since some desktops (COSMIC)
// move the mouse pointer with the pen and show its cursor there, and the hand with Space would show twice. The pen's own
// cursor goes by where the pen is, not by which was used last: on those desktops an arrow set for the pen when the mouse moved
// showed on top of the mouse's brush circle and hand.
void CanvasView::showCursor(const QCursor& system, CanvasGlyph glyph) {
    const bool penOnCanvas = m_penActive && m_penInProximity && m_penOver;
    const bool penOverCanvas = m_penInProximity && m_penOver;
    setCursor(penOnCanvas ? QCursor(Qt::BlankCursor) : system);
    if (penOverCanvas != m_penCursorHidden) {
        m_penCursorHidden = penOverCanvas;
        // Wayland: Doodlebold sets the pen's cursor itself. Only this view's own changes are passed on, so that another canvas
        // window updating its cursor does not undo it.
        if (platform::pen_cursor::available())
            platform::pen_cursor::set(penOverCanvas ? Qt::BlankCursor : Qt::ArrowCursor);
    }
    // Qt gives the pen the window's cursor too, which is the cursor where the mouse last was, often an arrow off the canvas: it
    // showed inside the brush circle with the pen. A blank override while the pen is on the canvas
    // keeps that blank as well, on Wayland too.
    setPenOverride(penOnCanvas, Qt::BlankCursor);
    const CanvasGlyph drawn = penOnCanvas ? glyph : CanvasGlyph::None;
    if (drawn != m_drawnGlyph) {
        m_drawnGlyph = drawn;
        update();
    }
}

void CanvasView::setPenOverride(bool on, Qt::CursorShape shape) {
    if (on && !m_overrideCursor) {
        QGuiApplication::setOverrideCursor(QCursor(shape));
        m_overrideCursor = true;
        m_overrideShape = shape;
    } else if (on && shape != m_overrideShape) {
        QGuiApplication::changeOverrideCursor(QCursor(shape));
        m_overrideShape = shape;
    } else if (!on && m_overrideCursor) {
        QGuiApplication::restoreOverrideCursor();
        m_overrideCursor = false;
    }
}

bool CanvasView::eventFilter(QObject* watched, QEvent* e) {
    // The pen moved on to another widget: the canvas gets no event for that, so end the pen cursor here.
    if ((e->type() == QEvent::TabletMove || e->type() == QEvent::TabletPress) && m_penOver && watched != this && watched->isWidgetType() &&
        !isAncestorOf(static_cast<QWidget*>(watched))) {
        m_penOver = false;
        updateCursor();
    }
    // Anything else asked of the program while a shape is adjusted: the shape is kept first, as the document refuses edits
    // during a stroke. Undo takes back only the snap.
    if (m_shapeTool && m_shapeTool->adjusting()) {
        if (e->type() == QEvent::Shortcut) {
            if (static_cast<QShortcutEvent*>(e)->key() == QKeySequence(QKeySequence::Undo)) {
                cancelShape();
                return true;
            }
            acceptShape();
        } else if (e->type() == QEvent::MouseButtonPress && watched != this && watched->isWidgetType() &&
                   !isAncestorOf(static_cast<QWidget*>(watched))) {
            acceptShape();
        }
    }
    return QOpenGLWidget::eventFilter(watched, e);
}

void CanvasView::setPenActive(bool pen) {
    if (pen == m_penActive)
        return;
    m_penActive = pen;
    updateCursor();
}

CanvasTool* CanvasView::activeTool() const {
    if (m_heldTool)
        return m_heldTool.get();
    if (m_shapeTool)
        return m_shapeTool.get();
    if (hasTransform())
        return m_transformTool.get();
    return m_tool.get();
}

void CanvasView::syncTool() {
    if (m_heldTool) {
        m_heldTool->cancel();
        m_mode = Mode::Idle;
        forgetHeldGesture();
    }
    if (m_tool)
        m_tool->cancel();
    m_tool.reset();
    if (const auto fixed = m_tools->fixedTool()) {
        switch (*fixed) {
        case FixedTool::Marquee: m_tool = std::make_unique<MarqueeTool>(asHost()); break;
        case FixedTool::Lasso: m_tool = std::make_unique<LassoTool>(asHost()); break;
        case FixedTool::MagicWand: m_tool = std::make_unique<WandTool>(asHost()); break;
        case FixedTool::Move: m_tool = std::make_unique<MoveTool>(asHost()); break;
        case FixedTool::Text: m_tool = std::make_unique<TextTool>(asHost()); break;
        default: break;
        }
    } else {
        switch (m_tools->currentTool().type()) {
        case ToolType::SelectionPen: m_tool = std::make_unique<SelectionPenTool>(asHost(), m_doc, SelectionBrush::Pen); break;
        case ToolType::SelectionEraser: m_tool = std::make_unique<SelectionPenTool>(asHost(), m_doc, SelectionBrush::Eraser); break;
        case ToolType::Bucket: m_tool = std::make_unique<BucketTool>(asHost()); break;
        default: break;
        }
    }
    m_overlay.invalidate();
    m_tint.invalidate();
    updateCursor();
    update();
}

bool CanvasView::isSelectionToolActive() const {
    const auto fixed = m_tools->fixedTool();
    return fixed && isSelectionTool(*fixed);
}

bool CanvasView::isCloneToolActive() const {
    return !m_tools->fixedTool() && !hasTransform() && m_tools->settings().tool == ToolType::Clone;
}

// The Clone Stamp's source: the cross glyph (light under dark, readable on any picture) where the stroke copies from.
void CanvasView::paintCloneCrosshair(QPainter& painter) {
    const BrushSettings& s = m_tools->settings();
    const QPointF pointer = widgetToDoc().map(m_hoverWidgetPos);
    const std::optional<QPointF> source =
        m_stroke.cloneStamp().crosshair(pointer, s.cloneAligned && pointerOver(), m_mode == Mode::Painting);
    if (source)
        paintCanvasCursor(painter, CanvasGlyph::Cross, docToWidget().map(*source));
}

bool CanvasView::showsBrushOutline() const {
    if (m_tools->fixedTool() || hasTransform() || m_tools->settings().tool == ToolType::Bucket)
        return false;
    if (m_shapeTool && m_shapeTool->adjusting() && (m_shapeTool->isActive() || m_shapeTool->takes(widgetToDoc().map(m_hoverWidgetPos))))
        return false;
    return m_mode == Mode::Idle || m_mode == Mode::Painting || m_mode == Mode::Tool;
}

ToolPointer CanvasView::pointer(const InputSample& s, const QPointF& widgetPos, Qt::KeyboardModifiers mods) const {
    return ToolPointer{s.pos, widgetPos, s, mods};
}

bool CanvasView::beginTool(const InputSample& s, Qt::KeyboardModifiers mods, const QPointF& widgetPos) {
    if (m_shapeTool && !m_shapeTool->takes(s.pos))
        acceptShape();  // and the press starts the next stroke
    CanvasTool* tool = activeTool();
    if (!tool)
        return false;
    settleStroke();
    m_mode = Mode::Tool;
    tool->press(pointer(s, widgetPos, mods));
    if (!tool->isActive())
        m_mode = Mode::Idle;
    toolChanged();
    return true;
}

void CanvasView::toolChanged() {
    pullSessionDirty();
    if (m_session && m_session->gpuPreviewActive())
        m_settleTimer.start();
    else
        m_settleTimer.stop();
    const std::optional<QRect> brushed = m_tool && !m_session ? m_tool->takeLiveSelectionChange() : std::nullopt;
    if (brushed) {
        if (!brushed->isEmpty()) {
            m_overlay.invalidate();
            m_tint.invalidate(*brushed);
        }
    } else if (m_session || (m_tool && m_tool->liveSelection())) {
        m_overlay.invalidate();
        m_tint.invalidate();
    }
    syncAnts();
    update();
}

void CanvasView::pullSessionDirty() {
    if (!m_session)
        return;
    const QRect d = m_session->takeDirty();
    if (d.isEmpty())
        return;
    m_dirty += d;
    reducedChanged(d);
    m_sessionTouched |= d;
}

void CanvasView::settleSessionPreview() {
    if (!m_session || !m_session->gpuPreviewActive())
        return;
    m_session->settlePreview();
    pullSessionDirty();
    m_overlay.invalidate();
    m_tint.invalidate();
    syncAnts();
    update();
}

bool CanvasView::selectionShownAsArea() const {
    if (m_tools->fixedTool())
        return false;
    const ToolType t = m_tools->settings().tool;
    return t == ToolType::SelectionPen || t == ToolType::SelectionEraser;
}

const SelectionMask* CanvasView::displayedSelection() const {
    if (m_session && m_session->gpuPreviewActive())
        return nullptr;  // the ants would trail behind the dragged pixels; they come back when the drag settles
    if (m_tool)
        if (const SelectionMask* live = m_tool->liveSelection())
            return live;
    if (m_session) {
        const std::optional<SelectionMask>& moved = m_session->selection();
        return moved ? &*moved : nullptr;
    }
    const std::optional<SelectionMask>& s = m_doc->selection();
    return s ? &*s : nullptr;
}

void CanvasView::syncAnts() {
    const bool shown = m_selectionVisible && displayedSelection() != nullptr;
    if (shown && !m_antsTimer.isActive())
        m_antsTimer.start();
    else if (!shown && m_antsTimer.isActive())
        m_antsTimer.stop();
}

void CanvasView::setSelectionVisible(bool visible) {
    if (visible == m_selectionVisible)
        return;
    m_selectionVisible = visible;
    syncAnts();
    update();
}

bool CanvasView::hasTransform() const {
    return m_session && m_session->kind() == TransformSession::Kind::Transform;
}

bool CanvasView::startMoveSession() {
    if (m_session)
        return false;
    const std::vector<LayerId> selected = m_selectedLayers ? m_selectedLayers() : std::vector<LayerId>{};
    const LayerNode* current = m_doc->currentLayer();
    const auto made =
        selected.size() > 1 || (current && current->isFolder())
            ? TransformSession::beginMoveLayers(*m_doc, selected.empty() ? std::vector<LayerId>{m_doc->currentLayerId()} : selected,
                                                m_tools->fixedOptions().marquee)
            : TransformSession::beginMove(*m_doc, m_doc->currentLayerId(), m_tools->fixedOptions().marquee);
    if (!made) {
        emit message(made.error() == EditError::NoChange ? tr("There is nothing to move on this layer.") : describe(made.error()));
        return false;
    }
    startSession(made.value());
    toolChanged();
    return true;
}

bool CanvasView::beginTransform(std::optional<TransformMode> mode) {
    if (m_session && hasTransform()) {
        if (mode && *mode != m_session->mode()) {
            m_session->setMode(*mode);
            emit transformChanged();
            update();
        }
        return true;
    }
    if (m_session || m_doc->isPainting())
        return false;
    if (m_tool)
        m_tool->cancel();
    const MarqueeOptions options = m_tools->fixedOptions().marquee;
    const auto made = TransformSession::beginTransform(*m_doc, m_doc->currentLayerId(), mode.value_or(options.transformMode), options);
    if (!made) {
        emit message(made.error() == EditError::NoChange ? tr("There is nothing to transform on this layer.") : describe(made.error()));
        return false;
    }
    startSession(made.value());
    emit transformChanged();
    toolChanged();
    updateCursor();
    return true;
}

void CanvasView::startSession(std::shared_ptr<TransformSession> session) {
    m_session = std::move(session);
    m_sessionTouched = QRect();
    m_session->setGpuPreview(m_glError.isEmpty());  // paintGL falls back to the CPU preview if the shader is missing
    if (m_glReady && m_session->gpuPreview()) {
        // Composite the pictures around the layer now, while the user reaches for a handle, not at the first drag step.
        QTimer::singleShot(0, this, [this, weak = std::weak_ptr<TransformSession>(m_session)] {
            if (const auto s = weak.lock(); s && s == m_session && s->gpuPreview() && m_floating.isReady()) {
                s->floatingPreview();
                update();
            }
        });
    }
}

void CanvasView::endSession() {
    if (!m_session)
        return;
    m_settleTimer.stop();
    m_unloadFloating = true;
    pullSessionDirty();
    const bool wasTransform = hasTransform();
    m_dirty += m_sessionTouched;
    reducedChanged(m_sessionTouched);
    m_sessionTouched = QRect();
    m_session.reset();
    m_overlay.invalidate();
    m_tint.invalidate();
    syncAnts();
    if (wasTransform)
        emit transformChanged();
    updateCursor();
    update();
}

void CanvasView::commitTransform() {
    if (!m_session)
        return;
    m_committing = true;
    const EditResult<> result = m_session->commit();
    m_committing = false;
    if (!result && result.error() != EditError::NoChange)
        emit message(describe(result.error()));
    endSession();
}

void CanvasView::cancelTransform() {
    if (CanvasTool* tool = m_heldTool ? m_heldTool.get() : m_tool.get(); tool && m_mode == Mode::Tool)
        tool->cancel();
    if (m_session)
        m_session->endDrag();
    if (m_mode == Mode::Tool)
        m_mode = Mode::Idle;
    forgetHeldGesture();
    endSession();
}

void CanvasView::finishSession(bool commit) {
    if (commit)
        commitTransform();
    else
        cancelTransform();
}

void CanvasView::nudgeLayer(const QPoint& offset) {
    if (m_session || m_doc->isPainting())
        return;
    const auto r = m_doc->moveLayerExactly(m_doc->currentLayerId(), transform::Turn::None, offset);
    if (!r && r.error() != EditError::NoChange)
        emit message(describe(r.error()));
}

void CanvasView::nudgeSelection(const QPoint& offset) {
    if (m_session || m_doc->isPainting() || !m_doc->selection())
        return;
    SelectionMask moved = transform::moveExactly(*m_doc->selection(), m_doc->rect(), transform::Turn::None, offset);
    const auto r = m_doc->setSelection(std::move(moved), selection::CombineMode::Replace, tr("Nudge Selection"));
    if (!r && r.error() != EditError::NoChange)
        emit message(describe(r.error()));
}

}
