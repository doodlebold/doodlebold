#include "CrashStack.h"

#ifdef _WIN32

#include <windows.h>
// After windows.h, which it needs.
#include <dbghelp.h>

#include <array>
#include <atomic>
#include <cstdio>

namespace {

// The first few crashes are worth a stack; gtest goes on after each, and the later ones are mostly what the first left broken.
constexpr int kStacksPrinted = 3;
// Past this many frames the stack is gtest and the C runtime's start.
constexpr int kMostFrames = 48;
constexpr DWORD kNameBytes = 512;

std::atomic<int> g_printed{0};
DWORD g_mainThread = 0;

bool isCrash(DWORD code) {
    switch (code) {
    case EXCEPTION_ACCESS_VIOLATION:
    case EXCEPTION_ILLEGAL_INSTRUCTION:
    case EXCEPTION_PRIV_INSTRUCTION:
    case EXCEPTION_INT_DIVIDE_BY_ZERO:
    case EXCEPTION_DATATYPE_MISALIGNMENT:
    case EXCEPTION_STACK_OVERFLOW:
        return true;
    default:
        return false;
    }
}

void printFrame(HANDLE process, int index, DWORD64 address) {
    std::array<char, sizeof(SYMBOL_INFO) + kNameBytes> buffer{};
    auto* symbol = reinterpret_cast<SYMBOL_INFO*>(buffer.data());
    symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
    symbol->MaxNameLen = kNameBytes;
    DWORD64 symbolOffset = 0;
    const bool named = SymFromAddr(process, address, &symbolOffset, symbol);

    IMAGEHLP_MODULE64 module{};
    module.SizeOfStruct = sizeof(module);
    const bool inModule = SymGetModuleInfo64(process, address, &module);

    IMAGEHLP_LINE64 line{};
    line.SizeOfStruct = sizeof(line);
    DWORD lineOffset = 0;
    const bool hasLine = SymGetLineFromAddr64(process, address, &lineOffset, &line);

    std::fprintf(stderr, "  #%-2d %s+0x%llx", index, inModule ? module.ModuleName : "?",
                 static_cast<unsigned long long>(inModule ? address - module.BaseOfImage : address));
    if (named)
        std::fprintf(stderr, " %s+0x%llx", symbol->Name, static_cast<unsigned long long>(symbolOffset));
    if (hasLine)
        std::fprintf(stderr, " (%s:%lu)", line.FileName, line.LineNumber);
    std::fprintf(stderr, "\n");
}

// Walked from the exception's own registers, so it starts where the crash was and not in this handler.
void printStack(const EXCEPTION_POINTERS* info) {
    const HANDLE process = GetCurrentProcess();
    const HANDLE thread = GetCurrentThread();
    static std::atomic<bool> symbolsLoaded{false};
    if (!symbolsLoaded.exchange(true)) {
        SymSetOptions(SYMOPT_DEFERRED_LOADS | SYMOPT_LOAD_LINES | SYMOPT_UNDNAME);
        SymInitialize(process, nullptr, TRUE);
    }
    const EXCEPTION_RECORD* record = info->ExceptionRecord;
    std::fprintf(stderr, "\n*** Crash 0x%08lx on thread %lu (%s)", record->ExceptionCode, GetCurrentThreadId(),
                 GetCurrentThreadId() == g_mainThread ? "the main thread" : "not the main thread");
    // An access violation's first parameter is what was tried (0 read, 1 write, 8 run) and its second where.
    if (record->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && record->NumberParameters >= 2)
        std::fprintf(stderr, ", %s 0x%llx", record->ExceptionInformation[0] == 0   ? "reading"
                                            : record->ExceptionInformation[0] == 1 ? "writing"
                                                                                   : "running",
                     static_cast<unsigned long long>(record->ExceptionInformation[1]));
    std::fprintf(stderr, "\n");

    CONTEXT context = *info->ContextRecord;
    STACKFRAME64 frame{};
    frame.AddrPC.Offset = context.Rip;
    frame.AddrPC.Mode = AddrModeFlat;
    frame.AddrFrame.Offset = context.Rbp;
    frame.AddrFrame.Mode = AddrModeFlat;
    frame.AddrStack.Offset = context.Rsp;
    frame.AddrStack.Mode = AddrModeFlat;
    for (int i = 0; i < kMostFrames; ++i) {
        if (!StackWalk64(IMAGE_FILE_MACHINE_AMD64, process, thread, &frame, &context, nullptr, SymFunctionTableAccess64,
                         SymGetModuleBase64, nullptr) ||
            frame.AddrPC.Offset == 0)
            break;
        printFrame(process, i, frame.AddrPC.Offset);
    }
    std::fflush(stderr);
}

LONG WINAPI onException(EXCEPTION_POINTERS* info) {
    if (isCrash(info->ExceptionRecord->ExceptionCode) && g_printed.fetch_add(1) < kStacksPrinted)
        printStack(info);
    return EXCEPTION_CONTINUE_SEARCH;  // on to gtest, which fails the test
}

}

void printCrashStacks() {
    g_mainThread = GetCurrentThreadId();
    AddVectoredExceptionHandler(1, onException);
}

#else

void printCrashStacks() {}

#endif
