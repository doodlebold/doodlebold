#include "core/document/DocumentContent.h"
#include "core/io/FileIo.h"
#include "core/io/FormatRegistry.h"

#include <QFile>
#include <QTemporaryDir>

#include <gtest/gtest.h>

using namespace doodlebold;
using doodlebold::io::loadFile;

namespace {

QString fileNamed(const QTemporaryDir& dir, const QString& name, const QByteArray& bytes = "anything") {
    const QString path = dir.filePath(name);
    QFile f(path);
    EXPECT_TRUE(f.open(QIODevice::WriteOnly));
    f.write(bytes);
    return path;
}

// A format as a plugin writes one: one class, registered through the interface.
class TestFormat : public DocumentFormat {
public:
    QString id() const override { return QStringLiteral("testformat"); }
    QString name() const override { return QStringLiteral("Test picture"); }
    QStringList extensions() const override { return {QStringLiteral("testpic")}; }
    bool canRead() const override { return true; }
    FileResult<LoadedDocument> read(QIODevice& device) const override {
        LoadedDocument out;
        out.content.state.layers = LayerStack(QSize(int(device.size()), 10));
        out.notes << QStringLiteral("read by the test format");
        return out;
    }
};

}

TEST(FormatPlugins, AFormatRegisteredThroughTheInterfaceLoadsItsFiles) {
    QTemporaryDir dir;
    const QString path = fileNamed(dir, QStringLiteral("a.testpic"), "12345");
    EXPECT_EQ(loadFile(path).error().kind, FileError::Kind::UnknownFormat) << "not registered in the program";

    FormatRegistry registry;
    registry.add(std::make_unique<TestFormat>());
    const auto loaded = loadFile(path, registry);
    ASSERT_TRUE(loaded.ok()) << qPrintable(loaded.error().describe());
    EXPECT_EQ(loaded.value().content.state.layers.size(), QSize(5, 10));
    EXPECT_EQ(loaded.value().notes, QStringList{QStringLiteral("read by the test format")});
    EXPECT_NE(registry.byId(QStringLiteral("testformat")), nullptr);
    EXPECT_TRUE(registry.openFilter().contains(QStringLiteral("Test picture (*.testpic)")));
    EXPECT_EQ(registry.forPath(path, true), nullptr) << "it cannot write";
}
