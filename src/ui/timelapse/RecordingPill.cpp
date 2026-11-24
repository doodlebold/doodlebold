#include "ui/timelapse/RecordingPill.h"

#include "core/document/Document.h"
#include "ui/Theme.h"

#include <QAction>
#include <QCheckBox>
#include <QDateTime>
#include <QLabel>
#include <QLocale>
#include <QVBoxLayout>
#include <QWidgetAction>
#include <QFontMetrics>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>

#include <cmath>

namespace doodlebold {

namespace {

constexpr int kHeight = Theme::compact::kControlSm;
constexpr int kDot = 8;  // px: the recording dot (component metric)
constexpr int kPadding = Theme::kSpace2;
constexpr int kGap = Theme::kSpace2;

QFont pillFont(const QWidget*) {
    return Theme::font(Theme::TextRole::Caption);
}

}

RecordingPill::RecordingPill(Document* doc, QWidget* parent) : QWidget(parent), m_doc(doc) {
    setObjectName(QStringLiteral("recordingPill"));
    setCursor(Qt::PointingHandCursor);
    setAttribute(Qt::WA_Hover);
    setFocusPolicy(Qt::NoFocus);
    if (doc)
        connect(doc, &Document::recordingChanged, this, &RecordingPill::sync);
    sync();
}

Document* RecordingPill::document() const {
    return m_doc.data();
}

QString RecordingPill::groupedNumber(int n) {
    QString digits = QString::number(std::abs(n));
    for (int i = int(digits.size()) - 3; i > 0; i -= 3)
        digits.insert(i, QChar(0x2009));
    return n < 0 ? QLatin1Char('-') + digits : digits;
}

QString RecordingPill::infoText(int steps, std::int64_t startedMs, std::int64_t paintedMs, std::int64_t bytes) {
    const std::int64_t minutes = paintedMs / 60000;
    QString painted;
    if (minutes >= 60)
        painted = tr("%1 h %2 min").arg(minutes / 60).arg(minutes % 60);
    else if (minutes >= 1)
        painted = tr("%1 min").arg(minutes);
    else
        painted = tr("%1 s").arg(paintedMs / 1000);
    const double mb = double(bytes) / 1e6;
    const QString size = mb >= 10.0  ? tr("%1 MB").arg(std::lround(mb))
                         : mb >= 0.1 ? tr("%1 MB").arg(mb, 0, 'f', 1)
                                     : tr("%1 KB").arg(std::max<std::int64_t>(1, bytes / 1000));
    QString count = steps == 1 ? tr("1 step") : tr("%1 steps").arg(groupedNumber(steps));
    if (startedMs > 0)
        count = tr("%1 since %2").arg(count, QLocale().toString(QDateTime::fromMSecsSinceEpoch(startedMs).date(), tr("d MMM")));
    return tr("%1 · %2 of painting · %3 in the file").arg(count, painted, size);
}

QString RecordingPill::text() const {
    if (!m_doc)
        return {};
    if (!m_doc->isRecording())
        return tr("REC");
    const int steps = m_doc->recordedSteps();
    const QString count = steps == 1 ? tr("1 step") : tr("%1 steps").arg(groupedNumber(steps));
    return (m_doc->isRecordingPaused() ? tr("Paused · %1") : tr("REC · %1")).arg(count);
}

void RecordingPill::sync() {
    const bool recording = m_doc && m_doc->isRecording();
    setVisible(m_doc && !(parentWidget() && parentWidget()->isMinimized()));
    setAccessibleName(text());
    setToolTip(recording ? tr("Timelapse recording") : tr("Not recording: press to start a timelapse recording"));
    resize(sizeHint());  // the window places it again (emit changed) so that it stays at the header's right end
    updateGeometry();
    update();
    emit changed();
}

QSize RecordingPill::sizeHint() const {
    const QFontMetrics fm(pillFont(this));
    return {kPadding + kDot + kGap + fm.horizontalAdvance(text()) + kPadding, kHeight};
}

void RecordingPill::paintEvent(QPaintEvent*) {
    if (!m_doc)
        return;
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const bool off = !m_doc->isRecording();
    const bool paused = m_doc->isRecordingPaused();
    using Token = Theme::Token;
    p.setPen(Qt::NoPen);
    p.setBrush(Theme::color(m_hover ? Token::Hover : Token::Base));
    p.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), kHeight / 2.0, kHeight / 2.0);
    p.setBrush(paused || off ? Theme::color(Token::TextDisabled) : Theme::color(Token::Danger));
    p.drawEllipse(QRectF(kPadding, (kHeight - kDot) / 2.0, kDot, kDot));
    p.setFont(pillFont(this));
    p.setPen(Theme::color(off ? Token::TextDisabled : paused ? Token::TextMuted : Token::Text));
    p.drawText(QRect(kPadding + kDot + kGap, 0, width() - kPadding - kDot - kGap - kPadding + 4, kHeight), Qt::AlignVCenter | Qt::AlignLeft,
               text());
}

QMenu* RecordingPill::buildMenu(QWidget* parent) {
    auto* menu = new QMenu(parent);
    menu->setObjectName(QStringLiteral("recordingMenu"));
    if (!m_doc)
        return menu;
    auto* heading = new QWidget(menu);
    heading->setObjectName(QStringLiteral("recordingMenuHeading"));
    auto* box = new QVBoxLayout(heading);
    box->setContentsMargins(Theme::kSpace3, Theme::kSpace2, Theme::kSpace3, Theme::kSpace2);  // as the menu's items
    box->setSpacing(Theme::kSpaceHair);
    auto* title = new QLabel(tr("Timelapse of this canvas"), heading);
    title->setObjectName(QStringLiteral("recordingMenuTitle"));
    title->setFont(Theme::font(Theme::TextRole::BodyStrong));
    const bool recording = m_doc->isRecording();
    auto* details = new QLabel(
        recording ? infoText(m_doc->recordedSteps(), m_doc->recordingStartedMs(), m_doc->recordedPaintingMs(), m_doc->recordedBytes())
                  : tr("Not recording"),
        heading);
    details->setObjectName(QStringLiteral("recordingMenuDetails"));
    details->setFont(Theme::font(Theme::TextRole::Caption));
    details->setProperty("role", "hint");
    box->addWidget(title);
    box->addWidget(details);
    auto* headingAction = new QWidgetAction(menu);
    headingAction->setObjectName(QStringLiteral("recordingInfo"));
    headingAction->setDefaultWidget(heading);
    menu->addAction(headingAction);
    menu->addSeparator();
    if (!recording) {
        QAction* start = menu->addAction(tr("Record Timelapse"));
        start->setObjectName(QStringLiteral("recordingStart"));
        connect(start, &QAction::triggered, this, [this] { emit commandRequested(Command::Start); });
    }
    const bool paused = m_doc->isRecordingPaused();
    QAction* pause = recording ? menu->addAction(paused ? tr("Resume Recording") : tr("Pause Recording")) : nullptr;
    if (pause) {
        pause->setObjectName(QStringLiteral("recordingPause"));
        connect(pause, &QAction::triggered, this, [this, paused] { emit commandRequested(paused ? Command::Resume : Command::Pause); });
    }
    QAction* replay = menu->addAction(tr("Timelapse Replay"));
    replay->setObjectName(QStringLiteral("recordingReplay"));
    connect(replay, &QAction::triggered, this, [this] { emit commandRequested(Command::Replay); });
    QAction* exportAction = menu->addAction(tr("Export Timelapse …"));
    exportAction->setObjectName(QStringLiteral("recordingExport"));
    connect(exportAction, &QAction::triggered, this, [this] { emit commandRequested(Command::Export); });
    QAction* restart = menu->addAction(tr("Restart the Recording …"));
    restart->setObjectName(QStringLiteral("recordingRestart"));
    connect(restart, &QAction::triggered, this, [this] { emit commandRequested(Command::Restart); });
    QAction* del = menu->addAction(tr("Delete the Recording …"));
    del->setObjectName(QStringLiteral("recordingDelete"));
    connect(del, &QAction::triggered, this, [this] { emit commandRequested(Command::Delete); });
    for (QAction* a : {replay, exportAction, restart, del})
        a->setEnabled(recording);
    return menu;
}

void RecordingPill::mousePressEvent(QMouseEvent* e) {
    if (e->button() != Qt::LeftButton) {
        e->ignore();
        return;
    }
    QMenu* menu = buildMenu(this);
    menu->setAttribute(Qt::WA_DeleteOnClose);
    menu->popup(mapToGlobal(QPoint(0, height() + 4)));
}

void RecordingPill::enterEvent(QEnterEvent*) {
    m_hover = true;
    update();
}

void RecordingPill::leaveEvent(QEvent*) {
    m_hover = false;
    update();
}

}
