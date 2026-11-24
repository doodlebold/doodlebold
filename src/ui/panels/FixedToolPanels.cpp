#include "ui/panels/FixedToolPanels.h"

#include "ui/ButtonTips.h"
#include "ui/Components.h"
#include "ui/Theme.h"
#include "ui/ValueBar.h"
#include "ui/panels/ControlTips.h"

#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QHBoxLayout>
#include <QDoubleSpinBox>
#include <QFontDatabase>
#include <QGridLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QRadioButton>
#include <QSpinBox>
#include <QVBoxLayout>

#include <algorithm>
#include <array>
#include <initializer_list>
#include <cmath>
#include <functional>

namespace doodlebold {

namespace {

using Theme::Metric;
using transform::Interpolation;

int gap() {
    return Theme::metric(Metric::Gap);
}

QVBoxLayout* pageLayout(QWidget* page) {
    auto* l = new QVBoxLayout(page);
    l->setContentsMargins(0, 0, 0, 0);
    l->setSpacing(Theme::kPanelRowGap);
    return l;
}

// Before the next section's heading: the rows' gap and this make the sections'.
void addSectionBreak(QVBoxLayout* root) {
    root->addSpacing(Theme::kPanelSectionGap - Theme::kPanelRowGap);
}

// Each control comes with what it does, as its tip.
QRadioButton* radio(QButtonGroup* group, QVBoxLayout* into, const QString& text, const QString& name, int id, const QString& tip) {
    auto* r = new QRadioButton(text, into->parentWidget());
    r->setObjectName(name);
    control_tips::set({r}, text, tip);
    group->addButton(r, id);
    into->addWidget(r);
    return r;
}

struct BarRow {
    QWidget* row;
    ValueBar* bar;
};

BarRow barRow(QWidget* parent, const QString& label, const char* key, int max, const QString& tip, const QString& suffix = {}) {
    auto* row = new QWidget(parent);
    auto* l = new QHBoxLayout(row);
    l->setContentsMargins(0, 0, 0, 0);
    l->setSpacing(Theme::kPanelColumnGap);
    auto* text = new QLabel(label, row);
    auto* bar = new ValueBar(row);
    bar->setObjectName(QString::fromLatin1(key) + QStringLiteral("Bar"));
    bar->setRange(0, max);
    bar->setSuffix(suffix);
    bar->setWheelSteps(false);  // the tool settings change only by clicking or typing; the wheel scrolls the panel
    control_tips::set({bar, text}, label, tip);
    l->addWidget(text);
    l->addWidget(bar, 1);
    return {row, bar};
}

// Gives the rows' labels one width, so their bars start in line.
void alignLabels(std::initializer_list<QWidget*> rows) {
    int widest = 0;
    for (QWidget* row : rows)
        widest = std::max(widest, row->layout()->itemAt(0)->widget()->sizeHint().width());
    for (QWidget* row : rows)
        row->layout()->itemAt(0)->widget()->setFixedWidth(widest);
}

QString smoothEdgeTip() {
    return QCoreApplication::translate("FixedToolPanels", "Softens the selection's edge by a pixel, so what you paint or fill "
                                                          "inside it has no jagged rim.");
}

constexpr std::array<Interpolation, 5> kQualities = {Interpolation::Nearest, Interpolation::Bilinear, Interpolation::Bicubic,
                                                     Interpolation::Lanczos2, Interpolation::Lanczos3};

QComboBox* qualityCombo(QWidget* parent, const char* name) {
    auto* c = new QComboBox(parent);
    c->setObjectName(QString::fromLatin1(name));
    for (Interpolation q : kQualities) {
        const char* text = q == Interpolation::Nearest    ? QT_TR_NOOP("Blocky")
                           : q == Interpolation::Bilinear ? QT_TR_NOOP("Smooth")
                           : q == Interpolation::Bicubic  ? QT_TR_NOOP("Smoother")
                           : q == Interpolation::Lanczos2 ? QT_TR_NOOP("Sharp")
                                                          : QT_TR_NOOP("Sharper");
        c->addItem(QCoreApplication::translate("FixedToolPanels", text), int(q));
    }
    return c;
}

}

FixedToolPanels::FixedToolPanels(ToolBox* tools, QWidget* parent) : QStackedWidget(parent), m_tools(tools) {
    setObjectName(QStringLiteral("fixedToolPanels"));
    m_pages[std::size_t(FixedTool::Marquee)] = buildMarquee();
    m_pages[std::size_t(FixedTool::Lasso)] = buildLasso();
    m_pages[std::size_t(FixedTool::MagicWand)] = buildWand();
    for (FixedTool t : {FixedTool::Move, FixedTool::Zoom, FixedTool::Rotate, FixedTool::Hand, FixedTool::Eyedropper})
        m_pages[std::size_t(t)] = buildHint(t);
    m_pages[std::size_t(FixedTool::Text)] = buildText();
    for (QWidget* page : m_pages)
        addWidget(page);

    connect(tools, &ToolBox::fixedOptionsChanged, this, &FixedToolPanels::sync);
    connect(tools, &ToolBox::activeToolChanged, this, &FixedToolPanels::showPageFor);
    sync();
    showPageFor();
}

void FixedToolPanels::showPageFor() {
    if (const auto fixed = m_tools->fixedTool())
        setCurrentWidget(m_pages[std::size_t(*fixed)]);
}

void FixedToolPanels::edit(const std::function<void(FixedToolOptions&)>& change) {
    if (m_syncing)
        return;
    FixedToolOptions o = m_tools->fixedOptions();
    change(o);
    m_tools->setFixedOptions(o);
}

void FixedToolPanels::setTransformActive(bool active) {
    m_ok->setEnabled(active);
    m_cancel->setEnabled(active);
}

QWidget* FixedToolPanels::buildMarquee() {
    auto* page = new QWidget(this);
    page->setObjectName(QStringLiteral("marqueePage"));
    QVBoxLayout* root = pageLayout(page);

    m_cutAndMove = new QCheckBox(tr("Drag to Move the Selection"), page);
    m_cutAndMove->setObjectName(QStringLiteral("cutAndMoveCheck"));
    control_tips::set({m_cutAndMove}, m_cutAndMove->text(),
                      tr("Dragging inside the selection moves what's selected. Off, Ctrl+Shift+drag moves it."));
    root->addWidget(m_cutAndMove);

    m_modes = new QButtonGroup(page);
    const std::array<TransformMode, 4> modes = {TransformMode::Freeform, TransformMode::Resize, TransformMode::Deform,
                                                TransformMode::Rotate};
    for (TransformMode m : modes) {
        radio(m_modes, root, transformModeName(m), QStringLiteral("mode_%1").arg(transformModeKey(m)), int(m),
              m == TransformMode::Freeform ? tr("Transforms the selection by moving the box's corners freely.")
              : m == TransformMode::Resize ? tr("Resizes the selection from the box's handles; Shift keeps its proportions.")
              : m == TransformMode::Deform ? tr("Moves the box's corners one at a time; the edge handles skew it.")
                                           : tr("Turns the selection: drag outside the box; Shift snaps to 15°."));
    }
    auto* okRow = new QHBoxLayout;
    okRow->setSpacing(gap());
    m_ok = new QPushButton(tr("OK"), page);
    m_ok->setObjectName(QStringLiteral("transformOk"));
    button_tips::setKeyTip(m_ok, tr("Apply the transform"), QKeySequence(Qt::Key_Return));
    m_cancel = new QPushButton(tr("Cancel"), page);
    m_cancel->setObjectName(QStringLiteral("transformCancel"));
    button_tips::setKeyTip(m_cancel, tr("Drop the transform"), QKeySequence(Qt::Key_Escape));
    okRow->addWidget(m_ok);
    okRow->addWidget(m_cancel);
    root->addLayout(okRow);
    setTransformActive(false);

    const BarRow perspective =
        barRow(page, tr("Perspective"), "perspective", 100,
               tr("When the corners are moved freely, how far the picture bends as if seen at an angle, from 0 (flat) to "
                  "100 %."));
    m_perspectiveBar = perspective.bar;
    root->addWidget(perspective.row);

    // Flip and turn: exact, no resampling.
    const struct {
        const char* name;
        const char* text;
        const char* tip;
    } kActions[] = {{"reverseHorizontal", QT_TR_NOOP("Flip Horizontal"),
                     QT_TR_NOOP("Mirrors the selection, or the layer, left to right. Exact: nothing is blurred.")},
                    {"reverseVertical", QT_TR_NOOP("Flip Vertical"),
                     QT_TR_NOOP("Mirrors the selection, or the layer, top to bottom. Exact: nothing is blurred.")},
                    {"rotate90ccw", QT_TR_NOOP("Rotate 90° CCW"),
                     QT_TR_NOOP("Turns the selection, or the layer, a quarter turn anticlockwise. Exact: nothing is blurred.")},
                    {"rotate90cw", QT_TR_NOOP("Rotate 90° CW"),
                     QT_TR_NOOP("Turns the selection, or the layer, a quarter turn clockwise. Exact: nothing is blurred.")}};
    QPushButton* buttons[4];
    for (int i = 0; i < 4; ++i) {
        buttons[i] = new QPushButton(tr(kActions[i].text), page);
        buttons[i]->setObjectName(QString::fromLatin1(kActions[i].name));
        control_tips::set({buttons[i]}, tr(kActions[i].text), tr(kActions[i].tip));
        root->addWidget(buttons[i]);
    }
    connect(buttons[0], &QPushButton::clicked, this, [this] { emit reverseRequested(true); });
    connect(buttons[1], &QPushButton::clicked, this, [this] { emit reverseRequested(false); });
    connect(buttons[2], &QPushButton::clicked, this, [this] { emit rotate90Requested(false); });
    connect(buttons[3], &QPushButton::clicked, this, [this] { emit rotate90Requested(true); });

    addSectionBreak(root);
    root->addWidget(components::sectionHeading(tr("Quality"), page));
    auto* grid = new QHBoxLayout;
    grid->setSpacing(Theme::kPanelColumnGap);
    auto* labels = new QVBoxLayout;
    auto* combos = new QVBoxLayout;
    labels->setSpacing(Theme::kPanelRowGap);
    combos->setSpacing(Theme::kPanelRowGap);
    const char* kNames[3] = {"draggingCombo", "previewCombo", "finalCombo"};
    const QString kTitles[3] = {tr("Dragging"), tr("Preview"), tr("Final")};
    const QString kTips[3] = {tr("How the picture is redrawn while you drag a handle. Blocky is the quickest; Sharper keeps the "
                                 "finest detail."),
                              tr("How the picture is redrawn between drags, while the transform waits for OK."),
                              tr("How the picture is redrawn when the transform is applied. Blocky keeps hard pixel edges; "
                                 "the others smooth them.")};
    for (int i = 0; i < 3; ++i) {
        m_resample[i] = qualityCombo(page, kNames[i]);
        auto* l = new QLabel(kTitles[i], page);
        l->setBuddy(m_resample[i]);
        control_tips::set({m_resample[i], l}, kTitles[i], kTips[i]);
        labels->addWidget(l);
        combos->addWidget(m_resample[i]);
    }
    grid->addLayout(labels);
    grid->addLayout(combos, 1);
    root->addLayout(grid);
    root->addStretch(1);

    connect(m_cutAndMove, &QCheckBox::toggled, this, [this](bool on) { edit([on](FixedToolOptions& o) { o.marquee.cutAndMove = on; }); });
    connect(m_modes, &QButtonGroup::idClicked, this, [this](int id) {
        const TransformMode mode = TransformMode(id);
        edit([mode](FixedToolOptions& o) { o.marquee.transformMode = mode; });
        emit transformRequested(mode);
    });
    connect(m_ok, &QPushButton::clicked, this, &FixedToolPanels::transformApplied);
    connect(m_cancel, &QPushButton::clicked, this, &FixedToolPanels::transformCancelled);
    connect(m_perspectiveBar, &ValueBar::valueChanged, this,
            [this](double d) { edit([v = int(std::lround(d))](FixedToolOptions& o) { o.marquee.perspective = v; }); });
    for (int i = 0; i < 3; ++i)
        connect(m_resample[i], &QComboBox::activated, this, [this, i](int index) {
            const Interpolation q = Interpolation(m_resample[i]->itemData(index).toInt());
            edit([i, q](FixedToolOptions& o) { (i == 0 ? o.marquee.dragging : i == 1 ? o.marquee.preview : o.marquee.final) = q; });
        });
    return page;
}

QWidget* FixedToolPanels::buildLasso() {
    auto* page = new QWidget(this);
    page->setObjectName(QStringLiteral("lassoPage"));
    QVBoxLayout* root = pageLayout(page);
    m_lassoModes = new QButtonGroup(page);
    radio(m_lassoModes, root, tr("Freeform"), QStringLiteral("lasso_freeform"), int(LassoMode::Freeform),
          tr("Drag to draw the outline freehand."));
    radio(m_lassoModes, root, tr("Polygonal"), QStringLiteral("lasso_polygonal"), int(LassoMode::Polygonal),
          tr("Click from corner to corner to draw the outline in straight edges."));
    radio(m_lassoModes, root, tr("Mixed"), QStringLiteral("lasso_polygonalFreeform"), int(LassoMode::PolygonalFreeform),
          tr("Drag for freehand parts of the outline and click for straight edges."));
    m_lassoAa = new QCheckBox(tr("Smooth Edge"), page);
    m_lassoAa->setObjectName(QStringLiteral("lassoAntialiasCheck"));
    control_tips::set({m_lassoAa}, m_lassoAa->text(), smoothEdgeTip());
    root->addWidget(m_lassoAa);
    root->addStretch(1);

    connect(m_lassoModes, &QButtonGroup::idClicked, this,
            [this](int id) { edit([id](FixedToolOptions& o) { o.lasso.mode = LassoMode(id); }); });
    connect(m_lassoAa, &QCheckBox::toggled, this, [this](bool on) { edit([on](FixedToolOptions& o) { o.lasso.antialias = on; }); });
    return page;
}

QWidget* FixedToolPanels::buildWand() {
    auto* page = new QWidget(this);
    page->setObjectName(QStringLiteral("wandPage"));
    QVBoxLayout* root = pageLayout(page);

    root->addWidget(components::sectionHeading(tr("Select"), page));
    m_wandModes = new QButtonGroup(page);
    radio(m_wandModes, root, tr("Transparent Area"), QStringLiteral("wand_enclosed"), int(selection::WandMode::EnclosedTransparent),
          tr("Selects the see-through area around the click, up to the lines that close it in."));
    radio(m_wandModes, root, tr("Connected Similar Colors"), QStringLiteral("wand_similar"), int(selection::WandMode::SimilarColour),
          tr("Selects the connected area around the click whose colour is close to the clicked pixel's."));
    radio(m_wandModes, root, tr("All Similar Colors"), QStringLiteral("wand_allSimilar"), int(selection::WandMode::AllSimilar),
          tr("Selects every pixel close to the clicked colour, joined or not."));

    const BarRow transparency = barRow(page, tr("Transparency Limit"), "transparency", 255,
                                       tr("How solid a pixel may be and still count as see-through, from 0 (fully clear only) to 255."));
    m_transparencyRow = transparency.row;
    m_transparencyBar = transparency.bar;
    root->addWidget(m_transparencyRow);
    const BarRow threshold =
        barRow(page, tr("Color Tolerance"), "threshold", 255,
               tr("How far a colour may differ from the clicked one and still be selected, from 0 (the same colour only) "
                  "to 255."));
    m_thresholdRow = threshold.row;
    m_thresholdBar = threshold.bar;
    root->addWidget(m_thresholdRow);
    const BarRow gapRow = barRow(page, tr("Close Gaps"), "gap", 100,
                                 tr("Gaps in the lines up to about this wide count as closed, so the selection doesn't leak out. "
                                    "From 0 to 100 px; 0 closes none."),
                                 tr(" px"));
    m_gapBar = gapRow.bar;
    root->addWidget(gapRow.row);
    alignLabels({m_transparencyRow, m_thresholdRow, gapRow.row});

    addSectionBreak(root);
    root->addWidget(components::sectionHeading(tr("Source"), page));
    m_wandSource = new QButtonGroup(page);
    radio(m_wandSource, root, tr("Current Layer"), QStringLiteral("source_current"), int(WandSource::CurrentLayer),
          tr("Finds the area in the current layer only."));
    radio(m_wandSource, root, tr("All Visible Layers"), QStringLiteral("source_merged"), int(WandSource::MergedImage),
          tr("Finds the area in all visible layers as they look together."));
    m_wandAa = new QCheckBox(tr("Smooth Edge"), page);
    m_wandAa->setObjectName(QStringLiteral("wandAntialiasCheck"));
    control_tips::set({m_wandAa}, m_wandAa->text(), smoothEdgeTip());
    root->addWidget(m_wandAa);
    root->addStretch(1);

    connect(m_wandModes, &QButtonGroup::idClicked, this,
            [this](int id) { edit([id](FixedToolOptions& o) { o.wand.mode = selection::WandMode(id); }); });
    connect(m_transparencyBar, &ValueBar::valueChanged, this,
            [this](double d) { edit([v = int(std::lround(d))](FixedToolOptions& o) { o.wand.transparencyRange = v; }); });
    connect(m_thresholdBar, &ValueBar::valueChanged, this,
            [this](double d) { edit([v = int(std::lround(d))](FixedToolOptions& o) { o.wand.colourThreshold = v; }); });
    connect(m_gapBar, &ValueBar::valueChanged, this,
            [this](double d) { edit([v = int(std::lround(d))](FixedToolOptions& o) { o.wand.gapTolerance = v; }); });
    connect(m_wandSource, &QButtonGroup::idClicked, this,
            [this](int id) { edit([id](FixedToolOptions& o) { o.wand.source = WandSource(id); }); });
    connect(m_wandAa, &QCheckBox::toggled, this, [this](bool on) { edit([on](FixedToolOptions& o) { o.wand.antialias = on; }); });
    return page;
}

QWidget* FixedToolPanels::buildHint(FixedTool tool) {
    auto* page = new QWidget(this);
    page->setObjectName(QStringLiteral("hintPage_%1").arg(fixedToolKey(tool)));
    QVBoxLayout* root = pageLayout(page);
    QString text;
    switch (tool) {
    case FixedTool::Move: text = tr("Drag to move the layer, or the selected part of it. Arrow keys nudge."); break;
    case FixedTool::Zoom: text = tr("Drag right or up to zoom in, left or down to zoom out."); break;
    case FixedTool::Rotate: text = tr("Drag around the centre of the view to rotate it."); break;
    case FixedTool::Hand: text = tr("Drag to pan the view."); break;
    case FixedTool::Eyedropper: text = tr("Click or drag to pick a colour from the canvas."); break;
    default: break;
    }
    auto* l = new QLabel(text, page);
    l->setWordWrap(true);
    l->setProperty("role", "hint");
    root->addWidget(components::sectionHeading(fixedToolName(tool), page));
    root->addWidget(l);
    root->addStretch(1);
    return page;
}

QWidget* FixedToolPanels::buildText() {
    auto* page = new QWidget(this);
    page->setObjectName(QStringLiteral("textToolPanel"));
    auto* grid = new QGridLayout;
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setHorizontalSpacing(Theme::kPanelColumnGap);
    grid->setVerticalSpacing(Theme::kPanelRowGap);
    int row = 0;
    m_textVertical = new QCheckBox(tr("Vertical"), page);
    m_textVertical->setObjectName(QStringLiteral("textVertical"));
    control_tips::set({m_textVertical}, m_textVertical->text(), tr("Sets the text in columns, top to bottom, instead of rows."));
    grid->addWidget(new QLabel(tr("Layout"), page), row, 0);
    grid->addWidget(m_textVertical, row++, 1, 1, 2);
    m_textColor = new QWidget(page);
    m_textColor->setObjectName(QStringLiteral("textColor"));
    m_textColor->setFixedSize(32, Theme::compact::kControlSm);  // 32 px wide: a colour chip (component metric)
    m_textColor->setAutoFillBackground(true);
    auto* colorLabel = new QLabel(tr("Color"), page);
    control_tips::set({m_textColor, colorLabel}, tr("Color"), tr("The text's colour, the main colour: choose it in the Color panel."));
    grid->addWidget(colorLabel, row, 0);
    grid->addWidget(m_textColor, row++, 1, Qt::AlignLeft);
    m_textSize = new QDoubleSpinBox(page);
    m_textSize->setObjectName(QStringLiteral("textSize"));
    m_textSize->setRange(1.0, 1000.0);
    m_textSize->setDecimals(0);
    m_textSize->setSuffix(tr(" px"));
    m_textSize->setKeyboardTracking(false);
    auto* sizeLabel = new QLabel(tr("Size"), page);
    control_tips::set({m_textSize, sizeLabel}, tr("Size"), tr("The letters' height in pixels, from 1 to 1000."));
    grid->addWidget(sizeLabel, row, 0);
    grid->addWidget(m_textSize, row++, 1, 1, 2);
    m_textBold = new QCheckBox(tr("Bold"), page);
    m_textBold->setObjectName(QStringLiteral("textBold"));
    m_textItalic = new QCheckBox(tr("Italic"), page);
    m_textItalic->setObjectName(QStringLiteral("textItalic"));
    control_tips::set({m_textBold}, m_textBold->text(), tr("Sets the text in the font's bold."));
    control_tips::set({m_textItalic}, m_textItalic->text(), tr("Sets the text in the font's italic, slanted."));
    grid->addWidget(new QLabel(tr("Styles"), page), row, 0);
    grid->addWidget(m_textBold, row, 1);
    grid->addWidget(m_textItalic, row++, 2);
    m_textFit = new QCheckBox(tr("Snap Text to Pixels"), page);
    m_textFit->setObjectName(QStringLiteral("textFitToPixelGrid"));
    control_tips::set({m_textFit}, m_textFit->text(), tr("Lines the letters up with whole pixels, so small text stays crisp."));
    grid->addWidget(m_textFit, row++, 0, 1, 3);
    grid->setColumnStretch(2, 1);

    m_fonts = new QListWidget(page);
    m_fonts->setObjectName(QStringLiteral("textFonts"));
    m_fonts->setMinimumHeight(220);  // px: about ten font names (component metric)
    control_tips::set({m_fonts}, tr("Font"), tr("The text's font: click one to use it."));
    for (const QString& family : QFontDatabase::families()) {
        if (QFontDatabase::isPrivateFamily(family))
            continue;
        auto* item = new QListWidgetItem(family, m_fonts);
        QFont f(family);
        f.setPixelSize(15);  // each font shown in itself, larger than the panel's text: the user's fonts, not themed (ui-tokens: allow)
        item->setFont(f);
    }

    auto* column = new QVBoxLayout(page);
    column->setContentsMargins(0, 0, 0, 0);
    column->addLayout(grid);
    column->addWidget(m_fonts, 1);

    connect(m_textVertical, &QCheckBox::toggled, this, [this](bool on) { edit([on](FixedToolOptions& o) { o.text.vertical = on; }); });
    connect(m_textFit, &QCheckBox::toggled, this, [this](bool on) { edit([on](FixedToolOptions& o) { o.text.fitToPixelGrid = on; }); });
    connect(m_textBold, &QCheckBox::toggled, this, [this](bool on) { edit([on](FixedToolOptions& o) { o.text.bold = on; }); });
    connect(m_textItalic, &QCheckBox::toggled, this, [this](bool on) { edit([on](FixedToolOptions& o) { o.text.italic = on; }); });
    connect(m_textSize, &QDoubleSpinBox::valueChanged, this,
            [this](double px) { edit([px](FixedToolOptions& o) { o.text.sizePx = px; }); });
    connect(m_fonts, &QListWidget::currentTextChanged, this, [this](const QString& family) {
        if (!family.isEmpty())
            edit([family](FixedToolOptions& o) { o.text.font = family; });
    });
    connect(m_tools, &ToolBox::colorChanged, this, &FixedToolPanels::showTextColor);
    showTextColor();
    return page;
}

void FixedToolPanels::showTextColor() {
    QPalette p = m_textColor->palette();
    p.setColor(QPalette::Window, m_tools->color());
    m_textColor->setPalette(p);
}

void FixedToolPanels::sync() {
    if (m_syncing)
        return;
    m_syncing = true;
    const FixedToolOptions& o = m_tools->fixedOptions();
    m_cutAndMove->setChecked(o.marquee.cutAndMove);
    if (QAbstractButton* b = m_modes->button(int(o.marquee.transformMode)))
        b->setChecked(true);
    m_perspectiveBar->setValue(o.marquee.perspective);
    const Interpolation qualities[3] = {o.marquee.dragging, o.marquee.preview, o.marquee.final};
    for (int i = 0; i < 3; ++i)
        m_resample[i]->setCurrentIndex(m_resample[i]->findData(int(qualities[i])));

    if (QAbstractButton* b = m_lassoModes->button(int(o.lasso.mode)))
        b->setChecked(true);
    m_lassoAa->setChecked(o.lasso.antialias);

    if (QAbstractButton* b = m_wandModes->button(int(o.wand.mode)))
        b->setChecked(true);
    const bool enclosed = o.wand.mode == selection::WandMode::EnclosedTransparent;
    m_transparencyRow->setVisible(enclosed);  // Range of Transparency belongs to the transparent-area search,
    m_thresholdRow->setVisible(!enclosed);    // the colour threshold to the two similar-colour searches
    m_transparencyBar->setValue(o.wand.transparencyRange);
    m_thresholdBar->setValue(o.wand.colourThreshold);
    m_gapBar->setValue(o.wand.gapTolerance);
    if (QAbstractButton* b = m_wandSource->button(int(o.wand.source)))
        b->setChecked(true);
    m_wandAa->setChecked(o.wand.antialias);

    m_textVertical->setChecked(o.text.vertical);
    m_textFit->setChecked(o.text.fitToPixelGrid);
    m_textBold->setChecked(o.text.bold);
    m_textItalic->setChecked(o.text.italic);
    m_textSize->setValue(o.text.sizePx);
    const auto found = m_fonts->findItems(o.text.font, Qt::MatchExactly);
    m_fonts->setCurrentItem(found.isEmpty() ? m_fonts->item(0) : found.front());
    m_syncing = false;
}

}
