#include "ui/animation/AnimationDialogs.h"

#include "ui/Theme.h"

#include <QButtonGroup>
#include <QCoreApplication>
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QRadioButton>
#include <QSlider>
#include <QSpinBox>
#include <QVBoxLayout>

#include <cmath>

namespace doodlebold {

namespace {

QLabel* hintLabel(const QString& text, QWidget* parent) {
    auto* label = new QLabel(text, parent);
    label->setProperty("role", "hint");
    label->setWordWrap(true);
    return label;
}

QPushButton* choiceButton(const QString& text, const QString& name, QWidget* parent) {
    auto* b = new QPushButton(text, parent);
    b->setObjectName(name);
    b->setCheckable(true);
    b->setAutoDefault(false);
    b->setFocusPolicy(Qt::TabFocus);
    return b;
}

QString rateText(double fps) {
    return std::abs(fps - std::round(fps)) < 1e-6 ? QString::number(std::lround(fps)) : QString::number(fps, 'f', 3);
}

QString frameNumber(int frame) {
    return QStringLiteral("%1").arg(frame + 1, 2, 10, QLatin1Char('0'));
}

}

AddVideoDialog::AddVideoDialog(const QString& fileName, const VideoInfo& info, const Animation& animation, const QSize& canvas,
                               QWidget* parent)
    : QDialog(parent), m_info(info), m_frames(animation.frameCount()), m_fps(animation.fps), m_currentFrame(animation.currentFrame()) {
    setObjectName(QStringLiteral("addVideoDialog"));
    setWindowTitle(tr("Add Video"));
    auto* layout = new QVBoxLayout(this);
    layout->setSpacing(Theme::comfortable::kSectionGap);

    auto* title = new QLabel(tr("Add Video"), this);
    title->setFont(Theme::font(Theme::TextRole::Title));
    const QString sound = info.hasSound ? tr("with sound") : tr("no sound");
    auto* about = new QLabel(tr("<b>%1</b> · %2 × %3 · %4 s · %5 fps · %6. It plays under your drawings and is never painted on.")
                                 .arg(fileName.toHtmlEscaped())
                                 .arg(info.size.width())
                                 .arg(info.size.height())
                                 .arg(info.seconds, 0, 'f', 1)
                                 .arg(rateText(info.fps), sound),
                             this);
    about->setWordWrap(true);
    layout->addWidget(title);
    layout->addWidget(about);

    auto* form = new QGridLayout;
    Theme::spaceFields(form);
    int row = 0;
    const auto addRow = [&](const QString& label, QLayout* content) {
        auto* l = new QLabel(label, this);
        form->addWidget(l, row, 0, Qt::AlignTop | Qt::AlignLeft);
        form->addLayout(content, row, 1);
        ++row;
    };

    m_follow = new QRadioButton(tr("Drawings follow the video"), this);
    m_follow->setObjectName(QStringLiteral("videoFollow"));
    m_keep = new QRadioButton(tr("Keep my timing"), this);
    m_keep->setObjectName(QStringLiteral("videoKeepTiming"));
    auto* timing = new QVBoxLayout;
    timing->setSpacing(Theme::kSpaceHair);
    timing->addWidget(m_follow);
    timing->addWidget(hintLabel(tr("One frame per video frame at the rate below. The timeline grows to the video's length; your %n "
                                   "frames keep their places at the start.",
                                   nullptr, m_frames),
                                this));
    timing->addSpacing(Theme::kSpace2);
    timing->addWidget(m_keep);
    timing->addWidget(
        hintLabel(tr("Stays at %1 fps and %n frames. The video plays under them at its own speed, as a reference.", nullptr, m_frames)
                      .arg(rateText(m_fps)),
                  this));
    addRow(tr("Timing"), timing);

    m_rates = new QButtonGroup(this);
    auto* rates = new QHBoxLayout;
    rates->setSpacing(Theme::kSpace1);  // the rates are one segmented choice, closer than separate controls
    int id = 0;
    for (double fps : kRates) {
        auto* b = choiceButton(rateText(fps), QStringLiteral("videoRate%1").arg(rateText(fps)), this);
        b->setProperty("fps", fps);
        m_rates->addButton(b, id++);
        rates->addWidget(b);
    }
    auto* source = choiceButton(tr("Source %1").arg(rateText(info.fps)), QStringLiteral("videoRateSource"), this);
    source->setProperty("fps", info.fps);
    m_rates->addButton(source, id++);
    rates->addWidget(source);
    m_custom = choiceButton(tr("Custom"), QStringLiteral("videoRateCustom"), this);
    m_rates->addButton(m_custom, id++);
    rates->addWidget(m_custom);
    m_customRate = new QDoubleSpinBox(this);
    m_customRate->setObjectName(QStringLiteral("videoCustomRate"));
    m_customRate->setRange(Animation::kMinFps, Animation::kMaxFps);
    m_customRate->setDecimals(3);
    m_customRate->setSuffix(tr(" fps"));
    m_customRate->setValue(std::clamp(info.fps, Animation::kMinFps, Animation::kMaxFps));
    rates->addWidget(m_customRate);
    rates->addStretch(1);
    addRow(tr("Frame rate"), rates);

    m_fromFirst = choiceButton(tr("Frame 1"), QStringLiteral("videoStartFirst"), this);
    m_fromCurrent = choiceButton(tr("This frame (%1)").arg(frameNumber(m_currentFrame)), QStringLiteral("videoStartCurrent"), this);
    auto* starts = new QButtonGroup(this);
    starts->addButton(m_fromFirst);
    starts->addButton(m_fromCurrent);
    m_fromFirst->setChecked(true);
    auto* startRow = new QHBoxLayout;
    startRow->addWidget(m_fromFirst);
    startRow->addWidget(m_fromCurrent);
    startRow->addStretch(1);
    addRow(tr("Starts at"), startRow);

    const QSize fit = fittedSize(info.size, canvas);
    auto* sizeRow = new QHBoxLayout;
    sizeRow->addWidget(
        new QLabel(fit == info.size ? tr("The video's own size; the canvas keeps its size.")
                                    : tr("Fitted into the canvas (%1 × %2). The canvas keeps its size.").arg(fit.width()).arg(fit.height()),
                   this));
    addRow(tr("Size"), sizeRow);

    m_opacity = new QSlider(Qt::Horizontal, this);
    m_opacity->setObjectName(QStringLiteral("videoOpacity"));
    m_opacity->setRange(0, 100);
    m_opacity->setValue(40);
    m_opacity->setFixedWidth(220);  // px (component metric)
    m_opacityText = new QLabel(this);
    auto* opacityRow = new QHBoxLayout;
    opacityRow->addWidget(m_opacity);
    opacityRow->addWidget(m_opacityText);
    opacityRow->addStretch(1);
    addRow(tr("Opacity"), opacityRow);

    m_sound = new QCheckBox(tr("Keep the video's sound as a sound lane"), this);
    m_sound->setObjectName(QStringLiteral("videoKeepSound"));
    m_sound->setChecked(info.hasSound);
    m_sound->setEnabled(info.hasSound);
    auto* soundRow = new QHBoxLayout;
    soundRow->addWidget(m_sound);
    addRow(tr("Sound"), soundRow);

    m_jpeg = choiceButton(tr("JPEG"), QStringLiteral("videoCacheJpeg"), this);
    m_lossless = choiceButton(tr("Lossless"), QStringLiteral("videoCacheLossless"), this);
    auto* cache = new QButtonGroup(this);
    cache->addButton(m_jpeg);
    cache->addButton(m_lossless);
    m_jpeg->setChecked(true);
    auto* cacheRow = new QHBoxLayout;
    cacheRow->addWidget(m_jpeg);
    cacheRow->addWidget(m_lossless);
    cacheRow->addStretch(1);
    addRow(tr("Frame cache"), cacheRow);
    layout->addLayout(form);

    m_summary = new QLabel(this);
    m_summary->setObjectName(QStringLiteral("videoSummary"));
    m_summary->setWordWrap(true);
    layout->addWidget(m_summary);

    auto* buttons = new QDialogButtonBox(this);
    QPushButton* add = buttons->addButton(tr("Add Video"), QDialogButtonBox::AcceptRole);
    add->setObjectName(QStringLiteral("addVideoButton"));
    add->setDefault(true);
    buttons->addButton(QDialogButtonBox::Cancel);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);

    m_follow->setChecked(true);
    setRate(m_fps);
    for (QAbstractButton* b : {static_cast<QAbstractButton*>(m_follow), static_cast<QAbstractButton*>(m_keep)})
        connect(b, &QAbstractButton::toggled, this, &AddVideoDialog::update);
    connect(m_rates, &QButtonGroup::idClicked, this, &AddVideoDialog::update);
    connect(m_customRate, &QDoubleSpinBox::valueChanged, this, &AddVideoDialog::update);
    connect(m_opacity, &QSlider::valueChanged, this, &AddVideoDialog::update);
    update();
    // Not to be stretched (feedback: "it should not be able to be stretched"): its width fixed, its height its contents'
    // at that width (fitHeight, again whenever the summary's text changes).
    setFixedWidth(kAddVideoWidth);
    fitHeight();
}

void AddVideoDialog::fitHeight() {
    ensurePolished();  // the style sheet's control heights first, or the height is measured short
    setFixedHeight(layout()->totalHeightForWidth(kAddVideoWidth));
}

void AddVideoDialog::setFollow(bool follow) {
    (follow ? m_follow : m_keep)->setChecked(true);
    update();
}

void AddVideoDialog::setRate(double fps) {
    for (QAbstractButton* b : m_rates->buttons())
        if (b != m_custom && std::abs(b->property("fps").toDouble() - fps) < 1e-6) {
            b->setChecked(true);
            update();
            return;
        }
    m_custom->setChecked(true);
    m_customRate->setValue(fps);
    update();
}

AddVideoChoice AddVideoDialog::choice() const {
    AddVideoChoice c;
    c.follow = m_follow->isChecked();
    if (!c.follow)
        c.fps = m_fps;
    else if (m_custom->isChecked())
        c.fps = m_customRate->value();
    else if (QAbstractButton* b = m_rates->checkedButton())
        c.fps = b->property("fps").toDouble();
    c.atCurrentFrame = m_fromCurrent->isChecked();
    c.opacity = m_opacity->value();
    c.keepSound = m_sound->isEnabled() && m_sound->isChecked();
    c.lossless = m_lossless->isChecked();
    return c;
}

int AddVideoDialog::pictureCount() const {
    VideoReference v;
    v.seconds = m_info.seconds;
    v.fps = choice().fps;
    return v.pictureCount();
}

QString AddVideoDialog::summary() const {
    return m_summary->text();
}

void AddVideoDialog::update() {
    const bool follow = m_follow->isChecked();
    for (QAbstractButton* b : m_rates->buttons())
        b->setEnabled(follow);
    m_customRate->setEnabled(follow && m_custom->isChecked());
    m_opacityText->setText(QStringLiteral("%1%").arg(m_opacity->value()));
    m_summary->setText(tr("<b>%n frames</b> at %1 fps. The frames are cached on disk and only those near the one you draw on are kept "
                          "in memory. The video file is not changed.",
                          nullptr, pictureCount())
                           .arg(rateText(choice().fps)));
    if (layout())
        fitHeight();
}

TimelineSettingsDialog::TimelineSettingsDialog(const Animation& animation, QWidget* parent) : QDialog(parent), m_view(animation.view) {
    setObjectName(QStringLiteral("timelineSettingsDialog"));
    setWindowTitle(tr("Timeline Settings"));
    auto* form = new QFormLayout;
    m_fps = new QDoubleSpinBox(this);
    m_fps->setObjectName(QStringLiteral("timelineFps"));
    m_fps->setRange(Animation::kMinFps, Animation::kMaxFps);
    m_fps->setDecimals(3);
    m_fps->setSuffix(tr(" fps"));
    m_fps->setValue(animation.fps);
    form->addRow(tr("Frame rate"), m_fps);
    m_before = new QSpinBox(this);
    m_before->setObjectName(QStringLiteral("onionBefore"));
    m_before->setRange(0, 5);
    m_before->setValue(animation.view.onionBefore);
    form->addRow(tr("Onion skin: frames before"), m_before);
    m_after = new QSpinBox(this);
    m_after->setObjectName(QStringLiteral("onionAfter"));
    m_after->setRange(0, 5);
    m_after->setValue(animation.view.onionAfter);
    form->addRow(tr("Onion skin: frames after"), m_after);
    m_opacity = new QSlider(Qt::Horizontal, this);
    m_opacity->setObjectName(QStringLiteral("onionOpacity"));
    m_opacity->setRange(TimelineView::kMinOnionOpacity, TimelineView::kMaxOnionOpacity);
    m_opacity->setValue(animation.view.onionOpacity);
    form->addRow(tr("Onion skin opacity"), m_opacity);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    auto* layout = new QVBoxLayout(this);
    layout->addLayout(form);
    layout->addWidget(buttons);
    layout->setSizeConstraint(QLayout::SetFixedSize);  // as large as its contents, not to be stretched (as Add Video)
}

double TimelineSettingsDialog::fps() const {
    return m_fps->value();
}

TimelineView TimelineSettingsDialog::view() const {
    TimelineView v = m_view;
    v.onionBefore = m_before->value();
    v.onionAfter = m_after->value();
    v.onionOpacity = m_opacity->value();
    return v;
}

QString keepOnlyQuestion(int frameCount, int currentFrame) {
    return QCoreApplication::translate("AnimationDialogs",
                                       "Delete the other %n frames and keep frame %1 as an ordinary picture?\n\n"
                                       "You can undo this.",
                                       nullptr, frameCount - 1)
        .arg(frameNumber(currentFrame));
}

QString keepOnlyButtonText(int currentFrame) {
    return QCoreApplication::translate("AnimationDialogs", "Keep Frame %1").arg(frameNumber(currentFrame));
}

}
