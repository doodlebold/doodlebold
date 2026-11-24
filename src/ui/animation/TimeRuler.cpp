#include "ui/animation/TimeRuler.h"

#include "core/document/Document.h"
#include "ui/Theme.h"
#include "ui/animation/FrameStrip.h"

#include <QMouseEvent>
#include <QPainter>

#include <algorithm>
#include <cmath>

namespace doodlebold {

namespace {

using Theme::Token;

// The ticks' lengths, px, from the ruler's bottom: a labelled second's runs up to the labels, another second's is the middle
// length, a beat's the shortest. Three lengths tell a second from a beat without reading the labels.
constexpr int kSecondTick = 11;
constexpr int kBeatTick = 5;
constexpr int kLabelTop = Theme::kSpaceHair;  // from the top to the labels (the labels and the chip are captions)
// The playhead's chip, px: kChipHeight tall, kChipPadding either side of its number.
constexpr int kChipHeight = 16;  // component metric, described above
constexpr int kChipPadding = Theme::kSpace1;
constexpr double kPlayheadWidth = Theme::kBorderStrong;  // as the line through the lanes (FrameStrip)

}

TimeRuler::TimeRuler(FrameStrip* strip, QWidget* parent) : QWidget(parent), m_strip(strip) {
    setObjectName(QStringLiteral("timeRuler"));
    setFixedHeight(kHeight);
    setCursor(Qt::PointingHandCursor);
    connect(strip, &FrameStrip::layoutChanged, this, qOverload<>(&QWidget::update));
}

void TimeRuler::setDocument(Document* doc) {
    if (m_doc)
        m_doc->disconnect(this);
    m_doc = doc;
    if (doc) {
        connect(doc, &Document::animationChanged, this, qOverload<>(&QWidget::update));
        connect(doc, &Document::currentFrameChanged, this, qOverload<>(&QWidget::update));
    }
    update();
}

void TimeRuler::setOffset(int x) {
    m_offset = x;
    update();
}

QSize TimeRuler::sizeHint() const {
    return {200, kHeight};
}

int TimeRuler::beatLabelStep(int beatWidth, int perSecond) {
    perSecond = std::max(1, perSecond);
    for (int step = 1; step <= perSecond; ++step)
        if (perSecond % step == 0 && step * beatWidth >= kLabelSpacing)
            return step;
    for (int seconds : {2, 5, 10, 15, 30, 60, 120, 300, 600})
        if (seconds * perSecond * beatWidth >= kLabelSpacing)
            return seconds * perSecond;
    return 1200 * perSecond;
}

QString TimeRuler::beatLabel(int beat) {
    return QStringLiteral("%1f").arg(beat);
}

QString TimeRuler::beatLabel(int beat, double fps, bool asTime) {
    return asTime ? QStringLiteral("%1s").arg(beat / std::max(fps, Animation::kMinFps), 0, 'f', 2) : beatLabel(beat);
}

void TimeRuler::setShowTime(bool asTime) {
    m_showTime = asTime;
    update();
}

QString TimeRuler::label(int beat) const {
    return beatLabel(beat, m_doc ? m_doc->animation().fps : Animation{}.fps, m_showTime);
}

int TimeRuler::xOfBeat(int beat) const {
    return m_strip->xOfBeat(beat) - m_offset;
}

int TimeRuler::beatAt(int x) const {
    return std::max(0, static_cast<int>(std::floor(double(x + m_offset - FrameStrip::kMargin) / m_strip->beatWidth())));
}

void TimeRuler::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.fillRect(rect(), Theme::color(Token::Window));
    p.setPen(Theme::color(Token::Border));
    p.drawLine(rect().bottomLeft(), rect().bottomRight());
    if (!m_doc)
        return;
    const double fps = m_doc->animation().fps;
    const int perSecond = std::max(1, static_cast<int>(std::lround(fps)));
    const int bw = m_strip->beatWidth();
    const int step = beatLabelStep(bw, perSecond);
    const int first = beatAt(0), last = beatAt(width()) + 1;
    QFont f = Theme::font(Theme::TextRole::Caption);
    p.setFont(f);
    const int labelHeight = QFontMetrics(f).height();
    const QRect chip = chipRect();
    for (int beat = first; beat <= last; ++beat) {
        const int x = xOfBeat(beat);
        const bool labelled = beat % step == 0;
        const bool second = beat % perSecond == 0;
        if (!labelled && bw < 5 && !second)
            continue;  // no room for a tick per beat
        const int tick = labelled ? kHeight - kLabelTop - labelHeight / 2 : second ? kSecondTick : kBeatTick;
        p.setPen(Theme::color(labelled ? Token::TextMuted : Token::BorderStrong));
        p.drawLine(QPoint(x, kHeight - 1 - tick), QPoint(x, kHeight - 1));
        const QRect at(x + 4, kLabelTop, QFontMetrics(f).horizontalAdvance(label(beat)), labelHeight);
        if (labelled && !at.intersects(chip)) {  // the chip says it
            p.setPen(Theme::color(Token::Text));
            p.drawText(at, Qt::AlignLeft | Qt::AlignVCenter, label(beat));
        }
    }
    paintPlayhead(p);
}

// The playhead: a line at its beat's start, and over it a chip with its beat, as the labels write it, kept inside the ruler.
QRect TimeRuler::chipRect() const {
    QFont f = Theme::font(Theme::TextRole::Caption);
    f.setWeight(QFont::DemiBold);
    const int x = xOfBeat(m_doc->beat());
    const int w = QFontMetrics(f).horizontalAdvance(label(m_doc->beat())) + 2 * kChipPadding;
    return QRect(std::clamp(x - w / 2, 0, std::max(0, width() - w)), kLabelTop, w, kChipHeight);
}

void TimeRuler::paintPlayhead(QPainter& p) {
    const int x = xOfBeat(m_doc->beat());
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QPen(Theme::color(Token::AccentHover), kPlayheadWidth));
    p.drawLine(QPointF(x, kLabelTop + kChipHeight), QPointF(x, kHeight));
    QFont f = Theme::font(Theme::TextRole::Caption);
    f.setWeight(QFont::DemiBold);
    p.setFont(f);
    const QRect chip = chipRect();
    p.setPen(Qt::NoPen);
    p.setBrush(Theme::color(Token::AccentHover));
    p.drawRoundedRect(chip, Theme::kRadiusXs, Theme::kRadiusXs);
    p.setPen(Theme::color(Token::AccentText));
    p.drawText(chip, Qt::AlignCenter, label(m_doc->beat()));
}

void TimeRuler::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton)
        scrub(event->position().toPoint().x());
}

void TimeRuler::mouseMoveEvent(QMouseEvent* event) {
    if (event->buttons() & Qt::LeftButton)
        scrub(event->position().toPoint().x());
}

void TimeRuler::scrub(int x) {
    if (m_doc && !m_doc->animation().tracks.empty())
        (void)m_doc->goToBeat(beatAt(x));
}

}
