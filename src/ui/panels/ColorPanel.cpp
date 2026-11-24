#include "ui/panels/ColorPanel.h"

#include "core/color/ColorAssets.h"
#include "core/color/ColorState.h"
#include "core/tools/ToolBox.h"
#include "ui/ButtonTips.h"
#include "ui/Components.h"
#include "ui/Theme.h"
#include "ui/color/ColorSliderViews.h"
#include "ui/color/ColorWheelView.h"
#include "ui/color/SwatchesView.h"

#include <QAction>
#include <QColorDialog>
#include <QCoreApplication>
#include <QFrame>
#include <QHBoxLayout>
#include <QPainter>
#include <QPainterPath>
#include <QScrollArea>
#include <QScrollBar>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>

namespace doodlebold {

namespace {

struct ViewInfo {
    const char* id;
    const char* icon;
    const char* tip;
};

constexpr std::array<ViewInfo, kColorViewCount> kViews = {{
    {"colorCircle", "circle", QT_TRANSLATE_NOOP("ColorPanel", "Show/Hide Color Circle")},
    {"rgbSlider", "rgb", QT_TRANSLATE_NOOP("ColorPanel", "Show/Hide RGB Sliders")},
    {"hsvSlider", "hsv", QT_TRANSLATE_NOOP("ColorPanel", "Show/Hide HSV/HSL Sliders")},
    {"swatches", "swatches", QT_TRANSLATE_NOOP("ColorPanel", "Show/Hide Swatches")},
}};

constexpr int kSwatchSize = 26;     // px: the brush colour's disc (component metric, user colour)
constexpr int kSecondarySize = 20;  // px: the second colour's disc (component metric, user colour)
constexpr int kToolSize = Theme::compact::kControlMd;  // the tool buttons beside the circle
constexpr int kCircleClearance = Theme::kSpace4;

QPixmap checkerDisc(int size, qreal ratio) {
    QPixmap pix(QSize(size, size) * ratio);
    pix.setDevicePixelRatio(ratio);
    pix.fill(Qt::transparent);
    QPainter p(&pix);
    p.setRenderHint(QPainter::Antialiasing);
    QPainterPath disc;
    disc.addEllipse(QRectF(0, 0, size, size));
    p.setClipPath(disc);
    components::paintChecker(p, QRect(0, 0, size, size), size / 4, Theme::kCheckerLight, Theme::kCheckerDark);
    return pix;
}

}

QString ColorPanel::viewId(ColorView v) {
    return QLatin1String(kViews[std::size_t(v)].id);
}

ColorPanel::ColorPanel(ToolBox* tools, ColorAssets* assets, QWidget* parent)
    : QWidget(parent), m_tools(tools), m_state(new ColorState(this)) {
    setObjectName(QStringLiteral("colorPanel"));
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);  // extra height goes to the tool panel below
    const int gap = Theme::metric(Theme::Metric::Gap);
    const int pad = Theme::metric(Theme::Metric::Padding);

    auto* toggles = new QHBoxLayout;
    toggles->setContentsMargins(0, 0, 0, 0);
    toggles->setSpacing(gap);
    for (int i = 0; i < kColorViewCount; ++i) {
        auto* b = new QToolButton(this);
        b->setObjectName(QStringLiteral("colorViewToggle_%1").arg(QLatin1String(kViews[std::size_t(i)].id)));
        b->setCheckable(true);
        b->setChecked(shownAtStart(ColorView(i)));
        b->setIcon(QIcon(QStringLiteral(":/icons/view-%1.svg").arg(QLatin1String(kViews[std::size_t(i)].icon))));
        components::toolButton(b);
        b->setProperty("role", "fixedTool");
        button_tips::setCommandTip(b, QCoreApplication::translate("ColorPanel", kViews[std::size_t(i)].tip),
                                   QStringLiteral("window.show.") + QLatin1String(kViews[std::size_t(i)].id));
        toggles->addWidget(b);
        m_toggles[std::size_t(i)] = b;
    }
    toggles->addStretch(1);

    auto* wheel = new ColorWheelView(m_state, this);
    m_secondary = new QToolButton(wheel);  // first, so the brush colour lies over it
    m_secondary->setObjectName(QStringLiteral("secondarySwatch"));
    button_tips::setCommandTip(m_secondary, tr("Second Color: press to exchange the two colors"), QStringLiteral("color.exchange"));
    m_secondary->setFixedSize(kSecondarySize, kSecondarySize);
    connect(m_secondary, &QToolButton::clicked, this, [this] {
        if (m_tools)
            m_tools->exchangeColors();
    });
    m_swatch = new QToolButton(wheel);
    m_swatch->setObjectName(QStringLiteral("colorSwatch"));
    button_tips::setButtonTip(m_swatch, tr("Brush Color: press to choose another"));
    m_swatch->setFixedSize(kSwatchSize, kSwatchSize);
    wheel->installEventFilter(this);
    connect(m_swatch, &QToolButton::clicked, this, [this] {
        const QColor picked = askColor(m_tools ? m_tools->color() : m_state->color());
        if (picked.isValid() && m_tools)
            m_tools->setColor(picked);
    });
    QWidget* content[kColorViewCount] = {
        wheel,
        new RgbSlidersView(m_state, this),
        new HsvSlidersView(m_state, assets, this),
        new SwatchesView(m_state, assets, this),
    };
    m_scroll = new QScrollArea(this);
    m_scroll->setObjectName(QStringLiteral("colorViewsScroll"));
    m_scroll->setWidgetResizable(true);
    m_scroll->setFrameShape(QFrame::NoFrame);
    // Never a scroll bar (feedback, 2026-11-22: \"The color wheel panel should never show a scrollbar"): the card is as
    // tall as its views (fitHeight), in both directions.
    m_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_column = new QWidget(m_scroll);
    auto* views = new QVBoxLayout(m_column);
    views->setContentsMargins(0, 0, 0, 0);
    views->setSpacing(gap);
    m_scroll->setWidget(m_column);
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(pad, pad, pad, pad);
    layout->setSpacing(gap);
    layout->addLayout(toggles);
    layout->addWidget(m_scroll, 1);
    for (int i = 0; i < kColorViewCount; ++i) {
        auto* frame = new QWidget(m_column);
        frame->setObjectName(QStringLiteral("colorView_%1").arg(QLatin1String(kViews[std::size_t(i)].id)));
        auto* column = new QVBoxLayout(frame);
        column->setContentsMargins(0, 0, 0, 0);
        column->setSpacing(gap);
        auto* line = new QFrame(frame);
        line->setFrameShape(QFrame::HLine);
        line->setFrameShadow(QFrame::Plain);
        column->addWidget(line);
        if (i == int(ColorView::Circle))
            column->addSpacing(std::max(0, kCircleClearance - gap));
        column->addWidget(content[i], 0, i == int(ColorView::Circle) ? Qt::AlignHCenter : Qt::Alignment());
        frame->setVisible(shownAtStart(ColorView(i)));
        views->addWidget(frame);
        m_views[std::size_t(i)] = frame;
        connect(m_toggles[std::size_t(i)], &QToolButton::toggled, frame, [this, frame, i](bool on) {
            if (!m_toggles[std::size_t(i)]->defaultAction())
                frame->setVisible(on);
            QMetaObject::invokeMethod(this, &ColorPanel::fitHeight, Qt::QueuedConnection);
        });
    }
    m_wheel = wheel;
    views->addStretch(0);

    QWidget* circle = m_views[std::size_t(ColorView::Circle)];
    const auto tool = [&](const char* name, const char* icon) {
        auto* b = new QToolButton(circle);
        b->setObjectName(QLatin1String(name));
        b->setIcon(QIcon(QStringLiteral(":/icons/%1.svg").arg(QLatin1String(icon))));
        components::toolButton(b);
        b->setProperty("role", "fixedTool");
        return b;
    };
    m_exchange = tool("exchangeButton", "color-swap");
    button_tips::setCommandTip(m_exchange, tr("Exchange the brush color and the second color"), QStringLiteral("color.exchange"));
    connect(m_exchange, &QToolButton::clicked, this, [this] {
        if (m_tools)
            m_tools->exchangeColors();
    });
    m_transparent = tool("transparentButton", "color-transparent");
    m_transparent->setCheckable(true);
    button_tips::setCommandTip(m_transparent, tr("Transparent Color: paint by taking away what is under the stroke"),
                               QStringLiteral("color.transparent"));
    connect(m_transparent, &QToolButton::clicked, this, [this] {
        if (m_tools)
            m_tools->setTransparent(!m_tools->transparent());
    });
    circle->installEventFilter(this);
    placeColorTools();

    QMetaObject::invokeMethod(this, &ColorPanel::fitHeight, Qt::QueuedConnection);

    if (tools) {
        showColor(tools->color());
        showSecondaryColor(tools->secondaryColor());
        connect(tools, &ToolBox::colorChanged, this, &ColorPanel::showColor);
        connect(tools, &ToolBox::secondaryColorChanged, this, &ColorPanel::showSecondaryColor);
        showTransparent(tools->transparent());
        connect(tools, &ToolBox::transparentChanged, this, &ColorPanel::showTransparent);
    }

    m_state->setColor(tools->color());
    connect(tools, &ToolBox::colorChanged, m_state, [this](const QColor& c) {
        if (c != m_state->color())
            m_state->setColor(c);
    });
    connect(m_state, &ColorState::changed, this, [this] {
        if (m_tools && m_tools->color() != m_state->color())
            m_tools->setColor(m_state->color());
    });
}

// At the width the views have without a scroll bar: the panel is as tall as they are, so none shows.
int ColorPanel::preferredHeight() const {
    const int pad = Theme::metric(Theme::Metric::Padding);
    const int width = std::max(1, m_scroll->width() - 2 * m_scroll->frameWidth());
    const int column = m_column->layout()->hasHeightForWidth() ? m_column->layout()->heightForWidth(width) : m_column->sizeHint().height();
    return 2 * pad + m_toggles[0]->sizeHint().height() + Theme::metric(Theme::Metric::Gap) + column + 4;
}

// Room for the circle and its buttons, and for the widest view shown, so nothing is cut off.
int ColorPanel::preferredWidth() const {
    const int pad = Theme::metric(Theme::Metric::Padding);
    const int circle = ColorWheelView::kSize + 2 * (kToolSize + Theme::metric(Theme::Metric::Gap));
    const int views = m_column->minimumSizeHint().width() + 2 * m_scroll->frameWidth();
    return std::max(circle, views) + 2 * pad;
}

void ColorPanel::fitHeight() {
    const int preferred = preferredHeight();
    if (m_fittedHeight > 0 && preferred != m_fittedHeight)
        emit heightChanging();
    m_fittedHeight = preferred;
    // Exactly as tall as the views shown: never smaller, so nothing is cut off and no scroll bar is needed.
    setMinimumHeight(preferred);
    setMaximumHeight(preferred);
    updateGeometry();
    emit sizeWanted();
}

QSize ColorPanel::sizeHint() const {
    return {QWidget::sizeHint().width(), preferredHeight()};
}

void ColorPanel::bindToggle(ColorView v, QAction* action) {
    // The button takes the action's look, so the action carries the button's icon (menus do not show icons).
    QToolButton* b = m_toggles[std::size_t(v)];
    action->setIcon(b->icon());
    action->setToolTip(QCoreApplication::translate("ColorPanel", kViews[std::size_t(v)].tip));
    b->setDefaultAction(action);
}

QColor ColorPanel::askColor(const QColor& initial) {
    return QColorDialog::getColor(initial, this, tr("Color"));
}

void ColorPanel::showColor(const QColor& color) {
    // The user's colour, so an inline sheet (the theme gives only its ring).
    m_swatch->setStyleSheet(QStringLiteral("background:%1; border:%2px solid %3; border-radius:%4px;")
                                .arg(color.name())
                                .arg(Theme::kBorderStrong)
                                .arg(Theme::color(Theme::Token::Text).name())
                                .arg(kSwatchSize / 2));
}

void ColorPanel::showTransparent(bool on) {
    m_transparent->setChecked(on);
    m_swatch->setIcon(on ? QIcon(checkerDisc(kSwatchSize - 4, devicePixelRatioF())) : QIcon());
    m_swatch->setIconSize(QSize(kSwatchSize - 4, kSwatchSize - 4));  // component metric: the checker disc 2 px inside the swatch's ring
}

void ColorPanel::showSecondaryColor(const QColor& color) {
    m_secondary->setStyleSheet(QStringLiteral("background:%1; border:%2px solid %3; border-radius:%4px;")
                                   .arg(color.name())
                                   .arg(Theme::kBorderStrong)
                                   .arg(Theme::color(Theme::Token::Text).name())
                                   .arg(kSecondarySize / 2));
}

void ColorPanel::placeColorTools() {
    const QWidget* circle = m_views[std::size_t(ColorView::Circle)];
    const int x = circle->width() - kToolSize - Theme::kSpaceHair;
    m_exchange->move(x, m_wheel->y());
    m_transparent->move(x, m_wheel->y() + kToolSize + Theme::kSpaceHair);
}

bool ColorPanel::eventFilter(QObject* watched, QEvent* event) {
    if (m_exchange && ((watched == m_wheel && (event->type() == QEvent::Resize || event->type() == QEvent::Move)) ||
                       (watched == m_views[std::size_t(ColorView::Circle)] && event->type() == QEvent::Resize)))
        placeColorTools();
    if (watched == m_swatch->parentWidget() && event->type() == QEvent::Resize) {
        const int right = m_wheel->width();
        m_secondary->move(right - kSecondarySize, kSwatchSize - kSecondarySize / 2);
        m_swatch->move(right - kSecondarySize / 2 - kSwatchSize, 0);
    }
    return QWidget::eventFilter(watched, event);
}

}
