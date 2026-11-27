#include "core/io/PluginHost.h"

#include "core/io/DocumentFormat.h"
#include "core/io/PluginLoader.h"
#include "core/paint/StrokeMemory.h"
#include "plugin_host/PluginHostProtocol.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>

#include <chrono>
#include <deque>
#include <string>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <knownfolders.h>
#include <sddl.h>
#include <shlobj.h>
#include <userenv.h>
#else
#include <csignal>
#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace doodlebold::io {

namespace pp = plugin_host;

namespace {

QString tr(const char* text) {
    return QCoreApplication::translate("PluginHost", text);
}

// A helper waits this long with nothing to do before it ends (doodlebold-plugin-host's kIdleExitMs, 60 s). One idle nearly that
// long is replaced before it's used, so a call never meets a helper as it ends.
constexpr std::chrono::milliseconds kReplaceIdleAfter{55000};

// How often a waiting call looks for the helper's answer, in ms: short beside the hang time, and a wait costs no work.
constexpr int kWaitSliceMs = 100;

// How long a wait for the helper only yields before it sleeps, in ms: a reply to a callback usually comes within it.
[[maybe_unused]] constexpr int kSpinMs = 2;

// The hang time, the owner's choice: a helper silent this long, the user is asked to wait or stop it.
constexpr int kDefaultHangSeconds = 15;

std::mutex& configMutex() {
    static std::mutex m;
    return m;
}

std::function<bool(const QString&)>& hangHandler() {
    static std::function<bool(const QString&)> handler = [](const QString&) { return true; };
    return handler;
}

QString& helperOverride() {
    static QString path;
    return path;
}

QString helperFileName() {
#ifdef _WIN32
    return QStringLiteral("doodlebold-plugin-host.exe");
#else
    return QStringLiteral("doodlebold-plugin-host");
#endif
}

QByteArray bytesOf(const pp::Bytes& b) {
    return QByteArray(reinterpret_cast<const char*>(b.data().data()), qsizetype(b.data().size()));
}

pp::Bytes messageOf(const QByteArray& body) {
    return pp::Bytes(std::vector<std::uint8_t>(body.begin(), body.end()));
}

#ifdef _WIN32

// The AppContainer every helper runs in: made once for the user, kept by Windows.
constexpr wchar_t kContainerName[] = L"Doodlebold.PluginHost";

struct Container {
    PSID sid = nullptr;
    QString folder;  // its own folder, the one place outside the system it may read
    QString why;     // why it couldn't be made
};

const Container& container() {
    static const Container made = [] {
        Container c;
        HRESULT result = CreateAppContainerProfile(kContainerName, L"Doodlebold plugin host",
                                                   L"Runs Doodlebold's format plugins apart from Doodlebold", nullptr, 0, &c.sid);
        if (result == HRESULT_FROM_WIN32(ERROR_ALREADY_EXISTS))
            result = DeriveAppContainerSidFromAppContainerName(kContainerName, &c.sid);
        if (FAILED(result)) {
            c.why = tr("this Windows can't make an AppContainer, which keeps a plugin from the network and from your files (0x%1)")
                        .arg(quint32(result), 8, 16, QLatin1Char('0'));
            return c;
        }
        LPWSTR sidText = nullptr;
        PWSTR folder = nullptr;
        if (ConvertSidToStringSidW(c.sid, &sidText) && SUCCEEDED(GetAppContainerFolderPath(sidText, &folder)))
            c.folder = QDir::fromNativeSeparators(QString::fromWCharArray(folder));
        if (sidText)
            LocalFree(sidText);
        if (folder)
            CoTaskMemFree(folder);
        if (c.folder.isEmpty())
            c.why = tr("the AppContainer's folder can't be found");
        return c;
    }();
    return made;
}

std::wstring quoted(const QString& s) {
    return L"\"" + QDir::toNativeSeparators(s).toStdWString() + L"\"";
}

#endif

// Where the approved copies of plugins go, which only Doodlebold writes: the AppContainer's folder on Windows, the one the helper
// may read.
QString copyFolder(QString* why) {
#ifdef _WIN32
    const Container& c = container();
    if (!c.why.isEmpty()) {
        *why = c.why;
        return {};
    }
    return c.folder + QStringLiteral("/plugins");
#else
    (void)why;
    return QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + QStringLiteral("/plugin-cache");
#endif
}

}

// ---- The helper's process ----

class PluginHost::Process {
public:
    static std::unique_ptr<Process> start(const QString& plugin, QString* why);
    Process() = default;
    ~Process() { kill(); }
    Process(const Process&) = delete;
    Process& operator=(const Process&) = delete;

    bool write(const char* data, std::size_t size);
    // Bytes read, 0 when none came within `ms`, -1 when the helper has ended.
    qint64 readSome(char* data, std::size_t size, int ms);
    void kill();
    // Why it ended: plainly, a crash, or a call its restrictions refused.
    QString endReason();

    std::chrono::steady_clock::time_point lastUse = std::chrono::steady_clock::now();

private:
#ifdef _WIN32
    HANDLE m_process = nullptr, m_toHelper = nullptr, m_fromHelper = nullptr;
#else
    pid_t m_pid = -1;
    int m_socket = -1;
    int m_status = 0;
    bool m_reaped = false;
#endif
};

#ifdef _WIN32

std::unique_ptr<PluginHost::Process> PluginHost::Process::start(const QString& plugin, QString* why) {
    const Container& c = container();
    if (!c.why.isEmpty()) {
        *why = c.why;
        return nullptr;
    }
    // The helper itself goes where the AppContainer may run it; it needs no library beside it (it is linked statically).
    const QString helper = c.folder + QLatin1Char('/') + helperFileName();
    if (QFileInfo(helper).size() != QFileInfo(helperPath()).size() || QFileInfo(helper).lastModified() < QFileInfo(helperPath()).lastModified()) {
        QFile::remove(helper);
        if (!QFile::copy(helperPath(), helper)) {
            *why = tr("the plugin helper can't be put where it may run (%1)").arg(QDir::toNativeSeparators(helper));
            return nullptr;
        }
    }
    SECURITY_ATTRIBUTES inherited{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
    HANDLE childIn = nullptr, toChild = nullptr, fromChild = nullptr, childOut = nullptr;
    if (!CreatePipe(&childIn, &toChild, &inherited, 0) || !CreatePipe(&fromChild, &childOut, &inherited, 0)) {
        *why = tr("the plugin helper's pipes can't be made");
        return nullptr;
    }
    SetHandleInformation(toChild, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(fromChild, HANDLE_FLAG_INHERIT, 0);
    HANDLE nul = CreateFileW(L"NUL", GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, &inherited, OPEN_EXISTING, 0, nullptr);

    SIZE_T listSize = 0;
    InitializeProcThreadAttributeList(nullptr, 2, 0, &listSize);
    std::vector<char> listBytes(listSize);
    auto* list = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(listBytes.data());
    SECURITY_CAPABILITIES capabilities{};
    capabilities.AppContainerSid = c.sid;  // and no capability: no network, no library, no device
    HANDLE inheritOnly[] = {childIn, childOut, nul};
    const bool listed = InitializeProcThreadAttributeList(list, 2, 0, &listSize) &&
                        UpdateProcThreadAttribute(list, 0, PROC_THREAD_ATTRIBUTE_SECURITY_CAPABILITIES, &capabilities,
                                                  sizeof capabilities, nullptr, nullptr) &&
                        UpdateProcThreadAttribute(list, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, inheritOnly, sizeof inheritOnly, nullptr,
                                                  nullptr);
    STARTUPINFOEXW start{};
    start.StartupInfo.cb = sizeof start;
    start.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
    start.StartupInfo.hStdInput = childIn;
    start.StartupInfo.hStdOutput = childOut;
    start.StartupInfo.hStdError = nul;
    start.lpAttributeList = list;
    std::wstring command = quoted(helper) + L" " + quoted(plugin) + L" " + std::to_wstring(std::max<std::int64_t>(0, stroke_memory::allowedBytes()));
    const std::wstring workingFolder = QDir::toNativeSeparators(c.folder).toStdWString();
    // Its own environment, of only what Windows needs to start a process in an AppContainer: the helper needs nothing of the user's.
    // Windows points LOCALAPPDATA and the temporary folders into the container's own, and without them CreateProcess fails with
    // error 203 (ERROR_ENVVAR_NOT_FOUND). They come from the system, not Doodlebold's environment, which may lack them.
    const auto knownFolder = [](REFKNOWNFOLDERID id) {
        PWSTR path = nullptr;
        std::wstring out;
        if (SUCCEEDED(SHGetKnownFolderPath(id, 0, nullptr, &path)))
            out = path;
        CoTaskMemFree(path);
        return out;
    };
    wchar_t system[MAX_PATH] = {};
    GetSystemWindowsDirectoryW(system, MAX_PATH);
    const std::wstring root = system, temporary = QDir::toNativeSeparators(c.folder).toStdWString();
    std::wstring environment;
    for (const auto& [name, value] :
         {std::pair<std::wstring, std::wstring>{L"SystemRoot", root}, {L"windir", root}, {L"LOCALAPPDATA", knownFolder(FOLDERID_LocalAppData)},
          {L"USERPROFILE", knownFolder(FOLDERID_Profile)}, {L"TEMP", temporary}, {L"TMP", temporary}})
        environment += name + L"=" + value + L'\0';
    environment += L'\0';
    PROCESS_INFORMATION started{};
    const DWORD listError = listed ? 0 : GetLastError();
    const bool ok = listed && CreateProcessW(QDir::toNativeSeparators(helper).toStdWString().c_str(), command.data(), nullptr, nullptr,
                                             TRUE, EXTENDED_STARTUPINFO_PRESENT | CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT,
                                             environment.data(), workingFolder.c_str(), &start.StartupInfo, &started);
    const DWORD error = ok ? 0 : listed ? GetLastError() : listError;
    if (listed)
        DeleteProcThreadAttributeList(list);
    CloseHandle(childIn);
    CloseHandle(childOut);
    if (nul != INVALID_HANDLE_VALUE)
        CloseHandle(nul);
    if (!ok) {
        CloseHandle(toChild);
        CloseHandle(fromChild);
        *why = listed ? tr("the plugin helper can't be started in its AppContainer (error %1)").arg(error)
                      : tr("the plugin helper's AppContainer can't be set up for it (error %1)").arg(error);
        return nullptr;
    }
    CloseHandle(started.hThread);
    auto p = std::unique_ptr<Process>(new Process);
    p->m_process = started.hProcess;
    p->m_toHelper = toChild;
    p->m_fromHelper = fromChild;
    return p;
}

bool PluginHost::Process::write(const char* data, std::size_t size) {
    while (size > 0) {
        DWORD put = 0;
        if (!WriteFile(m_toHelper, data, static_cast<DWORD>(std::min<std::size_t>(size, 1u << 30)), &put, nullptr) || put == 0)
            return false;
        data += put;
        size -= put;
    }
    return true;
}

qint64 PluginHost::Process::readSome(char* data, std::size_t size, int ms) {
    QElapsedTimer clock;
    clock.start();
    for (;;) {
        DWORD available = 0;
        if (!PeekNamedPipe(m_fromHelper, nullptr, 0, nullptr, &available, nullptr))
            return -1;
        if (available > 0) {
            DWORD got = 0;
            if (!ReadFile(m_fromHelper, data, static_cast<DWORD>(std::min<std::size_t>(size, available)), &got, nullptr) || got == 0)
                return -1;
            return got;
        }
        if (clock.elapsed() >= ms)
            return 0;
        // A quick answer is met at once; past that the wait sleeps, as Sleep(1) lasts the system's tick (about 15 ms).
        if (clock.elapsed() < kSpinMs)
            SwitchToThread();
        else
            Sleep(1);
    }
}

void PluginHost::Process::kill() {
    if (m_process) {
        TerminateProcess(m_process, 1);
        WaitForSingleObject(m_process, INFINITE);
    }
    for (HANDLE* h : {&m_toHelper, &m_fromHelper, &m_process})
        if (*h) {
            CloseHandle(*h);
            *h = nullptr;
        }
}

QString PluginHost::Process::endReason() {
    if (!m_process)
        return tr("stopped");
    WaitForSingleObject(m_process, INFINITE);
    DWORD code = 0;
    GetExitCodeProcess(m_process, &code);
    if (code == 0)
        return tr("it ended");
    if (code >= 0xC0000000u)
        return tr("it crashed, 0x%1").arg(quint32(code), 8, 16, QLatin1Char('0'));
    return tr("it ended with code %1").arg(code);
}

#else

std::unique_ptr<PluginHost::Process> PluginHost::Process::start(const QString& plugin, QString* why) {
    int pair[2];
    if (socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, pair) != 0) {
        *why = tr("the plugin helper's connection can't be made");
        return nullptr;
    }
    // Everything the child needs, made before fork: after it only calls a signal handler may make are safe.
    const QByteArray helper = QFile::encodeName(helperPath());
    const QByteArray pluginArg = QFile::encodeName(plugin);
    const QByteArray memory = QByteArray::number(qint64(std::max<std::int64_t>(0, stroke_memory::allowedBytes())));
    char* const argv[] = {const_cast<char*>(helper.constData()), const_cast<char*>(pluginArg.constData()), const_cast<char*>(memory.constData()),
                          nullptr};
    const long openMax = sysconf(_SC_OPEN_MAX);
    const pid_t pid = fork();
    if (pid == 0) {
        dup2(pair[1], 0);
        dup2(pair[1], 1);
        const int null = open("/dev/null", O_WRONLY);
        if (null >= 0)
            dup2(null, 2);
        for (long fd = 3; fd < openMax; ++fd)
            close(static_cast<int>(fd));
        execv(argv[0], argv);
        _exit(127);
    }
    close(pair[1]);
    if (pid < 0) {
        close(pair[0]);
        *why = tr("the plugin helper can't be started");
        return nullptr;
    }
    auto p = std::unique_ptr<Process>(new Process);
    p->m_pid = pid;
    p->m_socket = pair[0];
    return p;
}

bool PluginHost::Process::write(const char* data, std::size_t size) {
    while (size > 0) {
        const ssize_t put = ::send(m_socket, data, size, MSG_NOSIGNAL);
        if (put <= 0)
            return false;
        data += put;
        size -= static_cast<std::size_t>(put);
    }
    return true;
}

qint64 PluginHost::Process::readSome(char* data, std::size_t size, int ms) {
    pollfd in{m_socket, POLLIN, 0};
    const int ready = ::poll(&in, 1, ms);
    if (ready == 0)
        return 0;
    const ssize_t got = ready > 0 ? ::recv(m_socket, data, size, 0) : -1;
    return got > 0 ? got : -1;
}

void PluginHost::Process::kill() {
    if (m_pid > 0 && !m_reaped) {
        ::kill(m_pid, SIGKILL);
        waitpid(m_pid, &m_status, 0);
        m_reaped = true;
    }
    if (m_socket >= 0) {
        close(m_socket);
        m_socket = -1;
    }
}

QString PluginHost::Process::endReason() {
    if (m_pid > 0 && !m_reaped) {
        waitpid(m_pid, &m_status, 0);
        m_reaped = true;
    }
    if (WIFSIGNALED(m_status))
        return WTERMSIG(m_status) == SIGSYS ? tr("it tried something its restrictions don't allow")
                                             : tr("it crashed, signal %1").arg(WTERMSIG(m_status));
    if (WIFEXITED(m_status) && WEXITSTATUS(m_status) == 127)
        return tr("the plugin helper couldn't be run");
    return WIFEXITED(m_status) && WEXITSTATUS(m_status) == 0 ? tr("it ended") : tr("it ended with code %1").arg(WEXITSTATUS(m_status));
}

#endif

// ---- Configuration ----

void PluginHost::setHangHandler(std::function<bool(const QString&)> keepWaiting) {
    const std::scoped_lock lock(configMutex());
    hangHandler() = std::move(keepWaiting);
}

std::atomic<int>& PluginHost::hangSeconds() {
    static std::atomic<int> seconds{kDefaultHangSeconds};
    return seconds;
}

QString PluginHost::helperPath() {
    const std::scoped_lock lock(configMutex());
    if (!helperOverride().isEmpty())
        return helperOverride();
    return QDir(QCoreApplication::applicationDirPath()).filePath(helperFileName());
}

void PluginHost::setHelperPath(const QString& path) {
    const std::scoped_lock lock(configMutex());
    helperOverride() = path;
}

QString PluginHost::copiedForHelper(const QString& plugin, const QString& hash, QString* why) {
    const QString folder = copyFolder(why);
    if (folder.isEmpty())
        return {};
    const QString copy = QDir(folder).filePath(hash + QLatin1Char('.') + QFileInfo(plugin).suffix());
    if (pluginHash(copy) == hash)
        return copy;
    QDir().mkpath(folder);
    QFile::remove(copy);
    if (!QFile::copy(plugin, copy) || pluginHash(copy) != hash) {
        QFile::remove(copy);
        *why = tr("it changed while it was being copied, or can't be copied");
        return {};
    }
    return copy;
}

// ---- Calls ----

PluginHost::PluginHost(QString plugin, QString label) : m_plugin(std::move(plugin)), m_label(std::move(label)) {}

PluginHost::~PluginHost() {
    if (m_process) {
        const pp::Header quit{static_cast<std::uint32_t>(pp::Message::Quit), 0};
        m_process->write(reinterpret_cast<const char*>(&quit), sizeof quit);
    }
}

bool PluginHost::ensureRunning(QString* why) {
    if (m_process && std::chrono::steady_clock::now() - m_process->lastUse > kReplaceIdleAfter)
        m_process.reset();
    if (!m_process)
        m_process = Process::start(m_plugin, why);
    return m_process != nullptr;
}

QString PluginHost::whyItEnded() {
    std::uint32_t type = 0;
    QByteArray body;
    if (receive(type, body) == Wait::Got && type == std::uint32_t(pp::Message::Failed))
        if (const auto text = messageOf(body).getText())
            return QString::fromStdString(*text);
    return m_process->endReason();
}

bool PluginHost::send(std::uint32_t type, const QByteArray& body) {
    const pp::Header h{type, std::uint64_t(body.size())};
    return m_process->write(reinterpret_cast<const char*>(&h), sizeof h) && m_process->write(body.constData(), std::size_t(body.size()));
}

PluginHost::Wait PluginHost::receive(std::uint32_t& type, QByteArray& body) {
    QElapsedTimer quiet;
    quiet.start();
    // Reads `size` bytes, asking the user whether to wait on whenever the helper has been silent for the hang time.
    const auto readExact = [&](char* data, std::size_t size) -> Wait {
        while (size > 0) {
            const qint64 got = m_process->readSome(data, size, kWaitSliceMs);
            if (got < 0)
                return Wait::Ended;
            if (got > 0) {
                data += got;
                size -= std::size_t(got);
                quiet.restart();
                continue;
            }
            if (quiet.elapsed() >= qint64(hangSeconds().load()) * 1000) {
                std::function<bool(const QString&)> keepWaiting;
                {
                    const std::scoped_lock lock(configMutex());
                    keepWaiting = hangHandler();
                }
                if (!keepWaiting(m_label))
                    return Wait::Stopped;
                quiet.restart();
            }
        }
        return Wait::Got;
    };
    pp::Header h{};
    if (const Wait w = readExact(reinterpret_cast<char*>(&h), sizeof h); w != Wait::Got)
        return w;
    if (h.length > pp::kMostMessageBytes)
        return Wait::Ended;
    body = QByteArray(qsizetype(h.length), Qt::Uninitialized);
    if (const Wait w = readExact(body.data(), std::size_t(h.length)); w != Wait::Got)
        return w;
    type = h.type;
    m_process->lastUse = std::chrono::steady_clock::now();
    return Wait::Got;
}

int PluginHost::stopped(void* context, void (*setError)(void*, const char*), const QString& why) {
    m_lastEnd = why;
    m_process.reset();
    if (setError)
        setError(context, why.toUtf8().constData());
    return kStopped;
}

Result<PluginHost::Description, QString> PluginHost::describe() {
    using R = Result<Description, QString>;
    const std::scoped_lock lock(m_mutex);
    QString why;
    if (!ensureRunning(&why))
        return R::failure(why);
    std::uint32_t type = 0;
    QByteArray body;
    if (!send(std::uint32_t(pp::Message::Describe), {})) {
        why = whyItEnded();
        m_process.reset();
        return R::failure(why);
    }
    if (receive(type, body) != Wait::Got) {
        why = m_process ? m_process->endReason() : tr("stopped");
        m_process.reset();
        return R::failure(why);
    }
    pp::Bytes b = messageOf(body);
    if (type == std::uint32_t(pp::Message::Failed)) {
        m_process.reset();
        return R::failure(QString::fromStdString(b.getText().value_or(std::string())));
    }
    Description d;
    const auto protocol = b.get<std::uint32_t>(), api = b.get<std::uint32_t>();
    const auto name = b.getText();
    const auto count = b.get<std::uint32_t>();
    if (type != std::uint32_t(pp::Message::Description) || !protocol || *protocol != pp::kProtocolVersion || !api || !name || !count) {
        m_process.reset();
        return R::failure(tr("its helper answered with something else than its description"));
    }
    d.name = QString::fromStdString(*name);
    d.apiVersion = *api;
    for (std::uint32_t i = 0; i < *count; ++i) {
        Format f;
        const auto size = b.get<std::uint32_t>();
        const auto id = b.getText(), formatName = b.getText(), extensions = b.getText();
        const auto flags = b.get<std::uint32_t>();
        const auto recognizes = b.get<std::uint8_t>(), reads = b.get<std::uint8_t>(), writes = b.get<std::uint8_t>();
        if (!size || !id || !formatName || !extensions || !flags || !recognizes || !reads || !writes) {
            m_process.reset();
            return R::failure(tr("its helper's description is broken"));
        }
        f.structSize = *size;
        f.id = QString::fromStdString(*id), f.name = QString::fromStdString(*formatName);
        f.extensions = QString::fromStdString(*extensions);
        f.flags = *flags;
        f.recognizes = *recognizes != 0, f.reads = *reads != 0, f.writes = *writes != 0;
        d.formats.push_back(std::move(f));
    }
    return d;
}

bool PluginHost::recognizes(std::uint32_t format, const QByteArray& head) {
    const std::scoped_lock lock(m_mutex);
    QString why;
    if (!ensureRunning(&why))
        return false;
    pp::Bytes b;
    b.put(format);
    b.putBytes(head.constData(), std::size_t(head.size()));
    std::uint32_t type = 0;
    QByteArray body;
    if (!send(std::uint32_t(pp::Message::Recognize), bytesOf(b)) || receive(type, body) != Wait::Got ||
        type != std::uint32_t(pp::Message::Recognized)) {
        m_process.reset();
        return false;
    }
    return messageOf(body).get<std::uint8_t>().value_or(0) != 0;
}

int PluginHost::read(std::uint32_t format, const doodlebold_read_host* host) {
    const std::scoped_lock lock(m_mutex);
    QString why;
    if (!ensureRunning(&why))
        return stopped(host->context, host->set_error, tr("(the plugin %1 can't run: %2)").arg(m_label, why));
    if (host->size > pp::kMostFileBytes) {
        host->set_error(host->context, tr("(file)").toUtf8().constData());
        return DOODLEBOLD_ERROR_TOO_LARGE;
    }
    pp::Bytes b;
    b.put(format);
    b.putBytes(host->data, host->size);
    if (!send(std::uint32_t(pp::Message::Read), bytesOf(b)))
        return stopped(host->context, host->set_error, tr("(the plugin %1 stopped: %2)").arg(m_label, whyItEnded()));
    return relayRead(host);
}

namespace {

// A plane being received in bands: allocated only when its size is within the limits, and filled only within itself.
struct IncomingPlane {
    std::vector<std::uint16_t> values;
    std::int32_t width = 0, height = 0;
    std::size_t channels = 0;
    bool refused = false;

    void start(std::int32_t w, std::int32_t h, std::size_t c, bool present) {
        width = w, height = h, channels = c;
        refused = present && (w <= 0 || h <= 0 || w > pp::kWidestLayer || std::int64_t{w} * h > std::int64_t{kMaxCanvasSide} * kMaxCanvasSide);
        if (present && !refused)
            values.assign(std::size_t(w) * std::size_t(h) * c, 0);
    }
    bool take(std::uint32_t first, std::uint32_t rows, const std::uint8_t* data, std::size_t size) {
        if (values.empty())
            return refused;  // a refused plane's bands are let go
        const std::size_t row = std::size_t(width) * channels;
        if (std::uint64_t(first) + rows > std::uint64_t(height) || size != rows * row * sizeof(std::uint16_t))
            return false;
        std::memcpy(values.data() + std::size_t(first) * row, data, size);
        return true;
    }
};

}

int PluginHost::relayRead(const doodlebold_read_host* host) {
    for (;;) {
        std::uint32_t type = 0;
        QByteArray body;
        const Wait w = receive(type, body);
        if (w == Wait::Stopped)
            return stopped(host->context, host->set_error, tr("(you stopped the plugin %1)").arg(m_label));
        if (w == Wait::Ended)
            return stopped(host->context, host->set_error,
                           tr("(the plugin %1 stopped while reading the file: %2)").arg(m_label, m_process->endReason()));
        pp::Bytes b = messageOf(body);
        switch (static_cast<pp::Message>(type)) {
        case pp::Message::SetCanvas: {
            const auto width = b.get<std::int32_t>(), height = b.get<std::int32_t>();
            const auto ppi = b.get<double>();
            const std::uint8_t* background = b.getBytes(4);
            if (!width || !height || !ppi || !background)
                return stopped(host->context, host->set_error, tr("(the plugin %1's helper broke its protocol)").arg(m_label));
            pp::Bytes reply;
            reply.put(static_cast<std::int32_t>(host->set_canvas(host->context, *width, *height, *ppi, background)));
            send(std::uint32_t(pp::Message::Reply), bytesOf(reply));
            break;
        }
        case pp::Message::AddLayer: {
            doodlebold_layer layer;
            std::deque<std::string> texts;
            if (!pp::getLayer(b, layer, texts))
                return stopped(host->context, host->set_error, tr("(the plugin %1's helper broke its protocol)").arg(m_label));
            IncomingPlane pixels, mask;
            pixels.start(layer.width, layer.height, 4, layer.kind == DOODLEBOLD_LAYER_PIXELS && layer.width > 0 && layer.height > 0);
            mask.start(layer.mask_width, layer.mask_height, 1, layer.has_mask != 0);
            for (;;) {  // the planes' bands, to an empty one
                std::uint32_t bandType = 0;
                QByteArray band;
                const Wait bw = receive(bandType, band);
                if (bw != Wait::Got)
                    return stopped(host->context, host->set_error, bw == Wait::Stopped ? tr("(you stopped the plugin %1)").arg(m_label)
                                                                                       : tr("(the plugin %1 stopped: %2)").arg(m_label, m_process->endReason()));
                if (bandType != std::uint32_t(pp::Message::LayerBand))
                    return stopped(host->context, host->set_error, tr("(the plugin %1's helper broke its protocol)").arg(m_label));
                if (band.isEmpty())
                    break;
                pp::Bytes bb = messageOf(band);
                const auto plane = bb.get<std::uint8_t>();
                const auto first = bb.get<std::uint32_t>(), rows = bb.get<std::uint32_t>();
                const std::size_t size = bb.left();
                const std::uint8_t* data = bb.getBytes(size);
                IncomingPlane* into = !plane ? nullptr : *plane == std::uint8_t(pp::Plane::Pixels) ? &pixels : *plane == std::uint8_t(pp::Plane::Mask) ? &mask : nullptr;
                if (!into || !first || !rows || !into->take(*first, *rows, data, size))
                    return stopped(host->context, host->set_error, tr("(the plugin %1's helper broke its protocol)").arg(m_label));
            }
            std::int32_t added = -1;
            if (!pixels.refused && !mask.refused) {
                layer.pixels = pixels.values.empty() ? nullptr : pixels.values.data();
                layer.stride = std::size_t(layer.width) * 4 * sizeof(std::uint16_t);
                layer.mask = mask.values.empty() ? nullptr : mask.values.data();
                layer.mask_stride = std::size_t(layer.mask_width) * sizeof(std::uint16_t);
                added = host->add_layer(host->context, &layer);
            }
            pp::Bytes reply;
            reply.put(added);
            send(std::uint32_t(pp::Message::Reply), bytesOf(reply));
            break;
        }
        case pp::Message::Note:
            host->note(host->context, b.getText().value_or(std::string()).c_str());
            break;
        case pp::Message::SetError:
            host->set_error(host->context, b.getText().value_or(std::string()).c_str());
            break;
        case pp::Message::ReadDone: return b.get<std::int32_t>().value_or(DOODLEBOLD_ERROR_CORRUPT);
        case pp::Message::Failed:
            return stopped(host->context, host->set_error,
                           tr("(the plugin %1 can't run: %2)").arg(m_label, QString::fromStdString(b.getText().value_or(std::string()))));
        default: return stopped(host->context, host->set_error, tr("(the plugin %1's helper broke its protocol)").arg(m_label));
        }
    }
}

int PluginHost::write(std::uint32_t format, const doodlebold_write_host* host) {
    const std::scoped_lock lock(m_mutex);
    QString why;
    if (!ensureRunning(&why))
        return stopped(host->context, host->set_error, tr("(the plugin %1 can't run: %2)").arg(m_label, why));
    pp::Bytes b;
    b.put(format);
    b.put(host->width), b.put(host->height), b.put(host->ppi);
    b.putBytes(host->background, 4);
    b.put(host->layer_count);
    for (std::uint32_t i = 0; i < host->layer_count; ++i)
        pp::putLayer(b, host->layers[i]);
    if (!send(std::uint32_t(pp::Message::Write), bytesOf(b)))
        return stopped(host->context, host->set_error, tr("(the plugin %1 stopped: %2)").arg(m_label, whyItEnded()));
    return relayWrite(host);
}

int PluginHost::relayWrite(const doodlebold_write_host* host) {
    bool refused = false;  // a piece of the file this side couldn't write: the save fails, whatever the helper says
    for (;;) {
        std::uint32_t type = 0;
        QByteArray body;
        const Wait w = receive(type, body);
        if (w == Wait::Stopped)
            return stopped(host->context, host->set_error, tr("(you stopped the plugin %1)").arg(m_label));
        if (w == Wait::Ended)
            return stopped(host->context, host->set_error,
                           tr("(the plugin %1 stopped while writing the file: %2)").arg(m_label, m_process->endReason()));
        pp::Bytes b = messageOf(body);
        switch (static_cast<pp::Message>(type)) {
        case pp::Message::GetPlane: {
            const auto plane = b.get<std::uint8_t>();
            const auto layer = b.get<std::uint32_t>();
            if (!plane || !layer || (*plane != std::uint8_t(pp::Plane::Composite) && *layer >= host->layer_count))
                return stopped(host->context, host->set_error, tr("(the plugin %1's helper broke its protocol)").arg(m_label));
            const bool composite = *plane == std::uint8_t(pp::Plane::Composite), isMask = *plane == std::uint8_t(pp::Plane::Mask);
            const std::int32_t width = composite ? host->width : isMask ? host->layers[*layer].mask_width : host->layers[*layer].width;
            const std::int32_t height = composite ? host->height : isMask ? host->layers[*layer].mask_height : host->layers[*layer].height;
            const std::size_t channels = isMask ? 1 : 4;
            const std::size_t row = std::size_t(std::max(width, 0)) * channels;
            std::vector<std::uint16_t> values(row * std::size_t(std::max(height, 0)));
            const std::size_t stride = row * sizeof(std::uint16_t);
            const int ok = values.empty()                                                       ? 1
                           : composite                                                          ? host->get_composite(host->context, values.data(), stride)
                           : isMask ? host->get_mask(host->context, *layer, values.data(), stride)
                                    : host->get_pixels(host->context, *layer, values.data(), stride);
            for (std::int32_t first = 0; ok && first < height; first += std::int32_t(pp::kBandRows)) {
                const auto rows = std::uint32_t(std::min<std::int32_t>(std::int32_t(pp::kBandRows), height - first));
                pp::Bytes band;
                band.put(std::uint32_t(first));
                band.put(rows);
                band.putBytes(values.data() + std::size_t(first) * row, rows * stride);
                if (!send(std::uint32_t(pp::Message::Band), bytesOf(band)))
                    break;
            }
            pp::Bytes reply;
            reply.put(static_cast<std::int32_t>(ok));
            send(std::uint32_t(pp::Message::Reply), bytesOf(reply));
            break;
        }
        case pp::Message::WriteBytes: {
            pp::Bytes reply;
            const int written = host->write(host->context, body.constData(), std::size_t(body.size()));
            refused = refused || !written;
            reply.put(static_cast<std::int32_t>(written));
            send(std::uint32_t(pp::Message::Reply), bytesOf(reply));
            break;
        }
        case pp::Message::SetError:
            host->set_error(host->context, b.getText().value_or(std::string()).c_str());
            break;
        case pp::Message::WriteDone: {
            const std::int32_t result = b.get<std::int32_t>().value_or(DOODLEBOLD_ERROR_WRITE_FAILED);
            return result == DOODLEBOLD_OK && refused ? DOODLEBOLD_ERROR_WRITE_FAILED : result;
        }
        case pp::Message::Failed:
            return stopped(host->context, host->set_error,
                           tr("(the plugin %1 can't run: %2)").arg(m_label, QString::fromStdString(b.getText().value_or(std::string()))));
        default: return stopped(host->context, host->set_error, tr("(the plugin %1's helper broke its protocol)").arg(m_label));
        }
    }
}

}
