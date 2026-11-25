#include "ui/OptionsDialog.h"

#include "core/paint/StrokeMemory.h"

#include "ui/KeysPage.h"
#include "ui/ResourcesPage.h"
#include "ui/Components.h"
#include "ui/Theme.h"

#include <QDoubleSpinBox>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QScrollArea>
#include <QSpinBox>
#include <QSplitter>
#include <QStackedWidget>
#include <QTimer>
#include <QVBoxLayout>

#include <cmath>

namespace doodlebold {

namespace {

constexpr QSize kOpeningSize(960, 640);  // big enough that a page's buttons show without scrolling
constexpr QSize kLeastSize(640, 420);
constexpr int kMemoryInUseMs = 1000;  // how often "In use now" is read again while the dialog is open
constexpr double kMemoryStepGB = 0.5;

QString gb(double value) {
    return QString::number(value, 'f', 1);
}

QVBoxLayout* addSection(QVBoxLayout* page, const QString& title) {
    QVBoxLayout* body = nullptr;
    page->addWidget(components::sectionCard(title, body));
    return body;
}

QLabel* help(const QString& text) {
    auto* label = components::mutedLabel(text);
    label->setContentsMargins(Theme::kSpace5, 0, 0, Theme::kSpaceHair);  // under its check box's text
    return label;
}

QCheckBox* option(const QString& text, const char* name, bool checked) {
    auto* box = new QCheckBox(text);
    box->setObjectName(QString::fromLatin1(name));
    box->setChecked(checked);
    return box;
}

QVBoxLayout* newPage() {
    auto* layout = new QVBoxLayout;
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(Theme::kCardGap);
    layout->setAlignment(Qt::AlignTop);
    return layout;
}

QWidget* pageOf(QLayout* layout) {
    auto* page = new QWidget;
    page->setObjectName(QStringLiteral("optionsPage"));
    page->setLayout(layout);
    auto* scroll = new QScrollArea;
    scroll->setObjectName(QStringLiteral("optionsPageScroll"));
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setWidget(page);
    return scroll;
}

}

OptionsDialog::OptionsDialog(const AppOptions::Values& initial, QWidget* parent) : QDialog(parent) {
    setObjectName(QStringLiteral("optionsDialog"));
    setWindowTitle(tr("Options"));

    m_pages = new QListWidget;
    m_pages->setObjectName(QStringLiteral("optionsPages"));
    m_stack = new QStackedWidget;
    const AppOptions::Values v = AppOptions::clamped(initial);

    m_pages->addItem(tr("Pen Tablet"));
    m_stack->addWidget(penTabletPage());
    m_pages->addItem(tr("Performance"));
    m_stack->addWidget(environmentPage());
    m_pages->addItem(tr("View"));
    m_stack->addWidget(preferencePage());
    m_pages->addItem(tr("Drawing"));
    m_stack->addWidget(drawingPage());
    m_pages->addItem(tr("Keys"));
    m_stack->addWidget(buildKeysPage(v));
    m_pages->addItem(tr("Timelapse"));
    m_stack->addWidget(timelapsePage());

    for (int i = 0; i < m_pages->count(); ++i)
        // The style sheet's padding alone does not make a list row taller.
        m_pages->item(i)->setSizeHint(QSize(0, 34));

    m_minimumPressure->setValue(v.minimumPressure);
    m_fullPressure->setValue(v.fullPressure);
    m_frostedMenus->setChecked(v.frostedMenus);
    m_zoomedOutGiven = v.zoomedOut;
    m_zoomedOutShown = v.zoomedOut == AppOptions::ZoomedOut::Fast ? 1 : 0;
    m_zoomedOut->setCurrentIndex(m_zoomedOutShown);
    m_undoSteps->setValue(v.undoSteps);
    m_brushOutline->setChecked(v.brushOutline);
    m_strokeTiming->setChecked(v.strokeTiming);
    m_memoryGBGiven = v.customMemoryGB;
    m_memoryGBCustom =
        v.customMemoryGB > 0.0 ? v.customMemoryGB : std::floor(AppOptions::automaticMemoryGB() / kMemoryStepGB) * kMemoryStepGB;
    m_memory->setCurrentIndex(v.customMemory ? 1 : 0);
    showMemoryAmount();
    m_keepBrushesInMemory->setChecked(v.keepBrushesInMemory);
    m_fitOnReset->setChecked(v.fitOnResetPosition);
    m_swapWheel->setChecked(v.swapWheelZoom);
    m_wheelAtCentre->setChecked(v.wheelZoomAtCentre);
    m_checkForUpdates->setChecked(v.checkForUpdatesAtStart);
    m_recordNewCanvases->setChecked(v.recordNewCanvases);
    m_quickShapes->setChecked(v.quickShapes);
    m_quickShapeHold->setValue(v.quickShapeHoldMs / 1000.0);
    m_quickShapeHold->setEnabled(v.quickShapes);

    auto* splitter = new QSplitter;
    splitter->setObjectName(QStringLiteral("optionsSplitter"));
    splitter->setHandleWidth(12);
    splitter->addWidget(m_pages);
    splitter->addWidget(m_stack);
    splitter->setStretchFactor(1, 1);
    splitter->setSizes({200, 760});
    splitter->setChildrenCollapsible(false);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    auto* layout = new QVBoxLayout(this);
    Theme::padDialog(layout, true);  // resizable: a page with a list wants room
    layout->addWidget(splitter, 1);
    layout->addWidget(buttons);
    setMinimumSize(kLeastSize);
    resize(kOpeningSize);

    connect(m_pages, &QListWidget::currentRowChanged, m_stack, &QStackedWidget::setCurrentIndex);
    m_pages->setCurrentRow(0);
}

void OptionsDialog::addResourcesPage(BrushResourceLibrary* library, ToolBox* tools) {
    int at = m_pages->count();
    for (int i = 0; i < m_pages->count(); ++i)
        if (m_pages->item(i)->text() == tr("Timelapse"))
            at = i;
    m_pages->insertItem(at, tr("Resources"));
    m_pages->item(at)->setSizeHint(QSize(0, 34));
    m_stack->insertWidget(at, new ResourcesPage(library, tools));
}

QWidget* OptionsDialog::penTabletPage() {
    QVBoxLayout* page = newPage();
    QVBoxLayout* layout = addSection(page, tr("Pen Pressure"));
    auto* row = new QHBoxLayout;
    row->addWidget(new QLabel(tr("Ignore pressure below:")));
    m_minimumPressure = new QSpinBox;
    m_minimumPressure->setObjectName(QStringLiteral("minimumPressureSpin"));
    m_minimumPressure->setRange(0, 100);
    row->addWidget(m_minimumPressure);
    row->addWidget(new QLabel(tr("%")));
    row->addStretch(1);
    layout->addLayout(row);
    layout->addWidget(help(tr("A pen touch lighter than this counts as no pressure.")));
    row = new QHBoxLayout;
    row->addWidget(new QLabel(tr("Full pressure at:")));
    m_fullPressure = new QSpinBox;
    m_fullPressure->setObjectName(QStringLiteral("fullPressureSpin"));
    m_fullPressure->setRange(1, 100);
    row->addWidget(m_fullPressure);
    row->addWidget(new QLabel(tr("%")));
    row->addStretch(1);
    layout->addLayout(row);
    layout->addWidget(help(tr("Pressing this hard or harder gives a tool its full width and density.")));
    return pageOf(page);
}

// Options > Performance: feedback, "Lets remove the cores option, and add your settings instead". Nothing
// here changes how a stroke looks.
QWidget* OptionsDialog::environmentPage() {
    QVBoxLayout* page = newPage();
    QVBoxLayout* layout = addSection(page, tr("Display"));
    m_frostedMenus = option(tr("Frosted menus"), "frostedMenusCheck", true);
    layout->addWidget(m_frostedMenus);
    layout->addWidget(help(tr("Menus and popups blur what is behind them, where the desktop can. Turn it off if menus open "
                              "slowly. A menu that is open changes once it closes.")));
    auto* row = new QHBoxLayout;
    row->addWidget(new QLabel(tr("Display when zoomed out:")));
    m_zoomedOut = new QComboBox;
    m_zoomedOut->setObjectName(QStringLiteral("zoomedOutCombo"));
    m_zoomedOut->addItem(tr("Smooth"));
    m_zoomedOut->addItem(tr("Fast"));
    row->addWidget(m_zoomedOut);
    row->addStretch(1);
    layout->addLayout(row);
    layout->addWidget(help(tr("Fast shows a zoomed-out canvas without smoothing it: rougher, but quicker on weak graphics "
                              "with a large canvas. The picture itself is the same. Until you pick one, it is Fast for a large "
                              "canvas where the computer draws without a graphics card, and Smooth otherwise.")));
    m_brushOutline = option(tr("Brush outline under the pen"), "brushOutlineCheck", true);
    layout->addWidget(m_brushOutline);
    layout->addWidget(help(tr("Off, the painting tools show a small cross in place of the brush's outline.")));
    // Memory second, under Display (feedback: "In the performance tab in options, move the memory section below the first
    // section").
    buildMemorySection(page);

    layout = addSection(page, tr("Undo"));
    row = new QHBoxLayout;
    row->addWidget(new QLabel(tr("Undo history:")));
    m_undoSteps = new QSpinBox;
    m_undoSteps->setObjectName(QStringLiteral("undoStepsSpin"));
    m_undoSteps->setRange(AppOptions::kLeastUndoSteps, AppOptions::kMostUndoSteps);
    row->addWidget(m_undoSteps);
    row->addWidget(new QLabel(tr("steps")));
    row->addStretch(1);
    layout->addLayout(row);
    layout->addWidget(help(tr("More steps keep more memory. Canvases made or opened from now on keep this many; an open canvas "
                              "keeps the number it was opened with.")));

    layout = addSection(page, tr("Stroke Timing"));
    m_strokeTiming = option(tr("Show stroke timing"), "strokeTimingCheck", false);
    layout->addWidget(m_strokeTiming);
    layout->addWidget(help(tr("A small readout in the corner of the canvas shows how long each stroke's pen events take to "
                              "paint, so you can see whether lag comes from the brush.")));
    return pageOf(page);
}

// What painting may use, and whether a brush too large for it is kept to what fits (the decisions of 2026-11-08).
void OptionsDialog::buildMemorySection(QVBoxLayout* page) {
    QVBoxLayout* layout = addSection(page, tr("Memory"));
    m_memorySection = layout->parentWidget();
    auto* row = new QHBoxLayout;
    row->addWidget(new QLabel(tr("Memory Doodlebold may use:")));
    m_memory = new QComboBox;
    m_memory->setObjectName(QStringLiteral("memoryCombo"));
    m_memory->addItem(tr("Automatic"));
    m_memory->addItem(tr("Custom"));
    row->addWidget(m_memory);
    m_memoryGB = new QDoubleSpinBox;
    m_memoryGB->setObjectName(QStringLiteral("memorySpin"));
    m_memoryGB->setDecimals(1);
    m_memoryGB->setSingleStep(kMemoryStepGB);
    m_memoryGB->setRange(1.0, std::max(1.0, std::floor(AppOptions::machineMemoryGB() / kMemoryStepGB) * kMemoryStepGB));
    m_memoryGB->setSuffix(tr(" GB"));
    row->addWidget(m_memoryGB);
    row->addStretch(1);
    layout->addLayout(row);
    layout->addWidget(help(tr("Automatic leaves room for the system and other programs: on this computer that's %1 GB of %2 GB. "
                              "Custom sets your own amount, from 1 GB up to this computer's memory. Strokes are kept within it.")
                               .arg(gb(AppOptions::automaticMemoryGB()), gb(AppOptions::machineMemoryGB()))));
    m_memoryInUse = help(QString());
    m_memoryInUse->setObjectName(QStringLiteral("memoryInUseLabel"));
    layout->addWidget(m_memoryInUse);
    m_keepBrushesInMemory = option(tr("Keep brushes within this memory"), "keepBrushesInMemoryCheck", true);
    layout->addWidget(m_keepBrushesInMemory);
    layout->addWidget(help(tr("A brush too large for it is kept to the largest size that fits, so painting is never cut short. "
                              "Off, any size paints; a stroke that runs out still stops partway instead of closing the program, "
                              "with a note in the corner.")));
    connect(m_memory, &QComboBox::currentIndexChanged, this, [this] {
        if (!m_memoryGB->isEnabled()) {
            showMemoryAmount();
            return;
        }
        m_memoryGBCustom = m_memoryGB->value();  // leaving Custom: kept for coming back to it
        showMemoryAmount();
    });
    connect(m_memoryGB, &QDoubleSpinBox::valueChanged, this, &OptionsDialog::showMemoryInUse);
    auto* timer = new QTimer(this);
    connect(timer, &QTimer::timeout, this, &OptionsDialog::showMemoryInUse);
    timer->start(kMemoryInUseMs);
}

// Automatic shows its own amount, greyed; Custom the user's, or Automatic's rounded down to a step to start from.
void OptionsDialog::showMemoryAmount() {
    const bool custom = m_memory->currentIndex() == 1;
    m_memoryGB->setEnabled(custom);
    m_memoryGB->setValue(custom ? m_memoryGBCustom : AppOptions::automaticMemoryGB());
    showMemoryInUse();
}

void OptionsDialog::showMemoryInUse() {
    constexpr double kBytesPerGB = 1024.0 * 1024.0 * 1024.0;
    const double allowed = m_memory->currentIndex() == 1 ? m_memoryGB->value() : AppOptions::automaticMemoryGB();
    m_memoryInUse->setText(
        tr("In use now: %1 GB of %2 GB").arg(gb(static_cast<double>(stroke_memory::heldBytes()) / kBytesPerGB), gb(allowed)));
}

void OptionsDialog::showMemorySection() {
    for (int i = 0; i < m_pages->count(); ++i)
        if (m_pages->item(i)->text() == tr("Performance"))
            m_pages->setCurrentRow(i);
    // Once it's laid out, which it isn't until it shows.
    QTimer::singleShot(0, this, [this] {
        if (auto* scroll = qobject_cast<QScrollArea*>(m_stack->currentWidget()))
            scroll->ensureWidgetVisible(m_memorySection);
    });
}

QWidget* OptionsDialog::preferencePage() {
    QVBoxLayout* page = newPage();
    QVBoxLayout* layout = addSection(page, tr("View"));
    m_fitOnReset = option(tr("Fit the canvas to the window when resetting the view"), "fitOnResetCheck", false);
    m_swapWheel = option(tr("Reverse the mouse wheel's zoom direction"), "swapWheelCheck", false);
    m_wheelAtCentre = option(tr("Mouse wheel always zooms at the center of the view"), "wheelAtCentreCheck", false);
    layout->addWidget(m_fitOnReset);
    layout->addWidget(m_swapWheel);
    layout->addWidget(m_wheelAtCentre);
    layout = addSection(page, tr("Updates"));
    m_checkForUpdates = option(tr("Check for updates at start"), "checkForUpdatesCheck", true);
    layout->addWidget(m_checkForUpdates);
    layout->addWidget(help(tr("Once a day at most, Doodlebold asks GitHub for its latest release and tells you if it is newer. "
                              "Nothing is downloaded. Help > Check for Updates asks at any time.")));
    return pageOf(page);
}

QWidget* OptionsDialog::drawingPage() {
    QVBoxLayout* page = newPage();
    QVBoxLayout* layout = addSection(page, tr("Quick Shapes"));
    m_quickShapes = option(tr("Turn a stroke into a shape when the pen is held still"), "quickShapesCheck", true);
    layout->addWidget(m_quickShapes);
    auto* row = new QHBoxLayout;
    row->addWidget(new QLabel(tr("Hold for:")));
    m_quickShapeHold = new QDoubleSpinBox;
    m_quickShapeHold->setObjectName(QStringLiteral("quickShapeHoldSpin"));
    m_quickShapeHold->setDecimals(1);
    m_quickShapeHold->setRange(0.3, 3.0);
    m_quickShapeHold->setSingleStep(0.1);
    row->addWidget(m_quickShapeHold);
    row->addWidget(new QLabel(tr("seconds")));
    row->addStretch(1);
    layout->addLayout(row);
    layout->addWidget(help(tr("Draw a triangle, square, rectangle, circle, oval or polygon and keep the pen down at the end: it "
                              "becomes that shape in your brush, ready to resize and turn.")));
    connect(m_quickShapes, &QCheckBox::toggled, m_quickShapeHold, &QWidget::setEnabled);
    return pageOf(page);
}

// The keys held on the canvas moved here from the Drawing page.
QWidget* OptionsDialog::buildKeysPage(const AppOptions::Values& v) {
    QVBoxLayout* page = newPage();
    m_keys = new KeysPage(v);
    page->addWidget(m_keys);
    return pageOf(page);
}

QWidget* OptionsDialog::timelapsePage() {
    QVBoxLayout* page = newPage();
    QVBoxLayout* layout = addSection(page, tr("Recording"));
    m_recordNewCanvases = option(tr("Record new canvases"), "recordNewCanvasesCheck", true);
    layout->addWidget(m_recordNewCanvases);
    layout->addWidget(help(tr("Every new canvas, and every picture opened without a recording, starts recording a timelapse of how it "
                              "is painted. The recording is kept in the .doodle file. Off: nothing records until you choose "
                              "File > Record Timelapse on a canvas. A canvas can pause or delete its recording from the REC mark at "
                              "the top of its window.")));
    return pageOf(page);
}

void OptionsDialog::showGraphicsChoice(bool fast) {
    if (m_zoomedOutGiven != AppOptions::ZoomedOut::ByGraphics)
        return;
    m_zoomedOutShown = fast ? 1 : 0;
    m_zoomedOut->setCurrentIndex(m_zoomedOutShown);
}

AppOptions::Values OptionsDialog::values() const {
    AppOptions::Values v;
    v.minimumPressure = m_minimumPressure->value();
    v.fullPressure = m_fullPressure->value();
    v.frostedMenus = m_frostedMenus->isChecked();
    if (m_zoomedOutGiven == AppOptions::ZoomedOut::ByGraphics && m_zoomedOut->currentIndex() == m_zoomedOutShown)
        v.zoomedOut = AppOptions::ZoomedOut::ByGraphics;
    else
        v.zoomedOut = m_zoomedOut->currentIndex() == 1 ? AppOptions::ZoomedOut::Fast : AppOptions::ZoomedOut::Smooth;
    v.undoSteps = m_undoSteps->value();
    v.brushOutline = m_brushOutline->isChecked();
    v.strokeTiming = m_strokeTiming->isChecked();
    v.customMemory = m_memory->currentIndex() == 1;
    v.customMemoryGB = v.customMemory ? m_memoryGB->value() : m_memoryGBGiven;
    v.keepBrushesInMemory = m_keepBrushesInMemory->isChecked();
    v.fitOnResetPosition = m_fitOnReset->isChecked();
    v.swapWheelZoom = m_swapWheel->isChecked();
    v.wheelZoomAtCentre = m_wheelAtCentre->isChecked();
    v.checkForUpdatesAtStart = m_checkForUpdates->isChecked();
    v.recordNewCanvases = m_recordNewCanvases->isChecked();
    v.quickShapes = m_quickShapes->isChecked();
    v.quickShapeHoldMs = int(std::lround(m_quickShapeHold->value() * 1000.0));
    v.altPicksColour = m_keys->picks();
    v.ctrlMovesLayers = m_keys->moves();
    v.held = m_keys->heldKeys();
    return v;
}

}
