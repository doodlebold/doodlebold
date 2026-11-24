#pragma once

#include "core/brush/BrushResources.h"

#include <QComboBox>
#include <QPointer>

#include <vector>

namespace doodlebold {

// The Brush Form or Brush Texture list and the layer panel's Paper list, which lacks the `Layer paper` row only
// the brush texture list has. Importing an image file is an entry of the list, and a drop on it.
class BrushResourceList : public QComboBox {
    Q_OBJECT
public:
    enum class Content {
        Forms,
        Textures,
        PaperTextures
    };
    // A tip picture's row: its kind as shown under its name in the open list. Textures and papers, each one kind, have none.
    static constexpr int kKindRole = Qt::UserRole + 3;
    static QString kindLabel(ResourceKind kind);

    BrushResourceList(BrushResourceLibrary* library, Content content, QWidget* parent = nullptr);

    // A name the library does not have (a form deleted since) is shown greyed, so the tool keeps it until another is
    // chosen.
    void setCurrentName(const QString& name);
    QString currentName() const;
    void importFile(ResourceKind kind, const QString& path);

signals:
    void chosen(const QString& name);
    void refused(const QString& message);

protected:
    virtual QString askFile(ResourceKind kind);
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;

private:
    void rebuild();
    void onActivated(int index);
    static QString importLabel(ResourceKind kind);

    QPointer<BrushResourceLibrary> m_library;
    Content m_content;
    std::vector<ResourceKind> m_importKinds;
    QString m_current;
    bool m_rebuilding = false;
};

QPixmap resourceThumbnail(const BrushResource& resource, int side, qreal devicePixelRatio = 1.0);

}
