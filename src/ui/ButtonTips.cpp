#include "ui/ButtonTips.h"

#include "ui/Components.h"

#include <QAbstractButton>
#include <QAction>
#include <QApplication>
#include <QEvent>
#include <QPointer>

namespace doodlebold::button_tips {

namespace {

// Watches one widget: before the tip shows (and when the widget is shown) it writes the tip afresh, with the keys as they
// are then.
class TipFilter : public QObject {
public:
    explicit TipFilter(QWidget* widget) : QObject(widget), m_widget(widget) { widget->installEventFilter(this); }

    QString name;
    QPointer<QAction> action;
    QString commandId;
    QString fixedKeys;

    void refresh() {
        QString keys = fixedKeys;
        QAction* a = action ? action.data() : commandId.isEmpty() ? nullptr : findCommand(m_widget, commandId);
        if (a)
            keys = keysText(a);
        const QString html = tipHtml(name, keys);
        if (m_widget->toolTip() != html)
            m_widget->setToolTip(html);
    }

protected:
    bool eventFilter(QObject* watched, QEvent* event) override {
        if (watched == m_widget && (event->type() == QEvent::ToolTip || event->type() == QEvent::Show || event->type() == QEvent::Polish))
            refresh();
        return false;
    }

private:
    QWidget* m_widget;
};

// The widget's filter, or null (no Q_OBJECT here, so findChild cannot be used).
TipFilter* existingFilter(const QWidget* widget) {
    for (QObject* child : widget->children())
        if (auto* f = dynamic_cast<TipFilter*>(child))
            return f;
    return nullptr;
}

TipFilter* filterOf(QWidget* widget) {
    TipFilter* filter = existingFilter(widget);
    return filter ? filter : new TipFilter(widget);
}

}

QString tipHtml(const QString& name, const QString& keys) {
    return components::tipHtml(name, keys);
}

QString keysText(const QKeySequence& keys) {
    return keys.toString(QKeySequence::NativeText);
}

QString keysText(const QAction* action) {
    if (!action)
        return {};
    QStringList parts;
    for (const QKeySequence& k : action->shortcuts())
        if (!k.isEmpty())
            parts << keysText(k);
    return parts.join(QStringLiteral(", "));
}

QAction* findCommand(const QWidget* from, const QString& id) {
    if (id.isEmpty())
        return nullptr;  // findChild would match any action by its empty name
    for (const QObject* o = from; o; o = o->parent())
        if (auto* a = o->findChild<QAction*>(id, Qt::FindDirectChildrenOnly))
            return a;
    return nullptr;
}

QString commandTip(const QWidget* from, const QString& name, const QString& commandId) {
    return tipHtml(name, keysText(findCommand(from, commandId)));
}

void setButtonTip(QAbstractButton* button, const QString& name, QAction* action) {
    TipFilter* f = filterOf(button);
    f->name = name;
    f->action = action;
    f->commandId.clear();
    f->fixedKeys.clear();
    f->refresh();
}

void setCommandTip(QAbstractButton* button, const QString& name, const QString& commandId) {
    TipFilter* f = filterOf(button);
    f->name = name;
    f->action = nullptr;
    f->commandId = commandId;
    f->fixedKeys.clear();
    f->refresh();
}

void setKeyTip(QAbstractButton* button, const QString& name, const QKeySequence& keys) {
    TipFilter* f = filterOf(button);
    f->name = name;
    f->action = nullptr;
    f->commandId.clear();
    f->fixedKeys = keysText(keys);
    f->refresh();
}

}
