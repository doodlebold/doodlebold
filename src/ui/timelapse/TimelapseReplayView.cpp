#include "ui/timelapse/TimelapseReplayView.h"

#include "ui/Theme.h"
#include "ui/timelapse/TimelapseDialogs.h"

#include <QGraphicsDropShadowEffect>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QSlider>
#include <QTimer>

#include <algorithm>

namespace doodlebold {

namespace {

class ReplayBar : public QWidget {
public:
    using QWidget::QWidget;

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        QColor fill = Theme::color(Theme::Token::Surface);
        fill.setAlpha(Theme::kFloatingAlpha);
        p.setBrush(fill);
        p.setPen(QPen(Theme::color(Theme::Token::Border), Theme::kBorder));
        p.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), Theme::kRadiusLg, Theme::kRadiusLg);
    }
};

}

TimelapseReplayView::TimelapseReplayView(std::shared_ptr<const Timelapse> recording, QWidget* parent)
    : QWidget(parent), m_recording(std::move(recording)), m_replay(*m_recording) {
    setObjectName(QStringLiteral("timelapseReplayView"));
    setFocusPolicy(Qt::StrongFocus);
    setAttribute(Qt::WA_NoMousePropagation);
    setMouseTracking(false);
    TimelapseExportOptions options;
    options.length = TimelapseLength::Full;
    options.holdSeconds = 0;
    options.leaveOutUndone = true;
    m_frames = planTimelapse(*m_recording, options).frames;

    m_bar = new ReplayBar(this);
    m_bar->setObjectName(QStringLiteral("replayBar"));
    auto* shadow = new QGraphicsDropShadowEffect(m_bar);
    shadow->setBlurRadius(Theme::kOverlayShadowBlur);
    shadow->setOffset(0, Theme::kOverlayShadowOffset);
    QColor shade = Theme::color(Theme::Token::Shadow);
    shade.setAlpha(Theme::kOverlayShadowAlpha);
    shadow->setColor(shade);
    m_bar->setGraphicsEffect(shadow);
    auto* row = new QHBoxLayout(m_bar);
    row->setContentsMargins(Theme::kSpace3, Theme::kSpace3, Theme::kSpace3, Theme::kSpace3);
    row->setSpacing(Theme::kSpace2);
    m_play = new QPushButton(tr("Play"), m_bar);
    m_play->setObjectName(QStringLiteral("replayPlay"));
    m_play->setProperty("role", "primary");
    m_play->setMinimumSize(72, Theme::compact::kControlLg);  // 72 px: "Pause" fits (component metric)
    m_play->setCursor(Qt::PointingHandCursor);
    m_play->setFocusPolicy(Qt::NoFocus);
    m_scrubber = new QSlider(Qt::Horizontal, m_bar);
    m_scrubber->setObjectName(QStringLiteral("replayScrubber"));
    m_scrubber->setRange(0, std::max(0, frameCount() - 1));
    m_scrubber->setFocusPolicy(Qt::NoFocus);
    m_clock = new QLabel(m_bar);
    m_clock->setObjectName(QStringLiteral("replayClock"));
    m_clock->setMinimumWidth(84);  // component metric: "0:00 / 0:00" fits
    m_clock->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    m_clock->setProperty("role", "hint");
    auto* exportButton = new QPushButton(tr("Export …"), m_bar);
    exportButton->setObjectName(QStringLiteral("replayExport"));
    exportButton->setMinimumHeight(Theme::compact::kControlLg);
    exportButton->setFocusPolicy(Qt::NoFocus);
    auto* done = new QPushButton(tr("Done"), m_bar);
    done->setObjectName(QStringLiteral("replayDone"));
    done->setMinimumHeight(Theme::compact::kControlLg);
    done->setFocusPolicy(Qt::NoFocus);
    row->addWidget(m_play);
    row->addWidget(m_scrubber, 1);
    row->addWidget(m_clock);
    row->addWidget(exportButton);
    row->addWidget(done);

    m_timer = new QTimer(this);
    connect(m_timer, &QTimer::timeout, this, &TimelapseReplayView::tick);
    connect(m_play, &QPushButton::clicked, this, [this] { play(!playing()); });
    connect(m_scrubber, &QSlider::sliderPressed, this, [this] { play(false); });
    connect(m_scrubber, &QSlider::valueChanged, this, [this](int frame) { showFrame(frame); });
    connect(exportButton, &QPushButton::clicked, this, [this] {
        play(false);
        emit exportRequested();
    });
    connect(done, &QPushButton::clicked, this, &TimelapseReplayView::doneRequested);
    m_scrubber->setValue(m_scrubber->maximum());
    showFrame(m_scrubber->maximum());
    placeBar();
}

int TimelapseReplayView::position() const {
    return m_scrubber->value();
}

void TimelapseReplayView::setPosition(int frame) {
    m_scrubber->setValue(frame);
}

bool TimelapseReplayView::playing() const {
    return m_timer->isActive();
}

void TimelapseReplayView::play(bool on) {
    if (on == playing() || (on && m_frames.empty()))
        return;
    if (!on) {
        m_timer->stop();
        m_play->setText(tr("Play"));
        m_fastScaling = false;
        m_shownStep = -1;
        showFrame(m_scrubber->value());
        return;
    }
    m_playFrom = m_scrubber->value() < m_scrubber->maximum() ? m_scrubber->value() : 0;
    m_fastScaling = true;
    m_clockStart.start();
    m_play->setText(tr("Pause"));
    m_timer->start(1000 / kTimelapseFps);
    tick();
}

// The frame due now: frames are skipped rather than slowing the video down when a picture takes longer than a frame.
void TimelapseReplayView::tick() {
    const int frame = m_playFrom + int(m_clockStart.elapsed() * kTimelapseFps / 1000);
    const int last = int(m_frames.size()) - 1;
    if (frame > last) {
        m_scrubber->setValue(last);
        play(false);
        return;
    }
    m_scrubber->setValue(frame);
}

void TimelapseReplayView::showFrame(int frame) {
    if (m_frames.empty())
        return;
    frame = std::clamp(frame, 0, int(m_frames.size()) - 1);
    syncClock(frame);
    const int step = m_frames[std::size_t(frame)];
    if (step == m_shownStep && !m_pixmap.isNull())
        return;
    m_shownStep = step;
    const QImage& image = m_replay.seek(step);
    m_pixmap = QPixmap::fromImage(
        image.scaled(pictureRect().size(), Qt::KeepAspectRatio, m_fastScaling ? Qt::FastTransformation : Qt::SmoothTransformation));
    update();
}

void TimelapseReplayView::syncClock(int frame) {
    m_clock->setText(
        tr("%1 / %2").arg(timelapseClock(double(frame) / kTimelapseFps), timelapseClock(double(m_frames.size()) / kTimelapseFps)));
}

QImage TimelapseReplayView::picture() {
    return m_shownStep < 0 ? QImage() : m_replay.seek(m_shownStep);
}

QRect TimelapseReplayView::pictureRect() const {
    const QSize canvas = m_replay.recording().finalSize();
    const QRect free = rect().adjusted(24, 16, -24, -(m_bar->height() + kBarBottom + 18));
    QSize size = canvas.isValid() ? canvas.scaled(free.size().expandedTo(QSize(1, 1)), Qt::KeepAspectRatio) : QSize();
    if (canvas.isValid() && size.width() > canvas.width())
        size = canvas;
    QRect r(QPoint(0, 0), size);
    r.moveCenter(free.center());
    return r;
}

QRect TimelapseReplayView::barRect() const {
    return m_bar->geometry();
}

void TimelapseReplayView::placeBar() {
    const int width = std::min(kBarWidth, std::max(200, this->width() - 32));
    m_bar->resize(width, m_bar->sizeHint().height());
    m_bar->move((this->width() - width) / 2, this->height() - kBarBottom - m_bar->height());
    m_bar->raise();
}

void TimelapseReplayView::resizeEvent(QResizeEvent*) {
    placeBar();
    m_shownStep = -1;
    showFrame(m_scrubber->value());
}

void TimelapseReplayView::showEvent(QShowEvent*) {
    setFocus();
}

void TimelapseReplayView::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.fillRect(rect(), Theme::color(Theme::Token::Window));
    if (!m_pixmap.isNull())
        p.drawPixmap(pictureRect().topLeft(), m_pixmap);
}

void TimelapseReplayView::keyPressEvent(QKeyEvent* e) {
    if (e->key() == Qt::Key_Escape) {
        emit doneRequested();
    } else if (e->key() == Qt::Key_Space && !e->isAutoRepeat()) {
        play(!playing());
    } else {
        e->accept();  // nothing else reaches the canvas
    }
}

}
