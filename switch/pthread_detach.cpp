// libnx leaves newlib's thread_detach hook unimplemented, so pthread_detach answers ENOSYS and std::thread::detach
// throws std::system_error ("Function not implemented"): 0.0.2 ended there, at the runtime's first detached thread.
// Here a detached thread goes on a list, and a reaper joins each one once its kernel handle signals that it ended;
// the join frees its stack and handle, as it would for a joined thread.
#include <switch.h>

#include <pthread.h>

#include <mutex>
#include <vector>

#include "core/log.h"

namespace {

std::mutex g_lock;
std::vector<pthread_t> g_detached;
bool g_reaper = false;

// libnx's struct __pthread_t starts with its Thread, whose first field is the kernel handle
Handle HandleOf(pthread_t t) { return reinterpret_cast<const Thread*>(t)->handle; }

void* Reap(void*) {
    for (;;) {
        svcSleepThread(500'000'000);
        std::vector<pthread_t> ended;
        {
            std::lock_guard<std::mutex> lock(g_lock);
            for (size_t i = 0; i < g_detached.size();) {
                if (R_SUCCEEDED(svcWaitSynchronizationSingle(HandleOf(g_detached[i]), 0))) {
                    ended.push_back(g_detached[i]);
                    g_detached[i] = g_detached.back();
                    g_detached.pop_back();
                } else {
                    i++;
                }
            }
        }
        for (pthread_t t : ended) pthread_join(t, nullptr);
    }
    return nullptr;
}

}  // namespace

extern "C" int __syscall_thread_detach(struct __pthread_t* thread) {
    if (!thread || thread == reinterpret_cast<struct __pthread_t*>(~uintptr_t(0))) return 0;  // the main thread
    std::lock_guard<std::mutex> lock(g_lock);
    g_detached.push_back(thread);
    if (!g_reaper) {
        pthread_t th;
        if (pthread_create(&th, nullptr, Reap, nullptr) == 0)
            g_reaper = true;  // never detached or joined, it runs until the end
        else
            LOGW("threads: no reaper thread, ended threads keep their stacks");
    }
    return 0;
}
