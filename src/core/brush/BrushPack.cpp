#include "core/brush/BrushPack.h"

#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QSet>

#include <algorithm>
#include <map>
#include <memory>

#include "miniz.h"

namespace doodlebold {

namespace {

// Import Pack leaves out a pack over 512 MiB, a picture over 64 MiB unpacked and files past the 5000th.
// A zip read whole into memory: at 512 MiB the import peaks at about 0.6 GB; past it the pack isn't opened.
constexpr qint64 kLargestPack = qint64(512) << 20;
// One picture, unpacked into memory before it's decoded, so an entry can't unpack to gigabytes; at 64 MiB the install peaks at about
// 0.2 GB.
constexpr quint64 kLargestPicture = quint64(64) << 20;
// Files read from a pack, folder entries and hidden files counted; the rest are dropped unlisted. 5000 pictures install in 0.6 s.
constexpr int kMostFiles = 5000;

QString tr(const char* text) {
    return QCoreApplication::translate("BrushPack", text);
}

bool isPicture(const QString& path) {
    static const QSet<QString> suffixes = [] {
        QSet<QString> out;
        for (const QByteArray& format : QImageReader::supportedImageFormats())
            out.insert(QString::fromLatin1(format).toLower());
        return out;
    }();
    return suffixes.contains(QFileInfo(path).suffix().toLower());
}

// Files a desktop leaves in a folder, which aren't part of the pack.
bool isClutter(const QString& path) {
    return path.startsWith(QLatin1String("__MACOSX/")) || QFileInfo(path).fileName().startsWith(QLatin1Char('.')) ||
           QFileInfo(path).fileName().compare(QLatin1String("Thumbs.db"), Qt::CaseInsensitive) == 0;
}

void sortInto(BrushPack& pack, const QStringList& files) {
    std::map<QString, QStringList> byFolder;
    for (const QString& file : files) {
        if (isClutter(file))
            continue;
        if (!isPicture(file)) {
            pack.leftOut << file;
            continue;
        }
        const qsizetype slash = file.lastIndexOf(QLatin1Char('/'));
        byFolder[slash < 0 ? QString() : file.left(slash)] << file;
    }
    for (auto& [name, pictures] : byFolder) {
        PackFolder folder = guessFolder(name);
        pictures.sort(Qt::CaseInsensitive);
        folder.pictures = pictures;
        pack.folders.push_back(folder);
    }
}

struct Zip {
    QByteArray bytes;
    mz_zip_archive archive{};
    bool open = false;

    explicit Zip(const QString& path) {
        QFile file(path);
        if (file.size() > kLargestPack || !file.open(QIODevice::ReadOnly))
            return;
        bytes = file.readAll();
        open = mz_zip_reader_init_mem(&archive, bytes.constData(), size_t(bytes.size()), 0);
    }
    ~Zip() {
        if (open)
            mz_zip_reader_end(&archive);
    }
    Zip(const Zip&) = delete;
    Zip& operator=(const Zip&) = delete;

    QStringList files() {
        QStringList out;
        const int count = std::min(int(mz_zip_reader_get_num_files(&archive)), kMostFiles);
        for (int i = 0; i < count; ++i) {
            mz_zip_archive_file_stat stat;
            if (mz_zip_reader_file_stat(&archive, mz_uint(i), &stat) && !stat.m_is_directory)
                out << QString::fromUtf8(stat.m_filename);
        }
        return out;
    }

    QByteArray read(const QString& name) {
        const QByteArray key = name.toUtf8();
        const int index = mz_zip_reader_locate_file(&archive, key.constData(), nullptr, 0);
        mz_zip_archive_file_stat stat;
        if (index < 0 || !mz_zip_reader_file_stat(&archive, mz_uint(index), &stat) || stat.m_uncomp_size > kLargestPicture)
            return {};
        size_t size = 0;
        void* data = mz_zip_reader_extract_to_heap(&archive, mz_uint(index), &size, 0);
        if (!data)
            return {};
        QByteArray out(static_cast<const char*>(data), qsizetype(size));
        mz_free(data);
        return out;
    }
};

QImage readPicture(const BrushPack& pack, Zip* zip, const QString& file) {
    QImage image = zip ? QImage::fromData(zip->read(file)) : QImage(QDir(pack.source).filePath(file));
    if (image.isNull() || image.size().isEmpty())
        return {};
    const int most = BrushResourceLibrary::kMaxImportSide;
    if (image.width() > most || image.height() > most)
        image = image.scaled(most, most, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    return toGreyscale(image);
}

std::vector<ResourceKind> kindsOf(PackUse use) {
    switch (use) {
    case PackUse::Skip: return {};
    case PackUse::BlotMap: return {ResourceKind::BlotMap};
    case PackUse::Bristle: return {ResourceKind::Bristle};
    case PackUse::Shape: return {ResourceKind::Shape};
    case PackUse::Texture: return {ResourceKind::Texture};
    case PackUse::PaperTexture: return {ResourceKind::PaperTexture};
    case PackUse::TextureAndPaper: return {ResourceKind::Texture, ResourceKind::PaperTexture};
    }
    return {};
}

}

QString packUseName(PackUse use) {
    switch (use) {
    case PackUse::Skip: return tr("Do not install");
    case PackUse::BlotMap: return tr("Blot map");
    case PackUse::Bristle: return tr("Bristle");
    case PackUse::Shape: return tr("Brush shape");
    case PackUse::Texture: return tr("Brush texture");
    case PackUse::PaperTexture: return tr("Paper texture");
    case PackUse::TextureAndPaper: return tr("Brush and paper texture");
    }
    return {};
}

int picturesPerFile(PackUse use) {
    return int(kindsOf(use).size());
}

int BrushPack::pictureCount() const {
    int count = 0;
    for (const PackFolder& f : folders)
        count += int(f.pictures.size());
    return count;
}

PackFolder guessFolder(const QString& name) {
    // The deepest folder's name decides; earlier words win over later ones ("paper" before "texture", "blot" before "brush").
    static const std::vector<std::pair<QString, PackUse>> words = {
        {QStringLiteral("blot"), PackUse::BlotMap},
        {QStringLiteral("bristle"), PackUse::Bristle},
        {QStringLiteral("hair"), PackUse::Bristle},
        {QStringLiteral("paper"), PackUse::PaperTexture},
        {QStringLiteral("texture"), PackUse::TextureAndPaper},
        {QStringLiteral("grain"), PackUse::TextureAndPaper},
        {QStringLiteral("canvas"), PackUse::TextureAndPaper},
        {QStringLiteral("shape"), PackUse::Shape},
        {QStringLiteral("tip"), PackUse::Shape},
        {QStringLiteral("form"), PackUse::Shape},
        {QStringLiteral("brush"), PackUse::Shape},
    };
    PackFolder folder;
    folder.name = name;
    const QString last = name.section(QLatin1Char('/'), -1).toLower();
    for (const auto& [word, use] : words)
        if (last.contains(word)) {
            folder.guess = use;
            folder.why = tr("Folder \"%1\"").arg(name.section(QLatin1Char('/'), -1));
            return folder;
        }
    folder.why = name.isEmpty() ? tr("At the top of the pack") : tr("No kind in the folder's name");
    return folder;
}

BrushPack readBrushPack(const QString& path) {
    BrushPack pack;
    const QFileInfo info(path);
    pack.source = info.absoluteFilePath();
    pack.name = info.isDir() ? info.fileName() : info.completeBaseName();
    if (info.isDir()) {
        QStringList files;
        QDirIterator it(pack.source, QDir::Files | QDir::Hidden, QDirIterator::Subdirectories);
        while (it.hasNext() && files.size() < kMostFiles)
            files << QDir(pack.source).relativeFilePath(it.next());
        sortInto(pack, files);
    } else {
        pack.zip = true;
        Zip zip(pack.source);
        if (!zip.open)
            pack.error = tr("The file is not a .zip file that can be read.");
        else
            sortInto(pack, zip.files());
    }
    if (pack.error.isEmpty() && pack.folders.empty())
        pack.error = tr("There are no pictures in it.");
    return pack;
}

PackInstall installBrushPack(BrushResourceLibrary& library, const BrushPack& pack, const std::vector<PackUse>& uses) {
    PackInstall out;
    std::unique_ptr<Zip> zip = pack.zip ? std::make_unique<Zip>(pack.source) : nullptr;
    std::vector<BrushResourceLibrary::NewResource> resources;
    const auto taken = [&resources](ResourceKind kind, const QString& name) {
        return std::ranges::any_of(resources, [&](const auto& r) { return sameList(r.kind, kind) && r.name == name; });
    };
    for (std::size_t f = 0; f < pack.folders.size() && f < uses.size(); ++f) {
        const std::vector<ResourceKind> kinds = kindsOf(uses[f]);
        if (kinds.empty())
            continue;
        for (const QString& file : pack.folders[f].pictures) {
            const QImage image = readPicture(pack, zip.get(), file);
            if (image.isNull()) {
                out.unreadable << file;
                continue;
            }
            for (ResourceKind kind : kinds) {
                const QString base = BrushResourceLibrary::safeName(QFileInfo(file).completeBaseName());
                QString name = library.uniqueName(kind, base);
                for (int i = 2; taken(kind, name); ++i)
                    name = library.uniqueName(kind, QStringLiteral("%1 %2").arg(base).arg(i));
                resources.push_back({kind, name, image});
            }
        }
    }
    out.written = library.add(resources).ok();
    out.added = out.written ? int(resources.size()) : 0;
    return out;
}

}
