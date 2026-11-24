#pragma once

#include <QObject>
#include <QRegion>

#include <functional>

class QApplication;
class QWidget;
class QWindow;

namespace doodlebold {

// Frosted popups are made translucent when first polished (before the window exists, so it gets an alpha
// channel), marked glass=true for the style sheet's rules, and ask for blur behind their rounded shape each time they
// are shown or resized. Only where the desktop can blur: a translucent popup without blur would show the canvas sharp
// through its text.
class GlassPopups : public QObject {
    Q_OBJECT
public:
    using Blur = std::function<bool(QWindow* window, const QRegion& region)>;

    explicit GlassPopups(Blur blur, QObject* parent = nullptr);
    ~GlassPopups() override;

    static bool install(QApplication& app);
    // Options > Performance > Frosted menus: off, popups are made plain and opaque. A popup that's hidden changes
    // at once, one on screen once it closes. Nothing changes where no GlassPopups is installed (the desktop can't blur).
    static void setFrosted(bool on);
    static bool frosted();

    static bool isGlassKind(const QWidget* widget);
    static QRegion shape(const QSize& size, int radius);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    static void makeGlass(QWidget* popup);
    static void makePlain(QWidget* popup);
    static void follow(QWidget* popup);
    void blurBehind(QWidget* popup);

    Blur m_blur;
};

}
