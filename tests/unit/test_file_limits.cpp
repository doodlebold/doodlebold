#include "core/document/Document.h"
#include "core/io/DoodleFormat.h"
#include "core/io/FileIo.h"
#include "core/io/FileLimits.h"
#include "core/io/PluginFormat.h"
#include "core/io/PsdFormat.h"

#include <QBuffer>
#include <QFile>
#include <QTemporaryDir>

#include <gtest/gtest.h>

#include <vector>

using namespace doodlebold;

namespace {

// The decode budget for one test, then the machine's again.
struct BudgetOf {
    explicit BudgetOf(std::int64_t bytes) { file_limits::decodeBudgetOverride() = bytes; }
    ~BudgetOf() { file_limits::decodeBudgetOverride() = 0; }
    BudgetOf(const BudgetOf&) = delete;
    BudgetOf& operator=(const BudgetOf&) = delete;
};

// A budget too small for one tile of 64 × 64 pixels.
constexpr std::int64_t kLessThanATile = 1024;

QByteArray written(const DocumentFormat& format, const DocumentContent& content) {
    QBuffer buf;
    buf.open(QIODevice::WriteOnly);
    const auto r = format.write(content, buf, {});
    EXPECT_TRUE(r.ok()) << qPrintable(r.ok() ? QString() : r.error().describe());
    return buf.data();
}

FileResult<LoadedDocument> readBack(const DocumentFormat& format, const QByteArray& bytes) {
    QBuffer buf;
    buf.setData(bytes);
    buf.open(QIODevice::ReadOnly);
    return format.read(buf);
}

DocumentContent painted() {
    Document doc(QSize(200, 150), Qt::white);
    EXPECT_TRUE(doc.fillLayer(doc.currentLayerId(), Qt::blue));
    return doc.content();
}

// A plugin's reader that adds one folder more than a file may hold, and says it went well.
int manyFolders(const doodlebold_read_host* host) {
    const std::uint8_t white[4] = {255, 255, 255, 255};
    host->set_canvas(host->context, 10, 10, 0.0, white);
    doodlebold_layer folder{};
    folder.struct_size = sizeof(doodlebold_layer);
    folder.kind = DOODLEBOLD_LAYER_FOLDER;
    folder.parent = -1;
    for (int i = 0; i <= file_limits::kMaxLayers; ++i)
        host->add_layer(host->context, &folder);
    return DOODLEBOLD_OK;
}

// A plugin's reader that adds one 100 × 100 layer.
int oneLayer(const doodlebold_read_host* host) {
    constexpr int kSide = 100;
    const std::uint8_t white[4] = {255, 255, 255, 255};
    host->set_canvas(host->context, kSide, kSide, 0.0, white);
    std::vector<std::uint16_t> pixels(std::size_t(kSide) * kSide * 4, 0xFFFF);
    doodlebold_layer layer{};
    layer.struct_size = sizeof(doodlebold_layer);
    layer.kind = DOODLEBOLD_LAYER_PIXELS;
    layer.parent = -1;
    layer.opacity = 100;
    layer.visible = 1;
    layer.width = layer.height = kSide;
    layer.pixels = pixels.data();
    layer.stride = std::size_t(kSide) * 4 * sizeof(std::uint16_t);
    return host->add_layer(host->context, &layer) < 0 ? DOODLEBOLD_ERROR_CORRUPT : DOODLEBOLD_OK;
}

PluginFormat pluginReadingWith(int (*read)(const doodlebold_read_host*)) {
    doodlebold_format f{};
    f.struct_size = sizeof(doodlebold_format);
    f.id = "limits-test";
    f.name = "Limits test";
    f.extensions = "limitstest";
    f.read = read;
    return PluginFormat(nullptr, f, {QStringLiteral("limitstest")});
}

}

// A plugin reads files Doodlebold doesn't know, so what it hands over is held to the limits every reader keeps, whatever it returns.
TEST(FileLimits, APluginsLayersAreHeldToTheLayerLimit) {
    const PluginFormat plugin = pluginReadingWith(manyFolders);
    const auto r = readBack(plugin, "anything");
    ASSERT_FALSE(r.ok());
    EXPECT_EQ(r.error().kind, FileError::Kind::TooLarge);
}

TEST(FileLimits, APluginsPixelsAreHeldToTheDecodeBudget) {
    const PluginFormat plugin = pluginReadingWith(oneLayer);
    EXPECT_TRUE(readBack(plugin, "anything").ok()) << "within the machine's budget";
    const BudgetOf small(kLessThanATile);
    const auto r = readBack(plugin, "anything");
    ASSERT_FALSE(r.ok());
    EXPECT_EQ(r.error().kind, FileError::Kind::TooLarge);
}

// Checked before the layer's tiles are decoded: a file's lengths say what its pictures will take.
TEST(FileLimits, ADoodleFilesPixelsAreHeldToTheDecodeBudget) {
    const QByteArray file = written(DoodleFormat(), painted());
    EXPECT_TRUE(readBack(DoodleFormat(), file).ok());
    const BudgetOf small(kLessThanATile);
    const auto r = readBack(DoodleFormat(), file);
    ASSERT_FALSE(r.ok());
    EXPECT_EQ(r.error().kind, FileError::Kind::TooLarge);
}

TEST(FileLimits, APsdFilesPixelsAreHeldToTheDecodeBudget) {
    const QByteArray file = written(PsdFormat(), painted());
    EXPECT_TRUE(readBack(PsdFormat(), file).ok());
    const BudgetOf small(kLessThanATile);
    const auto r = readBack(PsdFormat(), file);
    ASSERT_FALSE(r.ok());
    EXPECT_EQ(r.error().kind, FileError::Kind::TooLarge);
}

// Each writer refuses what its reader would: the file would save, then not open again.
TEST(FileLimits, ADoodleFileWithMoreLayersThanItHoldsIsntWritten) {
    DocumentContent content = painted();
    LayerStack& layers = content.state.layers;
    while (layers.layerCount() <= file_limits::kMaxLayers)
        layers.insert(layers.makeFolder(QStringLiteral("f")), kNoLayer, -1);
    QBuffer buf;
    buf.open(QIODevice::WriteOnly);
    const auto r = DoodleFormat().write(content, buf, {});
    ASSERT_FALSE(r.ok());
    EXPECT_EQ(r.error().kind, FileError::Kind::TooLarge);
    EXPECT_TRUE(buf.data().isEmpty()) << "refused before anything is written";
}

// An 18-byte TGA header may claim 16384 × 16384 pixels it doesn't hold: refused before 2 GB are allocated for them.
TEST(FileLimits, ATgaHeaderClaimingPixelsItDoesntHoldIsRefused) {
    QByteArray tga(18, '\0');
    tga[2] = 2;                                         // truecolour, uncompressed
    tga[12] = char(0x00), tga[13] = char(0x40);         // width 16384
    tga[14] = char(0x00), tga[15] = char(0x40);         // height 16384
    tga[16] = 32;                                       // bits per pixel
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("claims.tga"));
    QFile f(path);
    ASSERT_TRUE(f.open(QIODevice::WriteOnly));
    f.write(tga);
    f.close();
    EXPECT_FALSE(io::loadFile(path).ok());
}
