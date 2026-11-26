#include "ui/animation/TimelinePanel.h"

#include "core/document/Document.h"
#include "ui/ButtonTips.h"
#include "ui/animation/TrackLabels.h"
#include "core/util/AppIdentity.h"
#include "ui/Theme.h"
#include "ui/animation/FrameStrip.h"
#include "ui/animation/TimeRuler.h"
#include "ui/animation/TrackHeaders.h"

#include <QAbstractButton>
#include <QActionGroup>
#include <QApplication>
#include <QFileInfo>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLineEdit>
#include <QLabel>
#include <QSettings>
#include <QMenu>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSlider>
#include <QSpinBox>
#include <QToolButton>
#include <QUndoStack>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <QWidgetAction>

#include <algorithm>
#include <optional>
#include <cmath>

namespace doodlebold {

namespace {

// The timeline's questions (hold, speed, a sound's start and end, a track's name) as QInputDialog's, but not to be stretched:
// feedback on the Add Video dialog: "it should not be able to be stretched". Each is as large as its contents.
namespace fixedDialog {

bool run(QInputDialog& d) {
    d.ensurePolished();  // the style sheet's control heights first, or the size is measured short
    d.setFixedSize(d.sizeHint());
    return d.exec() == QDialog::Accepted;
}

int getInt(QWidget* parent, const QString& title, const QString& label, int value, int min, int max, int step, bool* ok) {
    QInputDialog d(parent);
    d.setWindowTitle(title);
    d.setLabelText(label);
    d.setIntRange(min, max);
    d.setIntStep(step);
    d.setIntValue(value);
    *ok = run(d);
    return d.intValue();
}

double getDouble(QWidget* parent, const QString& title, const QString& label, double value, double min, double max, int decimals,
                 bool* ok) {
    QInputDialog d(parent);
    d.setWindowTitle(title);
    d.setLabelText(label);
    d.setDoubleDecimals(decimals);
    d.setDoubleRange(min, max);
    d.setDoubleValue(value);
    *ok = run(d);
    return d.doubleValue();
}

QString getText(QWidget* parent, const QString& title, const QString& label, QLineEdit::EchoMode mode, const QString& text, bool* ok) {
    QInputDialog d(parent);
    d.setWindowTitle(title);
    d.setLabelText(label);
    d.setTextEchoMode(mode);
    d.setTextValue(text);
    *ok = run(d);
    return d.textValue();
}

}

int nextOpacityKey() {
    static int key = 0x0f1d0000;
    return ++key;
}

// The bars' spacing, px: kBarGap between the controls of the top and bottom bars, kBarGroupGap between the mock's groups
// (the Timeline menu and undo / redo; Frame and Play; the right-hand buttons). They were 6 and 14, as the mock spaces them;
// feedback found "alot of empty space, compact it a little more".
constexpr int kBarGap = Theme::compact::kControlGap;
constexpr int kBarGroupGap = Theme::kSpace2;
// The rows of the panel (bars, video bar, lanes), px apart: the panels' row gap (7) left the "empty space".
constexpr int kRowGap = Theme::compact::kStackGap;
// The switches in the menus (Onion Skin, Loop, Show Time) in place of the platform's tick, which feedback found strange
// ("Change it to something else that looks good cross-platform"): a rounded track twice as wide as tall with a knob
// kSwitchInset px in, as the mock's Keyframe switch, drawn so it looks the same everywhere.
constexpr int kSwitchWidth = 32;  // component metric: the menu switch
constexpr int kSwitchHeight = 18;  // component metric: the menu switch
constexpr double kSwitchInset = Theme::kSpaceHair;
// The mark beside the chosen speed in its menu, px square: a drawn dot in the accent, again in place of the platform's tick.
constexpr int kMarkSize = Theme::kIconMd;
constexpr double kMarkRadius = Theme::kRadiusXs;
// The settings key of the frames-or-time choice (feedback: "the choice is saved in settings").
const QString kShowTimeKey = QStringLiteral("timeline/showTime");
// The Height slider in the bottom bar, px: master's width for it.
constexpr int kHeightSliderWidth = 70;  // component metric, described above
// The ⋯ menu's slider (the onion skin's strength) and the bottom bar's zoom, px: the mock's zoom slider is about 110 px beside
// a 1774 px wide panel; 100 fitted the 1280 px window until Height came back beside it. Now master's Height slider's
// width, 70, for both sliders of the bottom bar, keeping it compact; where it still doesn't fit, its right half wraps.
constexpr int kSliderWidth = kHeightSliderWidth;
// The drawn icons (transport, maximise), px square, and their lines' width.
constexpr int kGlyphSize = Theme::kIconMd;
constexpr double kGlyphPen = Theme::kGlyphStroke;
// The track headers' bar's icon buttons, px square: the mock's eye, lock, duplicate and delete, the size of the layers panel's.
constexpr int kTrackBarButton = Theme::compact::kControlMd;  // 26 before the compacting
// A wheel's notch in Qt's angle units (1/8 degree; 15 degrees a notch).
constexpr double kWheelNotch = 120.0;
// Ctrl+wheel: each notch makes the frames this many times wider (or narrower): the narrowest to the widest in about a dozen
// notches. 1.1 would need two dozen; 1.5 jumps past the width wanted.
constexpr double kZoomPerNotch = 1.25;
// Ctrl+Shift+wheel: each notch changes the tracks' height by this many percent points: the smallest to the full height in
// seven notches.
constexpr double kHeightPerNotch = 10.0;  // ui-tokens: allow: percent points of height a wheel notch, not a size
// Under this width, px, the track column folds away: it would leave the lanes less than its own width. At the narrowest
// window (745 px here, the side panels taking the rest) the timeline is about 190 px wide.
constexpr int kCompactWidth = 2 * TrackHeaders::kWidth;
// The zoom's − and + buttons: each press this many times narrower or wider, as a notch of Ctrl+wheel.
constexpr double kZoomPerPress = kZoomPerNotch;

enum class Glyph {
    First,
    Previous,
    Play,
    Stop,
    Next,
    Last,
    Maximize
};

// The transport's and the maximise toggle's icons, drawn: the theme's icons have none of them, and the mock's are these
// plain shapes.
QIcon glyphIcon(Glyph glyph) {
    const qreal ratio = qApp->devicePixelRatio();
    QPixmap pixmap(QSize(kGlyphSize, kGlyphSize) * ratio);
    pixmap.setDevicePixelRatio(ratio);
    pixmap.fill(Qt::transparent);
    QPainter p(&pixmap);
    p.setRenderHint(QPainter::Antialiasing);
    const QColor ink = Theme::color(Theme::Token::Text);
    p.setPen(QPen(ink, kGlyphPen, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.setBrush(ink);
    const double s = kGlyphSize;
    const auto triangle = [&](double left, double right) {  // pointing from left to right (or back when right < left)
        QPainterPath path;
        path.moveTo(left, s * 0.2);
        path.lineTo(right, s * 0.5);
        path.lineTo(left, s * 0.8);
        path.closeSubpath();
        p.drawPath(path);
    };
    switch (glyph) {
    case Glyph::First:
        p.drawLine(QPointF(s * 0.22, s * 0.2), QPointF(s * 0.22, s * 0.8));
        triangle(s * 0.8, s * 0.32);
        break;
    case Glyph::Previous: triangle(s * 0.7, s * 0.3); break;
    case Glyph::Play: triangle(s * 0.3, s * 0.8); break;
    case Glyph::Stop: p.drawRect(QRectF(s * 0.28, s * 0.28, s * 0.44, s * 0.44)); break;
    case Glyph::Next: triangle(s * 0.3, s * 0.7); break;
    case Glyph::Last:
        triangle(s * 0.2, s * 0.68);
        p.drawLine(QPointF(s * 0.78, s * 0.2), QPointF(s * 0.78, s * 0.8));
        break;
    case Glyph::Maximize: {
        p.setBrush(Qt::NoBrush);
        const double a = s * 0.18, b = s * 0.82, l = s * 0.22;  // the corners' brackets
        for (const QPointF& c : {QPointF(a, a), QPointF(b, a), QPointF(a, b), QPointF(b, b)}) {
            const double dx = c.x() < s / 2 ? l : -l, dy = c.y() < s / 2 ? l : -l;
            p.drawPolyline(QPolygonF({QPointF(c.x() + dx, c.y()), c, QPointF(c.x(), c.y() + dy)}));
        }
        break;
    }
    }
    return QIcon(pixmap);
}

// "1.0x", "0.25x", "3.3x": one decimal when that says it, else two.
QString speedText(double speed) {
    const bool tenths = std::abs(speed * 10 - std::round(speed * 10)) < 1e-9;
    return QStringLiteral("%1x").arg(speed, 0, 'f', tenths ? 1 : 2);
}

// A switch, on or off.
QPixmap switchPixmap(bool on) {
    const qreal ratio = qApp->devicePixelRatio();
    QPixmap pixmap(QSize(kSwitchWidth, kSwitchHeight) * ratio);
    pixmap.setDevicePixelRatio(ratio);
    pixmap.fill(Qt::transparent);
    QPainter p(&pixmap);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen);
    p.setBrush(Theme::color(on ? Theme::Token::Accent : Theme::Token::BorderStrong));
    p.drawRoundedRect(QRectF(0, 0, kSwitchWidth, kSwitchHeight), kSwitchHeight / 2.0, kSwitchHeight / 2.0);
    const double knob = kSwitchHeight - 2.0 * kSwitchInset;
    p.setBrush(Theme::color(Theme::Token::AccentText));
    p.drawEllipse(QRectF(on ? kSwitchWidth - kSwitchInset - knob : kSwitchInset, kSwitchInset, knob, knob));
    return pixmap;
}

// The chosen item's mark in a menu: a dot in the accent (empty: the others', so the items' text lines up).
QIcon markIcon(bool chosen) {
    const qreal ratio = qApp->devicePixelRatio();
    QPixmap pixmap(QSize(kMarkSize, kMarkSize) * ratio);
    pixmap.setDevicePixelRatio(ratio);
    pixmap.fill(Qt::transparent);
    if (chosen) {
        QPainter p(&pixmap);
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(Qt::NoPen);
        p.setBrush(Theme::color(Theme::Token::Accent));
        p.drawEllipse(QPointF(kMarkSize / 2.0, kMarkSize / 2.0), kMarkRadius, kMarkRadius);
    }
    return QIcon(pixmap);
}

// A swatch of a track's label colour for its menu.
QIcon labelIcon(int label) {
    const qreal ratio = qApp->devicePixelRatio();
    QPixmap pixmap(QSize(kMarkSize, kMarkSize) * ratio);
    pixmap.setDevicePixelRatio(ratio);
    pixmap.fill(Qt::transparent);
    QPainter p(&pixmap);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QPen(Theme::color(Theme::Token::BorderStrong), Theme::kBorder));
    p.setBrush(label ? QBrush(track_labels::colour(label)) : QBrush(Qt::NoBrush));
    p.drawRoundedRect(QRectF(2.5, 2.5, kMarkSize - 5, kMarkSize - 5), Theme::kRadiusXs, Theme::kRadiusXs);
    return QIcon(pixmap);
}

// The play range's fields: beats from 0, shown as frames ("12f") or as time ("0.50s"), as the ruler shows them.
class BeatSpinBox : public QSpinBox {
public:
    using QSpinBox::QSpinBox;
    void setUnits(double fps, bool asTime) {
        m_fps = fps;
        m_asTime = asTime;
        setSuffix(QString());  // the text says its unit
        lineEdit()->setText(textFromValue(value()));
        updateGeometry();
    }

protected:
    QString textFromValue(int beat) const override { return TimeRuler::beatLabel(beat, m_fps, m_asTime); }
    int valueFromText(const QString& text) const override {
        QString t = text;
        t.remove(prefix());
        t = t.trimmed();
        if (t.endsWith(QLatin1Char('s')))
            return static_cast<int>(std::lround(t.chopped(1).toDouble() * m_fps));
        if (t.endsWith(QLatin1Char('f')))
            t.chop(1);
        return t.toInt();
    }
    QValidator::State validate(QString&, int&) const override { return QValidator::Acceptable; }

private:
    double m_fps = 12.0;
    bool m_asTime = false;
};

}

TimelinePanel::TimelinePanel(QWidget* parent)
    : QWidget(parent), m_strip(new FrameStrip), m_headers(new TrackHeaders(m_strip, this)), m_scroll(new QScrollArea(this)) {
    setObjectName(QStringLiteral("timelinePanel"));
    m_videoBar = new QWidget(this);
    m_videoText = new QLabel(m_videoBar);
    m_videoText->setObjectName(QStringLiteral("timelineVideoText"));
    m_videoStatus = new QLabel(m_videoBar);
    m_videoStatus->setProperty("role", "hint");
    // The video's name and how far its pictures are read can be long: they are cut short rather than widen the window.
    m_videoText->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    m_videoStatus->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    auto* videoRow = new QHBoxLayout(m_videoBar);
    videoRow->setContentsMargins(0, 0, 0, 0);
    videoRow->addWidget(m_videoText);
    videoRow->addWidget(m_videoStatus);
    videoRow->addStretch(1);
    m_videoGroup = makeVideoGroup();
    m_videoGroup->setParent(m_videoBar);
    videoRow->addWidget(m_videoGroup);

    m_scroll->setObjectName(QStringLiteral("timelineScroll"));
    m_scroll->setWidget(m_strip);
    m_scroll->setWidgetResizable(false);
    m_scroll->setFrameShape(QFrame::NoFrame);
    m_scroll->setMinimumHeight(FrameStrip::kCellHeight + FrameStrip::kLabelHeight + 4 * FrameStrip::kMargin +
                               20);  // component metric: the lanes' least room
    m_scroll->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    m_ruler = new TimeRuler(m_strip, this);
    auto* addTrack = makeButton(tr("+ Add Track"), "timelineAddTrackButton", tr("A new track above the current one, drawn over it"));
    addTrack->setProperty("role", "timelineAddRow");
    connect(addTrack, &QPushButton::clicked, this, &TimelinePanel::addTrackRequested);
    // + Add Track has a row of its own under the track rows, beside the lanes' scroll bar: under the rows in the same cell it
    // hid the last of them, which couldn't scroll past it (with six tracks), and was cut off at the bottom.
    addTrack->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    m_side = addTrack;
    auto* lanes = new QGridLayout;
    lanes->setSpacing(0);
    lanes->setVerticalSpacing(kRowGap);
    m_trackBar = makeTrackBar();
    lanes->addWidget(m_trackBar, 0, 0);
    lanes->addWidget(m_ruler, 0, 1);
    lanes->addWidget(m_headers, 1, 0);
    lanes->addWidget(m_scroll, 1, 1);  // as tall as the rows, so the bottom track scrolls into sight above + Add Track
    lanes->addWidget(addTrack, 2, 0);
    lanes->setColumnStretch(1, 1);
    lanes->setRowStretch(1, 1);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(kRowGap);
    layout->addWidget(makeTopBar());
    layout->addWidget(m_videoBar);
    layout->addLayout(lanes, 1);
    layout->addWidget(makeBottomBar());

    connect(m_strip, &FrameStrip::selectionChanged, this, [this] {
        syncButtons();
        emit selectionChanged();
    });
    connect(m_strip, &FrameStrip::refused, this, &TimelinePanel::refused);
    connect(m_strip, &FrameStrip::frameMenuRequested, this, &TimelinePanel::showFrameMenu);
    connect(m_strip, &FrameStrip::soundMenuRequested, this, &TimelinePanel::showSoundMenu);
    connect(m_strip, &FrameStrip::removeSoundsRequested, this, &TimelinePanel::removeSoundsRequested);
    connect(m_strip, &FrameStrip::playRequested, this, &TimelinePanel::playRequested);
    connect(m_scroll->verticalScrollBar(), &QScrollBar::valueChanged, m_headers, &TrackHeaders::setOffset);
    connect(m_scroll->horizontalScrollBar(), &QScrollBar::valueChanged, m_ruler, &TimeRuler::setOffset);
    connect(m_headers, &TrackHeaders::trackClicked, this, [this](int track) {
        const Animation* a = m_doc ? &m_doc->animation() : nullptr;
        if (a && track < a->trackCount() && a->tracks[static_cast<std::size_t>(track)].hasFrames())
            report(m_doc->goToCell(track, a->tracks[static_cast<std::size_t>(track)].frameAtBeat(m_doc->beat())));
    });
    connect(m_headers, &TrackHeaders::trackHiddenToggled, this, &TimelinePanel::trackHiddenRequested);
    connect(m_headers, &TrackHeaders::trackLockToggled, this, &TimelinePanel::trackLockedRequested);
    connect(m_headers, &TrackHeaders::renameTrackRequested, this, &TimelinePanel::askTrackName);
    connect(m_headers, &TrackHeaders::trackMenuRequested, this, &TimelinePanel::showTrackMenu);
    connect(m_headers, &TrackHeaders::collapseToggled, this, &TimelinePanel::toggleCollapsed);
    connect(m_headers, &TrackHeaders::deleteTracksRequested, this, [this](const std::vector<int>& tracks) {
        if (m_doc)
            report(m_doc->deleteTracks(tracks));
    });
    // A drag on a sound track's volume bar is one undo step, its release included.
    connect(m_headers, &TrackHeaders::volumeChanged, this, [this](int track, int volume, bool dragging) {
        if (!m_doc)
            return;
        if (dragging && !m_volumeKey)
            m_volumeKey = nextOpacityKey();
        report(m_doc->setTrackVolume(track, volume, m_volumeKey));
        if (!dragging)
            m_volumeKey = 0;
    });
    connect(m_headers, &TrackHeaders::moveTrackRequested, this, [this](int track, int to) {
        if (m_doc)
            report(m_doc->moveTrack(track, to));
    });
    connect(m_strip, &FrameStrip::clearFramesRequested, this, &TimelinePanel::clearFramesRequested);
    // The wheel scrolls the lanes from over the track headers and the ruler too, and Shift+wheel sideways; with Ctrl it zooms.
    for (QWidget* w : std::initializer_list<QWidget*>{m_headers, m_ruler, m_strip, m_scroll->viewport()})
        w->installEventFilter(this);
    // The timeline never asks for more width than the canvases have: its rows of buttons, laid side by side, wanted more than
    // the tiled half of a 2560 px screen, and the main window grew past its tile. In
    // a window that narrow the rows squeeze instead.
    setMinimumWidth(1);
    setDocument(nullptr);
    setShowTime(app_identity::settings().value(kShowTimeKey, false).toBool());
}

QToolButton* TimelinePanel::makeToolButton(const QString& text, const char* name, const QString& tip) {
    auto* b = new QToolButton(this);
    b->setText(text);
    b->setObjectName(QString::fromLatin1(name));
    b->setFocusPolicy(Qt::NoFocus);
    b->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    button_tips::setButtonTip(b, tip);
    return b;
}

// A menu item that is a switch: its text, and the switch at the right, a press anywhere on it turning it; the menu stays open.
// The item is a checkable QAction like any (trigger() turns it), drawn without the platform's tick.
QAction* TimelinePanel::makeSwitch(QMenu* menu, const QString& text, const char* name, const QString& tip) {
    auto* action = new QWidgetAction(menu);
    action->setObjectName(QString::fromLatin1(name));
    action->setText(text);
    action->setCheckable(true);
    auto* row = new QToolButton(menu);
    row->setObjectName(QString::fromLatin1(name) + QStringLiteral("Switch"));
    row->setProperty("role", "menuSwitch");
    row->setText(text);
    row->setCheckable(true);
    button_tips::setButtonTip(row, tip);
    row->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    row->setLayoutDirection(Qt::RightToLeft);  // the switch at the right, as the mock's
    row->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    QIcon icon;
    icon.addPixmap(switchPixmap(false), QIcon::Normal, QIcon::Off);
    icon.addPixmap(switchPixmap(true), QIcon::Normal, QIcon::On);
    row->setIcon(icon);
    row->setIconSize(QSize(kSwitchWidth, kSwitchHeight));
    connect(row, &QToolButton::toggled, action, &QAction::setChecked);
    connect(action, &QAction::toggled, row, [row](bool on) {
        const QSignalBlocker block(row);
        row->setChecked(on);
    });
    m_switches << action;
    action->setDefaultWidget(row);
    menu->addAction(action);
    return action;
}

QAction* TimelinePanel::makeAction(QMenu* menu, const QString& text, const char* name) {
    QAction* a = menu->addAction(text);
    a->setObjectName(QString::fromLatin1(name));
    return a;
}

// A menu button: the menu opens on a press.
static QMenu* menuOf(QToolButton* button) {
    auto* menu = new QMenu(button);
    button->setMenu(menu);
    button->setPopupMode(QToolButton::InstantPopup);
    return menu;
}

// The mock's top bar: Timeline ▾ │ undo redo │ Frame ▾ ▶ Play ▾ ······ 1.0x ▾ │ Export │ + Add │ ⋯
QWidget* TimelinePanel::makeTopBar() {
    auto* bar = new QWidget(this);
    auto* row = new QHBoxLayout(bar);
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(kBarGap);
    QWidget* title = makeTimelineMenu();
    row->addWidget(title);
    row->addSpacing(kBarGroupGap);
    m_secondary << title;
    m_undo = makeToolButton(QString(), "timelineUndo", tr("Undo"));
    m_undo->setIcon(QIcon(QStringLiteral(":/icons/undo.svg")));
    m_undo->setToolButtonStyle(Qt::ToolButtonIconOnly);
    m_redo = makeToolButton(QString(), "timelineRedo", tr("Redo"));
    m_redo->setIcon(QIcon(QStringLiteral(":/icons/redo.svg")));
    m_redo->setToolButtonStyle(Qt::ToolButtonIconOnly);
    connect(m_undo, &QToolButton::clicked, this, [this] {
        if (m_doc)
            report(m_doc->undo());
    });
    connect(m_redo, &QToolButton::clicked, this, [this] {
        if (m_doc)
            report(m_doc->redo());
    });
    // Undo and redo in one pill, as the mock's.
    auto* history = new QWidget(bar);
    history->setObjectName(QStringLiteral("timelineHistory"));
    history->setProperty("role", "timelineGroup");
    history->setAttribute(Qt::WA_StyledBackground);
    auto* pair = new QHBoxLayout(history);
    pair->setContentsMargins(kBarGap, 0, kBarGap, 0);
    pair->setSpacing(0);
    for (QToolButton* b : {m_undo, m_redo}) {
        b->setParent(history);
        b->setProperty("role", "fixedTool");
        pair->addWidget(b);
    }
    row->addWidget(history);
    row->addSpacing(kBarGroupGap);
    m_secondary << history;
    row->addWidget(makeFrameMenu());
    row->addWidget(makePlayButton());
    row->addStretch(1);
    row->addWidget(makeSpeedMenu());
    auto* exportVideo = makeToolButton(tr("Export"), "timelineExport", tr("Export the animation as a video"));
    connect(exportVideo, &QToolButton::clicked, this, &TimelinePanel::exportRequested);
    row->addWidget(exportVideo);
    m_secondary << m_speed << exportVideo;
    QWidget* add = makeAddMenu();
    row->addWidget(add);
    row->addWidget(makeMoreMenu());
    m_secondary << add;
    return bar;
}

QWidget* TimelinePanel::makeTimelineMenu() {
    auto* b = makeToolButton(tr("Timeline"), "timelineMenu", tr("The timeline's menu"));
    b->setProperty("role", "timelineTitle");
    QMenu* menu = menuOf(b);
    connect(makeAction(menu, tr("Hide Timeline"), "timelineHide"), &QAction::triggered, this, &TimelinePanel::hideRequested);
    connect(makeAction(menu, tr("Keep Only This Frame …"), "timelineKeepOnly"), &QAction::triggered, this,
            &TimelinePanel::keepOnlyRequested);
    return b;
}

// The frame commands, Delete apart at the foot so it isn't chosen for + Frame.
QWidget* TimelinePanel::makeFrameMenu() {
    auto* b = makeToolButton(tr("Frame"), "timelineFrameMenu", tr("New, duplicate, hold or delete frames"));
    QMenu* menu = menuOf(b);
    // The key shown beside it is the Canvas menu's New Frame's; the item itself has none, so no key is taken twice.
    QAction* newFrame = makeAction(menu, tr("+ Frame") + QStringLiteral("\tShift+N"), "timelineNewFrame");
    m_duplicate = makeAction(menu, tr("Duplicate"), "timelineDuplicate");
    m_holdFrame = makeAction(menu, tr("Hold …"), "timelineHoldFrame");
    menu->addSeparator();
    m_delete = makeAction(menu, tr("Delete"), "timelineDelete");
    connect(newFrame, &QAction::triggered, this, &TimelinePanel::newFrameRequested);
    connect(m_duplicate, &QAction::triggered, this, &TimelinePanel::duplicateRequested);
    connect(m_holdFrame, &QAction::triggered, this, &TimelinePanel::askHold);
    connect(m_delete, &QAction::triggered, this, &TimelinePanel::deleteRequested);
    return b;
}

// Play, the bar's one prominent button; its arrow opens Loop and Settings.
QWidget* TimelinePanel::makePlayButton() {
    m_play = makeToolButton(tr("Play"), "timelinePlay", tr("Play the animation (Space)"));
    m_play->setProperty("role", "primary");
    m_play->setIcon(glyphIcon(Glyph::Play));
    auto* menu = new QMenu(m_play);
    m_play->setMenu(menu);
    m_play->setPopupMode(QToolButton::MenuButtonPopup);
    m_loop = makeSwitch(menu, tr("Loop"), "timelineLoop", tr("Play the animation in a loop"));
    menu->addSeparator();
    QAction* settings = makeAction(menu, tr("Settings …"), "timelineSettings");
    connect(m_play, &QToolButton::clicked, this, &TimelinePanel::playRequested);
    connect(m_loop, &QAction::toggled, this, &TimelinePanel::loopRequested);
    connect(settings, &QAction::triggered, this, &TimelinePanel::settingsRequested);
    return m_play;
}

// Play's speed (TimelineView::speed): playback only, not an undo step; the frames and Export keep the frame rate. The presets,
// and Custom … for any from TimelineView::kMinSpeed to kMaxSpeed (up to ×10). The chosen one has a mark.
QWidget* TimelinePanel::makeSpeedMenu() {
    m_speed = makeToolButton(speedText(1.0), "timelineSpeed", tr("Play's speed"));
    QMenu* menu = menuOf(m_speed);
    for (double speed : TimelineView::kSpeeds) {
        QAction* a = menu->addAction(speedText(speed));
        a->setObjectName(QStringLiteral("timelineSpeed%1").arg(static_cast<int>(std::lround(speed * 100))));
        a->setData(speed);
        m_speeds << a;
        connect(a, &QAction::triggered, this, [this, speed] { setSpeed(speed); });
    }
    menu->addSeparator();
    m_customSpeed = makeAction(menu, tr("Custom …"), "timelineSpeedCustom");
    connect(m_customSpeed, &QAction::triggered, this, [this] {
        if (!m_doc)
            return;
        if (const std::optional<double> speed = askSpeed(m_doc->animation().view.speed))
            setSpeed(*speed);
    });
    askSpeed = [this](double now) -> std::optional<double> {
        bool ok = false;
        const double speed = fixedDialog::getDouble(this, tr("Play Speed"), tr("Times the frame rate:"), now, TimelineView::kMinSpeed,
                                                    TimelineView::kMaxSpeed, 2, &ok);
        return ok ? std::optional<double>(speed) : std::nullopt;
    };
    return m_speed;
}

void TimelinePanel::setSpeed(double speed) {
    if (!m_doc)
        return;
    TimelineView view = m_doc->animation().view;
    view.speed = std::clamp(speed, TimelineView::kMinSpeed, TimelineView::kMaxSpeed);
    m_doc->setTimelineView(view);
}

QWidget* TimelinePanel::makeAddMenu() {
    auto* b = makeToolButton(tr("+ Add"), "timelineAddMenu", tr("Add a sound or a video"));
    QMenu* menu = menuOf(b);
    // No + Track here (feedback: "Remove the 'Add Track' option from the '+Add' dropdown"): + Add Track is under the rows.
    connect(makeAction(menu, tr("+ Sound …"), "timelineAddSound"), &QAction::triggered, this, &TimelinePanel::addSoundRequested);
    m_addVideo = makeAction(menu, tr("+ Video …"), "timelineAddVideo");
    connect(m_addVideo, &QAction::triggered, this, &TimelinePanel::addVideoRequested);
    return b;
}

// A label and a slider as one item of a menu.
QWidget* TimelinePanel::sliderRow(const QString& label, QSlider* slider) {
    auto* row = new QWidget(this);
    auto* line = new QHBoxLayout(row);
    line->setContentsMargins(Theme::kPanelSectionGap, Theme::kPanelRowGap / 2, Theme::kPanelSectionGap, Theme::kPanelRowGap / 2);
    line->addWidget(new QLabel(label, row), 1);
    slider->setParent(row);
    slider->setFixedWidth(kSliderWidth);
    line->addWidget(slider);
    return row;
}

// ⋯: the rest. The onion skin and its strength (feedback: "I should be able to adjust the strength of the onion skin"),
// frames or time (Show Time) and Fit.
QWidget* TimelinePanel::makeMoreMenu() {
    auto* b = makeToolButton(QStringLiteral("⋯"), "timelineMore", tr("More"));
    QMenu* menu = menuOf(b);
    m_onion = makeSwitch(menu, tr("Onion Skin"), "timelineOnionSkin", tr("Show the neighbouring frames faintly under this one"));
    connect(m_onion, &QAction::toggled, this, [this](bool on) {
        if (!m_doc)
            return;
        TimelineView view = m_doc->animation().view;
        view.onionSkin = on;
        m_doc->setTimelineView(view);
    });
    m_onionStrength = new QSlider(Qt::Horizontal, this);
    m_onionStrength->setObjectName(QStringLiteral("timelineOnionStrength"));
    m_onionStrength->setRange(TimelineView::kMinOnionOpacity, TimelineView::kMaxOnionOpacity);
    connect(m_onionStrength, &QSlider::valueChanged, this, [this](int value) {
        if (!m_doc)
            return;
        TimelineView view = m_doc->animation().view;
        view.onionOpacity = value;
        m_doc->setTimelineView(view);
    });
    auto* strength = new QWidgetAction(menu);
    strength->setDefaultWidget(sliderRow(tr("Strength"), m_onionStrength));
    menu->addAction(strength);
    menu->addSeparator();
    m_showTime =
        makeSwitch(menu, tr("Show Time"), "timelineShowTime", tr("The ruler, the frames and the play range in seconds instead of frames"));
    connect(m_showTime, &QAction::toggled, this, &TimelinePanel::setShowTime);
    menu->addSeparator();
    connect(makeAction(menu, tr("Fit All Frames"), "timelineFit"), &QAction::triggered, this,
            [this] { m_zoom->setValue(m_strip->fitBeatWidth(m_scroll->viewport()->width())); });
    return b;
}

// Over the track headers, beside the ruler: the current track's eye, lock, duplicate and delete, as the mock's.
QWidget* TimelinePanel::makeTrackBar() {
    auto* bar = new QWidget(this);
    bar->setObjectName(QStringLiteral("timelineTrackBar"));
    // The ruler's ground and bottom line, so the rows scrolled up under it end at its edge, as the lanes end at the ruler's.
    bar->setAttribute(Qt::WA_StyledBackground);
    bar->setFixedSize(TrackHeaders::kWidth, TimeRuler::kHeight);
    auto* row = new QHBoxLayout(bar);
    row->setContentsMargins(Theme::kPanelColumnGap, 0, Theme::kPanelColumnGap, 0);
    row->setSpacing(Theme::kPanelColumnGap);
    const auto iconButton = [&](const char* icon, const char* name, const QString& tip) {
        QToolButton* b = makeToolButton(QString(), name, tip);
        b->setParent(bar);
        b->setIcon(QIcon(QStringLiteral(":/icons/%1.svg").arg(QLatin1String(icon))));
        b->setFixedSize(kTrackBarButton, kTrackBarButton);
        b->setToolButtonStyle(Qt::ToolButtonIconOnly);
        b->setProperty("role", "fixedTool");
        row->addWidget(b);
        return b;
    };
    m_trackEye = iconButton("visible", "timelineTrackEye", tr("Hide or show the current track"));
    m_trackLock = iconButton("lock", "timelineTrackLock", tr("Lock or unlock the current track"));
    m_trackLock->setCheckable(true);
    m_trackDuplicate = iconButton("layer-duplicate", "timelineTrackDuplicate", tr("Duplicate the current track"));
    m_trackDelete = iconButton("layer-delete", "timelineTrackDelete", tr("Delete the current track"));
    row->addStretch(1);
    const auto current = [this] { return m_doc && !m_doc->animation().tracks.empty() ? m_doc->animation().currentTrack : -1; };
    connect(m_trackEye, &QToolButton::clicked, this, [this, current] {
        if (const int t = current(); t >= 0)
            emit trackHiddenRequested(t, !m_doc->animation().tracks[static_cast<std::size_t>(t)].hidden);
    });
    connect(m_trackLock, &QToolButton::clicked, this, [this, current] {
        if (const int t = current(); t >= 0)
            emit trackLockedRequested(t, !m_doc->animation().tracks[static_cast<std::size_t>(t)].locked);
    });
    connect(m_trackDuplicate, &QToolButton::clicked, this, [this, current] {
        if (const int t = current(); t >= 0)
            emit duplicateTrackRequested(t);
    });
    connect(m_trackDelete, &QToolButton::clicked, this, [this, current] {
        if (const int t = current(); t >= 0)
            emit deleteTrackRequested(t);
    });
    return bar;
}

// The mock's bottom bar: |◀ ◀ ▶ ▶ ▶| │ Start End Duration ······ − zoom + ⛶
// The bottom bar's two halves: the transport and the play range on the left; the tracks' Height, the zoom and maximise on the
// right. In a timeline too narrow for both side by side, the right half goes under the left (resizeEvent), so nothing is
// squeezed past its text.
QWidget* TimelinePanel::makeBottomBar() {
    auto* bar = new QWidget(this);
    m_bottomRows = new QBoxLayout(QBoxLayout::LeftToRight, bar);
    m_bottomRows->setContentsMargins(0, 0, 0, 0);
    m_bottomRows->setSpacing(0);  // each half ends in its stretch: gaps only between controls, so one row fits at 1280 px
    m_bottomLeft = new QWidget(bar);
    m_bottomLeft->setObjectName(QStringLiteral("timelineBottomLeft"));
    auto* left = new QHBoxLayout(m_bottomLeft);
    left->setContentsMargins(0, 0, 0, 0);
    left->setSpacing(0);
    left->addWidget(makeTransport());
    left->addSpacing(kBarGap);
    QWidget* range = makeRange();
    left->addWidget(range);
    left->addStretch(1);
    m_bottomRight = new QWidget(bar);
    m_bottomRight->setObjectName(QStringLiteral("timelineBottomRight"));
    auto* right = new QHBoxLayout(m_bottomRight);
    right->setContentsMargins(0, 0, 0, 0);
    right->setSpacing(0);
    right->addStretch(1);
    QWidget* height = makeHeight();
    right->addWidget(height);
    right->addSpacing(kBarGap);
    QWidget* zoom = makeZoom();
    right->addWidget(zoom);
    m_secondary << range << height << zoom;
    auto* maximize = makeToolButton(QString(), "timelineMaximize", tr("The timeline as tall as the window allows"));
    maximize->setIcon(glyphIcon(Glyph::Maximize));
    maximize->setToolButtonStyle(Qt::ToolButtonIconOnly);
    maximize->setCheckable(true);
    connect(maximize, &QToolButton::toggled, this, &TimelinePanel::maximizeToggled);
    right->addSpacing(kBarGap);
    right->addWidget(maximize);
    m_bottomRows->addWidget(m_bottomLeft, 1);
    m_bottomRows->addWidget(m_bottomRight, 1);
    return bar;
}

QWidget* TimelinePanel::makeTransport() {
    auto* box = new QWidget(this);
    box->setObjectName(QStringLiteral("timelineTransport"));
    box->setProperty("role", "timelineGroup");
    box->setAttribute(Qt::WA_StyledBackground);
    auto* row = new QHBoxLayout(box);
    row->setContentsMargins(kBarGap, 0, kBarGap, 0);
    row->setSpacing(0);
    const auto step = [&](Glyph glyph, const char* name, const QString& tip) {
        QToolButton* b = makeToolButton(QString(), name, tip);
        b->setParent(box);
        b->setIcon(glyphIcon(glyph));
        b->setToolButtonStyle(Qt::ToolButtonIconOnly);
        b->setProperty("role", "fixedTool");
        row->addWidget(b);
        return b;
    };
    connect(step(Glyph::First, "timelineFirst", tr("First Frame")), &QToolButton::clicked, this, &TimelinePanel::firstRequested);
    connect(step(Glyph::Previous, "timelinePrevious", tr("Previous Frame")), &QToolButton::clicked, this,
            &TimelinePanel::previousRequested);
    m_playBottom = step(Glyph::Play, "timelinePlayBottom", tr("Play the animation (Space)"));
    connect(m_playBottom, &QToolButton::clicked, this, &TimelinePanel::playRequested);
    connect(step(Glyph::Next, "timelineNext", tr("Next Frame")), &QToolButton::clicked, this, &TimelinePanel::nextRequested);
    connect(step(Glyph::Last, "timelineLast", tr("Last Frame")), &QToolButton::clicked, this, &TimelinePanel::lastRequested);
    return box;
}

// The play range, in beats from 0 as the ruler counts them, and how long it plays.
QWidget* TimelinePanel::makeRange() {
    auto* box = new QWidget(this);
    auto* row = new QHBoxLayout(box);
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(kBarGap);
    const auto field = [&](const QString& prefix, const char* name, const QString& tip) {
        auto* spin = new BeatSpinBox(box);
        spin->setObjectName(QString::fromLatin1(name));
        spin->setPrefix(prefix);
        spin->setKeyboardTracking(false);  // one change when typed, not one a key
        spin->setToolTip(tip);
        connect(spin, &QSpinBox::valueChanged, this, &TimelinePanel::setPlayRange);
        row->addWidget(spin);
        return spin;
    };
    m_rangeStart = field(tr("Start: "), "timelineRangeStart", tr("The first frame Play plays and loops from"));
    m_rangeEnd = field(tr("End: "), "timelineRangeEnd", tr("The last frame Play plays"));
    // A click on the Duration switches between frames and time, as ⋯ ▸ Show Time does.
    m_duration = makeToolButton(QString(), "timelineDuration", tr("Click to show frames or time"));
    m_duration->setParent(box);
    m_duration->setProperty("role", "fixedTool");
    connect(m_duration, &QToolButton::clicked, this, [this] { m_showTime->setChecked(!m_showTime->isChecked()); });
    row->addWidget(m_duration);  // its own padding sets it apart
    return box;
}

// The tracks' height, as master has it: "Height" and its slider in the bottom bar (asked for back there).
QWidget* TimelinePanel::makeHeight() {
    auto* box = new QWidget(this);
    box->setObjectName(QStringLiteral("timelineHeightGroup"));
    auto* row = new QHBoxLayout(box);
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(kBarGap);
    row->addWidget(new QLabel(tr("Height"), box));
    m_laneHeight = new QSlider(Qt::Horizontal, box);
    m_laneHeight->setObjectName(QStringLiteral("timelineTrackHeight"));
    m_laneHeight->setRange(FrameStrip::kMinLaneScale, 100);
    m_laneHeight->setValue(m_strip->laneScale());
    m_laneHeight->setFixedWidth(kHeightSliderWidth);
    m_laneHeight->setToolTip(tr("The tracks' height: smaller shows more tracks (Ctrl+Shift+wheel)"));
    connect(m_laneHeight, &QSlider::valueChanged, m_strip, &FrameStrip::setLaneScale);
    row->addWidget(m_laneHeight);
    return box;
}

QWidget* TimelinePanel::makeZoom() {
    auto* box = new QWidget(this);
    auto* row = new QHBoxLayout(box);
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(kBarGap);
    m_zoom = new QSlider(Qt::Horizontal, box);
    m_zoom->setObjectName(QStringLiteral("timelineZoom"));
    m_zoom->setRange(FrameStrip::kMinBeatWidth, FrameStrip::kMaxBeatWidth);
    m_zoom->setValue(m_strip->beatWidth());
    m_zoom->setFixedWidth(kSliderWidth);
    m_zoom->setToolTip(tr("The frames' width (Ctrl+wheel)"));
    connect(m_zoom, &QSlider::valueChanged, m_strip, &FrameStrip::setBeatWidth);
    const auto zoomButton = [&](const char* icon, const char* name, const QString& tip, double by) {
        QToolButton* b = makeToolButton(QString(), name, tip);
        b->setParent(box);
        b->setIcon(QIcon(QStringLiteral(":/icons/%1.svg").arg(QLatin1String(icon))));
        b->setToolButtonStyle(Qt::ToolButtonIconOnly);
        b->setProperty("role", "fixedTool");
        connect(b, &QToolButton::clicked, this, [this, by] {
            const int was = m_zoom->value();
            const int wanted = static_cast<int>(std::lround(was * by));
            m_zoom->setValue(wanted != was ? wanted : was + (by > 1 ? 1 : -1));
        });
        return b;
    };
    row->addWidget(zoomButton("zoom-out", "timelineZoomOut", tr("Narrower frames"), 1.0 / kZoomPerPress));
    row->addWidget(m_zoom);
    row->addWidget(zoomButton("zoom-in", "timelineZoomIn", tr("Wider frames"), kZoomPerPress));
    return box;
}

QWidget* TimelinePanel::makeVideoGroup() {
    auto* box = new QWidget(this);
    box->setObjectName(QStringLiteral("timelineVideoGroup"));
    auto* row = new QHBoxLayout(box);
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(kBarGap);
    row->addWidget(new QLabel(tr("Opacity"), box));
    m_videoOpacity = new QSlider(Qt::Horizontal, box);
    m_videoOpacity->setObjectName(QStringLiteral("timelineVideoOpacity"));
    m_videoOpacity->setRange(0, 100);
    m_videoOpacity->setFixedWidth(kSliderWidth);
    m_videoOpacity->setToolTip(tr("The video's opacity under the frames"));
    connect(m_videoOpacity, &QSlider::sliderPressed, this, [this] { m_opacityKey = nextOpacityKey(); });
    connect(m_videoOpacity, &QSlider::valueChanged, this, [this](int value) {
        if (m_doc && m_doc->animation().video && m_doc->animation().video->opacity != value)
            (void)m_doc->setVideoOpacity(value, m_videoOpacity->isSliderDown() ? m_opacityKey : 0);
    });
    row->addWidget(m_videoOpacity);
    return box;
}

bool TimelinePanel::eventFilter(QObject* watched, QEvent* event) {
    if (event->type() == QEvent::Wheel) {
        auto* wheel = static_cast<QWheelEvent*>(event);
        if (wheel->modifiers() & Qt::ControlModifier)
            return zoomByWheel(watched, wheel);
        return scrollLanes(watched, wheel);
    }
    return QWidget::eventFilter(watched, event);
}

// How far a wheel event turned, in notches (a mouse's click is one; a touchpad sends fractions). Some systems turn a
// Shift+wheel into a sideways one themselves, so a sideways turn counts when there is no upright one.
double TimelinePanel::wheelNotches(const QWheelEvent* event) {
    const QPoint angle = event->angleDelta();
    return (angle.y() != 0 ? angle.y() : angle.x()) / kWheelNotch;
}

// Feedback: "Ctrl+mouse wheel on the timeline should zoom in and out. Ctrl+shift+mouse wheel should increase/decrease
// height of the timeline". Through the sliders, so they show it. Zooming keeps the beat under the pointer where it is.
bool TimelinePanel::zoomByWheel(QObject* over, QWheelEvent* event) {
    const double notches = wheelNotches(event);
    event->accept();
    if (notches == 0.0)
        return true;
    const int sign = notches > 0 ? 1 : -1;
    if (event->modifiers() & Qt::ShiftModifier) {
        const int step = static_cast<int>(std::lround(notches * kHeightPerNotch));
        m_laneHeight->setValue(m_laneHeight->value() + (step != 0 ? step : sign));
        return true;
    }
    const int was = m_strip->beatWidth();
    int wanted = static_cast<int>(std::lround(was * std::pow(kZoomPerNotch, notches)));
    if (wanted == was)
        wanted += sign;
    // The pointer's place along the lanes, in the strip's pixels.
    auto* w = qobject_cast<QWidget*>(over);
    const int pointerX = w ? m_strip->mapFrom(w, event->position().toPoint()).x() : m_strip->xOfBeat(m_doc ? m_doc->beat() : 0);
    const double beat = double(pointerX - FrameStrip::kMargin) / was;
    m_zoom->setValue(wanted);
    const int movedBy = static_cast<int>(std::lround(FrameStrip::kMargin + beat * m_strip->beatWidth())) - pointerX;
    m_scroll->horizontalScrollBar()->setValue(m_scroll->horizontalScrollBar()->value() + movedBy);
    return true;
}

void TimelinePanel::toggleCollapsed(int track) {
    if (!m_doc || track < 0 || track >= m_doc->animation().trackCount())
        return;
    TimelineView view = m_doc->animation().view;
    const int id = m_doc->animation().tracks[static_cast<std::size_t>(track)].id;
    if (!view.collapsed.erase(id))
        view.collapsed.insert(id);
    m_doc->setTimelineView(view);
}

// Over the headers the wheel scrolls the tracks up and down, as over the lanes; with Shift, anywhere, it scrolls along the
// frames (where Qt's own Shift+wheel would page up and down). Over the lanes without Shift the scroll area does it itself.
bool TimelinePanel::scrollLanes(QObject* over, QWheelEvent* event) {
    const bool sideways = event->modifiers() & Qt::ShiftModifier;
    if (!sideways && (over == m_scroll->viewport() || over == m_strip))
        return false;
    // Some systems turn a Shift+wheel into a sideways one themselves.
    const QPoint pixels = event->pixelDelta(), angle = event->angleDelta();
    const int along = pixels.isNull() ? (angle.y() != 0 ? angle.y() : angle.x()) : (pixels.y() != 0 ? pixels.y() : pixels.x());
    QScrollBar* bar = sideways ? m_scroll->horizontalScrollBar() : m_scroll->verticalScrollBar();
    const int step = pixels.isNull() ? along * QApplication::wheelScrollLines() * bar->singleStep() / 120 : along;
    bar->setValue(bar->value() - step);
    event->accept();
    return true;
}

QPushButton* TimelinePanel::makeButton(const QString& text, const char* name, const QString& tip) {
    auto* b = new QPushButton(text, this);
    b->setObjectName(QString::fromLatin1(name));
    b->setFocusPolicy(Qt::NoFocus);
    button_tips::setButtonTip(b, tip);
    return b;
}

// In a timeline too narrow for the track column beside the lanes, the column folds away and the lanes keep the room, and
// the bars keep only Frame, Play (its icon), ⋯ and the transport: the Timeline and + Add menus, undo and redo, the speed,
// Export, the play range and the zoom go (the canvas's title bar, the Edit, Canvas and File menus and Ctrl+wheel still
// have them), so what stays can be read.
void TimelinePanel::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    const bool compact = width() < kCompactWidth;
    m_side->setVisible(!compact);
    m_headers->setVisible(!compact);
    m_trackBar->setVisible(!compact);
    for (QWidget* w : m_secondary)
        w->setVisible(!compact);
    m_play->setToolButtonStyle(compact ? Qt::ToolButtonIconOnly : Qt::ToolButtonTextBesideIcon);
    const int oneRow = m_bottomLeft->sizeHint().width() + m_bottomRight->sizeHint().width();
    m_bottomRows->setDirection(width() < oneRow ? QBoxLayout::TopToBottom : QBoxLayout::LeftToRight);
    m_strip->relayout();
}

QAbstractButton* TimelinePanel::button(const QString& objectName) const {
    return findChild<QAbstractButton*>(objectName);
}

QAction* TimelinePanel::action(const QString& objectName) const {
    return findChild<QAction*>(objectName);
}

void TimelinePanel::setDocument(Document* doc) {
    if (m_doc) {
        m_doc->disconnect(this);
        m_doc->undoStack()->disconnect(this);
    }
    m_doc = doc;
    m_strip->setDocument(doc);
    m_strip->setSelectionScope(this);
    m_headers->setDocument(doc);
    m_ruler->setDocument(doc);
    if (doc) {
        connect(doc, &Document::animationChanged, this, [this] {
            syncButtons();
            syncVideoBar();
        });
        connect(doc, &Document::currentFrameChanged, this, [this] {
            syncButtons();
            // The playhead's beat stays in sight (a long-held frame's cell may be wider than the lanes).
            m_scroll->ensureVisible(m_strip->xOfBeat(m_doc->beat()) + m_strip->beatWidth() / 2, m_strip->cellRect(0).center().y(),
                                    m_strip->beatWidth() * 2, 0);
        });
        connect(doc->undoStack(), &QUndoStack::canUndoChanged, m_undo, &QWidget::setEnabled);
        connect(doc->undoStack(), &QUndoStack::canRedoChanged, m_redo, &QWidget::setEnabled);
    }
    setEnabled(doc != nullptr);
    m_undo->setEnabled(doc && doc->undoStack()->canUndo());
    m_redo->setEnabled(doc && doc->undoStack()->canRedo());
    syncButtons();
    syncVideoBar();
}

std::vector<int> TimelinePanel::selectedFrames() const {
    return m_strip->selectedFrames();
}

void TimelinePanel::setPlaying(bool playing) {
    m_play->setText(playing ? tr("Stop") : tr("Play"));
    m_play->setIcon(glyphIcon(playing ? Glyph::Stop : Glyph::Play));
    m_playBottom->setIcon(glyphIcon(playing ? Glyph::Stop : Glyph::Play));
}

void TimelinePanel::setVideoStatus(const QString& text) {
    m_videoStatus->setText(text);
    m_strip->update();
}

void TimelinePanel::syncButtons() {
    const Animation* a = m_doc ? &m_doc->animation() : nullptr;
    const std::vector<int> selected = selectedFrames();
    m_delete->setEnabled(a && static_cast<int>(selected.size()) < a->frameCount());
    m_duplicate->setEnabled(a != nullptr);
    m_holdFrame->setEnabled(a != nullptr);
    const QSignalBlocker blockOnion(m_onion), blockLoop(m_loop), blockStrength(m_onionStrength);
    m_onion->setChecked(a && a->view.onionSkin);
    m_loop->setChecked(a && a->loop);
    m_onionStrength->setValue(a ? a->view.onionOpacity : TimelineView{}.onionOpacity);
    m_onionStrength->setEnabled(a && a->view.onionSkin);
    syncSwitches();
    const double speed = a ? a->view.speed : 1.0;
    m_speed->setText(speedText(speed));
    bool preset = false;
    for (QAction* s : m_speeds) {
        s->setIcon(markIcon(s->data().toDouble() == speed));
        preset = preset || s->data().toDouble() == speed;
    }
    m_customSpeed->setIcon(markIcon(!preset));
    m_customSpeed->setText(preset ? tr("Custom …") : tr("Custom (%1) …").arg(speedText(speed)));
    syncTrackBar();
    syncRange();
}

// The track headers' bar acts on the current track: its eye shows whether it is hidden, its lock whether it is locked.
void TimelinePanel::syncTrackBar() {
    const Animation* a = m_doc ? &m_doc->animation() : nullptr;
    const AnimationTrack* t = a && !a->tracks.empty() ? a->track() : nullptr;
    m_trackEye->setIcon(QIcon(t && t->hidden ? QStringLiteral(":/icons/hidden.svg") : QStringLiteral(":/icons/visible.svg")));
    m_trackEye->setEnabled(t != nullptr);
    const QSignalBlocker block(m_trackLock);
    m_trackLock->setChecked(t && t->locked);
    m_trackLock->setEnabled(t != nullptr);
    m_trackDuplicate->setEnabled(t && t->isDrawn());
    m_trackDelete->setEnabled(t && !t->locked && a->trackCount() > 1);
}

// The play range's fields, in beats from 0 as the ruler counts them, and its length.
void TimelinePanel::syncRange() {
    const Animation* a = m_doc ? &m_doc->animation() : nullptr;
    const int last = a ? std::max(0, a->totalBeats() - 1) : 0;
    const QSignalBlocker blockStart(m_rangeStart), blockEnd(m_rangeEnd);
    m_rangeStart->setRange(0, last);
    m_rangeEnd->setRange(0, last);
    m_rangeStart->setValue(a ? a->playFirst() : 0);
    m_rangeEnd->setValue(a ? a->playLast() : 0);
    const double fps = a ? a->fps : Animation{}.fps;
    for (QSpinBox* spin : {m_rangeStart, m_rangeEnd})
        static_cast<BeatSpinBox*>(spin)->setUnits(fps, m_showTime->isChecked());
    const int beats = a ? a->playLast() - a->playFirst() + 1 : 0;
    m_duration->setText(tr("Duration: %1").arg(TimeRuler::beatLabel(beats, fps, m_showTime->isChecked())));
}

// The End field at the animation's last beat follows the animation as it grows (Animation::playEnd -1).
void TimelinePanel::setPlayRange() {
    if (!m_doc)
        return;
    const int last = std::max(0, m_doc->animation().totalBeats() - 1);
    const int start = m_rangeStart->value();
    const int end = std::max(start, m_rangeEnd->value());
    report(m_doc->setPlayRange(start, end >= last ? -1 : end));
    syncRange();
}

// Frames or time, for the ruler, the frames' labels and the play range; kept in the settings.
void TimelinePanel::setShowTime(bool asTime) {
    {
        const QSignalBlocker block(m_showTime);
        m_showTime->setChecked(asTime);
    }
    syncSwitches();
    m_ruler->setShowTime(asTime);
    m_strip->setShowTime(asTime);
    app_identity::settings().setValue(kShowTimeKey, asTime);
    syncRange();
}

// The switches' rows show their items, set above with their signals blocked.
void TimelinePanel::syncSwitches() {
    for (QAction* action : m_switches) {
        auto* row = static_cast<QToolButton*>(static_cast<QWidgetAction*>(action)->defaultWidget());
        const QSignalBlocker block(row);
        row->setChecked(action->isChecked());
    }
}

bool TimelinePanel::showsTime() const {
    return m_showTime->isChecked();
}

void TimelinePanel::syncVideoBar() {
    const bool video = m_doc && m_doc->animation().video;
    m_videoBar->setVisible(video);
    m_addVideo->setVisible(!video);
    m_videoGroup->setVisible(video);
    if (!video)
        return;
    const VideoReference& v = *m_doc->animation().video;
    m_videoText->setText(tr("<b>%1</b> Video · %2% · %3 fps").arg(QFileInfo(v.path).fileName().toHtmlEscaped()).arg(v.opacity).arg(v.fps));
    const QSignalBlocker block(m_videoOpacity);
    m_videoOpacity->setValue(v.opacity);
}

void TimelinePanel::showFrameMenu(int, const QPoint& globalPos) {
    QMenu menu(this);
    menu.setObjectName(QStringLiteral("frameMenu"));
    QAction* duplicate = menu.addAction(tr("Duplicate Frame"));
    QAction* hold = menu.addAction(tr("Hold …"));
    QAction* clear = menu.addAction(tr("Clear Frame"));
    menu.addSeparator();
    QAction* remove = menu.addAction(tr("Delete Frame"));
    remove->setEnabled(m_delete->isEnabled());
    QAction* chosen = menu.exec(globalPos);
    if (chosen == duplicate)
        emit duplicateRequested();
    else if (chosen == hold)
        askHold();
    else if (chosen == clear)
        emit clearFramesRequested();
    else if (chosen == remove)
        emit deleteRequested();
}

}

namespace doodlebold {

void TimelinePanel::askHold() {
    if (!m_doc)
        return;
    bool ok = false;
    const int now = m_doc->animation().holdOf(selectedFrames().front());
    const int hold =
        fixedDialog::getInt(this, tr("Hold Frame"), tr("Beats to hold the selected frames for:"), now, 1, AnimationFrame::kMaxHold, 1, &ok);
    if (ok)
        emit holdSetRequested(hold);
}

void TimelinePanel::showSoundMenu(int id, const QPoint& globalPos) {
    if (!m_doc)
        return;
    QMenu menu(this);
    menu.setObjectName(QStringLiteral("soundMenu"));
    QAction* start = menu.addAction(tr("Start At …"));
    QAction* end = menu.addAction(tr("End At …"));
    const auto clipIt =
        std::find_if(m_doc->animation().sounds.begin(), m_doc->animation().sounds.end(), [id](const SoundClip& c) { return c.id == id; });
    const bool muted = clipIt != m_doc->animation().sounds.end() && clipIt->muted;
    QAction* mute = menu.addAction(muted ? tr("Unmute") : tr("Mute"));
    menu.addSeparator();
    QAction* remove = menu.addAction(tr("Remove Sound"));
    QAction* chosen = menu.exec(globalPos);
    if (chosen == remove) {
        emit removeSoundRequested(id);
        return;
    }
    if (chosen == mute) {
        report(m_doc->setSoundMuted(id, !muted));
        return;
    }
    if (chosen != start && chosen != end)
        return;
    const Animation& a = m_doc->animation();
    const auto clip = std::ranges::find_if(a.sounds, [id](const SoundClip& c) { return c.id == id; });
    if (clip == a.sounds.end())
        return;
    bool ok = false;
    if (chosen == start) {
        const double seconds = fixedDialog::getDouble(this, tr("Sound Start"), tr("Start the sound at (seconds):"),
                                                      clip->startSeconds(a.fps), 0.0, 24.0 * 3600.0, 2, &ok);
        if (ok)
            emit soundStartRequested(id, seconds);
        return;
    }
    const double latest = clip->startSeconds(a.fps) + clip->seconds - clip->trimStart;
    const double seconds =
        fixedDialog::getDouble(this, tr("Sound End"), tr("End the sound at (seconds):"), clip->endSeconds(a.fps), 0.0, latest, 2, &ok);
    if (ok)
        emit soundEndRequested(id, seconds);
}

}

namespace doodlebold {

template <typename T> void TimelinePanel::report(const T& result) {
    if (!result && result.error() != EditError::NoChange)
        emit refused(describe(result.error()));
}

void TimelinePanel::askTrackName(int track) {
    if (!m_doc || track < 0 || track >= m_doc->animation().trackCount())
        return;
    bool ok = false;
    const QString name = fixedDialog::getText(this, tr("Rename Track"), tr("Track name:"), QLineEdit::Normal,
                                              m_doc->animation().tracks[static_cast<std::size_t>(track)].name, &ok);
    if (ok)
        report(m_doc->renameTrack(track, name));
}

void TimelinePanel::showTrackMenu(int track, const QPoint& globalPos) {
    if (!m_doc || track < 0 || track >= m_doc->animation().trackCount())
        return;
    const Animation& a = m_doc->animation();
    const AnimationTrack& t = a.tracks[static_cast<std::size_t>(track)];
    QMenu menu(this);
    menu.setObjectName(QStringLiteral("trackMenu"));
    QAction* rename = menu.addAction(tr("Rename …"));
    QAction* up = menu.addAction(tr("Move Up"));
    QAction* down = menu.addAction(tr("Move Down"));
    up->setEnabled(track + 1 < a.trackCount());
    down->setEnabled(track > 0);
    menu.addSeparator();
    // A sound track hidden is silent: its row's speaker says Mute.
    const bool sound = t.kind == TrackKind::Sound;
    QAction* hide =
        menu.addAction(sound ? (t.hidden ? tr("Unmute Track") : tr("Mute Track")) : (t.hidden ? tr("Show Track") : tr("Hide Track")));
    QAction* lock = menu.addAction(t.locked ? tr("Unlock Track") : tr("Lock Track"));
    QAction* fold = menu.addAction(a.view.collapsed.contains(t.id) ? tr("Expand Track") : tr("Collapse Track"));
    // The track's label colour, to sort tracks by (feedback: "similar to how after effects does it").
    QMenu* colours = menu.addMenu(tr("Label Colour"));
    colours->setObjectName(QStringLiteral("trackLabelMenu"));
    std::map<QAction*, int> labels;
    for (int label = 0; label <= AnimationTrack::kLabels; ++label) {
        QAction* c = colours->addAction(labelIcon(label), track_labels::name(label));
        c->setObjectName(QStringLiteral("trackLabel%1").arg(label));
        if (label == t.label)
            c->setText(c->text() + QStringLiteral("  ✓"));
        labels[c] = label;
    }
    menu.addSeparator();
    QAction* clear = t.kind == TrackKind::Video ? nullptr : menu.addAction(tr("Clear Track"));
    QAction* remove = menu.addAction(tr("Delete Track"));
    const auto framesTracks = std::count_if(a.tracks.begin(), a.tracks.end(), [](const AnimationTrack& o) { return o.isDrawn(); });
    remove->setEnabled(!(t.isDrawn() && framesTracks < 2) && !t.locked);
    if (clear)
        clear->setEnabled(!t.locked);
    // Kept before the menu runs: playing on under it replaces the document's state, and `t` with it.
    const bool wasHidden = t.hidden, wasLocked = t.locked;
    QAction* chosen = menu.exec(globalPos);
    if (!chosen)
        return;
    if (chosen == rename)
        askTrackName(track);
    else if (chosen == up)
        report(m_doc->moveTrack(track, track + 1));
    else if (chosen == down)
        report(m_doc->moveTrack(track, track - 1));
    else if (chosen == hide)
        emit trackHiddenRequested(track, !wasHidden);
    else if (chosen == lock)
        emit trackLockedRequested(track, !wasLocked);
    else if (chosen == fold)
        toggleCollapsed(track);
    else if (const auto it = labels.find(chosen); it != labels.end())
        report(m_doc->setTrackLabel(track, it->second));
    else if (chosen == clear)
        emit clearTrackRequested(track);
    else if (chosen == remove)
        emit deleteTrackRequested(track);
}

}
