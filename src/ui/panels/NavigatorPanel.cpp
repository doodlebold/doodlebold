#include "ui/panels/NavigatorPanel.h"

#include "core/document/Document.h"
#include "core/view/ViewNavigator.h"
#include "ui/ButtonTips.h"
#include "ui/CanvasView.h"
#include "ui/Components.h"
#include "ui/Theme.h"
#include "ui/ValueBar.h"

#include <QDoubleSpinBox>
#include <QElapsedTimer>
#include <QHBoxLayout>
#include <QIcon>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QPainter>
#include <QPainterPath>
#include <QSignalBlocker>
#include <QSlider>
#include <QSpinBox>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>

namespace doodlebold {

namespace {

using Theme::Token;

constexpr int kSliderMax = 1000;
constexpr int kPreviewMinHeight = 140;  // px: the preview's least height (component metric)
constexpr int kPreviewMargin = Theme::kSpace2;
constexpr int kRefreshDelayMs = 120;
// Before refreshing, the canvas's composite catches up a slice at a time, as many tiles at once as kCatchUpTiles, for at most
// kCatchUpSliceMs a turn of the event loop: after a big stroke that's about 100 ms of work, which mustn't hold up the next stroke.
constexpr std::size_t kCatchUpTiles = 256;
constexpr qint64 kCatchUpSliceMs = 4;
constexpr double kViewOutlineWidth = Theme::kGlyphStroke;
constexpr int kLayoutSpacing = Theme::compact::kControlGap;

QToolButton* makeButton(const QString& name, const QString& iconPath, const QString& tip, const QString& commandId, QWidget* parent) {
    auto* b = new QToolButton(parent);
    b->setObjectName(name);
    b->setIcon(QIcon(iconPath));
    components::toolButton(b);
    button_tips::setCommandTip(b, tip, commandId);
    return b;
}

// The pixels of `area` inside `image`, read where they are: copying a wide stroke's box out first took longer than scaling it, 7 ms
// for one 2000 px across, on the window's thread as the stroke ends.
QImage viewOf(const QImage& image, const QRect& area) {
    return QImage(image.constScanLine(area.top()) + static_cast<qsizetype>(area.left()) * (image.depth() / 8), area.width(), area.height(),
                  image.bytesPerLine(), image.format());
}

}

// The thumbnail owns a cache of the document scaled to fit and re-scales only the part an edit touched. It does not
// follow a stroke while it is painted: the area is collected and refreshed once the stroke ends, so painting does no
// thumbnail work.
class NavigatorPreview : public QWidget {
    Q_OBJECT
public:
    explicit NavigatorPreview(QWidget* parent) : QWidget(parent) {
        setObjectName(QStringLiteral("navigatorPreview"));
        setMinimumHeight(kPreviewMinHeight);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        m_timer.setSingleShot(true);
        m_timer.setInterval(kRefreshDelayMs);
        connect(&m_timer, &QTimer::timeout, this, &NavigatorPreview::flush);
        connect(&m_catchUp, &QTimer::timeout, this, &NavigatorPreview::catchUpSlice);
    }

    void setCanvas(CanvasView* canvas) {
        for (const auto& c : m_connections)
            disconnect(c);
        m_connections.clear();
        m_canvas = canvas;
        m_doc = canvas ? canvas->document() : nullptr;
        m_timer.stop();
        m_catchUp.stop();
        m_dirty = QRect();
        m_cache = QImage();
        if (canvas && m_doc) {
            m_connections.push_back(connect(canvas, &CanvasView::viewChanged, this, qOverload<>(&QWidget::update)));
            m_connections.push_back(connect(m_doc, &Document::imageChanged, this, &NavigatorPreview::onImageChanged));
            m_connections.push_back(connect(m_doc, &Document::paintingChanged, this, [this](bool painting) {
                if (!painting && !m_dirty.isNull())
                    flush();
            }));
        }
        update();
    }

    QSize sizeHint() const override { return QSize(220, kPreviewMinHeight + 40); }

    QRect imageRect() const {
        if (!m_doc || m_doc->size().isEmpty())
            return {};
        const QSizeF avail(std::max(1, width() - 2 * kPreviewMargin), std::max(1, height() - 2 * kPreviewMargin));
        const double scale = std::min(avail.width() / m_doc->size().width(), avail.height() / m_doc->size().height());
        const QSize size(std::max(1, int(std::lround(m_doc->size().width() * scale))),
                         std::max(1, int(std::lround(m_doc->size().height() * scale))));
        return QRect(QPoint((width() - size.width()) / 2, (height() - size.height()) / 2), size);
    }

    QPointF toDocument(const QPointF& p) const {
        const QRect img = imageRect();
        if (img.isEmpty())
            return {};
        return QPointF((p.x() - img.x()) * m_doc->size().width() / img.width(), (p.y() - img.y()) * m_doc->size().height() / img.height());
    }

    QPointF fromDocument(const QPointF& p) const {
        const QRect img = imageRect();
        if (img.isEmpty())
            return {};
        return QPointF(img.x() + p.x() * img.width() / m_doc->size().width(), img.y() + p.y() * img.height() / m_doc->size().height());
    }

    // The thumbnail's size in the screen's own pixels: on a scaled screen (125 %, say) one of imageRect()'s logical
    // size would be stretched when drawn, and the scribbles in it came out jagged.
    QSize cacheSize() const { return (QSizeF(imageRect().size()) * devicePixelRatioF()).toSize().expandedTo(QSize(1, 1)); }

    const QImage& image() {
        if (!m_doc)
            return m_cache;
        const QSize want = cacheSize();
        if (m_cache.size() != want) {
            m_dirty = QRect();
            rebuild(want);
        }
        return m_cache;
    }

    // Refreshes the thumbnail once the canvas's composite has caught up, a slice at a time.
    void flush() {
        m_timer.stop();
        if (!m_doc || m_dirty.isNull() || m_cache.isNull() || m_cache.size() != cacheSize()) {
            refresh();
            return;  // nothing to refresh: it drops the area at once
        }
        if (!m_catchUp.isActive())
            m_catchUp.start();
    }

    void catchUpSlice() {
        if (!m_doc || m_doc->isPainting()) {
            m_catchUp.stop();  // a stroke comes first; its end starts this again
            return;
        }
        QElapsedTimer slice;
        slice.start();
        while (m_doc->catchUpProjection(kCatchUpTiles))
            if (slice.elapsed() >= kCatchUpSliceMs)
                return;  // the rest on the next turn of the event loop
        m_catchUp.stop();
        refresh();
    }

    // At once, for tests.
    void flushNow() {
        m_timer.stop();
        m_catchUp.stop();
        refresh();
    }

    void refresh() {
        const QRect dirty = m_dirty;
        m_dirty = QRect();
        if (!m_doc || dirty.isNull() || m_cache.isNull())
            return;
        if (m_cache.size() != cacheSize())
            return;
        updateRegion(dirty & m_doc->rect());
        update();
    }

    int rebuilds() const { return m_rebuilds; }
    int partialUpdates() const { return m_partials; }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.fillRect(rect(), Theme::color(Token::Window));
        if (!m_canvas || !m_doc)
            return;
        const QRect img = imageRect();
        if (img.isEmpty())
            return;
        p.save();
        p.setRenderHint(QPainter::SmoothPixmapTransform);
        p.drawImage(QRectF(img), image());
        p.restore();
        p.setPen(Theme::color(Token::Border));
        p.drawRect(img.adjusted(-1, -1, 0, 0));

        QPolygonF view;
        for (const QPointF& pt : m_canvas->visibleDocumentArea())
            view << fromDocument(pt);
        QPainterPath outside;
        outside.addRect(QRectF(img));
        outside.addPolygon(view);
        outside.closeSubpath();
        p.save();
        p.setClipRect(img);
        p.fillPath(outside, Theme::color(Token::Shadow));  // the picture outside the view, dimmed
        p.restore();

        p.setRenderHint(QPainter::Antialiasing);
        QPen pen(Theme::color(Token::Accent), kViewOutlineWidth);
        pen.setJoinStyle(Qt::MiterJoin);
        p.setPen(pen);
        p.setBrush(Qt::NoBrush);
        p.drawPolygon(view);
    }

    void mousePressEvent(QMouseEvent* e) override {
        if (e->button() == Qt::LeftButton) {
            centreOn(e->position());
            e->accept();
        }
    }

    void mouseMoveEvent(QMouseEvent* e) override {
        if (e->buttons() & Qt::LeftButton) {
            centreOn(e->position());
            e->accept();
        }
    }

    void wheelEvent(QWheelEvent* e) override {
        const double steps = e->angleDelta().y() / 120.0;
        if (!m_canvas || steps == 0.0)
            return;
        double z = m_canvas->zoom();
        for (int i = 0; i < int(std::lround(std::abs(steps))); ++i)
            z = ViewNavigator::nextWheelZoom(z, steps > 0 ? 1 : -1);
        m_canvas->setZoom(z, m_canvas->zoomAnchor());
        e->accept();
    }

private slots:
    void onImageChanged(const QRect& rect) {
        m_dirty = m_dirty.isNull() ? rect : m_dirty.united(rect);
        if (m_doc && m_doc->isPainting())
            return;
        if (!m_timer.isActive())
            m_timer.start();
    }

private:
    void centreOn(const QPointF& previewPos) {
        if (!m_canvas || !m_doc)
            return;
        const QPointF d = toDocument(previewPos);
        m_canvas->centreOn(
            QPointF(std::clamp(d.x(), 0.0, double(m_doc->size().width())), std::clamp(d.y(), 0.0, double(m_doc->size().height()))));
    }

    void rebuild(const QSize& size) {
        m_cache = m_doc->projection().scaled(size, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
        ++m_rebuilds;
    }

    // The part is widened to whole thumbnail pixels and the document pixels behind it are copied out and scaled into
    // the cache, so the cost follows the size of the change and not the size of the document.
    void updateRegion(const QRect& docRect) {
        if (docRect.isEmpty())
            return;
        const QSize doc = m_doc->size();
        const double sx = double(m_cache.width()) / doc.width();
        const double sy = double(m_cache.height()) / doc.height();
        const int x0 = int(std::floor(docRect.left() * sx));
        const int y0 = int(std::floor(docRect.top() * sy));
        const int x1 = int(std::ceil((docRect.right() + 1) * sx));
        const int y1 = int(std::ceil((docRect.bottom() + 1) * sy));
        const QRect target = QRect(x0, y0, x1 - x0, y1 - y0) & m_cache.rect();
        if (target.isEmpty())
            return;
        if (target.width() * target.height() * 2 > m_cache.width() * m_cache.height()) {
            rebuild(m_cache.size());  // most of it changed anyway; scaling the whole image is the cheaper way
            return;
        }
        const QRect source =
            QRectF(target.x() / sx, target.y() / sy, target.width() / sx, target.height() / sy).toAlignedRect() & m_doc->rect();
        const QRectF dest(source.x() * sx, source.y() * sy, source.width() * sx, source.height() * sy);
        // Two steps: area-averaged scaling to about the final size (what QImage::scaled does for the full rebuild
        // too), then a drawn copy at the exact sub-pixel place, clipped to the pixels being replaced.
        const QSize pieceSize(std::max(1, int(std::lround(dest.width()))), std::max(1, int(std::lround(dest.height()))));
        const QImage piece = viewOf(m_doc->projection(), source).scaled(pieceSize, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
        QPainter p(&m_cache);
        p.setCompositionMode(QPainter::CompositionMode_Source);
        p.setRenderHint(QPainter::SmoothPixmapTransform);
        p.setClipRect(target);
        p.drawImage(dest, piece);
        ++m_partials;
    }

    QPointer<CanvasView> m_canvas;
    QPointer<Document> m_doc;
    std::vector<QMetaObject::Connection> m_connections;
    QImage m_cache;
    QRect m_dirty;
    QTimer m_timer;
    QTimer m_catchUp;  // fires on every turn of the event loop while the composite catches up
    int m_rebuilds = 0;
    int m_partials = 0;
};

NavigatorPanel::NavigatorPanel(QWidget* parent) : QWidget(parent) {
    setObjectName(QStringLiteral("navigatorPanel"));

    m_preview = new NavigatorPreview(this);

    m_zoomRow = new QWidget(this);
    auto* zoomOut = makeButton(QStringLiteral("zoomOutButton"), QStringLiteral(":/icons/zoom-out.svg"), tr("Zoom Out"),
                               QStringLiteral("view.zoomOut"), m_zoomRow);
    auto* zoomIn = makeButton(QStringLiteral("zoomInButton"), QStringLiteral(":/icons/zoom-in.svg"), tr("Zoom In"),
                              QStringLiteral("view.zoomIn"), m_zoomRow);
    auto* actual = new QToolButton(m_zoomRow);
    actual->setObjectName(QStringLiteral("actualPixelsButton"));
    actual->setText(tr("100%"));
    button_tips::setCommandTip(actual, tr("Actual Pixels (100%)"), QStringLiteral("view.scale100"));
    m_zoomBar = new ValueBar(m_zoomRow);
    m_zoomBar->setObjectName(QStringLiteral("zoomBar"));
    m_zoomBar->setLook(ValueBar::Look::Line);
    m_zoomBar->setLabel(tr("Scale"));
    m_zoomBar->setRange(ViewNavigator::kMinZoom * 100.0, ViewNavigator::kMaxZoom * 100.0);
    m_zoomBar->setDecimals(1);
    m_zoomBar->setSuffix(QStringLiteral("%"));
    m_zoomBar->setMapping([](double percent) { return zoomToSlider(percent / 100.0) / double(kSliderMax); },
                          [](double p) { return sliderToZoom(int(std::lround(p * kSliderMax))) * 100.0; });
    auto* zoomLayout = new QHBoxLayout(m_zoomRow);
    zoomLayout->setContentsMargins(0, 0, 0, 0);
    zoomLayout->setSpacing(kLayoutSpacing);
    zoomLayout->addWidget(m_zoomBar, 1);
    zoomLayout->addWidget(zoomOut);
    zoomLayout->addWidget(zoomIn);
    zoomLayout->addWidget(actual);

    m_angleRow = new QWidget(this);
    auto* rotateCcw = makeButton(QStringLiteral("rotateCcwButton"), QStringLiteral(":/icons/rotate-ccw.svg"), tr("Rotate Counterclockwise"),
                                 QStringLiteral("view.rotateCCW"), m_angleRow);
    auto* rotateCw = makeButton(QStringLiteral("rotateCwButton"), QStringLiteral(":/icons/rotate-cw.svg"), tr("Rotate Clockwise"),
                                QStringLiteral("view.rotateCW"), m_angleRow);
    auto* resetAngle = makeButton(QStringLiteral("resetAngleButton"), QStringLiteral(":/icons/reset-angle.svg"), tr("Reset Angle"),
                                  QStringLiteral("view.resetAngle"), m_angleRow);
    m_flipButton = makeButton(QStringLiteral("flipButton"), QStringLiteral(":/icons/flip-h.svg"), tr("Flip Horizontally"),
                              QStringLiteral("view.flip"), m_angleRow);
    m_flipButton->setCheckable(true);
    m_angleBar = new ValueBar(m_angleRow);
    m_angleBar->setObjectName(QStringLiteral("angleBar"));
    m_angleBar->setLook(ValueBar::Look::Line);
    m_angleBar->setLabel(tr("Angle"));
    m_angleBar->setRange(-180, 180);
    m_angleBar->setSuffix(QStringLiteral("°"));
    auto* angleLayout = new QHBoxLayout(m_angleRow);
    angleLayout->setContentsMargins(0, 0, 0, 0);
    angleLayout->setSpacing(kLayoutSpacing);
    angleLayout->addWidget(m_angleBar, 1);
    angleLayout->addWidget(rotateCcw);
    angleLayout->addWidget(rotateCw);
    angleLayout->addWidget(resetAngle);
    angleLayout->addWidget(m_flipButton);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(kLayoutSpacing, kLayoutSpacing, kLayoutSpacing, kLayoutSpacing);
    layout->setSpacing(kLayoutSpacing);
    layout->addWidget(m_preview, 1);
    layout->addWidget(m_zoomRow);
    layout->addWidget(m_angleRow);

    // The controls act on the canvas only through user input: syncControls blocks their signals.
    connect(zoomOut, &QToolButton::clicked, this, [this] {
        if (m_canvas)
            m_canvas->zoomStep(-1, canvasCentre());
    });
    connect(zoomIn, &QToolButton::clicked, this, [this] {
        if (m_canvas)
            m_canvas->zoomStep(1, canvasCentre());
    });
    connect(actual, &QToolButton::clicked, this, [this] {
        if (m_canvas)
            m_canvas->actualPixels();
    });
    connect(m_zoomBar, &ValueBar::valueChanged, this, [this](double percent) {
        if (m_canvas)
            m_canvas->setZoom(percent / 100.0, canvasCentre());
    });
    connect(rotateCcw, &QToolButton::clicked, this, [this] {
        if (m_canvas)
            m_canvas->rotateBy(-CanvasView::kRotateStepDeg);
    });
    connect(rotateCw, &QToolButton::clicked, this, [this] {
        if (m_canvas)
            m_canvas->rotateBy(CanvasView::kRotateStepDeg);
    });
    connect(resetAngle, &QToolButton::clicked, this, [this] {
        if (m_canvas)
            m_canvas->resetAngle();
    });
    connect(m_flipButton, &QToolButton::clicked, this, [this] {
        if (m_canvas)
            m_canvas->toggleFlip();
    });
    connect(m_angleBar, &ValueBar::valueChanged, this, [this](double deg) {
        if (m_canvas)
            m_canvas->setRotation(deg);
    });

    syncControls();
}

void NavigatorPanel::setCanvas(CanvasView* canvas) {
    for (const auto& c : m_connections)
        disconnect(c);
    m_connections.clear();
    m_canvas = canvas;
    m_preview->setCanvas(canvas);
    if (canvas) {
        m_connections.push_back(connect(canvas, &CanvasView::viewChanged, this, &NavigatorPanel::syncControls));
        m_connections.push_back(connect(canvas, &QObject::destroyed, this, [this] { setCanvas(nullptr); }));
    }
    syncControls();
}

// The zoom slider: one logarithmic scale from end to end, so the same distance always multiplies the zoom by the same amount.
int NavigatorPanel::zoomToSlider(double zoom) {
    const double low = std::log2(ViewNavigator::kMinZoom), high = std::log2(ViewNavigator::kMaxZoom);
    const double t = (std::log2(std::clamp(zoom, ViewNavigator::kMinZoom, ViewNavigator::kMaxZoom)) - low) / (high - low);
    return std::clamp(int(std::lround(t * kSliderMax)), 0, kSliderMax);
}

double NavigatorPanel::sliderToZoom(int value) {
    const double t = double(std::clamp(value, 0, kSliderMax)) / kSliderMax;
    const double low = std::log2(ViewNavigator::kMinZoom), high = std::log2(ViewNavigator::kMaxZoom);
    return std::exp2(low + t * (high - low));
}

QPointF NavigatorPanel::previewToDocument(const QPointF& previewPos) const {
    return m_preview->toDocument(previewPos);
}

QPointF NavigatorPanel::documentToPreview(const QPointF& docPos) const {
    return m_preview->fromDocument(docPos);
}

const QImage& NavigatorPanel::previewImage() {
    return m_preview->image();
}

void NavigatorPanel::flushPreview() {
    m_preview->flushNow();
}

int NavigatorPanel::previewRebuilds() const {
    return m_preview->rebuilds();
}

int NavigatorPanel::previewPartialUpdates() const {
    return m_preview->partialUpdates();
}

QPointF NavigatorPanel::canvasCentre() const {
    return m_canvas ? QPointF(m_canvas->width() / 2.0, m_canvas->height() / 2.0) : QPointF();
}

void NavigatorPanel::syncControls() {
    const bool attached = m_canvas != nullptr;
    m_zoomRow->setEnabled(attached);
    m_angleRow->setEnabled(attached);
    if (!attached)
        return;
    const QSignalBlocker b1(m_zoomBar), b3(m_angleBar), b5(m_flipButton);
    m_zoomBar->setValue(m_canvas->zoom() * 100.0);
    m_angleBar->setValue(std::round(m_canvas->rotation()));
    m_flipButton->setChecked(m_canvas->isFlipped());
}

}

#include "NavigatorPanel.moc"
