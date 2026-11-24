#include "ui/JpegQualityDialog.h"

#include "ui/Theme.h"

#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QProgressBar>
#include <QPushButton>
#include <QRadioButton>
#include <QScrollBar>
#include <QTransform>
#include <QVBoxLayout>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>

namespace doodlebold {

using Theme::Token;

QualityBar::QualityBar(int value, QWidget* parent) : QWidget(parent), m_value(std::clamp(value, kMin, kMax)) {
    setFocusPolicy(Qt::StrongFocus);
    setFixedHeight(Theme::comfortable::kControlMd);
    setCursor(Qt::PointingHandCursor);
}

void QualityBar::setValue(int value) {
    value = std::clamp(value, kMin, kMax);
    if (value == m_value)
        return;
    m_value = value;
    update();
    emit valueChanged(m_value);
}

void QualityBar::paintEvent(QPaintEvent*) {
    QPainter p(this);
    // The value bar's look (ValueBar, Look::Bar): the raised ground, the filled part, the value at the right, the focus ring.
    p.setRenderHint(QPainter::Antialiasing);
    const QRectF frame = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    const double radius = std::min<double>(Theme::kRadiusMd, height() / 2.0);
    QPainterPath clip;
    clip.addRoundedRect(frame, radius, radius);
    p.fillPath(clip, Theme::color(Token::Base));
    const double fill = frame.width() * (m_value - kMin) / (kMax - kMin);
    p.save();
    p.setClipPath(clip);
    p.fillRect(QRectF(frame.left(), frame.top(), fill, frame.height()), Theme::color(isEnabled() ? Token::Fill : Token::Border));
    p.restore();
    if (hasFocus()) {
        p.setPen(QPen(Theme::color(Token::FocusRing), Theme::kBorder));
        p.drawPath(clip);
    }
    p.setFont(Theme::font(Theme::TextRole::Label));
    p.setPen(Theme::color(isEnabled() ? Token::Text : Token::TextDisabled));
    p.drawText(rect().adjusted(0, 0, -(Theme::kSpace1 + Theme::kSpace2), 0), Qt::AlignRight | Qt::AlignVCenter, QString::number(m_value));
}

void QualityBar::setFromX(int x) {
    const int w = std::max(1, width() - 1);
    setValue(kMin + static_cast<int>(std::lround(double(std::clamp(x, 0, w)) * (kMax - kMin) / w)));
}

void QualityBar::mousePressEvent(QMouseEvent* e) {
    if (e->button() == Qt::LeftButton)
        setFromX(static_cast<int>(e->position().x()));
}

void QualityBar::mouseMoveEvent(QMouseEvent* e) {
    if (e->buttons() & Qt::LeftButton)
        setFromX(static_cast<int>(e->position().x()));
}

void QualityBar::keyPressEvent(QKeyEvent* e) {
    switch (e->key()) {
    case Qt::Key_Left:
    case Qt::Key_Down: setValue(m_value - 1); break;
    case Qt::Key_Right:
    case Qt::Key_Up: setValue(m_value + 1); break;
    case Qt::Key_PageDown: setValue(m_value - 10); break;
    case Qt::Key_PageUp: setValue(m_value + 10); break;
    case Qt::Key_Home: setValue(kMin); break;
    case Qt::Key_End: setValue(kMax); break;
    default: QWidget::keyPressEvent(e); return;
    }
}

void QualityBar::wheelEvent(QWheelEvent* e) {
    if (!hasFocus()) {  // as the value bars: the wheel over an unfocused bar is not for it
        e->ignore();
        return;
    }
    setValue(m_value + (e->angleDelta().y() > 0 ? 1 : -1));
    e->accept();
}

JpegPreviewView::JpegPreviewView(QWidget* parent) : QAbstractScrollArea(parent) {
    viewport()->setAutoFillBackground(false);
    horizontalScrollBar()->setSingleStep(16);
    verticalScrollBar()->setSingleStep(16);
}

void JpegPreviewView::setImage(const QImage& image) {
    m_image = image;
    rebuildScaled();
    updateScrollBars();
    viewport()->update();
}

void JpegPreviewView::setScalePercent(double percent) {
    const QSizeF before = scaledSize();
    const QSize pane = deviceViewportSize();
    const double cx = before.width() > 0 ? (horizontalScrollBar()->value() + pane.width() / 2.0) / before.width() : 0.5;
    const double cy = before.height() > 0 ? (verticalScrollBar()->value() + pane.height() / 2.0) / before.height() : 0.5;
    m_scale = percent;
    rebuildScaled();
    updateScrollBars();
    const QSizeF after = scaledSize();
    horizontalScrollBar()->setValue(static_cast<int>(cx * after.width() - pane.width() / 2.0));
    verticalScrollBar()->setValue(static_cast<int>(cy * after.height() - pane.height() / 2.0));
    viewport()->update();
}

QSize JpegPreviewView::scaledSize() const {
    return QSize(static_cast<int>(std::lround(m_image.width() * m_scale / 100.0)),
                 static_cast<int>(std::lround(m_image.height() * m_scale / 100.0)));
}

QSize JpegPreviewView::deviceViewportSize() const {
    const double dpr = viewport()->devicePixelRatioF();
    return QSize(static_cast<int>(std::lround(viewport()->width() * dpr)), static_cast<int>(std::lround(viewport()->height() * dpr)));
}

void JpegPreviewView::rebuildScaled() {
    m_scaled = QImage();
    if (m_scale < 100.0 && !m_image.isNull()) {
        const QSize s = scaledSize().expandedTo(QSize(1, 1));
        m_scaled = m_image.scaled(s, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    }
}

void JpegPreviewView::updateScrollBars() {
    const QSize s = scaledSize();
    const QSize pane = deviceViewportSize();
    horizontalScrollBar()->setRange(0, std::max(0, s.width() - pane.width()));
    horizontalScrollBar()->setPageStep(pane.width());
    verticalScrollBar()->setRange(0, std::max(0, s.height() - pane.height()));
    verticalScrollBar()->setPageStep(pane.height());
}

bool JpegPreviewView::event(QEvent* e) {
    // Another display scale (the setting changed, or the window moved to another monitor) changes the size of the pane in
    // screen pixels without a resize: the scroll ranges follow.
#if QT_VERSION >= QT_VERSION_CHECK(6, 6, 0)
    if (e->type() == QEvent::DevicePixelRatioChange)
        updateScrollBars();
#endif
    return QAbstractScrollArea::event(e);
}

void JpegPreviewView::resizeEvent(QResizeEvent* e) {
    QAbstractScrollArea::resizeEvent(e);
    updateScrollBars();
}

void JpegPreviewView::paintEvent(QPaintEvent*) {
    QPainter p(viewport());
    p.fillRect(viewport()->rect(), Theme::color(Token::Base));
    if (m_image.isNull())
        return;
    // Drawn in device pixels: on a scaled display (COSMIC at 125 %) 100 % still shows one picture pixel per screen pixel,
    // instead of a picture enlarged by the display scale with blocky, uneven pixels.
    const double dpr = viewport()->devicePixelRatioF();
    p.setWorldTransform(QTransform::fromScale(1.0 / dpr, 1.0 / dpr));
    const QSize pane = deviceViewportSize();
    const QSize s = scaledSize();
    const int left = s.width() < pane.width() ? (pane.width() - s.width()) / 2 : -horizontalScrollBar()->value();
    const int top = s.height() < pane.height() ? (pane.height() - s.height()) / 2 : -verticalScrollBar()->value();
    const QRect target(QPoint(left, top), s);
    const QRect visible = target.intersected(QRect(QPoint(0, 0), pane));
    if (visible.isEmpty())
        return;
    if (!m_scaled.isNull()) {
        p.drawImage(visible.topLeft(), m_scaled, visible.translated(-target.topLeft()));
    } else {
        const double k = m_scale / 100.0;
        const QRectF source((visible.left() - left) / k, (visible.top() - top) / k, visible.width() / k, visible.height() / k);
        p.drawImage(QRectF(visible), m_image, source);
    }
}

bool JpegQualityDialog::hasOptions(const DocumentFormat::Features& f) {
    return f.quality;
}

JpegQualityDialog::JpegQualityDialog(const QImage& canvas, const ExportOptions& initial, double resolution, QWidget* parent)
    : QDialog(parent), m_canvas(canvas), m_resolution(resolution) {
    setWindowTitle(tr("JPEG Quality"));
    setSizeGripEnabled(true);
    resize(640, 492);

    m_view = new JpegPreviewView(this);
    m_view->setObjectName(QStringLiteral("jpegPreview"));
    m_view->setImage(canvas);

    m_scale = new QComboBox(this);
    m_scale->setObjectName(QStringLiteral("scaleCombo"));
    for (int i = 0; i < io::kPreviewScaleCount; ++i) {
        if (i == io::kPreviewScale100 || i == io::kPreviewScale100 + 1)
            m_scale->insertSeparator(m_scale->count());  // the list sets 100 % apart
        m_scale->addItem(io::previewScaleLabel(i), i);
    }
    m_scale->setCurrentIndex(m_scale->findData(io::kPreviewScale100));
    const auto smallButton = [this](const QString& text, const QString& tip, const char* name) {
        auto* b = new QPushButton(text, this);
        b->setObjectName(QString::fromLatin1(name));
        b->setToolTip(tip);
        b->setFixedSize(Theme::comfortable::kControlSm, Theme::comfortable::kControlSm);
        b->setProperty("role", "glyph");  // no padding, so the sign shows in the small square
        b->setFocusPolicy(Qt::NoFocus);
        return b;
    };
    QPushButton* zoomOut = smallButton(QStringLiteral("−"), tr("Zoom out"), "zoomOutButton");
    QPushButton* zoomIn = smallButton(QStringLiteral("+"), tr("Zoom in"), "zoomInButton");
    QPushButton* fit = smallButton(QStringLiteral("="), tr("Fit"), "fitButton");

    m_original = new QRadioButton(tr("Original Image"), this);
    m_original->setObjectName(QStringLiteral("originalRadio"));
    m_jpeg = new QRadioButton(tr("JPEG Image"), this);
    m_jpeg->setObjectName(QStringLiteral("jpegRadio"));
    m_jpeg->setChecked(true);
    auto* imageGroup = new QButtonGroup(this);
    imageGroup->addButton(m_original);
    imageGroup->addButton(m_jpeg);

    m_quality = new QualityBar(initial.quality, this);
    m_quality->setObjectName(QStringLiteral("qualityBar"));
    m_horizontal = new QCheckBox(tr("Horizontal"), this);
    m_horizontal->setObjectName(QStringLiteral("horizontalBox"));
    m_horizontal->setChecked(initial.chromaHorizontal);
    m_vertical = new QCheckBox(tr("Vertical"), this);
    m_vertical->setObjectName(QStringLiteral("verticalBox"));
    m_vertical->setChecked(initial.chromaVertical);

    const auto valueLabel = [this](const char* name) {
        auto* l = new QLabel(QStringLiteral("-"), this);
        l->setObjectName(QString::fromLatin1(name));
        return l;
    };
    m_horzPixels = valueLabel("horzPixelsLabel");
    m_vertPixels = valueLabel("vertPixelsLabel");
    m_fileSize = valueLabel("fileSizeLabel");
    m_rate = valueLabel("rateLabel");

    auto* resultHeader = new QLabel(tr("Compression Result"), this);
    resultHeader->setProperty("role", "accent");
    auto* resultGrid = new QGridLayout;
    resultGrid->setColumnStretch(1, 1);
    const auto resultRow = [&](int row, const QString& label, QLabel* value) {
        auto* l = new QLabel(label, this);
        l->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        resultGrid->addWidget(l, row, 0);
        resultGrid->addWidget(value, row, 1);
    };
    resultRow(0, tr("Horz. Pixels :"), m_horzPixels);
    resultRow(1, tr("Vert. Pixels :"), m_vertPixels);
    resultRow(2, tr("File Size :"), m_fileSize);
    resultRow(3, tr("Compression Rate :"), m_rate);

    auto* scaleRow = new QHBoxLayout;
    scaleRow->addWidget(m_scale, 1);
    scaleRow->addWidget(zoomOut);
    scaleRow->addWidget(zoomIn);
    scaleRow->addWidget(fit);

    auto* side = new QVBoxLayout;
    side->addWidget(new QLabel(tr("Scale:"), this));
    side->addLayout(scaleRow);
    side->addSpacing(Theme::kSpace2);
    side->addWidget(new QLabel(tr("Image:"), this));
    side->addWidget(m_original);
    side->addWidget(m_jpeg);
    side->addSpacing(Theme::kSpace2);
    side->addWidget(new QLabel(tr("Compression Quality:"), this));
    side->addWidget(m_quality);
    side->addSpacing(Theme::kSpace2);
    side->addWidget(new QLabel(tr("Chroma Downsampling:"), this));
    side->addWidget(m_horizontal);
    side->addWidget(m_vertical);
    side->addSpacing(Theme::kSpace3);
    side->addWidget(resultHeader);
    side->addLayout(resultGrid);
    side->addStretch(1);
    auto* sideWidget = new QWidget(this);
    sideWidget->setLayout(side);
    sideWidget->setFixedWidth(212);  // px: the settings column (component metric)
    side->setContentsMargins(0, 0, 0, 0);

    m_progress = new QProgressBar(this);
    m_progress->setObjectName(QStringLiteral("jpegProgress"));
    m_progress->setRange(0, 1);
    m_progress->setValue(0);
    m_progress->setTextVisible(false);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    auto* bottom = new QHBoxLayout;
    bottom->addWidget(m_progress, 1);
    bottom->addWidget(buttons);

    auto* top = new QHBoxLayout;
    top->addWidget(m_view, 1);
    top->addWidget(sideWidget);
    auto* layout = new QVBoxLayout(this);
    Theme::padDialog(layout, true);
    layout->addLayout(top, 1);
    layout->addLayout(bottom);

    connect(m_scale, &QComboBox::currentIndexChanged, this, [this](int i) {
        if (i >= 0)
            m_view->setScalePercent(io::previewScales()[static_cast<std::size_t>(m_scale->itemData(i).toInt())]);
    });
    connect(zoomOut, &QPushButton::clicked, this, [this] { stepScale(-1); });
    connect(zoomIn, &QPushButton::clicked, this, [this] { stepScale(1); });
    connect(fit, &QPushButton::clicked, this, [this] { fitScale(); });
    connect(m_original, &QRadioButton::toggled, this, [this] { updatePreviewImage(); });

    m_debounce.setSingleShot(true);
    m_debounce.setInterval(150);
    connect(&m_debounce, &QTimer::timeout, this, &JpegQualityDialog::startEncode);
    connect(m_quality, &QualityBar::valueChanged, this, [this] { requestEncode(); });
    connect(m_horizontal, &QCheckBox::toggled, this, [this] { requestEncode(); });
    connect(m_vertical, &QCheckBox::toggled, this, [this] { requestEncode(); });

    updatePreviewImage();
    QTimer::singleShot(0, this, &JpegQualityDialog::startEncode);
    m_progress->setRange(0, 0);
    m_dirty = true;
}

JpegQualityDialog::~JpegQualityDialog() {
    if (m_job.valid())
        m_job.wait();
}

ExportOptions JpegQualityDialog::options() const {
    ExportOptions o;
    o.quality = m_quality->value();
    o.chromaHorizontal = m_horizontal->isChecked();
    o.chromaVertical = m_vertical->isChecked();
    return o;
}

bool JpegQualityDialog::isIdle() const {
    return !m_debounce.isActive() && !m_running && !m_dirty;
}

void JpegQualityDialog::requestEncode() {
    m_dirty = true;
    m_progress->setRange(0, 0);
    m_debounce.start();
}

void JpegQualityDialog::startEncode() {
    if (m_running)
        return;
    m_dirty = false;
    m_running = true;
    io::JpegSettings settings;
    const ExportOptions o = options();
    settings.quality = o.quality;
    settings.chromaHorizontal = o.chromaHorizontal;
    settings.chromaVertical = o.chromaVertical;
    settings.resolution = m_resolution;
    const QImage canvas = m_canvas;
    m_job = std::async(std::launch::async, [this, canvas, settings] {
        const auto made = io::makeJpegPreview(canvas, settings);
        const bool ok = made.ok();
        const io::JpegPreview value = ok ? made.value() : io::JpegPreview{};
        QMetaObject::invokeMethod(this, [this, value, ok] { onEncoded(value, ok); }, Qt::QueuedConnection);
    });
}

void JpegQualityDialog::onEncoded(const io::JpegPreview& preview, bool ok) {
    m_running = false;
    if (ok) {
        m_result = preview.result;
        m_decoded = preview.decoded;
    }
    m_resultValid = ok;
    if (m_dirty && !m_debounce.isActive()) {
        startEncode();
        return;
    }
    if (m_dirty)
        return;
    m_progress->setRange(0, 1);
    m_progress->setValue(0);
    showResult(ok);
    updatePreviewImage();
}

void JpegQualityDialog::showResult(bool ok) {
    if (!ok) {
        for (QLabel* l : {m_horzPixels, m_vertPixels, m_fileSize, m_rate})
            l->setText(QStringLiteral("-"));
        return;
    }
    m_horzPixels->setText(QString::number(m_result.pixels.width()));
    m_vertPixels->setText(QString::number(m_result.pixels.height()));
    m_fileSize->setText(m_result.fileSizeText());
    m_rate->setText(m_result.rateText());
}

void JpegQualityDialog::updatePreviewImage() {
    const bool jpeg = !m_original->isChecked() && !m_decoded.isNull();
    m_view->setImage(jpeg ? m_decoded : m_canvas);
}

void JpegQualityDialog::stepScale(int delta) {
    int row = m_scale->currentIndex() + delta;
    while (row >= 0 && row < m_scale->count() && !m_scale->itemData(row).isValid())
        row += delta;
    if (row >= 0 && row < m_scale->count())
        m_scale->setCurrentIndex(row);
}

void JpegQualityDialog::fitScale() {
    const int best = io::fitScaleIndex(m_canvas.size(), m_view->deviceViewportSize());
    m_scale->setCurrentIndex(m_scale->findData(best));
}

}
