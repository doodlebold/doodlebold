#include "ui/color/ColorSliderViews.h"

#include "core/color/ColorAssets.h"
#include "core/color/ColorState.h"
#include "ui/Theme.h"

#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace doodlebold {

namespace {

// A channel row's geometry, px (component metrics): the channel's letter, its three-digit value and the colour track.
constexpr int kLetterWidth = 14;  // component metric, described above
constexpr int kNumberWidth = 30;  // component metric, described above
constexpr int kTrackHeight = 11;  // component metric, described above

QVBoxLayout* column(QWidget* w) {
    auto* layout = new QVBoxLayout(w);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    return layout;
}

}

ColorSliderRow::ColorSliderRow(QString letter, int maximum, QWidget* parent)
    : QWidget(parent), m_letter(std::move(letter)), m_maximum(maximum) {
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
}

void ColorSliderRow::setLetter(const QString& letter) {
    m_letter = letter;
    update();
}

void ColorSliderRow::setGradient(Gradient gradient) {
    m_gradient = std::move(gradient);
    update();
}

void ColorSliderRow::setPosition(double position) {
    m_position = std::clamp(position, 0.0, 1.0);
    update();
}

int ColorSliderRow::number() const {
    return int(std::lround(m_position * m_maximum));
}

QRect ColorSliderRow::track() const {
    return QRect(kLetterWidth + 2, 2, width() - kLetterWidth - kNumberWidth - 6, kTrackHeight);
}

void ColorSliderRow::paintEvent(QPaintEvent*) {
    QPainter p(this);
    const QRect t = track();
    p.setPen(Theme::color(Theme::Token::Text));
    p.drawText(QRect(0, 0, kLetterWidth, t.bottom() + 2), Qt::AlignCenter, m_letter);
    if (m_gradient && t.width() > 1) {
        for (int x = 0; x < t.width(); ++x) {
            p.setPen(m_gradient((x + 0.5) / t.width()));
            p.drawLine(t.left() + x, t.top(), t.left() + x, t.bottom());
        }
    }
    p.setPen(Theme::color(Theme::Token::Border));
    p.setBrush(Qt::NoBrush);
    p.drawRect(t.adjusted(-1, -1, 0, 0));
    const double x = t.left() + m_position * (t.width() - 1);
    QPainterPath arrow;
    arrow.moveTo(x, t.bottom() + 2);
    arrow.lineTo(x - 4.5, t.bottom() + 8);
    arrow.lineTo(x + 4.5, t.bottom() + 8);
    arrow.closeSubpath();
    p.setRenderHint(QPainter::Antialiasing);
    p.setBrush(Theme::color(Theme::Token::Window));
    p.setPen(QPen(Theme::color(Theme::Token::Text), Theme::kBorder));
    p.drawPath(arrow);
    p.setRenderHint(QPainter::Antialiasing, false);
    p.drawText(QRect(width() - kNumberWidth, 0, kNumberWidth, t.bottom() + 2), Qt::AlignRight | Qt::AlignVCenter,
               QStringLiteral("%1").arg(number(), 3, 10, QLatin1Char('0')));
}

void ColorSliderRow::mousePressEvent(QMouseEvent* e) {
    if (e->button() == Qt::LeftButton)
        dragTo(e->position().x());
}

void ColorSliderRow::mouseMoveEvent(QMouseEvent* e) {
    if (e->buttons() & Qt::LeftButton)
        dragTo(e->position().x());
}

void ColorSliderRow::dragTo(double x) {
    const QRect t = track();
    const double raw = std::clamp((x - t.left()) / std::max(1, t.width() - 1), 0.0, 1.0);
    setPosition(std::round(raw * m_maximum) / m_maximum);
    emit moved(m_position);
}

RgbSlidersView::RgbSlidersView(ColorState* state, QWidget* parent) : QWidget(parent), m_state(state) {
    setObjectName(QStringLiteral("rgbSliders"));
    auto* layout = column(this);
    const QString letters[3] = {QStringLiteral("R"), QStringLiteral("G"), QStringLiteral("B")};
    for (int i = 0; i < 3; ++i) {
        auto* r = new ColorSliderRow(letters[i], 255, this);
        r->setObjectName(QStringLiteral("rgbSlider%1").arg(letters[i]));
        layout->addWidget(r);
        m_rows[std::size_t(i)] = r;
        connect(r, &ColorSliderRow::moved, this, [this, i](double position) {
            if (!m_state)
                return;
            int c[3] = {m_state->color().red(), m_state->color().green(), m_state->color().blue()};
            c[i] = int(std::lround(position * 255));
            m_state->setColor(QColor(c[0], c[1], c[2]));
        });
    }
    connect(state, &ColorState::changed, this, &RgbSlidersView::sync);
    sync();
}

void RgbSlidersView::sync() {
    if (!m_state)
        return;
    const QColor c = m_state->color();
    const int v[3] = {c.red(), c.green(), c.blue()};
    for (int i = 0; i < 3; ++i) {
        m_rows[std::size_t(i)]->setPosition(v[i] / 255.0);
        m_rows[std::size_t(i)]->setGradient([c, i](double t) {
            int ch[3] = {c.red(), c.green(), c.blue()};
            ch[i] = int(std::lround(t * 255));
            return QColor(ch[0], ch[1], ch[2]);
        });
    }
}

HsvSlidersView::HsvSlidersView(ColorState* state, ColorAssets* assets, QWidget* parent)
    : QWidget(parent), m_state(state), m_assets(assets) {
    setObjectName(QStringLiteral("hsvSliders"));
    auto* layout = column(this);
    const QString letters[3] = {QStringLiteral("H"), QStringLiteral("S"), QStringLiteral("V")};
    const int maxima[3] = {359, 100, 100};
    for (int i = 0; i < 3; ++i) {
        auto* r = new ColorSliderRow(letters[i], maxima[i], this);
        r->setObjectName(QStringLiteral("hsvSlider%1").arg(i));
        layout->addWidget(r);
        m_rows[std::size_t(i)] = r;
        connect(r, &ColorSliderRow::moved, this, [this, i](double position) { edit(i, position); });
    }
    connect(state, &ColorState::changed, this, &HsvSlidersView::sync);
    connect(assets, &ColorAssets::optionsChanged, this, [this] {
        m_shown = QColor();
        sync();
    });
    sync();
}

void HsvSlidersView::sync() {
    if (!m_state || !m_assets)
        return;
    const color::SliderMode mode = m_assets->sliderMode();
    if (m_state->color() != m_shown) {
        m_values = color::slidersFor(mode, m_state->color(), m_state->hue());
        m_shown = m_state->color();
    }
    m_values.hue = m_state->hue();
    m_rows[2]->setLetter(mode == color::SliderMode::Hsl ? QStringLiteral("L") : QStringLiteral("V"));
    m_rows[0]->setPosition(std::fmod(std::round(m_values.hue), 360.0) / 359.0);  // 359.6° reads 000, as 0°
    m_rows[1]->setPosition(m_values.second);
    m_rows[2]->setPosition(m_values.third);
    const color::SliderValues v = m_values;
    m_rows[0]->setGradient([](double t) { return color::fromHsv(t * 360.0, 1.0, 1.0); });
    m_rows[1]->setGradient([mode, v](double t) { return color::fromSliders(mode, {v.hue, t, v.third}); });
    m_rows[2]->setGradient([mode, v](double t) { return color::fromSliders(mode, {v.hue, v.second, t}); });
}

void HsvSlidersView::edit(int index, double position) {
    if (!m_state || !m_assets)
        return;
    switch (index) {
    case 0: m_values.hue = position * 359.0; break;
    case 1: m_values.second = position; break;
    default: m_values.third = position; break;
    }
    m_shown = color::fromSliders(m_assets->sliderMode(), m_values);
    m_state->setColor(m_shown, m_values.hue);
}

}
