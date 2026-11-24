#include "platform/PenCursor.h"
#include "platform/Platform.h"

#include <gtest/gtest.h>

using namespace doodlebold;

TEST(Platform, DefaultSurfaceFormatRequestsDesktopGlCore) {
    const QSurfaceFormat fmt = platform::defaultSurfaceFormat();
    EXPECT_EQ(fmt.renderableType(), QSurfaceFormat::OpenGL);
    EXPECT_EQ(fmt.profile(), QSurfaceFormat::CoreProfile);
    EXPECT_EQ(fmt.version(), qMakePair(3, 3));
}

TEST(Platform, TheCanvasNeedsDesktopOpenGl33) {
    const auto context = [](int major, int minor, QSurfaceFormat::RenderableType type = QSurfaceFormat::OpenGL) {
        QSurfaceFormat f;
        f.setVersion(major, minor);
        f.setRenderableType(type);
        return f;
    };
    EXPECT_TRUE(platform::canDrawTheCanvas(context(3, 3)));
    EXPECT_TRUE(platform::canDrawTheCanvas(context(4, 6)));
    EXPECT_FALSE(platform::canDrawTheCanvas(context(3, 2)));
    EXPECT_FALSE(platform::canDrawTheCanvas(context(3, 0))) << "Qt's software OpenGL on Windows";
    EXPECT_FALSE(platform::canDrawTheCanvas(context(2, 1)));
    EXPECT_FALSE(platform::canDrawTheCanvas(context(3, 3, QSurfaceFormat::OpenGLES)));
}

TEST(PenCursor, OnlyOnWayland) {
    EXPECT_FALSE(platform::pen_cursor::available());
    platform::pen_cursor::set(Qt::BlankCursor);
    platform::pen_cursor::set(Qt::ArrowCursor);
}
