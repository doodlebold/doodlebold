#pragma once

#include <QKeySequence>
#include <QString>

class QAbstractButton;
class QAction;
class QWidget;

namespace doodlebold::button_tips {

// The tip is the button's short name and, under it in smaller muted letters, the shortcut of its command. The shortcut
// is read when the tip is about to show (and when the button is shown), so it is right after a key is changed.

QString tipHtml(const QString& name, const QString& keys = {});

QString keysText(const QAction* action);
QString keysText(const QKeySequence& keys);

QAction* findCommand(const QWidget* from, const QString& id);

QString commandTip(const QWidget* from, const QString& name, const QString& commandId);

void setButtonTip(QAbstractButton* button, const QString& name, QAction* action = nullptr);
void setCommandTip(QAbstractButton* button, const QString& name, const QString& commandId);
void setKeyTip(QAbstractButton* button, const QString& name, const QKeySequence& keys);

}
