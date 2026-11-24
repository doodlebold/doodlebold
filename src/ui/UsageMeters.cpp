#include "ui/UsageMeters.h"

#include "platform/SystemUsage.h"
#include "ui/Theme.h"

#include <QDir>
#include <QPainter>
#include <QStandardPaths>

#include <algorithm>
#include <cmath>

namespace doodlebold {

namespace {

constexpr int kBarWidth = 90;   // px: a meter's length (component metric)
constexpr int kBarHeight = 6;   // px: a meter's thickness (component metric)
constexpr int kGap = Theme::kSpace2;
constexpr int kMeterGap = Theme::kSpace5;

}

UsageMeters::UsageMeters(QWidget* parent) : QWidget(parent) {
    setObjectName(QStringLiteral("usageMeters"));
    m_timer.setInterval(1000);
    connect(&m_timer, &QTimer::timeout, this, &UsageMeters::refresh);
    m_timer.start();
    refresh();
}

void UsageMeters::refresh() {
    if (const auto m = platform::usage::memory()) {
        m_memory = platform::usage::percent(m->privateBytes, m->totalBytes);
        m_resident = platform::usage::percent(m->residentBytes, m->totalBytes);
    } else {
        m_memory = m_resident = -1;
    }
    const QString work = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (const auto d = platform::usage::driveFullness(work.isEmpty() ? QDir::homePath() : work))
        m_drive = std::clamp(static_cast<int>(std::lround(*d * 100.0)), 0, 100);  // 9.9 % full shows 10 %
    else
        m_drive = -1;
    update();
}

QString UsageMeters::memoryText() const {
    return m_memory < 0 ? QStringLiteral("–") : QStringLiteral("%1% (%2%)").arg(m_memory).arg(m_resident);
}

QString UsageMeters::driveText() const {
    return m_drive < 0 ? QStringLiteral("–") : QStringLiteral("%1%").arg(m_drive);
}

QSize UsageMeters::sizeHint() const {
    const QFontMetrics fm = fontMetrics();
    const int labels = fm.horizontalAdvance(tr("Memory Usage")) + fm.horizontalAdvance(tr("Drive Usage"));
    const int values = fm.horizontalAdvance(QStringLiteral("100% (100%)")) + fm.horizontalAdvance(QStringLiteral("100%"));
    return {labels + values + 2 * kBarWidth + 4 * kGap + kMeterGap + kGap, fm.height() + kGap};
}

void UsageMeters::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QFontMetrics fm = fontMetrics();
    int x = 0;
    const auto meter = [&](const QString& label, int value, const QString& text, const QColor& fill) {
        p.setPen(Theme::color(Theme::Token::TextMuted));
        const int w = fm.horizontalAdvance(label);
        p.drawText(QRect(x, 0, w, height()), Qt::AlignVCenter | Qt::AlignLeft, label);
        x += w + kGap;
        const QRectF bar(x, (height() - kBarHeight) / 2.0, kBarWidth, kBarHeight);
        const double r = kBarHeight / 2.0;
        p.setPen(Qt::NoPen);
        p.setBrush(Theme::color(Theme::Token::Base));
        p.drawRoundedRect(bar, r, r);
        if (value > 0) {
            p.setBrush(fill);
            p.drawRoundedRect(QRectF(bar.left(), bar.top(), std::max(kBarHeight * 1.0, bar.width() * value / 100.0), bar.height()), r, r);
        }
        x += kBarWidth + kGap;
        p.setPen(Theme::color(Theme::Token::Text));
        const int t = fm.horizontalAdvance(text);
        p.drawText(QRect(x, 0, t, height()), Qt::AlignVCenter | Qt::AlignLeft, text);
        x += t + kMeterGap;
    };
    meter(tr("Memory Usage"), m_memory, memoryText(), Theme::color(Theme::Token::Success));
    meter(tr("Drive Usage"), m_drive, driveText(), QColor::fromRgba(Theme::kDriveMeter));
}

}
