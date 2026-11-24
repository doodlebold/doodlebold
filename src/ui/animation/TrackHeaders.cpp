#include "ui/animation/TrackHeaders.h"

#include "core/document/Document.h"
#include "ui/Theme.h"
#include "ui/animation/FrameStrip.h"
#include "ui/animation/TrackLabels.h"

#include <QContextMenuEvent>
#include <QHelpEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QToolTip>

#include <algorithm>
#include <cmath>

namespace doodlebold {

namespace {

using Theme::Token;

// A row's controls, px: the eye and lock are kIcon square, kInset from the row's edges and kButtonGap apart; the chevron is
// smaller, kSign square. 18 is the icons' size in the layers panel, so an eye reads the same in both.
constexpr int kIcon = Theme::kIconMd;
constexpr int kSign = Theme::kIconSm;
constexpr int kInset = Theme::kSpace2;
constexpr int kButtonGap = Theme::kSpaceHair;
constexpr int kDragDistance = 5;
// The tile of what the track shows, px: as tall as the row allows up to kThumbMax (the mock's tiles are about two thirds of
// its rows' height), with corners of kThumbRadius.
constexpr int kThumbMax = 44;  // 52 before the compacting
constexpr double kThumbRadius = Theme::kRadiusSm;
// The volume bar, px: kVolumeHeight tall (its knob kVolumeKnob across), kInlineVolumeWidth wide beside the speaker. Chosen by
// eye: a knob a little smaller than the sliders' handles (12), as the row is smaller than a toolbar.
constexpr int kVolumeHeight = 4;  // component metric, described above
constexpr int kVolumeKnob = 10;
constexpr int kInlineVolumeWidth = 44;  // component metric, described above
// A chosen (selected) track's row, the accent at this alpha (0-255) over the panel: told apart from the current track's full
// accent at a glance. By eye.
constexpr int kChosenAlpha = 70;  // component metric, described above
// A labelled row's ground: the label colour's share over the panel's, more for the current track's. By eye on the 1280 px
// screenshots: at a half the colours read at a glance and the light text and icons stay clear; the current row at 0.7,
// with the accent's kCurrentFrame px frame round it, stands out from the others in any colour.
constexpr double kLabelShare = 0.5;
constexpr double kLabelCurrentShare = 0.7;
constexpr double kCurrentFrame = Theme::kBorderStrong;
// Above this lightness (0 … 1) a row's ground takes dark text: the light labels at the current row's share.
constexpr double kLightRow = 0.62;
// How faint a button that is off (an unlocked lock) is drawn: there, but not asking to be pressed.
constexpr double kOffOpacity = 0.3;
// The drawn signs (chevron, kind, speaker), px: one line width for all, as the layers panel's icons' strokes look at 18 px.
// Chosen by eye on the redesign's screenshots. The signs' own shapes are drawn in fractions of their box.
constexpr double kSignPen = Theme::kGlyphStroke;

}

TrackHeaders::TrackHeaders(FrameStrip* strip, QWidget* parent)
    : QWidget(parent),
      m_strip(strip),
      m_visible(QStringLiteral(":/icons/visible.svg")),
      m_hidden(QStringLiteral(":/icons/hidden.svg")),
      m_lock(QStringLiteral(":/icons/lock.svg")) {
    setObjectName(QStringLiteral("trackHeaders"));
    setFixedWidth(kWidth);
    setFocusPolicy(Qt::ClickFocus);  // Del deletes the selected tracks while the rows have the keyboard
    connect(strip, &FrameStrip::layoutChanged, this, qOverload<>(&QWidget::update));
    connect(strip, &FrameStrip::thumbnailsChanged, this, qOverload<>(&QWidget::update));
}

void TrackHeaders::setDocument(Document* doc) {
    if (m_doc)
        m_doc->disconnect(this);
    m_doc = doc;
    if (doc) {
        connect(doc, &Document::animationChanged, this, [this] {
            pruneSelection();
            update();
        });
        connect(doc, &Document::currentFrameChanged, this, qOverload<>(&QWidget::update));
    }
    update();
}

void TrackHeaders::setOffset(int y) {
    m_offset = y;
    update();
}

QSize TrackHeaders::sizeHint() const {
    return {kWidth, 100};
}

int TrackHeaders::trackCount() const {
    return m_doc ? std::max(1, m_doc->animation().trackCount()) : 0;
}

QRect TrackHeaders::trackRow(int track) const {
    if (track < 0 || track >= trackCount())
        return {};
    const QRect slot = m_strip->slotRect(track);
    return QRect(0, slot.top() - m_offset, width(), slot.height());
}

// The controls' line, in the row's middle.
int TrackHeaders::controlsTop(const QRect& row) const {
    return row.center().y() - kIcon / 2;
}

QRect TrackHeaders::thumbRect(const QRect& row) const {
    const int side = std::clamp(row.height() - 2 * kInset, 0, kThumbMax);
    return QRect(row.left() + kInset, row.center().y() - side / 2, side, side);
}

// From the right: the chevron, the eye, the lock, and a sound track's volume.
QRect TrackHeaders::chevronRect(const QRect& row) const {
    return QRect(row.right() - kInset - kSign + 1, row.center().y() - kSign / 2, kSign, kSign);
}

QRect TrackHeaders::eyeRect(const QRect& row) const {
    return QRect(chevronRect(row).left() - kInset - kIcon, controlsTop(row), kIcon, kIcon);
}

QRect TrackHeaders::lockRect(const QRect& row) const {
    return eyeRect(row).translated(-(kIcon + kButtonGap), 0);
}

QRect TrackHeaders::volumeRect(int track) const {
    if (!m_doc || track < 0 || track >= m_doc->animation().trackCount() ||
        m_doc->animation().tracks[static_cast<std::size_t>(track)].kind != TrackKind::Sound)
        return {};
    const QRect row = trackRow(track);
    return QRect(lockRect(row).left() - kInset - kInlineVolumeWidth, row.center().y() - kVolumeKnob / 2, kInlineVolumeWidth, kVolumeKnob);
}

int TrackHeaders::volumeAt(int track, int x) const {
    const QRect r = volumeRect(track).adjusted(kVolumeKnob / 2, 0, -kVolumeKnob / 2, 0);
    const double along = std::clamp(double(x - r.left()) / std::max(1, r.width()), 0.0, 1.0);
    return static_cast<int>(std::lround(along * AnimationTrack::kFullVolume));
}

int TrackHeaders::trackAt(const QPoint& pos) const {
    for (int t = 0; t < trackCount(); ++t)
        if (trackRow(t).contains(pos))
            return t;
    return -1;
}

int TrackHeaders::dropIndex(int y) const {
    if (const int t = trackAt(QPoint(width() / 2, y)); t >= 0)
        return t;
    return y < trackRow(trackCount() - 1).top() ? trackCount() - 1 : 0;
}

void TrackHeaders::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.fillRect(rect(), Theme::color(Token::Base));
    if (!m_doc)
        return;
    for (int t = 0; t < trackCount(); ++t)
        paintRow(p, t);
    if (m_dragging && m_dropTrack >= 0) {
        p.setPen(QPen(Theme::color(Token::AccentHover), Theme::kBorderStrong));
        p.setBrush(Qt::NoBrush);
        p.drawRect(QRectF(trackRow(m_dropTrack)).adjusted(1, 1, -1, -1));
    }
}

// A row filling the track's slot, a line between it and the next; the current track's filled with the accent, as the mock's.
void TrackHeaders::paintRow(QPainter& p, int track) {
    const QRect row = trackRow(track);
    if (!row.intersects(rect()))
        return;
    const Animation& a = m_doc->animation();
    const AnimationTrack* t = a.tracks.empty() ? nullptr : &a.tracks[static_cast<std::size_t>(track)];
    const bool current = !t || (t->hasFrames() && track == a.currentTrack);
    const bool hidden = t && t->hidden;
    const TrackKind kind = t ? t->kind : TrackKind::Frames;
    const bool selected = t && m_selected.contains(t->id);
    // Feedback: "Label coloring should color the entire block": a labelled track's row is its colour, over the panel's
    // ground, stronger for the current track, which also has the accent's frame round it; a row without a label is the
    // panel's, or the accent for the current track, as before. A chosen track has the accent faintly over that.
    const QColor label = track_labels::colour(t ? t->label : 0);
    const auto mixed = [](const QColor& base, const QColor& over, double share) {
        const auto towards = [share](float from, float to) { return static_cast<float>(from + (to - from) * share); };
        return QColor::fromRgbF(towards(base.redF(), over.redF()), towards(base.greenF(), over.greenF()),
                                towards(base.blueF(), over.blueF()));
    };
    const QColor ground = label.isValid() ? mixed(Theme::color(Token::Window), label, current ? kLabelCurrentShare : kLabelShare)
                                          : Theme::color(current ? Token::Accent : Token::Window);
    p.fillRect(row, ground);
    if (selected && !current) {
        QColor chosen = Theme::color(Token::Accent);
        chosen.setAlpha(kChosenAlpha);
        p.fillRect(row, chosen);
    }
    if (current && label.isValid()) {
        p.setPen(QPen(Theme::color(Token::Accent), kCurrentFrame));
        p.setBrush(Qt::NoBrush);
        p.drawRect(QRectF(row).adjusted(kCurrentFrame / 2, kCurrentFrame / 2, -kCurrentFrame / 2, -kCurrentFrame / 2 - 1));
    }
    p.setPen(Theme::color(Token::Base));
    p.drawLine(row.bottomLeft(), row.bottomRight());
    // Dark ink on a light row (a yellow or sea-foam label), the theme's light ink on the others.
    const QColor ink = hidden                            ? Theme::color(Token::TextDisabled)
                       : ground.lightnessF() > kLightRow ? Theme::color(Token::Base)
                                                         : Theme::color(current && !label.isValid() ? Token::AccentText : Token::Text);
    p.setRenderHint(QPainter::Antialiasing);
    paintThumb(p, thumbRect(row), track, kind);
    paintChevron(p, chevronRect(row), t && a.view.collapsed.contains(t->id), ink);
    if (kind == TrackKind::Sound)
        paintSpeaker(p, eyeRect(row), hidden, ink);
    else
        (hidden ? m_hidden : m_visible).paint(&p, eyeRect(row));
    p.save();
    p.setOpacity(t && t->locked ? 1.0 : kOffOpacity);
    m_lock.paint(&p, lockRect(row).adjusted(2, 2, -2, -2));
    p.restore();
    const QRect volume = volumeRect(track);
    if (!volume.isEmpty())
        paintVolume(p, volume, t->volume);
    QFont f = font();
    f.setWeight(current ? QFont::DemiBold : QFont::Normal);
    p.setFont(f);
    const int left = thumbRect(row).right() + kInset + kButtonGap;
    const int right = (!volume.isEmpty() ? volume.left() : lockRect(row).left()) - kInset;
    const QRect text(left, row.top(), right - left, row.height());
    p.setPen(ink);
    const QString name = t ? t->name : tr("Track 1");
    p.drawText(text, Qt::AlignLeft | Qt::AlignVCenter, QFontMetrics(f).elidedText(name, Qt::ElideRight, text.width()));
}

// › open, ⌄ folded: the mock's chevron points along the row.
void TrackHeaders::paintChevron(QPainter& p, const QRect& r, bool collapsed, const QColor& ink) const {
    const QRectF box = QRectF(r).adjusted(3, 3, -3, -3);
    QPainterPath path;
    if (!collapsed) {
        path.moveTo(box.left() + box.width() * 0.3, box.top());
        path.lineTo(box.right() - box.width() * 0.2, box.center().y());
        path.lineTo(box.left() + box.width() * 0.3, box.bottom());
    } else {
        path.moveTo(box.left(), box.top() + box.height() * 0.3);
        path.lineTo(box.center().x(), box.bottom() - box.height() * 0.2);
        path.lineTo(box.right(), box.top() + box.height() * 0.3);
    }
    p.setPen(QPen(ink, kSignPen, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.setBrush(Qt::NoBrush);
    p.drawPath(path);
}

// The track's tile: its frame's thumbnail at the playhead (or the video's picture); a sound's sign, and a frames track's
// until its thumbnail is made.
void TrackHeaders::paintThumb(QPainter& p, const QRect& r, int track, TrackKind kind) const {
    if (r.width() < kSign)
        return;
    QPainterPath tile;
    tile.addRoundedRect(QRectF(r), kThumbRadius, kThumbRadius);
    p.save();
    p.setClipPath(tile);
    p.fillRect(r, Theme::color(Token::Base));
    const QImage image = m_strip->shownThumbnail(track);
    if (!image.isNull()) {
        const double scale = std::max(double(r.width()) / image.width(), double(r.height()) / image.height());
        const QSizeF size(r.width() / scale, r.height() / scale);
        p.drawImage(QRectF(r), image, QRectF(QPointF((image.width() - size.width()) / 2, (image.height() - size.height()) / 2), size));
    } else {
        const int side = std::min(r.width(), kIcon);
        paintKind(p, QRect(r.center().x() - side / 2, r.center().y() - side / 2, side, side), kind);
    }
    p.restore();
    p.setPen(QPen(Theme::color(Token::BorderStrong), Theme::kBorder));
    p.setBrush(Qt::NoBrush);
    p.drawRoundedRect(QRectF(r).adjusted(0.5, 0.5, -0.5, -0.5), kThumbRadius, kThumbRadius);
}

// The track's kind, drawn: a frame (a picture's outline with a hill), a sound (bars of a waveform), the video (a play sign).
void TrackHeaders::paintKind(QPainter& p, const QRect& r, TrackKind kind) const {
    const QRectF box = QRectF(r).adjusted(1, 2, -1, -2);
    p.setPen(QPen(Theme::color(Token::TextMuted), kSignPen, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.setBrush(Qt::NoBrush);
    switch (kind) {
    case TrackKind::Frames: {
        p.drawRoundedRect(box, Theme::kRadiusXs, Theme::kRadiusXs);
        QPainterPath hill;
        hill.moveTo(box.left() + 1, box.bottom() - 1);
        hill.lineTo(box.left() + box.width() * 0.4, box.center().y());
        hill.lineTo(box.right() - 1, box.bottom() - 1);
        p.drawPath(hill);
        break;
    }
    case TrackKind::Sound: {
        const double heights[] = {0.35, 0.8, 0.55, 1.0, 0.45};  // a waveform's look, not a measurement
        const double step = box.width() / (std::size(heights) - 1);
        for (std::size_t i = 0; i < std::size(heights); ++i) {
            const double x = box.left() + static_cast<double>(i) * step, h = box.height() * heights[i] / 2;
            p.drawLine(QPointF(x, box.center().y() - h), QPointF(x, box.center().y() + h));
        }
        break;
    }
    case TrackKind::Video: {
        p.drawRoundedRect(box, Theme::kRadiusXs, Theme::kRadiusXs);
        QPainterPath play;
        play.moveTo(box.center().x() - 2, box.center().y() - 3);
        play.lineTo(box.center().x() + 3, box.center().y());
        play.lineTo(box.center().x() - 2, box.center().y() + 3);
        play.closeSubpath();
        p.setBrush(Theme::color(Token::TextMuted));
        p.drawPath(play);
        break;
    }
    }
}

// A speaker; crossed out when the track is muted (hidden).
void TrackHeaders::paintSpeaker(QPainter& p, const QRect& r, bool muted, const QColor& ink) const {
    const QRectF box = QRectF(r).adjusted(2, 3, -2, -3);
    const double w = box.width(), h = box.height();
    QPainterPath cone;
    cone.moveTo(box.left(), box.top() + h * 0.35);
    cone.lineTo(box.left() + w * 0.25, box.top() + h * 0.35);
    cone.lineTo(box.left() + w * 0.55, box.top());
    cone.lineTo(box.left() + w * 0.55, box.bottom());
    cone.lineTo(box.left() + w * 0.25, box.top() + h * 0.65);
    cone.lineTo(box.left(), box.top() + h * 0.65);
    cone.closeSubpath();
    p.setPen(QPen(ink, kSignPen, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.setBrush(ink);
    p.drawPath(cone);
    p.setBrush(Qt::NoBrush);
    if (muted) {
        p.drawLine(QPointF(box.left() + w * 0.7, box.top() + h * 0.3), QPointF(box.right(), box.bottom() - h * 0.3));
        p.drawLine(QPointF(box.left() + w * 0.7, box.bottom() - h * 0.3), QPointF(box.right(), box.top() + h * 0.3));
    } else {
        p.drawArc(QRectF(box.left() + w * 0.35, box.top() + h * 0.2, w * 0.5, h * 0.6), -50 * 16, 100 * 16);
    }
}

// A bar filled up to the volume, with a knob at it.
void TrackHeaders::paintVolume(QPainter& p, const QRect& r, int volume) const {
    const QRectF bar(r.left() + kVolumeKnob / 2.0, r.center().y() - kVolumeHeight / 2.0 + 0.5, r.width() - kVolumeKnob, kVolumeHeight);
    const double x = bar.left() + bar.width() * volume / AnimationTrack::kFullVolume;
    p.setPen(Qt::NoPen);
    p.setBrush(Theme::color(Token::Border));
    p.drawRoundedRect(bar, kVolumeHeight / 2.0, kVolumeHeight / 2.0);
    p.setBrush(Theme::color(Token::TextMuted));
    p.drawRoundedRect(QRectF(bar.left(), bar.top(), x - bar.left(), bar.height()), kVolumeHeight / 2.0, kVolumeHeight / 2.0);
    p.setBrush(Theme::color(Token::Accent));
    p.drawEllipse(QPointF(x, bar.center().y()), kVolumeKnob / 2.0, kVolumeKnob / 2.0);
}

void TrackHeaders::mousePressEvent(QMouseEvent* event) {
    if (!m_doc || event->button() != Qt::LeftButton || m_doc->animation().tracks.empty())
        return;
    const QPoint pos = event->position().toPoint();
    const int t = trackAt(pos);
    if (t < 0)
        return;
    const AnimationTrack& track = m_doc->animation().tracks[static_cast<std::size_t>(t)];
    const QRect row = trackRow(t);
    if (eyeRect(row).contains(pos)) {
        emit trackHiddenToggled(t, !track.hidden);
    } else if (lockRect(row).contains(pos)) {
        emit trackLockToggled(t, !track.locked);
    } else if (chevronRect(row).adjusted(-kInset / 2, -kInset / 2, kInset / 2, kInset / 2).contains(pos)) {
        emit collapseToggled(t);
    } else if (volumeRect(t).contains(pos)) {
        m_volumeTrack = t;
        emit volumeChanged(t, volumeAt(t, pos.x()), true);
    } else {
        selectByClick(t, event->modifiers());
        m_pressTrack = event->modifiers() & (Qt::ControlModifier | Qt::ShiftModifier) ? -1 : t;  // a plain press goes to the track
        m_pressPos = pos;
    }
}

// Feedback: "Multiple selection of tracks: Ctrl+click to toggle, Shift+click for a range"; a plain click selects that one.
void TrackHeaders::selectByClick(int track, Qt::KeyboardModifiers mods) {
    const auto& tracks = m_doc->animation().tracks;
    const int id = tracks[static_cast<std::size_t>(track)].id;
    if (mods & Qt::ShiftModifier) {
        const int anchor = std::max(0, m_doc->animation().indexOfTrack(m_anchor));
        if (!(mods & Qt::ControlModifier))
            m_selected.clear();
        for (int i = std::min(anchor, track); i <= std::max(anchor, track); ++i)
            m_selected.insert(tracks[static_cast<std::size_t>(i)].id);
    } else if (mods & Qt::ControlModifier) {
        if (!m_selected.erase(id))
            m_selected.insert(id);
        m_anchor = id;
    } else {
        m_selected = {id};
        m_anchor = id;
    }
    update();
}

std::vector<int> TrackHeaders::selectedTracks() const {
    std::vector<int> out;
    if (!m_doc)
        return out;
    const auto& tracks = m_doc->animation().tracks;
    for (int i = 0; i < static_cast<int>(tracks.size()); ++i)
        if (m_selected.contains(tracks[static_cast<std::size_t>(i)].id))
            out.push_back(i);
    return out;
}

void TrackHeaders::pruneSelection() {
    std::erase_if(m_selected, [this](int id) { return !m_doc || m_doc->animation().indexOfTrack(id) < 0; });
}

void TrackHeaders::keyPressEvent(QKeyEvent* event) {
    if ((event->key() == Qt::Key_Delete || event->key() == Qt::Key_Backspace) && !m_selected.empty()) {
        emit deleteTracksRequested(selectedTracks());
        return;
    }
    QWidget::keyPressEvent(event);
}

void TrackHeaders::mouseMoveEvent(QMouseEvent* event) {
    const QPoint pos = event->position().toPoint();
    if (m_volumeTrack >= 0) {
        emit volumeChanged(m_volumeTrack, volumeAt(m_volumeTrack, pos.x()), true);
        return;
    }
    if (m_pressTrack < 0)
        return;
    if (!m_dragging && std::abs(pos.y() - m_pressPos.y()) >= kDragDistance)
        m_dragging = true;
    if (m_dragging) {
        m_dropTrack = dropIndex(pos.y());
        setCursor(Qt::ClosedHandCursor);
        update();
    }
}

void TrackHeaders::mouseReleaseEvent(QMouseEvent* event) {
    if (const int volumeTrack = std::exchange(m_volumeTrack, -1); volumeTrack >= 0) {
        emit volumeChanged(volumeTrack, volumeAt(volumeTrack, event->position().toPoint().x()), false);
        return;
    }
    const int pressed = std::exchange(m_pressTrack, -1);
    const bool dragged = std::exchange(m_dragging, false);
    unsetCursor();
    update();
    if (pressed < 0)
        return;
    if (dragged) {
        const int to = dropIndex(event->position().toPoint().y());
        if (to != pressed)
            emit moveTrackRequested(pressed, to);
    } else {
        emit trackClicked(pressed);
    }
}

void TrackHeaders::mouseDoubleClickEvent(QMouseEvent* event) {
    const QPoint pos = event->position().toPoint();
    const int t = trackAt(pos);
    if (t < 0)
        return;
    const QRect row = trackRow(t);
    for (const QRect& control : {eyeRect(row), lockRect(row), chevronRect(row), volumeRect(t)})
        if (control.contains(pos))
            return;
    emit renameTrackRequested(t);
}

void TrackHeaders::contextMenuEvent(QContextMenuEvent* event) {
    const int t = trackAt(event->pos());
    if (t >= 0 && m_doc && !m_doc->animation().tracks.empty())
        emit trackMenuRequested(t, event->globalPos());
}

bool TrackHeaders::event(QEvent* event) {
    // Del is the rows' while they have the keyboard and tracks are selected, not the window's Clear (which empties a layer).
    if (event->type() == QEvent::ShortcutOverride) {
        const auto* key = static_cast<QKeyEvent*>(event);
        if ((key->key() == Qt::Key_Delete || key->key() == Qt::Key_Backspace) && !m_selected.empty()) {
            event->accept();
            return true;
        }
    }
    if (event->type() != QEvent::ToolTip || !m_doc)
        return QWidget::event(event);
    const auto* help = static_cast<QHelpEvent*>(event);
    const int t = trackAt(help->pos());
    const QRect row = trackRow(t);
    const bool sound =
        t >= 0 && t < m_doc->animation().trackCount() && m_doc->animation().tracks[static_cast<std::size_t>(t)].kind == TrackKind::Sound;
    QString tip;
    if (t >= 0 && eyeRect(row).contains(help->pos()))
        tip = sound ? tr("Mute the track: its sounds aren't played or exported")
                    : tr("Hide or show the track: a hidden track shows and plays nothing, and isn't exported");
    else if (t >= 0 && lockRect(row).contains(help->pos()))
        tip = tr("Lock the track: its frames, sounds or video can't be changed or painted");
    else if (t >= 0 && chevronRect(row).contains(help->pos()))
        tip = tr("Fold the track to a thin row, or open it");
    else if (t >= 0 && volumeRect(t).contains(help->pos()))
        tip = tr("The track's volume: %1%").arg(m_doc->animation().tracks[static_cast<std::size_t>(t)].volume);
    else if (t >= 0)
        tip = tr("Drag to move the track up or down; right-click for its menu");
    if (tip.isEmpty())
        QToolTip::hideText();
    else
        QToolTip::showText(help->globalPos(), tip, this);
    return true;
}

}
