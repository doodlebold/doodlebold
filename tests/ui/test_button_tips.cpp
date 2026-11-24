#include "ScreenshotSupport.h"
#include "core/tools/ToolBox.h"
#include "ui/ButtonTips.h"
#include "ui/MainWindow.h"
#include "ui/Theme.h"

#include <QAbstractButton>
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QHelpEvent>
#include <QMenu>
#include <QPushButton>
#include <QRadioButton>
#include <QStandardPaths>
#include <QToolBar>
#include <QToolButton>
#include <QThread>
#include <QToolTip>
#include <QTextDocument>

#include <gtest/gtest.h>

#include <memory>

using namespace doodlebold;

namespace {

QString native(const char* portable) {
    return QKeySequence(QString::fromLatin1(portable)).toString(QKeySequence::NativeText);
}

bool hasKeysLine(const QString& tip) {
    return tip.contains(QStringLiteral("font-size"));
}

QString tipOf(QWidget* w) {
    QToolTip::hideText();
    QHelpEvent help(QEvent::ToolTip, w->rect().center(), w->mapToGlobal(w->rect().center()));
    QCoreApplication::sendEvent(w, &help);
    return w->toolTip();
}

class ButtonTipsTest : public testing::Test {
protected:
    void SetUp() override {
        QStandardPaths::setTestModeEnabled(true);
        test::applyTheme();
        window = std::make_unique<MainWindow>();
        window->resize(1400, 900);
        window->newDocument();
        window->show();
        for (int i = 0; i < 5; ++i) {
            QCoreApplication::processEvents();
            QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
        }
    }
    QAction* command(const char* id) const { return window->command(QString::fromLatin1(id)); }
    template <typename T> T* child(const char* name) const {
        T* w = window->findChild<T*>(QString::fromLatin1(name));
        EXPECT_NE(w, nullptr) << name;
        return w;
    }
    QAbstractButton* quickBarButton(const char* id) const {
        return qobject_cast<QAbstractButton*>(child<QToolBar>("quickBar")->widgetForAction(command(id)));
    }
    std::unique_ptr<MainWindow> window;
};

}

TEST(ButtonTipsHtml, NameThenSmallMutedKeys) {
    test::applyTheme();
    const QString both = button_tips::tipHtml(QStringLiteral("Undo"), QStringLiteral("Ctrl+Z"));
    EXPECT_TRUE(Qt::mightBeRichText(both));
    EXPECT_TRUE(both.contains(QStringLiteral("Undo")));
    EXPECT_TRUE(both.contains(QStringLiteral("Ctrl+Z")));
    EXPECT_LT(both.indexOf(QStringLiteral("Undo")), both.indexOf(QStringLiteral("Ctrl+Z")));
    EXPECT_TRUE(both.contains(Theme::color(Theme::Token::TextMuted).name())) << "the muted colour of the theme";
    EXPECT_TRUE(hasKeysLine(both));
    const QString plain = button_tips::tipHtml(QStringLiteral("New Layer"));
    EXPECT_FALSE(hasKeysLine(plain)) << "no keys, no second line";
    EXPECT_EQ(button_tips::tipHtml(QStringLiteral("A < B & C")).contains(QStringLiteral("A &lt; B &amp; C")), true) << "escaped";
}

TEST_F(ButtonTipsTest, UndoShowsItsShortcutUnderTheName) {
    QAbstractButton* undo = quickBarButton("edit.undo");
    ASSERT_NE(undo, nullptr);
    const QString tip = tipOf(undo);
    EXPECT_TRUE(tip.contains(QStringLiteral("Undo")));
    EXPECT_TRUE(tip.contains(native("Ctrl+Z"))) << tip.toStdString();
    EXPECT_TRUE(hasKeysLine(tip));
    EXPECT_TRUE(QToolTip::isVisible());
}

TEST_F(ButtonTipsTest, ARebindingShowsAtOnce) {
    QAbstractButton* undo = quickBarButton("edit.undo");
    ASSERT_NE(undo, nullptr);
    command("edit.undo")->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+Z")));
    const QString tip = tipOf(undo);
    EXPECT_TRUE(tip.contains(native("Ctrl+Shift+Z"))) << tip.toStdString();
    // (The keys line is a div of its own: ">" keeps "Ctrl+Z" from matching the end of "Ctrl+Shift+Z", and macOS's "⌘Z" the end of "⇧⌘Z".)
    EXPECT_FALSE(tip.contains(QStringLiteral(">") + native("Ctrl+Z") + QStringLiteral("<"))) << "the old key is gone";
    command("edit.undo")->setShortcuts({});
    EXPECT_FALSE(hasKeysLine(tipOf(undo)));
}

TEST_F(ButtonTipsTest, QuickBarButtonsNameWhatTheyDo) {
    EXPECT_TRUE(tipOf(quickBarButton("edit.redo")).contains(native("Ctrl+Y")));
    EXPECT_TRUE(tipOf(quickBarButton("selection.deselect")).contains(native("Ctrl+D")));
    EXPECT_TRUE(tipOf(quickBarButton("selection.show")).contains(native("Ctrl+H")));
    EXPECT_TRUE(tipOf(quickBarButton("view.rotateCCW")).contains(native("Shift+PgUp")));
    EXPECT_FALSE(hasKeysLine(tipOf(quickBarButton("view.zoomIn")))) << "Zoom In has no key";
}

TEST_F(ButtonTipsTest, ToolTrayToolsShowTheirKeys) {
    QWidget* grid = child<QWidget>("toolGrid");
    ASSERT_NE(grid, nullptr);
    const auto tipAtCell = [&](int cell) {
        QToolTip::showText(QPoint(), QString());
        // A row's pitch: a cell (two lines of names and its padding above and below) and the gap to the next.
        const int pitch = 2 * grid->fontMetrics().height() + 3 * Theme::kSpace2;
        const QPoint at = QPoint(10 + (cell % 3) * (grid->width() / 3), 10 + (cell / 3) * pitch);
        QHelpEvent help(QEvent::ToolTip, at, grid->mapToGlobal(at));
        QCoreApplication::sendEvent(grid, &help);
        return QToolTip::isVisible() ? QToolTip::text() : QString();
    };
    const QString pencil = tipAtCell(0);
    EXPECT_TRUE(pencil.contains(QStringLiteral("Pencil"))) << pencil.toStdString();
    EXPECT_TRUE(pencil.contains(native("N")));
    EXPECT_TRUE(hasKeysLine(pencil));
    EXPECT_TRUE(tipAtCell(1).contains(native("V"))) << "Brush";
    EXPECT_TRUE(tipAtCell(2).contains(native("C"))) << "Water Color";
    EXPECT_TRUE(tipAtCell(4).contains(native("E"))) << "Eraser";
    const QString marker = tipAtCell(3);
    EXPECT_TRUE(marker.contains(QStringLiteral("Marker")));
    EXPECT_FALSE(hasKeysLine(marker)) << "the Marker has no key";
    const QString stamp = tipAtCell(8);
    EXPECT_TRUE(stamp.contains(QStringLiteral("Clone Stamp"))) << stamp.toStdString();
    EXPECT_FALSE(hasKeysLine(stamp)) << "the Clone Stamp has no key until the user binds one";
    const QString bucket = tipAtCell(5);
    EXPECT_TRUE(bucket.contains(QStringLiteral("Bucket"))) << bucket.toStdString();
    EXPECT_FALSE(hasKeysLine(bucket)) << "the Bucket has no key until the user binds one";
    tipAtCell(3);
    tipAtCell(10);  // the first empty cell
    for (int i = 0; i < 60 && QToolTip::isVisible(); ++i) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        QThread::msleep(10);
    }
    EXPECT_FALSE(QToolTip::isVisible());
}

TEST_F(ButtonTipsTest, FixedToolsShowTheirKeysWhereThereAreSome) {
    QToolButton* marquee = child<QToolButton>("fixedTool_marquee");
    QToolButton* lasso = child<QToolButton>("fixedTool_lasso");
    QToolButton* wand = child<QToolButton>("fixedTool_wand");
    EXPECT_TRUE(tipOf(marquee).contains(native("M")));
    EXPECT_TRUE(tipOf(lasso).contains(native("L")));
    EXPECT_TRUE(tipOf(wand).contains(native("W")));
    EXPECT_FALSE(hasKeysLine(tipOf(child<QToolButton>("fixedTool_hand")))) << "the Hand has no key here";
    EXPECT_TRUE(tipOf(marquee).contains(QStringLiteral("Marquee")));
}

TEST_F(ButtonTipsTest, LayerButtonsNameThemAndShowKeysWhereThereAreSome) {
    const QString newLayer = tipOf(child<QToolButton>("newLayerButton"));
    EXPECT_TRUE(newLayer.contains(QStringLiteral("New Layer")));
    EXPECT_TRUE(newLayer.contains(native("Shift+Ctrl+N"))) << newLayer.toStdString();
    EXPECT_FALSE(hasKeysLine(tipOf(child<QToolButton>("newFolderButton")))) << "New Layer Folder has no key";
    const QString merge = tipOf(child<QToolButton>("mergeDownButton"));
    EXPECT_TRUE(merge.contains(QStringLiteral("Merge Down")));
    EXPECT_TRUE(merge.contains(native("Ctrl+E"))) << merge.toStdString();
}

TEST_F(ButtonTipsTest, NavigatorAndColorButtons) {
    EXPECT_TRUE(tipOf(child<QToolButton>("rotateCcwButton")).contains(native("Shift+PgUp")));
    EXPECT_TRUE(tipOf(child<QToolButton>("actualPixelsButton")).contains(native("Ctrl+Alt+0")));
    EXPECT_TRUE(tipOf(child<QToolButton>("secondarySwatch")).contains(native("X")));
    EXPECT_TRUE(tipOf(child<QToolButton>("exchangeButton")).contains(native("X")));
    const QString transparent = tipOf(child<QToolButton>("transparentButton"));
    EXPECT_TRUE(transparent.contains(QStringLiteral("Transparent Color"))) << transparent.toStdString();
    EXPECT_TRUE(transparent.contains(native("-"))) << transparent.toStdString();
    const QString toggle = tipOf(child<QToolButton>("colorViewToggle_colorCircle"));
    EXPECT_TRUE(toggle.contains(QStringLiteral("Color Circle")));
    EXPECT_FALSE(hasKeysLine(tipOf(child<QToolButton>("colorSwatch"))));
}

TEST_F(ButtonTipsTest, EveryButtonOfTheWindowHasATip) {
    int checked = 0;
    for (QAbstractButton* b : window->findChildren<QAbstractButton*>()) {
        if (qobject_cast<QCheckBox*>(b) || qobject_cast<QRadioButton*>(b) ||
            b->objectName() == QLatin1String("qt_dockwidget_floatbutton") ||
            b->objectName() == QLatin1String("qt_dockwidget_closebutton") || b->objectName().startsWith(QLatin1String("qt_")) ||
            b->objectName().endsWith(QLatin1String("ScrollLeftButton")) || b->objectName().endsWith(QLatin1String("ScrollRightButton")))
            continue;
        ++checked;
        EXPECT_FALSE(b->toolTip().isEmpty()) << b->metaObject()->className() << " " << b->objectName().toStdString() << " '"
                                             << b->text().toStdString() << "'";
    }
    EXPECT_GT(checked, 40);
}

TEST_F(ButtonTipsTest, ScreenshotsOfTips) {
    const auto shoot = [&](QWidget* w, const QString& name) {
        QToolTip::hideText();
        const QPoint at = w->rect().center();
        QToolTip::showText(w->mapToGlobal(at), w->toolTip().isEmpty() ? tipOf(w) : w->toolTip(), w);
        QCoreApplication::processEvents();
        for (QWidget* top : QApplication::topLevelWidgets())
            if (top->isVisible() && top->objectName() == QLatin1String("qtooltip_label")) {
                EXPECT_TRUE(test::saveScreenshot(top->grab(), name));
                return;
            }
        ADD_FAILURE() << "no tool tip label to grab";
    };
    tipOf(quickBarButton("edit.undo"));
    shoot(quickBarButton("edit.undo"), QStringLiteral("tip_undo"));
    tipOf(child<QToolButton>("newLayerButton"));
    shoot(child<QToolButton>("newLayerButton"), QStringLiteral("tip_new_layer"));
    tipOf(child<QToolButton>("mergeDownButton"));
    shoot(child<QToolButton>("mergeDownButton"), QStringLiteral("tip_merge_down"));
}
