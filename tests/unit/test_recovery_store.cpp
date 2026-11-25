#include "IoTestSupport.h"

#include "core/io/FileIo.h"
#include "core/io/RecoveryStore.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTimeZone>

#include <gtest/gtest.h>

#include <memory>

using namespace doodlebold;
using namespace doodlebold::testing;

namespace {

const QString kCanvas = QStringLiteral("canvas-1");

RecoveryManifest manifestAt(int second, const QString& name = QStringLiteral("Portrait.doodle")) {
    return {name, QStringLiteral("C:/Art/Portrait.doodle"),
            QDateTime(QDate(2026, 11, 25), QTime(10, 0, second, 250), QTimeZone::UTC)};
}

// A run that ends without end(), as a crash would leave it: its lock goes with the process, its copies stay.
QString crashedRunWithACopy(const QString& root, int second, const QString& name = QStringLiteral("Portrait.doodle")) {
    RecoveryStore crashed(root);
    EXPECT_TRUE(crashed.isOpen());
    EXPECT_TRUE(crashed.writeCopy(kCanvas, richContent(), manifestAt(second, name), 3));
    return crashed.runFolder();
}

QStringList filesIn(const QString& folder) {
    return QDir(folder).entryList(QDir::Files, QDir::Name);
}

}

TEST(RecoveryStore, AManifestReadsBackAsWritten) {
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("copy.json"));
    const RecoveryManifest written = manifestAt(7, QStringLiteral("Skizze é中.doodle"));
    ASSERT_TRUE(RecoveryStore::writeManifest(path, written));
    EXPECT_EQ(RecoveryStore::readManifest(path), written);

    RecoveryManifest unsaved = manifestAt(8, QStringLiteral("NewCanvas1"));
    unsaved.originalPath.clear();
    ASSERT_TRUE(RecoveryStore::writeManifest(path, unsaved));
    EXPECT_EQ(RecoveryStore::readManifest(path), unsaved) << "a canvas never saved has no original file";
}

TEST(RecoveryStore, ADamagedOrMissingManifestIsNone) {
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("copy.json"));
    EXPECT_FALSE(RecoveryStore::readManifest(path)) << "missing";
    QFile file(path);
    ASSERT_TRUE(file.open(QIODevice::WriteOnly));
    file.write("{\"name\": \"Portrait.doodle\", \"savedAt\": ");
    file.close();
    EXPECT_FALSE(RecoveryStore::readManifest(path)) << "cut short";
    ASSERT_TRUE(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    file.write("{\"name\": \"Portrait.doodle\"}");
    file.close();
    EXPECT_FALSE(RecoveryStore::readManifest(path)) << "no time";
}

TEST(RecoveryStore, ACopyIsTheCanvasInTheDoodleFormatWithItsManifest) {
    QTemporaryDir root;
    RecoveryStore store(root.path());
    ASSERT_TRUE(store.isOpen());
    EXPECT_TRUE(store.runFolder().startsWith(root.path() + QStringLiteral("/run-")));
    const DocumentContent content = richContent();
    ASSERT_TRUE(store.writeCopy(kCanvas, content, manifestAt(1), 3));

    const std::vector<RecoveryCopy> copies = RecoveryStore::copiesIn(store.runFolder() + QLatin1Char('/') + kCanvas);
    ASSERT_EQ(copies.size(), 1u);
    EXPECT_EQ(copies[0].manifest, manifestAt(1));
    EXPECT_TRUE(copies[0].file.endsWith(QStringLiteral("/20261125-100001-250.doodle"))) << copies[0].file.toStdString();
    const FileResult<LoadedDocument> loaded = io::loadFile(copies[0].file);
    ASSERT_TRUE(loaded);
    EXPECT_EQ(loaded.value().content.size(), content.size());
    EXPECT_EQ(loaded.value().content.state.layers.layerCount(), content.state.layers.layerCount());
}

TEST(RecoveryStore, OnlyTheNewestCopiesAreKept) {
    QTemporaryDir root;
    RecoveryStore store(root.path());
    for (int second = 0; second < 5; ++second)
        ASSERT_TRUE(store.writeCopy(kCanvas, richContent(), manifestAt(second), 3));
    const QString folder = store.runFolder() + QLatin1Char('/') + kCanvas;
    const std::vector<RecoveryCopy> copies = RecoveryStore::copiesIn(folder);
    ASSERT_EQ(copies.size(), 3u);
    EXPECT_EQ(copies[0].manifest.savedAt, manifestAt(4).savedAt) << "newest first";
    EXPECT_EQ(copies[1].manifest.savedAt, manifestAt(3).savedAt);
    EXPECT_EQ(copies[2].manifest.savedAt, manifestAt(2).savedAt);
    EXPECT_EQ(filesIn(folder).size(), 6) << "each copy with its manifest, the older ones gone with theirs";

    ASSERT_TRUE(store.writeCopy(kCanvas, richContent(), manifestAt(5), 1));
    EXPECT_EQ(filesIn(folder), (QStringList{QStringLiteral("20261125-100005-250.doodle"), QStringLiteral("20261125-100005-250.json")}));
}

TEST(RecoveryStore, ACopyWithoutItsManifestIsNotOffered) {
    QTemporaryDir root;
    RecoveryStore store(root.path());
    ASSERT_TRUE(store.writeCopy(kCanvas, richContent(), manifestAt(1), 3));
    const QString folder = store.runFolder() + QLatin1Char('/') + kCanvas;
    ASSERT_TRUE(QFile::remove(folder + QStringLiteral("/20261125-100001-250.json")));
    EXPECT_TRUE(RecoveryStore::copiesIn(folder).empty());
}

TEST(RecoveryStore, ARunThatEndsNormallyLeavesNothing) {
    QTemporaryDir root;
    RecoveryStore store(root.path());
    ASSERT_TRUE(store.writeCopy(kCanvas, richContent(), manifestAt(1), 3));
    const QString run = store.runFolder();
    store.end();
    EXPECT_FALSE(store.isOpen());
    EXPECT_FALSE(QDir(run).exists());
    EXPECT_FALSE(store.writeCopy(kCanvas, richContent(), manifestAt(2), 3)) << "an ended run writes nothing";
    EXPECT_FALSE(QDir(run).exists());
    EXPECT_TRUE(RecoveryStore::leftovers(root.path()).empty());
}

TEST(RecoveryStore, AClosedCanvasLosesItsCopies) {
    QTemporaryDir root;
    RecoveryStore store(root.path());
    ASSERT_TRUE(store.writeCopy(kCanvas, richContent(), manifestAt(1), 3));
    ASSERT_TRUE(store.writeCopy(QStringLiteral("canvas-2"), richContent(), manifestAt(1), 3));
    store.removeCanvas(kCanvas);
    EXPECT_FALSE(QDir(store.runFolder() + QLatin1Char('/') + kCanvas).exists());
    EXPECT_TRUE(QDir(store.runFolder() + QStringLiteral("/canvas-2")).exists());
}

TEST(RecoveryStore, ACrashedRunsCopiesAreLeftoversAndARunningOnesAreNot) {
    QTemporaryDir root;
    RecoveryStore running(root.path());
    ASSERT_TRUE(running.writeCopy(kCanvas, richContent(), manifestAt(9, QStringLiteral("Running.doodle")), 3));
    const QString crashed = crashedRunWithACopy(root.path(), 1);
    EXPECT_NE(crashed, running.runFolder());

    const std::vector<LeftoverCanvas> found = RecoveryStore::leftovers(root.path());
    ASSERT_EQ(found.size(), 1u);
    EXPECT_EQ(found[0].folder, crashed + QLatin1Char('/') + kCanvas);
    ASSERT_EQ(found[0].copies.size(), 1u);
    EXPECT_EQ(found[0].copies[0].manifest, manifestAt(1));
    EXPECT_TRUE(running.isOpen()) << "looking leaves the running run's lock alone";
    EXPECT_EQ(RecoveryStore::leftovers(root.path()).size(), 1u) << "kept until restored or discarded";
}

TEST(RecoveryStore, LeftoversComeTheMostRecentlyCopiedFirst) {
    QTemporaryDir root;
    crashedRunWithACopy(root.path(), 1, QStringLiteral("Older.doodle"));
    crashedRunWithACopy(root.path(), 30, QStringLiteral("Newer.doodle"));
    crashedRunWithACopy(root.path(), 15, QStringLiteral("Middle.doodle"));
    const std::vector<LeftoverCanvas> found = RecoveryStore::leftovers(root.path());
    ASSERT_EQ(found.size(), 3u);
    EXPECT_EQ(found[0].copies[0].manifest.name, QStringLiteral("Newer.doodle"));
    EXPECT_EQ(found[1].copies[0].manifest.name, QStringLiteral("Middle.doodle"));
    EXPECT_EQ(found[2].copies[0].manifest.name, QStringLiteral("Older.doodle"));
}

TEST(RecoveryStore, ACrashedRunWithNoCopyIsClearedAway) {
    QTemporaryDir root;
    QString run;
    {
        RecoveryStore crashed(root.path());
        run = crashed.runFolder();
        ASSERT_TRUE(QDir().mkpath(run + QStringLiteral("/canvas-3")));  // a canvas whose first copy never got written
    }
    ASSERT_TRUE(QDir(run).exists());
    EXPECT_TRUE(RecoveryStore::leftovers(root.path()).empty());
    EXPECT_FALSE(QDir(run).exists());
}

TEST(RecoveryStore, DiscardDeletesTheCopiesAndTheRunTheyCameFrom) {
    QTemporaryDir root;
    const QString run = crashedRunWithACopy(root.path(), 1);
    const std::vector<LeftoverCanvas> found = RecoveryStore::leftovers(root.path());
    ASSERT_EQ(found.size(), 1u);
    RecoveryStore::discard(found[0]);
    EXPECT_FALSE(QDir(run).exists());
    EXPECT_TRUE(RecoveryStore::leftovers(root.path()).empty());
}

TEST(RecoveryStore, AnAdoptedCanvassCopiesMoveIntoTheRun) {
    QTemporaryDir root;
    const QString crashed = crashedRunWithACopy(root.path(), 1);
    RecoveryStore store(root.path());
    const std::vector<LeftoverCanvas> found = RecoveryStore::leftovers(root.path());
    ASSERT_EQ(found.size(), 1u);
    ASSERT_TRUE(store.adopt(found[0], QStringLiteral("canvas-7")));
    const std::vector<RecoveryCopy> copies = RecoveryStore::copiesIn(store.runFolder() + QStringLiteral("/canvas-7"));
    ASSERT_EQ(copies.size(), 1u);
    EXPECT_EQ(copies[0].manifest, manifestAt(1));
    EXPECT_FALSE(QDir(crashed).exists());
    EXPECT_TRUE(RecoveryStore::leftovers(root.path()).empty()) << "now the running run's";
    store.end();
    EXPECT_FALSE(QDir(store.runFolder()).exists());
}
