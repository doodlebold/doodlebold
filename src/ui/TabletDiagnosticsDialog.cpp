#include "ui/TabletDiagnosticsDialog.h"

#include "core/input/InputSample.h"
#include "core/input/TabletStats.h"
#include "platform/Platform.h"
#include "ui/TabletInput.h"
#include "ui/Theme.h"
#include "platform/PenCursor.h"

#include <QClipboard>
#include <QCoreApplication>
#include <QDateTime>
#include <QFormLayout>
#include <QFrame>
#include <QGridLayout>
#include <QGroupBox>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QTabletEvent>
#include <QStyle>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <deque>
#include <utility>

namespace doodlebold {

namespace {

QString penCursorText() {
    return platform::pen_cursor::available() ? QStringLiteral("Doodlebold (Wayland tablet-v2)") : QStringLiteral("Qt");
}

using Theme::Token;

constexpr int kSpacing = Theme::comfortable::kRowGap;
constexpr int kRefreshIntervalMs = 17;
constexpr qint64 kHistoryMs = 3000;
constexpr double kMaxTilt = 60.0;

QString number(double v, int precision, int width = 0, bool withSign = false) {
    QString s = QString::number(v, 'f', precision);
    if (withSign && v >= 0.0)
        s.prepend(QLatin1Char('+'));
    return s.rightJustified(width);
}

QString kindName(PointerKind kind) {
    switch (kind) {
    case PointerKind::Mouse: return QObject::tr("Mouse");
    case PointerKind::Pen: return QObject::tr("Pen");
    case PointerKind::Eraser: return QObject::tr("Eraser");
    case PointerKind::Cursor: return QObject::tr("Cursor");
    case PointerKind::Touch: return QObject::tr("Touch");
    case PointerKind::Unknown: break;
    }
    return QObject::tr("Unknown");
}

QString orDash(const QString& s) {
    return s.isEmpty() ? QStringLiteral("-") : s;
}

QString buttonsText(std::uint8_t buttons) {
    QStringList names;
    if (buttons & ButtonTip)
        names << QObject::tr("Tip");
    if (buttons & ButtonBarrel1)
        names << QObject::tr("Button 1 (lower)");
    if (buttons & ButtonBarrel2)
        names << QObject::tr("Button 2 (upper)");
    return names.isEmpty() ? QObject::tr("none") : names.join(QStringLiteral(", "));
}

}

class DiagBar : public QWidget {
public:
    explicit DiagBar(QWidget* parent = nullptr) : QWidget(parent) {
        setMinimumSize(80, 16);  // component metric: a pressure bar
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    }

    void setValues(double value, double peak) {
        if (value == m_value && peak == m_peak)
            return;
        m_value = value;
        m_peak = peak;
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        const QRectF r = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
        p.fillRect(r, Theme::color(Token::Base));
        const double w = r.width() * std::clamp(m_value, 0.0, 1.0);
        p.fillRect(QRectF(r.left(), r.top(), w, r.height()), Theme::color(Token::Accent));
        if (m_peak > 0.0) {
            const double x = r.left() + (r.width() - 1.0) * std::clamp(m_peak, 0.0, 1.0);
            p.setPen(QPen(Theme::color(Token::Text), Theme::kBorder));
            p.drawLine(QPointF(x + 0.5, r.top()), QPointF(x + 0.5, r.bottom()));
        }
        p.setPen(Theme::color(Token::Border));
        p.drawRect(r);
    }

private:
    double m_value = 0.0;
    double m_peak = 0.0;
};

// Small circular indicator: the dot sits where the pen leans (centre = upright, edge = 60 degrees).
class DiagTilt : public QWidget {
public:
    explicit DiagTilt(QWidget* parent = nullptr) : QWidget(parent) { setFixedSize(56, 56); }  // component metric: the tilt dial

    void setTilt(double x, double y) {
        if (x == m_x && y == m_y)
            return;
        m_x = x;
        m_y = y;
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const QRectF r = QRectF(rect()).adjusted(2.5, 2.5, -2.5, -2.5);
        const QPointF c = r.center();
        const double radius = r.width() / 2.0;
        p.setBrush(Theme::color(Token::Base));
        p.setPen(Theme::color(Token::Border));
        p.drawEllipse(r);
        p.drawLine(QPointF(r.left(), c.y()), QPointF(r.right(), c.y()));
        p.drawLine(QPointF(c.x(), r.top()), QPointF(c.x(), r.bottom()));
        QPointF d(m_x / kMaxTilt, m_y / kMaxTilt);
        const double len = std::hypot(d.x(), d.y());
        if (len > 1.0)
            d /= len;
        const QPointF tip = c + d * radius;
        p.setPen(QPen(Theme::color(Token::Accent), Theme::kGlyphStroke));
        p.drawLine(c, tip);
        p.setPen(Qt::NoPen);
        p.setBrush(Theme::color(Token::Accent));
        p.drawEllipse(tip, 3.5, 3.5);
    }

private:
    double m_x = 0.0;
    double m_y = 0.0;
};

class DiagIndicator : public QWidget {
public:
    explicit DiagIndicator(QString text, QWidget* parent = nullptr) : QWidget(parent), m_text(std::move(text)) {
        setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    }

    void setLit(bool lit) {
        if (lit == m_lit)
            return;
        m_lit = lit;
        update();
    }

    QSize sizeHint() const override {
        const QFontMetrics fm(font());
        return {fm.horizontalAdvance(m_text) + 14, fm.height() + 8};
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const QRectF r = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
        p.setBrush(Theme::color(m_lit ? Token::Accent : Token::Surface));
        p.setPen(Theme::color(m_lit ? Token::Accent : Token::Border));
        p.drawRoundedRect(r, Theme::kRadiusXs, Theme::kRadiusXs);
        p.setPen(Theme::color(m_lit ? Token::AccentText : Token::TextMuted));
        p.drawText(r, Qt::AlignCenter, m_text);
    }

private:
    QString m_text;
    bool m_lit = false;
};

class DiagHistory : public QWidget {
public:
    explicit DiagHistory(QWidget* parent = nullptr) : QWidget(parent) {
        setMinimumHeight(90);  // component metric: the pressure history graph
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    }

    void add(qint64 timeMs, double pressure) {
        m_points.push_back({timeMs, pressure});
        m_now = timeMs;
        prune();
    }

    void setNow(qint64 timeMs) {
        m_now = timeMs;
        prune();
        update();
    }

    bool hasData() const { return !m_points.empty(); }

    void clear() {
        m_points.clear();
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.fillRect(rect(), Theme::color(Token::Base));

        const QFontMetrics fm(font());
        const int left = fm.horizontalAdvance(QStringLiteral("0.00")) + 8;
        const QRectF plot(left, 6, width() - left - 6, height() - 12);
        if (plot.width() <= 0 || plot.height() <= 0)
            return;

        p.setFont(font());
        for (int i = 0; i <= 4; ++i) {
            const double y = plot.bottom() - plot.height() * i / 4.0;
            p.setPen(Theme::color(Token::Border));
            p.drawLine(QPointF(plot.left(), y), QPointF(plot.right(), y));
            p.setPen(Theme::color(Token::TextMuted));
            p.drawText(QRectF(0, y - fm.height() / 2.0, left - 4, fm.height()), Qt::AlignRight | Qt::AlignVCenter,
                       QString::number(i * 0.25, 'f', 2));
        }

        if (m_points.size() < 2)
            return;
        QPainterPath path;
        bool first = true;
        for (const Point& pt : m_points) {
            const double x = plot.right() - plot.width() * static_cast<double>(m_now - pt.time) / kHistoryMs;
            const double y = plot.bottom() - plot.height() * std::clamp(pt.pressure, 0.0, 1.0);
            if (first)
                path.moveTo(x, y);
            else
                path.lineTo(x, y);
            first = false;
        }
        p.setClipRect(plot.adjusted(-1, -2, 1, 2));
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(QPen(Theme::color(Token::Accent), Theme::kGlyphStroke, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.drawPath(path);
    }

private:
    struct Point {
        qint64 time;
        double pressure;
    };

    void prune() {
        while (!m_points.empty() && m_now - m_points.front().time > kHistoryMs)
            m_points.pop_front();
    }

    std::deque<Point> m_points;
    qint64 m_now = 0;
};

class DiagTestPad : public QWidget {
public:
    explicit DiagTestPad(TabletStats* stats, QWidget* parent = nullptr) : QWidget(parent), m_stats(stats) {
        setAttribute(Qt::WA_TabletTracking);
        setMouseTracking(true);
        setMinimumSize(240, 160);  // component metric: the test pad
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    }

    void clearStroke() {
        m_stroke.clear();
        m_down = false;
        update();
    }

protected:
    void tabletEvent(QTabletEvent* e) override {
        const InputSample s = TabletInput::fromTablet(*e, QTransform());
        m_stats->record(s, TabletInput::describe(e->pointingDevice()));
        m_hover = e->position();
        m_hasHover = true;
        switch (e->type()) {
        case QEvent::TabletPress:
            m_stroke.clear();
            m_down = true;
            addPoint(e->position(), s.pressure);
            break;
        case QEvent::TabletMove:
            if (m_down)
                addPoint(e->position(), s.pressure);
            break;
        case QEvent::TabletRelease: m_down = false; break;
        default: break;
        }
        update();
        e->accept();  // accepted tablet events are not re-sent as synthesized mouse events
    }

    void mousePressEvent(QMouseEvent* e) override {
        const InputSample s = TabletInput::fromMouse(*e, QTransform());
        m_stats->record(s, TabletInput::describe(e->pointingDevice()));
        if (e->button() == Qt::LeftButton) {
            m_stroke.clear();
            m_down = true;
            addPoint(e->position(), s.pressure);
        }
        m_hover = e->position();
        m_hasHover = true;
        update();
        e->accept();
    }

    void mouseMoveEvent(QMouseEvent* e) override {
        const InputSample s = TabletInput::fromMouse(*e, QTransform());
        m_stats->record(s, TabletInput::describe(e->pointingDevice()));
        if (m_down && (e->buttons() & Qt::LeftButton))
            addPoint(e->position(), s.pressure);
        m_hover = e->position();
        m_hasHover = true;
        update();
        e->accept();
    }

    void mouseReleaseEvent(QMouseEvent* e) override {
        const InputSample s = TabletInput::fromMouse(*e, QTransform());
        m_stats->record(s, TabletInput::describe(e->pointingDevice()));
        if (e->button() == Qt::LeftButton)
            m_down = false;
        update();
        e->accept();
    }

    void leaveEvent(QEvent*) override {
        m_hasHover = false;
        update();
    }

    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.fillRect(rect(), Theme::color(Token::Base));
        p.setPen(Theme::color(Token::Border));
        p.drawRect(rect().adjusted(0, 0, -1, -1));

        if (m_stroke.empty() && !m_hasHover) {
            p.setPen(Theme::color(Token::TextMuted));
            p.drawText(rect(), Qt::AlignCenter | Qt::TextWordWrap, tr("Test pad: draw here with your pen or mouse"));
        }

        const QColor ink = Theme::color(Token::Accent);
        if (m_stroke.size() == 1) {
            p.setPen(Qt::NoPen);
            p.setBrush(ink);
            const double r = widthFor(m_stroke.front().pressure) / 2;
            p.drawEllipse(m_stroke.front().pos, r, r);
        }
        for (std::size_t i = 1; i < m_stroke.size(); ++i) {
            const double pressure = (m_stroke[i - 1].pressure + m_stroke[i].pressure) / 2.0;
            p.setPen(QPen(ink, widthFor(pressure), Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            p.drawLine(m_stroke[i - 1].pos, m_stroke[i].pos);
        }

        if (m_hasHover && !m_down) {
            p.setPen(QPen(Theme::color(Token::TextMuted), Theme::kBorder));
            p.setBrush(Qt::NoBrush);
            p.drawEllipse(m_hover, 5.0, 5.0);
        }
    }

private:
    struct StrokePoint {
        QPointF pos;
        double pressure;
    };

    static constexpr std::size_t kMaxPoints = 20000;

    static double widthFor(double pressure) { return 1.0 + 13.0 * std::clamp(pressure, 0.0, 1.0); }

    void addPoint(const QPointF& pos, double pressure) {
        if (m_stroke.size() >= kMaxPoints)
            m_stroke.pop_front();
        m_stroke.push_back({pos, pressure});
    }

    TabletStats* m_stats;
    std::deque<StrokePoint> m_stroke;
    QPointF m_hover;
    bool m_hasHover = false;
    bool m_down = false;
};

TabletDiagnosticsDialog::TabletDiagnosticsDialog(TabletStats* stats, QWidget* parent) : QDialog(parent), m_stats(stats) {
    setWindowTitle(tr("Tablet Diagnostics"));
    setModal(false);
    resize(900, 600);
    m_clock.start();

    m_refreshTimer = new QTimer(this);
    m_refreshTimer->setSingleShot(true);
    m_refreshTimer->setInterval(kRefreshIntervalMs);
    connect(m_refreshTimer, &QTimer::timeout, this, &TabletDiagnosticsDialog::refresh);

    buildUi();

    connect(m_stats, &TabletStats::sampleRecorded, this, &TabletDiagnosticsDialog::onSample);
    connect(m_stats, &TabletStats::proximityChanged, this, &TabletDiagnosticsDialog::onProximity);
    onProximity(m_stats->inProximity());
    refresh();
}

void TabletDiagnosticsDialog::buildUi() {
    const QFont mono = Theme::font(Theme::TextRole::Mono);

    auto makeValue = [&mono](QWidget* parent) {
        auto* label = new QLabel(parent);
        label->setFont(mono);
        label->setTextFormat(Qt::PlainText);
        label->setTextInteractionFlags(Qt::TextSelectableByMouse);
        return label;
    };
    auto makeText = [](QWidget* parent) {
        auto* label = new QLabel(parent);
        label->setTextFormat(Qt::PlainText);
        label->setWordWrap(true);
        label->setTextInteractionFlags(Qt::TextSelectableByMouse);
        return label;
    };
    auto makeForm = [](QWidget* host) {
        auto* form = new QFormLayout(host);
        form->setSpacing(kSpacing);
        form->setLabelAlignment(Qt::AlignLeft);
        form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
        return form;
    };

    auto* deviceBox = new QGroupBox(tr("Device"), this);
    auto* deviceForm = makeForm(deviceBox);
    m_proximity = new QLabel(deviceBox);
    m_proximity->setFont(Theme::font(Theme::TextRole::BodyStrong));
    m_deviceName = makeText(deviceBox);
    m_deviceType = makeText(deviceBox);
    m_pointerType = makeText(deviceBox);
    m_capabilities = makeText(deviceBox);
    m_uniqueId = makeValue(deviceBox);
    m_buttonCount = makeValue(deviceBox);
    deviceForm->addRow(tr("Proximity"), m_proximity);
    deviceForm->addRow(tr("Name"), m_deviceName);
    deviceForm->addRow(tr("Device type"), m_deviceType);
    deviceForm->addRow(tr("Pointer type"), m_pointerType);
    deviceForm->addRow(tr("Capabilities"), m_capabilities);
    deviceForm->addRow(tr("Unique ID"), m_uniqueId);
    deviceForm->addRow(tr("Button count"), m_buttonCount);

    auto* liveBox = new QGroupBox(tr("Live values"), this);
    auto* grid = new QGridLayout(liveBox);
    grid->setSpacing(kSpacing);
    grid->setColumnStretch(2, 1);
    m_pressure = makeValue(liveBox);
    m_peak = makeValue(liveBox);
    m_tiltX = makeValue(liveBox);
    m_tiltY = makeValue(liveBox);
    m_rotation = makeValue(liveBox);
    m_tangential = makeValue(liveBox);
    m_position = makeValue(liveBox);
    m_pointerKind = makeValue(liveBox);
    m_rate = makeValue(liveBox);
    m_interval = makeValue(liveBox);
    m_total = makeValue(liveBox);
    m_pressureBar = new DiagBar(liveBox);
    m_tiltView = new DiagTilt(liveBox);
    auto* resetPeak = new QPushButton(tr("Reset"), liveBox);
    resetPeak->setToolTip(tr("Reset the peak pressure"));
    connect(resetPeak, &QPushButton::clicked, this, [this] {
        m_stats->resetPeaks();
        refresh();
    });
    m_tipIndicator = new DiagIndicator(tr("Tip"), liveBox);
    m_barrel1Indicator = new DiagIndicator(tr("Button 1 (lower)"), liveBox);
    m_barrel2Indicator = new DiagIndicator(tr("Button 2 (upper)"), liveBox);
    auto* buttonRow = new QHBoxLayout;
    buttonRow->setSpacing(kSpacing);
    buttonRow->addWidget(m_tipIndicator);
    buttonRow->addWidget(m_barrel1Indicator);
    buttonRow->addWidget(m_barrel2Indicator);
    buttonRow->addStretch(1);

    int row = 0;
    auto addRow = [&](const QString& name, QLabel* value) {
        grid->addWidget(new QLabel(name, liveBox), row, 0);
        grid->addWidget(value, row, 1);
        return row++;
    };
    grid->addWidget(m_pressureBar, addRow(tr("Pressure"), m_pressure), 2);
    {
        const int r = addRow(tr("Peak pressure"), m_peak);
        grid->addWidget(resetPeak, r, 2, Qt::AlignLeft);
    }
    {
        const int r = addRow(tr("Tilt X"), m_tiltX);
        addRow(tr("Tilt Y"), m_tiltY);
        grid->addWidget(m_tiltView, r, 2, 2, 1, Qt::AlignLeft | Qt::AlignVCenter);
    }
    addRow(tr("Rotation"), m_rotation);
    addRow(tr("Tangential pressure"), m_tangential);
    addRow(tr("Position (doc)"), m_position);
    grid->addWidget(new QLabel(tr("Buttons"), liveBox), row, 0);
    grid->addLayout(buttonRow, row++, 1, 1, 2);
    addRow(tr("Pointer kind"), m_pointerKind);
    addRow(tr("Event rate"), m_rate);
    addRow(tr("Median interval"), m_interval);
    addRow(tr("Total events"), m_total);

    auto* systemBox = new QGroupBox(tr("System"), this);
    auto* systemForm = makeForm(systemBox);
    auto* session = makeText(systemBox);
    session->setText(platform::sessionDescription());
    auto* qtVersion = makeText(systemBox);
    qtVersion->setText(QString::fromLatin1(qVersion()));
    auto* theme = makeText(systemBox);
    theme->setText(Theme::current().name);
    auto* penCursor = makeText(systemBox);
    penCursor->setText(penCursorText());
    m_deviceList = new QPlainTextEdit(systemBox);
    m_deviceList->setReadOnly(true);
    m_deviceList->setFont(mono);
    m_deviceList->setLineWrapMode(QPlainTextEdit::NoWrap);
    m_deviceList->setPlainText(platform::inputDeviceReport().join(QLatin1Char('\n')));
    m_deviceList->setFixedHeight(QFontMetrics(mono).lineSpacing() * 5 + 12);  // component metric: five lines and the list's padding
    systemForm->addRow(tr("Session"), session);
    systemForm->addRow(tr("Qt version"), qtVersion);
    systemForm->addRow(tr("Theme"), theme);
    systemForm->addRow(tr("Pen cursor"), penCursor);
    systemForm->addRow(tr("Input devices"), m_deviceList);
    if (!platform::padButtonsSupported()) {
        auto* pad = makeText(systemBox);
        pad->setText(tr("Pad buttons (ExpressKeys): not available to applications on this platform"));
        systemForm->addRow(pad);
    }

    auto* leftContent = new QWidget;
    auto* leftLayout = new QVBoxLayout(leftContent);
    leftLayout->setContentsMargins(0, 0, kSpacing, 0);
    leftLayout->setSpacing(kSpacing);
    leftLayout->addWidget(deviceBox);
    leftLayout->addWidget(liveBox);
    leftLayout->addWidget(systemBox);
    leftLayout->addStretch(1);
    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setWidget(leftContent);
    scroll->setMinimumWidth(460);  // component metric: the readings column

    m_pad = new DiagTestPad(m_stats, this);
    m_history = new DiagHistory(this);
    auto* clear = new QPushButton(tr("Clear"), this);
    clear->setToolTip(tr("Clear the test pad"));
    connect(clear, &QPushButton::clicked, m_pad, &DiagTestPad::clearStroke);
    m_copyButton = new QPushButton(tr("Copy report"), this);
    m_copyButton->setToolTip(tr("Copy a plain-text report to the clipboard"));
    connect(m_copyButton, &QPushButton::clicked, this, &TabletDiagnosticsDialog::copyReport);

    auto* padBox = new QGroupBox(tr("Test pad"), this);
    auto* padLayout = new QVBoxLayout(padBox);
    padLayout->setSpacing(kSpacing);
    padLayout->addWidget(m_pad, 1);
    auto* historyBox = new QGroupBox(tr("Pressure history (last 3 s)"), this);
    auto* historyLayout = new QVBoxLayout(historyBox);
    historyLayout->addWidget(m_history);
    auto* actions = new QHBoxLayout;
    actions->setSpacing(kSpacing);
    actions->addWidget(clear);
    actions->addStretch(1);
    actions->addWidget(m_copyButton);
    padLayout->addLayout(actions);

    auto* right = new QVBoxLayout;
    right->setSpacing(kSpacing);
    right->addWidget(padBox, 3);
    right->addWidget(historyBox, 1);

    auto* root = new QHBoxLayout(this);
    Theme::padDialog(root, true);
    root->addWidget(scroll, 11);
    root->addLayout(right, 9);
}

bool TabletDiagnosticsDialog::idle() const {
    return m_lastEventAt < 0 || m_clock.elapsed() - m_lastEventAt > static_cast<qint64>(TabletStats::kWindowMs);
}

// The stats window only advances on new events, so report 0 once the pen has been idle for longer than the window.
double TabletDiagnosticsDialog::eventRateHz() const {
    return idle() ? 0.0 : m_stats->eventRateHz();
}

double TabletDiagnosticsDialog::medianIntervalMs() const {
    return idle() ? 0.0 : m_stats->medianIntervalMs();
}

void TabletDiagnosticsDialog::onSample() {
    m_lastEventAt = m_clock.elapsed();
    m_history->add(m_lastEventAt, m_stats->lastSample().pressure);
    if (!m_refreshTimer->isActive())
        m_refreshTimer->start();
}

void TabletDiagnosticsDialog::onProximity(bool inProximity) {
    m_proximity->setText(inProximity ? tr("In range") : tr("Out of range"));
    m_proximity->setProperty("role", inProximity ? "accent" : "hint");  // the style sheet's TextAccent or TextMuted
    m_proximity->style()->unpolish(m_proximity);
    m_proximity->style()->polish(m_proximity);
    refresh();
}

void TabletDiagnosticsDialog::refresh() {
    const InputSample& s = m_stats->lastSample();
    const InputDeviceInfo& d = m_stats->lastDevice();
    const double pressure = m_stats->totalEvents() == 0 ? 0.0 : s.pressure;  // InputSample defaults to 1.0
    const bool haveDevice = !d.name.isEmpty();

    m_deviceName->setText(haveDevice ? d.name : tr("(no events received yet)"));
    m_deviceType->setText(orDash(d.type));
    m_pointerType->setText(orDash(d.pointerType));
    m_capabilities->setText(orDash(d.capabilities));
    m_uniqueId->setText(d.uniqueId < 0 ? QStringLiteral("-") : QString::number(d.uniqueId));
    m_buttonCount->setText(haveDevice ? QString::number(d.buttonCount) : QStringLiteral("-"));

    m_pressure->setText(number(pressure, 4, 6));
    m_peak->setText(number(m_stats->peakPressure(), 4, 6));
    m_pressureBar->setValues(pressure, m_stats->peakPressure());
    m_tiltX->setText(number(s.tiltX, 1, 5, true) + QStringLiteral(" °"));
    m_tiltY->setText(number(s.tiltY, 1, 5, true) + QStringLiteral(" °"));
    m_tiltView->setTilt(s.tiltX, s.tiltY);
    m_rotation->setText(number(s.rotation, 1, 5) + QStringLiteral(" °"));
    m_tangential->setText(number(s.tangentialPressure, 4, 7, true));
    m_position->setText(number(s.pos.x(), 2, 8) + QStringLiteral(", ") + number(s.pos.y(), 2, 8));
    m_pointerKind->setText(kindName(s.kind));
    m_rate->setText(number(eventRateHz(), 1, 6) + QStringLiteral(" Hz"));
    m_interval->setText(number(medianIntervalMs(), 1, 6) + QStringLiteral(" ms"));
    m_total->setText(QString::number(m_stats->totalEvents()));
    m_tipIndicator->setLit(s.buttons & ButtonTip);
    m_barrel1Indicator->setLit(s.buttons & ButtonBarrel1);
    m_barrel2Indicator->setLit(s.buttons & ButtonBarrel2);

    m_history->setNow(m_clock.elapsed());
    if (m_history->hasData())
        m_refreshTimer->start();
}

void TabletDiagnosticsDialog::copyReport() {
    QGuiApplication::clipboard()->setText(reportText());
    m_copyButton->setText(tr("Copied"));
    QTimer::singleShot(1500, m_copyButton, [this] { m_copyButton->setText(tr("Copy report")); });
}

QString TabletDiagnosticsDialog::reportText() const {
    const InputSample& s = m_stats->lastSample();
    const InputDeviceInfo& d = m_stats->lastDevice();
    const double pressure = m_stats->totalEvents() == 0 ? 0.0 : s.pressure;
    const QDateTime now = QDateTime::currentDateTime();
    const QString version = QCoreApplication::applicationVersion();

    QStringList lines;
    auto add = [&lines](const QString& key, const QString& value) { lines << key + QStringLiteral(": ") + value; };

    lines << QStringLiteral("Doodlebold Tablet Diagnostics");
    add(QStringLiteral("Doodlebold version"), version.isEmpty() ? QStringLiteral("unknown") : version);
    add(QStringLiteral("Date"), now.toOffsetFromUtc(now.offsetFromUtc()).toString(Qt::ISODate));

    lines << QString() << QStringLiteral("[Device]");
    add(QStringLiteral("Proximity"), m_stats->inProximity() ? QStringLiteral("In range") : QStringLiteral("Out of range"));
    add(QStringLiteral("Name"), orDash(d.name));
    add(QStringLiteral("Device type"), orDash(d.type));
    add(QStringLiteral("Pointer type"), orDash(d.pointerType));
    add(QStringLiteral("Capabilities"), orDash(d.capabilities));
    add(QStringLiteral("Unique ID"), d.uniqueId < 0 ? QStringLiteral("-") : QString::number(d.uniqueId));
    add(QStringLiteral("Button count"), d.name.isEmpty() ? QStringLiteral("-") : QString::number(d.buttonCount));

    lines << QString() << QStringLiteral("[Live values]");
    add(QStringLiteral("Pressure"), number(pressure, 4));
    add(QStringLiteral("Peak pressure"), number(m_stats->peakPressure(), 4));
    add(QStringLiteral("Tilt X (deg)"), number(s.tiltX, 1));
    add(QStringLiteral("Tilt Y (deg)"), number(s.tiltY, 1));
    add(QStringLiteral("Rotation (deg)"), number(s.rotation, 1));
    add(QStringLiteral("Tangential pressure"), number(s.tangentialPressure, 4));
    add(QStringLiteral("Position (doc)"), number(s.pos.x(), 2) + QStringLiteral(", ") + number(s.pos.y(), 2));
    add(QStringLiteral("Buttons"), buttonsText(s.buttons));
    add(QStringLiteral("Pointer kind"), kindName(s.kind));
    add(QStringLiteral("Event rate (Hz)"), number(eventRateHz(), 1));
    add(QStringLiteral("Median interval (ms)"), number(medianIntervalMs(), 1));
    add(QStringLiteral("Total events"), QString::number(m_stats->totalEvents()));

    lines << QString() << QStringLiteral("[System]");
    add(QStringLiteral("Session"), platform::sessionDescription());
    add(QStringLiteral("Qt version"), QString::fromLatin1(qVersion()));
    add(QStringLiteral("Theme"), Theme::current().name);
    add(QStringLiteral("Pen cursor"), penCursorText());
    add(QStringLiteral("Pad buttons (ExpressKeys)"),
        platform::padButtonsSupported() ? QStringLiteral("available") : QStringLiteral("not available to applications on this platform"));
    lines << QStringLiteral("Input devices:");
    const QStringList devices = platform::inputDeviceReport();
    for (const QString& line : devices)
        lines << QStringLiteral("  ") + line;
    if (devices.isEmpty())
        lines << QStringLiteral("  (none)");

    return lines.join(QLatin1Char('\n')) + QLatin1Char('\n');
}

}
