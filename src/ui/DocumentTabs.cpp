#include "ui/DocumentTabs.h"

#include "core/document/Document.h"
#include "core/view/ViewText.h"
#include "ui/CanvasView.h"
#include "ui/DoodleboldStyle.h"
#include "ui/Theme.h"
#include "ui/ViewSelector.h"
#include "ui/animation/TimelineTitleButtons.h"

#include <QCloseEvent>
#include <QLabel>
#include <QMdiArea>
#include <QStyle>
#include <QMdiSubWindow>
#include <QMenu>
#include <QMessageBox>
#include <QPixmap>
#include <QSplitter>
#include <QVBoxLayout>

#include <algorithm>

namespace doodlebold {

namespace {

constexpr int kTitleButtonsWidth = DoodleboldStyle::kButtonGap + 3 * (DoodleboldStyle::kButtonSize + DoodleboldStyle::kButtonGap);

// Some Qt versions emit windowStateChanged from ~QMdiSubWindow while its children are going, so nothing may hear it then. Only
// that signal: a blanket disconnect() would also cut destroyed() from Qt's accessibility cache, which then keeps the dead window's
// interface, and Qt 6.8's setWindowTitle hands it to a later window made at the same address and crashes reading its title.
void silenceStateChanges(QMdiSubWindow* window) {
    QObject::disconnect(window, &QMdiSubWindow::windowStateChanged, nullptr, nullptr);
}

// The title bar as it shows, inside the window's invisible margin, in the window's own coordinates.
QRect visibleTitleBar(const QMdiSubWindow* window) {
    const int margin = DoodleboldStyle::kFrameMargin;
    return {margin, margin, window->width() - 2 * margin, DoodleboldStyle::kTitleBarHeight - margin};
}

// The offset down that brings [from, from + length) inside [0, room).
int offsetInto(int from, int length, int room) {
    return std::max(0, std::min(from, room - length)) - from;
}

// The offset across: up to half of [from, from + length) may go past the room's left edge, but none past its right, where a
// title bar has its buttons; those come first when the two can't both hold.
int offsetAcross(int from, int length, int room) {
    return std::min(std::max(from, -length / 2), room - length) - from;
}

}

QSize DocumentTabs::minimumWindowSize() {
    const int margin = DoodleboldStyle::kFrameMargin;
    return {2 * margin + kMinimumTitleWidth + kTitleButtonsWidth, DoodleboldStyle::kTitleBarHeight + kMinimumCanvasHeight + margin};
}

DocumentTabs::DocumentTabs(TabletStats* stats, QWidget* parent)
    : QWidget(parent), m_stats(stats), m_area(new QMdiArea(this)), m_selector(new ViewSelector(this)) {
    m_area->setObjectName(QStringLiteral("canvasArea"));
    m_area->setBackground(Theme::color(Theme::Token::Window));
    m_area->setActivationOrder(QMdiArea::CreationOrder);
    m_selector->setObjectName(QStringLiteral("viewSelector"));
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(m_area, 1);
    layout->addWidget(m_selector);
    m_area->viewport()->installEventFilter(this);  // when the free area shrinks, windows move back in
    connect(m_area, &QMdiArea::subWindowActivated, this, &DocumentTabs::onWindowActivated);
    connect(m_selector, &ViewSelector::activated, this, &DocumentTabs::setCurrentIndex);
    connect(m_selector, &ViewSelector::closeRequested, this, [this](int index) {
        // Queued: closing may show a dialog and changes the tabs the selector is still handling an event for.
        QPointer<CanvasView> view = canvasAt(index);
        QMetaObject::invokeMethod(
            this,
            [this, view] {
                if (view)
                    requestCloseTab(indexOf(view));
            },
            Qt::QueuedConnection);
    });
    m_iconTimer.setSingleShot(true);
    m_iconTimer.setInterval(500);
    connect(&m_iconTimer, &QTimer::timeout, this, &DocumentTabs::refreshIcons);
}

void DocumentTabs::setOuterGap(int gap) {
    const int margin = std::max(0, gap - DoodleboldStyle::kFrameMargin);
    layout()->setContentsMargins(0, margin, 0, margin);
}

DocumentTabs::~DocumentTabs() {
    // Documents emit while their undo stacks are torn down, and some Qt versions emit windowStateChanged from
    // ~QMdiSubWindow; by then the views are gone.
    for (const Entry& e : m_entries) {
        if (Document* doc = e.view->document())
            doc->disconnect(this);
        silenceStateChanges(e.window);
    }
}

void DocumentTabs::watchDocument(CanvasView* view, Document* doc) {
    if (doc->property("watchedByTabs").toBool())
        return;
    doc->setProperty("watchedByTabs", true);
    connect(doc, &Document::modifiedChanged, this, [this, view] { refresh(view); });
    connect(doc, &Document::displayNameChanged, this, [this, view] { refresh(view); });
    connect(doc, &Document::canvasResized, this, [this, view] { refresh(view); });
    connect(doc, &Document::imageChanged, this, [this] { m_iconTimer.start(); });
}

CanvasView* DocumentTabs::addDocument(Document* doc) {
    auto* view = new CanvasView(doc, m_stats);
    doc->setParent(view);
    watchDocument(view, doc);
    addView(view, doc->size(), doc);
    return view;
}

DocumentTabs::Entry* DocumentTabs::entryOf(const CanvasView* view) {
    const int at = indexOf(view);
    return at >= 0 ? &m_entries[static_cast<std::size_t>(at)] : nullptr;
}

const DocumentTabs::Entry* DocumentTabs::entryOf(const CanvasView* view) const {
    const int at = indexOf(view);
    return at >= 0 ? &m_entries[static_cast<std::size_t>(at)] : nullptr;
}

void DocumentTabs::updateHeader(Entry& entry) {
    placePill(entry);
}

void DocumentTabs::addView(CanvasView* view, const QSize& canvas, Document* recorded) {
    QMdiSubWindow* window = m_area->addSubWindow(view);
    window->setAttribute(Qt::WA_DeleteOnClose, false);
    window->setAutoFillBackground(false);
    window->setMinimumSize(minimumWindowSize());
    QPixmap blank(16, 16);  // no icon in the title bar or the menu bar (Qt would show its own logo)
    blank.fill(Qt::transparent);
    window->setWindowIcon(QIcon(blank));
    window->installEventFilter(this);
    view->installEventFilter(this);
    // Qt's window menu carries Ctrl+W, Ctrl+F4 … ; View → Close View has Ctrl+W, and the menus own every shortcut.
    if (QMenu* menu = window->systemMenu())
        for (QAction* a : menu->actions())
            a->setShortcuts({});
    m_entries.push_back({view, window});
    connect(window, &QMdiSubWindow::windowStateChanged, this, [this, window](Qt::WindowStates, Qt::WindowStates) {
        const int at = indexOfWindow(window);
        if (at >= 0)
            updateHeader(m_entries[static_cast<std::size_t>(at)]);
    });
    if (recorded) {
        auto* timeline = new TimelineTitleButtons(recorded, window);
        m_entries.back().timeline = timeline;
        const auto forward = [this, timeline](TimelineCommand c) {
            if (Document* doc = timeline->document())
                emit timelineCommand(doc, c);
        };
        connect(timeline, &TimelineTitleButtons::timelineToggled, this, [forward] { forward(TimelineCommand::Toggle); });
        connect(timeline, &TimelineTitleButtons::previousFrame, this, [forward] { forward(TimelineCommand::Previous); });
        connect(timeline, &TimelineTitleButtons::nextFrame, this, [forward] { forward(TimelineCommand::Next); });
        connect(timeline, &TimelineTitleButtons::changed, this, [this, window] {
            const int at = indexOfWindow(window);
            if (at >= 0)
                placePill(m_entries[static_cast<std::size_t>(at)]);
        });
        RecordingPill* pill = new RecordingPill(recorded, window);
        m_entries.back().pill = pill;
        const auto place = [this, window] {
            const int at = indexOfWindow(window);
            if (at >= 0)
                placePill(m_entries[static_cast<std::size_t>(at)]);
        };
        connect(pill, &RecordingPill::changed, this, place);
        connect(pill, &RecordingPill::commandRequested, this,
                [this, pill](RecordingPill::Command command) { emit recordingCommand(pill->document(), command); });
        connect(window, &QMdiSubWindow::windowStateChanged, pill, [place](Qt::WindowStates, Qt::WindowStates) { place(); });
        place();
    }
    connect(view, &CanvasView::viewChanged, this, [this, view] { refresh(view); });
    connect(window, &QMdiSubWindow::windowStateChanged, this, [this, window](Qt::WindowStates, Qt::WindowStates now) {
        if (window == m_area->activeSubWindow() || window == m_area->currentSubWindow())
            m_maximized = now.testFlag(Qt::WindowMaximized);
    });
    rebuildSelector();
    place(window, canvas);
    if (m_maximized)
        window->showMaximized();
    else
        window->show();
    m_area->setActiveSubWindow(window);
    refresh(view);
    onWindowActivated(window);
    refreshIcons();
}

void DocumentTabs::placePill(const Entry& entry) {
    RecordingPill* pill = entry.pill;
    if (!pill)
        return;
    constexpr int kGapToButtons = Theme::kSpace2;
    constexpr int kGapBetween = Theme::kSpace2;
    constexpr int kGapToEdge = Theme::kSpace2;
    const int margin = DoodleboldStyle::kFrameMargin;
    const int header = DoodleboldStyle::kTitleBarHeight - margin;
    QWidget* mark = entry.badge ? static_cast<QWidget*>(entry.badge) : static_cast<QWidget*>(pill);
    const int buttons = kTitleButtonsWidth;
    const bool minimized = entry.window->isMinimized();
    const bool shown = !minimized && (entry.badge || !pill->text().isEmpty());
    if (entry.badge)
        pill->hide();
    int fromRight = buttons + kGapToButtons;
    // Right to left, the most useful first; what no longer fits in a narrow window hides rather than being cut off.
    const auto put = [&](QWidget* w, bool wanted) {
        if (!w)
            return;
        const bool fits = wanted && fromRight + w->width() + kGapToEdge <= entry.window->width() - 2 * margin;
        w->setVisible(fits);
        if (!fits)
            return;
        fromRight += w->width();
        w->move(entry.window->width() - margin - fromRight, margin + (header - w->height()) / 2);
        w->raise();
        fromRight += kGapBetween;
    };
    put(mark, shown);
    put(entry.timeline, !minimized && !entry.badge);
    const bool any = fromRight > buttons + kGapToButtons;
    entry.window->setProperty(DoodleboldStyle::kHeaderInsetProperty, any ? fromRight : 0);
    entry.window->update();
}

void DocumentTabs::placeReplay(const Entry& entry) {
    if (entry.replay)
        entry.replay->setGeometry(entry.view->geometry());
}

TimelapseReplayView* DocumentTabs::showReplay(Document* doc, std::shared_ptr<const Timelapse> recording) {
    for (Entry& e : m_entries) {
        if (e.view->document() != doc)
            continue;
        if (e.replay || !e.pill || !recording)
            return nullptr;
        e.replay = new TimelapseReplayView(std::move(recording), e.window);
        e.replay->setGeometry(e.view->geometry());
        e.replay->show();
        e.replay->raise();
        auto* badge = new QLabel(tr("Timelapse Replay"), e.window);
        badge->setObjectName(QStringLiteral("replayBadge"));
        badge->setFont(Theme::font(Theme::TextRole::Caption));
        badge->setProperty("role", "badge");  // the style sheet's accent-tinted pill
        badge->setAlignment(Qt::AlignCenter);
        badge->adjustSize();
        e.badge = badge;
        placePill(e);
        return e.replay;
    }
    return nullptr;
}

void DocumentTabs::endReplay(Document* doc) {
    for (Entry& e : m_entries) {
        if (e.view->document() != doc)
            continue;
        if (e.replay)
            e.replay->hide();
        delete e.replay.data();
        e.replay.clear();
        delete e.badge;
        e.badge = nullptr;
        placePill(e);
        if (e.window == m_area->activeSubWindow())
            e.view
                ->setFocus();  // the canvas takes the keys back (not when another window came to the front: that would bring this one back)
        return;
    }
}

TimelapseReplayView* DocumentTabs::replayOf(const Document* doc) const {
    for (const Entry& e : m_entries)
        if (e.view->document() == doc)
            return e.replay.data();
    return nullptr;
}

QLabel* DocumentTabs::badgeOf(const Document* doc) const {
    for (const Entry& e : m_entries)
        if (e.view->document() == doc)
            return e.badge;
    return nullptr;
}

TimelineTitleButtons* DocumentTabs::timelineButtonsOf(const Document* doc) const {
    for (const Entry& e : m_entries)
        if (e.view->document() == doc)
            return e.timeline;
    return nullptr;
}

void DocumentTabs::setTimelinePanel(QWidget* panel) {
    m_timelinePanel = panel;
    auto* box = static_cast<QVBoxLayout*>(layout());
    const int at = box->indexOf(m_area);
    m_timelineSplitter = new QSplitter(Qt::Vertical, this);
    m_timelineSplitter->setObjectName(QStringLiteral("timelineSplitter"));
    m_timelineSplitter->setChildrenCollapsible(false);
    m_timelineSplitter->setHandleWidth(8);
    box->removeWidget(m_area);
    m_timelineSplitter->addWidget(m_area);
    m_timelineSplitter->addWidget(panel);
    m_timelineSplitter->setStretchFactor(0, 1);
    m_timelineSplitter->setStretchFactor(1, 0);
    box->insertWidget(at, m_timelineSplitter, 1);
    connect(m_timelineSplitter, &QSplitter::splitterMoved, this, [this] {
        m_timelineHeight = timelineHeight();
        emit timelineResized(m_timelineHeight);
    });
    panel->setVisible(m_timelineShown);
}

void DocumentTabs::setTimelineShown(bool shown) {
    m_timelineShown = shown;
    if (!m_timelinePanel)
        return;
    m_timelinePanel->setVisible(shown);
    if (shown)
        setTimelineHeight(m_timelineHeight);
}

int DocumentTabs::timelineHeight() const {
    return m_timelineSplitter && m_timelineSplitter->sizes().size() == 2 ? m_timelineSplitter->sizes().at(1) : m_timelineHeight;
}

void DocumentTabs::setTimelineHeight(int height) {
    m_timelineHeight = std::max(height, m_timelinePanel ? m_timelinePanel->minimumHeight() : 0);
    if (!m_timelineSplitter || !m_timelineShown)
        return;
    const int total = std::max(m_timelineSplitter->height(), m_timelineHeight + 200);
    m_timelineSplitter->setSizes({total - m_timelineHeight, m_timelineHeight});
}

RecordingPill* DocumentTabs::pillOf(const Document* doc) const {
    for (const Entry& e : m_entries)
        if (e.view->document() == doc)
            return e.pill;
    return nullptr;
}

// A new window shows its canvas at 100 % where the area allows, 26 px right and down from the last one; when that would
// leave the area the cascade starts again at the top-left.
void DocumentTabs::place(QMdiSubWindow* window, const QSize& canvas) {
    const QSize area = m_area->contentsRect().size();
    const QSize frame = window->size() - window->widget()->size();
    QSize size = (canvas + frame).boundedTo(area * 3 / 4).expandedTo(QSize(240, 180));
    QPoint at = m_cascade.x() < 0 ? QPoint(0, 0) : m_cascade + QPoint(kCascadeStep, kCascadeStep);
    if (!area.isEmpty() && (at.x() + size.width() > area.width() || at.y() + size.height() > area.height()))
        at = QPoint(0, 0);
    m_cascade = at;
    window->setGeometry(QRect(at, size));
}

int DocumentTabs::currentIndex() const {
    return indexOfWindow(m_area->currentSubWindow());
}

void DocumentTabs::setCurrentIndex(int index) {
    if (QMdiSubWindow* w = windowAt(index)) {
        if (w->isMinimized())
            w->showNormal();
        m_area->setActiveSubWindow(w);
        onWindowActivated(w);
    }
}

int DocumentTabs::indexOf(const CanvasView* view) const {
    for (std::size_t i = 0; i < m_entries.size(); ++i)
        if (m_entries[i].view == view)
            return static_cast<int>(i);
    return -1;
}

int DocumentTabs::indexOfWindow(const QMdiSubWindow* window) const {
    for (std::size_t i = 0; i < m_entries.size(); ++i)
        if (m_entries[i].window == window)
            return static_cast<int>(i);
    return -1;
}

CanvasView* DocumentTabs::canvasAt(int index) const {
    return index >= 0 && index < count() ? m_entries[static_cast<std::size_t>(index)].view : nullptr;
}

QMdiSubWindow* DocumentTabs::windowAt(int index) const {
    return index >= 0 && index < count() ? m_entries[static_cast<std::size_t>(index)].window : nullptr;
}

CanvasView* DocumentTabs::currentCanvas() const {
    return canvasAt(currentIndex());
}

Document* DocumentTabs::currentDocument() const {
    CanvasView* v = currentCanvas();
    return v ? v->document() : nullptr;
}

bool DocumentTabs::requestCloseTab(int index) {
    QPointer<CanvasView> view = canvasAt(index);
    if (!view)
        return false;
    Document* doc = view->document();
    if (doc && isModified(doc)) {
        switch (askUnsaved(doc)) {
        case UnsavedChoice::Cancel: return false;
        case UnsavedChoice::Discard: break;
        case UnsavedChoice::Save:
            if (!m_save || !m_save(doc))
                return false;
            break;
        }
    }
    // The dialogs are modal, so windows may have closed meanwhile.
    if (!view)
        return false;
    const int at = indexOf(view);
    if (at < 0)
        return false;
    QMdiSubWindow* window = m_entries[static_cast<std::size_t>(at)].window;
    m_entries.erase(m_entries.begin() + at);
    if (Document* d = view->document())
        d->disconnect(this);
    window->removeEventFilter(this);
    view->removeEventFilter(this);
    silenceStateChanges(window);
    view->disconnect(this);
    m_area->removeSubWindow(window);
    delete window;
    if (m_entries.empty())
        m_cascade = QPoint(-1, -1);
    rebuildSelector();
    onWindowActivated(m_area->currentSubWindow());
    return true;
}

bool DocumentTabs::requestCloseAll() {
    while (count() > 0)
        if (!requestCloseTab(0))
            return false;
    return true;
}

void DocumentTabs::selectNextTab() {
    if (count() > 0)
        setCurrentIndex((currentIndex() + 1) % count());
}

void DocumentTabs::selectPreviousTab() {
    if (count() > 0)
        setCurrentIndex((currentIndex() + count() - 1) % count());
}

QString DocumentTabs::nameOf(const Document* doc) {
    return doc->displayName();
}

bool DocumentTabs::isModified(const Document* doc) {
    return doc->isModified();
}

QString DocumentTabs::titleTextOf(const Document* doc) {
    return doc->titleText();
}

// "NewCanvas1 (5.34%) (16000 × 16000 px / 350 dpi)": the name, " (*)" while it has unsaved changes, the zoom, and the canvas's size and
// resolution.
QString DocumentTabs::windowTitleFor(const Document* doc, double zoom) {
    QString name = nameOf(doc);
    if (isModified(doc))
        name += QStringLiteral(" (*)");
    return QStringLiteral("%1 (%2) (%3 × %4 px / %5 dpi)")
        .arg(name, view_text::zoomText(zoom), QString::number(doc->size().width()), QString::number(doc->size().height()),
             QString::number(doc->resolution()));
}

DocumentTabs::UnsavedChoice DocumentTabs::askUnsaved(const Document* doc) {
    QMessageBox box(QMessageBox::Warning, tr("Close Canvas"), tr("“%1” has unsaved changes. Save them before closing?").arg(nameOf(doc)),
                    QMessageBox::NoButton, this);
    box.addButton(QMessageBox::Save);
    box.addButton(QMessageBox::Discard);
    box.addButton(QMessageBox::Cancel);
    box.setDefaultButton(QMessageBox::Save);
    box.setEscapeButton(QMessageBox::Cancel);
    switch (box.exec()) {
    case QMessageBox::Save: return UnsavedChoice::Save;
    case QMessageBox::Discard: return UnsavedChoice::Discard;
    default: return UnsavedChoice::Cancel;
    }
}

// A window in its normal state keeps its title bar's buttons in the free area, and at least half of the title bar (test
// pass 2): it can't be dragged further, and one the shrinking area would hide that much moves back in.
void DocumentTabs::keepInside(QMdiSubWindow* window) {
    const QSize room = m_area->viewport()->size();
    if (room.isEmpty() || window->windowState() & (Qt::WindowMinimized | Qt::WindowMaximized))
        return;
    const QRect bar = visibleTitleBar(window).translated(window->pos());
    const QPoint by(offsetAcross(bar.left(), bar.width(), room.width()), offsetInto(bar.top(), bar.height(), room.height()));
    if (!by.isNull())
        window->move(window->pos() + by);
}

bool DocumentTabs::eventFilter(QObject* watched, QEvent* event) {
    if (event->type() == QEvent::Resize && watched == m_area->viewport()) {
        for (const Entry& e : m_entries)
            keepInside(e.window);
        return false;
    }
    if (event->type() == QEvent::Move || event->type() == QEvent::Resize)
        if (const int at = indexOfWindow(qobject_cast<QMdiSubWindow*>(watched)); at >= 0)
            keepInside(m_entries[static_cast<std::size_t>(at)].window);
    if (event->type() == QEvent::Resize || event->type() == QEvent::Move || event->type() == QEvent::Show) {
        const int at = indexOf(qobject_cast<CanvasView*>(watched));
        if (at >= 0)
            placeReplay(m_entries[static_cast<std::size_t>(at)]);
    }
    if (event->type() == QEvent::Resize || event->type() == QEvent::Show) {
        const int at = indexOfWindow(qobject_cast<QMdiSubWindow*>(watched));
        if (at >= 0)
            placePill(m_entries[static_cast<std::size_t>(at)]);
    }
    if (event->type() == QEvent::Close) {
        const int index = indexOfWindow(qobject_cast<QMdiSubWindow*>(watched));
        if (index >= 0) {
            event->ignore();
            QPointer<CanvasView> view = canvasAt(index);
            QMetaObject::invokeMethod(
                this,
                [this, view] {
                    if (view)
                        requestCloseTab(indexOf(view));
                },
                Qt::QueuedConnection);
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
}

void DocumentTabs::refresh(CanvasView* view) {
    const int index = indexOf(view);
    Document* doc = view->document();
    if (index < 0 || !doc)
        return;
    QMdiSubWindow* window = m_entries[static_cast<std::size_t>(index)].window;
    window->setWindowTitle(windowTitleFor(doc, view->zoom()));
    ViewSelector::Item item = m_selector->item(index);
    item.name = nameOf(doc);
    item.zoom = view_text::zoomText(view->zoom());
    item.toolTip = doc->filePath().isEmpty() ? nameOf(doc) : doc->filePath();
    m_selector->setItem(index, std::move(item));
    emit documentTitleChanged();
}

void DocumentTabs::refreshIcons() {
    for (int i = 0; i < count(); ++i) {
        Document* doc = canvasAt(i)->document();
        if (!doc)
            continue;
        ViewSelector::Item item = m_selector->item(i);
        item.icon =
            doc->projection().scaled(ViewSelector::kIconSize, ViewSelector::kIconSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        m_selector->setItem(i, std::move(item));
    }
}

void DocumentTabs::rebuildSelector() {
    std::vector<ViewSelector::Item> items;
    items.reserve(m_entries.size());
    for (const Entry& e : m_entries) {
        ViewSelector::Item item;
        if (Document* doc = e.view->document()) {
            item.name = nameOf(doc);
            item.zoom = view_text::zoomText(e.view->zoom());
            item.toolTip = doc->filePath().isEmpty() ? nameOf(doc) : doc->filePath();
        }
        items.push_back(std::move(item));
    }
    m_selector->setItems(std::move(items));
    m_selector->setCurrent(currentIndex());
    m_iconTimer.start();
}

void DocumentTabs::onWindowActivated(QMdiSubWindow* window) {
    if (window && indexOfWindow(window) < 0)
        return;
    m_selector->setCurrent(indexOfWindow(m_area->currentSubWindow()));
    announceCurrent();
}

void DocumentTabs::announceCurrent() {
    Document* doc = currentDocument();
    if (doc == m_lastCurrent.data())
        return;
    m_lastCurrent = doc;
    emit currentDocumentChanged(doc);
}

}
