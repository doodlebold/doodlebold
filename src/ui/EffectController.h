#pragma once

#include "core/document/EditError.h"
#include "core/effects/EffectSettings.h"

#include <QObject>
#include <QPointer>
#include <QRect>
#include <QTimer>

#include <functional>
#include <memory>

class QProgressDialog;
class QWidget;

namespace doodlebold {

class Document;
class EffectDialog;
class EffectSession;

// Runs the Effects menu's entries on the current layer. An entry with sliders opens its dialog, window-modal, and
// shows the effect on the layer as the values change; one without (Invert, Desaturate) applies at once. The work runs on
// threads of its own: the window never waits on it, and a result that takes more than a moment shows its progress until
// it is in. OK and the dialog-less entries record one undo step; Cancel puts the layer back.
class EffectController : public QObject {
    Q_OBJECT
public:
    // How often finished tiles are put on the canvas while a job runs.
    static constexpr int kUpdateMs = 30;
    // A dialog-less entry waits this long for its result before it shows a progress dialog.
    static constexpr int kQuietWaitMs = 150;

    // `window` parents the dialogs; `focus` gives the part of the canvas on screen, which a preview fills first.
    EffectController(QWidget* window, std::function<QRect()> focus, QObject* parent = nullptr);
    ~EffectController() override;

    void run(EffectKind kind, Document* doc);
    bool isBusy() const { return m_session != nullptr; }
    EffectDialog* dialog() const;  // the open dialog, or null

signals:
    void refused(EditError error);
    void message(const QString& text);

private:
    void openDialog(EffectKind kind);
    void applyAtOnce();
    void tick();
    void requestOk();
    void finish();
    void abort();
    void showProgress();

    QWidget* m_window;
    std::function<QRect()> m_focus;
    QPointer<Document> m_doc;
    std::shared_ptr<EffectSession> m_session;
    QPointer<EffectDialog> m_dialog;
    QPointer<QProgressDialog> m_progress;
    QTimer m_timer;
    bool m_applying = false;  // OK given (or a dialog-less entry): the result is kept as soon as it is complete
};

}
