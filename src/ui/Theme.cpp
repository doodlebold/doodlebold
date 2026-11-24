#include "ui/Theme.h"

#include "ui/DoodleboldStyle.h"
#include "ui/HoverWheelGuard.h"
#include "ui/GlassPopups.h"

#include <QWidget>
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFont>
#include <QFontDatabase>
#include <QFontMetrics>
#include <QHash>
#include <QFormLayout>
#include <QGridLayout>
#include <QLayout>
#include <QPalette>
#include <QRegularExpression>
#include <QStyleFactory>

#include <array>

namespace doodlebold::Theme {

namespace {

QColor mix(const QColor& from, const QColor& to, int percent) {
    const float amount = static_cast<float>(percent) / 100.0f;
    return QColor::fromRgbF(from.redF() + (to.redF() - from.redF()) * amount, from.greenF() + (to.greenF() - from.greenF()) * amount,
                            from.blueF() + (to.blueF() - from.blueF()) * amount);
}

QColor over(const QColor& top, const QColor& below) {
    return mix(below, QColor(top.red(), top.green(), top.blue()), qRound(top.alphaF() * 100));
}

Values& active() {
    static Values values = builtIn();
    return values;
}

constexpr int kRoleCount = static_cast<int>(TextRole::Mono) + 1;
// Below this the app font is raised to it: Inter at 8 pt and less was hard to read in the panels.
constexpr double kLeastAppPointSize = 9.0;

// The type roles' fonts, made from `base` (the app font).
std::array<QFont, kRoleCount> makeFonts(const QFont& base) {
    std::array<QFont, kRoleCount> f;
    const double size = base.pointSizeF() > 0 ? base.pointSizeF() : kLeastAppPointSize;
    const auto sized = [&](double delta, QFont::Weight weight) {
        QFont font = base;
        font.setPointSizeF(size + delta);
        font.setWeight(weight);
        return font;
    };
    f[int(TextRole::Caption)] = sized(kCaptionDelta, QFont::Normal);
    f[int(TextRole::Overline)] = sized(kCaptionDelta, QFont::DemiBold);
    f[int(TextRole::Overline)].setCapitalization(QFont::AllUppercase);
    f[int(TextRole::Overline)].setLetterSpacing(QFont::AbsoluteSpacing, kOverlineSpacing);
    f[int(TextRole::Body)] = sized(0, QFont::Normal);
    f[int(TextRole::Label)] = sized(0, QFont::Medium);
    f[int(TextRole::BodyStrong)] = sized(0, QFont::DemiBold);
    f[int(TextRole::Title)] = sized(kTitleDelta, QFont::DemiBold);
    f[int(TextRole::Heading)] = sized(kHeadingDelta, QFont::DemiBold);
    f[int(TextRole::Mono)] = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    f[int(TextRole::Mono)].setPointSizeF(size);
    return f;
}

std::optional<std::array<QFont, kRoleCount>>& fonts() {
    static std::optional<std::array<QFont, kRoleCount>> made;
    return made;
}

// The size tokens a style sheet can name, px.
const QHash<QString, int>& sizePlaceholders() {
    static const QHash<QString, int> sizes = {
        {QStringLiteral("spaceHair"), kSpaceHair},
        {QStringLiteral("space1"), kSpace1},
        {QStringLiteral("space2"), kSpace2},
        {QStringLiteral("space3"), kSpace3},
        {QStringLiteral("space4"), kSpace4},
        {QStringLiteral("space5"), kSpace5},
        {QStringLiteral("radiusXs"), kRadiusXs},
        {QStringLiteral("radiusSm"), kRadiusSm},
        {QStringLiteral("radiusMd"), kRadiusMd},
        {QStringLiteral("radiusLg"), kRadiusLg},
        {QStringLiteral("border"), kBorder},
        {QStringLiteral("borderStrong"), kBorderStrong},
        {QStringLiteral("compactSm"), compact::kControlSm},
        // A QSS min-height is the content's: the control's height less its two 1 px borders.
        {QStringLiteral("compactInnerMd"), compact::kControlMd - 2 * kBorder},
        {QStringLiteral("comfortableInnerMd"), comfortable::kControlMd - 2 * kBorder},
    };
    return sizes;
}

QPalette buildPalette() {
    const QColor window = color(Token::Window);
    const QColor base = color(Token::Base);
    const QColor surface = color(Token::Surface);
    const QColor text = color(Token::Text);
    const QColor muted = color(Token::TextMuted);
    const QColor accent = color(Token::Accent);

    QPalette p;
    for (QPalette::ColorGroup group : {QPalette::Active, QPalette::Inactive, QPalette::Disabled}) {
        p.setColor(group, QPalette::Window, window);
        p.setColor(group, QPalette::WindowText, text);
        p.setColor(group, QPalette::Base, base);
        p.setColor(group, QPalette::AlternateBase, color(Token::AlternateBase));
        p.setColor(group, QPalette::ToolTipBase, surface);
        p.setColor(group, QPalette::ToolTipText, text);
        p.setColor(group, QPalette::PlaceholderText, muted);
        p.setColor(group, QPalette::Text, text);
        p.setColor(group, QPalette::Button, surface);
        p.setColor(group, QPalette::ButtonText, text);
        p.setColor(group, QPalette::BrightText, color(Token::Danger));
        p.setColor(group, QPalette::Light, mix(surface, Qt::white, 15));
        p.setColor(group, QPalette::Midlight, mix(surface, Qt::white, 7));
        p.setColor(group, QPalette::Mid, color(Token::Border));
        p.setColor(group, QPalette::Dark, mix(window, Qt::black, 35));
        p.setColor(group, QPalette::Shadow, Qt::black);
        p.setColor(group, QPalette::Highlight, color(Token::Selected));
        p.setColor(group, QPalette::HighlightedText, color(Token::SelectedText));
        p.setColor(group, QPalette::Link, accent);
        p.setColor(group, QPalette::LinkVisited, mix(accent, color(Token::Danger), 35));
    }

    const QColor dimmed = color(Token::TextDisabled);
    for (QPalette::ColorRole role : {QPalette::WindowText, QPalette::Text, QPalette::ButtonText})
        p.setColor(QPalette::Disabled, role, dimmed);
    p.setColor(QPalette::Disabled, QPalette::PlaceholderText, dimmed);
    p.setColor(QPalette::Disabled, QPalette::Button, mix(window, surface, 60));
    p.setColor(QPalette::Disabled, QPalette::Highlight, mix(window, accent, 45));
    p.setColor(QPalette::Disabled, QPalette::HighlightedText, dimmed);
    return p;
}

// The entries of one COSMIC theme file (RON) by dotted path: "base", "component.hover", "radius_s", … Only what the theme
// files use: nested structs `name: ( … )`, strings, numbers and number tuples (kept as their text).
QHash<QString, QString> parseRon(const QString& text) {
    static const QRegularExpression open(QStringLiteral(R"(^\s*(\w+)\s*:\s*\(\s*$)"));
    static const QRegularExpression close(QStringLiteral(R"(^\s*\)\s*,?\s*$)"));
    static const QRegularExpression value(QStringLiteral(R"(^\s*(\w+)\s*:\s*(.+?)\s*,?\s*$)"));
    QHash<QString, QString> out;
    QStringList path;
    for (const QString& line : text.split(QLatin1Char('\n'))) {
        if (const auto m = open.match(line); m.hasMatch()) {
            path << m.captured(1);
        } else if (close.match(line).hasMatch()) {
            if (!path.isEmpty())
                path.removeLast();
        } else if (const auto v = value.match(line); v.hasMatch()) {
            QString s = v.captured(2);
            if (s.startsWith(QLatin1Char('"')) && s.endsWith(QLatin1Char('"')))
                s = s.mid(1, s.size() - 2);
            out.insert((path + QStringList{v.captured(1)}).join(QLatin1Char('.')), s);
        }
    }
    return out;
}

std::optional<QColor> parseColour(const QString& s) {
    if (!s.startsWith(QLatin1Char('#')) || (s.size() != 9 && s.size() != 7))
        return std::nullopt;
    bool ok = false;
    const uint v = s.mid(1).toUInt(&ok, 16);
    if (!ok)
        return std::nullopt;
    return s.size() == 9 ? QColor(int(v >> 24), int(v >> 16 & 0xFF), int(v >> 8 & 0xFF), int(v & 0xFF))
                         : QColor(int(v >> 16), int(v >> 8 & 0xFF), int(v & 0xFF));
}

}

QColor color(Token token) {
    return active().colors[static_cast<int>(token)];
}

int metric(Metric m) {
    const Values& v = active();
    switch (m) {
    case Metric::Radius: return v.radius;
    case Metric::RadiusSmall: return v.radiusSmall;
    case Metric::Gap: return v.gap;
    case Metric::Padding: return v.padding;
    case Metric::CardRadius: return v.cardRadius;
    case Metric::CardGap: return v.cardGap;
    case Metric::DialogPadding: return v.dialogPadding;
    case Metric::FieldSpacing: return v.fieldSpacing;
    case Metric::FieldGap: return v.fieldGap;
    }
    return 0;
}

void padDialog(QLayout* layout, bool resizable) {
    const int pad = metric(Metric::DialogPadding);
    layout->setContentsMargins(pad, pad, pad, pad);
    layout->setSpacing(pad);
    if (!resizable) {
        layout->setSizeConstraint(QLayout::SetFixedSize);
        // Without it Windows keeps the resizable frame, and its resize cursors at the corners, on a dialog that cannot resize.
        if (QWidget* window = layout->parentWidget(); window && window->isWindow())
            window->setWindowFlag(Qt::MSWindowsFixedSizeDialogHint);
    }
}

void spaceFields(QGridLayout* grid) {
    grid->setHorizontalSpacing(metric(Metric::FieldGap));
    grid->setVerticalSpacing(metric(Metric::FieldSpacing));
}

void spaceFields(QFormLayout* form) {
    form->setHorizontalSpacing(metric(Metric::FieldGap));
    form->setVerticalSpacing(metric(Metric::FieldSpacing));
}

const Values& current() {
    return active();
}

const QFont& font(TextRole role) {
    if (!fonts())
        fonts() = makeFonts(QGuiApplication::font());
    return (*fonts())[static_cast<int>(role)];
}

Values builtIn() {
    Values v;
    const auto set = [&v](Token t, QColor c) { v.colors[static_cast<int>(t)] = c; };
    set(Token::Window, QColor(0x16, 0x16, 0x18));
    set(Token::Base, QColor(0x2a, 0x2a, 0x2e));
    set(Token::AlternateBase, QColor(0x1b, 0x1b, 0x1e));
    set(Token::Surface, QColor(0x1f, 0x1f, 0x23));
    set(Token::Border, QColor(0x2e, 0x2e, 0x33));
    set(Token::BorderStrong, QColor(0x45, 0x45, 0x4b));
    set(Token::BorderDisabled, QColor(0x26, 0x26, 0x2a));
    set(Token::Text, QColor(0xf2, 0xf2, 0xf4));
    set(Token::TextMuted, QColor(0x9b, 0x9b, 0xa5));
    set(Token::TextDisabled, QColor(0x5c, 0x5c, 0x64));
    set(Token::Accent, QColor(0x25, 0x63, 0xeb));
    set(Token::AccentText, QColor(0xff, 0xff, 0xff));
    set(Token::Danger, QColor(0xf0, 0x73, 0x62));
    // Hover and Pressed one step apart and both clear of Base (1.12:1 and 1.22:1): #2c2c31 was 1.03:1 on Base, so a hovered
    // tool cell looked the same as the others.
    set(Token::Hover, QColor(0x32, 0x32, 0x37));
    set(Token::Pressed, QColor(0x3a, 0x3a, 0x40));
    set(Token::Selected, QColor(0x25, 0x63, 0xeb));
    set(Token::SelectedText, QColor(0xff, 0xff, 0xff));
    set(Token::Fill, QColor(0x4a, 0x4a, 0x50));
    set(Token::AntDark, QColor(0, 0, 0));
    set(Token::AntLight, QColor(255, 255, 255));
    set(Token::SelectionTint, QColor(0, 0, 255, 128));
    // White text reads 4.70:1 on this hover (4.25 on #3b74f0 before) and 6.25 on the pressed accent.
    set(Token::AccentHover, QColor(0x2f, 0x6b, 0xef));
    set(Token::AccentPressed, QColor(0x1e, 0x56, 0xd6));
    // The accent lightened until it reads as text: 6.6:1 on Window, 5.2 on Base. The focus ring uses it for the same reason
    // (Accent at the old 150 alpha was 1.8:1 on a field).
    set(Token::TextAccent, QColor(0x6b, 0x9b, 0xf7));
    set(Token::FocusRing, QColor(0x6b, 0x9b, 0xf7));
    set(Token::Success, QColor(0x3c, 0xc8, 0x78));  // the memory meter's green, 8.4:1 on Window
    set(Token::Warning, QColor(0xe8, 0xb1, 0x4a));
    set(Token::Shadow, QColor(0, 0, 0, 90));  // the value bar thumb's shadow, 35 %
    v.name = QStringLiteral("built-in");
    return v;
}

std::optional<Values> cosmic(const QString& configRoot) {
    // A file of the dark theme: the v2 layout first (COSMIC since 2024), then v1.
    const auto read = [&configRoot](const char* entry) -> QHash<QString, QString> {
        for (const char* version : {"v2", "v1"}) {
            QFile f(QDir(configRoot)
                        .filePath(QStringLiteral("com.system76.CosmicTheme.Dark/%1/%2").arg(QLatin1String(version), QLatin1String(entry))));
            if (f.open(QIODevice::ReadOnly | QIODevice::Text))
                return parseRon(QString::fromUtf8(f.readAll()));
        }
        return {};
    };
    const auto background = read("background"), primary = read("primary"), accent = read("accent");
    const auto destructive = read("destructive");
    const auto bg = parseColour(background.value(QStringLiteral("base")));
    const auto accentBase = parseColour(accent.value(QStringLiteral("base")));
    if (!bg || !accentBase)
        return std::nullopt;

    Values v = builtIn();
    const auto set = [&v](Token t, const QColor& c) { v.colors[static_cast<int>(t)] = c; };
    const auto pick = [](const QHash<QString, QString>& file, const char* key, const QColor& fallback) {
        return parseColour(file.value(QLatin1String(key))).value_or(fallback);
    };
    const QColor window = *bg;
    const QColor surface = pick(primary, "base", mix(window, Qt::white, 6));
    const QColor text = pick(background, "on", Qt::white);
    set(Token::Window, window);
    set(Token::Surface, surface);
    set(Token::Base, mix(window, Qt::black, 18));
    set(Token::AlternateBase, mix(window, Qt::black, 9));
    set(Token::Border, pick(background, "divider", mix(window, Qt::white, 18)));
    set(Token::BorderStrong, pick(primary, "divider", mix(window, Qt::white, 25)));
    set(Token::BorderDisabled, mix(window, v.colors[static_cast<int>(Token::Border)], 50));
    set(Token::Text, text);
    set(Token::TextMuted, over(pick(background, "component.on_disabled", QColor(255, 255, 255, 166)), window));
    set(Token::TextDisabled, mix(window, v.colors[static_cast<int>(Token::TextMuted)], 55));
    set(Token::Accent, *accentBase);
    set(Token::AccentHover, pick(accent, "hover", mix(*accentBase, Qt::white, 15)));
    set(Token::AccentText, pick(accent, "on", Qt::white));
    set(Token::Danger, pick(destructive, "base", QColor(0xe0, 0x6c, 0x5f)));
    set(Token::Hover, pick(background, "component.hover", mix(surface, Qt::white, 8)));
    set(Token::Pressed, pick(background, "component.pressed", mix(surface, Qt::white, 15)));
    set(Token::Selected, *accentBase);
    set(Token::SelectedText, v.colors[static_cast<int>(Token::AccentText)]);
    set(Token::Fill, mix(v.colors[static_cast<int>(Token::Base)], *accentBase, 43));  // the accent, softened
    // The roles COSMIC has no entry for, made from its accent the way the built-in ones are made from the built-in accent.
    // Percent of black or white mixed into COSMIC's accent: about the steps from the built-in accent to its pressed and text
    // colours (#2563eb to #1e56d6 and #6b9bf7).
    constexpr int kPressedDarken = 15;
    constexpr int kTextLighten = 35;
    set(Token::AccentPressed, mix(*accentBase, Qt::black, kPressedDarken));
    set(Token::TextAccent, mix(*accentBase, Qt::white, kTextLighten));
    set(Token::FocusRing, v.colors[static_cast<int>(Token::TextAccent)]);
    const auto success = read("success"), warning = read("warning");
    set(Token::Success, pick(success, "base", v.colors[static_cast<int>(Token::Success)]));
    set(Token::Warning, pick(warning, "base", v.colors[static_cast<int>(Token::Warning)]));
    for (Token t : {Token::Window, Token::Surface, Token::Border, Token::BorderStrong, Token::Text, Token::Accent, Token::AccentHover,
                    Token::AccentText, Token::Danger, Token::Hover, Token::Pressed, Token::Selected, Token::SelectedText,
                    Token::AccentPressed, Token::TextAccent, Token::FocusRing, Token::Success, Token::Warning})
        v.colors[static_cast<int>(t)].setAlpha(255);  // the style sheet and the panels draw opaque
    // Corners and spacing stay Doodlebold's own, not COSMIC's.
    v.name = QStringLiteral("COSMIC (dark)");
    return v;
}

QString styleSheet(const QString& source, const Values& values) {
    static const QHash<QString, Token> tokens = [] {
        QHash<QString, Token> t;
        const char* names[kTokenCount] = {"Window",         "Base",    "AlternateBase", "Surface",       "Border",     "BorderStrong",
                                          "BorderDisabled", "Text",    "TextMuted",     "TextDisabled",  "Accent",     "AccentHover",
                                          "AccentText",     "Danger",  "Hover",         "Pressed",       "Selected",   "SelectedText",
                                          "Fill",           "AntDark", "AntLight",      "SelectionTint", "TextAccent", "AccentPressed",
                                          "FocusRing",      "Success", "Warning",       "Shadow"};
        for (int i = 0; i < kTokenCount; ++i)
            t.insert(QLatin1String(names[i]), static_cast<Token>(i));
        return t;
    }();
    static const QRegularExpression placeholder(QStringLiteral(R"(@(\w+)(?:~(\d+))?)"));
    QString out;
    out.reserve(source.size());
    qsizetype last = 0;
    for (auto it = placeholder.globalMatch(source); it.hasNext();) {
        const auto m = it.next();
        out += source.mid(last, m.capturedStart() - last);
        last = m.capturedEnd();
        const QString name = m.captured(1);
        if (name == QLatin1String("radius")) {
            out += QStringLiteral("%1px").arg(values.radius);
        } else if (name == QLatin1String("radiusSmall")) {
            out += QStringLiteral("%1px").arg(values.radiusSmall);
        } else if (const auto s = sizePlaceholders().find(name); s != sizePlaceholders().end()) {
            out += QStringLiteral("%1px").arg(*s);
        } else if (name == QLatin1String("captionSize") || name == QLatin1String("titleSize")) {
            const TextRole role = name == QLatin1String("captionSize") ? TextRole::Caption : TextRole::Title;
            out += QStringLiteral("%1pt").arg(font(role).pointSizeF());
        } else if (name == QLatin1String("menuItemPadTop") || name == QLatin1String("menuItemPadBottom")) {
            // A menu-bar item's text, padded to a compact control's height: what's left over goes below.
            const int spare = std::max(0, compact::kControlMd - QFontMetrics(font(TextRole::Body)).height());
            out += QStringLiteral("%1px").arg(name == QLatin1String("menuItemPadTop") ? spare / 2 : spare - spare / 2);
        } else if (name == QLatin1String("overlineSpacing")) {
            out += QStringLiteral("%1px").arg(kOverlineSpacing);
        } else if (const auto t = tokens.find(name); t != tokens.end()) {
            const QColor c = values.colors[static_cast<int>(*t)];
            out += m.captured(2).isEmpty()
                       ? c.name()
                       : QStringLiteral("rgba(%1, %2, %3, %4)").arg(c.red()).arg(c.green()).arg(c.blue()).arg(m.captured(2));
        } else {
            out += m.captured(0);  // not a placeholder (e.g. an @ in a comment)
        }
    }
    out += source.mid(last);
    return out;
}

void apply(QApplication& app, Source source) {
    Values values = builtIn();
    if (source == Source::Auto && qEnvironmentVariable("DOODLEBOLD_THEME") == QLatin1String("cosmic")) {
        QString config = qEnvironmentVariable("XDG_CONFIG_HOME");
        if (config.isEmpty())
            config = QDir::homePath() + QStringLiteral("/.config");
        for (const QString& root : {config + QStringLiteral("/cosmic"), QStringLiteral("/usr/share/cosmic")})
            if (auto c = cosmic(root)) {
                values = *c;
                break;
            }
    }
    active() = values;

    QApplication::setStyle(new DoodleboldStyle(QStyleFactory::create(QStringLiteral("Fusion"))));
    QApplication::setPalette(buildPalette());

    QFont font = QApplication::font();
    // Inter is bundled, so every platform lays the interface out with the same metrics.
    for (const char* weight : {"Regular", "Medium", "SemiBold", "Bold"})
        QFontDatabase::addApplicationFont(QStringLiteral(":/fonts/Inter-%1.ttf").arg(QLatin1String(weight)));
    if (QFontDatabase::hasFamily(QStringLiteral("Inter")))
        font.setFamily(QStringLiteral("Inter"));
    if (font.pointSizeF() < kLeastAppPointSize)
        font.setPointSizeF(kLeastAppPointSize);
    QApplication::setFont(font);
    fonts() = makeFonts(font);

    QFile sheet(QStringLiteral(":/theme/doodlebold-dark.qss"));
    if (sheet.open(QIODevice::ReadOnly | QIODevice::Text))
        app.setStyleSheet(styleSheet(QString::fromUtf8(sheet.readAll()), values));
    GlassPopups::install(app);
    HoverWheelGuard::install(app);
}

}
