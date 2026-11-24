#pragma once

#include "core/util/Result.h"

#include <QImage>
#include <QObject>
#include <QString>

#include <array>
#include <memory>
#include <optional>
#include <vector>

namespace doodlebold {

struct BrushSettings;

// The kinds of picture the panels' lists hold. Three of them are lists of tip shapes, the Texture and PaperTexture ones of
// surfaces; a PaperTexture is what a layer's Layer Effect > Paper shows.
enum class ResourceKind {
    BlotMap,
    Bristle,
    Shape,
    Texture,
    PaperTexture
};
inline constexpr std::array kResourceKinds = {ResourceKind::BlotMap, ResourceKind::Bristle, ResourceKind::Shape, ResourceKind::Texture,
                                              ResourceKind::PaperTexture};

// The folder inside the library that holds a kind's pictures.
QString resourceFolder(ResourceKind kind);
inline bool isForm(ResourceKind kind) {
    return kind == ResourceKind::BlotMap || kind == ResourceKind::Bristle || kind == ResourceKind::Shape;
}
// Whether two kinds share a list, and so a name space: the three kinds of form are one list.
inline bool sameList(ResourceKind a, ResourceKind b) {
    return a == b || (isForm(a) && isForm(b));
}

// A brush shape's settings, from a file beside its picture with the same name and ".ini", in the reference's layout: a
// [Parameters] section of `Key = value` lines, `;` starting a comment. A key the file leaves out keeps the reference's own default,
// which is what each starts at here.
struct ShapeSettings {
    int density = 100;  // Density, %
    int angleControl = 1;  // Angle Control: 0 none, 1 auto (along the stroke), 2 with the pen
    int angle = 0;  // degrees
    int angleJitter = 0;  // %
    int scaling = 100;  // %
    int sizeJitter = 0;  // %
    int spacing = 25;  // % of the size
    int scattering = 0;  // %
    bool highQuality = false;  // HQ Down Scaling
    // Doodlebold's own key, OneStroke, which the reference's files don't have: the stamps lay as one continuous stroke, each
    // pixel as dark as the darkest stamp over it, so close stamps don't add up toward solid. A property of the shape, not a setting:
    // it isn't put into a tool's settings, so another shape chosen after it paints as before.
    bool oneStroke = false;
};
// The settings in such a file, or none if it can't be read.
std::optional<ShapeSettings> readShapeSettings(const QString& path);
// A brush shape's settings put into a tool's, as choosing the shape does.
void applyShapeSettings(BrushSettings& settings, const ShapeSettings& shape);

// One picture, 8-bit greyscale.
struct BrushResource {
    QString name;
    ResourceKind kind = ResourceKind::BlotMap;
    QImage image;
    QString file;
    bool builtIn = false;  // one of the pictures that come with Doodlebold; it can't be removed or renamed
    std::optional<ShapeSettings> shapeSettings;  // a brush shape's settings file, if it has one
};

// Doodlebold's own blot maps leave their white ground empty, so no band runs along a stroke's middle. Only the built-in pictures:
// a user's own, even one that takes a built-in's name, paints by the blot-map rule alone.
inline bool groundLaysNothing(const BrushResource& resource) {
    return resource.builtIn && resource.kind == ResourceKind::BlotMap;
}

enum class ResourceError {
    Unreadable,
    Empty,
    WriteFailed,
    BuiltIn,
    NameTaken,
    NotFound,
};
QString describe(ResourceError error);

// The pictures that come with Doodlebold, and the user's own in the library folder, one sub-folder per kind. Any image Qt
// reads can be imported: converted to greyscale and stored as PNG. A user's picture with the name of a built-in one in the
// same list takes its place.
class BrushResourceLibrary : public QObject {
    Q_OBJECT
public:
    // The names of "no form" and "no texture"; they head the lists and have no bitmap.
    static const QString kSimpleCircle;
    static const QString kNoTexture;
    // The second entry of the texture list; it has no bitmap of its own.
    static const QString kLayerPaper;
    // Larger imports are scaled down to this many pixels a side, so each picture the library keeps decoded holds at most 1 MiB; detail
    // past 1024 samples is lost on tips wider than that. It came with the starting point (8621d01).
    static constexpr int kMaxImportSide = 1024;

    // Left: only the user's own pictures, for tests that count what they added.
    enum class BuiltIns {
        Included,
        Left
    };
    // What an import does when the list already has its name: take the next free "name 2", or replace that picture.
    enum class SameName {
        Renumber,
        Replace
    };

    explicit BrushResourceLibrary(QString folder, QObject* parent = nullptr);
    BrushResourceLibrary(QString folder, BuiltIns builtIns, QObject* parent = nullptr);
    ~BrushResourceLibrary() override;

    static QString defaultFolder();
    QString folder() const { return m_folder; }
    // Where the user's pictures of `kind` are kept.
    QString kindFolder(ResourceKind kind) const;

    void reload();

    // The entries of each list, by name. The "none" entries are not in these lists.
    std::vector<const BrushResource*> forms() const;
    std::vector<const BrushResource*> textures() const;
    std::vector<const BrushResource*> paperTextures() const;
    // The entries of one kind only, by name.
    std::vector<const BrushResource*> ofKind(ResourceKind kind) const;
    // By name; null for the "none" names and for names not found (a tool may name a form that was deleted).
    const BrushResource* form(const QString& name) const;
    const BrushResource* texture(const QString& name) const;
    const BrushResource* paperTexture(const QString& name) const;
    // The entry a new resource of `kind` named `name` would meet: among the forms of any kind for a form, else among its own
    // kind; null if the name is free of entries (it may still be reserved, see isReserved()).
    const BrushResource* lookup(ResourceKind kind, const QString& name) const;
    // The names of the "none" entries and the layer paper entry, which no resource may take; a file so named gets " (file)" added.
    static bool isReserved(const QString& name);
    static QString safeName(const QString& name);

    // Adds an image file as a resource of kind, named after the file; returns the name.
    Result<QString, ResourceError> importImage(ResourceKind kind, const QString& path, SameName sameName = SameName::Renumber);
    // A name in kind's list that is free (or reserved, see isReserved()) made from `base`: base, else "base 2" and on.
    QString uniqueName(ResourceKind kind, const QString& base) const;

    // A resource ready to be written: `image` is greyscale, `name` is a free, file-safe name.
    struct NewResource {
        ResourceKind kind = ResourceKind::BlotMap;
        QString name;
        QImage image;
    };
    // Writes the resources and reads the folder again once; emits changed() once. Stops at the first one that cannot be written.
    Result<void, ResourceError> add(const std::vector<NewResource>& resources);
    // The user's pictures only: a built-in one stays.
    bool remove(ResourceKind kind, const QString& name);
    // Renames one of the user's pictures, its file too; emits renamed() and then changed(). Returns the name it took, made
    // file-safe. A built-in name may be taken, and that picture then gives way; another of the user's may not.
    Result<QString, ResourceError> rename(ResourceKind kind, const QString& name, const QString& newName);

signals:
    void changed();
    void renamed(doodlebold::ResourceKind kind, const QString& from, const QString& to);

private:
    const BrushResource* find(ResourceKind kind, const QString& name) const;
    const BrushResource* findUser(ResourceKind kind, const QString& name) const;
    std::vector<const BrushResource*> sorted(bool (*wanted)(ResourceKind)) const;
    void gather();

    QString m_folder;
    BuiltIns m_builtIns;
    std::vector<std::unique_ptr<BrushResource>> m_user;
    std::vector<const BrushResource*> m_shown;  // the user's, and the built-ins none of them replaces
};

// Greyscale as the library stores it: luma, with any transparency laid over white first.
QImage toGreyscale(const QImage& image);

// The pictures that come with Doodlebold, read once from the program's resources.
const std::vector<BrushResource>& builtInResources();

}
