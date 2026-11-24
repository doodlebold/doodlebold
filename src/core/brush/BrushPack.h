#pragma once

#include "core/brush/BrushResources.h"

#include <QString>
#include <QStringList>

#include <vector>

namespace doodlebold {

// What a folder of a brush pack goes into the library as. TextureAndPaper puts each picture in both lists.
enum class PackUse {
    Skip,
    BlotMap,
    Bristle,
    Shape,
    Texture,
    PaperTexture,
    TextureAndPaper
};
inline constexpr std::array kPackUses = {PackUse::Skip,    PackUse::BlotMap,      PackUse::Bristle,        PackUse::Shape,
                                         PackUse::Texture, PackUse::PaperTexture, PackUse::TextureAndPaper};
QString packUseName(PackUse use);
// How many pictures one picture of the pack becomes.
int picturesPerFile(PackUse use);

// One folder of a pack and the pictures directly in it, with what its name suggests they are.
struct PackFolder {
    QString name;          // its path inside the pack, "" for the pictures at the top
    QStringList pictures;  // paths inside the pack
    PackUse guess = PackUse::Skip;
    QString why;           // what the guess was made from, for the dialog
};

// A brush pack, a folder or a .zip file, read for what it holds: its pictures by folder, and the files that aren't pictures.
struct BrushPack {
    QString name;  // the folder's or the zip file's name
    QString source;
    bool zip = false;
    std::vector<PackFolder> folders;
    QStringList leftOut;  // paths of files that aren't pictures Doodlebold reads
    QString error;        // set when the pack couldn't be read at all

    int pictureCount() const;
};

// Reads what's in the folder or zip at `path`. Nothing is installed.
BrushPack readBrushPack(const QString& path);
// The guess for a folder of that name: by the words in it, as "blot", "bristle", "paper" or "texture".
PackFolder guessFolder(const QString& name);

struct PackInstall {
    int added = 0;          // pictures added to the library, counting both lists for TextureAndPaper
    QStringList unreadable;  // pictures that couldn't be read as images
    bool written = true;
};
// Adds the pictures of each folder as `uses` says, one entry per folder of the pack, each under a free name in its list.
// The library emits changed() once.
PackInstall installBrushPack(BrushResourceLibrary& library, const BrushPack& pack, const std::vector<PackUse>& uses);

}
