#include "ui/QuickBar.h"

#include "ui/Theme.h"

#include "core/tools/ToolBox.h"
#include "core/view/ViewState.h"
#include "core/view/ViewText.h"
#include "ui/ButtonTips.h"
#include "ui/CanvasView.h"
#include "ui/panels/StabilizerCombo.h"

#include <QAbstractButton>
#include <QAction>
#include <QComboBox>
#include <QLabel>
#include <QSignalBlocker>
#include <QStyleOptionComboBox>
#include <QStylePainter>
#include <QToolButton>

#include <cmath>

namespace doodlebold {

namespace {

// A list that shows a text of its own: the zoom and angle lists show the view's exact value ("118%", "33.8°"), which is
// usually not one of the entries. Not editable.
class ReadoutCombo : public QComboBox {
public:
    using QComboBox::QComboBox;

    void setReadout(const QString& text) {
        if (text == m_text)
            return;
        m_text = text;
        update();
    }
    const QString& readout() const { return m_text; }

protected:
    void paintEvent(QPaintEvent*) override {
        QStylePainter painter(this);
        QStyleOptionComboBox option;
        initStyleOption(&option);
        option.currentText = m_text;
        option.currentIcon = QIcon();
        painter.drawComplexControl(QStyle::CC_ComboBox, option);
        painter.drawControl(QStyle::CE_ComboBoxLabel, option);
    }

private:
    QString m_text;
};

ReadoutCombo* readoutCombo(QComboBox* c) {
    return static_cast<ReadoutCombo*>(c);
}

}

const std::vector<double>& QuickBar::zoomFactors() {
    static const std::vector<double> factors = {1.0 / 128, 1.0 / 64, 1.0 / 32, 1.0 / 16, 1.0 / 8, 1.0 / 4, 1.0 / 2,
                                                1.0,       2.0,      4.0,      8.0,      16.0,    32.0};
    return factors;
}

const std::vector<int>& QuickBar::angleSteps() {
    static const std::vector<int> steps = {-180, -135, -90, -45, 0, 45, 90, 135, 180};
    return steps;
}

QuickBar::QuickBar(ToolBox* tools, QWidget* parent) : QToolBar(tr("Quick Bar"), parent), m_tools(tools) {
    setIconSize(QSize(Theme::kIconMd, Theme::kIconMd));
    setObjectName(QStringLiteral("quickBar"));
    setMovable(false);
    setFloatable(false);
    setToolButtonStyle(Qt::ToolButtonIconOnly);
}

void QuickBar::addButton(QAction* action, const QString& icon, const QString& tip) {
    action->setIcon(QIcon(icon));
    action->setToolTip(tip);
    addAction(action);
    if (auto* button = qobject_cast<QAbstractButton*>(widgetForAction(action)))
        button_tips::setButtonTip(button, tip, action);
}

void QuickBar::populate(const Commands& c) {
    addButton(c.undo, QStringLiteral(":/icons/undo.svg"), tr("Undo"));
    addButton(c.redo, QStringLiteral(":/icons/redo.svg"), tr("Redo"));
    addSeparator();

    addButton(c.deselect, QStringLiteral(":/icons/deselect.svg"), tr("Deselect"));
    addButton(c.invertSelection, QStringLiteral(":/icons/invert-selection.svg"), tr("Invert Selection"));
    c.showSelection->setIconText(tr("Selection"));
    addButton(c.showSelection, QStringLiteral(":/icons/visible.svg"), tr("Show/Hide Selection"));
    static_cast<QToolButton*>(widgetForAction(c.showSelection))->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    addSeparator();

    m_zoom = new ReadoutCombo(this);
    m_zoom->setObjectName(QStringLiteral("zoomCombo"));
    m_zoom->setToolTip(tr("Pick a zoom for the current canvas"));
    for (const double factor : zoomFactors())
        m_zoom->addItem(view_text::zoomText(factor), factor);
    m_zoom->setMinimumContentsLength(7);
    m_zoom->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    m_zoom->setMaxVisibleItems(m_zoom->count());
    addWidget(m_zoom);
    addButton(c.zoomOut, QStringLiteral(":/icons/zoom-out.svg"), tr("Zoom Out"));
    addButton(c.zoomIn, QStringLiteral(":/icons/zoom-in.svg"), tr("Zoom In"));
    addButton(c.resetView, QStringLiteral(":/icons/reset-view.svg"), tr("Reset View"));
    addSeparator();

    m_angle = new ReadoutCombo(this);
    m_angle->setObjectName(QStringLiteral("angleCombo"));
    m_angle->setToolTip(tr("Pick an angle for the current canvas"));
    for (const int degrees : angleSteps())
        m_angle->addItem(view_text::angleListText(degrees), degrees);
    m_angle->setMinimumContentsLength(7);
    m_angle->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    m_angle->setMaxVisibleItems(m_angle->count());
    addWidget(m_angle);
    addButton(c.rotateCcw, QStringLiteral(":/icons/rotate-ccw.svg"), tr("Rotate View Counterclockwise"));
    addButton(c.rotateCw, QStringLiteral(":/icons/rotate-cw.svg"), tr("Rotate View Clockwise"));
    addButton(c.resetAngle, QStringLiteral(":/icons/reset-angle.svg"), tr("Reset Angle"));
    addSeparator();

    addButton(c.flip, QStringLiteral(":/icons/flip-h.svg"), tr("Flip View Horizontally"));
    addSeparator();

    // Stabilizer: the global level; a tool with its own level overrides it.
    addWidget(new QLabel(tr(" Stabilizer "), this));
    m_stabilizer = new StabilizerCombo(false, this);
    m_stabilizer->setObjectName(QStringLiteral("globalStabilizer"));
    m_stabilizer->setToolTip(tr("Stroke stabilizer level (a tool can have its own)"));
    m_stabilizer->setLevel(m_tools->options().stabilizer);
    addWidget(m_stabilizer);
    connect(m_stabilizer, &StabilizerCombo::levelChanged, this, [this](const std::optional<StabilizerLevel>& level) {
        BrushOptions o = m_tools->options();
        o.stabilizer = *level;
        m_tools->setOptions(o);
    });
    connect(m_tools, &ToolBox::optionsChanged, this, [this] { m_stabilizer->setLevel(m_tools->options().stabilizer); });

    connect(m_zoom, &QComboBox::activated, this, [this](int index) {
        if (m_canvas && index >= 0)
            m_canvas->setZoom(m_zoom->itemData(index).toDouble(), m_canvas->viewCentre());
        syncReadouts();
    });
    connect(m_angle, &QComboBox::activated, this, [this](int index) {
        if (m_canvas && index >= 0)
            m_canvas->setRotation(m_angle->itemData(index).toInt());
        syncReadouts();
    });
    setCanvas(nullptr);
}

void QuickBar::setCanvas(CanvasView* canvas) {
    disconnect(m_viewConnection);
    m_canvas = canvas;
    if (canvas)
        m_viewConnection = connect(canvas, &CanvasView::viewChanged, this, &QuickBar::syncReadouts);
    if (m_zoom) {
        m_zoom->setEnabled(canvas != nullptr);
        m_angle->setEnabled(canvas != nullptr);
    }
    syncReadouts();
}

// The lists show the actual zoom and angle; the entry that equals it (if any) is the current one.
void QuickBar::syncReadouts() {
    if (!m_zoom)
        return;
    const QSignalBlocker zoomBlock(m_zoom), angleBlock(m_angle);
    if (!m_canvas) {
        readoutCombo(m_zoom)->setReadout(QString());
        readoutCombo(m_angle)->setReadout(QString());
        m_zoom->setCurrentIndex(-1);
        m_angle->setCurrentIndex(-1);
        return;
    }
    const double zoom = m_canvas->zoom();
    readoutCombo(m_zoom)->setReadout(view_text::zoomText(zoom));
    int zoomIndex = -1;
    for (int i = 0; i < m_zoom->count(); ++i)
        if (std::abs(m_zoom->itemData(i).toDouble() - zoom) < 1e-9 * zoom)
            zoomIndex = i;
    m_zoom->setCurrentIndex(zoomIndex);

    const double angle = m_canvas->rotation();
    readoutCombo(m_angle)->setReadout(view_text::angleText(angle));
    const int whole = static_cast<int>(std::lround(angle));
    m_angle->setCurrentIndex(std::abs(angle - whole) < 1e-9 ? m_angle->findData(whole) : -1);
}

QString QuickBar::zoomReadout() const {
    return readoutCombo(m_zoom)->readout();
}

QString QuickBar::angleReadout() const {
    return readoutCombo(m_angle)->readout();
}

}
