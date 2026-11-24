#include "ui/timelapse/TimelapseDialogs.h"

#include "ui/Components.h"
#include "ui/Theme.h"
#include "ui/timelapse/RecordingPill.h"

#include <QButtonGroup>
#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QCheckBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPixmap>
#include <QPushButton>
#include <QSlider>
#include <QSpinBox>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <array>
#include <cmath>
#include <utility>

namespace doodlebold {

namespace {

constexpr QSize kPreviewBox{ExportTimelapseDialog::kPreviewWidth, ExportTimelapseDialog::kPreviewHeight};
constexpr int kShortId = 1;

QString sizeText(const QSize& s) {
    return QStringLiteral("%1 × %2").arg(s.width()).arg(s.height());
}

// Secondary text inside rich text, where the style sheet's roles don't reach: the token's colour written into the span.
QString mutedSpanStyle() {
    return QStringLiteral("color: %1;").arg(Theme::color(Theme::Token::TextMuted).name());
}

QLabel* mutedLabel(const QString& text, QWidget* parent, const QString& name = {}) {
    auto* label = components::mutedLabel(text, parent);
    if (!name.isEmpty())
        label->setObjectName(name);
    return label;
}

// A settings row's label: secondary text (it was a third grey, between Text and TextMuted, before the design system).
QLabel* softLabel(const QString& text, QWidget* parent) {
    auto* label = new QLabel(text, parent);
    label->setProperty("role", "hint");
    return label;
}

// A chosen length card's ground: the accent mixed this far into Base.
constexpr double kChosenTint = 0.2;

QColor mixed(const QColor& a, const QColor& b, double t) {
    return QColor(int(a.red() + (b.red() - a.red()) * t), int(a.green() + (b.green() - a.green()) * t),
                  int(a.blue() + (b.blue() - a.blue()) * t));
}

class LengthCard : public QPushButton {
public:
    LengthCard(const QString& title, const QString& text, const QString& name, QWidget* parent) : QPushButton(parent) {
        setObjectName(name);
        setCheckable(true);
        setCursor(Qt::PointingHandCursor);
        QSizePolicy policy(QSizePolicy::Expanding, QSizePolicy::Minimum);
        policy.setHeightForWidth(true);
        setSizePolicy(policy);
        auto* box = new QVBoxLayout(this);
        box->setContentsMargins(Theme::kSpace4, Theme::kSpace4, Theme::kSpace4, Theme::kSpace4);
        box->setSpacing(Theme::kSpace1);
        auto* head = new QLabel(title, this);
        head->setFont(Theme::font(Theme::TextRole::Title));
        m_text = new QLabel(text, this);
        m_text->setWordWrap(true);
        m_text->setProperty("role", "hint");
        for (QLabel* l : {head, m_text}) {
            l->setAttribute(Qt::WA_TransparentForMouseEvents);
            l->setAlignment(Qt::AlignLeft | Qt::AlignTop);
            box->addWidget(l);
        }
    }
    QLabel* textLabel() const { return m_text; }
    QSize minimumSizeHint() const override { return sizeHint(); }
    QSize sizeHint() const override { return {200, layout()->totalHeightForWidth(std::max(200, width()))}; }
    int heightForWidth(int w) const override { return layout()->totalHeightForWidth(w); }

protected:
    void paintEvent(QPaintEvent*) override {
        using Token = Theme::Token;
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const QColor accent = Theme::color(Token::Accent);
        QColor fill = Theme::color(isChecked() ? Token::Base : (underMouse() ? Token::Hover : Token::Base));
        if (isChecked())
            fill = mixed(fill, accent, kChosenTint);
        p.setPen(Qt::NoPen);
        p.setBrush(fill);
        p.drawRoundedRect(QRectF(rect()), Theme::kRadiusMd, Theme::kRadiusMd);
        if (isChecked() || hasFocus()) {
            p.setBrush(Qt::NoBrush);
            p.setPen(QPen(isChecked() ? accent : Theme::color(Token::FocusRing), Theme::kBorderStrong));
            p.drawRoundedRect(QRectF(rect()).adjusted(1, 1, -1, -1), Theme::kRadiusMd - 1, Theme::kRadiusMd - 1);
        }
    }

private:
    QLabel* m_text;
};

class DisclosureButton : public QAbstractButton {
public:
    DisclosureButton(const QString& text, const QString& name, QWidget* parent) : QAbstractButton(parent) {
        setObjectName(name);
        setText(text);
        setCheckable(true);
        setCursor(Qt::PointingHandCursor);
        setFocusPolicy(Qt::StrongFocus);
    }
    QSize sizeHint() const override {
        const QFontMetrics fm(font());
        return {kChevron + Theme::kSpace2 + fm.horizontalAdvance(text()) + Theme::kSpace1,
                std::max(fm.height() + Theme::kSpace2, Theme::comfortable::kControlSm)};
    }
    QSize minimumSizeHint() const override { return sizeHint(); }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const QColor color = Theme::color(underMouse() || hasFocus() ? Theme::Token::Text : Theme::Token::TextMuted);
        p.setPen(QPen(color, Theme::kGlyphStroke, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.save();
        p.translate(kChevron / 2.0, height() / 2.0);
        if (isChecked())
            p.rotate(90);
        p.drawPolyline(QPolygonF{QPointF(-2, -4), QPointF(2, 0), QPointF(-2, 4)});
        p.restore();
        p.drawText(QRect(kChevron + Theme::kSpace2, 0, width() - kChevron - Theme::kSpace2, height()), Qt::AlignVCenter | Qt::AlignLeft,
                   text());
    }
    void enterEvent(QEnterEvent*) override { update(); }
    void leaveEvent(QEvent*) override { update(); }

private:
    static constexpr int kChevron = 10;  // px: the chevron's box (component metric)
};

class SettingsPanel : public QWidget {
public:
    using QWidget::QWidget;

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(Qt::NoPen);
        p.setBrush(Theme::color(Theme::Token::AlternateBase));
        p.drawRoundedRect(QRectF(rect()), Theme::kRadiusMd, Theme::kRadiusMd);
    }
};

}

QString timelapseClock(double seconds) {
    const int whole = std::max(0, int(seconds));
    return QStringLiteral("%1:%2").arg(whole / 60).arg(whole % 60, 2, 10, QLatin1Char('0'));
}

ExportTimelapseDialog::ExportTimelapseDialog(std::shared_ptr<const Timelapse> recording, const QString& canvasName, QString encoderNote,
                                             QWidget* parent)
    : QDialog(parent), m_recording(std::move(recording)), m_replay(*m_recording), m_encoderNote(std::move(encoderNote)) {
    setObjectName(QStringLiteral("exportTimelapseDialog"));
    setWindowTitle(tr("Export Timelapse"));
    setMinimumWidth(1120);  // px: the preview and the settings side by side (component metric)
    auto* root = new QVBoxLayout(this);
    Theme::padDialog(root);

    auto* titleRow = new QHBoxLayout;
    titleRow->setSpacing(Theme::kSpace3);
    auto* title = new QLabel(tr("Export Timelapse"), this);
    title->setObjectName(QStringLiteral("timelapseTitle"));
    title->setFont(Theme::font(Theme::TextRole::Title));
    titleRow->addWidget(title);
    const int strokes = m_recording->editCount();
    titleRow->addWidget(
        mutedLabel(tr("%1 · %2 · %3 recorded")
                       .arg(canvasName.isEmpty() ? tr("This canvas") : canvasName,
                            strokes == 1 ? tr("1 stroke") : tr("%1 strokes").arg(RecordingPill::groupedNumber(strokes)),
                            m_recording->frameCount() == 1 ? tr("1 frame")
                                                           : tr("%1 frames").arg(RecordingPill::groupedNumber(m_recording->frameCount()))),
                   this, QStringLiteral("timelapseSubtitle")),
        1);
    root->addLayout(titleRow);

    auto* columns = new QHBoxLayout;
    columns->setSpacing(Theme::kSpace5);
    root->addLayout(columns, 1);

    auto* left = new QVBoxLayout;
    left->setSpacing(Theme::kSpace2);
    m_preview = new QLabel(this);
    m_preview->setObjectName(QStringLiteral("timelapsePreview"));
    m_preview->setFixedSize(kPreviewBox);
    m_preview->setAlignment(Qt::AlignCenter);
    left->addWidget(m_preview);
    auto* playRow = new QHBoxLayout;
    playRow->setSpacing(Theme::comfortable::kControlGap);
    m_play = new QPushButton(tr("Play"), this);
    m_play->setObjectName(QStringLiteral("timelapsePlay"));
    m_play->setMinimumSize(76, Theme::comfortable::kControlLg);  // 76 px: "Pause" fits (component metric)
    playRow->addWidget(m_play);
    m_scrubber = new QSlider(Qt::Horizontal, this);
    m_scrubber->setObjectName(QStringLiteral("timelapseScrubber"));
    playRow->addWidget(m_scrubber, 1);
    left->addLayout(playRow);
    m_playTimer = new QTimer(this);
    connect(m_playTimer, &QTimer::timeout, this, &ExportTimelapseDialog::playTick);
    connect(m_play, &QPushButton::clicked, this, [this] { setPlaying(!isPlaying()); });
    auto* where = new QHBoxLayout;
    m_videoTime = mutedLabel({}, this, QStringLiteral("timelapseVideoTime"));
    m_videoTime->setWordWrap(false);
    m_videoLength = mutedLabel({}, this, QStringLiteral("timelapseVideoLength"));
    m_videoLength->setWordWrap(false);
    m_videoLength->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    where->addWidget(m_videoTime);
    where->addStretch(1);
    where->addWidget(m_videoLength);
    left->addLayout(where);
    left->addStretch(1);
    columns->addLayout(left);

    auto* right = new QVBoxLayout;
    right->setSpacing(Theme::comfortable::kSectionGap);
    columns->addLayout(right, 1);
    right->addWidget(softLabel(tr("Length"), this));
    auto* cards = new QWidget(this);
    cards->setObjectName(QStringLiteral("timelapseLength"));
    auto* cardsLayout = new QVBoxLayout(cards);
    cardsLayout->setContentsMargins(0, 0, 0, 0);
    cardsLayout->setSpacing(Theme::kCardGap);
    m_length = new QButtonGroup(this);
    auto* full = new LengthCard(tr("Full length"), QString(), QStringLiteral("timelapseFullLength"), cards);
    auto* shortCard = new LengthCard(tr("%1 seconds").arg(kTimelapseShortSeconds),
                                     tr("The whole painting sped up to fit %1 s, for posting.").arg(kTimelapseShortSeconds),
                                     QStringLiteral("timelapse30Seconds"), cards);
    m_fullNote = full->textLabel();
    full->setChecked(true);
    m_length->addButton(full, 0);
    m_length->addButton(shortCard, kShortId);
    cardsLayout->addWidget(full);
    cardsLayout->addWidget(shortCard);
    right->addWidget(cards);

    m_more = new DisclosureButton(tr("More settings"), QStringLiteral("timelapseMoreSettings"), this);
    right->addWidget(m_more, 0, Qt::AlignLeft);
    m_settings = new SettingsPanel(this);
    m_settings->setObjectName(QStringLiteral("timelapseSettingsPanel"));
    auto* form = new QFormLayout(m_settings);
    form->setContentsMargins(Theme::kCardPadding, Theme::kSpace3, Theme::kCardPadding, Theme::kSpace3);
    Theme::spaceFields(form);
    form->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);

    auto* holdRow = new QWidget(m_settings);
    auto* hl = new QHBoxLayout(holdRow);
    hl->setContentsMargins(0, 0, 0, 0);
    m_hold = new QSpinBox(holdRow);
    m_hold->setObjectName(QStringLiteral("timelapseHoldSeconds"));
    m_hold->setRange(0, TimelapseExportOptions::kMaxHoldSeconds);
    m_hold->setValue(3);
    m_hold->setToolTip(tr("0 holds nothing: the video ends with the last step."));
    hl->addWidget(new QLabel(tr("Hold the finished picture for"), holdRow));
    hl->addWidget(m_hold);
    hl->addWidget(new QLabel(tr("s"), holdRow));
    hl->addStretch(1);
    form->addRow(softLabel(tr("End"), m_settings), holdRow);

    m_leaveOutUndone = new QCheckBox(tr("Leave them out"), m_settings);
    m_leaveOutUndone->setObjectName(QStringLiteral("timelapseLeaveOutUndone"));
    m_leaveOutUndone->setChecked(true);
    form->addRow(softLabel(tr("Undone strokes"), m_settings), m_leaveOutUndone);

    auto* soundRow = new QWidget(m_settings);
    auto* sh = new QHBoxLayout(soundRow);
    sh->setContentsMargins(0, 0, 0, 0);
    sh->setSpacing(Theme::kSpace1);
    m_soundNone = new QLabel(tr("None ·"), soundRow);
    m_soundNone->setObjectName(QStringLiteral("timelapseSoundNone"));
    m_soundNone->setProperty("role", "hint");
    m_sound = new QPushButton(tr("Add a sound file …"), soundRow);
    m_sound->setObjectName(QStringLiteral("timelapseSound"));
    m_sound->setFlat(true);
    m_sound->setCursor(Qt::PointingHandCursor);
    m_sound->setProperty("role", "link");
    m_soundChip = new QPushButton(soundRow);
    m_soundChip->setObjectName(QStringLiteral("timelapseSoundChip"));
    m_soundChip->setToolTip(tr("Remove the sound"));
    m_soundChip->hide();
    sh->addWidget(m_soundNone);
    sh->addWidget(m_sound);
    sh->addWidget(m_soundChip);
    sh->addStretch(1);
    form->addRow(softLabel(tr("Sound"), m_settings), soundRow);

    m_sizeLabel = new QLabel(m_settings);
    m_sizeLabel->setObjectName(QStringLiteral("timelapseSize"));
    form->addRow(softLabel(tr("Size"), m_settings), m_sizeLabel);
    m_rows = new ExportQualityRows(form, m_settings, QStringLiteral("timelapse"));
    right->addWidget(m_settings);
    // The right column is as wide as the open settings from the start, so More settings only opens downward: the dialog no longer
    // widened and the Length cards no longer reflowed as it opened (feedback: "awkward transition due to the sudden side
    // change").
    cards->setMinimumWidth(m_settings->sizeHint().width());
    right->addStretch(1);
    m_settings->hide();

    auto* bottom = new QHBoxLayout;
    bottom->setSpacing(Theme::comfortable::kControlGap);
    m_summary = mutedLabel({}, this, QStringLiteral("timelapseSummary"));
    m_summary->setToolTip(m_encoderNote);
    bottom->addWidget(m_summary, 1);
    // Plain buttons, not a QDialogButtonBox: its order follows the platform, and Export goes rightmost.
    auto* cancel = new QPushButton(tr("Cancel"), this);
    auto* exportButton = new QPushButton(tr("Export …"), this);
    exportButton->setDefault(true);
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    connect(exportButton, &QPushButton::clicked, this, &QDialog::accept);
    bottom->addWidget(cancel);
    bottom->addWidget(exportButton);
    root->addLayout(bottom);

    connect(m_length, &QButtonGroup::idClicked, this, &ExportTimelapseDialog::update);
    connect(m_leaveOutUndone, &QCheckBox::toggled, this, &ExportTimelapseDialog::update);
    connect(m_hold, &QSpinBox::valueChanged, this, &ExportTimelapseDialog::update);
    connect(m_rows, &ExportQualityRows::changed, this, &ExportTimelapseDialog::update);
    connect(m_more, &QAbstractButton::toggled, this, &ExportTimelapseDialog::showMoreSettings);
    connect(m_scrubber, &QSlider::valueChanged, this, &ExportTimelapseDialog::showPicture);
    connect(m_scrubber, &QSlider::sliderPressed, this, [this] { setPlaying(false); });
    connect(m_sound, &QPushButton::clicked, this, [this] {
        const QString file = askSoundFile();
        if (!file.isEmpty())
            setSoundFile(file);
    });
    connect(m_soundChip, &QPushButton::clicked, this, [this] { setSoundFile({}); });
    syncSoundChip();
    update();
    m_scrubber->setValue(m_scrubber->maximum());
    showPicture();
}

QString ExportTimelapseDialog::askSoundFile() {
    return QFileDialog::getOpenFileName(
        this, tr("Add Sound"), QString(),
        tr("Sound and video files (*.mp3 *.wav *.ogg *.opus *.flac *.m4a *.aac *.mp4 *.mov *.mkv *.webm);;All files (*)"));
}

void ExportTimelapseDialog::setSoundFile(const QString& file) {
    m_soundFile = file;
    syncSoundChip();
    update();
}

void ExportTimelapseDialog::syncSoundChip() {
    const bool has = !m_soundFile.isEmpty();
    m_soundChip->setVisible(has);
    m_soundNone->setVisible(!has);
    m_sound->setVisible(!has);
    m_soundChip->setText(tr("%1  ×").arg(QFileInfo(m_soundFile).fileName()));
}

void ExportTimelapseDialog::showMoreSettings(bool shown) {
    m_settings->setVisible(shown);
    layout()->activate();
    if (isVisible())
        resize(width(), std::max(sizeHint().height(), minimumSizeHint().height()));
}

TimelapseExportChoice ExportTimelapseDialog::choice() const {
    TimelapseExportChoice c;
    c.options.length = m_length->checkedId() == kShortId ? TimelapseLength::Seconds30 : TimelapseLength::Full;
    c.options.holdSeconds = m_hold->value();
    c.options.leaveOutUndone = m_leaveOutUndone->isChecked();
    c.crf = m_rows->crf();
    c.maxBitrateKbps = m_rows->maxBitrateKbps();
    c.maxFileMb = m_rows->maxFileMb();
    c.soundFile = m_soundFile;
    return c;
}

TimelapsePlan ExportTimelapseDialog::plan() const {
    return planTimelapse(*m_recording, choice().options);
}

int ExportTimelapseDialog::scrubberPosition() const {
    return m_scrubber->value();
}

void ExportTimelapseDialog::setScrubberPosition(int position) {
    m_scrubber->setValue(position);
}

QSize ExportTimelapseDialog::previewedSize() const {
    return m_plan.frames.empty()
               ? QSize()
               : m_replay.sizeAt(m_plan.frames[std::size_t(std::clamp(m_scrubber->value(), 0, int(m_plan.frames.size()) - 1))]);
}

void ExportTimelapseDialog::update() {
    setPlaying(false);
    const TimelapseExportChoice c = choice();
    m_plan = planTimelapse(*m_recording, c.options);
    const int old = m_scrubber->value();
    const bool atEnd = old >= m_scrubber->maximum();
    m_scrubber->blockSignals(true);
    m_scrubber->setRange(0, std::max(0, int(m_plan.frames.size()) - 1));
    m_scrubber->setValue(atEnd ? m_scrubber->maximum() : std::min(old, m_scrubber->maximum()));
    m_scrubber->blockSignals(false);

    TimelapseExportOptions full = c.options;
    full.length = TimelapseLength::Full;
    full.holdSeconds = 0;
    const double fullSeconds = planTimelapse(*m_recording, full).seconds();
    m_fullNote->setText(
        tr("Every recorded frame: each stroke drawn in as you drew it. %1 for this canvas.").arg(timelapseClock(fullSeconds)));
    m_sizeLabel->setText(tr("%1 <span style=\"%2\">(fits 1080p)</span>").arg(sizeText(m_plan.frameSize), mutedSpanStyle()));
    m_videoLength->setText(timelapseClock(m_plan.seconds()));

    const double audio = c.soundFile.isEmpty() ? 0 : c.audioKbps;
    // Rough bitrates, scaled by the area; a timelapse of a still picture is far
    // under them, so this is the most it can be.
    const double area = double(m_plan.frameSize.width()) * m_plan.frameSize.height() / (1920.0 * 1080.0);
    double kbps = std::array{12000.0, 6000.0, 2500.0}[std::size_t(m_rows->qualityId())] * area;
    if (c.maxBitrateKbps > 0)
        kbps = std::min(kbps, double(c.maxBitrateKbps));
    if (c.maxFileMb > 0.0)
        kbps = std::min(kbps, double(bitrateForFileSize(c.maxFileMb, m_plan.seconds(), int(audio))));
    const double mb = (kbps + audio) * m_plan.seconds() / 8.0 / 1000.0;
    QString text = tr("MP4 · about %1 MB").arg(mb < 10.0 ? QString::number(mb, 'f', 1) : QString::number(std::lround(mb)));
    if (c.maxFileMb > 0.0 && bitrateForFileSize(c.maxFileMb, m_plan.seconds(), 0) < 100)
        text += tr(" · the file size limit is too small for this length");
    m_summary->setText(text);
    showPicture();
}

void ExportTimelapseDialog::showTime(int frame) {
    m_videoTime->setText(timelapseClock(double(frame) / std::max(1, m_plan.fps)));
}

void ExportTimelapseDialog::showPicture() {
    if (m_plan.frames.empty())
        return;
    const int frame = std::clamp(m_scrubber->value(), 0, int(m_plan.frames.size()) - 1);
    const QImage& picture = m_replay.seek(m_plan.frames[std::size_t(frame)]);
    m_preview->setPixmap(QPixmap::fromImage(
        picture.scaled(kPreviewBox, Qt::KeepAspectRatio, isPlaying() ? Qt::FastTransformation : Qt::SmoothTransformation)));
    showTime(frame);
}

bool ExportTimelapseDialog::isPlaying() const {
    return m_playTimer->isActive();
}

void ExportTimelapseDialog::setPlaying(bool playing) {
    if (playing == isPlaying())
        return;
    if (!playing) {
        m_playTimer->stop();
        m_play->setText(tr("Play"));
        showPicture();
        return;
    }
    if (m_plan.frames.empty())
        return;
    m_playFrom = m_scrubber->value() < m_scrubber->maximum() ? m_scrubber->value() : 0;
    m_playShown = -1;
    m_playShownStep = -1;
    m_playClock.start();
    m_play->setText(tr("Pause"));
    m_playTimer->start(1000 / std::max(1, m_plan.fps));
    playTick();
}

// Frames are skipped rather than slowing the video down when drawing one takes longer than a frame.
void ExportTimelapseDialog::playTick() {
    const int frame = m_playFrom + int(m_playClock.elapsed() * std::max(1, m_plan.fps) / 1000);
    const int last = int(m_plan.frames.size()) - 1;
    if (frame > last) {
        m_scrubber->blockSignals(true);
        m_scrubber->setValue(last);
        m_scrubber->blockSignals(false);
        setPlaying(false);
        return;
    }
    if (frame == m_playShown)
        return;
    m_playShown = frame;
    const int step = m_plan.frames[std::size_t(frame)];
    if (m_playShownStep != step) {
        m_playShownStep = step;
        m_preview->setPixmap(QPixmap::fromImage(m_replay.seek(step).scaled(kPreviewBox, Qt::KeepAspectRatio, Qt::FastTransformation)));
    }
    m_scrubber->blockSignals(true);
    m_scrubber->setValue(frame);
    m_scrubber->blockSignals(false);
    showTime(frame);
}

QWidget* segmentedRow(QButtonGroup* group, const QStringList& labels, int checked, const QString& name, QWidget* parent) {
    auto* row = new QWidget(parent);
    row->setObjectName(name);
    auto* layout = new QHBoxLayout(row);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(Theme::kSpace1);
    for (int i = 0; i < labels.size(); ++i) {
        auto* b = new QPushButton(labels.at(i), row);
        b->setCheckable(true);
        b->setChecked(i == checked);
        group->addButton(b, i);
        layout->addWidget(b);
    }
    layout->addStretch(1);
    return row;
}

ExportQualityRows::ExportQualityRows(QFormLayout* form, QWidget* parent, const QString& prefix) : QObject(parent) {
    m_quality = new QButtonGroup(this);
    form->addRow(
        QCoreApplication::translate("TimelapseDialogs", "Quality"),
        segmentedRow(m_quality,
                     {QCoreApplication::translate("TimelapseDialogs", "High"), QCoreApplication::translate("TimelapseDialogs", "Medium"),
                      QCoreApplication::translate("TimelapseDialogs", "Small file")},
                     0, prefix + QStringLiteral("Quality"), parent));
    const auto limitRow = [&](QCheckBox*& box, QSpinBox*& spin, const QString& boxName, const QString& spinName, int min, int max,
                              int value, const QString& suffix, const QString& note) {
        auto* row = new QWidget(parent);
        auto* h = new QHBoxLayout(row);
        h->setContentsMargins(0, 0, 0, 0);
        box = new QCheckBox(row);
        box->setObjectName(boxName);
        spin = new QSpinBox(row);
        spin->setObjectName(spinName);
        spin->setRange(min, max);
        spin->setValue(value);
        spin->setSuffix(suffix);
        spin->setEnabled(false);
        connect(box, &QCheckBox::toggled, spin, &QWidget::setEnabled);
        h->addWidget(box);
        h->addWidget(spin);
        auto* hint = new QLabel(note, row);
        hint->setEnabled(false);
        h->addWidget(hint);
        h->addStretch(1);
        return row;
    };
    form->addRow(QCoreApplication::translate("TimelapseDialogs", "Max bitrate"),
                 limitRow(m_capRate, m_maxRate, prefix + QStringLiteral("CapRate"), prefix + QStringLiteral("MaxRate"), 1, 100, 8,
                          QCoreApplication::translate("TimelapseDialogs", " Mbps"),
                          QCoreApplication::translate("TimelapseDialogs", "caps the peaks, not only the average")));
    form->addRow(QCoreApplication::translate("TimelapseDialogs", "Max file size"),
                 limitRow(m_capSize, m_maxSize, prefix + QStringLiteral("CapSize"), prefix + QStringLiteral("MaxSize"), 1, 4000, 512,
                          QCoreApplication::translate("TimelapseDialogs", " MB"),
                          QCoreApplication::translate("TimelapseDialogs", "the bitrate is lowered to fit")));
    connect(m_quality, &QButtonGroup::idClicked, this, &ExportQualityRows::changed);
    connect(m_capRate, &QCheckBox::toggled, this, &ExportQualityRows::changed);
    connect(m_capSize, &QCheckBox::toggled, this, &ExportQualityRows::changed);
    connect(m_maxRate, &QSpinBox::valueChanged, this, &ExportQualityRows::changed);
    connect(m_maxSize, &QSpinBox::valueChanged, this, &ExportQualityRows::changed);
}

int ExportQualityRows::qualityId() const {
    return std::clamp(m_quality->checkedId(), 0, 2);
}

int ExportQualityRows::crf() const {
    return std::array{18, 23, 28}[std::size_t(qualityId())];
}

int ExportQualityRows::maxBitrateKbps() const {
    return m_capRate->isChecked() ? m_maxRate->value() * 1000 : 0;
}

double ExportQualityRows::maxFileMb() const {
    return m_capSize->isChecked() ? double(m_maxSize->value()) : 0.0;
}

QString FfmpegMissingDialog::packageHint() {
#ifdef Q_OS_LINUX
    return QStringLiteral("sudo dnf install ffmpeg-free   (or ffmpeg from RPM Fusion; sudo apt install ffmpeg on Debian and Ubuntu)");
#elif defined(Q_OS_MACOS)
    return QStringLiteral("brew install ffmpeg");
#elif defined(Q_OS_WIN)
    return QStringLiteral("winget install ffmpeg");
#else
    return {};
#endif
}

FfmpegMissingDialog::FfmpegMissingDialog(const QString& problem, QWidget* parent) : QDialog(parent) {
    setWindowTitle(tr("Exporting needs FFmpeg"));
    auto* layout = new QVBoxLayout(this);
    Theme::padDialog(layout);
    auto* text = new QLabel(tr("<b>Exporting needs FFmpeg</b><br>Doodlebold writes the timelapse with FFmpeg, a free program that "
                               "runs beside it.<br>%1")
                                .arg(problem.toHtmlEscaped()),
                            this);
    text->setWordWrap(true);
    layout->addWidget(text);
    if (const QString hint = packageHint(); !hint.isEmpty()) {
        auto* install = new QLabel(tr("Install it with your system's packages, then press Try Again:"), this);
        install->setWordWrap(true);
        layout->addWidget(install);
        auto* command = new QLabel(hint, this);
        command->setTextInteractionFlags(Qt::TextSelectableByMouse);
        command->setObjectName(QStringLiteral("ffmpegPackageHint"));
        layout->addWidget(command);
    }
    auto* buttons = new QDialogButtonBox(this);
    QPushButton* locate = buttons->addButton(tr("Locate ffmpeg …"), QDialogButtonBox::ActionRole);
    QPushButton* retry = buttons->addButton(tr("Try Again"), QDialogButtonBox::AcceptRole);
    buttons->addButton(QDialogButtonBox::Cancel);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(retry, &QPushButton::clicked, this, [this] {
        m_answer = Answer::TryAgain;
        accept();
    });
    connect(locate, &QPushButton::clicked, this, [this] {
        const QString path = QFileDialog::getOpenFileName(this, tr("Locate ffmpeg"));
        if (path.isEmpty())
            return;
        m_located = path;
        m_answer = Answer::Located;
        accept();
    });
    layout->addWidget(buttons);
}

}
