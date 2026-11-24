#pragma once

#include "core/brush/BrushResources.h"

#include <QPointer>
#include <QWidget>

#include <optional>

class QComboBox;
class QLabel;
class QListWidget;
class QPushButton;

namespace doodlebold {

class PackImportDialog;
class ToolBox;

// Options > Resources: the brush pictures of one kind at a time as a grid of thumbnails, with Add …, Remove, Rename …,
// Import Pack … and Open Folder. Changes take effect at once, not
// on OK. The built-in pictures are shown and can't be removed or renamed; a file of the same name replaces one. Removing a
// picture that tools paint with names them first, and they go back to their default form or texture.
class ResourcesPage : public QWidget {
    Q_OBJECT
public:
    // `tools`, if given, are the tools a removed picture is taken from.
    explicit ResourcesPage(BrushResourceLibrary* library, ToolBox* tools = nullptr, QWidget* parent = nullptr);

    ResourceKind kind() const;
    void showKind(ResourceKind kind);
    // The selected picture's name, or none.
    QString selectedName() const;
    void select(const QString& name);

    // Adds image files as pictures of the kind shown; a picture of the same name is replaced.
    void addFiles(const QStringList& paths);
    void removeSelected();
    void renameSelected();
    // Reads the pack and asks which of its folders to keep.
    void importPack(const QString& path);
    // What the last action did, or why it couldn't.
    QString message() const;

protected:
    virtual QStringList askFiles();
    // `tools`: the names of the tools that paint with the picture.
    virtual bool askRemove(const QString& name, const QStringList& tools);
    virtual std::optional<QString> askNewName(const QString& name);
    virtual void openFolder(const QString& path);
    virtual QString askPack(bool folder);
    virtual bool askPackChoices(PackImportDialog& dialog);
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;

private:
    void rebuild();
    void syncButtons();
    void report(const QString& text);
    const BrushResource* selected() const;

    QStringList toolsUsing(const QString& name) const;

    QPointer<BrushResourceLibrary> m_library;
    QPointer<ToolBox> m_tools;
    QComboBox* m_kinds = nullptr;
    QListWidget* m_grid = nullptr;
    QPushButton* m_add = nullptr;
    QPushButton* m_remove = nullptr;
    QPushButton* m_rename = nullptr;
    QPushButton* m_pack = nullptr;
    QPushButton* m_folder = nullptr;
    QLabel* m_message = nullptr;
};

}
