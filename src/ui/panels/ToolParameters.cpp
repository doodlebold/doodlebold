#include "ui/panels/ToolParameters.h"

#include "core/paint/StrokeMemory.h"

#include "ui/ButtonTips.h"
#include "ui/Components.h"
#include "ui/Theme.h"
#include "ui/ValueBar.h"
#include "ui/panels/BrushResourceSection.h"
#include "ui/panels/ControlTips.h"
#include "ui/panels/ParameterGrid.h"
#include "ui/panels/StabilizerCombo.h"

#include <QButtonGroup>
#include <QCheckBox>
#include <QCoreApplication>
#include <QDoubleSpinBox>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QPushButton>
#include <QRadioButton>
#include <QResizeEvent>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <array>
#include <cmath>

namespace doodlebold {

namespace {

using Theme::Metric;

constexpr int kButtonSize = Theme::compact::kControlMd;  // the pressure buttons' column: one tool button
constexpr int kMaxSize = BrushSettings::kMaxSize;  // 0.1 px units, as BrushSettings::size

constexpr int kColumnGap = kParameterColumnGap;
constexpr int kRowGap = kParameterRowGap;
constexpr int kGroupGap = Theme::kPanelSectionGap;
constexpr int kBoxGap = Theme::kSpace2;

int gap() {
    return Theme::metric(Metric::Gap);
}

// The size bar is logarithmic over 0.1 … 4000 px, so every size gets the same amount of travel (px; 0…1 along the bar).
double sizeToPosition(double px) {
    return std::log(std::max(px * 10.0, 1.0)) / std::log(double(kMaxSize));
}
double positionToSize(double p) {
    return std::max(1.0, std::round(std::exp(p * std::log(double(kMaxSize))))) / 10.0;
}

bool allTools(ToolType) {
    return true;
}
bool pixelArt(ToolType t) {
    return t == ToolType::PixelArtPen;
}
// Which tools show which controls.
bool hasDensity(ToolType t) {
    return !pixelArt(t);
}
bool brushShaped(ToolType t) {
    return !pixelArt(t);
}
bool hasBrushResources(ToolType t) {
    return !pixelArt(t);
}
bool clone(ToolType t) {
    return t == ToolType::Clone;
}
bool mixes(ToolType t) {
    return t == ToolType::Brush || t == ToolType::WaterColor || t == ToolType::Marker;
}
// The Marker has no Dilution or Blur Pressure.
bool dilutes(ToolType t) {
    return t == ToolType::Brush || t == ToolType::WaterColor;
}
bool blurs(ToolType t) {
    return t == ToolType::Blur;
}

QFrame* divider(QWidget* parent) {
    auto* line = new QFrame(parent);
    line->setFrameShape(QFrame::HLine);
    line->setFrameShadow(QFrame::Plain);
    line->setFixedHeight(Theme::kBorder);
    return line;
}

// The grid of rows: the pressure buttons' column 0 (a row without a button leaves it blank), the labels' column 1, the bars' 2.
QGridLayout* newGrid(QWidget* parent) {
    auto* grid = new QGridLayout;
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setHorizontalSpacing(kColumnGap);
    grid->setVerticalSpacing(kRowGap);
    // Keeps the column's width, and the gap after it, whether a row has a button or not: a column without a widget has neither.
    auto* blank = new QWidget(parent);
    blank->setFixedSize(kButtonSize, 0);
    grid->addWidget(blank, 0, 0);
    grid->setColumnStretch(2, 1);
    return grid;
}

}

class SectionHeader : public QToolButton {
public:
    explicit SectionHeader(const QString& title, QWidget* parent) : QToolButton(parent) {
        setObjectName(QStringLiteral("sectionHeader"));
        setText(QString(title).replace(QLatin1Char('&'), QStringLiteral("&&")));  // not a mnemonic
        setCheckable(true);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        setCursor(Qt::PointingHandCursor);
        button_tips::setButtonTip(this, QCoreApplication::translate("ToolParameters", "Show or hide %1").arg(title.toLower()));
    }

    QSize sizeHint() const override {
        return {fontMetrics().horizontalAdvance(text()) + kArrow + Theme::kSpace2, fontMetrics().height() + Theme::kSpace1};
    }
    QSize minimumSizeHint() const override { return {kArrow, sizeHint().height()}; }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const QColor ink = palette().color(QPalette::ButtonText);
        p.setPen(ink);
        p.setFont(font());
        const QRect textRect = rect().adjusted(0, 0, -(kArrow + Theme::kSpace2), 0);
        p.drawText(textRect, Qt::AlignLeft | Qt::AlignVCenter | Qt::TextShowMnemonic, text());
        const QPointF c(width() - kArrow / 2.0 - 1.0, height() / 2.0);
        const double r = kArrow / 4.0;
        QPainterPath chevron;
        if (isChecked()) {
            chevron.moveTo(c.x() - r, c.y() - r / 2);
            chevron.lineTo(c.x(), c.y() + r / 2);
            chevron.lineTo(c.x() + r, c.y() - r / 2);
        } else {
            chevron.moveTo(c.x() - r / 2, c.y() - r);
            chevron.lineTo(c.x() + r / 2, c.y());
            chevron.lineTo(c.x() - r / 2, c.y() + r);
        }
        p.setPen(QPen(ink, Theme::kGlyphStroke, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.setBrush(Qt::NoBrush);
        p.drawPath(chevron);
    }

private:
    static constexpr int kArrow = Theme::kIconSm;
};

class CollapsibleSection : public QWidget {
public:
    CollapsibleSection(const QString& title, QWidget* body, QWidget* parent, QWidget* folded = nullptr)
        : QWidget(parent), m_title(title), m_body(body), m_folded(folded) {
        m_header = new SectionHeader(title, this);
        m_header->setFont(Theme::font(Theme::TextRole::Overline));
        auto* layout = new QVBoxLayout(this);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(kRowGap);
        layout->addWidget(m_header);
        layout->addWidget(body);
        if (m_folded)
            layout->addWidget(m_folded);
        connect(m_header, &QToolButton::toggled, this, [this] { applyState(); });
        applyState();
    }

    QString title() const { return m_title; }
    bool isExpanded() const { return m_header->isChecked(); }
    void setExpanded(bool expanded) { m_header->setChecked(expanded); }
    QToolButton* header() const { return m_header; }

private:
    void applyState() {
        m_header->update();
        m_body->setVisible(isExpanded());
        if (m_folded)
            m_folded->setVisible(!isExpanded());
    }

    QString m_title;
    QToolButton* m_header;
    QWidget* m_body;
    QWidget* m_folded;
};

// A label that cuts a name too long for it short with an ellipsis instead of widening the panel; the whole name is its tool tip.
class ElidedLabel : public QLabel {
public:
    explicit ElidedLabel(QWidget* parent) : QLabel(parent) { setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred); }

    void setFullText(const QString& text) {
        m_full = text;
        setToolTip(text);
        elide();
    }
    QSize minimumSizeHint() const override { return {0, QLabel::minimumSizeHint().height()}; }

protected:
    void resizeEvent(QResizeEvent* event) override {
        QLabel::resizeEvent(event);
        elide();
    }

private:
    void elide() { setText(fontMetrics().elidedText(m_full, Qt::ElideRight, width())); }

    QString m_full;
};

// A row of the panel. `tip` says what the bar does; `tipName` names it there when the label alone doesn't, and the
// pressure button's own tip is `pressureName` and `pressureTip`.
struct ToolParameters::IntRow {
    const char* key = nullptr;
    const char* label = nullptr;
    int BrushSettings::* value = nullptr;
    int max = 0;
    bool percent = false;
    bool BrushSettings::* pressure = nullptr;
    const char* pressureName = nullptr;
    const char* pressureTip = nullptr;
    bool (*usedBy)(ToolType) = nullptr;
    const char* tip = nullptr;
    const char* tipName = nullptr;
};

namespace {

enum RowId {
    MinSize,
    Density,
    MinDensity,
    Opacity,
    Sharpness,
    Blending,
    Dilution,
    Persistence,
    BlurPressure,
    AmpDensity,
    BlurWidth,
    SensitivitySize,
    SensitivityDensity,
    SensitivityBlending,
    RowCount
};

}

ToolParameters::ToolParameters(ToolBox* tools, BrushResourceLibrary* resources, QWidget* parent) : QWidget(parent), m_tools(tools) {
    static const std::array<IntRow, RowCount> kRows = {{
        {"minSize", QT_TR_NOOP("Min Size"), &BrushSettings::minSize, 100, true, nullptr, nullptr, nullptr, allTools,
         QT_TR_NOOP("The width at the lightest touch, as a share of Size, when the size follows pressure. At 100 % pressure "
                    "doesn't change the width.")},
        {"density", QT_TR_NOOP("Density"), &BrushSettings::density, 100, true, &BrushSettings::pressureDensity,
         QT_TR_NOOP("Pressure changes the density"),
         QT_TR_NOOP("A lighter touch lays the paint more thinly, down to Min Density; full pressure lays the whole Density."), hasDensity,
         QT_TR_NOOP("How much paint the tip lays. At 100 % it covers what's under it; lower values are see-through.")},
        {"minDensity", QT_TR_NOOP("Min Density"), &BrushSettings::minDensity, 100, true, nullptr, nullptr, nullptr, hasDensity,
         QT_TR_NOOP("The density at the lightest touch, as a share of Density, when the density follows pressure.")},
        {"opacity", QT_TR_NOOP("Opacity"), &BrushSettings::opacity, 255, false, nullptr, nullptr, nullptr, pixelArt,
         QT_TR_NOOP("How solid each pixel the pen sets is, from 0 (clear) to 255 (solid).")},
        {"sharpness", QT_TR_NOOP("Sharpness"), &BrushSettings::sharpness, 100, true, nullptr, nullptr, nullptr, brushShaped,
         QT_TR_NOOP("How crisp the tip's edge is. Lower values soften it; 100 % is as crisp as the Shape allows.")},
        {"blending", QT_TR_NOOP("Blending"), &BrushSettings::blending, 100, true, &BrushSettings::pressureBlending,
         QT_TR_NOOP("Pressure changes the blending"),
         QT_TR_NOOP("Full pressure reaches Blending; a lighter touch mixes in less and lays more of your colour."), mixes,
         QT_TR_NOOP("How much of the paint under the brush each dab mixes in with your colour. At 0 % it lays your colour alone; "
                    "higher values mix in more of what it picks up, and at 100 % it only pushes that paint around.")},
        {"dilution", QT_TR_NOOP("Dilution"), &BrushSettings::dilution, 100, true, &BrushSettings::pressureDilution,
         QT_TR_NOOP("Pressure changes the dilution"), QT_TR_NOOP("Full pressure reaches Dilution; a lighter touch dilutes less."), dilutes,
         QT_TR_NOOP("How far paint picked up from the canvas keeps its own transparency. At 0 % it counts as solid; higher "
                    "values let the brush thin out over see-through parts.")},
        {"persistence", QT_TR_NOOP("Persistence"), &BrushSettings::persistence, 100, true, nullptr, nullptr, nullptr, mixes,
         QT_TR_NOOP("How long the colour the brush carries lingers from one dab to the next. Higher values drag colour "
                    "further along the stroke.")},
        {"blurPressure", QT_TR_NOOP("Blur Pressure"), &BrushSettings::blurPressure, 100, true, nullptr, nullptr, nullptr, dilutes,
         QT_TR_NOOP("Below this pressure the brush adds none of your colour and only blends the paint already there. 0 % "
                    "turns it off.")},
        {"ampDensity", QT_TR_NOOP("Amp. Density"), &BrushSettings::ampDensity, 100, true, nullptr, nullptr, nullptr, mixes,
         QT_TR_NOOP("Kept with the tool, from 0 to 100 %. For now it doesn't change how the paint goes on.")},
        // How far the Blur reaches: at 0 it hardly blurs, from 50 up it nearly flattens stripes at full pressure.
        // A saved width shows as saved.
        {"blurWidth", QT_TR_NOOP("Blur Width"), &BrushSettings::blurWidth, 100, true, nullptr, nullptr, nullptr, blurs,
         QT_TR_NOOP("How strongly the Blur softens what's under it. At 0 % it hardly blurs; from 50 % up it nearly evens out "
                    "fine detail at full pressure.")},
        {"sensitivitySize", QT_TR_NOOP("Size"), &BrushSettings::sensitivitySize, 200, true, nullptr, nullptr, nullptr, allTools,
         QT_TR_NOOP("How strongly pen pressure changes the width, from 0 to 200 %. At 100 % the width follows the pen as it "
                    "is; above, a lighter touch reaches full size; below, you have to press harder."),
         QT_TR_NOOP("Size sensitivity")},
        {"sensitivityDensity", QT_TR_NOOP("Density"), &BrushSettings::sensitivityDensity, 200, true, nullptr, nullptr, nullptr, hasDensity,
         QT_TR_NOOP("How strongly pen pressure changes the density, from 0 to 200 %. At 100 % it follows the pen as it is; "
                    "above, a lighter touch reaches full density; below, you have to press harder."),
         QT_TR_NOOP("Density sensitivity")},
        {"sensitivityBlending", QT_TR_NOOP("Blending"), &BrushSettings::sensitivityBlending, 200, true, nullptr, nullptr, nullptr, mixes,
         QT_TR_NOOP("How strongly pen pressure changes the blending, from 0 to 200 %. At 100 % it follows the pen as it is; "
                    "above, a lighter touch reaches full blending; below, you have to press harder."),
         QT_TR_NOOP("Blending sensitivity")},
    }};

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(kGroupGap);

    // A tool shows only the headings that are about it.
    Group& size = addGroup(tr("Size"), "sizeGroup");
    buildSizeRow(size);
    buildRow(size, kRows[MinSize]);

    Group& density = addGroup(tr("Density"), "densityGroup");
    for (RowId id : {Density, MinDensity, Opacity})
        buildRow(density, kRows[id]);

    Group& mixing = addGroup(tr("Paint mixing"), "mixingGroup");
    for (RowId id : {Blending, Dilution, Persistence, BlurPressure, AmpDensity})
        buildRow(mixing, kRows[id]);

    Group& blur = addGroup(tr("Blur"), "blurGroup");
    buildRow(blur, kRows[BlurWidth]);

    Group& edge = addGroup(tr("Edge"), "edgeGroup");
    buildEdgeRow(edge);
    buildRow(edge, kRows[Sharpness]);

    Group& cloneGroup = addGroup(tr("Clone"), "cloneGroup");
    buildCloneOptions(cloneGroup);

    std::vector<QGridLayout*> grids;
    grids.reserve(m_groups.size());
    for (const auto& group : m_groups)
        grids.push_back(group->grid);

    if (resources) {
        const auto apply = [this](const std::function<void(BrushSettings&)>& change) { edit(change); };
        m_formSection = new BrushResourceSection(resources, BrushResourceList::Content::Forms, apply, this);
        m_textureSection = new BrushResourceSection(resources, BrushResourceList::Content::Textures, apply, this);
        auto* boxes = new QWidget(this);
        auto* boxColumn = new QVBoxLayout(boxes);
        boxColumn->setContentsMargins(0, 0, 0, 0);
        boxColumn->setSpacing(kBoxGap);
        for (BrushResourceSection* section : {m_formSection, m_textureSection}) {
            boxColumn->addWidget(section);
            connect(section, &BrushResourceSection::refused, this, &ToolParameters::refused);
        }

        auto* summary = new QWidget(this);
        QGridLayout* summaryGrid = newGrid(summary);
        summary->setLayout(summaryGrid);
        int summaryRow = 0;
        const auto line = [&](const QString& label, const char* name, ElidedLabel*& value) {
            auto* l = new QLabel(label, summary);
            l->setProperty("role", "hint");
            value = new ElidedLabel(summary);
            value->setObjectName(QLatin1String(name));
            summaryGrid->addWidget(l, summaryRow, 1);
            summaryGrid->addWidget(value, summaryRow++, 2);
        };
        line(tr("Tip shape"), "tipShapeSummary", m_formSummary);
        line(tr("Texture"), "textureSummary", m_textureSummary);
        grids.push_back(summaryGrid);

        m_tipDivider = divider(this);
        root->addWidget(m_tipDivider);
        m_tipSection = new CollapsibleSection(tr("Tip shape & texture"), boxes, this, summary);
        m_tipSection->setObjectName(QStringLiteral("tipShapeSection"));
        root->addWidget(m_tipSection);
    }

    auto* sensitivities = new QWidget(this);
    QGridLayout* sensGrid = newGrid(sensitivities);
    sensitivities->setLayout(sensGrid);
    grids.push_back(sensGrid);
    buildRow(sensGrid, 0, kRows[SensitivitySize]);
    buildRow(sensGrid, 1, kRows[SensitivityDensity]);
    buildRow(sensGrid, 2, kRows[SensitivityBlending]);
    root->addWidget(divider(this));
    auto* pressureSection = new CollapsibleSection(tr("Pressure sensitivity"), sensitivities, this);
    pressureSection->setObjectName(QStringLiteral("pressureSection"));
    root->addWidget(pressureSection);

    auto* misc = new QWidget(this);
    auto* miscColumn = new QVBoxLayout(misc);
    miscColumn->setContentsMargins(0, 0, 0, 0);
    miscColumn->setSpacing(gap());
    const auto strokeSwitch = [&](const QString& text, const char* name, const QString& tip, bool BrushSettings::* field) {
        auto* c = new QCheckBox(text, misc);
        c->setObjectName(QString::fromLatin1(name));
        control_tips::set({c}, text, tip);
        miscColumn->addWidget(c);
        connect(c, &QCheckBox::toggled, this, [this, field](bool on) { edit([field, on](BrushSettings& s) { s.*field = on; }); });
        return c;
    };
    m_curveInterpolation = strokeSwitch(tr("Curve Interpolation"), "curveInterpolationCheck",
                                        tr("On, the stroke follows a smooth curve through the pen's samples; off, it runs straight from "
                                           "sample to sample."),
                                        &BrushSettings::curveInterpolation);
    m_disableDensityAssist = strokeSwitch(tr("Disable Density Control Assist"), "disableDensityAssistCheck",
                                          tr("Off, a line's tone follows the pressure evenly. On, each dab lays its share of Density and "
                                             "the dabs build up, so a light touch is darker and half pressure nearly solid."),
                                          &BrushSettings::disableDensityAssist);
    m_antiRipple = strokeSwitch(tr("Anti-Ripple"), "antiRippleCheck",
                                tr("On, a hard edge stays flat and solid to its last pixel or two; off, it falls away over several."),
                                &BrushSettings::antiRipple);
    auto* miscLayout = new QHBoxLayout;
    miscLayout->setSpacing(gap());
    miscColumn->addLayout(miscLayout);
    miscLayout->addWidget(new QLabel(tr("Stabilizer"), misc));
    m_stabilizer = new StabilizerCombo(true, misc);
    m_stabilizer->setObjectName(QStringLiteral("stabilizerCombo"));
    control_tips::set({m_stabilizer, miscLayout->itemAt(0)->widget()}, tr("Stabilizer"),
                      tr("Smooths the line by letting it trail behind the pen: higher levels, up to 20, give steadier lines with "
                         "more lag. Global uses the level in the quick bar."));
    miscLayout->addWidget(m_stabilizer, 1);
    root->addWidget(divider(this));
    auto* strokeSection = new CollapsibleSection(tr("Stroke"), misc, this);
    strokeSection->setObjectName(QStringLiteral("strokeSection"));
    root->addWidget(strokeSection);

    alignLabelColumns(grids, 1);

    // The brush settings change only by clicking, dragging or typing; the wheel scrolls the panel.
    for (ValueBar* bar : findChildren<ValueBar*>())
        bar->setWheelSteps(false);

    auto* buttons = new QHBoxLayout;
    buttons->setSpacing(kColumnGap);
    m_reserve = new QPushButton(tr("Reserve Settings"), this);
    m_reserve->setObjectName(QStringLiteral("reserveButton"));
    control_tips::set({m_reserve}, tr("Reserve Settings"),
                      tr("Remembers this tool's settings as they are now, so you can try others and come back with Recall "
                         "Settings."));
    m_recall = new QPushButton(tr("Recall Settings"), this);
    m_recall->setObjectName(QStringLiteral("recallButton"));
    control_tips::set({m_recall}, tr("Recall Settings"), tr("Goes back to the settings last reserved for this tool."));
    buttons->addWidget(m_reserve);
    buttons->addWidget(m_recall);
    root->addLayout(buttons);
    root->addStretch(1);

    connect(m_stabilizer, &StabilizerCombo::levelChanged, this,
            [this](const std::optional<StabilizerLevel>& level) { edit([level](BrushSettings& s) { s.stabilizer = level; }); });
    connect(m_reserve, &QPushButton::clicked, this, [this] {
        m_tools->reserveSettings();
        sync();
    });
    connect(m_recall, &QPushButton::clicked, this, [this] {
        if (const auto recalled = m_tools->recallSettings(); !recalled)
            emit refused(describe(recalled.error()));
    });
    connect(m_tools, &ToolBox::settingsChanged, this, &ToolParameters::sync);
    connect(m_tools, &ToolBox::currentSlotChanged, this, &ToolParameters::sync);
    sync();
}

ToolParameters::Group& ToolParameters::addGroup(const QString& title, const char* name) {
    auto group = std::make_unique<Group>();
    group->box = new QWidget(this);
    group->box->setObjectName(QLatin1String(name));
    auto* column = new QVBoxLayout(group->box);
    column->setContentsMargins(0, 0, 0, 0);
    column->setSpacing(kRowGap);
    column->addWidget(components::sectionHeading(title, group->box));
    group->grid = newGrid(group->box);
    column->addLayout(group->grid);
    static_cast<QVBoxLayout*>(layout())->addWidget(group->box);
    m_groups.push_back(std::move(group));
    return *m_groups.back();
}

QToolButton* ToolParameters::makePressureButton(const QString& name, const QString& explanation, QWidget* parent) {
    auto* b = new QToolButton(parent);
    b->setCheckable(true);
    b->setProperty("role", "pressure");  // the style sheet drops the padding
    b->setIcon(QIcon(QStringLiteral(":/icons/pressure.svg")));
    components::toolButton(b);
    control_tips::set({b}, name, explanation);
    return b;
}

void ToolParameters::buildSizeRow(Group& group) {
    const int row = group.rows++;
    group.usedBy.push_back(allTools);
    m_sizePressure = makePressureButton(tr("Pressure changes the size"),
                                        tr("A lighter touch draws thinner, down to Min Size; full pressure, the whole Size."), this);
    m_sizePressure->setObjectName(QStringLiteral("sizePressure"));
    m_sizeLabel = new QLabel(tr("Size"), this);
    m_sizeBar = new ValueBar(this);
    m_sizeBar->setObjectName(QStringLiteral("sizeBar"));
    m_sizeBar->setDecimals(1);
    m_sizeBar->setRange(0.1, kMaxSize / 10.0);
    m_sizeBar->setSuffix(tr(" px"));
    m_sizeBar->setMapping(sizeToPosition, positionToSize);
    control_tips::set({m_sizeBar, m_sizeLabel}, tr("Size"),
                      tr("The tip's width in pixels, from 0.1 to 4000. When the size follows pressure, this is the width at full "
                         "pressure."));
    group.grid->addWidget(m_sizePressure, row, 0);
    group.grid->addWidget(m_sizeLabel, row, 1);
    group.grid->addWidget(m_sizeBar, row, 2);

    connect(m_sizePressure, &QToolButton::toggled, this, [this](bool on) { edit([on](BrushSettings& s) { s.pressureSize = on; }); });
    connect(m_sizeBar, &ValueBar::valueChanged, this,
            [this](double px) { edit([px](BrushSettings& s) { s.size = int(std::lround(px * 10.0)); }); });
}

void ToolParameters::buildRow(Group& group, const IntRow& def) {
    buildRow(group.grid, group.rows++, def);
    group.usedBy.push_back(def.usedBy);
}

void ToolParameters::buildRow(QGridLayout* grid, int row, const IntRow& def) {
    RowUi ui;
    ui.def = &def;
    const QString key = QString::fromLatin1(def.key);
    if (def.pressure) {
        ui.pressure = makePressureButton(tr(def.pressureName), tr(def.pressureTip), this);
        ui.pressure->setObjectName(key + QStringLiteral("Pressure"));
        grid->addWidget(ui.pressure, row, 0);
        connect(ui.pressure, &QToolButton::toggled, this,
                [this, member = def.pressure](bool on) { edit([on, member](BrushSettings& s) { s.*member = on; }); });
    }
    ui.label = new QLabel(tr(def.label), this);
    ui.bar = new ValueBar(this);
    ui.bar->setObjectName(key + QStringLiteral("Bar"));
    ui.bar->setRange(0, def.max);
    if (def.percent)
        ui.bar->setSuffix(QStringLiteral("%"));
    control_tips::set({ui.bar, ui.label}, tr(def.tipName ? def.tipName : def.label), tr(def.tip));
    grid->addWidget(ui.label, row, 1);
    grid->addWidget(ui.bar, row, 2);

    connect(ui.bar, &ValueBar::valueChanged, this,
            [this, member = def.value](double v) { edit([v = int(std::lround(v)), member](BrushSettings& s) { s.*member = v; }); });
    m_rows.push_back(ui);
}

QButtonGroup* ToolParameters::addRadioPair(Group& group, const QString& label, const QString& first, const QString& second,
                                           const char* name) {
    const int row = group.rows;
    group.rows += 2;
    group.grid->addWidget(new QLabel(label, this), row, 1, Qt::AlignTop);
    auto* buttons = new QButtonGroup(group.box);
    int id = 0;
    for (const QString& text : {first, second}) {
        auto* b = new QRadioButton(text, this);
        b->setObjectName(QStringLiteral("%1%2").arg(QLatin1String(name)).arg(id));
        buttons->addButton(b, id);
        group.grid->addWidget(b, row + id, 2);
        ++id;
    }
    return buttons;
}

namespace {

constexpr int kEdgeIcon = Theme::kIconMd;
constexpr int kEdgeIconDetail =
    2;  // the Shape icons' pixels per icon pixel, so they stay sharp on screens at twice the scale (component metric)

// Each Shape as a disc of the same size, its rim blurred over a width stepped evenly from the 1st Shape's, which fades from near the
// middle, to the 5th's, crisp (feedback: "The pen shapes 1-5 should be circles ranging from soft to hard"). The stamp's own rim
// profiles were tried first: at 16 px the soft Shapes read as smaller, fainter dots rather than softer ones, and the 1st and 2nd out of
// order.
// In drawn pixels, kEdgeIconDetail to an icon pixel (32 across): the disc's outer edge, half a pixel inside the icon's, and the softest
// and crispest rims, the softest leaving a solid middle about a quarter of the disc across, the crispest one pixel, an antialiased edge.
constexpr double kEdgeDiscOuter = kEdgeIcon - 0.5;
constexpr double kSoftestRim = kEdgeIcon * 0.75;
constexpr double kCrispestRim = 1.0;

QIcon edgeIcon(int edge, const QColor& colour) {
    const int side = kEdgeIcon * kEdgeIconDetail;
    QImage picture(side, side, QImage::Format_ARGB32_Premultiplied);
    picture.fill(Qt::transparent);
    const double rim = kSoftestRim + (kCrispestRim - kSoftestRim) * edge / (kEdgePresets - 1);
    const double middle = side / 2.0;
    for (int y = 0; y < side; ++y)
        for (int x = 0; x < side; ++x) {
            const double distance = std::hypot(x + 0.5 - middle, y + 0.5 - middle);
            const double t = std::clamp((kEdgeDiscOuter - distance) / rim, 0.0, 1.0);  // 0 at the outer edge, 1 inside the rim
            QColor c = colour;
            c.setAlphaF(static_cast<float>(colour.alphaF() * t * t * (3.0 - 2.0 * t)));  // a smooth step, so the rim has no hard inner edge
            picture.setPixelColor(x, y, c);
        }
    picture.setDevicePixelRatio(kEdgeIconDetail);
    return QIcon(QPixmap::fromImage(picture));
}

}

namespace {

QString shapeTip() {
    return QCoreApplication::translate("ToolParameters", "The tip's edge, from the softest on the left, which fades over about a "
                                                         "quarter of the width, to the firmest on the right.");
}

}

void ToolParameters::buildEdgeRow(Group& group) {
    const int row = group.rows++;
    auto* buttons = new QWidget(this);
    auto* layout = new QHBoxLayout(buttons);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(gap());
    m_edgeGroup = new QButtonGroup(buttons);
    m_edgeGroup->setExclusive(true);
    for (int i = 0; i < kEdgePresets; ++i) {
        auto* b = new QToolButton(buttons);
        b->setObjectName(QStringLiteral("edgeButton%1").arg(i));
        b->setCheckable(true);
        b->setIcon(edgeIcon(i, palette().color(QPalette::Text)));
        components::toolButton(b);
        const QString name = i == 0                  ? tr("Edge Shape 1, the softest edge")
                             : i == kEdgePresets - 1 ? tr("Edge Shape %1, the firmest edge").arg(i + 1)
                                                     : tr("Edge Shape %1").arg(i + 1);
        control_tips::set({b}, name, shapeTip());
        m_edgeGroup->addButton(b, i);
        layout->addWidget(b);
    }
    layout->addStretch(1);
    auto* label = new QLabel(tr("Shape"), this);
    control_tips::set({label}, tr("Shape"), shapeTip());
    group.grid->addWidget(label, row, 1);
    group.grid->addWidget(buttons, row, 2);
    connect(m_edgeGroup, &QButtonGroup::idClicked, this, [this](int id) { edit([id](BrushSettings& s) { s.edge = id; }); });
}

void ToolParameters::buildCloneOptions(Group& group) {
    group.usedBy.push_back(clone);
    m_cloneAligned = new QCheckBox(tr("Aligned"), this);
    m_cloneAligned->setObjectName(QStringLiteral("cloneAlignedCheck"));
    control_tips::set({m_cloneAligned}, tr("Aligned"),
                      tr("On, every stroke keeps the same offset from the source, so a later stroke carries on where the last "
                         "one ended. Off, every stroke starts copying from the source point again."));
    group.grid->addWidget(m_cloneAligned, group.rows++, 1, 1, 2);
    m_cloneSample = addRadioPair(group, tr("Sample"), tr("Current Layer"), tr("All Layers"), "cloneSampleRadio");
    control_tips::set({m_cloneSample->button(0)}, tr("Current Layer"), tr("The Clone Stamp copies from the current layer only."));
    control_tips::set({m_cloneSample->button(1)}, tr("All Layers"),
                      tr("The Clone Stamp copies from all visible layers, as they look together."));
    connect(m_cloneAligned, &QCheckBox::toggled, this, [this](bool on) { edit([on](BrushSettings& s) { s.cloneAligned = on; }); });
    connect(m_cloneSample, &QButtonGroup::idClicked, this,
            [this](int id) { edit([id](BrushSettings& s) { s.cloneAllLayers = id == 1; }); });
}

void ToolParameters::edit(const std::function<void(BrushSettings&)>& change) {
    if (m_syncing)
        return;
    BrushSettings s = m_tools->settings();
    change(s);
    m_tools->setSettings(s);
}

void ToolParameters::setCanvasSize(QSize size) {
    if (size == m_canvas)
        return;
    m_canvas = size;
    showSizeLimit();
}

void ToolParameters::refreshSizeLimit() {
    showSizeLimit();
}

// Only where the size chosen is over what memory allows for this tool on this canvas, and worked out only when one of those changes.
void ToolParameters::showSizeLimit() {
    if (!m_tools)
        return;
    const std::optional<int> limit = m_canvas.isEmpty() ? std::nullopt : stroke_memory::sizeLimit(m_tools->settings(), m_canvas);
    if (!limit) {
        m_sizeBar->setLimit(std::nullopt);
        return;
    }
    m_sizeBar->setLimit(
        *limit / 10.0,
        tr("Limited to %1 px by this computer's memory. Options › Performance › Memory").arg(QString::number(*limit / 10.0)));
}

void ToolParameters::sync() {
    if (m_syncing)
        return;
    m_syncing = true;
    const BrushSettings& s = m_tools->settings();
    const ToolType type = s.tool;

    m_sizePressure->setChecked(s.pressureSize);
    m_sizeBar->setValue(s.size / 10.0);
    showSizeLimit();

    for (RowUi& r : m_rows) {
        r.bar->setValue(s.*(r.def->value));
        if (r.pressure)
            r.pressure->setChecked(s.*(r.def->pressure));
        const bool shown = r.def->usedBy(type);
        for (QWidget* w : {static_cast<QWidget*>(r.pressure), static_cast<QWidget*>(r.label), static_cast<QWidget*>(r.bar)})
            if (w)
                w->setVisible(shown);
    }
    for (const auto& group : m_groups)
        group->box->setVisible(std::any_of(group->usedBy.begin(), group->usedBy.end(), [type](auto used) { return used(type); }));
    if (QAbstractButton* edge = m_edgeGroup->button(s.edge))
        edge->setChecked(true);
    if (m_tipSection) {
        for (BrushResourceSection* section : {m_formSection, m_textureSection})
            section->showSettings(s);
        m_formSummary->setFullText(m_formSection->list()->currentName());
        m_textureSummary->setFullText(m_textureSection->list()->currentName());
        m_tipDivider->setVisible(hasBrushResources(type));
        m_tipSection->setVisible(hasBrushResources(type));
    }
    m_cloneAligned->setChecked(s.cloneAligned);
    m_cloneSample->button(s.cloneAllLayers ? 1 : 0)->setChecked(true);
    m_curveInterpolation->setChecked(s.curveInterpolation);
    m_disableDensityAssist->setChecked(s.disableDensityAssist);
    m_disableDensityAssist->setVisible(hasDensity(type));
    m_antiRipple->setChecked(s.antiRipple);
    m_antiRipple->setVisible(brushShaped(type));
    m_stabilizer->setLevel(s.stabilizer);
    m_recall->setEnabled(m_tools->currentTool().reserved.has_value());
    m_syncing = false;
}

}
