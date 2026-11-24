// Every control on the tools' panels says what it does.
#include "core/brush/BrushResources.h"
#include "core/tools/ToolBox.h"
#include "ui/ValueBar.h"
#include "ui/panels/BrushResourceList.h"
#include "ui/panels/ToolPanel.h"
#include "ScreenshotSupport.h"

#include <QAbstractButton>
#include <QAbstractItemView>
#include <QAbstractSpinBox>
#include <QComboBox>
#include <QLabel>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTextDocument>

#include <gtest/gtest.h>

#include <memory>

using namespace doodlebold;

namespace {

bool isControl(const QWidget* w) {
    return qobject_cast<const QAbstractButton*>(w) || qobject_cast<const QComboBox*>(w) || qobject_cast<const QAbstractSpinBox*>(w) ||
           qobject_cast<const ValueBar*>(w) || qobject_cast<const QAbstractItemView*>(w);
}

// A control's own parts (a box's list, a bar's editor) are the control's, not controls of their own.
bool insideAnotherControl(const QWidget* w, const QWidget* top) {
    for (const QWidget* p = w->parentWidget(); p && p != top; p = p->parentWidget())
        if (isControl(p))
            return true;
    return false;
}

QString plain(const QString& tip) {
    QTextDocument doc;
    doc.setHtml(tip);
    return doc.toPlainText().simplified();
}

// What the control shows of itself, so a tip that only repeats it doesn't count.
QString ownText(const QWidget* w) {
    if (const auto* b = qobject_cast<const QAbstractButton*>(w))
        return b->text();
    if (const auto* c = qobject_cast<const QComboBox*>(w))
        return c->currentText();
    return {};
}

class ToolTipsTest : public testing::Test {
protected:
    void SetUp() override {
        QStandardPaths::setTestModeEnabled(true);
        test::applyTheme();
        library = std::make_unique<BrushResourceLibrary>(folder.filePath(QStringLiteral("brushes")), BrushResourceLibrary::BuiltIns::Left);
        QImage picture(64, 64, QImage::Format_Grayscale8);
        picture.fill(128);
        ASSERT_TRUE(library
                        ->add({{ResourceKind::BlotMap, QStringLiteral("Blots"), picture},
                               {ResourceKind::Shape, QStringLiteral("Leaf"), picture},
                               {ResourceKind::Bristle, QStringLiteral("Hair"), picture},
                               {ResourceKind::Texture, QStringLiteral("Weave"), picture}})
                        .ok());
        panel = std::make_unique<ToolPanel>(&tools, library.get());
        panel->resize(320, 900);
        panel->show();
        QCoreApplication::processEvents();
    }

    // Every tool's settings: the brush tools' rows (each built once, shown by tool), the Bucket's and the fixed tools' pages.
    QWidget* settings() const { return panel->findChild<QWidget*>(QStringLiteral("toolParametersScroll")); }

    QTemporaryDir folder;
    ToolBox tools;
    std::unique_ptr<BrushResourceLibrary> library;
    std::unique_ptr<ToolPanel> panel;
};

}

// Feedback: "we should add a tooltip that appears that explains every single field in every tool."
TEST_F(ToolTipsTest, EveryControlOnTheToolsPanelsHasATipThatExplainsIt) {
    QWidget* top = settings();
    ASSERT_NE(top, nullptr);
    int controls = 0;
    for (QWidget* w : top->findChildren<QWidget*>()) {
        if (!isControl(w) || insideAnotherControl(w, top))
            continue;
        ++controls;
        const QString tip = plain(w->toolTip());
        const QString name = w->objectName().isEmpty() ? QString::fromLatin1(w->metaObject()->className()) : w->objectName();
        EXPECT_FALSE(tip.isEmpty()) << name.toStdString() << " has no tip";
        EXPECT_GT(tip.size(), ownText(w).size() + 8)
            << name.toStdString() << ": \"" << tip.toStdString() << "\" doesn't say more than the control shows";
    }
    EXPECT_GT(controls, 80) << "every tool's controls were looked at, the hidden ones too";
}

TEST_F(ToolTipsTest, ARowsLabelShowsTheSameTipAsItsControl) {
    for (const char* bar : {"sizeBar", "densityBar", "blendingBar", "sensitivitySizeBar", "bucketThresholdBar", "thresholdBar"}) {
        auto* control = settings()->findChild<QWidget*>(QString::fromLatin1(bar));
        ASSERT_NE(control, nullptr) << bar;
        bool labelled = false;
        for (QLabel* label : control->parentWidget()->findChildren<QLabel*>(Qt::FindDirectChildrenOnly))
            labelled = labelled || label->toolTip() == control->toolTip();
        EXPECT_TRUE(labelled) << bar;
    }
    const QString tip = plain(settings()->findChild<QWidget*>(QStringLiteral("sensitivitySizeBar"))->toolTip());
    EXPECT_TRUE(tip.startsWith(QStringLiteral("Size sensitivity"))) << tip.toStdString();
    EXPECT_TRUE(tip.contains(QStringLiteral("200"))) << "the range where it helps";
}

// The picture's strength bar is Mess., Hair or Dens. by the picture's kind, and its tip follows.
TEST_F(ToolTipsTest, TheTipShapesStrengthTipFollowsThePicturesKind) {
    ASSERT_TRUE(tools.selectSlot({0, 1}).ok());  // the Brush
    auto* forms = settings()->findChild<BrushResourceList*>(QStringLiteral("brushFormList"));
    auto* strength = settings()->findChild<ValueBar*>(QStringLiteral("formStrengthBar"));
    ASSERT_NE(forms, nullptr);
    ASSERT_NE(strength, nullptr);
    for (const auto& [picture, label] : {std::pair{"Blots", "Mess."}, std::pair{"Hair", "Hair"}, std::pair{"Leaf", "Dens."}}) {
        const int i = forms->findText(QString::fromLatin1(picture));
        ASSERT_GE(i, 0) << picture;
        forms->setCurrentIndex(i);
        emit forms->activated(i);
        QCoreApplication::processEvents();
        EXPECT_TRUE(plain(strength->toolTip()).startsWith(QString::fromLatin1(label))) << plain(strength->toolTip()).toStdString();
    }
    for (const char* name : {"bristleMinimumBar", "bristleRandomizeBar", "bristleDirectionCombo", "bristleDetectTurnCheck"})
        EXPECT_FALSE(plain(settings()->findChild<QWidget*>(QString::fromLatin1(name))->toolTip()).contains(QStringLiteral("yet")))
            << name << ": every bristle setting changes the stroke now";
}
