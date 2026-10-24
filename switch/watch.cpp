// Two small threads above the game's priority. One writes the log to the SD card every 250 ms, the only thread that
// does: the others hand it their lines (logx::Background). It also flushes the card's cache then, as 0.0.3's log
// lost its last lines when a frozen console was reset. The other writes a
// pulse line every 2 s with the frame and sound counters and, for every guest thread, the kernel or library function
// it is in, so a hang names the call it hangs in.
#include <switch.h>

#include <pthread.h>

#include <map>
#include <string>

#include "core/log.h"
#include "kernel/kernel.h"
#include "switch/state.h"

namespace sw {

namespace {

constexpr int kWatchPriority = 0x2C;  // the main thread's; the game's threads run at 0x3B

bool Start(void* (*fn)(void*)) {
    pthread_t th;
    if (pthread_create(&th, nullptr, fn, nullptr) == 0) {
        pthread_detach(th);
        return true;
    }
    LOGW("watch: a watch thread could not start");
    return false;
}

void* SyncLoop(void*) {
    kernel::NameHostThread("watch");  // off the game's core: it is above the game's priority
    svcSetThreadPriority(CUR_THREAD_HANDLE, kWatchPriority);
    for (;;) {
        svcSleepThread(250'000'000);
        logx::Sync();
    }
    return nullptr;
}

struct Seen {
    const char* call = nullptr;
    uint32_t calls = 0;
    int still = 0;  // pulses with neither changed
};

void* PulseLoop(void*) {
    kernel::NameHostThread("watch");
    svcSetThreadPriority(CUR_THREAD_HANDLE, kWatchPriority);
    std::map<uint32_t, Seen> seen;
    uint64_t start = armGetSystemTick();
    for (;;) {
        svcSleepThread(2'000'000'000);
        std::string line;
        char buf[160];
        for (auto& t : kernel::Threads()) {
            X86Context* c = t ? t->ctx : nullptr;
            if (!c) continue;
            const char* call = t->call.load(std::memory_order_relaxed);
            uint32_t calls = t->calls.load(std::memory_order_relaxed);
            Seen& s = seen[t->id];
            s.still = call == s.call && calls == s.calls ? s.still + 1 : 0;
            s.call = call;
            uint32_t made = calls - s.calls;
            s.calls = calls;
            std::snprintf(buf, sizeof(buf), " | %u %s %u calls icall %08X esp %08X%s", t->id, call ? call : "game-code",
                          made, c->last_icall, c->esp, s.still >= 2 ? " STILL" : "");
            line += buf;
        }
        double up = double(armTicksToNs(armGetSystemTick() - start)) / 1e9;
        LOGI("pulse: %.0f s, frames %llu drawn %llu shown %llu, draws %llu, sound %llu%s", up,
             (unsigned long long)g_stats.frames.load(), (unsigned long long)g_stats.drawn.load(),
             (unsigned long long)g_stats.shown.load(), (unsigned long long)g_stats.draws.load(),
             (unsigned long long)g_stats.sound.load(), line.c_str());
    }
    return nullptr;
}

}  // namespace

void StartWatch() {
    // from now on the threads that log only hand their lines over, and this thread writes them to the card: none of
    // them waits for the SD card any more (in 0.2.1 the game did, and froze for seconds in 16:9 when the card was slow)
    logx::Background(true);
    if (!Start(SyncLoop)) logx::Background(false);
    Start(PulseLoop);
}

}  // namespace sw
