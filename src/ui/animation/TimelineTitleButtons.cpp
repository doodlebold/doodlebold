#include "ui/animation/TimelineTitleButtons.h"

#include "core/document/Document.h"
#include "ui/ButtonTips.h"
#include "ui/Theme.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QToolButton>

namespace doodlebold {

namespace {

QToolButton* chipButton(const QString& text, const char* name, QWidget* parent) {
    auto* b = new QToolButton(parent);
    b->setObjectName(QString::fromLatin1(name));
    b->setText(text);
    b->setAutoRaise(true);
    b->setFocusPolicy(Qt::NoFocus);
    b->setFixedSize(16, Theme::compact::kControlSm);  // 16 px wide: the chevron and its room (component metric)
    b->setProperty("role", "chip");
    return b;
}

}

TimelineTitleButtons::TimelineTitleButtons(Document* doc, QWidget* parent)
    : QWidget(parent), m_doc(doc), m_chip(new QWidget(this)), m_frame(new QLabel(m_chip)), m_timeline(new QToolButton(this)) {
    setObjectName(QStringLiteral("timelineTitleButtons"));
    setFont(Theme::font(Theme::TextRole::Caption));

    m_chip->setObjectName(QStringLiteral("frameChip"));

    m_frame->setObjectName(QStringLiteral("frameChipText"));
    auto* previous = chipButton(QStringLiteral("‹"), "frameChipPrevious", m_chip);
    auto* next = chipButton(QStringLiteral("›"), "frameChipNext", m_chip);
    button_tips::setCommandTip(previous, tr("Previous Frame"), QStringLiteral("animation.previousFrame"));
    button_tips::setCommandTip(next, tr("Next Frame"), QStringLiteral("animation.nextFrame"));
    auto* chipRow = new QHBoxLayout(m_chip);
    chipRow->setContentsMargins(Theme::kSpace1, 0, Theme::kSpace1, 0);
    chipRow->setSpacing(Theme::kSpaceHair);
    chipRow->addWidget(previous);
    chipRow->addWidget(m_frame);
    chipRow->addWidget(next);
    connect(previous, &QToolButton::clicked, this, &TimelineTitleButtons::previousFrame);
    connect(next, &QToolButton::clicked, this, &TimelineTitleButtons::nextFrame);

    m_timeline->setObjectName(QStringLiteral("timelineButton"));
    m_timeline->setText(tr("Timeline"));
    m_timeline->setIcon(QIcon(QStringLiteral(":/icons/timeline.svg")));
    m_timeline->setIconSize(QSize(Theme::kIconSm, Theme::kIconSm));
    m_timeline->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_timeline->setCheckable(true);
    m_timeline->setFocusPolicy(Qt::NoFocus);
    m_timeline->setFixedHeight(Theme::compact::kControlSm);
    button_tips::setCommandTip(m_timeline, tr("Show Timeline"), QStringLiteral("canvas.showTimeline"));
    connect(m_timeline, &QToolButton::clicked, this, [this] {
        m_timeline->setChecked(timelineShown());  // the main window decides, and the canvas says so (sync)
        emit timelineToggled();
    });

    auto* row = new QHBoxLayout(this);
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(Theme::kSpace2);
    row->addWidget(m_chip);
    row->addWidget(m_timeline);
    if (doc) {
        connect(doc, &Document::animationChanged, this, &TimelineTitleButtons::sync);
        connect(doc, &Document::currentFrameChanged, this, &TimelineTitleButtons::sync);
    }
    sync();
}

bool TimelineTitleButtons::timelineShown() const {
    return m_doc && m_doc->animation().view.timelineShown;
}

QString TimelineTitleButtons::frameText() const {
    if (!m_doc)
        return {};
    const Animation& a = m_doc->animation();
    const int beat = a.beat + 1;
    return tr("Frame %1 / %2").arg(beat, 2, 10, QLatin1Char('0')).arg(a.totalBeats(), 2, 10, QLatin1Char('0'));
}

void TimelineTitleButtons::sync() {
    m_timeline->setChecked(timelineShown());
    const bool chip = !timelineShown() && m_doc && m_doc->animation().isAnimation();
    m_frame->setText(frameText());
    m_chip->setVisible(chip);
    const QSize before = size();
    adjustSize();
    if (size() != before)
        emit changed();
}

}
