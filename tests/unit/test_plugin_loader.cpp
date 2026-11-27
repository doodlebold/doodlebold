#include "IoTestSupport.h"

#include "core/io/FileIo.h"
#include "core/io/FormatRegistry.h"
#include "core/io/PluginHost.h"
#include "core/io/PluginLoader.h"
#include "core/layers/Compositor.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>

#include <gtest/gtest.h>

#include <tuple>

using namespace doodlebold;
using namespace doodlebold::testing;

namespace {

// The helper the plugins run in, as this build made it.
const bool kHelperFound = (io::PluginHost::setHelperPath(QStringLiteral(DOODLEBOLD_PLUGIN_HOST)), true);

// These tests are about loading, so every plugin they meet was said yes to.
io::PluginChoice allowAll(const QString&) {
    return io::PluginChoice::Allowed;
}

QString exampleFolder() {
    return QStringLiteral(DOODLEBOLD_EXAMPLE_PLUGIN_DIR);
}

QString testPluginFolder(const char* name) {
    return QDir(QStringLiteral(DOODLEBOLD_TEST_PLUGIN_DIR)).filePath(QString::fromLatin1(name));
}

std::unique_ptr<FormatRegistry> builtInRegistry() {
    auto registry = std::make_unique<FormatRegistry>();
    registerBuiltInFormats(*registry);
    return registry;
}

using FormatSummary = std::tuple<QString, QString, QStringList, bool, bool>;

std::vector<FormatSummary> summaries(const FormatRegistry& registry) {
    std::vector<FormatSummary> out;
    for (const auto& f : registry.all())
        out.emplace_back(f->id(), f->name(), f->extensions(), f->canRead(), f->canWrite());
    return out;
}

QByteArray readAll(const QString& path) {
    QFile f(path);
    EXPECT_TRUE(f.open(QIODevice::ReadOnly)) << qPrintable(path);
    return f.readAll();
}

void writeAll(const QString& path, const QByteArray& bytes) {
    QFile f(path);
    ASSERT_TRUE(f.open(QIODevice::WriteOnly)) << qPrintable(path);
    f.write(bytes);
}

LoadedDocument roundTrip(const FormatRegistry& registry, const DocumentContent& content, const QString& path) {
    const DocumentFormat* format = registry.forPath(path, true);
    EXPECT_NE(format, nullptr) << qPrintable(path);
    if (!format)
        return {};
    const auto saved = io::saveFile(content, path, *format);
    EXPECT_TRUE(saved.ok()) << (saved.ok() ? "" : qPrintable(saved.error().describe()));
    auto loaded = io::loadFile(path, registry);
    EXPECT_TRUE(loaded.ok()) << (loaded.ok() ? "" : qPrintable(loaded.error().describe()));
    return loaded.ok() ? loaded.value() : LoadedDocument{};
}

// Everything the plugin interface carries: a folder with a clipped layer and a layer with a mask, a hidden layer, blend modes,
// opacity, names, pixels up to the canvas's edge and an empty layer.
DocumentContent layeredContent() {
    DocumentContent c;
    LayerStack& s = c.state.layers;
    s = LayerStack(QSize(150, 100));

    LayerNode bottom = raster(s, QStringLiteral("Bottom"), BlendMode::Normal, 100);
    scribble(bottom.pixels = TiledImage(s.size()), QRect(0, 0, 150, 100), 11);
    s.insert(std::move(bottom), kNoLayer, -1);

    LayerNode folder = s.makeFolder(QStringLiteral("Mappe é中"));
    folder.props.mode = BlendMode::PassThrough;
    folder.props.opacity = 64;
    const LayerId folderId = s.insert(std::move(folder), kNoLayer, -1);

    LayerNode base = raster(s, QStringLiteral("Base"), BlendMode::Screen, 90);
    base.pixels = TiledImage(s.size());
    scribble(base.pixels, QRect(20, 30, 70, 50), 12);
    SelectionMask mask(s.size());
    mask.setUniform(0, 0, SelectionMask::kOne);
    std::vector<std::uint16_t> row(60);
    for (std::size_t i = 0; i < row.size(); ++i)
        row[i] = static_cast<std::uint16_t>(i * 1000);
    for (int y = 40; y < 70; ++y)
        mask.writeRow(y, 80, int(row.size()), row.data());
    mask.squeeze();
    base.mask = mask;
    s.insert(std::move(base), folderId, -1);

    LayerNode clipped = raster(s, QStringLiteral("Clipped"), BlendMode::Multiply, 55);
    clipped.pixels = TiledImage(s.size());
    scribble(clipped.pixels, QRect(100, 60, 50, 40), 13);
    clipped.props.clipping = true;
    clipped.mask = mask;
    clipped.props.maskEnabled = false;
    s.insert(std::move(clipped), folderId, -1);

    LayerNode hidden = raster(s, QStringLiteral("Hidden"), BlendMode::Overlay, 30);
    hidden.props.visible = false;
    hidden.pixels = TiledImage(s.size());
    s.insert(std::move(hidden), kNoLayer, -1);

    c.state.current = s.displayOrder().front()->props.id;
    c.state.background = CanvasBackground::fromPaper(QColor(250, 245, 230));
    c.resolution = 300.0;
    return c;
}

// The canvas's pixels only: a file keeps nothing outside it.
void expectSameOnCanvas(const TiledImage& a, const TiledImage& b) {
    ASSERT_EQ(a.size(), b.size());
    for (int y = 0; y < a.size().height(); ++y)
        for (int x = 0; x < a.size().width(); ++x)
            ASSERT_EQ(a.pixel(x, y), b.pixel(x, y)) << x << "," << y;
}

TiledImage flattenedOverTransparency(const DocumentContent& c) {
    std::vector<const LayerNode*> nodes;
    for (const LayerNode& n : c.state.layers.root().children)
        nodes.push_back(&n);
    return Compositor::flatten(nodes, c.size());
}

}

TEST(PluginLoader, TheExamplePluginJoinsOpenAndSave) {
    auto registry = builtInRegistry();
    const auto report = io::loadFormatPlugins(*registry, {exampleFolder()}, allowAll);
    EXPECT_EQ(report.loaded, QStringList{QStringLiteral("Farbfeld")});
    EXPECT_TRUE(report.notes.isEmpty()) << qPrintable(report.notes.join(QLatin1Char('\n')));
    for (const bool writing : {false, true}) {
        const DocumentFormat* format = registry->forPath(QStringLiteral("picture.ff"), writing);
        ASSERT_NE(format, nullptr);
        EXPECT_EQ(format->id(), QStringLiteral("farbfeld"));
        EXPECT_EQ(format->name(), QStringLiteral("Farbfeld image"));
    }
    EXPECT_TRUE(registry->openFilter().contains(QStringLiteral("Farbfeld image (*.ff)")));
    EXPECT_TRUE(registry->readableExtensions().contains(QStringLiteral("ff")));
}

TEST(PluginLoader, AFarbfeldRoundTripKeepsTheCanvasAt16Bits) {
    auto registry = builtInRegistry();
    io::loadFormatPlugins(*registry, {exampleFolder()}, allowAll);
    DocumentContent content = layeredContent();
    content.state.background.mode = BackgroundMode::TransparencyWhite;
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("picture.ff"));
    const LoadedDocument loaded = roundTrip(*registry, content, path);

    const QByteArray bytes = readAll(path);
    ASSERT_EQ(bytes.size(), 16 + 150 * 100 * 8);
    EXPECT_EQ(bytes.left(8), QByteArray("farbfeld"));
    EXPECT_EQ(bytes.mid(8, 8), QByteArray::fromHex("0000009600000064")) << "width and height, big-endian";

    ASSERT_EQ(loaded.content.state.layers.root().children.size(), 1u);
    EXPECT_TRUE(loaded.content.background().isTransparent());
    expectSameOnCanvas(loaded.content.state.layers.root().children[0].pixels, flattenedOverTransparency(content));
}

TEST(PluginLoader, AnOpaqueCanvasIsSavedOverItsBackground) {
    auto registry = builtInRegistry();
    io::loadFormatPlugins(*registry, {exampleFolder()}, allowAll);
    const DocumentContent content = layeredContent();
    QTemporaryDir dir;
    const LoadedDocument loaded = roundTrip(*registry, content, dir.filePath(QStringLiteral("picture.ff")));
    ASSERT_EQ(loaded.content.state.layers.root().children.size(), 1u);
    const TiledImage expected = Compositor::flattenOverPaper(content.state.layers, content.background().paperColor());
    expectSameOnCanvas(loaded.content.state.layers.root().children[0].pixels, expected);
}

TEST(PluginLoader, APluginFileIsRecognisedByItsContent) {
    auto registry = builtInRegistry();
    io::loadFormatPlugins(*registry, {exampleFolder()}, allowAll);
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("picture.ff"));
    roundTrip(*registry, layeredContent(), path);
    const QString renamed = dir.filePath(QStringLiteral("picture"));
    ASSERT_TRUE(QFile::rename(path, renamed));
    const auto loaded = io::loadFile(renamed, *registry);
    ASSERT_TRUE(loaded.ok()) << qPrintable(loaded.error().describe());
    EXPECT_EQ(loaded.value().content.size(), QSize(150, 100));
}

TEST(PluginLoader, EveryLayerFieldSurvivesARoundTripThroughThePlugin) {
    auto registry = builtInRegistry();
    const auto report = io::loadFormatPlugins(*registry, {testPluginFolder("layer_dump")}, allowAll);
    ASSERT_EQ(report.loaded, QStringList{QStringLiteral("Layer dump")});
    const DocumentContent content = layeredContent();
    QTemporaryDir dir;
    const LoadedDocument loaded = roundTrip(*registry, content, dir.filePath(QStringLiteral("layers.layerdump")));

    EXPECT_EQ(loaded.notes, QStringList{QStringLiteral("read by the layer dump plugin")});
    EXPECT_EQ(loaded.content.size(), content.size());
    EXPECT_EQ(loaded.content.state.background, content.state.background);
    EXPECT_DOUBLE_EQ(loaded.content.resolution, 300.0);
    const auto& a = content.state.layers.root().children;
    const auto& b = loaded.content.state.layers.root().children;
    ASSERT_EQ(a.size(), b.size());
    for (std::size_t i = 0; i < a.size(); ++i)
        expectSameNode(a[i], b[i], false);
    EXPECT_EQ(loaded.content.state.current, loaded.content.state.layers.displayOrder().front()->props.id) << "the top layer";
}

TEST(PluginLoader, AnUnknownBlendModeOpensAsNormalWithANote) {
    auto registry = builtInRegistry();
    io::loadFormatPlugins(*registry, {testPluginFolder("layer_dump")}, allowAll);
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("layers.layerdump"));
    roundTrip(*registry, layeredContent(), path);
    QByteArray bytes = readAll(path);
    ASSERT_EQ(bytes.count("multiply"), 1);
    writeAll(path, bytes.replace("multiply", "mulXiply"));

    const auto loaded = io::loadFile(path, *registry);
    ASSERT_TRUE(loaded.ok()) << qPrintable(loaded.error().describe());
    EXPECT_EQ(loaded.value().content.state.layers.root().children[1].children[1].props.mode, BlendMode::Normal);
    EXPECT_TRUE(loaded.value().notes.contains(QStringLiteral("Blend mode \"mulXiply\" is not available in Doodlebold; Normal is used.")));
}

TEST(PluginLoader, ADamagedFileIsAnErrorNotACrash) {
    auto registry = builtInRegistry();
    io::loadFormatPlugins(*registry, {testPluginFolder("layer_dump"), exampleFolder()}, allowAll);
    QTemporaryDir dir;
    for (const QString& name : {QStringLiteral("layers.layerdump"), QStringLiteral("picture.ff")}) {
        const QString path = dir.filePath(name);
        roundTrip(*registry, layeredContent(), path);
        const QByteArray bytes = readAll(path);
        for (const qsizetype keep : {qsizetype{0}, qsizetype{12}, qsizetype{40}, bytes.size() / 2, bytes.size() - 1}) {
            writeAll(path, bytes.left(keep));
            const auto loaded = io::loadFile(path, *registry);
            EXPECT_FALSE(loaded.ok()) << qPrintable(name) << " cut to " << keep;
        }
    }
}

TEST(PluginLoader, BrokenAndIncompatiblePluginsAreSkippedWithANote) {
    for (const char* name : {"no_entry", "future_version", "declines", "broken_formats"}) {
        SCOPED_TRACE(name);
        auto registry = builtInRegistry();
        const auto before = summaries(*registry);
        const auto report = io::loadFormatPlugins(*registry, {testPluginFolder(name)}, allowAll);
        EXPECT_TRUE(report.loaded.isEmpty());
        EXPECT_FALSE(report.notes.isEmpty());
        EXPECT_EQ(summaries(*registry), before);
    }
    auto registry = builtInRegistry();
    const auto future = io::loadFormatPlugins(*registry, {testPluginFolder("future_version")}, allowAll);
    ASSERT_EQ(future.notes.size(), 1);
    EXPECT_TRUE(future.notes[0].contains(QStringLiteral("built for plugin interface version 2"))) << qPrintable(future.notes[0]);
    const auto broken = io::loadFormatPlugins(*registry, {testPluginFolder("broken_formats")}, allowAll);
    EXPECT_EQ(broken.notes.size(), 2) << qPrintable(broken.notes.join(QLatin1Char('\n')));
}

TEST(PluginLoader, AFileThatIsNotAPluginIsSkippedWithANote) {
    QTemporaryDir dir;
#ifdef Q_OS_WIN
    const QString name = QStringLiteral("fake.dll");
#elif defined(Q_OS_MACOS)
    const QString name = QStringLiteral("fake.dylib");
#else
    const QString name = QStringLiteral("fake.so");
#endif
    writeAll(dir.filePath(name), "not a library");
    writeAll(dir.filePath(QStringLiteral("readme.txt")), "not looked at");
    auto registry = builtInRegistry();
    const auto before = summaries(*registry);
    const auto report = io::loadFormatPlugins(*registry, {dir.path(), dir.filePath(QStringLiteral("missing")), exampleFolder()}, allowAll);
    ASSERT_EQ(report.notes.size(), 1) << qPrintable(report.notes.join(QLatin1Char('\n')));
    EXPECT_TRUE(report.notes[0].contains(name));
    EXPECT_TRUE(report.notes[0].contains(QStringLiteral("the plugin can't be loaded"))) << "the helper says why, not only how it ended";
    EXPECT_EQ(report.loaded, QStringList{QStringLiteral("Farbfeld")}) << "the plugins after it still load";
    EXPECT_EQ(summaries(*registry).size(), before.size() + 1);
}

TEST(PluginLoader, BuiltInFormatsAreUnchangedAndKeepTheirExtensions) {
    auto registry = builtInRegistry();
    const auto before = summaries(*registry);
    const QString png = registry->forPath(QStringLiteral("a.png"), false)->id();
    const auto report = io::loadFormatPlugins(*registry, {testPluginFolder("claims"), exampleFolder()}, allowAll);
    EXPECT_EQ(report.loaded, (QStringList{QStringLiteral("Bad"), QStringLiteral("Farbfeld")}));
    ASSERT_EQ(report.notes.size(), 1) << qPrintable(report.notes.join(QLatin1Char('\n')));
    EXPECT_TRUE(report.notes[0].contains(QStringLiteral(".png files stay with"))) << qPrintable(report.notes[0]);

    const auto after = summaries(*registry);
    ASSERT_EQ(after.size(), before.size() + 2);
    EXPECT_TRUE(std::equal(before.begin(), before.end(), after.begin())) << "the built-in formats as they were";
    EXPECT_EQ(registry->forPath(QStringLiteral("a.png"), false)->id(), png);
    EXPECT_EQ(registry->forPath(QStringLiteral("a.claimtest"), false)->id(), QStringLiteral("claims"));
}

TEST(PluginLoader, ThePluginInTwoFoldersLoadsOnce) {
    auto registry = builtInRegistry();
    const auto report = io::loadFormatPlugins(*registry, {exampleFolder(), exampleFolder()}, allowAll);
    EXPECT_EQ(report.loaded, QStringList{QStringLiteral("Farbfeld")});
    ASSERT_EQ(report.notes.size(), 1);
    EXPECT_TRUE(report.notes[0].contains(QStringLiteral("already loaded"))) << qPrintable(report.notes[0]);
    QTemporaryDir dir;
    EXPECT_EQ(roundTrip(*registry, layeredContent(), dir.filePath(QStringLiteral("picture.ff"))).content.size(), QSize(150, 100))
        << "the first copy still works";
}

// A plugin runs inside Doodlebold with the user's rights: none is loaded before they say yes to it, by its contents.
TEST(PluginLoader, OnlyAllowedPluginsAreLoaded) {
    const QString farbfeld = QDir(exampleFolder()).entryInfoList(QDir::Files).value(0).absoluteFilePath();
    const QString hash = io::pluginHash(farbfeld);
    ASSERT_EQ(hash.size(), 64) << "SHA-256, in hex";
    for (const io::PluginChoice choice : {io::PluginChoice::Unasked, io::PluginChoice::Refused}) {
        auto registry = builtInRegistry();
        const auto before = summaries(*registry);
        const auto report = io::loadFormatPlugins(*registry, {exampleFolder()}, [&](const QString&) { return choice; });
        EXPECT_TRUE(report.loaded.isEmpty());
        EXPECT_EQ(summaries(*registry), before) << "no format added";
        ASSERT_EQ(report.found.size(), 1u);
        EXPECT_EQ(report.found[0].hash, hash);
        EXPECT_EQ(report.found[0].choice, choice);
    }
    auto registry = builtInRegistry();
    const auto allowed = io::loadFormatPlugins(*registry, {exampleFolder()}, [&](const QString& h) {
        return h == hash ? io::PluginChoice::Allowed : io::PluginChoice::Unasked;
    });
    EXPECT_EQ(allowed.loaded, QStringList{QStringLiteral("Farbfeld")});
}

TEST(PluginLoader, AChangedFileIsAskedAboutAgain) {
    QTemporaryDir dir;
    const QString farbfeld = QDir(exampleFolder()).entryInfoList(QDir::Files).value(0).absoluteFilePath();
    const QString copy = dir.filePath(QFileInfo(farbfeld).fileName());
    ASSERT_TRUE(QFile::copy(farbfeld, copy));
    const QString was = io::pluginHash(copy);
    QFile file(copy);
    ASSERT_TRUE(file.open(QIODevice::Append));
    file.write("changed");
    file.close();
    EXPECT_NE(io::pluginHash(copy), was) << "a yes was for the file as it was";
}

// A crash in a plugin last time: it is kept from loading this time, till the user says whether to keep it off.
TEST(PluginLoader, APluginTheLastCrashWasInIsHeldBack) {
    const QString farbfeld = QDir(exampleFolder()).entryInfoList(QDir::Files).value(0).absoluteFilePath();
    const QString name = QFileInfo(farbfeld).fileName();
    const QString windowsReport = QStringLiteral("Crash: access violation 0xC0000005\nAt: %1+0x1A2B\nStack:\n  #0 %1+0x1A2B\n").arg(name);
    const QString linuxReport = QStringLiteral("Stack:\n/opt/doodlebold/plugins/%1(+0x1a2b)[0x7f00]\n").arg(name);
    EXPECT_EQ(io::pluginsNamedIn(windowsReport, {farbfeld}), QStringList{farbfeld});
    EXPECT_EQ(io::pluginsNamedIn(linuxReport, {farbfeld}), QStringList{farbfeld});
    EXPECT_TRUE(io::pluginsNamedIn(QStringLiteral("At: doodlebold.exe+0x10\n"), {farbfeld}).isEmpty());

    auto registry = builtInRegistry();
    const auto report = io::loadFormatPlugins(*registry, {exampleFolder()}, allowAll, {farbfeld});
    EXPECT_TRUE(report.loaded.isEmpty());
    ASSERT_EQ(report.found.size(), 1u);
    EXPECT_TRUE(report.found[0].heldBack);
}
