#pragma once

#include <QPointer>
#include <QWidget>

#include <array>

class QAction;
class QScrollArea;
class QToolButton;

namespace doodlebold {

class ColorAssets;
class ColorState;
class ColorWheelView;
class ToolBox;

enum class ColorView {
    Circle,
    Rgb,
    Hsv,
    Swatches
};
inline constexpr int kColorViewCount = 4;

class ColorPanel : public QWidget {
    Q_OBJECT
public:
    ColorPanel(ToolBox* tools, ColorAssets* assets, QWidget* parent = nullptr);

    ColorState* state() const { return m_state; }
    QWidget* view(ColorView v) const { return m_views[std::size_t(v)]; }
    QToolButton* toggle(ColorView v) const { return m_toggles[std::size_t(v)]; }
    static QString viewId(ColorView v);
    static bool shownAtStart(ColorView v) { return v == ColorView::Circle; }
    void bindToggle(ColorView v, QAction* action);
    // The panel is never taller than that (the tool panel below takes the rest) and scrolls when it gets less.
    int preferredHeight() const;
    int preferredWidth() const;
    void fitHeight();
    QSize sizeHint() const override;
    QToolButton* colorSwatch() const { return m_swatch; }
    QToolButton* secondarySwatch() const { return m_secondary; }
    QToolButton* exchangeButton() const { return m_exchange; }
    QToolButton* transparentButton() const { return m_transparent; }

signals:
    void sizeWanted();
    // Sent before the panel takes its new size limits: the views are about to change height. The dock notes its sizes.
    void heightChanging();

protected:
    // Virtual so tests can answer without a dialog.
    virtual QColor askColor(const QColor& initial);
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void showColor(const QColor& color);
    void showSecondaryColor(const QColor& color);
    void showTransparent(bool on);
    void placeColorTools();

    QPointer<ToolBox> m_tools;
    ColorState* m_state;
    QToolButton* m_swatch = nullptr;
    QToolButton* m_secondary = nullptr;
    QToolButton* m_exchange = nullptr;
    QToolButton* m_transparent = nullptr;
    std::array<QWidget*, kColorViewCount> m_views{};
    std::array<QToolButton*, kColorViewCount> m_toggles{};
    QScrollArea* m_scroll = nullptr;
    QWidget* m_column = nullptr;
    ColorWheelView* m_wheel = nullptr;
    int m_fittedHeight = 0;
};

}
