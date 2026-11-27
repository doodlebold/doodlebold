#include "plugin_host/PluginHostRestrictions.h"

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <shellapi.h>
#else
#include <dlfcn.h>
#include <linux/audit.h>
#include <linux/filter.h>
#include <linux/seccomp.h>
#include <sched.h>
#include <sys/prctl.h>
#include <sys/resource.h>
#include <sys/syscall.h>
#include <unistd.h>

#include <cstddef>
#endif

namespace doodlebold::plugin_host {

namespace {

std::uint64_t memoryLimit(int argc, char** argv) {
    return argc > kMemoryArgument ? std::strtoull(argv[kMemoryArgument], nullptr, 10) : 0;
}

}

#ifdef _WIN32

const char* restrictBeforeLoading(int argc, char** argv) {
    HANDLE token = nullptr;
    DWORD inContainer = 0, size = 0;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token))
        return "the helper's own rights can't be read";
    const BOOL read = GetTokenInformation(token, TokenIsAppContainer, &inContainer, sizeof inContainer, &size);
    CloseHandle(token);
    if (!read || !inContainer)
        return "the helper isn't in an AppContainer";

    // The job's handle is kept, never closed: the job lasts as long as the helper.
    HANDLE job = CreateJobObjectW(nullptr, nullptr);
    if (!job)
        return "the helper's job can't be made";
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE | JOB_OBJECT_LIMIT_ACTIVE_PROCESS;
    limits.BasicLimitInformation.ActiveProcessLimit = 1;
    if (const std::uint64_t most = memoryLimit(argc, argv); most > 0) {
        limits.BasicLimitInformation.LimitFlags |= JOB_OBJECT_LIMIT_PROCESS_MEMORY;
        limits.ProcessMemoryLimit = static_cast<SIZE_T>(most);
    }
    JOBOBJECT_BASIC_UI_RESTRICTIONS ui{};
    ui.UIRestrictionsClass = JOB_OBJECT_UILIMIT_ALL;
    if (!SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits, sizeof limits) ||
        !SetInformationJobObject(job, JobObjectBasicUIRestrictions, &ui, sizeof ui) || !AssignProcessToJobObject(job, GetCurrentProcess()))
        return "the helper's job can't be set up";
    return nullptr;
}

const char* loadPlugin(int, char**, const doodlebold_plugin*& plugin) {
    int count = 0;
    LPWSTR* args = CommandLineToArgvW(GetCommandLineW(), &count);
    if (!args || count <= kPluginArgument)
        return "no plugin was named";
    // Its own folder first for the libraries it needs: a plugin may come with some.
    HMODULE library = LoadLibraryExW(args[kPluginArgument], nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    LocalFree(args);
    if (!library)
        return "the plugin can't be loaded";
    const auto entry = reinterpret_cast<doodlebold_plugin_entry_fn>(reinterpret_cast<void*>(GetProcAddress(library, DOODLEBOLD_PLUGIN_ENTRY)));
    if (!entry)
        return "the plugin has no doodlebold_plugin_entry function";
    plugin = entry(DOODLEBOLD_PLUGIN_API_VERSION);
    return plugin ? nullptr : "the plugin declined to work with this version of Doodlebold";
}

const char* restrictAfterLoading() {
    return nullptr;
}

#else

const char* restrictBeforeLoading(int argc, char** argv) {
    if (prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) != 0)
        return "the helper can't give up gaining rights";
    // No network: a namespace of its own, where the system lets a user make one (some turn that off; seccomp below still holds).
    (void)unshare(CLONE_NEWUSER | CLONE_NEWNET);
    const rlimit none{0, 0};
    if (setrlimit(RLIMIT_CORE, &none) != 0 || setrlimit(RLIMIT_NPROC, &none) != 0)
        return "the helper's limits can't be set";
    if (const std::uint64_t most = memoryLimit(argc, argv); most > 0) {
        const rlimit memory{static_cast<rlim_t>(most), static_cast<rlim_t>(most)};
        if (setrlimit(RLIMIT_AS, &memory) != 0)
            return "the helper's memory limit can't be set";
    }
    return nullptr;
}

const char* loadPlugin(int argc, char** argv, const doodlebold_plugin*& plugin) {
    if (argc <= kPluginArgument)
        return "no plugin was named";
    void* library = dlopen(argv[kPluginArgument], RTLD_NOW | RTLD_LOCAL);
    if (!library)
        return "the plugin can't be loaded";
    const auto entry = reinterpret_cast<doodlebold_plugin_entry_fn>(dlsym(library, DOODLEBOLD_PLUGIN_ENTRY));
    if (!entry)
        return "the plugin has no doodlebold_plugin_entry function";
    plugin = entry(DOODLEBOLD_PLUGIN_API_VERSION);
    return plugin ? nullptr : "the plugin declined to work with this version of Doodlebold";
}

namespace {

#if defined(__x86_64__)
constexpr std::uint32_t kArch = AUDIT_ARCH_X86_64;
#elif defined(__aarch64__)
constexpr std::uint32_t kArch = AUDIT_ARCH_AARCH64;
#else
#error "the plugin helper's seccomp filter knows x86-64 and ARM64"
#endif

#ifndef SECCOMP_RET_KILL_PROCESS
#define SECCOMP_RET_KILL_PROCESS SECCOMP_RET_KILL
#endif

// What the helper's loop and the C library's memory and time need, and nothing that opens, connects or starts anything.
const std::vector<int>& allowedCalls() {
    static const std::vector<int> calls = {
        __NR_read, __NR_write, __NR_readv, __NR_writev, __NR_close, __NR_mmap, __NR_munmap, __NR_mprotect, __NR_madvise,
        __NR_mremap, __NR_brk, __NR_futex, __NR_exit, __NR_exit_group, __NR_rt_sigreturn, __NR_rt_sigprocmask, __NR_rt_sigaction,
        __NR_clock_gettime, __NR_clock_nanosleep, __NR_nanosleep, __NR_gettimeofday, __NR_getrandom, __NR_ppoll, __NR_sched_yield,
        __NR_getpid, __NR_gettid, __NR_tgkill, __NR_lseek, __NR_fstat, __NR_newfstatat, __NR_restart_syscall,
#ifdef __NR_poll
        __NR_poll,
#endif
    };
    return calls;
}

}

const char* restrictAfterLoading() {
    std::vector<sock_filter> filter = {
        BPF_STMT(BPF_LD | BPF_W | BPF_ABS, offsetof(seccomp_data, arch)),
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, kArch, 1, 0),
        BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_KILL_PROCESS),
        BPF_STMT(BPF_LD | BPF_W | BPF_ABS, offsetof(seccomp_data, nr)),
    };
    for (const int call : allowedCalls()) {
        filter.push_back(BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, static_cast<std::uint32_t>(call), 0, 1));
        filter.push_back(BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ALLOW));
    }
    filter.push_back(BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_KILL_PROCESS));
    const sock_fprog program{static_cast<unsigned short>(filter.size()), filter.data()};
    if (prctl(PR_SET_SECCOMP, SECCOMP_MODE_FILTER, &program) != 0)
        return "the system doesn't let the helper limit its own calls (seccomp)";
    return nullptr;
}

#endif

}
