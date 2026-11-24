#include "core/brush/BrushPack.h"

#include <QBuffer>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QTemporaryDir>

#include <gtest/gtest.h>

#include "miniz.h"

using namespace doodlebold;

namespace {

QByteArray png(int grey) {
    QImage image(24, 24, QImage::Format_Grayscale8);
    image.fill(grey);
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, "PNG");
    return bytes;
}

// The pack of the parity list's picture: blot maps, bristles, a
// folder of another kind, and textures, with a file that's no picture.
const std::vector<std::pair<QString, QByteArray>>& packFiles() {
    static const std::vector<std::pair<QString, QByteArray>> files = {
        {QStringLiteral("blotmap/one.png"), png(10)},
        {QStringLiteral("blotmap/two.png"), png(20)},
        {QStringLiteral("bristle/hairs.png"), png(30)},
        {QStringLiteral("bristle/more hairs.png"), png(40)},
        {QStringLiteral("scatter/dots.png"), png(50)},
        {QStringLiteral("Textures/grain.png"), png(60)},
        {QStringLiteral("Textures/weave.png"), png(70)},
        {QStringLiteral("readme.txt"), QByteArray("hello")},
        {QStringLiteral("Textures/.DS_Store"), QByteArray("x")},
    };
    return files;
}

void writeFolder(const QString& root) {
    for (const auto& [path, bytes] : packFiles()) {
        const QString file = QDir(root).filePath(path);
        QDir().mkpath(QFileInfo(file).path());
        QFile out(file);
        ASSERT_TRUE(out.open(QIODevice::WriteOnly));
        out.write(bytes);
    }
}

void writeZip(const QString& file) {
    mz_zip_archive zip{};
    ASSERT_TRUE(mz_zip_writer_init_heap(&zip, 0, 0));
    for (const auto& [path, bytes] : packFiles())
        ASSERT_TRUE(
            mz_zip_writer_add_mem(&zip, path.toUtf8().constData(), bytes.constData(), size_t(bytes.size()), MZ_DEFAULT_COMPRESSION));
    void* data = nullptr;
    size_t size = 0;
    ASSERT_TRUE(mz_zip_writer_finalize_heap_archive(&zip, &data, &size));
    QFile out(file);
    ASSERT_TRUE(out.open(QIODevice::WriteOnly));
    out.write(static_cast<const char*>(data), qint64(size));
    mz_free(data);
    mz_zip_writer_end(&zip);
}

const PackFolder* folderNamed(const BrushPack& pack, const QString& name) {
    for (const PackFolder& f : pack.folders)
        if (f.name == name)
            return &f;
    return nullptr;
}

void expectTheTestPack(const BrushPack& pack) {
    ASSERT_TRUE(pack.error.isEmpty()) << pack.error.toStdString();
    EXPECT_EQ(pack.pictureCount(), 7);
    ASSERT_EQ(pack.folders.size(), 4u);
    EXPECT_EQ(folderNamed(pack, QStringLiteral("blotmap"))->guess, PackUse::BlotMap);
    EXPECT_EQ(folderNamed(pack, QStringLiteral("bristle"))->guess, PackUse::Bristle);
    EXPECT_EQ(folderNamed(pack, QStringLiteral("scatter"))->guess, PackUse::Skip);
    EXPECT_EQ(folderNamed(pack, QStringLiteral("Textures"))->guess, PackUse::TextureAndPaper);
    EXPECT_EQ(folderNamed(pack, QStringLiteral("Textures"))->pictures.size(), 2);
    EXPECT_EQ(pack.leftOut, QStringList{QStringLiteral("readme.txt")}) << "the desktop's clutter isn't listed";
}

std::vector<PackUse> guesses(const BrushPack& pack) {
    std::vector<PackUse> out;
    out.reserve(pack.folders.size());
    for (const PackFolder& f : pack.folders)
        out.push_back(f.guess);
    return out;
}

}

TEST(BrushPack, FolderNamesSayWhatTheirPicturesAre) {
    EXPECT_EQ(guessFolder(QStringLiteral("pack/Blot Maps")).guess, PackUse::BlotMap);
    EXPECT_EQ(guessFolder(QStringLiteral("Hair")).guess, PackUse::Bristle);
    EXPECT_EQ(guessFolder(QStringLiteral("Paper Textures")).guess, PackUse::PaperTexture) << "paper before texture";
    EXPECT_EQ(guessFolder(QStringLiteral("grains")).guess, PackUse::TextureAndPaper);
    EXPECT_EQ(guessFolder(QStringLiteral("Brush Tips")).guess, PackUse::Shape);
    EXPECT_EQ(guessFolder(QStringLiteral("brush blots")).guess, PackUse::BlotMap) << "blot before brush";
    EXPECT_EQ(guessFolder(QStringLiteral("misc")).guess, PackUse::Skip);
    EXPECT_EQ(guessFolder(QStringLiteral("misc")).why, QStringLiteral("No kind in the folder's name"));
    EXPECT_EQ(guessFolder(QStringLiteral("blotmap")).why, QStringLiteral("Folder \"blotmap\""));
    EXPECT_EQ(picturesPerFile(PackUse::TextureAndPaper), 2);
    EXPECT_EQ(picturesPerFile(PackUse::Skip), 0);
}

TEST(BrushPack, AFolderPackIsReadAndItsTickedFoldersInstalled) {
    QTemporaryDir dir;
    const QString root = dir.filePath(QStringLiteral("Test Pack"));
    writeFolder(root);
    const BrushPack pack = readBrushPack(root);
    EXPECT_EQ(pack.name, QStringLiteral("Test Pack"));
    EXPECT_FALSE(pack.zip);
    expectTheTestPack(pack);

    BrushResourceLibrary library(dir.filePath(QStringLiteral("brushes")), BrushResourceLibrary::BuiltIns::Left);
    int changes = 0;
    QObject::connect(&library, &BrushResourceLibrary::changed, [&] { ++changes; });
    const PackInstall installed = installBrushPack(library, pack, guesses(pack));
    EXPECT_TRUE(installed.written);
    EXPECT_EQ(installed.added, 8) << "2 blot maps, 2 bristles, 2 textures, 2 papers; scatter left out";
    EXPECT_EQ(changes, 1);
    EXPECT_EQ(library.ofKind(ResourceKind::BlotMap).size(), 2u);
    EXPECT_EQ(library.ofKind(ResourceKind::Bristle).size(), 2u);
    EXPECT_EQ(library.ofKind(ResourceKind::Shape).size(), 0u);
    EXPECT_NE(library.texture(QStringLiteral("grain")), nullptr);
    EXPECT_NE(library.paperTexture(QStringLiteral("grain")), nullptr);
    EXPECT_EQ(qGray(library.form(QStringLiteral("one"))->image.pixel(1, 1)), 10);

    const PackInstall again = installBrushPack(library, pack, guesses(pack));
    EXPECT_EQ(again.added, 8);
    EXPECT_NE(library.form(QStringLiteral("one 2")), nullptr) << "a second install keeps the first";
}

TEST(BrushPack, AZipPackIsReadAsTheFolderIs) {
    QTemporaryDir dir;
    const QString file = dir.filePath(QStringLiteral("Test Pack.zip"));
    writeZip(file);
    const BrushPack pack = readBrushPack(file);
    EXPECT_EQ(pack.name, QStringLiteral("Test Pack"));
    EXPECT_TRUE(pack.zip);
    expectTheTestPack(pack);

    BrushResourceLibrary library(dir.filePath(QStringLiteral("brushes")), BrushResourceLibrary::BuiltIns::Left);
    std::vector<PackUse> uses(pack.folders.size(), PackUse::Skip);
    for (std::size_t i = 0; i < pack.folders.size(); ++i)
        if (pack.folders[i].name == QLatin1String("scatter"))
            uses[i] = PackUse::Shape;  // ticked by the user
    const PackInstall installed = installBrushPack(library, pack, uses);
    EXPECT_EQ(installed.added, 1);
    ASSERT_NE(library.form(QStringLiteral("dots")), nullptr);
    EXPECT_EQ(library.form(QStringLiteral("dots"))->kind, ResourceKind::Shape);
    EXPECT_EQ(qGray(library.form(QStringLiteral("dots"))->image.pixel(1, 1)), 50);
}

TEST(BrushPack, WhatIsNotAPackSaysSo) {
    QTemporaryDir dir;
    const QString notZip = dir.filePath(QStringLiteral("broken.zip"));
    QFile out(notZip);
    ASSERT_TRUE(out.open(QIODevice::WriteOnly));
    out.write("not a zip");
    out.close();
    EXPECT_FALSE(readBrushPack(notZip).error.isEmpty());
    const QString empty = dir.filePath(QStringLiteral("empty"));
    QDir().mkpath(empty);
    EXPECT_EQ(readBrushPack(empty).error, QStringLiteral("There are no pictures in it."));
}
