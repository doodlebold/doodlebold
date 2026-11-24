#include "ui/panels/ToolPanel.h"

#include "core/color/ColorState.h"
#include "core/tools/ToolBox.h"
#include "ui/ButtonTips.h"
#include "ui/Components.h"
#include "ui/Theme.h"
#include "ui/panels/BrushPreviewStrip.h"
#include "ui/panels/BucketPanel.h"
#include "ui/panels/FixedToolPanels.h"
#include "ui/panels/RowHandle.h"
#include "ui/panels/ToolIcons.h"
#include "ui/panels/ToolParameters.h"
#include "ui/panels/ToolTray.h"

#include <QEvent>
#include <QHBoxLayout>
#include <QScrollArea>
#include <QScrollBar>
#include <QStackedWidget>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace doodlebold {

namespace {

constexpr int kSectionGap = Theme::kPanelSectionGap;

// Only where a fixed tool has a key (M, L, W); the others show the name only.
QString toolCommand(FixedTool t) {
    switch (t) {
    case FixedTool::Marquee: return QStringLiteral("tool.marquee");
    case FixedTool::Lasso: return QStringLiteral("tool.lasso");
    case FixedTool::MagicWand: return QStringLiteral("tool.wand");
    default: return {};
    }
}

// Scrolls its content up and down only, so it is never narrower than the content needs: a column dragged narrower
// stops there instead of cutting off the values at the right of the brush parameters.
class VerticalScrollArea : public QScrollArea {
public:
    using QScrollArea::QScrollArea;

    // When the content's layout changes (a section opened, a row shown), so may its least width: the panel is told, so it widens to
    // fit rather than cutting the rows off at the right.
    void watchContent() {
        if (widget())
            widget()->installEventFilter(this);
    }

    bool eventFilter(QObject* watched, QEvent* event) override {
        if (watched == widget() && event->type() == QEvent::LayoutRequest)
            updateGeometry();
        return QScrollArea::eventFilter(watched, event);
    }

    QSize minimumSizeHint() const override {
        QSize hint = QScrollArea::minimumSizeHint();
        if (widget())
            hint.setWidth(
                std::max(hint.width(), widget()->minimumSizeHint().width() + verticalScrollBar()->sizeHint().width() + 2 * frameWidth()));
        return hint;
    }
};

}

ToolPanel::ToolPanel(ToolBox* tools, BrushResourceLibrary* resources, QWidget* parent) : QWidget(parent), m_tools(tools) {
    auto* fixedRow = new QWidget(this);
    fixedRow->setObjectName(QStringLiteral("fixedToolsRow"));
    auto* fixedLayout = new QHBoxLayout(fixedRow);
    fixedLayout->setContentsMargins(0, 0, 0, 0);
    fixedLayout->setSpacing(0);
    for (FixedTool t : kFixedTools) {
        if (!m_fixedButtons.empty())
            fixedLayout->addStretch(1);
        auto* b = new QToolButton(fixedRow);
        b->setObjectName(QStringLiteral("fixedTool_%1").arg(fixedToolKey(t)));
        b->setCheckable(true);
        b->setIcon(fixedToolIcon(t));
        components::toolButton(b);
        button_tips::setCommandTip(b, fixedToolName(t), toolCommand(t));
        b->setProperty("role", "fixedTool");
        fixedLayout->addWidget(b);
        m_fixedButtons[t] = b;
        connect(b, &QToolButton::clicked, this, [this, t] { m_tools->setFixedTool(t); });
    }

    m_tray = new ToolTray(tools, this);
    m_preview = new BrushPreviewStrip(this);
    m_parameters = new ToolParameters(tools, resources, this);
    m_fixedPanels = new FixedToolPanels(tools, this);
    m_bucket = new BucketPanel(tools, this);

    m_options = new QStackedWidget(this);
    m_options->addWidget(m_parameters);
    m_options->addWidget(m_fixedPanels);
    m_options->addWidget(m_bucket);
    auto* optionsScroll = new VerticalScrollArea(this);
    m_optionsScroll = optionsScroll;
    m_optionsScroll->setObjectName(QStringLiteral("toolParametersScroll"));
    m_optionsScroll->setWidgetResizable(true);
    m_optionsScroll->setFrameShape(QFrame::NoFrame);
    m_optionsScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_optionsScroll->setWidget(m_options);
    optionsScroll->watchContent();

    // The handle fills the gap between the tray and the preview.
    m_trayHandle = new RowHandle(this);
    m_trayHandle->setObjectName(QStringLiteral("toolTrayHandle"));
    auto* trayAndPreview = new QVBoxLayout;
    trayAndPreview->setContentsMargins(0, 0, 0, 0);
    trayAndPreview->setSpacing(0);
    trayAndPreview->addWidget(m_tray);
    trayAndPreview->addWidget(m_trayHandle);
    trayAndPreview->addWidget(m_preview);

    const int pad = Theme::metric(Theme::Metric::Padding);
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(pad, pad, pad, pad);
    layout->setSpacing(kSectionGap);
    layout->addWidget(fixedRow);
    layout->addLayout(trayAndPreview);
    layout->addWidget(m_optionsScroll, 1);

    connect(m_trayHandle, &RowHandle::dragStarted, this, &ToolPanel::startTrayDrag);
    connect(m_trayHandle, &RowHandle::dragged, this, &ToolPanel::dragTray);

    connect(m_tray, &ToolTray::refused, this, &ToolPanel::refused);
    connect(m_parameters, &ToolParameters::refused, this, &ToolPanel::refused);
    for (const auto signal : {&ToolBox::settingsChanged, &ToolBox::currentSlotChanged})
        connect(tools, signal, this, &ToolPanel::updatePreview);
    connect(tools, &ToolBox::colorChanged, this, &ToolPanel::updatePreview);
    connect(tools, &ToolBox::transparentChanged, this, &ToolPanel::updatePreview);
    connect(tools, &ToolBox::activeToolChanged, this, &ToolPanel::syncActiveTool);
    connect(tools, &ToolBox::currentSlotChanged, this, &ToolPanel::syncActiveTool);
    updatePreview();
    syncActiveTool();
}

void ToolPanel::setColorState(ColorState* state) {
    if (m_colorState)
        disconnect(m_colorState, nullptr, this, nullptr);
    m_colorState = state;
    if (state)
        connect(state, &ColorState::changed, this, &ToolPanel::updatePreview);
    updatePreview();
}

// The tray shows a whole number of rows, from one to all of its group's, and takes no more than the parameters below it can give
// up: the panel never grows taller than its dock.
void ToolPanel::startTrayDrag() {
    m_trayDrag.rows = m_tray->shownRows();
    const int room = std::max(0, m_optionsScroll->height() - m_optionsScroll->minimumSizeHint().height());
    m_trayDrag.most = std::clamp(m_trayDrag.rows + room / m_tray->rowPitch(), m_trayDrag.rows, m_tray->groupRows());
}

void ToolPanel::dragTray(int dy) {
    const int rows = m_trayDrag.rows + int(std::lround(double(dy) / m_tray->rowPitch()));
    m_tray->setVisibleRows(std::clamp(rows, 1, std::max(1, m_trayDrag.most)));
}

void ToolPanel::updatePreview() {
    m_preview->setBrush(m_tools->settings(), m_tools->paintColor(), m_colorState ? m_colorState->hue() : -1.0);
}

void ToolPanel::syncActiveTool() {
    const auto fixed = m_tools->fixedTool();
    for (const auto& [tool, button] : m_fixedButtons)
        button->setChecked(fixed == tool);
    // The Bucket paints no strokes: its panel holds its own choices, with no brush preview.
    const bool bucket = !fixed && m_tools->currentTool().type() == ToolType::Bucket;
    m_options->setCurrentWidget(fixed    ? static_cast<QWidget*>(m_fixedPanels)
                                : bucket ? static_cast<QWidget*>(m_bucket)
                                         : static_cast<QWidget*>(m_parameters));
    m_preview->setVisible(!fixed && !bucket);
}

}
