#include "platform/CrashReport.h"

#include "core/util/ProjectLinks.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QStandardPaths>
#include <QSysInfo>

#include <array>
#include <atomic>
#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <exception>

#ifdef Q_OS_WIN
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/file.h>
#include <time.h>
#include <unistd.h>
#ifdef __GLIBC__
#include <execinfo.h>
#endif
#endif

namespace doodlebold::crash_report {

namespace {

// A whole report: the header (about 300 bytes) and kMostFrames frames of about 60 bytes each, with room to spare.
constexpr std::size_t kReportBytes = 16384;
// The crashing thread's frames written: the crash is in the first ten or so, and past 64 they're Qt's event loop and the thread's start.
constexpr int kMostFrames = 64;
constexpr int kMillisecondsPerSecond = 1000;

QString reportFolder() {
    return QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
}

QString lastReportPath() {
    return reportFolder() + QStringLiteral("/last-crash-report.txt");
}

// A file for each run, named for its process, so two copies of the program running at once never write or take each other's report.
const QString kRunReportPattern = QStringLiteral("crash-report-*.txt");

// Text built without the heap, as it's built when the heap may be broken: static, so a crash with the stack used up has it too.
class Text {
public:
    void add(const char* s) {
        while (*s && m_used < m_bytes.size())
            m_bytes[m_used++] = *s++;
    }
    void add(const QByteArray& s) { add(s.constData()); }
    void addHex(std::uint64_t value) { addNumber(value, 16); }
    void addDecimal(std::uint64_t value) { addNumber(value, 10); }
    const char* data() const { return m_bytes.data(); }
    std::size_t size() const { return m_used; }
    void clear() { m_used = 0; }

private:
    void addNumber(std::uint64_t value, unsigned base) {
        std::array<char, 24> digits{};  // 2^64 has 20 decimal digits
        std::size_t count = 0;
        do {
            digits[count++] = "0123456789ABCDEF"[value % base];
            value /= base;
        } while (value != 0);
        while (count > 0 && m_used < m_bytes.size())
            m_bytes[m_used++] = digits[--count];
    }

    std::array<char, kReportBytes> m_bytes{};
    std::size_t m_used = 0;
};

Text& report() {
    static Text text;
    return text;
}

// Made as the program starts: the heap may be broken by the time a report is written.
QByteArray& header() {
    static QByteArray text;
    return text;
}

QString& runReportPath() {
    static QString path;
    return path;
}

std::atomic<bool>& writing() {
    static std::atomic<bool> once{false};
    return once;
}

QByteArray makeHeader() {
    const links::SystemFacts facts = links::systemFacts();
#if defined(__clang__)
    const QByteArray compiler = "Clang " __clang_version__;
#elif defined(__GNUC__)
    const QByteArray compiler = "GCC " __VERSION__;
#elif defined(_MSC_VER)
    const QByteArray compiler = "MSVC " + QByteArray::number(_MSC_FULL_VER);
#else
    const QByteArray compiler = "an unknown compiler";
#endif
    QByteArray text = "Doodlebold crash report\n";
    text += "Version: " + facts.version.toUtf8() + "\n";
    text += "Built with: " + compiler + ", Qt " QT_VERSION_STR "\n";
    text += "System: " + facts.system.toUtf8() + ", " + QSysInfo::kernelVersion().toUtf8() + ", " +
            QSysInfo::currentCpuArchitecture().toUtf8() + "\n";
    text += "Pen tablet: " + (facts.tablet.isEmpty() ? QByteArray("none seen yet") : facts.tablet.toUtf8()) + "\n";
    text += "Started: " + QDateTime::currentDateTimeUtc().toString(Qt::ISODate).toUtf8() + "\n";
    return text;
}

#ifdef Q_OS_WIN

// STATUS_HEAP_CORRUPTION, from ntstatus.h, which windows.h leaves out: the heap's own check found it broken.
constexpr DWORD kHeapCorruption = 0xC0000374;

HANDLE g_file = INVALID_HANDLE_VALUE;
DWORD g_mainThread = 0;
ULONGLONG g_started = 0;

const char* nameOf(DWORD code) {
    switch (code) {
    case EXCEPTION_ACCESS_VIOLATION: return "access violation";
    case kHeapCorruption: return "heap corruption";
    case EXCEPTION_STACK_OVERFLOW: return "stack overflow";
    case EXCEPTION_ILLEGAL_INSTRUCTION: return "illegal instruction";
    case EXCEPTION_PRIV_INSTRUCTION: return "privileged instruction";
    case EXCEPTION_INT_DIVIDE_BY_ZERO: return "integer divide by zero";
    case EXCEPTION_IN_PAGE_ERROR: return "in-page error (a file the program runs from couldn't be read)";
    case EXCEPTION_DATATYPE_MISALIGNMENT: return "misaligned data";
    default: return "exception";
    }
}

void writeOut(Text& t) {
    DWORD written = 0;
    WriteFile(g_file, t.data(), static_cast<DWORD>(t.size()), &written, nullptr);
    FlushFileBuffers(g_file);
    t.clear();
}

// As module+offset, not an address: a module loads at another address each run, and the offset is what the build's symbols turn
// into a function and line.
void addPlace(Text& t, DWORD64 address) {
    HMODULE module = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            reinterpret_cast<LPCWSTR>(address), &module) ||
        !module) {
        t.add("0x");
        t.addHex(address);
        return;
    }
    std::array<wchar_t, MAX_PATH> path{};
    const DWORD length = GetModuleFileNameW(module, path.data(), static_cast<DWORD>(path.size()));
    const wchar_t* name = path.data();
    for (DWORD i = 0; i < length; ++i)
        if (path[i] == L'\\' || path[i] == L'/')
            name = path.data() + i + 1;
    std::array<char, MAX_PATH> utf8{};
    WideCharToMultiByte(CP_UTF8, 0, name, -1, utf8.data(), static_cast<int>(utf8.size()) - 1, nullptr, nullptr);
    t.add(utf8.data());
    t.add("+0x");
    t.addHex(address - reinterpret_cast<DWORD64>(module));
}

// From the crash's own registers, so the stack starts where it crashed and not in this handler.
void addStack(Text& t, CONTEXT context) {
#if defined(_M_X64) || defined(__x86_64__)
    ULONG_PTR low = 0, high = 0;
    GetCurrentThreadStackLimits(&low, &high);
    t.add("Stack:\n");
    for (int frame = 0; frame < kMostFrames && context.Rip != 0; ++frame) {
        t.add("  #");
        t.addDecimal(static_cast<std::uint64_t>(frame));
        t.add(" ");
        addPlace(t, context.Rip);
        t.add("\n");
        DWORD64 imageBase = 0;
        if (PRUNTIME_FUNCTION function = RtlLookupFunctionEntry(context.Rip, &imageBase, nullptr)) {
            void* handlerData = nullptr;
            DWORD64 establisher = 0;
            RtlVirtualUnwind(UNW_FLAG_NHANDLER, imageBase, context.Rip, function, &context, &handlerData, &establisher, nullptr);
        } else {  // a leaf function, which has no unwind entry: its return address is on the top of the stack
            if (context.Rsp < low || context.Rsp + sizeof(DWORD64) > high)
                break;
            context.Rip = *reinterpret_cast<const DWORD64*>(context.Rsp);
            context.Rsp += sizeof(DWORD64);
        }
        if (context.Rsp < low || context.Rsp >= high)
            break;
    }
#else
    (void)t;
    (void)context;
#endif
}

void writeReport(const char* what, const EXCEPTION_RECORD* record, const CONTEXT& context) {
    if (g_file == INVALID_HANDLE_VALUE || writing().exchange(true))
        return;
    Text& t = report();
    t.add(header());
    t.add("Crashed after: ");
    t.addDecimal((GetTickCount64() - g_started) / kMillisecondsPerSecond);
    t.add(" s\nThread: ");
    t.addDecimal(GetCurrentThreadId());
    t.add(GetCurrentThreadId() == g_mainThread ? " (the main thread)\n" : " (not the main thread)\n");
    t.add("Crash: ");
    t.add(what);
    if (record) {
        t.add(" 0x");
        t.addHex(record->ExceptionCode);
        // An access violation's first parameter is what was tried (0 read, 1 write, 8 run) and its second where.
        if (record->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && record->NumberParameters >= 2) {
            const ULONG_PTR kind = record->ExceptionInformation[0];
            t.add(kind == 0 ? ", reading 0x" : kind == 1 ? ", writing 0x" : ", running 0x");
            t.addHex(record->ExceptionInformation[1]);
        }
        t.add("\nAt: ");
        addPlace(t, reinterpret_cast<DWORD64>(record->ExceptionAddress));
    }
    t.add("\n");
    writeOut(t);  // what's known so far is kept, if unwinding a broken stack crashes again
    addStack(t, context);
    writeOut(t);
}

LONG WINAPI onUnhandledException(EXCEPTION_POINTERS* info) {
    writeReport(nameOf(info->ExceptionRecord->ExceptionCode), info->ExceptionRecord, *info->ContextRecord);
    return EXCEPTION_CONTINUE_SEARCH;  // on to Windows' own error reporting, which ends the program
}

// The heap's check ends the program without reaching the unhandled-exception filter, so it's caught as it's raised. Nothing
// handles heap corruption, so this never reports a crash that wasn't one.
LONG WINAPI onHeapCorruption(EXCEPTION_POINTERS* info) {
    if (info->ExceptionRecord->ExceptionCode == kHeapCorruption)
        writeReport(nameOf(kHeapCorruption), info->ExceptionRecord, *info->ContextRecord);
    return EXCEPTION_CONTINUE_SEARCH;
}

void writeHere(const char* what) {
    CONTEXT context{};
    RtlCaptureContext(&context);
    writeReport(what, nullptr, context);
}

bool openRunReport(const QString& path) {
    // Not shared for deleting, so another copy of the program running at the same time leaves it alone (takeNewReport).
    g_file = CreateFileW(reinterpret_cast<LPCWSTR>(path.utf16()), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS,
                         FILE_ATTRIBUTE_NORMAL, nullptr);
    return g_file != INVALID_HANDLE_VALUE;
}

void closeRunReport() {
    if (g_file != INVALID_HANDLE_VALUE)
        CloseHandle(g_file);
    g_file = INVALID_HANDLE_VALUE;
}

void catchCrashes() {
    g_mainThread = GetCurrentThreadId();
    g_started = GetTickCount64();
    SetUnhandledExceptionFilter(onUnhandledException);
    AddVectoredExceptionHandler(1, onHeapCorruption);
}

bool inUseByAnotherRun(const QString& path) {
    const HANDLE file = CreateFileW(reinterpret_cast<LPCWSTR>(path.utf16()), GENERIC_READ, 0, nullptr, OPEN_EXISTING,
                                    FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return GetLastError() == ERROR_SHARING_VIOLATION;
    CloseHandle(file);
    return false;
}

#else

int g_fd = -1;
timespec g_started{};

// The signal handler's own stack, so a stack overflow is reported too.
constexpr std::size_t kSignalStackBytes = 64 * 1024;
constexpr std::array kFatalSignals = {SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGABRT};

const char* nameOf(int signal) {
    switch (signal) {
    case SIGSEGV: return "segmentation fault (SIGSEGV)";
    case SIGBUS: return "bus error (SIGBUS)";
    case SIGILL: return "illegal instruction (SIGILL)";
    case SIGFPE: return "arithmetic error (SIGFPE)";
    case SIGABRT: return "abort() (SIGABRT)";
    default: return "signal";
    }
}

void writeOut(Text& t) {
    std::size_t done = 0;
    while (done < t.size()) {
        const ssize_t n = write(g_fd, t.data() + done, t.size() - done);
        if (n <= 0)
            break;
        done += static_cast<std::size_t>(n);
    }
    t.clear();
}

// The fault's address comes from `fault`, as a null pointer's 0 is itself the address of the commonest crash.
void writeReport(const char* what, const siginfo_t* fault) {
    if (g_fd < 0 || writing().exchange(true))
        return;
    Text& t = report();
    t.add(header());
    timespec now{};
    clock_gettime(CLOCK_MONOTONIC, &now);
    t.add("Crashed after: ");
    t.addDecimal(static_cast<std::uint64_t>(now.tv_sec - g_started.tv_sec));
    t.add(" s\nCrash: ");
    t.add(what);
    if (fault) {
        t.add(", at 0x");
        t.addHex(reinterpret_cast<std::uintptr_t>(fault->si_addr));
    }
    t.add("\n");
    writeOut(t);
#ifdef __GLIBC__
    t.add("Stack:\n");
    writeOut(t);
    std::array<void*, kMostFrames> frames{};
    backtrace_symbols_fd(frames.data(), backtrace(frames.data(), kMostFrames), g_fd);
#endif
}

void onSignal(int signal, siginfo_t* info, void*) {
    const bool hasAddress = signal == SIGSEGV || signal == SIGBUS;
    writeReport(nameOf(signal), hasAddress ? info : nullptr);
    std::signal(signal, SIG_DFL);
    std::raise(signal);
}

void writeHere(const char* what) {
    writeReport(what, nullptr);
}

bool openRunReport(const QString& path) {
    g_fd = open(QFile::encodeName(path).constData(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, S_IRUSR | S_IWUSR);
    // Locked, so another copy of the program running at the same time leaves it alone (takeNewReport).
    return g_fd >= 0 && flock(g_fd, LOCK_EX | LOCK_NB) == 0;
}

void closeRunReport() {
    if (g_fd >= 0)
        close(g_fd);
    g_fd = -1;
}

void catchCrashes() {
    clock_gettime(CLOCK_MONOTONIC, &g_started);
#ifdef __GLIBC__
    std::array<void*, 1> warm{};
    backtrace(warm.data(), 1);  // its first call loads libgcc, which mustn't happen in a signal handler
#endif
    static std::array<char, kSignalStackBytes> signalStack{};
    stack_t stack{};
    stack.ss_sp = signalStack.data();
    stack.ss_size = signalStack.size();
    sigaltstack(&stack, nullptr);
    struct sigaction action {};
    action.sa_sigaction = onSignal;
    action.sa_flags = SA_SIGINFO | SA_ONSTACK;
    sigemptyset(&action.sa_mask);
    for (const int signal : kFatalSignals)
        sigaction(signal, &action, nullptr);
}

bool inUseByAnotherRun(const QString& path) {
    const int fd = open(QFile::encodeName(path).constData(), O_RDONLY | O_CLOEXEC);
    if (fd < 0)
        return false;
    const bool locked = flock(fd, LOCK_SH | LOCK_NB) != 0;
    close(fd);
    return locked;
}

#endif

#ifdef Q_OS_WIN
void onAbort(int) {
    writeHere("abort()");
}
#endif

[[noreturn]] void onTerminate() {
    QByteArray what = "std::terminate: an exception nothing caught";
    if (const std::exception_ptr thrown = std::current_exception()) {
        try {
            std::rethrow_exception(thrown);
        } catch (const std::exception& e) {
            what += QByteArray(" (") + e.what() + ")";
        } catch (...) {
        }
    }
    writeHere(what.constData());
    std::abort();
}

}

void install() {
    if (!QDir().mkpath(reportFolder()))
        return;
    runReportPath() = reportFolder() + QStringLiteral("/crash-report-%1.txt").arg(QCoreApplication::applicationPid());
    if (!openRunReport(runReportPath()))
        return;
    header() = makeHeader();
    catchCrashes();
#ifdef Q_OS_WIN
    std::signal(SIGABRT, onAbort);  // Linux has it among the fatal signals
#endif
    std::set_terminate(onTerminate);
}

void uninstall() {
    closeRunReport();
    if (!runReportPath().isEmpty())
        QFile::remove(runReportPath());
}

QString takeNewReport() {
    QString newest;
    const QDir folder(reportFolder());
    for (const QFileInfo& found : folder.entryInfoList({kRunReportPattern}, QDir::Files, QDir::Time)) {
        const QString path = found.absoluteFilePath();
        if (path == QFileInfo(runReportPath()).absoluteFilePath() || inUseByAnotherRun(path))
            continue;
        QFile file(path);
        const QString text = file.open(QIODevice::ReadOnly) ? QString::fromUtf8(file.readAll()) : QString();
        file.close();
        QFile::remove(path);  // an empty one is a run that ended without uninstall(), as when it was killed
        if (newest.isEmpty() && !text.trimmed().isEmpty())
            newest = text;  // newest first; older ones are let go
    }
    if (!newest.isEmpty()) {
        QFile last(lastReportPath());
        if (last.open(QIODevice::WriteOnly | QIODevice::Truncate))
            last.write(newest.toUtf8());
    }
    return newest;
}

QString lastReport() {
    QFile file(lastReportPath());
    return file.open(QIODevice::ReadOnly) ? QString::fromUtf8(file.readAll()) : QString();
}

void crashForTest() {
    static volatile std::uintptr_t nowhere = 0;
    *reinterpret_cast<volatile int*>(nowhere) = 1;
    std::abort();
}

}
