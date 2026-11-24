#include "core/util/ThreadPriority.h"

#ifdef __linux__
#include <sys/resource.h>
#include <sys/syscall.h>
#include <unistd.h>
#elif defined(_WIN32)
#include <windows.h>
#elif defined(__APPLE__)
#include <pthread.h>
#include <sys/qos.h>
#endif

namespace doodlebold {

void lowerThisThreadsPriority() {
#ifdef __linux__
    constexpr int kNiceness = 10;  // on Linux this applies to the one thread
    setpriority(PRIO_PROCESS, static_cast<id_t>(syscall(SYS_gettid)), kNiceness);
#elif defined(_WIN32)
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
#elif defined(__APPLE__)
    pthread_set_qos_class_self_np(QOS_CLASS_UTILITY, 0);
#endif
}

}
