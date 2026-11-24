#include "core/document/Document.h"
#include "core/tools/ToolBox.h"
#include "core/tools/ToolBoxStore.h"
#include "core/view/ViewText.h"
#include "ui/CanvasView.h"
#include "ui/DocumentTabs.h"
#include "ui/MainWindow.h"
#include "ui/QuickBar.h"
#include "ui/panels/StabilizerCombo.h"
#include "ScreenshotSupport.h"

#include <QAction>
#include <QComboBox>
#include <QFile>
#include <QLabel>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QToolButton>

#include <gtest/gtest.h>

#include <memory>

using namespace doodlebold;

namespace {

class QuickBarTest : public testing::Test {
protected:
    void SetUp() override {
        QStandardPaths::setTestModeEnabled(true);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDir.path());
        test::applyTheme();
        QFile::remove(ToolBoxStore::defaultPath());
        window = std::make_unique<MainWindow>();
        bar = window->quickBar();
    }
    void TearDown() override { QFile::remove(ToolBoxStore::defaultPath()); }

    void openCanvas() {
        window->newDocument();  // before show(): the offscreen platform cannot paint a canvas added to a visible window
        window->show();
        QCoreApplication::processEvents();
    }
    CanvasView* canvas() const { return window->tabs()->currentCanvas(); }
    QAction* command(const char* id) const { return window->command(QString::fromLatin1(id)); }
    QToolButton* button(const char* id) const { return qobject_cast<QToolButton*>(bar->widgetForAction(command(id))); }

    QStringList order() const {
        QStringList out;
        for (QAction* a : bar->actions()) {
            if (a->isSeparator()) {
                out << QStringLiteral("|");
            } else if (!a->objectName().isEmpty()) {
                out << a->objectName();
            } else if (QWidget* w = bar->widgetForAction(a)) {
                if (const auto* label = qobject_cast<QLabel*>(w))
                    out << QStringLiteral("label:") + label->text().trimmed();
                else
                    out << w->objectName();
            }
        }
        return out;
    }

    QTemporaryDir settingsDir;
    std::unique_ptr<MainWindow> window;
    QuickBar* bar = nullptr;
};

QStringList itemTexts(const QComboBox* c) {
    QStringList out;
    for (int i = 0; i < c->count(); ++i)
        out << (c->itemText(i).isEmpty() ? QStringLiteral("(separator)") : c->itemText(i));
    return out;
}

}

TEST_F(QuickBarTest, ControlsAreInTheirOrder) {
    EXPECT_EQ(order(), (QStringList{"edit.undo",
                                    "edit.redo",
                                    "|",
                                    "selection.deselect",
                                    "selection.invert",
                                    "selection.show",
                                    "|",
                                    "zoomCombo",
                                    "view.zoomOut",
                                    "view.zoomIn",
                                    "view.resetPosition",
                                    "|",
                                    "angleCombo",
                                    "view.rotateCCW",
                                    "view.rotateCW",
                                    "view.resetAngle",
                                    "|",
                                    "view.flip",
                                    "|",
                                    "label:Stabilizer",
                                    "globalStabilizer"}));
}

TEST_F(QuickBarTest, ListsHaveTheExpectedEntriesAndAreNotEditable) {
    EXPECT_FALSE(bar->zoomCombo()->isEditable());
    EXPECT_FALSE(bar->angleCombo()->isEditable());
    EXPECT_EQ(itemTexts(bar->zoomCombo()),
              (QStringList{"0.78%", "1.56%", "3.13%", "6.25%", "12.5%", "25%", "50%", "100%", "200%", "400%", "800%", "1600%", "3200%"}));
    EXPECT_EQ(itemTexts(bar->angleCombo()), (QStringList{"-180°", "-135°", "-90°", "-45°", "0°", "45°", "90°", "135°", "180°"}));
    QStringList stabilizer;
    for (int n = 0; n <= 20; ++n)  // the levels, the reference's S-1 to S-5 as 16 to 20
        stabilizer << QString::number(n);
    EXPECT_EQ(itemTexts(bar->stabilizerCombo()), stabilizer);
    EXPECT_FALSE(bar->stabilizerCombo()->isEditable());
}

TEST_F(QuickBarTest, ControlsAreDisabledWithoutACanvasAndFollowTheCurrentOne) {
    EXPECT_FALSE(bar->zoomCombo()->isEnabled());
    EXPECT_FALSE(bar->angleCombo()->isEnabled());
    EXPECT_FALSE(command("edit.undo")->isEnabled());
    EXPECT_FALSE(button("view.zoomIn")->isEnabled());
    EXPECT_FALSE(button("view.flip")->isEnabled());
    EXPECT_TRUE(bar->stabilizerCombo()->isEnabled());

    openCanvas();
    EXPECT_TRUE(bar->zoomCombo()->isEnabled());
    EXPECT_TRUE(button("view.zoomIn")->isEnabled());
    EXPECT_FALSE(button("edit.undo")->isEnabled());
    ASSERT_TRUE(window->tabs()->currentDocument()->addLayer());
    EXPECT_TRUE(button("edit.undo")->isEnabled());
    EXPECT_FALSE(button("edit.redo")->isEnabled());
}

TEST_F(QuickBarTest, ListsShowTheActualZoomAndAngle) {
    openCanvas();
    CanvasView* c = canvas();
    c->setZoom(1.0, c->viewCentre());
    EXPECT_EQ(bar->zoomReadout(), QStringLiteral("100%"));
    EXPECT_EQ(bar->zoomCombo()->currentText(), QStringLiteral("100%"));
    c->setZoom(1.1892, c->viewCentre());
    EXPECT_EQ(bar->zoomReadout(), QStringLiteral("119%"));
    EXPECT_EQ(bar->zoomCombo()->currentIndex(), -1);

    c->setRotation(0.0);
    EXPECT_EQ(bar->angleReadout(), QStringLiteral("0.0°"));
    c->setRotation(33.75);
    EXPECT_EQ(bar->angleReadout(), QStringLiteral("33.8°"));
    c->setRotation(-90.0);
    EXPECT_EQ(bar->angleReadout(), QStringLiteral("-90.0°"));
    EXPECT_EQ(bar->angleCombo()->currentText(), QStringLiteral("-90°"));
    c->setRotation(180.0);
    EXPECT_EQ(bar->angleReadout(), QStringLiteral("180.0°"));

    window->newDocument();
    EXPECT_EQ(bar->zoomReadout(), view_text::zoomText(canvas()->zoom()));
    window->tabs()->setCurrentIndex(0);
    EXPECT_EQ(bar->angleReadout(), QStringLiteral("180.0°"));
}

TEST_F(QuickBarTest, PickingFromTheListsSetsTheViewAboutItsCentre) {
    openCanvas();
    CanvasView* c = canvas();
    c->setZoom(1.0, c->viewCentre());
    QComboBox* zoom = bar->zoomCombo();
    emit zoom->activated(zoom->findText(QStringLiteral("400%")));
    EXPECT_DOUBLE_EQ(c->zoom(), 4.0);
    emit zoom->activated(zoom->findText(QStringLiteral("0.78%")));
    EXPECT_DOUBLE_EQ(c->zoom(), 1.0 / 128);

    QComboBox* angle = bar->angleCombo();
    emit angle->activated(angle->findText(QStringLiteral("135°")));
    EXPECT_DOUBLE_EQ(c->rotation(), 135.0);
    emit angle->activated(angle->findText(QStringLiteral("-180°")));
    EXPECT_DOUBLE_EQ(c->rotation(), 180.0);
    EXPECT_EQ(bar->angleReadout(), QStringLiteral("180.0°"));
}

TEST_F(QuickBarTest, ButtonsDoWhatTheMenuEntriesDo) {
    openCanvas();
    CanvasView* c = canvas();
    c->setZoom(1.0, c->viewCentre());
    button("view.zoomIn")->click();
    EXPECT_DOUBLE_EQ(c->zoom(), 1.5);  // the reference's levels
    button("view.zoomOut")->click();
    button("view.zoomOut")->click();
    EXPECT_DOUBLE_EQ(c->zoom(), 0.75);
    button("view.rotateCW")->click();
    EXPECT_DOUBLE_EQ(c->rotation(), 15.0);
    button("view.rotateCCW")->click();
    button("view.rotateCCW")->click();
    EXPECT_DOUBLE_EQ(c->rotation(), -15.0);
    button("view.resetAngle")->click();
    EXPECT_DOUBLE_EQ(c->rotation(), 0.0);
    EXPECT_DOUBLE_EQ(c->zoom(), 0.75);

    EXPECT_TRUE(button("view.flip")->isCheckable());
    button("view.flip")->click();
    EXPECT_TRUE(c->isFlipped());
    EXPECT_TRUE(button("view.flip")->isChecked());
    c->setRotation(45.0);
    button("view.resetPosition")->click();
    EXPECT_DOUBLE_EQ(c->zoom(), 1.0);
    EXPECT_DOUBLE_EQ(c->rotation(), 0.0);
    EXPECT_FALSE(c->isFlipped());
    EXPECT_FALSE(button("view.flip")->isChecked());
}

TEST_F(QuickBarTest, SelectionButtonsAndTheEye) {
    openCanvas();
    Document* d = window->tabs()->currentDocument();
    EXPECT_FALSE(button("selection.deselect")->isEnabled());
    EXPECT_FALSE(button("selection.invert")->isEnabled());
    command("selection.all")->trigger();
    ASSERT_TRUE(d->selection());
    EXPECT_TRUE(button("selection.deselect")->isEnabled());
    button("selection.deselect")->click();
    EXPECT_FALSE(d->selection());

    QToolButton* eye = button("selection.show");
    EXPECT_EQ(eye->text(), QStringLiteral("Selection"));
    EXPECT_EQ(eye->toolButtonStyle(), Qt::ToolButtonTextBesideIcon);
    EXPECT_TRUE(eye->isCheckable());
    EXPECT_TRUE(eye->isChecked());
    eye->click();
    EXPECT_FALSE(canvas()->selectionVisible());
    eye->click();
    EXPECT_TRUE(canvas()->selectionVisible());
}

TEST_F(QuickBarTest, StabilizerComboSetsTheGlobalLevel) {
    StabilizerCombo* combo = bar->stabilizerCombo();
    combo->setCurrentIndex(combo->findText(QStringLiteral("2")));
    EXPECT_EQ(window->tools()->options().stabilizer, (StabilizerLevel{2}));
    combo->setCurrentIndex(combo->findText(QStringLiteral("10")));
    EXPECT_EQ(window->tools()->options().stabilizer, (StabilizerLevel{10}));
    BrushOptions o = window->tools()->options();
    o.stabilizer = StabilizerLevel{6};
    window->tools()->setOptions(o);
    EXPECT_EQ(combo->level(), o.stabilizer);
    EXPECT_EQ(combo->currentText(), QStringLiteral("6"));
}

TEST_F(QuickBarTest, StabilizerComboIsWideEnoughForItsLongestLevel) {
    StabilizerCombo* combo = bar->stabilizerCombo();
    int widest = 0;
    for (int i = 0; i < combo->count(); ++i)
        widest = std::max(widest, combo->fontMetrics().horizontalAdvance(combo->itemText(i)));
    EXPECT_GE(combo->sizeHint().width(), widest) << "the box shows every level whole";
    EXPECT_LE(combo->sizeHint().width(), widest + 60);
}

TEST_F(QuickBarTest, TooltipsAreDoodleboldsOwnWords) {
    for (QAction* a : bar->actions()) {
        if (a->isSeparator() || a->objectName().isEmpty())
            continue;
        EXPECT_FALSE(a->toolTip().isEmpty()) << a->objectName().toStdString();
        for (const char* typo : {"cloclwise", "potision", "diplay", "Show/Hide the selection."})
            EXPECT_FALSE(a->toolTip().contains(QString::fromLatin1(typo))) << a->toolTip().toStdString();
    }
    EXPECT_TRUE(command("view.zoomIn")->toolTip().toLower().contains(QStringLiteral("in")));
    EXPECT_FALSE(command("view.zoomIn")->toolTip().toLower().contains(QStringLiteral("out")));
    EXPECT_TRUE(command("view.zoomOut")->toolTip().toLower().contains(QStringLiteral("out")));
}

TEST_F(QuickBarTest, TheColorSwatchLivesInTheColorPanelNotInTheBar) {
    for (const QToolButton* b : bar->findChildren<QToolButton*>())
        EXPECT_NE(b->objectName(), QStringLiteral("colorSwatch"));
    EXPECT_NE(window->findChild<QToolButton*>(QStringLiteral("colorSwatch")), nullptr);
}
