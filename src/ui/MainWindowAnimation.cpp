// The main window's timeline and animation commands: Canvas → Show Timeline, the frame commands and the video, File → New from
// Video … and Export → Video, the keys for the previous and next frame, and the frame shown in the status bar.

#include "ui/MainWindow.h"

#include "core/document/Document.h"
#include "core/util/AppIdentity.h"
#include "ui/DocumentTabs.h"
#include "ui/PanelCard.h"
#include "ui/animation/AnimationController.h"
#include "ui/animation/FrameStrip.h"
#include "ui/animation/TimelinePanel.h"
#include "ui/timelapse/TimelapseController.h"

#include <QAction>
#include <QLabel>
#include <QSettings>
#include <QStatusBar>

#include <memory>

namespace doodlebold {

namespace {

const QString kTimelineHeightKey = QStringLiteral("timeline/height");

}

void MainWindow::buildTimeline() {
    auto* panel = new TimelinePanel;
    auto* card = new PanelCard(tr("Timeline"), panel);
    card->setObjectName(QStringLiteral("timelineCard"));
    card->setMinimumHeight(170);  // component metric: the timeline card's least height
    // The panel's own Timeline menu is its title.
    if (auto* title = card->findChild<QLabel*>(QStringLiteral("cardTitle")))
        title->hide();
    m_tabs->setTimelinePanel(card);
    m_tabs->setTimelineHeight(app_identity::settings().value(kTimelineHeightKey, 300).toInt());
    // Maximised, the timeline takes all the room the canvases leave (the splitter keeps their least height); back, the height
    // it had. Not saved as the timeline's height.
    connect(panel, &TimelinePanel::maximizeToggled, this, [this, before = std::make_shared<int>(0)](bool maximized) {
        if (maximized) {
            *before = m_tabs->timelineHeight();
            m_tabs->setTimelineHeight(m_tabs->height());
        } else {
            m_tabs->setTimelineHeight(*before);
        }
    });
    connect(m_tabs, &DocumentTabs::timelineResized, this, [](int height) {
        QSettings settings = app_identity::settings();
        settings.setValue(kTimelineHeightKey, height);
    });
    m_animation = new AnimationController(panel, [this](bool ask) { return m_timelapse->tools(ask); }, this, this);
    connect(m_animation, &AnimationController::message, this, [this](const QString& text) { statusBar()->showMessage(text, 4000); });
    connect(m_animation, &AnimationController::refused, this, [this](const QString& why) { showError(why); });
    connect(m_animation, &AnimationController::documentCreated, this, [this](Document* doc) { addDocument(doc, false); });
    connect(m_animation, &AnimationController::timelineShownChanged, this, &MainWindow::setTimelineShown);
    connect(panel, &TimelinePanel::selectionChanged, this, &MainWindow::updateFrameStatus);
    connect(m_tabs, &DocumentTabs::timelineCommand, this, [this](Document* doc, DocumentTabs::TimelineCommand command) {
        if (doc != m_tabs->currentDocument()) {
            if (command == DocumentTabs::TimelineCommand::Toggle)
                m_animation->setTimelineShown(doc, !doc->animation().view.timelineShown);
            return;
        }
        switch (command) {
        case DocumentTabs::TimelineCommand::Toggle: setTimelineShown(!m_animation->timelineShown()); break;
        case DocumentTabs::TimelineCommand::Previous: m_animation->previousFrame(); break;
        case DocumentTabs::TimelineCommand::Next: m_animation->nextFrame(); break;
        }
    });
    m_frameStatus = new QLabel(this);
    m_frameStatus->setObjectName(QStringLiteral("frameStatus"));
    statusBar()->addWidget(m_frameStatus);
}

void MainWindow::registerAnimationCommands() {
    bindCheck("canvas.showTimeline", false, [this](bool on) { setTimelineShown(on); });
    bind("canvas.newFrame", [this] { m_animation->newFrame(); });
    bind("canvas.duplicateFrame", [this] { m_animation->duplicateFrames(false); });
    bind("canvas.deleteFrame", [this] { m_animation->deleteFrames(false); }, Enable::SeveralFrames);
    bind("canvas.addVideo", [this] { m_animation->addVideo(); });
    bind("canvas.removeVideo", [this] { m_animation->removeVideo(); }, Enable::Video);
    bind("canvas.keepOnlyFrame", [this] { m_animation->keepOnlyThisFrame(); }, Enable::SeveralFrames);
    bind("file.newFromVideo", [this] { m_animation->newFromVideo(); }, Enable::Always);
    bind("file.exportVideo", [this] { m_animation->exportVideo(); });
    bind("animation.previousFrame", [this] { m_animation->previousFrame(); });
    bind("animation.nextFrame", [this] { m_animation->nextFrame(); });
    bind("animation.play", [this] { m_animation->togglePlay(); });
}

// For the current canvas only: each canvas keeps its own timeline open or closed (TimelineView::timelineShown).
void MainWindow::setTimelineShown(bool shown) {
    m_animation->setTimelineShown(shown);
    shown = m_animation->timelineShown();  // no canvas: none to show
    m_tabs->setTimelineShown(shown);
    if (QAction* check = command(QStringLiteral("canvas.showTimeline")); check && check->isChecked() != shown) {
        const QSignalBlocker block(check);
        check->setChecked(shown);
    }
    updateFrameStatus();
}

// "Frame 3 of 8", or "4 frames selected" while several are selected in the timeline; nothing for a picture with the timeline
// hidden.
void MainWindow::updateFrameStatus() {
    if (!m_frameStatus || !m_animation)
        return;
    const Document* d = m_tabs->currentDocument();
    const bool shown = m_animation->timelineShown();
    if (!d || (!shown && !d->animation().isAnimation())) {
        m_frameStatus->clear();
        return;
    }
    const std::size_t selected = shown ? m_animation->panel()->selectedFrames().size() : 1;
    m_frameStatus->setText(selected > 1 ? tr("%n frames selected", nullptr, static_cast<int>(selected))
                                        : tr("Frame %1 of %2").arg(d->currentFrame() + 1).arg(d->frameCount()));
}

bool MainWindow::deleteInTimeline() {
    if (!m_animation->timelineShown() || !m_animation->panel()->strip()->selectionActive())
        return false;
    m_animation->clearSelected();
    return true;
}

}
