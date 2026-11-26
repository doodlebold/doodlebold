#include "core/io/FileIo.h"
#include "core/io/FormatRegistry.h"
#include "core/io/PluginHost.h"
#include "core/io/PluginLoader.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>

#include <gtest/gtest.h>

using namespace doodlebold;

namespace {

// The helper the plugins run in, as this build made it.
const bool kHelperFound = (io::PluginHost::setHelperPath(QStringLiteral(DOODLEBOLD_PLUGIN_HOST)), true);

io::PluginChoice allowAll(const QString&) {
    return io::PluginChoice::Allowed;
}

// A registry with the misbehaving plugin `variant` (tests/plugins/misbehaving_plugin.c) loaded.
std::unique_ptr<FormatRegistry> registryWith(const char* variant) {
    auto registry = std::make_unique<FormatRegistry>();
    const auto report =
        io::loadFormatPlugins(*registry, {QDir(QStringLiteral(DOODLEBOLD_TEST_PLUGIN_DIR)).filePath(QString::fromLatin1(variant))}, allowAll);
    EXPECT_EQ(report.loaded, QStringList{QStringLiteral("Misbehaving")}) << qPrintable(report.notes.join(QLatin1Char('\n')));
    return registry;
}

QString fileWith(const QTemporaryDir& dir, const QByteArray& bytes) {
    const QString path = dir.filePath(QStringLiteral("test.misbehaving"));
    QFile f(path);
    EXPECT_TRUE(f.open(QIODevice::WriteOnly));
    f.write(bytes);
    return path;
}

// The hang handler and time for one test, then the program's own again.
struct HangAnswers {
    explicit HangAnswers(std::vector<bool> answers) : m_answers(std::move(answers)) {
        io::PluginHost::hangSeconds() = 1;
        io::PluginHost::setHangHandler([this](const QString&) {
            const bool wait = asked < m_answers.size() && m_answers[asked];
            ++asked;
            return wait;
        });
    }
    ~HangAnswers() {
        io::PluginHost::hangSeconds() = 15;
        io::PluginHost::setHangHandler([](const QString&) { return true; });
    }
    HangAnswers(const HangAnswers&) = delete;
    HangAnswers& operator=(const HangAnswers&) = delete;

    std::size_t asked = 0;

private:
    std::vector<bool> m_answers;
};

}

// A plugin's crash ends its helper, not Doodlebold: the call fails, and the next one starts a helper again.
TEST(PluginHost, APluginThatCrashesFailsTheCallAndDoodleboldGoesOn) {
    const auto registry = registryWith("crash");
    QTemporaryDir dir;
    const QString path = fileWith(dir, "anything");
    for (int attempt = 0; attempt < 2; ++attempt) {
        const auto loaded = io::loadFile(path, *registry);
        ASSERT_FALSE(loaded.ok());
        EXPECT_EQ(loaded.error().kind, FileError::Kind::PluginFailed);
        EXPECT_TRUE(loaded.error().detail.contains(QStringLiteral("Misbehaving")) ||
                    loaded.error().detail.contains(QStringLiteral("crashed")))
            << qPrintable(loaded.error().describe());
    }
}

// Nothing is stopped without asking: the user is asked after the hang time, Wait waits as long again, and Stop ends it.
TEST(PluginHost, AHangingPluginIsStoppedOnlyWhenTheUserSaysSo) {
    const auto registry = registryWith("hang");
    QTemporaryDir dir;
    const HangAnswers answers({true, false});
    const auto loaded = io::loadFile(fileWith(dir, "anything"), *registry);
    ASSERT_FALSE(loaded.ok());
    EXPECT_EQ(loaded.error().kind, FileError::Kind::PluginFailed);
    EXPECT_TRUE(loaded.error().detail.contains(QStringLiteral("stopped"))) << qPrintable(loaded.error().describe());
    EXPECT_EQ(answers.asked, 2u) << "asked, told to wait, asked again, told to stop";
}

// The helper may read nothing of the user's: on Windows the AppContainer refuses the file, on Linux seccomp ends the helper at the
// attempt. Run in Doodlebold, the plugin would have opened it.
TEST(PluginHost, APluginCantOpenTheUsersFiles) {
    QTemporaryDir dir;
    const QString secret = dir.filePath(QStringLiteral("secret.txt"));
    QFile f(secret);
    ASSERT_TRUE(f.open(QIODevice::WriteOnly));
    f.write("the user's own");
    f.close();
    const auto registry = registryWith("pry");
    const auto loaded = io::loadFile(fileWith(dir, QDir::toNativeSeparators(secret).toLocal8Bit()), *registry);
#ifdef Q_OS_WIN
    ASSERT_TRUE(loaded.ok()) << qPrintable(loaded.error().describe());
    EXPECT_EQ(loaded.value().notes, QStringList{QStringLiteral("refused")});
#else
    ASSERT_FALSE(loaded.ok()) << "the helper was let open the file: " << qPrintable(loaded.value().notes.join(QLatin1Char(' ')));
    EXPECT_EQ(loaded.error().kind, FileError::Kind::PluginFailed);
    EXPECT_TRUE(loaded.error().detail.contains(QStringLiteral("restrictions"))) << qPrintable(loaded.error().describe());
#endif
}
