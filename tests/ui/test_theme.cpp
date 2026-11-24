#include "ui/DoodleboldStyle.h"
#include "ui/tools/CanvasCursor.h"
#include "ui/Theme.h"
#include "ScreenshotSupport.h"

#include <QCursor>
#include <QDir>
#include <QMainWindow>
#include <QMdiArea>
#include <QPainter>
#include <QMenuBar>
#include <QMdiSubWindow>
#include <QStyleFactory>
#include <QStyleOption>
#include <QFile>
#include <QRegularExpression>
#include <QTemporaryDir>

#include <cmath>

#include <gtest/gtest.h>

using namespace doodlebold;
using Theme::Token;

namespace {

void write(const QString& path, const char* text) {
    QDir().mkpath(QFileInfo(path).path());
    QFile f(path);
    ASSERT_TRUE(f.open(QIODevice::WriteOnly | QIODevice::Text));
    f.write(text);
}

void writeCosmicTheme(const QString& root) {
    const QString dir = root + QStringLiteral("/com.system76.CosmicTheme.Dark/v2/");
    write(dir + QStringLiteral("background"), R"((
    base: "#2B2E34FF",
    component: (
        base: "#40434AFF",
        hover: "#53565CFF",
        pressed: "#66696EFF",
        on_disabled: "#FFFFFFA6",
    ),
    divider: "#55585DFF",
    on: "#FFFFFFFF",
))");
    write(dir + QStringLiteral("primary"), R"((
    base: "#383B42FF",
    component: (
        base: "#484C52FF",
    ),
    divider: "#606368FF",
    on: "#FFFFFFFF",
))");
    write(dir + QStringLiteral("accent"), R"((
    base: "#E79CFEFF",
    hover: "#CD91DFFF",
    on: "#000000FF",
))");
    write(dir + QStringLiteral("destructive"), R"((
    base: "#FD8CA4FF",
))");
    write(dir + QStringLiteral("corner_radii"), R"((
    radius_0: (0.0, 0.0, 0.0, 0.0),
    radius_xs: (4.0, 4.0, 4.0, 4.0),
    radius_s: (8.0, 8.0, 8.0, 8.0),
))");
}

}

TEST(Theme, ReadsTheCosmicDarkTheme) {
    QTemporaryDir root;
    ASSERT_TRUE(root.isValid());
    writeCosmicTheme(root.path());
    const auto v = Theme::cosmic(root.path());
    ASSERT_TRUE(v);
    const auto c = [&](Token t) { return v->colors[static_cast<int>(t)]; };
    EXPECT_EQ(c(Token::Window), QColor(0x2b, 0x2e, 0x34));
    EXPECT_EQ(c(Token::Surface), QColor(0x38, 0x3b, 0x42));
    EXPECT_EQ(c(Token::Border), QColor(0x55, 0x58, 0x5d));
    EXPECT_EQ(c(Token::Accent), QColor(0xe7, 0x9c, 0xfe));
    EXPECT_EQ(c(Token::AccentHover), QColor(0xcd, 0x91, 0xdf));
    EXPECT_EQ(c(Token::AccentText), QColor(Qt::black)) << "text on a light accent is dark, as COSMIC says";
    EXPECT_EQ(c(Token::Danger), QColor(0xfd, 0x8c, 0xa4));
    EXPECT_EQ(c(Token::Hover), QColor(0x53, 0x56, 0x5c));
    EXPECT_LT(c(Token::Base).lightness(), c(Token::Window).lightness()) << "wells sit below the window";
    EXPECT_EQ(v->radius, Theme::builtIn().radius) << "COSMIC's radii are not taken: Doodlebold's own everywhere";
    EXPECT_EQ(v->radiusSmall, Theme::builtIn().radiusSmall);
    EXPECT_EQ(c(Token::Selected), QColor(0xe7, 0x9c, 0xfe)) << "COSMIC marks the current item with its accent";
    EXPECT_EQ(c(Token::SelectedText), QColor(Qt::black));
    EXPECT_EQ(v->name, QStringLiteral("COSMIC (dark)"));
}

TEST(Theme, NoCosmicThemeMeansNone) {
    QTemporaryDir root;
    EXPECT_FALSE(Theme::cosmic(root.path()));
    write(root.path() + QStringLiteral("/com.system76.CosmicTheme.Dark/v2/background"), "( base: \"nonsense\", )");
    EXPECT_FALSE(Theme::cosmic(root.path())) << "no usable colours";
}

TEST(Theme, BuiltInIsNeutralWithABlueCurrentItem) {
    const Theme::Values v = Theme::builtIn();
    const auto c = [&](Token t) { return v.colors[static_cast<int>(t)]; };
    for (Token t : {Token::Window, Token::Surface, Token::Base, Token::Border, Token::Fill})
        EXPECT_LE(c(t).hslSaturation(), 40) << "near-grey: " << static_cast<int>(t);
    EXPECT_GT(c(Token::Surface).lightness(), c(Token::Window).lightness()) << "cards stand out from the workspace";
    EXPECT_GT(c(Token::Base).lightness(), c(Token::Surface).lightness()) << "fields stand out in a card";
    EXPECT_GT(c(Token::Fill).lightness(), c(Token::Base).lightness()) << "a value bar's filled part shows";
    EXPECT_EQ(c(Token::Selected), c(Token::Accent));
    EXPECT_GT(c(Token::Accent).hslSaturation(), 150);
    const auto luminance = [](const QColor& col) {
        const auto lin = [](double u) { return u <= 0.04045 ? u / 12.92 : std::pow((u + 0.055) / 1.055, 2.4); };
        return 0.2126 * lin(col.redF()) + 0.7152 * lin(col.greenF()) + 0.0722 * lin(col.blueF());
    };
    const double ratio = (luminance(c(Token::SelectedText)) + 0.05) / (luminance(c(Token::Selected)) + 0.05);
    EXPECT_GE(ratio, 4.5);
    EXPECT_GT(v.cardRadius, v.radius);
}

TEST(Theme, StyleSheetPlaceholders) {
    Theme::Values v = Theme::builtIn();
    v.colors[static_cast<int>(Token::Accent)] = QColor(1, 2, 3);
    v.radius = 9;
    v.radiusSmall = 5;
    EXPECT_EQ(Theme::styleSheet(QStringLiteral("a { color: @Accent; background: @Accent~90; border-radius: @radius; }"), v),
              QStringLiteral("a { color: #010203; background: rgba(1, 2, 3, 90); border-radius: 9px; }"));
    EXPECT_EQ(Theme::styleSheet(QStringLiteral("b { x: @radiusSmall; } /* @unknown */"), v),
              QStringLiteral("b { x: 5px; } /* @unknown */"));
    EXPECT_EQ(
        Theme::styleSheet(QStringLiteral("c { padding: @space1 @space3; border-radius: @radiusLg; min-height: @compactInnerMd; }"), v),
        QStringLiteral("c { padding: 4px 12px; border-radius: 14px; min-height: 22px; }"));
    EXPECT_TRUE(Theme::styleSheet(QStringLiteral("@captionSize"), v).endsWith(QStringLiteral("pt")));
}

// The design system's contrast promises (WCAG 2.1): text roles 4.5:1 on the grounds they are allowed on, the focus ring 3:1.
TEST(Theme, BuiltInRolesKeepTheirContrast) {
    const Theme::Values v = Theme::builtIn();
    const auto c = [&](Token t) { return v.colors[static_cast<int>(t)]; };
    const auto luminance = [](const QColor& col) {
        const auto lin = [](double u) { return u <= 0.04045 ? u / 12.92 : std::pow((u + 0.055) / 1.055, 2.4); };
        return 0.2126 * lin(col.redF()) + 0.7152 * lin(col.greenF()) + 0.0722 * lin(col.blueF());
    };
    const auto ratio = [&](Token a, Token b) {
        const double la = luminance(c(a)), lb = luminance(c(b));
        return (std::max(la, lb) + 0.05) / (std::min(la, lb) + 0.05);
    };
    for (Token ground : {Token::Window, Token::Surface, Token::AlternateBase, Token::Base}) {
        EXPECT_GE(ratio(Token::TextMuted, ground), 4.5) << static_cast<int>(ground);
        EXPECT_GE(ratio(Token::TextAccent, ground), 4.5) << static_cast<int>(ground);
        EXPECT_GE(ratio(Token::FocusRing, ground), 3.0) << static_cast<int>(ground);
    }
    for (Token fill : {Token::Accent, Token::AccentHover, Token::AccentPressed, Token::Selected})
        EXPECT_GE(ratio(Token::AccentText, fill), 4.5) << static_cast<int>(fill);
    EXPECT_GE(ratio(Token::Hover, Token::Base), 1.1) << "a hovered control can be told from a resting one";
    EXPECT_GT(c(Token::Pressed).lightness(), c(Token::Hover).lightness());
}

TEST(Theme, ShippedStyleSheetHasNoUnfilledPlaceholder) {
    QFile f(QStringLiteral(":/theme/doodlebold-dark.qss"));
    ASSERT_TRUE(f.open(QIODevice::ReadOnly | QIODevice::Text));
    QString sheet = Theme::styleSheet(QString::fromUtf8(f.readAll()), Theme::builtIn());
    sheet.remove(QRegularExpression(QStringLiteral(R"(/\*.*?\*/)"), QRegularExpression::DotMatchesEverythingOption));
    EXPECT_FALSE(sheet.contains(QLatin1Char('@'))) << qPrintable(sheet.mid(sheet.indexOf(QLatin1Char('@')), 40));
    EXPECT_TRUE(sheet.contains(Theme::builtIn().colors[static_cast<int>(Token::Selected)].name())) << "the built-in current-item pill";
}

TEST(DoodleboldStyle, TitleBarButtonsAndHitTesting) {
    DoodleboldStyle style(QStyleFactory::create(QStringLiteral("Fusion")));
    QStyleOptionTitleBar bar;
    bar.rect = QRect(0, 0, 400, DoodleboldStyle::kTitleBarHeight);
    bar.titleBarFlags = Qt::SubWindow | Qt::WindowSystemMenuHint | Qt::WindowMinMaxButtonsHint;
    bar.subControls = QStyle::SC_All;
    const QRect close = style.subControlRect(QStyle::CC_TitleBar, &bar, QStyle::SC_TitleBarCloseButton);
    const QRect max = style.subControlRect(QStyle::CC_TitleBar, &bar, QStyle::SC_TitleBarMaxButton);
    const QRect min = style.subControlRect(QStyle::CC_TitleBar, &bar, QStyle::SC_TitleBarMinButton);
    EXPECT_EQ(close.size(), QSize(DoodleboldStyle::kButtonSize, DoodleboldStyle::kButtonSize));
    EXPECT_GT(close.left(), max.right());
    EXPECT_GT(max.left(), min.right());
    EXPECT_TRUE(style.subControlRect(QStyle::CC_TitleBar, &bar, QStyle::SC_TitleBarSysMenu).isEmpty());
    EXPECT_TRUE(style.subControlRect(QStyle::CC_TitleBar, &bar, QStyle::SC_TitleBarNormalButton).isEmpty());
    const QRect label = style.subControlRect(QStyle::CC_TitleBar, &bar, QStyle::SC_TitleBarLabel);
    EXPECT_EQ(label.center().x(), bar.rect.center().x()) << "the title is centred";
    EXPECT_LT(label.right(), min.left());
    for (const auto& [rect, sub] : {std::pair{close, QStyle::SC_TitleBarCloseButton}, std::pair{max, QStyle::SC_TitleBarMaxButton},
                                    std::pair{min, QStyle::SC_TitleBarMinButton}})
        EXPECT_EQ(style.hitTestComplexControl(QStyle::CC_TitleBar, &bar, rect.center()), sub);

    bar.titleBarState = Qt::WindowMaximized;
    EXPECT_EQ(style.subControlRect(QStyle::CC_TitleBar, &bar, QStyle::SC_TitleBarNormalButton), max) << "Restore takes Maximize's place";
    EXPECT_EQ(style.pixelMetric(QStyle::PM_TitleBarHeight), DoodleboldStyle::kTitleBarHeight);
}

TEST(DoodleboldStyle, SavesAScreenshotOfCanvasWindows) {
    test::applyTheme();
    QMdiArea area;
    area.resize(700, 420);
    area.setBackground(Theme::color(Token::Base));
    for (const QString& title : {QStringLiteral("NewCanvas1 (35.7%)"), QStringLiteral("NewCanvas2 (35.7%)")}) {
        auto* canvas = new QWidget;
        canvas->setAutoFillBackground(true);
        canvas->setPalette(QPalette(Qt::white));
        QMdiSubWindow* w = area.addSubWindow(canvas);
        w->setWindowTitle(title);
        w->resize(420, 300);
        const int n = int(area.subWindowList().size());
        w->move(n * 40, n * 30);
        w->show();
    }
    area.show();
    QCoreApplication::processEvents();
    EXPECT_TRUE(test::saveScreenshot(area.grab(), QStringLiteral("canvas_windows")));
}

TEST(DoodleboldStyle, SavesAScreenshotOfAMaximizedCanvas) {
    test::applyTheme();
    QMainWindow window;
    window.resize(700, 300);
    window.menuBar()->addMenu(QStringLiteral("File"));
    window.menuBar()->addMenu(QStringLiteral("Edit"));
    auto* area = new QMdiArea;
    area->setBackground(Theme::color(Token::Base));
    window.setCentralWidget(area);
    auto* canvas = new QWidget;
    canvas->setAutoFillBackground(true);
    canvas->setPalette(QPalette(Qt::white));
    QMdiSubWindow* w = area->addSubWindow(canvas);
    w->setWindowTitle(QStringLiteral("NewCanvas1 (35.7%)"));
    window.show();
    w->showMaximized();
    QCoreApplication::processEvents();
    EXPECT_TRUE(test::saveScreenshot(window.grab(), QStringLiteral("canvas_maximized")));
}

TEST(DoodleboldStyle, DisabledIconsFade) {
    DoodleboldStyle style(QStyleFactory::create(QStringLiteral("Fusion")));
    QPixmap white(8, 8);
    white.fill(Qt::white);
    const QImage faded = style.generatedIconPixmap(QIcon::Disabled, white, nullptr).toImage();
    EXPECT_NEAR(qAlpha(faded.pixel(4, 4)), int(255 * DoodleboldStyle::kDisabledIconOpacity), 2);
    EXPECT_EQ(style.generatedIconPixmap(QIcon::Normal, white, nullptr).toImage().pixel(4, 4), white.toImage().pixel(4, 4));
}

TEST(CanvasCursor, RotateCursorIsACurvedDoubleArrow) {
    const QCursor cursor = canvasCursor(CanvasGlyph::Rotate);
    const QPixmap pixmap = cursor.pixmap();
    ASSERT_FALSE(pixmap.isNull());
    EXPECT_EQ(cursor.hotSpot(), QPoint(16, 16));
    const QImage image = pixmap.toImage();
    int ink = 0;
    for (int y = 0; y < image.height(); ++y)
        for (int x = 0; x < image.width(); ++x)
            ink += qAlpha(image.pixel(x, y)) > 0;
    EXPECT_GT(ink, 100);
    EXPECT_EQ(qAlpha(image.pixel(32, 40)), 0) << "the curve's middle is open below";
    EXPECT_TRUE(test::saveScreenshot(QPixmap::fromImage(image.scaled(128, 128)), QStringLiteral("rotate_cursor")));
}

TEST(DoodleboldStyle, SelectBoxesAreFlat) {
    test::applyTheme();
    DoodleboldStyle style(QStyleFactory::create(QStringLiteral("Fusion")));
    QStyleOptionComboBox box;
    box.rect = QRect(0, 0, 120, 24);
    box.state = QStyle::State_Enabled;
    box.subControls = QStyle::SC_All;
    QImage img(box.rect.size(), QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::transparent);
    {
        QPainter p(&img);
        style.drawComplexControl(QStyle::CC_ComboBox, &box, &p, nullptr);
    }
    const QColor well = Theme::color(Token::Base);
    for (int y : {5, 12, 18})
        EXPECT_EQ(img.pixelColor(40, y), well) << "one colour from top to bottom, y " << y;
}

TEST(DoodleboldStyle, TheActiveCanvasHasNoColouredOutline) {
    test::applyTheme();
    DoodleboldStyle style(QStyleFactory::create(QStringLiteral("Fusion")));
    QStyleOptionTitleBar bar;
    bar.rect = QRect(0, 0, 300, DoodleboldStyle::kTitleBarHeight);
    bar.state = QStyle::State_Enabled | QStyle::State_Active;
    bar.titleBarFlags = Qt::SubWindow | Qt::WindowSystemMenuHint;
    bar.subControls = QStyle::SC_All;
    QImage img(bar.rect.size(), QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::transparent);
    {
        QPainter p(&img);
        style.drawComplexControl(QStyle::CC_TitleBar, &bar, &p, nullptr);
    }
    const int m = DoodleboldStyle::kFrameMargin;
    EXPECT_EQ(img.pixelColor(150, m), Theme::color(Token::Border));
    EXPECT_EQ(img.pixelColor(m, 25), Theme::color(Token::Border));
    EXPECT_EQ(img.pixelColor(150, 0).alpha(), 0) << "the margin where the pointer resizes shows what is behind";
    EXPECT_EQ(img.pixelColor(150, 20), Theme::color(Token::Surface)) << "the header is the top of a card";
    EXPECT_EQ(img.pixelColor(m + 1, m + 1).alpha(), 0) << "its corner is rounded";
}
