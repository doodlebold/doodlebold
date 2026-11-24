#include "core/document/Document.h"
#include "core/input/TabletStats.h"
#include "core/view/ViewNavigator.h"
#include "ui/CanvasView.h"
#include "ui/Theme.h"
#include "ui/panels/NavigatorPanel.h"
#include "ui/ValueBar.h"
#include "ScreenshotSupport.h"

#include <QApplication>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QMouseEvent>
#include <QPainter>
#include <QPolygonF>
#include <QThread>
#include <QToolButton>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>
#include <memory>

#include <gtest/gtest.h>

using namespace doodlebold;

namespace {

struct Rig {
    explicit Rig(const QSize& docSize = QSize(1200, 800)) : doc(docSize, Qt::white), canvas(&doc, &stats) {
        canvas.resize(800, 600);
        canvas.resetView();
        panel.resize(300, 420);
        panel.show();
        panel.setCanvas(&canvas);
        settle();
    }

    template <typename T> T* find(const char* name) { return panel.findChild<T*>(QString::fromLatin1(name)); }

    void settle() { QCoreApplication::processEvents(); }

    void press(const QPointF& previewPos) {
        QWidget* preview = find<QWidget>("navigatorPreview");
        ASSERT_NE(preview, nullptr);
        QMouseEvent e(QEvent::MouseButtonPress, previewPos, preview->mapToGlobal(previewPos), Qt::LeftButton, Qt::LeftButton,
                      Qt::NoModifier);
        QCoreApplication::sendEvent(preview, &e);
    }

    TabletStats stats;
    Document doc;
    CanvasView canvas;
    NavigatorPanel panel;
};

QPointF centreOf(const QPolygonF& poly) {
    QPointF sum;
    for (int i = 0; i < 4; ++i)  // the fifth point of a mapped rectangle repeats the first
        sum += poly[i];
    return sum / 4.0;
}

LayerId addPaintLayer(Document& doc) {
    const auto added = doc.addLayer();
    EXPECT_TRUE(added.ok());
    return added.ok() ? added.value() : kNoLayer;
}

void paintRect(Document& doc, LayerId id, const QRect& r, const QColor& c) {
    const auto target = doc.paintTarget(id);
    ASSERT_TRUE(target.ok());
    target.value()->fill(r, c);
    doc.notifyPixelsChanged(id, r);
}

template <typename F> bool waitUntil(F&& done) {
    QElapsedTimer t;
    t.start();
    while (!done() && t.elapsed() < 1000) {
        QCoreApplication::processEvents();
        QThread::msleep(5);
    }
    return done();
}

}

TEST(NavigatorPanel, ZoomSliderMappingRoundTripsAndIsMonotonic) {
    EXPECT_EQ(NavigatorPanel::zoomToSlider(ViewNavigator::kMinZoom), 0);
    EXPECT_EQ(NavigatorPanel::zoomToSlider(ViewNavigator::kMaxZoom), 1000);
    EXPECT_NEAR(NavigatorPanel::sliderToZoom(0), ViewNavigator::kMinZoom, 1e-12);
    EXPECT_NEAR(NavigatorPanel::sliderToZoom(1000), ViewNavigator::kMaxZoom, 1e-9);
    EXPECT_EQ(NavigatorPanel::zoomToSlider(0.0001), 0);
    EXPECT_EQ(NavigatorPanel::zoomToSlider(500.0), 1000);

    double previous = 0.0;
    for (int v = 0; v <= 1000; ++v) {
        const double z = NavigatorPanel::sliderToZoom(v);
        EXPECT_GT(z, previous) << "at " << v;
        previous = z;
        EXPECT_EQ(NavigatorPanel::zoomToSlider(z), v) << "at " << v;
    }
    for (double z : {0.01, 0.05, 0.25, 0.5, 1.0, 2.0, 7.3, 16.0, 32.0}) {
        const double back = NavigatorPanel::sliderToZoom(NavigatorPanel::zoomToSlider(z));
        EXPECT_NEAR(std::log(back), std::log(z), 0.006) << "zoom " << z;
    }
    EXPECT_EQ(NavigatorPanel::zoomToSlider(1.0), 583);
    EXPECT_NEAR(NavigatorPanel::sliderToZoom(583 + 42), std::exp2(0.5), 0.01);
    EXPECT_NEAR(NavigatorPanel::sliderToZoom(583 - 42), std::exp2(-0.5), 0.01);
}

TEST(NavigatorPanel, ControlsDriveTheCanvas) {
    Rig r;
    auto* zoomSpin = r.find<ValueBar>("zoomBar");
    auto* angleSpin = r.find<ValueBar>("angleBar");
    auto* flip = r.find<QToolButton>("flipButton");
    ASSERT_NE(zoomSpin, nullptr);
    ASSERT_NE(angleSpin, nullptr);
    ASSERT_NE(flip, nullptr);
    EXPECT_NEAR(zoomSpin->minimum(), 0.78, 0.01);
    EXPECT_DOUBLE_EQ(zoomSpin->maximum(), 3200.0);
    EXPECT_TRUE(zoomSpin->text().endsWith(QLatin1Char('%'))) << qPrintable(zoomSpin->text());
    EXPECT_EQ(angleSpin->text(), QStringLiteral("0°"));

    zoomSpin->setValue(200.0);
    EXPECT_NEAR(r.canvas.zoom(), 2.0, 1e-9);
    EXPECT_NEAR(centreOf(r.canvas.visibleDocumentArea()).x(), 600.0, 1e-6);

    angleSpin->setValue(30);
    EXPECT_NEAR(r.canvas.rotation(), 30.0, 1e-9);

    EXPECT_FALSE(r.canvas.isFlipped());
    flip->click();
    EXPECT_TRUE(r.canvas.isFlipped());
    EXPECT_TRUE(flip->isChecked());
    flip->click();
    EXPECT_FALSE(r.canvas.isFlipped());
    EXPECT_FALSE(flip->isChecked());

    r.find<QToolButton>("rotateCwButton")->click();
    EXPECT_NEAR(r.canvas.rotation(), 30.0 + CanvasView::kRotateStepDeg, 1e-9);
    r.find<QToolButton>("rotateCcwButton")->click();
    r.find<QToolButton>("rotateCcwButton")->click();
    EXPECT_NEAR(r.canvas.rotation(), 30.0 - CanvasView::kRotateStepDeg, 1e-9);
    r.find<QToolButton>("resetAngleButton")->click();
    EXPECT_NEAR(r.canvas.rotation(), 0.0, 1e-9);

    r.find<QToolButton>("actualPixelsButton")->click();
    EXPECT_NEAR(r.canvas.zoom(), 1.0, 1e-9);
    r.find<QToolButton>("zoomInButton")->click();
    EXPECT_NEAR(r.canvas.zoom(), 1.5, 1e-9);  // the reference's levels
    r.find<QToolButton>("zoomOutButton")->click();
    r.find<QToolButton>("zoomOutButton")->click();
    EXPECT_NEAR(r.canvas.zoom(), 0.75, 1e-9);

    zoomSpin->setValue(400.0);
    EXPECT_NEAR(r.canvas.zoom(), 4.0, 0.03);
    angleSpin->setValue(-90);
    EXPECT_NEAR(r.canvas.rotation(), -90.0, 1e-9);
    QMouseEvent right(QEvent::MouseButtonPress, QPointF(10, 5), QPointF(10, 5), Qt::RightButton, Qt::RightButton, Qt::NoModifier);
    QCoreApplication::sendEvent(zoomSpin, &right);
    EXPECT_TRUE(zoomSpin->isEditing());
}

TEST(NavigatorPanel, ControlsFollowTheCanvasWithoutFeedingBack) {
    Rig r;
    int changes = 0;
    QObject::connect(&r.canvas, &CanvasView::viewChanged, &r.canvas, [&changes] { ++changes; });
    auto* zoomSpin = r.find<ValueBar>("zoomBar");
    auto* angleSpin = r.find<ValueBar>("angleBar");
    auto* flip = r.find<QToolButton>("flipButton");

    r.canvas.setZoom(3.0, QPointF(400, 300));
    EXPECT_EQ(changes, 1);
    EXPECT_NEAR(zoomSpin->value(), 300.0, 0.05);
    EXPECT_NEAR(r.canvas.zoom(), 3.0, 1e-12);

    r.canvas.rotateBy(45.0);
    EXPECT_EQ(changes, 2);
    EXPECT_EQ(angleSpin->value(), 45);
    EXPECT_NEAR(r.canvas.rotation(), 45.0, 1e-12);

    r.canvas.toggleFlip();
    EXPECT_EQ(changes, 3);
    EXPECT_TRUE(flip->isChecked());
    EXPECT_EQ(angleSpin->value(), -45);

    r.canvas.setRotation(12.4);
    EXPECT_EQ(angleSpin->value(), 12);
    EXPECT_NEAR(r.canvas.rotation(), 12.4, 1e-12);

    r.canvas.actualPixels();
    EXPECT_NEAR(zoomSpin->value(), 100.0, 1e-9);
    r.canvas.resetAngle();
    EXPECT_EQ(angleSpin->value(), 0);
    r.canvas.toggleFlip();
    EXPECT_FALSE(flip->isChecked());
    EXPECT_EQ(changes, 7);
}

TEST(NavigatorPanel, PreviewMappingKeepsAspectRatioAndCentres) {
    Rig r(QSize(1200, 600));
    const QWidget* preview = r.find<QWidget>("navigatorPreview");
    ASSERT_NE(preview, nullptr);
    EXPECT_GE(preview->minimumHeight(), 140);

    const QPointF topLeft = r.panel.documentToPreview(QPointF(0, 0));
    const QPointF bottomRight = r.panel.documentToPreview(QPointF(1200, 600));
    EXPECT_NEAR((bottomRight.x() - topLeft.x()) / (bottomRight.y() - topLeft.y()), 2.0, 0.02);
    EXPECT_NEAR((topLeft.x() + bottomRight.x()) / 2.0, preview->width() / 2.0, 1.0);
    EXPECT_NEAR((topLeft.y() + bottomRight.y()) / 2.0, preview->height() / 2.0, 1.0);
    EXPECT_GE(topLeft.x(), 0.0);
    EXPECT_LE(bottomRight.x(), preview->width());
    EXPECT_LE(bottomRight.y(), preview->height());

    const QPointF p(333.5, 211.25);
    const QPointF back = r.panel.previewToDocument(r.panel.documentToPreview(p));
    EXPECT_NEAR(back.x(), p.x(), 1e-6);
    EXPECT_NEAR(back.y(), p.y(), 1e-6);
}

TEST(NavigatorPanel, PressInPreviewCentresTheCanvasOnThatPoint) {
    Rig r;
    r.canvas.setZoom(2.5, QPointF(400, 300));
    r.canvas.setRotation(30.0);
    r.canvas.toggleFlip();
    for (const QPointF& target : {QPointF(900, 300), QPointF(150, 700), QPointF(600, 400)}) {
        r.press(r.panel.documentToPreview(target));
        const QPointF c = centreOf(r.canvas.visibleDocumentArea());
        EXPECT_NEAR(c.x(), target.x(), 1e-6);
        EXPECT_NEAR(c.y(), target.y(), 1e-6);
    }
    r.press(QPointF(1, 1));
    const QPointF c = centreOf(r.canvas.visibleDocumentArea());
    EXPECT_NEAR(c.x(), 0.0, 1e-6);
    EXPECT_NEAR(c.y(), 0.0, 1e-6);
}

TEST(NavigatorPanel, WheelOverPreviewZoomsInSteps) {
    Rig r;
    QWidget* preview = r.find<QWidget>("navigatorPreview");
    r.canvas.actualPixels();
    const QPointF pos(50, 50);
    QWheelEvent up(pos, preview->mapToGlobal(pos), QPoint(), QPoint(0, 120), Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
    QCoreApplication::sendEvent(preview, &up);
    EXPECT_NEAR(r.canvas.zoom(), std::exp2(0.25), 1e-9);  // the next level of 2^(k/4)
    QWheelEvent down(pos, preview->mapToGlobal(pos), QPoint(), QPoint(0, -240), Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
    QCoreApplication::sendEvent(preview, &down);
    EXPECT_NEAR(r.canvas.zoom(), std::exp2(-0.25), 1e-9);
}

TEST(NavigatorPanel, SurvivesNoCanvasAndSwitchingCanvases) {
    TabletStats stats;
    Document docA(QSize(800, 600), Qt::white);
    Document docB(QSize(500, 500), Qt::white);
    CanvasView a(&docA, &stats);
    CanvasView b(&docB, &stats);
    a.resize(640, 480);
    b.resize(640, 480);
    a.resetView();
    b.resetView();
    b.setZoom(4.0, QPointF(320, 240));
    NavigatorPanel panel;
    panel.resize(300, 420);
    panel.show();
    auto* zoomSpin = panel.findChild<ValueBar*>(QStringLiteral("zoomBar"));
    auto* angleSpin = panel.findChild<ValueBar*>(QStringLiteral("angleBar"));
    auto* flip = panel.findChild<QToolButton*>(QStringLiteral("flipButton"));
    ASSERT_NE(zoomSpin, nullptr);

    EXPECT_EQ(panel.canvas(), nullptr);
    EXPECT_FALSE(zoomSpin->isEnabled());
    EXPECT_FALSE(angleSpin->isEnabled());
    EXPECT_FALSE(flip->isEnabled());
    EXPECT_TRUE(panel.previewImage().isNull());
    zoomSpin->setValue(150.0);
    angleSpin->setValue(10);
    EXPECT_EQ(panel.previewToDocument(QPointF(10, 10)), QPointF());

    panel.setCanvas(&a);
    EXPECT_TRUE(zoomSpin->isEnabled());
    EXPECT_NEAR(zoomSpin->value(), a.zoom() * 100.0, 0.051);
    EXPECT_FALSE(panel.previewImage().isNull());

    panel.setCanvas(&b);
    EXPECT_NEAR(zoomSpin->value(), 400.0, 1e-9);
    EXPECT_EQ(panel.previewImage().size() != QSize(), true);

    const double aZoom = a.zoom();
    a.setZoom(7.0, QPointF(320, 240));
    a.rotateBy(60.0);
    a.toggleFlip();
    EXPECT_NEAR(zoomSpin->value(), 400.0, 1e-9);
    EXPECT_EQ(angleSpin->value(), 0);
    EXPECT_FALSE(flip->isChecked());
    zoomSpin->setValue(250.0);
    EXPECT_NEAR(b.zoom(), 2.5, 1e-9);
    EXPECT_NEAR(a.zoom(), 7.0, 1e-12);
    EXPECT_NE(aZoom, 7.0);

    panel.setCanvas(nullptr);
    EXPECT_FALSE(zoomSpin->isEnabled());
    b.setZoom(1.0, QPointF(320, 240));
    EXPECT_NEAR(zoomSpin->value(), 250.0, 1e-9);
    zoomSpin->setValue(900.0);
    EXPECT_NEAR(b.zoom(), 1.0, 1e-12);

    auto gone = std::make_unique<CanvasView>(&docA, &stats);
    gone->resize(640, 480);
    panel.setCanvas(gone.get());
    EXPECT_TRUE(zoomSpin->isEnabled());
    gone.reset();
    EXPECT_EQ(panel.canvas(), nullptr);
    EXPECT_FALSE(zoomSpin->isEnabled());
    EXPECT_TRUE(panel.previewImage().isNull());
}

TEST(NavigatorPanel, PreviewRescalesOnlyWhatChangedAndCoalescesUpdates) {
    Rig r(QSize(2000, 2000));
    const LayerId layer = addPaintLayer(r.doc);
    const QImage& before = r.panel.previewImage();
    ASSERT_FALSE(before.isNull());
    EXPECT_EQ(r.panel.previewRebuilds(), 1);
    EXPECT_EQ(r.panel.previewPartialUpdates(), 0);
    const QPointF where = r.panel.documentToPreview(QPointF(130, 130));
    const QPoint inside(int(where.x()), int(where.y()));
    const QPoint inCache = inside - r.panel.documentToPreview(QPointF(0, 0)).toPoint();
    EXPECT_EQ(QColor(before.pixel(inCache)).red(), 255);
    EXPECT_EQ(QColor(before.pixel(inCache)).green(), 255);

    paintRect(r.doc, layer, QRect(100, 100, 60, 60), Qt::red);
    r.panel.flushPreview();
    EXPECT_EQ(r.panel.previewRebuilds(), 1);
    EXPECT_EQ(r.panel.previewPartialUpdates(), 1);
    const QImage& after = r.panel.previewImage();
    EXPECT_EQ(QColor(after.pixel(inCache)).red(), 255);
    EXPECT_LT(QColor(after.pixel(inCache)).green(), 40);
    EXPECT_EQ(QColor(after.pixel(after.width() - 2, after.height() - 2)).green(), 255);

    paintRect(r.doc, layer, QRect(1000, 1000, 40, 40), Qt::blue);
    paintRect(r.doc, layer, QRect(1030, 1020, 40, 40), Qt::blue);
    paintRect(r.doc, layer, QRect(1060, 1040, 40, 40), Qt::blue);
    EXPECT_EQ(r.panel.previewPartialUpdates(), 1);
    ASSERT_TRUE(waitUntil([&] { return r.panel.previewPartialUpdates() > 1; }));
    QThread::msleep(200);
    r.settle();
    EXPECT_EQ(r.panel.previewPartialUpdates(), 2);
    EXPECT_EQ(r.panel.previewRebuilds(), 1);
    const QPointF blue = r.panel.documentToPreview(QPointF(1050, 1030)) - r.panel.documentToPreview(QPointF(0, 0));
    EXPECT_GT(QColor(r.panel.previewImage().pixel(blue.toPoint())).blue(), 200);
    EXPECT_LT(QColor(r.panel.previewImage().pixel(blue.toPoint())).red(), 60);

    ASSERT_TRUE(r.doc.fillLayer(layer, Qt::green).ok());
    r.panel.flushPreview();
    EXPECT_EQ(r.panel.previewRebuilds(), 2);
    EXPECT_EQ(QColor(r.panel.previewImage().pixel(10, 10)).green(), 255);
    EXPECT_LT(QColor(r.panel.previewImage().pixel(10, 10)).red(), 40);

    r.panel.resize(360, 520);
    r.settle();
    r.panel.previewImage();
    r.panel.previewImage();
    EXPECT_EQ(r.panel.previewRebuilds(), 3);
}

// The changed part is scaled from the projection where it is, not from a copy of it: the same thumbnail as the copy gave.
TEST(NavigatorPanel, APartialUpdateIsTheChangedPartScaledAsACopyOfItWas) {
    Rig r(QSize(2000, 2000));
    const LayerId layer = addPaintLayer(r.doc);
    const QImage before = r.panel.previewImage().copy();
    ASSERT_FALSE(before.isNull());
    const QRect changed(333, 517, 610, 455);
    paintRect(r.doc, layer, changed, Qt::darkCyan);
    for (int x = changed.left(); x <= changed.right(); x += 7)
        paintRect(r.doc, layer, QRect(x, changed.top() + x % 11, 7, changed.height()).intersected(changed),
                  QColor::fromHsv(x % 360, 200, 40 + x % 200));
    const int partials = r.panel.previewPartialUpdates();
    r.panel.flushPreview();
    ASSERT_EQ(r.panel.previewPartialUpdates(), partials + 1);

    // What the partial update did before, with QImage::copy.
    QImage expected = before;
    const double sx = double(expected.width()) / 2000.0, sy = double(expected.height()) / 2000.0;
    const int x0 = int(std::floor(changed.left() * sx)), y0 = int(std::floor(changed.top() * sy));
    const QRect target =
        QRect(x0, y0, int(std::ceil((changed.right() + 1) * sx)) - x0, int(std::ceil((changed.bottom() + 1) * sy)) - y0) & expected.rect();
    const QRect source = QRectF(target.x() / sx, target.y() / sy, target.width() / sx, target.height() / sy).toAlignedRect() & r.doc.rect();
    const QRectF dest(source.x() * sx, source.y() * sy, source.width() * sx, source.height() * sy);
    const QSize pieceSize(std::max(1, int(std::lround(dest.width()))), std::max(1, int(std::lround(dest.height()))));
    const QImage piece = r.doc.projection().copy(source).scaled(pieceSize, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    QPainter p(&expected);
    p.setCompositionMode(QPainter::CompositionMode_Source);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    p.setClipRect(target);
    p.drawImage(dest, piece);
    p.end();
    EXPECT_EQ(r.panel.previewImage(), expected);
    EXPECT_NE(r.panel.previewImage(), before);
}

TEST(NavigatorPanel, PreviewWaitsForTheStrokeToEnd) {
    Rig r(QSize(2000, 2000));
    const LayerId layer = addPaintLayer(r.doc);
    ASSERT_FALSE(r.panel.previewImage().isNull());
    const int partials = r.panel.previewPartialUpdates();

    r.doc.setPainting(true);
    for (int i = 0; i < 5; ++i)
        paintRect(r.doc, layer, QRect(100 + i * 30, 100, 40, 40), Qt::red);
    QThread::msleep(200);
    r.settle();
    EXPECT_EQ(r.panel.previewPartialUpdates(), partials);

    r.doc.setPainting(false);  // the canvas's composite catches up a slice at a time on the event loop, then the preview follows
    r.settle();
    EXPECT_EQ(r.panel.previewPartialUpdates(), partials + 1);
    const QPointF red = r.panel.documentToPreview(QPointF(150, 120)) - r.panel.documentToPreview(QPointF(0, 0));
    EXPECT_LT(QColor(r.panel.previewImage().pixel(red.toPoint())).green(), 60);
}

// Renders the panel as it looks in the dark theme, for review by eye. The resources are not linked into the test
// executable, so the style sheet and icons are read from the source tree.
TEST(NavigatorPanel, SavesScreenshot) {
    test::applyTheme();

    Rig r(QSize(1600, 1200));
    const LayerId layer = addPaintLayer(r.doc);
    paintRect(r.doc, layer, QRect(100, 100, 600, 420), QColor(0xe0, 0x6c, 0x5f));
    paintRect(r.doc, layer, QRect(800, 200, 500, 500), QColor(0x5b, 0x8d, 0xef));
    paintRect(r.doc, layer, QRect(300, 700, 900, 300), QColor(0x7c, 0xc2, 0x7a));
    paintRect(r.doc, layer, QRect(1150, 800, 300, 300), QColor(0xf0, 0xc0, 0x4a));
    r.panel.resize(320, 420);
    r.canvas.setZoom(2.0, QPointF(400, 300));
    r.canvas.setRotation(25.0);
    r.canvas.centreOn(QPointF(560, 380));
    r.panel.flushPreview();
    r.settle();

    const QPixmap shot = r.panel.grab();
    EXPECT_FALSE(shot.isNull());
    EXPECT_TRUE(test::saveScreenshot(shot, QStringLiteral("navigator_panel")));
}
