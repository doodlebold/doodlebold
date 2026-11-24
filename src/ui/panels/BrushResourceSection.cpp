#include "ui/panels/BrushResourceSection.h"

#include "ui/ValueBar.h"
#include "ui/panels/ControlTips.h"
#include "ui/panels/ParameterGrid.h"

#include <QCheckBox>
#include <QComboBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPushButton>
#include <QVBoxLayout>

#include <cmath>
#include <tuple>

namespace doodlebold {

namespace {

constexpr int kBoxPadding = Theme::kSpace3;
constexpr int kCheckTextGap = Theme::kSpace2;  // between a switch's box and its text

// A switch's text, wrapped to the column: a click on it toggles the switch, and it greys with it.
class WrappedCheckText : public QLabel {
public:
    WrappedCheckText(const QString& text, QCheckBox* box, QWidget* parent) : QLabel(text, parent), m_box(box) {
        setWordWrap(true);
        setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        setMinimumWidth(fontMetrics().horizontalAdvance(QStringLiteral("Transparency")));
        box->installEventFilter(this);
        setEnabled(box->isEnabled());
    }

protected:
    void mouseReleaseEvent(QMouseEvent* event) override {
        if (m_box->isEnabled() && rect().contains(event->position().toPoint()))
            m_box->click();
        QLabel::mouseReleaseEvent(event);
    }
    bool eventFilter(QObject* watched, QEvent* event) override {
        if (watched == m_box && event->type() == QEvent::EnabledChange)
            setEnabled(m_box->isEnabled());
        return QLabel::eventFilter(watched, event);
    }

private:
    QCheckBox* m_box;
};
const QString kPercent = QStringLiteral("%");

}

BrushResourceSection::BrushResourceSection(BrushResourceLibrary* library, BrushResourceList::Content content, Edit edit, QWidget* parent)
    : QWidget(parent), m_content(content), m_library(library), m_edit(std::move(edit)) {
    const QString prefix = forms() ? QStringLiteral("form") : QStringLiteral("texture");
    setObjectName(prefix + QStringLiteral("Section"));
    setProperty("role", "insetBox");
    setAttribute(Qt::WA_StyledBackground);

    auto* title = new QLabel(forms() ? tr("Tip shape") : tr("Texture"), this);
    title->setProperty("role", "boxTitle");

    m_list = new BrushResourceList(library, content, this);
    m_list->setObjectName(forms() ? QStringLiteral("brushFormList") : QStringLiteral("brushTextureList"));
    m_list->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    if (forms())
        control_tips::set({m_list}, tr("Tip shape"),
                          tr("The picture the tip stamps along the stroke in place of a plain circle. Drop an image here to "
                             "import it."));
    else
        control_tips::set({m_list}, tr("Texture"),
                          tr("A paper grain the stroke takes on, fixed to the canvas so it stays put as the tip moves. Drop an "
                             "image here to import it."));

    auto* strengthRow = new QGridLayout;
    strengthRow->setContentsMargins(0, 0, 0, 0);
    strengthRow->setHorizontalSpacing(kParameterColumnGap);
    strengthRow->setColumnStretch(1, 1);
    m_strengthLabel = new QLabel(this);
    m_strengthLabel->setObjectName(prefix + QStringLiteral("StrengthLabel"));
    m_strength = new ValueBar(this);
    m_strength->setObjectName(prefix + QStringLiteral("StrengthBar"));
    m_strength->setRange(0, 100);
    m_strength->setSuffix(kPercent);
    strengthRow->addWidget(m_strengthLabel, 0, 0);
    strengthRow->addWidget(m_strength, 0, 1);

    std::vector<QGridLayout*> grids{strengthRow};
    const auto block = [this, &grids]() {
        auto* w = new QWidget(m_body);
        auto* g = new QGridLayout(w);
        g->setContentsMargins(0, 0, 0, 0);
        g->setHorizontalSpacing(kParameterColumnGap);
        g->setVerticalSpacing(kParameterRowGap);
        g->setColumnStretch(1, 1);
        grids.push_back(g);
        return w;
    };
    m_body = new QWidget(this);
    auto* bodyLayout = new QVBoxLayout(m_body);
    bodyLayout->setContentsMargins(0, 0, 0, 0);
    bodyLayout->setSpacing(kParameterRowGap);
    m_mainBody = block();
    bodyLayout->addWidget(m_mainBody);
    const auto bar = [&](QWidget* where, const QString& label, const QString& name, double minimum, double maximum, const QString& tip,
                         const QString& suffix = kPercent) {
        auto* g = static_cast<QGridLayout*>(where->layout());
        auto* b = new ValueBar(where);
        b->setObjectName(name);
        b->setRange(minimum, maximum);
        b->setSuffix(suffix);
        const int row = g->rowCount();
        auto* l = new QLabel(label, where);
        control_tips::set({b, l}, label, tip);
        g->addWidget(l, row, 0);
        g->addWidget(b, row, 1);
        return std::pair(b, l);
    };
    // A switch sits in the value column, under the bars, its text on as many lines as the column needs: "Invert for Transparency" on one
    // line made the box wider than the Tools panel, and the panel's rows ran out of view at the right (a screenshot of
    // 2026-11-20). The text is a label beside the box that toggles it, greyed with it.
    const auto check = [&](QWidget* where, const QString& text, const QString& name, const QString& tip) {
        auto* g = static_cast<QGridLayout*>(where->layout());
        auto* row = new QWidget(where);
        auto* h = new QHBoxLayout(row);
        h->setContentsMargins(0, 0, 0, 0);
        h->setSpacing(kCheckTextGap);
        auto* c = new QCheckBox(row);
        c->setObjectName(name);
        c->setAccessibleName(text);
        auto* l = new WrappedCheckText(text, c, row);
        control_tips::set({c, l}, text, tip);
        h->addWidget(c, 0, Qt::AlignTop);
        h->addWidget(l, 1);
        g->addWidget(row, g->rowCount(), 1);
        return c;
    };
    const auto combo = [&](QWidget* where, const QString& label, const QString& name, const QString& tip) {
        auto* g = static_cast<QGridLayout*>(where->layout());
        auto* c = new QComboBox(where);
        c->setObjectName(name);
        c->addItems({tr("None"), tr("Auto"), tr("Pen Direction")});
        const int row = g->rowCount();
        auto* l = new QLabel(label, where);
        control_tips::set({c, l}, label, tip);
        g->addWidget(l, row, 0);
        g->addWidget(c, row, 1);
        return c;
    };
    std::tie(m_scale, m_scaleLabel) =
        bar(m_mainBody, tr("Scale"), prefix + QStringLiteral("ScaleBar"), 10, 500,
            forms() ? tr("The picture's size against the tip's width, from 10 to 500 %: at 100 % it fits the width.")
                    : tr("How large the grain is, from 10 to 500 %: at 100 % the picture keeps its own size."));
    if (!forms())
        m_scratch = bar(m_mainBody, tr("Scratch Prs."), prefix + QStringLiteral("ScratchBar"), 0, 100,
                        tr("How much more of the paper a light touch leaves bare, from 0 to 100 %: the lighter the pressure, the deeper "
                           "into the paper's low places it reaches. With density on pressure only."))
                        .first;
    m_invert = check(m_mainBody, tr("Invert"), prefix + QStringLiteral("InvertCheck"),
                     forms() ? tr("Swaps light and dark in the picture. Normally its dark parts paint; inverted, its light parts do.")
                             : tr("Swaps light and dark in the picture. Normally its light parts take paint, as a paper's tops do; "
                                  "inverted, its dark parts do."));
    if (!forms())
        m_eraseLow = check(m_mainBody, tr("Invert for Transparency"), prefix + QStringLiteral("EraseLowCheck"),
                           tr("When the tool erases (the Eraser, or any tool painting the transparent colour): on, it takes most from "
                              "the paper's low places, where painting lays least; off, most from the tops, as painting lays paint."));
    if (forms()) {
        m_formEraseLow = check(m_mainBody, tr("Invert for Transparency"), prefix + QStringLiteral("EraseLowCheck"),
                               tr("When the tool erases (the Eraser, or any tool painting the transparent colour): on, the tip shape is "
                                  "turned over, as Invert does. With ordinary paint it does nothing."));
        m_sharpen = check(m_mainBody, tr("Sharpen"), prefix + QStringLiteral("SharpenCheck"),
                          tr("A blot map's torn edge, crisper: each dab is worked out at its exact place, not slid there from its whole "
                             "pixel."));
        m_bristleBody = block();
        bodyLayout->addWidget(m_bristleBody);
        m_bristleMinimum = bar(m_bristleBody, tr("Hair Min"), QStringLiteral("bristleMinimumBar"), 0, 100,
                               tr("How thin each hair gets under a light touch, as a share of its full width, as Min Size is the size's. "
                                  "With Size on pressure the hairs widen with the pressure up to their full width; at 100 % they keep it."))
                               .first;
        m_bristleRandomize = bar(m_bristleBody, tr("Randomize"), QStringLiteral("bristleRandomizeBar"), 0, 20,
                                 tr("Each hair wanders slowly about its place, the middle hairs most, and lags the turn a little at high "
                                    "values; new every stroke. 0 keeps every hair in its place."))
                                 .first;
        m_bristleDirection =
            combo(m_bristleBody, tr("Direction"), QStringLiteral("bristleDirectionCombo"),
                  tr("Which way the hairs lie. Auto: turned with the stroke, the map's top along the way it goes. None: as "
                     "for a stroke going right, whichever way it goes. Pen Direction: the way the pen leans; with a mouse "
                     "as None."));
        m_bristleDetectTurn =
            check(m_bristleBody, tr("Detect Turn"), QStringLiteral("bristleDetectTurnCheck"),
                  tr("With Direction Auto: at a sharp corner the hairs stop and start afresh the new way, crossing, rather "
                     "than swinging round it."));

        m_shapeBody = block();
        bodyLayout->addWidget(m_shapeBody);
        m_shapeAngleControl = combo(m_shapeBody, tr("Angle Control"), QStringLiteral("shapeAngleControlCombo"),
                                    tr("How the shape turns: fixed at Angle, or along the stroke, turned by Angle from the way it "
                                       "goes. With the pen is kept with the tool, but turns it by Angle alone for now."));
        m_shapeAngle = bar(m_shapeBody, tr("Angle"), QStringLiteral("shapeAngleBar"), -180, 180, tr("Turns the shape, from -180° to 180°."),
                           QStringLiteral("°"))
                           .first;
        m_shapeAngleJitter = bar(m_shapeBody, tr("Ang. Jitter"), QStringLiteral("shapeAngleJitterBar"), 0, 100,
                                 tr("Turns each stamp by a random amount: at 100 % anywhere round."))
                                 .first;
        m_shapeScaling = bar(m_shapeBody, tr("Scaling"), QStringLiteral("shapeScalingBar"), 1, 200,
                             tr("Sizes the shape against the tip, from 1 to 200 %: at 100 % it fits the tip's width."))
                             .first;
        m_shapeSizeJitter = bar(m_shapeBody, tr("Size Jitter"), QStringLiteral("shapeSizeJitterBar"), 0, 100,
                                tr("Makes each stamp larger or smaller at random, by up to this share of its size."))
                                .first;
        m_shapeSpacing = bar(m_shapeBody, tr("Spacing"), QStringLiteral("shapeSpacingBar"), 1, 50,
                             tr("How far apart the stamps go, from 1 to 50 % of the tip's size."))
                             .first;
        m_shapeScattering =
            bar(m_shapeBody, tr("Scattering"), QStringLiteral("shapeScatteringBar"), 0, 50,
                tr("Moves each stamp off the stroke's line at random, across and along it, by up to this share of the tip's size."))
                .first;
        m_shapeHighQuality = check(m_shapeBody, tr("HQ Down Scaling"), QStringLiteral("shapeHighQualityCheck"),
                                   tr("Where a stamp is much smaller than its picture: on, each pixel averages the picture over its area; "
                                      "off, it blends the two nearest halved copies of the picture, which is faster."));
    }
    alignLabelColumns(grids, 0);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(kBoxPadding, kBoxPadding, kBoxPadding, kBoxPadding);
    layout->setSpacing(kParameterRowGap);
    layout->addWidget(title);
    layout->addWidget(m_list);
    layout->addLayout(strengthRow);
    layout->addWidget(m_body);
    // At the box's foot, outside its body, so it works whatever the body shows, "Round" or "None" chosen too.
    m_reset = new QPushButton(tr("Reset"), this);
    m_reset->setObjectName(prefix + QStringLiteral("ResetButton"));
    control_tips::set({m_reset}, tr("Reset"),
                      forms() ? tr("Puts this tool's tip shape settings back to their starting values, after asking. The texture, size, "
                                   "colour, Shape and Sharpness stay as they are.")
                              : tr("Puts this tool's texture settings back to their starting values, after asking. The tip shape, size, "
                                   "colour, Shape and Sharpness stay as they are."));
    layout->addWidget(m_reset);
    connect(m_reset, &QPushButton::clicked, this, &BrushResourceSection::askToReset);
    applyKind();

    const auto apply = [this](const std::function<void(BrushSettings&)>& change) {
        if (!m_showing)
            m_edit(change);
    };
    const bool f = forms();
    connect(m_list, &BrushResourceList::refused, this, &BrushResourceSection::refused);
    connect(m_list, &BrushResourceList::chosen, this, [this, apply, f](const QString& name) {
        // A brush shape brings its settings file's settings, as the reference's does.
        const BrushResource* r = f && m_library ? m_library->form(name) : nullptr;
        const std::optional<ShapeSettings> shape = r ? r->shapeSettings : std::nullopt;
        apply([f, name, shape](BrushSettings& s) {
            (f ? s.form : s.texture) = name;
            if (shape)
                applyShapeSettings(s, *shape);
        });
        applyKind();
        applyEnabled();
    });
    connect(m_strength, &ValueBar::valueChanged, this, [this, apply, f](double v) {
        const ResourceKind kind = formKind();
        apply([f, kind, v = int(std::lround(v))](BrushSettings& s) {
            if (!f)
                s.textureStrength = v;
            else if (kind == ResourceKind::Bristle)
                s.bristleSize = v;
            else if (kind == ResourceKind::Shape)
                s.shapeDensity = v;
            else
                s.formStrength = v;
        });
    });
    const auto editInt = [this, apply](ValueBar* b, int BrushSettings::* field) {
        connect(b, &ValueBar::valueChanged, this,
                [apply, field](double v) { apply([field, v = int(std::lround(v))](BrushSettings& s) { s.*field = v; }); });
    };
    const auto editFlag = [this, apply](QCheckBox* c, bool BrushSettings::* field) {
        connect(c, &QCheckBox::toggled, this, [apply, field](bool on) { apply([field, on](BrushSettings& s) { s.*field = on; }); });
    };
    const auto editChoice = [this, apply](QComboBox* c, int BrushSettings::* field) {
        connect(c, &QComboBox::activated, this,
                [apply, c, field](int) { apply([field, v = c->currentIndex()](BrushSettings& s) { s.*field = v; }); });
    };
    editInt(m_scale, f ? &BrushSettings::formScale : &BrushSettings::textureScale);
    editFlag(m_invert, f ? &BrushSettings::formInvert : &BrushSettings::textureInvert);
    if (m_eraseLow)
        editFlag(m_eraseLow, &BrushSettings::textureEraseLow);
    if (m_scratch)
        editInt(m_scratch, &BrushSettings::textureScratch);
    if (m_formEraseLow)
        editFlag(m_formEraseLow, &BrushSettings::formEraseLow);
    if (m_sharpen)
        editFlag(m_sharpen, &BrushSettings::formSharpen);
    if (f) {
        editInt(m_bristleMinimum, &BrushSettings::bristleMinimum);
        editInt(m_bristleRandomize, &BrushSettings::bristleRandomize);
        editChoice(m_bristleDirection, &BrushSettings::bristleDirection);
        editFlag(m_bristleDetectTurn, &BrushSettings::bristleDetectTurn);
        editChoice(m_shapeAngleControl, &BrushSettings::shapeAngleControl);
        editInt(m_shapeAngle, &BrushSettings::shapeAngle);
        editInt(m_shapeAngleJitter, &BrushSettings::shapeAngleJitter);
        editInt(m_shapeScaling, &BrushSettings::shapeScaling);
        editInt(m_shapeSizeJitter, &BrushSettings::shapeSizeJitter);
        editInt(m_shapeSpacing, &BrushSettings::shapeSpacing);
        editInt(m_shapeScattering, &BrushSettings::shapeScattering);
        editFlag(m_shapeHighQuality, &BrushSettings::shapeHighQuality);
    }
}

// The box's Reset: only once the user confirms, as one change of the tool's settings. Cancel, or Escape, leaves them.
void BrushResourceSection::askToReset() {
    const QString prefix = forms() ? QStringLiteral("form") : QStringLiteral("texture");
    QMessageBox box(QMessageBox::Question, forms() ? tr("Reset Tip Shape") : tr("Reset Texture"),
                    forms() ? tr("Reset this tool's tip shape settings to their starting values?")
                            : tr("Reset this tool's texture settings to their starting values?"),
                    QMessageBox::NoButton, this);
    box.setObjectName(prefix + QStringLiteral("ResetDialog"));
    QPushButton* reset = box.addButton(tr("Reset"), QMessageBox::AcceptRole);
    reset->setObjectName(prefix + QStringLiteral("ResetConfirm"));
    QPushButton* cancel = box.addButton(QMessageBox::Cancel);
    cancel->setObjectName(prefix + QStringLiteral("ResetCancel"));
    box.setDefaultButton(cancel);
    box.setEscapeButton(cancel);
    box.exec();
    if (box.clickedButton() == reset)
        m_edit(forms() ? resetTipShape : resetTexture);
}

void BrushResourceSection::showSettings(const BrushSettings& s) {
    m_showing = true;
    const bool f = forms();
    m_list->setCurrentName(f ? s.form : s.texture);
    applyKind();
    switch (f ? formKind() : ResourceKind::Texture) {
    case ResourceKind::Bristle: m_strength->setValue(s.bristleSize); break;
    case ResourceKind::Shape: m_strength->setValue(s.shapeDensity); break;
    case ResourceKind::Texture:
    case ResourceKind::PaperTexture: m_strength->setValue(s.textureStrength); break;
    case ResourceKind::BlotMap: m_strength->setValue(s.formStrength); break;
    }
    m_scale->setValue(f ? s.formScale : s.textureScale);
    m_invert->setChecked(f ? s.formInvert : s.textureInvert);
    if (m_eraseLow)
        m_eraseLow->setChecked(s.textureEraseLow);
    if (m_scratch)
        m_scratch->setValue(s.textureScratch);
    if (m_formEraseLow)
        m_formEraseLow->setChecked(s.formEraseLow);
    if (m_sharpen)
        m_sharpen->setChecked(s.formSharpen);
    if (f) {
        m_bristleMinimum->setValue(s.bristleMinimum);
        m_bristleRandomize->setValue(s.bristleRandomize);
        m_bristleDirection->setCurrentIndex(s.bristleDirection);
        m_bristleDetectTurn->setChecked(s.bristleDetectTurn);
        m_shapeAngleControl->setCurrentIndex(s.shapeAngleControl);
        m_shapeAngle->setValue(s.shapeAngle);
        m_shapeAngleJitter->setValue(s.shapeAngleJitter);
        m_shapeScaling->setValue(s.shapeScaling);
        m_shapeSizeJitter->setValue(s.shapeSizeJitter);
        m_shapeSpacing->setValue(s.shapeSpacing);
        m_shapeScattering->setValue(s.shapeScattering);
        m_shapeHighQuality->setChecked(s.shapeHighQuality);
    }
    applyEnabled();
    m_showing = false;
}

ResourceKind BrushResourceSection::formKind() const {
    const BrushResource* r = m_library ? m_library->form(m_list->currentName()) : nullptr;
    return r ? r->kind : ResourceKind::BlotMap;
}

void BrushResourceSection::applyKind() {
    QString label;
    QString tip = tr("How far the tip turns from a plain circle into the picture: 0 % is the circle, 100 % the picture.");
    if (!forms()) {
        label = tr("Intensity");
        tip = tr("How strongly the grain shows in the stroke: 0 % not at all, 100 % fully.");
    } else {
        const ResourceKind kind = formKind();
        m_mainBody->setVisible(kind == ResourceKind::BlotMap);
        m_bristleBody->setVisible(kind == ResourceKind::Bristle);
        m_shapeBody->setVisible(kind == ResourceKind::Shape);
        if (kind == ResourceKind::Bristle) {
            label = tr("Hair");
            tip = tr("How wide each hair is: at 100 % about a fourteenth of the tip's width. Each dark pixel of the map is one hair.");
        } else if (kind == ResourceKind::Shape) {
            label = tr("Dens.");
        } else {
            label = tr("Mess.");
        }
    }
    m_strengthLabel->setText(label);
    control_tips::set({m_strength, m_strengthLabel}, label, tip);
}

void BrushResourceSection::applyEnabled() {
    const QString none = forms() ? BrushResourceLibrary::kSimpleCircle : BrushResourceLibrary::kNoTexture;
    const bool chosen = m_list->currentName() != none;
    m_strength->setEnabled(chosen);
    m_strengthLabel->setEnabled(chosen);
    m_body->setEnabled(chosen);
    // The `Layer paper` entry has no scale of its own.
    const bool ownScale = forms() || m_list->currentName() != BrushResourceLibrary::kLayerPaper;
    m_scale->setEnabled(ownScale);
    if (m_scaleLabel)
        m_scaleLabel->setEnabled(ownScale);
}

}
