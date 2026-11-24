#include "ui/panels/BucketPanel.h"

#include "core/tools/ToolBox.h"
#include "ui/Components.h"
#include "ui/Theme.h"
#include "ui/ValueBar.h"
#include "ui/panels/ControlTips.h"

#include <QButtonGroup>
#include <QCheckBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QRadioButton>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace doodlebold {

namespace {

using selection::WandMode;

// Each control comes with what it does, as its tip.
void radio(QButtonGroup* group, QVBoxLayout* into, const QString& text, const char* name, int id, const QString& tip) {
    auto* r = new QRadioButton(text, into->parentWidget());
    r->setObjectName(QString::fromLatin1(name));
    control_tips::set({r}, text, tip);
    group->addButton(r, id);
    into->addWidget(r);
}

QCheckBox* check(QVBoxLayout* into, const QString& text, const char* name, const QString& tip) {
    auto* c = new QCheckBox(text, into->parentWidget());
    c->setObjectName(QString::fromLatin1(name));
    control_tips::set({c}, text, tip);
    into->addWidget(c);
    return c;
}

// A label and its bar on one row; `label` is the label's width, so the bars start in line.
ValueBar* barRow(QVBoxLayout* into, const QString& text, const char* name, int max, const QString& suffix, int label, const QString& tip) {
    auto* row = new QHBoxLayout;
    row->setSpacing(Theme::kPanelColumnGap);
    auto* l = new QLabel(text, into->parentWidget());
    l->setMinimumWidth(label);
    auto* bar = new ValueBar(into->parentWidget());
    bar->setObjectName(QString::fromLatin1(name));
    bar->setRange(0, max);
    bar->setSuffix(suffix);
    bar->setWheelSteps(false);  // as the other panels: the wheel scrolls the panel
    control_tips::set({bar, l}, text, tip);
    row->addWidget(l);
    row->addWidget(bar, 1);
    into->addLayout(row);
    return bar;
}

}

BucketPanel::BucketPanel(ToolBox* tools, QWidget* parent) : QWidget(parent), m_tools(tools) {
    setObjectName(QStringLiteral("bucketPanel"));
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(Theme::kPanelRowGap);

    root->addWidget(components::sectionHeading(tr("Selection Mode"), this));
    m_modes = new QButtonGroup(this);
    radio(m_modes, root, tr("Enclosed Transparent Area"), "bucket_enclosed", int(WandMode::EnclosedTransparent),
          tr("Fills the see-through area around the click, up to the lines that close it in, and a little under their soft "
             "edges."));
    radio(m_modes, root, tr("Similar Colored Area"), "bucket_similar", int(WandMode::SimilarColour),
          tr("Fills the connected area around the click whose colour is close to the clicked pixel's."));
    radio(m_modes, root, tr("All Similar Colored Pixels"), "bucket_allSimilar", int(WandMode::AllSimilar),
          tr("Fills every pixel close to the clicked colour, anywhere on the layer, joined or not."));
    const QString thresholdText = tr("Color Threshold"), gapText = tr("Gap Tolerance");
    const int label = std::max(fontMetrics().horizontalAdvance(thresholdText), fontMetrics().horizontalAdvance(gapText));
    m_threshold = barRow(root, thresholdText, "bucketThresholdBar", 255, {}, label,
                         tr("How far a colour may differ from the clicked one and still be filled, from 0 (the same colour "
                            "only) to 255. For a transparent area, how solid a pixel may be and still count as see-through."));
    m_gap = barRow(root, gapText, "bucketGapBar", 100, tr(" px"), label,
                   tr("Gaps in the lines up to about this wide count as closed, so the fill doesn't leak out. From 0 to "
                      "100 px; 0 closes none."));

    root->addSpacing(Theme::kPanelSectionGap - Theme::kPanelRowGap);  // the sections' gap
    root->addWidget(components::sectionHeading(tr("Selection Source"), this));
    m_source = new QButtonGroup(this);
    radio(m_source, root, tr("Current Layer"), "bucket_sourceCurrent", 0, tr("Finds the area in the current layer only."));
    radio(m_source, root, tr("Merged Image"), "bucket_sourceMerged", 1,
          tr("Finds the area in all visible layers as they look together. The fill still goes on the current layer."));
    m_antialias = check(root, tr("Anti-Aliasing"), "bucketAntialiasCheck",
                        tr("Softens the fill's edge outward by a pixel, so it meets the lines without a jagged rim."));
    m_ignoreSelection = check(root, tr("Ignore Existing Selection"), "bucketIgnoreSelectionCheck",
                              tr("Fills past the selection's edge. Off, the fill stays inside the selection."));
    root->addStretch(1);

    connect(m_modes, &QButtonGroup::idClicked, this, [this](int id) { edit([id](BucketSettings& b) { b.mode = WandMode(id); }); });
    connect(m_threshold, &ValueBar::valueChanged, this,
            [this](double d) { edit([v = int(std::lround(d))](BucketSettings& b) { b.colourThreshold = v; }); });
    connect(m_gap, &ValueBar::valueChanged, this,
            [this](double d) { edit([v = int(std::lround(d))](BucketSettings& b) { b.gapTolerance = v; }); });
    connect(m_source, &QButtonGroup::idClicked, this, [this](int id) { edit([id](BucketSettings& b) { b.mergedSource = id == 1; }); });
    connect(m_antialias, &QCheckBox::toggled, this, [this](bool on) { edit([on](BucketSettings& b) { b.antialias = on; }); });
    connect(m_ignoreSelection, &QCheckBox::toggled, this, [this](bool on) { edit([on](BucketSettings& b) { b.ignoreSelection = on; }); });
    connect(tools, &ToolBox::settingsChanged, this, &BucketPanel::sync);
    connect(tools, &ToolBox::currentSlotChanged, this, &BucketPanel::sync);
    sync();
}

void BucketPanel::edit(const std::function<void(BucketSettings&)>& change) {
    if (m_syncing || !m_tools)
        return;
    BrushSettings s = m_tools->settings();
    change(s.bucket);
    m_tools->setSettings(s);
}

void BucketPanel::sync() {
    if (m_syncing || !m_tools)
        return;
    m_syncing = true;
    const BucketSettings& b = m_tools->settings().bucket;
    if (QAbstractButton* button = m_modes->button(int(b.mode)))
        button->setChecked(true);
    m_threshold->setValue(b.colourThreshold);
    m_gap->setValue(b.gapTolerance);
    if (QAbstractButton* button = m_source->button(b.mergedSource ? 1 : 0))
        button->setChecked(true);
    m_antialias->setChecked(b.antialias);
    m_ignoreSelection->setChecked(b.ignoreSelection);
    m_syncing = false;
}

}
