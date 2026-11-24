#include "ScreenshotSupport.h"
#include "core/document/Document.h"
#include "core/input/TabletStats.h"
#include "ui/AboutDialog.h"
#include "ui/GlassPopups.h"
#include "ui/JpegQualityDialog.h"
#include "ui/NewCanvasDialog.h"
#include "ui/OptionsDialog.h"
#include "ui/SelectionGrowDialog.h"
#include "ui/TabletDiagnosticsDialog.h"
#include "ui/Theme.h"
#include "ui/timelapse/TimelapseDialogs.h"

#include <QApplication>
#include <QColorDialog>
#include <QInputDialog>
#include <QLabel>
#include <QLayout>
#include <QPainter>
#include <QMessageBox>
#include <QProgressDialog>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QGridLayout>
#include <QWindow>

#include <gtest/gtest.h>

#include <functional>
#include <memory>
#include <vector>

using namespace doodlebold;

namespace {

struct Request {
    QWindow* window;
    QRegion region;
};

using Maker = std::function<QDialog*()>;
struct Named {
    const char* name;
    Maker make;
};

class DialogGlassTest : public ::testing::Test {
protected:
    void SetUp() override {
        QStandardPaths::setTestModeEnabled(true);
        test::applyTheme();
        Document doc(QSize(200, 100), Qt::white);
        doc.startRecording();
        recording = doc.timelapse();
    }

    std::vector<Named> dialogs() {
        return {
            {"Options", [] { return new OptionsDialog(AppOptions::Values{}); }},
            {"NewCanvas", [] { return new NewCanvasDialog(QStringLiteral("a"), NewCanvasValues{}, defaultCanvasPresets()); }},
            {"PresetEdit", [] { return new PresetEditDialog(CanvasPreset{}); }},
            {"PresetList", [] { return new PresetListDialog(defaultCanvasPresets(), NewCanvasValues{}); }},
            {"JpegQuality", [] { return new JpegQualityDialog(QImage(64, 64, QImage::Format_ARGB32), ExportOptions{}, 72.0); }},
            {"SelectionGrow", [] { return new SelectionGrowDialog(true); }},
            {"TabletDiagnostics", [this] { return new TabletDiagnosticsDialog(&stats); }},
            {"About", [] { return new AboutDialog; }},
            {"ExportTimelapse", [this] { return new ExportTimelapseDialog(recording, QStringLiteral("a.doodle"), QString()); }},
            {"FfmpegMissing", [] { return new FfmpegMissingDialog(QStringLiteral("not found")); }},
        };
    }
    static std::vector<Named> qtDialogs() {
        return {
            {"MessageBox",
             [] {
                 return new QMessageBox(QMessageBox::Question, QStringLiteral("Delete"), QStringLiteral("Delete it?"),
                                        QMessageBox::Yes | QMessageBox::No);
             }},
            {"InputDialog",
             [] {
                 auto* d = new QInputDialog;
                 d->setLabelText(QStringLiteral("Name:"));
                 return d;
             }},
            {"ProgressDialog", [] { return new QProgressDialog(QStringLiteral("Writing …"), QStringLiteral("Cancel"), 0, 10); }},
            {"ColorDialog",
             [] {
                 auto* d = new QColorDialog;
                 d->setOption(QColorDialog::DontUseNativeDialog);
                 return d;
             }},
        };
    }

    TabletStats stats;
    std::shared_ptr<const Timelapse> recording;
};

QMargins marginsOf(const QLayout* layout) {
    int l, t, r, b;
    layout->getContentsMargins(&l, &t, &r, &b);
    return {l, t, r, b};
}

}

TEST_F(DialogGlassTest, EveryDialogIsAGlassKind) {
    for (const auto& list : {dialogs(), qtDialogs()})
        for (const Named& d : list) {
            std::unique_ptr<QDialog> dialog(d.make());
            EXPECT_TRUE(GlassPopups::isGlassKind(dialog.get())) << d.name;
        }
}

TEST_F(DialogGlassTest, DialogsAreTranslucentAndAskForBlurWhereTheDesktopBlurs) {
    std::vector<Request> requests;
    GlassPopups glass([&](QWindow* w, const QRegion& r) {
        requests.push_back({w, r});
        return true;
    });
    qApp->installEventFilter(&glass);
    for (const auto& list : {dialogs(), qtDialogs()})
        for (const Named& d : list) {
            std::unique_ptr<QDialog> dialog(d.make());
            dialog->ensurePolished();
            EXPECT_TRUE(dialog->testAttribute(Qt::WA_TranslucentBackground)) << d.name;
            EXPECT_TRUE(dialog->property("glass").toBool()) << d.name;
            EXPECT_FALSE(dialog->testAttribute(Qt::WA_WState_Created)) << d.name << ": polished before its window exists";

            requests.clear();
            dialog->show();
            QApplication::processEvents();
            ASSERT_FALSE(requests.empty()) << d.name;
            EXPECT_EQ(requests.back().window, dialog->windowHandle()) << d.name;
            EXPECT_EQ(requests.back().region.boundingRect(), dialog->rect()) << d.name;
            EXPECT_TRUE(requests.back().region.contains(QPoint(0, 0))) << d.name << ": the desktop rounds a dialog's corners";

            // Shown again after hiding: the blur is asked for again (BackgroundBlur releases it when the window hides).
            dialog->hide();
            requests.clear();
            dialog->show();
            QApplication::processEvents();
            EXPECT_FALSE(requests.empty()) << d.name << " shown a second time";
            dialog->hide();
        }
    qApp->removeEventFilter(&glass);
}

// A dialog whose window exists before it is polished (on Wayland the New Canvas dialog's did) is made again with an alpha
// channel: a window made without one paints opaque over the blur.
TEST_F(DialogGlassTest, AWindowMadeBeforeThePolishIsMadeAgainWithAlpha) {
    GlassPopups glass([](QWindow*, const QRegion&) { return true; });
    qApp->installEventFilter(&glass);
    for (const auto& list : {dialogs(), qtDialogs()})
        for (const Named& d : list) {
            std::unique_ptr<QDialog> dialog(d.make());
            dialog->winId();
            ASSERT_TRUE(dialog->windowHandle()->handle()) << d.name;
            dialog->show();
            QApplication::processEvents();
            EXPECT_TRUE(dialog->testAttribute(Qt::WA_TranslucentBackground)) << d.name;
            EXPECT_EQ(dialog->windowHandle()->requestedFormat().alphaBufferSize(), 8) << d.name;
#ifndef Q_OS_WIN  // Windows makes translucent windows layered and reports no alpha bits in their format
            EXPECT_GT(dialog->windowHandle()->format().alphaBufferSize(), 0) << d.name << ": the shown window has an alpha channel";
#endif
            dialog->hide();
        }
    qApp->removeEventFilter(&glass);
}

TEST_F(DialogGlassTest, DialogsStayOpaqueWhereTheDesktopCannotBlur) {
    for (const auto& list : {dialogs(), qtDialogs()})
        for (const Named& d : list) {
            std::unique_ptr<QDialog> dialog(d.make());
            dialog->show();
            QApplication::processEvents();
            EXPECT_FALSE(dialog->testAttribute(Qt::WA_TranslucentBackground)) << d.name;
            EXPECT_FALSE(dialog->property("glass").isValid()) << d.name;
            dialog->hide();
        }
}

TEST_F(DialogGlassTest, TheStyleSheetMakesGlassDialogsTranslucent) {
    SelectionGrowDialog plain(true);
    plain.resize(240, 120);
    const QColor opaque = plain.grab().toImage().pixelColor(2, 2);
    EXPECT_EQ(opaque.alpha(), 255);

    GlassPopups glass([](QWindow*, const QRegion&) { return true; });
    qApp->installEventFilter(&glass);
    SelectionGrowDialog frosted(true);
    frosted.resize(240, 120);
    frosted.ensurePolished();
    const QColor seen = frosted.grab().toImage().pixelColor(2, 2);
    EXPECT_LT(seen.alpha(), 255) << "see-through";
    EXPECT_GT(seen.alpha(), 160) << "but mostly the dialog";
    qApp->removeEventFilter(&glass);
}

TEST_F(DialogGlassTest, EveryDialogKeepsTheMenusPaddingRoundItsContent) {
    const int pad = Theme::metric(Theme::Metric::DialogPadding);
    EXPECT_EQ(pad, 16) << "the menu's padding (4) and its items' left padding (12)";
    for (const Named& d : dialogs()) {
        std::unique_ptr<QDialog> dialog(d.make());
        ASSERT_NE(dialog->layout(), nullptr) << d.name;
        EXPECT_EQ(marginsOf(dialog->layout()), QMargins(pad, pad, pad, pad)) << d.name;
        EXPECT_EQ(dialog->layout()->spacing(), pad) << d.name;
    }
    for (const Named& d : qtDialogs()) {
        std::unique_ptr<QDialog> dialog(d.make());
        dialog->show();
        if (auto* progress = qobject_cast<QProgressDialog*>(dialog.get())) {
            EXPECT_EQ(progress->findChild<QLabel*>()->x(), pad) << d.name;
            continue;
        }
        ASSERT_NE(dialog->layout(), nullptr) << d.name;
        EXPECT_EQ(marginsOf(dialog->layout()), QMargins(pad, pad, pad, pad)) << d.name;
    }
}

TEST_F(DialogGlassTest, DialogsCannotBeStretchedAndSpaceTheirFieldsAlike) {
    // Options resizes since its pages may hold long lists.
    const QStringList resizable{QStringLiteral("PresetList"), QStringLiteral("JpegQuality"), QStringLiteral("TabletDiagnostics"),
                                QStringLiteral("Options")};
    for (const Named& d : dialogs()) {
        std::unique_ptr<QDialog> dialog(d.make());
        const bool fixed = dialog->layout()->sizeConstraint() == QLayout::SetFixedSize;
        EXPECT_EQ(fixed, !resizable.contains(QString::fromLatin1(d.name))) << d.name;
    }
    NewCanvasDialog dialog(QStringLiteral("a"), NewCanvasValues{}, defaultCanvasPresets());
    const auto* grid = dialog.findChild<QGridLayout*>();
    ASSERT_TRUE(grid);
    EXPECT_EQ(grid->verticalSpacing(), Theme::metric(Theme::Metric::FieldSpacing));
    EXPECT_EQ(grid->horizontalSpacing(), Theme::metric(Theme::Metric::FieldGap));
    dialog.show();
    QApplication::processEvents();
    const QSize natural = dialog.size();
    dialog.resize(natural + QSize(200, 300));
    QApplication::processEvents();
    EXPECT_EQ(dialog.size(), natural) << "a fixed dialog keeps its size";
}

TEST_F(DialogGlassTest, SaveScreenshots) {
    GlassPopups glass([](QWindow*, const QRegion&) { return true; });
    qApp->installEventFilter(&glass);
    auto shoot = [&](const Named& d, const QString& file) {
        std::unique_ptr<QDialog> dialog(d.make());
        dialog->show();
        QApplication::processEvents();
        const QImage front = dialog->grab().toImage();
        QImage shot(front.size(), QImage::Format_ARGB32_Premultiplied);
        QPainter p(&shot);
        for (int y = 0; y < shot.height(); y += 24)
            for (int x = 0; x < shot.width(); x += 24)
                p.fillRect(x, y, 24, 24, ((x + y) / 24) % 2 ? QColor(230, 190, 60) : QColor(60, 140, 220));
        p.drawImage(0, 0, front);
        p.end();
        EXPECT_TRUE(test::saveScreenshot(QPixmap::fromImage(shot), file));
    };
    const char* shots[][2] = {{"Options", "dialog-options"},
                              {"NewCanvas", "dialog-new-canvas"},
                              {"ExportTimelapse", "dialog-export-timelapse"},
                              {"SelectionGrow", "dialog-selection-grow"},
                              {"About", "dialog-about"},
                              {"MessageBox", "dialog-question"}};
    for (const auto& s : shots)
        for (const auto& list : {dialogs(), qtDialogs()})
            for (const Named& d : list)
                if (QLatin1String(d.name) == QLatin1String(s[0]))
                    shoot(d, QLatin1String(s[1]));
    qApp->removeEventFilter(&glass);
}
