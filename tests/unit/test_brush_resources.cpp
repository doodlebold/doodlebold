#include "core/brush/BrushResources.h"
#include "core/brush/BrushSettings.h"
#include "core/tools/ToolBox.h"

#include <QDir>
#include <QFile>
#include <QImage>
#include <QPainter>
#include <QTemporaryDir>

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdlib>

using namespace doodlebold;

namespace {

QStringList namesOf(const std::vector<const BrushResource*>& list) {
    QStringList out;
    for (const BrushResource* r : list)
        out << r->name;
    return out;
}

QImage grey(int value = 99) {
    QImage image(32, 32, QImage::Format_Grayscale8);
    image.fill(value);
    return image;
}

}

TEST(BrushResources, ANewLibraryIsEmpty) {
    QTemporaryDir dir;
    BrushResourceLibrary lib(dir.path(), BrushResourceLibrary::BuiltIns::Left);
    EXPECT_TRUE(lib.forms().empty());
    EXPECT_TRUE(lib.textures().empty());
    EXPECT_TRUE(lib.paperTextures().empty());
    EXPECT_EQ(lib.form(BrushResourceLibrary::kSimpleCircle), nullptr) << "the plain round tip has no bitmap";
    EXPECT_EQ(lib.texture(BrushResourceLibrary::kNoTexture), nullptr);
}

TEST(BrushResources, ImportsAnyImageAsGreyscaleWithAUniqueName) {
    QTemporaryDir dir;
    BrushResourceLibrary lib(dir.filePath(QStringLiteral("brushes")), BrushResourceLibrary::BuiltIns::Left);
    int changes = 0;
    QObject::connect(&lib, &BrushResourceLibrary::changed, [&] { ++changes; });

    QImage source(40, 30, QImage::Format_ARGB32);
    source.fill(Qt::transparent);
    QPainter(&source).fillRect(QRect(10, 10, 10, 10), QColor(255, 0, 0, 128));
    const QString file = dir.filePath(QStringLiteral("My Picture.png"));
    ASSERT_TRUE(source.save(file));

    const auto imported = lib.importImage(ResourceKind::Texture, file);
    ASSERT_TRUE(imported.ok());
    EXPECT_EQ(imported.value(), QStringLiteral("My Picture"));
    EXPECT_EQ(changes, 1);
    const BrushResource* r = lib.texture(QStringLiteral("My Picture"));
    ASSERT_NE(r, nullptr);
    EXPECT_EQ(r->image.format(), QImage::Format_Grayscale8);
    EXPECT_EQ(r->image.size(), QSize(40, 30));
    EXPECT_EQ(qGray(r->image.pixel(0, 0)), 255) << "transparent: white";
    EXPECT_LT(qGray(r->image.pixel(15, 15)), 255) << "the red, half over white";
    EXPECT_GT(qGray(r->image.pixel(15, 15)), 100);
    EXPECT_TRUE(QFile::exists(dir.filePath(QStringLiteral("brushes/textures/My Picture.png")))) << "in the texture folder";

    const auto again = lib.importImage(ResourceKind::Texture, file);
    ASSERT_TRUE(again.ok());
    EXPECT_EQ(again.value(), QStringLiteral("My Picture 2"));
    const auto clash = lib.importImage(ResourceKind::BlotMap, file);
    ASSERT_TRUE(clash.ok());
    EXPECT_EQ(clash.value(), QStringLiteral("My Picture")) << "forms and textures are separate lists";

    BrushResourceLibrary reread(dir.filePath(QStringLiteral("brushes")), BrushResourceLibrary::BuiltIns::Left);
    EXPECT_NE(reread.texture(QStringLiteral("My Picture 2")), nullptr);
    EXPECT_NE(reread.form(QStringLiteral("My Picture")), nullptr);

    EXPECT_TRUE(lib.remove(ResourceKind::Texture, QStringLiteral("My Picture 2")));
    EXPECT_EQ(lib.texture(QStringLiteral("My Picture 2")), nullptr);
    EXPECT_FALSE(lib.remove(ResourceKind::Texture, QStringLiteral("Not There")));
}

TEST(BrushResources, BigImportsAreScaledDownAndBrokenFilesRefused) {
    QTemporaryDir dir;
    BrushResourceLibrary lib(dir.filePath(QStringLiteral("brushes")), BrushResourceLibrary::BuiltIns::Left);
    QImage big(5000, 1000, QImage::Format_RGB32);
    big.fill(Qt::gray);
    const QString bigFile = dir.filePath(QStringLiteral("big.jpg"));
    ASSERT_TRUE(big.save(bigFile));
    const auto imported = lib.importImage(ResourceKind::BlotMap, bigFile);
    ASSERT_TRUE(imported.ok());
    const QSize size = lib.form(imported.value())->image.size();
    EXPECT_EQ(size.width(), BrushResourceLibrary::kMaxImportSide);
    EXPECT_NEAR(size.height(), BrushResourceLibrary::kMaxImportSide / 5, 1) << "the shape kept";

    const QString notAnImage = dir.filePath(QStringLiteral("notes.png"));
    QFile f(notAnImage);
    ASSERT_TRUE(f.open(QIODevice::WriteOnly));
    f.write("hello");
    f.close();
    const auto refused = lib.importImage(ResourceKind::BlotMap, notAnImage);
    ASSERT_FALSE(refused.ok());
    EXPECT_EQ(refused.error(), ResourceError::Unreadable);
}

TEST(BrushResources, PaperTexturesHaveTheirOwnListAndNameSpace) {
    QTemporaryDir dir;
    BrushResourceLibrary lib(dir.filePath(QStringLiteral("brushes")), BrushResourceLibrary::BuiltIns::Left);
    int changes = 0;
    QObject::connect(&lib, &BrushResourceLibrary::changed, [&] { ++changes; });
    ASSERT_TRUE(lib.add({{ResourceKind::PaperTexture, QStringLiteral("Weave"), grey()},
                         {ResourceKind::Texture, QStringLiteral("Weave"), grey()},
                         {ResourceKind::Shape, QStringLiteral("Stamp"), grey()}})
                    .ok());
    EXPECT_EQ(changes, 1) << "one change for the whole batch";
    EXPECT_TRUE(QFile::exists(dir.filePath(QStringLiteral("brushes/papers/Weave.png"))));
    ASSERT_NE(lib.paperTexture(QStringLiteral("Weave")), nullptr);
    EXPECT_EQ(lib.paperTexture(QStringLiteral("Weave"))->kind, ResourceKind::PaperTexture);
    EXPECT_EQ(namesOf(lib.paperTextures()), (QStringList{"Weave"}));
    EXPECT_EQ(lib.texture(QStringLiteral("Weave"))->kind, ResourceKind::Texture) << "the brush texture of that name is its own";
    EXPECT_FALSE(isForm(ResourceKind::PaperTexture));
    BrushResourceLibrary again(dir.filePath(QStringLiteral("brushes")), BrushResourceLibrary::BuiltIns::Left);
    EXPECT_NE(again.paperTexture(QStringLiteral("Weave")), nullptr);
    EXPECT_TRUE(lib.remove(ResourceKind::PaperTexture, QStringLiteral("Weave")));
    EXPECT_EQ(lib.paperTexture(QStringLiteral("Weave")), nullptr);
    EXPECT_NE(lib.texture(QStringLiteral("Weave")), nullptr);
}

TEST(BrushResources, LookupReservedAndSafeNames) {
    QTemporaryDir dir;
    BrushResourceLibrary lib(dir.path(), BrushResourceLibrary::BuiltIns::Left);
    ASSERT_TRUE(lib.add({{ResourceKind::Bristle, QStringLiteral("Hairy"), grey()},
                         {ResourceKind::BlotMap, QStringLiteral("Blotty"), grey()},
                         {ResourceKind::Texture, QStringLiteral("Weave"), grey()},
                         {ResourceKind::PaperTexture, QStringLiteral("Weave"), grey()}})
                    .ok());
    EXPECT_NE(lib.lookup(ResourceKind::BlotMap, QStringLiteral("Hairy")), nullptr) << "forms share one name space";
    EXPECT_NE(lib.lookup(ResourceKind::Shape, QStringLiteral("Blotty")), nullptr);
    EXPECT_EQ(lib.lookup(ResourceKind::Texture, QStringLiteral("Blotty")), nullptr);
    EXPECT_NE(lib.lookup(ResourceKind::Texture, QStringLiteral("Weave")), nullptr);
    EXPECT_NE(lib.lookup(ResourceKind::PaperTexture, QStringLiteral("Weave")), nullptr);
    EXPECT_EQ(lib.lookup(ResourceKind::PaperTexture, QStringLiteral("Blotty")), nullptr) << "a form is no paper";
    EXPECT_TRUE(BrushResourceLibrary::isReserved(BrushResourceLibrary::kLayerPaper));
    EXPECT_FALSE(BrushResourceLibrary::isReserved(QStringLiteral("Weave")));
    EXPECT_EQ(BrushResourceLibrary::safeName(QStringLiteral("a/b:c")), QStringLiteral("a-b-c"));
    EXPECT_EQ(BrushResourceLibrary::safeName(QStringLiteral("  ")), QStringLiteral("Imported"));
}

TEST(BrushResources, AFileCannotTakeTheNameOfANoneEntry) {
    QTemporaryDir dir;
    const QString folder = resourceFolder(ResourceKind::BlotMap);
    ASSERT_TRUE(QDir(dir.path()).mkpath(folder));
    ASSERT_TRUE(grey().save(QDir(dir.path()).filePath(folder + QStringLiteral("/Round.png"))));
    BrushResourceLibrary lib(dir.path(), BrushResourceLibrary::BuiltIns::Left);
    EXPECT_EQ(lib.form(BrushResourceLibrary::kSimpleCircle), nullptr) << "tools without a form stay plain";
    EXPECT_NE(lib.form(QStringLiteral("Round (file)")), nullptr);
}

// A real set to paint with, each with its picture.
TEST(BrushResources, TheBuiltInPicturesAreARealSetOfEveryKind) {
    QTemporaryDir dir;
    BrushResourceLibrary lib(dir.path());
    for (ResourceKind kind : kResourceKinds) {
        const auto list = lib.ofKind(kind);
        EXPECT_GE(list.size(), 3u) << resourceFolder(kind).toStdString();
        for (const BrushResource* r : list) {
            EXPECT_TRUE(r->builtIn) << r->name.toStdString();
            EXPECT_EQ(r->image.format(), QImage::Format_Grayscale8);
            // A bristle map is the reference's size, 63 px a side, a pixel a hair; every other picture is larger.
            EXPECT_GE(r->image.width(), kind == ResourceKind::Bristle ? 63 : 128) << r->name.toStdString();
            int lightest = 0, darkest = 255;
            for (int y = 0; y < r->image.height(); ++y)
                for (int x = 0; x < r->image.width(); ++x) {
                    lightest = std::max(lightest, qGray(r->image.pixel(x, y)));
                    darkest = std::min(darkest, qGray(r->image.pixel(x, y)));
                }
            EXPECT_GT(lightest - darkest, 100) << r->name.toStdString() << ": a picture, not a plain grey";
            EXPECT_FALSE(BrushResourceLibrary::isReserved(r->name));
        }
    }
    for (const char* placeholder : {"Tip One", "Speckle", "Weave"})
        EXPECT_EQ(lib.lookup(ResourceKind::Texture, QString::fromLatin1(placeholder)), nullptr);
    for (const char* name : {"Soft Edge", "Blots", "Round Bristle"})
        EXPECT_NE(lib.form(QString::fromLatin1(name)), nullptr) << name;
    EXPECT_NE(lib.texture(QStringLiteral("Canvas")), nullptr);
    EXPECT_NE(lib.paperTexture(QStringLiteral("Watercolor Paper")), nullptr);
    EXPECT_EQ(lib.form(BrushResourceLibrary::kSimpleCircle), nullptr) << "the starting tools' round tip is still no picture";
}

// A brush shape's settings file, in the reference's layout, sits beside its picture: it's read with it, comes along when the picture is
// imported, renamed or removed, and choosing the shape puts its settings into the tool's. A key it leaves out keeps the
// reference's default.
TEST(BrushResources, AShapesSettingsFileComesWithIt) {
    QTemporaryDir source, dir;
    QImage picture(32, 32, QImage::Format_Grayscale8);
    picture.fill(0);
    ASSERT_TRUE(picture.save(source.filePath(QStringLiteral("Splash.png"))));
    QFile ini(source.filePath(QStringLiteral("Splash.ini")));
    ASSERT_TRUE(ini.open(QIODevice::WriteOnly | QIODevice::Text));
    ini.write(";\n; Initial Parameters\n;\n[Parameters]\n\t; [Density]\n\tDensity = 40\n\tAngle = -30\n\tSpacing = 12 ; a note\n"
              "\tHighQualitySampling = 1\n");
    ini.close();
    BrushResourceLibrary lib(dir.path());
    ASSERT_TRUE(lib.importImage(ResourceKind::Shape, source.filePath(QStringLiteral("Splash.png"))).ok());
    ASSERT_NE(lib.form(QStringLiteral("Splash")), nullptr);
    ASSERT_TRUE(lib.form(QStringLiteral("Splash"))->shapeSettings.has_value());
    const ShapeSettings read = *lib.form(QStringLiteral("Splash"))->shapeSettings;
    EXPECT_EQ(read.density, 40);
    EXPECT_EQ(read.angle, -30);
    EXPECT_EQ(read.spacing, 12);
    EXPECT_TRUE(read.highQuality);
    EXPECT_EQ(read.angleControl, 1) << "left out, the reference's default";
    EXPECT_EQ(read.scaling, 100);
    EXPECT_FALSE(read.oneStroke) << "left out: the reference's files don't have it";
    BrushSettings settings = defaultSettings(ToolType::Pencil);
    applyShapeSettings(settings, read);
    EXPECT_EQ(settings.shapeDensity, 40);
    EXPECT_EQ(settings.shapeAngle, -30);
    EXPECT_EQ(settings.shapeSpacing, 12);
    EXPECT_EQ(settings.shapeAngleControl, 1);
    EXPECT_TRUE(settings.shapeHighQuality);
    ASSERT_TRUE(lib.rename(ResourceKind::Shape, QStringLiteral("Splash"), QStringLiteral("Drop")).ok());
    ASSERT_TRUE(lib.form(QStringLiteral("Drop"))->shapeSettings.has_value());
    EXPECT_EQ(lib.form(QStringLiteral("Drop"))->shapeSettings->density, 40);
    ASSERT_TRUE(lib.remove(ResourceKind::Shape, QStringLiteral("Drop")));
    EXPECT_FALSE(QFile::exists(QDir(lib.kindFolder(ResourceKind::Shape)).filePath(QStringLiteral("Drop.ini"))));
}

// Flat Nib comes with a settings file that makes it a calligraphic nib: held still at its Angle, its stamps close enough that
// none shows. Choosing it puts those into the tool's.
TEST(BrushResources, FlatNibIsHeldStillAsACalligraphicNib) {
    QTemporaryDir dir;
    const BrushResourceLibrary lib(dir.path());
    const BrushResource* nib = lib.form(QStringLiteral("Flat Nib"));
    ASSERT_NE(nib, nullptr);
    ASSERT_TRUE(nib->shapeSettings.has_value());
    EXPECT_EQ(nib->shapeSettings->angleControl, 0);
    EXPECT_EQ(nib->shapeSettings->angle, -45);
    EXPECT_EQ(nib->shapeSettings->spacing, 1);
    EXPECT_EQ(nib->shapeSettings->density, 100) << "left out, the default";
    EXPECT_TRUE(nib->shapeSettings->oneStroke) << "its stamps laid as one stroke, so Dens. sets its tone";
    for (const BrushResource* other : lib.ofKind(ResourceKind::Shape)) {
        if (other->name != nib->name) {
            EXPECT_FALSE(other->shapeSettings.has_value()) << other->name.toStdString();
        }
    }
}

// A grain repeats across the canvas: its left and right edges, and its top and bottom, meet as its neighbouring pixels do.
TEST(BrushResources, BuiltInTexturesAndPapersRepeatWithoutASeam) {
    QTemporaryDir dir;
    BrushResourceLibrary lib(dir.path());
    std::vector<const BrushResource*> grains = lib.textures();
    for (const BrushResource* r : lib.paperTextures())
        grains.push_back(r);
    for (const BrushResource* r : grains) {
        const QImage& im = r->image;
        double seam = 0, inside = 0;
        for (int y = 0; y < im.height(); ++y) {
            seam += std::abs(qGray(im.pixel(0, y)) - qGray(im.pixel(im.width() - 1, y)));
            inside += std::abs(qGray(im.pixel(im.width() / 2, y)) - qGray(im.pixel(im.width() / 2 - 1, y)));
        }
        EXPECT_LT(seam, inside * 1.6 + im.height() * 4.0) << r->name.toStdString();
    }
}

// Built-ins are shown but can't be removed or renamed, and a file of the same name replaces
// one.
TEST(BrushResources, AFileOfABuiltInsNameTakesItsPlaceUntilItIsRemoved) {
    QTemporaryDir dir;
    BrushResourceLibrary lib(dir.path());
    const BrushResource* builtIn = lib.form(QStringLiteral("Blots"));
    ASSERT_NE(builtIn, nullptr);
    ASSERT_TRUE(builtIn->builtIn);
    EXPECT_FALSE(lib.remove(ResourceKind::BlotMap, QStringLiteral("Blots")));
    EXPECT_EQ(lib.rename(ResourceKind::BlotMap, QStringLiteral("Blots"), QStringLiteral("Mine")).error(), ResourceError::BuiltIn);
    const std::size_t before = lib.forms().size();

    const QString file = dir.filePath(QStringLiteral("Blots.png"));
    ASSERT_TRUE(grey(40).save(file));
    const auto added = lib.importImage(ResourceKind::BlotMap, file, BrushResourceLibrary::SameName::Replace);
    ASSERT_TRUE(added.ok());
    EXPECT_EQ(added.value(), QStringLiteral("Blots"));
    EXPECT_EQ(lib.forms().size(), before) << "in its place, not beside it";
    EXPECT_FALSE(lib.form(QStringLiteral("Blots"))->builtIn);
    EXPECT_EQ(qGray(lib.form(QStringLiteral("Blots"))->image.pixel(3, 3)), 40);

    ASSERT_TRUE(grey(90).save(file));
    ASSERT_TRUE(lib.importImage(ResourceKind::BlotMap, file, BrushResourceLibrary::SameName::Replace).ok());
    EXPECT_EQ(qGray(lib.form(QStringLiteral("Blots"))->image.pixel(3, 3)), 90) << "the user's own is replaced too";
    EXPECT_EQ(lib.forms().size(), before);

    EXPECT_TRUE(lib.remove(ResourceKind::BlotMap, QStringLiteral("Blots")));
    ASSERT_NE(lib.form(QStringLiteral("Blots")), nullptr);
    EXPECT_TRUE(lib.form(QStringLiteral("Blots"))->builtIn) << "the built-in one is back";

    const auto renumbered = lib.importImage(ResourceKind::BlotMap, file);
    ASSERT_TRUE(renumbered.ok());
    EXPECT_EQ(renumbered.value(), QStringLiteral("Blots 2")) << "the tool panel's import keeps both";
}

TEST(BrushResources, RenamingMovesTheFileAndRefusesATakenName) {
    QTemporaryDir dir;
    BrushResourceLibrary lib(dir.path());
    ASSERT_TRUE(
        lib.add({{ResourceKind::Texture, QStringLiteral("Mine"), grey()}, {ResourceKind::Texture, QStringLiteral("Other"), grey()}}).ok());
    std::vector<std::pair<QString, QString>> renamed;
    QObject::connect(&lib, &BrushResourceLibrary::renamed, [&](ResourceKind kind, const QString& from, const QString& to) {
        EXPECT_EQ(kind, ResourceKind::Texture);
        renamed.emplace_back(from, to);
    });
    EXPECT_EQ(lib.rename(ResourceKind::Texture, QStringLiteral("Mine"), QStringLiteral("Other")).error(), ResourceError::NameTaken);
    EXPECT_EQ(lib.rename(ResourceKind::Texture, QStringLiteral("Mine"), BrushResourceLibrary::kNoTexture).error(),
              ResourceError::NameTaken);
    EXPECT_EQ(lib.rename(ResourceKind::Texture, QStringLiteral("Gone"), QStringLiteral("X")).error(), ResourceError::NotFound);
    const auto done = lib.rename(ResourceKind::Texture, QStringLiteral("Mine"), QStringLiteral("My: Grain"));
    ASSERT_TRUE(done.ok());
    EXPECT_EQ(done.value(), QStringLiteral("My- Grain"));
    ASSERT_EQ(renamed.size(), 1u);
    EXPECT_EQ(renamed[0], std::make_pair(QStringLiteral("Mine"), QStringLiteral("My- Grain")));
    EXPECT_EQ(lib.texture(QStringLiteral("Mine")), nullptr);
    ASSERT_NE(lib.texture(QStringLiteral("My- Grain")), nullptr);
    EXPECT_TRUE(QFile::exists(QDir(lib.kindFolder(ResourceKind::Texture)).filePath(QStringLiteral("My- Grain.png"))));
    EXPECT_TRUE(lib.rename(ResourceKind::Texture, QStringLiteral("My- Grain"), QStringLiteral("Canvas")).ok())
        << "a built-in's name may be taken: the file replaces it";
    EXPECT_FALSE(lib.texture(QStringLiteral("Canvas"))->builtIn);
}

TEST(BrushResources, ToolsFollowARenamedPicture) {
    ToolBox tools;
    BrushSettings s = tools.settings();
    s.form = QStringLiteral("Mine");
    s.texture = QStringLiteral("Mine");
    tools.setSettings(s);
    int changed = 0;
    QObject::connect(&tools, &ToolBox::modified, [&] { ++changed; });
    EXPECT_EQ(tools.renameResource(true, QStringLiteral("Mine"), QStringLiteral("Yours")), 1);
    EXPECT_EQ(tools.settings().form, QStringLiteral("Yours"));
    EXPECT_EQ(tools.settings().texture, QStringLiteral("Mine")) << "a texture of that name is another picture";
    EXPECT_EQ(changed, 1);
    EXPECT_EQ(tools.renameResource(false, QStringLiteral("Mine"), QStringLiteral("Grain")), 1);
    EXPECT_EQ(tools.settings().texture, QStringLiteral("Grain"));
    EXPECT_EQ(tools.renameResource(true, QStringLiteral("Nobody"), QStringLiteral("X")), 0);
    EXPECT_EQ(changed, 2);
}
