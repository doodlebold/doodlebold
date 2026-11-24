#include "ui/EffectController.h"

#include "core/document/Document.h"
#include "core/effects/EffectSession.h"
#include "ui/EffectDialog.h"

#include <QProgressDialog>
#include <QWidget>

namespace doodlebold {

EffectController::EffectController(QWidget* window, std::function<QRect()> focus, QObject* parent)
    : QObject(parent), m_window(window), m_focus(std::move(focus)) {
    m_timer.setInterval(kUpdateMs);
    connect(&m_timer, &QTimer::timeout, this, &EffectController::tick);
}

EffectController::~EffectController() {
    abort();
}

EffectDialog* EffectController::dialog() const {
    return m_dialog;
}

void EffectController::run(EffectKind kind, Document* doc) {
    if (!doc || isBusy())
        return;
    if (doc->isPainting()) {
        emit refused(EditError::StrokeInProgress);
        return;
    }
    if (doc->maskTarget()) {
        emit message(tr("Effects change a layer's own pixels: click its thumbnail rather than its mask."));
        return;
    }
    auto begun = EffectSession::begin(*doc, EffectSettings::start(kind, doc->size()));
    if (!begun) {
        emit refused(begun.error());
        return;
    }
    m_doc = doc;
    m_session = begun.value();
    m_session->setFocus(m_focus ? m_focus() : QRect());
    if (effectSpec(kind).sliders.empty())
        applyAtOnce();
    else
        openDialog(kind);
}

void EffectController::openDialog(EffectKind kind) {
    auto* dlg = new EffectDialog(kind, m_doc->size(), m_window);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    m_dialog = dlg;
    connect(dlg, &EffectDialog::settingsChanged, this, [this](const EffectSettings& s) {
        if (m_session)
            m_session->setSettings(s);
    });
    connect(dlg, &EffectDialog::previewToggled, this, [this](bool on) {
        if (m_session)
            m_session->setPreview(on);
    });
    connect(dlg, &EffectDialog::okRequested, this, &EffectController::requestOk);
    connect(dlg, &QDialog::rejected, this, &EffectController::abort);
    m_session->setSettings(dlg->settings());
    m_session->compute();
    m_timer.start();
    dlg->open();  // window-modal, and the event loop keeps running
}

// Invert and Desaturate: a small layer is done before anyone could notice; a large one shows its progress.
void EffectController::applyAtOnce() {
    m_applying = true;
    m_session->setPreview(false);
    if (m_session->waitForResult(std::chrono::milliseconds(kQuietWaitMs))) {
        finish();
        return;
    }
    showProgress();
    m_timer.start();
}

void EffectController::requestOk() {
    if (!m_session)
        return;
    m_applying = true;
    m_session->compute();
    tick();
}

void EffectController::tick() {
    if (!m_session) {
        m_timer.stop();
        return;
    }
    if (!m_doc) {  // its canvas was closed
        abort();
        return;
    }
    const bool complete = m_session->update();
    if (!m_applying)
        return;
    if (complete)
        finish();
    else
        showProgress();
}

void EffectController::showProgress() {
    const double progress = m_session ? m_session->progress() : 0.0;
    if (m_dialog) {
        m_dialog->showApplying(progress);
        return;
    }
    if (!m_progress) {
        auto* p = new QProgressDialog(m_window);
        p->setObjectName(QStringLiteral("effectProgressDialog"));
        p->setWindowTitle(effectTitle(m_session->settings().kind));
        p->setLabelText(tr("Applying %1 …").arg(effectTitle(m_session->settings().kind)));
        p->setRange(0, 100);
        p->setMinimumDuration(0);
        p->setAutoClose(false);
        p->setAutoReset(false);
        p->setAttribute(Qt::WA_DeleteOnClose);
        connect(p, &QProgressDialog::canceled, this, &EffectController::abort);
        m_progress = p;
        p->open();
    }
    m_progress->setValue(static_cast<int>(progress * 100));
}

void EffectController::finish() {
    m_timer.stop();
    m_applying = false;
    const EditResult<> result = m_session->commit();
    m_session.reset();
    if (m_progress) {
        m_progress->disconnect(this);
        m_progress->close();
    }
    if (m_dialog) {
        m_dialog->disconnect(this);
        if (m_dialog->isVisible())
            m_dialog->accept();
    }
    if (!result && result.error() != EditError::NoChange)
        emit refused(result.error());
}

void EffectController::abort() {
    m_timer.stop();
    m_applying = false;
    if (m_session) {
        if (m_doc)
            m_session->cancel();
        else
            m_session->abandon();  // closed with its canvas
        m_session.reset();
    }
    if (m_progress) {
        m_progress->disconnect(this);
        m_progress->close();
    }
    if (m_dialog) {
        m_dialog->disconnect(this);
        if (m_dialog->isVisible())  // not when its own Cancel brought us here
            m_dialog->reject();
    }
}

}
