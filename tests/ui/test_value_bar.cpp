#include "ui/ValueBar.h"

#include <QApplication>
#include <QContextMenuEvent>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMenu>
#include <QMouseEvent>
#include <QWheelEvent>

#include <gtest/gtest.h>

using namespace doodlebold;

namespace {

void mouse(QWidget* w, QEvent::Type type, double x, Qt::MouseButton button) {
    const QPointF p(x, w->height() / 2.0);
    QMouseEvent e(type, p, w->mapToGlobal(p), button, type == QEvent::MouseButtonRelease ? Qt::NoButton : button, Qt::NoModifier);
    QCoreApplication::sendEvent(w, &e);
}

void key(QWidget* w, int k) {
    QKeyEvent e(QEvent::KeyPress, k, Qt::NoModifier);
    QCoreApplication::sendEvent(w, &e);
}

}

TEST(ValueBar, DraggingSetsTheValueAtThePointer) {
    ValueBar bar;
    bar.setRange(0, 100);
    bar.resize(202, 18);
    int drags = 0, finished = 0;
    QObject::connect(&bar, &ValueBar::dragStarted, [&] { ++drags; });
    QObject::connect(&bar, &ValueBar::dragFinished, [&] { ++finished; });
    mouse(&bar, QEvent::MouseButtonPress, 101, Qt::LeftButton);
    EXPECT_EQ(bar.value(), 50);
    EXPECT_TRUE(bar.isDragging());
    mouse(&bar, QEvent::MouseMove, 1, Qt::LeftButton);
    EXPECT_EQ(bar.value(), 0);
    mouse(&bar, QEvent::MouseMove, 500, Qt::LeftButton);
    EXPECT_EQ(bar.value(), 100) << "past the end: the maximum";
    mouse(&bar, QEvent::MouseButtonRelease, 500, Qt::LeftButton);
    EXPECT_FALSE(bar.isDragging());
    EXPECT_EQ(drags, 1);
    EXPECT_EQ(finished, 1);
}

TEST(ValueBar, TheWheelCanBeSwitchedOffAndThenPassesOn) {
    ValueBar bar;
    bar.setRange(0, 100);
    bar.setValue(50);
    bar.setWheelSteps(false);
    QWheelEvent up(QPointF(5, 5), QPointF(5, 5), QPoint(), QPoint(0, 240), Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
    QCoreApplication::sendEvent(&bar, &up);
    EXPECT_EQ(bar.value(), 50);
    EXPECT_FALSE(up.isAccepted()) << "ignored, so the panel around it scrolls";
    key(&bar, Qt::Key_Right);
    EXPECT_EQ(bar.value(), 51) << "the keys still step";
}

TEST(ValueBar, WheelAndKeysStep) {
    ValueBar bar;
    bar.setRange(0, 100);
    bar.setValue(50);
    QWheelEvent up(QPointF(5, 5), QPointF(5, 5), QPoint(), QPoint(0, 240), Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
    QCoreApplication::sendEvent(&bar, &up);
    EXPECT_EQ(bar.value(), 50) << "the pointer only lies over it: the wheel is for the panel";
    EXPECT_FALSE(up.isAccepted());
    bar.show();
    bar.activateWindow();
    bar.setFocus();
    QCoreApplication::processEvents();
    ASSERT_TRUE(bar.hasFocus());
    QWheelEvent again(QPointF(5, 5), QPointF(5, 5), QPoint(), QPoint(0, 240), Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
    QCoreApplication::sendEvent(&bar, &again);
    EXPECT_EQ(bar.value(), 52) << "with the keyboard's focus the wheel steps";
    key(&bar, Qt::Key_Left);
    EXPECT_EQ(bar.value(), 51);
    key(&bar, Qt::Key_PageUp);
    EXPECT_EQ(bar.value(), 61);
    key(&bar, Qt::Key_End);
    EXPECT_EQ(bar.value(), 100);
}

TEST(ValueBar, RightClickTypesAnExactValue) {
    ValueBar bar;
    bar.setRange(0.1, 5000);
    bar.setDecimals(1);
    bar.setSuffix(QStringLiteral(" px"));
    bar.setValue(10);
    bar.resize(120, 18);
    bar.show();
    mouse(&bar, QEvent::MouseButtonPress, 30, Qt::RightButton);
    ASSERT_TRUE(bar.isEditing());
    EXPECT_EQ(bar.value(), 10) << "a right click does not move the value";
    auto* editor = bar.findChild<QLineEdit*>(QStringLiteral("valueEditor"));
    ASSERT_NE(editor, nullptr);
    EXPECT_EQ(editor->text(), QStringLiteral("10.0"));
    editor->setText(QStringLiteral("23.45"));
    key(editor, Qt::Key_Return);
    EXPECT_FALSE(bar.isEditing());
    EXPECT_DOUBLE_EQ(bar.value(), 23.5) << "rounded to the bar's decimals";
    EXPECT_EQ(bar.text(), QStringLiteral("23.5 px"));

    bar.editValue();
    editor->setText(QStringLiteral("99"));
    key(editor, Qt::Key_Escape);
    EXPECT_FALSE(bar.isEditing());
    EXPECT_DOUBLE_EQ(bar.value(), 23.5) << "Escape keeps the value";

    bar.editValue();
    editor->setText(QStringLiteral("99999"));
    key(editor, Qt::Key_Return);
    EXPECT_DOUBLE_EQ(bar.value(), 5000) << "clamped to the range";
}

TEST(ValueBar, ASuffixShowsAfterTheNumberAndTypingAcceptsItOrNot) {
    ValueBar bar;
    bar.setRange(0, 100);
    bar.setSuffix(QStringLiteral("%"));
    bar.resize(120, 22);
    bar.show();
    bar.setValue(7);
    EXPECT_EQ(bar.text(), QStringLiteral("7%"));
    auto* editor = bar.findChild<QLineEdit*>(QStringLiteral("valueEditor"));
    for (const char* typed : {"43", "43%", " 43 % ", "43 %"}) {
        bar.setValue(0);
        bar.editValue();
        editor = bar.findChild<QLineEdit*>(QStringLiteral("valueEditor"));
        ASSERT_NE(editor, nullptr);
        EXPECT_EQ(editor->text(), QStringLiteral("0")) << "the editor holds the bare number";
        editor->setText(QString::fromLatin1(typed));
        key(editor, Qt::Key_Return);
        EXPECT_EQ(bar.value(), 43) << typed;
        EXPECT_EQ(bar.text(), QStringLiteral("43%"));
    }
    bar.setValue(100);
    EXPECT_GE(bar.minimumSizeHint().width(), bar.fontMetrics().horizontalAdvance(QStringLiteral("100%")))
        << "the widest value with its % fits";
}

TEST(ValueBar, MappingPlacesNonLinearValues) {
    ValueBar bar;
    bar.setRange(1, 10000);
    bar.setMapping([](double v) { return std::log10(v) / 4.0; }, [](double p) { return std::pow(10.0, p * 4.0); });
    bar.resize(202, 18);
    mouse(&bar, QEvent::MouseButtonPress, 101, Qt::LeftButton);
    EXPECT_NEAR(bar.value(), 100, 5) << "the middle of a logarithmic bar";
    mouse(&bar, QEvent::MouseButtonRelease, 101, Qt::LeftButton);
    key(&bar, Qt::Key_Right);
    EXPECT_GT(bar.value(), 100) << "keys step along the field";
}

TEST(ValueBar, SnapKeepsValuesOnAGridFromTheMinimum) {
    ValueBar bar;
    bar.setRange(10, 500);
    bar.setSnap(5);
    bar.setSingleStep(5);
    bar.setValue(37);
    EXPECT_EQ(bar.value(), 35);
    bar.setValue(38);
    EXPECT_EQ(bar.value(), 40);
    bar.setValue(9999);
    EXPECT_EQ(bar.value(), 500);
    bar.setValue(-3);
    EXPECT_EQ(bar.value(), 10);
    ValueBar odd;
    odd.setRange(3, 20);
    odd.setSnap(4);
    odd.setValue(10);
    EXPECT_EQ(odd.value(), 11) << "the grid starts at the minimum: 3, 7, 11, 15, 19";
}

// Windows sends the context menu at the right button's release, to the editor the press opened under the pointer.
TEST(ValueBar, RightClickOpensNoMenu) {
    ValueBar bar;
    bar.setRange(0, 100);
    bar.resize(120, 18);
    bar.show();
    mouse(&bar, QEvent::MouseButtonPress, 30, Qt::RightButton);
    auto* editor = bar.findChild<QLineEdit*>(QStringLiteral("valueEditor"));
    ASSERT_NE(editor, nullptr);
    for (QWidget* target : {static_cast<QWidget*>(editor), static_cast<QWidget*>(&bar)}) {
        const QPoint at(30, 9);
        QContextMenuEvent menu(QContextMenuEvent::Mouse, at, target->mapToGlobal(at));
        QCoreApplication::sendEvent(target, &menu);
        for (QWidget* window : QApplication::topLevelWidgets())
            if (qobject_cast<QMenu*>(window) && window->isVisible()) {
                ADD_FAILURE() << "a menu opened over " << target->objectName().toStdString();
                window->close();
            }
    }
    EXPECT_TRUE(bar.isEditing());
}
