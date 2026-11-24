#include "core/document/Document.h"
#include "core/input/TabletStats.h"
#include "core/raster/TiledImage.h"
#include "ui/CanvasView.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QImage>
#include <QPainter>
#include <QRandomGenerator>

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdlib>
#include <memory>

using namespace doodlebold;

namespace {

// Each side of `canvas` is covered by the parts' shown rectangles exactly once, and each holds its shown part with the margin.
void expectCovers(const std::vector<CanvasView::TexturePart>& parts, const QSize& canvas, int largest) {
    const QRect whole(QPoint(0, 0), canvas);
    QRegion covered;
    qint64 area = 0;
    for (const CanvasView::TexturePart& part : parts) {
        EXPECT_LE(part.held.width(), largest);
        EXPECT_LE(part.held.height(), largest);
        EXPECT_EQ(part.held, part.shown.adjusted(-CanvasView::kTextureMargin, -CanvasView::kTextureMargin, CanvasView::kTextureMargin,
                                                 CanvasView::kTextureMargin) &
                                 whole);
        EXPECT_EQ(part.shown.left() % CanvasView::kTextureAlign, 0);
        EXPECT_EQ(part.shown.top() % CanvasView::kTextureAlign, 0);
        covered += part.shown;
        area += qint64(part.shown.width()) * part.shown.height();
    }
    EXPECT_EQ(covered, QRegion(whole));
    EXPECT_EQ(area, qint64(canvas.width()) * canvas.height());
}

// Noise with lines through it, so every way of sampling shows.
QImage pattern(const QSize& size) {
    QImage img(size, QImage::Format_ARGB32_Premultiplied);
    QRandomGenerator random(7);
    for (int y = 0; y < size.height(); ++y) {
        auto* row = reinterpret_cast<QRgb*>(img.scanLine(y));
        for (int x = 0; x < size.width(); ++x)
            row[x] = qPremultiply(QRgb(random.generate() | 0xff000000u));
    }
    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing);
    for (int i = 0; i < 40; ++i) {
        p.setPen(QPen(QColor::fromHsv(i * 9, 255, 200), 1 + i % 7));
        p.drawLine(QPointF(i * 53.0, 0), QPointF(size.width() - i * 31.0, size.height()));
    }
    return img;
}

struct View {
    TabletStats stats;
    Document doc;
    std::unique_ptr<CanvasView> canvas;

    View(const QImage& picture, int largestTexture) : doc(picture.size(), Qt::white) {
        doc.paintTarget(doc.currentLayerId()).value()->writeImage(picture, {0, 0});
        CanvasView::setLargestTextureForTesting(largestTexture);
        canvas = std::make_unique<CanvasView>(&doc, &stats);
        canvas->resize(640, 400);
        canvas->show();
        QElapsedTimer shown;
        shown.start();
        while (!canvas->isValid() && shown.elapsed() < 5000)
            QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        CanvasView::setLargestTextureForTesting(0);
        canvas->resetView();
    }

    QImage shot(double zoom, double degrees, bool fast, const QPointF& focus) {
        canvas->setFastZoomedOut(fast);
        canvas->resetView();
        canvas->setZoom(zoom, canvas->rect().center());
        canvas->centreOn(focus);
        canvas->setRotation(degrees);
        QCoreApplication::processEvents();
        return canvas->grabFramebuffer();
    }
};

int largestDifference(const QImage& a, const QImage& b) {
    int most = 0;
    for (int y = 0; y < a.height(); ++y)
        for (int x = 0; x < a.width(); ++x) {
            const QRgb p = a.pixel(x, y), q = b.pixel(x, y);
            most = std::max({most, std::abs(qRed(p) - qRed(q)), std::abs(qGreen(p) - qGreen(q)), std::abs(qBlue(p) - qBlue(q))});
        }
    return most;
}

}

TEST(CanvasDisplay, ACanvasThatFitsIsOneTexture) {
    const auto parts = CanvasView::textureLayout(QSize(3000, 2000), 16384);
    ASSERT_EQ(parts.size(), 1u);
    EXPECT_EQ(parts[0].shown, QRect(0, 0, 3000, 2000));
    EXPECT_EQ(parts[0].held, QRect(0, 0, 3000, 2000));
    EXPECT_EQ(CanvasView::textureLayout(QSize(16384, 16384), 16384).size(), 1u);
}

TEST(CanvasDisplay, ALargerCanvasIsCutWithinTheLimit) {
    for (const auto& [canvas, largest] : {std::pair{QSize(16384, 16384), 8192}, std::pair{QSize(20000, 300), 16384},
                                          std::pair{QSize(3000, 2000), 1024}, std::pair{QSize(2048, 1024), 1024}}) {
        SCOPED_TRACE(QStringLiteral("%1x%2 in %3").arg(canvas.width()).arg(canvas.height()).arg(largest).toStdString());
        const auto parts = CanvasView::textureLayout(canvas, largest);
        EXPECT_GT(parts.size(), 1u);
        expectCovers(parts, canvas, largest);
    }
    // As few cuts as fit: 16384 in 8192 takes three a side, each 5632 shown (11 × 512).
    const auto parts = CanvasView::textureLayout(QSize(16384, 16384), 8192);
    EXPECT_EQ(parts.size(), 9u);
    EXPECT_EQ(parts[0].shown, QRect(0, 0, 5632, 5632));
    EXPECT_EQ(parts[4].held, QRect(5632 - 256, 5632 - 256, 5632 + 512, 5632 + 512));
}

// A canvas cut into textures looks as it would in one, magnified, at its pixels, zoomed out with and without mipmaps and
// turned, right across the cuts.
TEST(CanvasDisplay, ACutCanvasLooksAsItWouldInOneTexture) {
    const QImage picture = pattern(QSize(2048, 1024));
    View whole(picture, 0);
    if (!whole.canvas->isValid() || whole.canvas->grabFramebuffer().isNull())
        GTEST_SKIP() << "no OpenGL on this platform";
    View cut(picture, 1024);
    const QPointF onACut(1024, 512);
    struct Shot {
        double zoom, degrees;
        bool fast;
        int tolerance;  // texture coordinates are rounded a little differently in a part than in the whole
    };
    for (const Shot& s : {Shot{2.5, 0, false, 1}, Shot{2.5, 30, false, 1}, Shot{1, 0, false, 1}, Shot{0.7, 0, false, 1},
                          Shot{0.4, 30, false, 1}, Shot{0.4, 0, true, 1}, Shot{0.1, 0, false, 1}, Shot{0.1, 0, true, 1}}) {
        SCOPED_TRACE(QStringLiteral("zoom %1, %2°%3").arg(s.zoom).arg(s.degrees).arg(s.fast ? ", fast" : "").toStdString());
        const QImage a = whole.shot(s.zoom, s.degrees, s.fast, onACut), b = cut.shot(s.zoom, s.degrees, s.fast, onACut);
        ASSERT_EQ(a.size(), b.size());
        EXPECT_LE(largestDifference(a, b), s.tolerance);
    }
}
