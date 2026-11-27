#include "ui/PluginQuestions.h"

#include <QAbstractButton>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QMessageBox>
#include <QPushButton>

namespace doodlebold::plugin_questions {

namespace {

QString tr(const char* text) {
    return QCoreApplication::translate("PluginQuestions", text);
}

}

NewPluginAnswer askAboutNew(QWidget* parent, const QString& path) {
    QMessageBox box(QMessageBox::Question, tr("Load a Plugin?"),
                    tr("Doodlebold found a format plugin it hasn't loaded before, or one that changed since:\n\n%1\n\nA plugin runs "
                       "inside Doodlebold with your rights, and can do anything you can. Load only plugins you trust.")
                        .arg(QDir::toNativeSeparators(path)),
                    QMessageBox::NoButton, parent);
    QAbstractButton* load = box.addButton(tr("Load It"), QMessageBox::AcceptRole);
    QAbstractButton* later = box.addButton(tr("Not Now"), QMessageBox::RejectRole);
    QAbstractButton* never = box.addButton(tr("Never"), QMessageBox::DestructiveRole);
    box.setDefaultButton(qobject_cast<QPushButton*>(later));
    box.setEscapeButton(later);
    box.exec();
    if (box.clickedButton() == load)
        return NewPluginAnswer::Load;
    return box.clickedButton() == never ? NewPluginAnswer::Never : NewPluginAnswer::NotNow;
}

bool keepOffAfterCrash(QWidget* parent, const QString& path) {
    QMessageBox box(QMessageBox::Warning, tr("A Plugin Crashed"),
                    tr("When Doodlebold last ran, it crashed inside the plugin\n\n%1\n\nIt wasn't loaded this time. Keep it turned off? "
                       "Options > Plugins can turn it on again.")
                        .arg(QDir::toNativeSeparators(path)),
                    QMessageBox::NoButton, parent);
    QAbstractButton* keepOff = box.addButton(tr("Keep It Off"), QMessageBox::AcceptRole);
    box.addButton(tr("Load It Again"), QMessageBox::RejectRole);
    box.setDefaultButton(qobject_cast<QPushButton*>(keepOff));
    box.exec();
    return box.clickedButton() == keepOff;
}

}
