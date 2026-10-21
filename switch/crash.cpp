// What a crash leaves behind on the SD card: logs/crash.txt with the exception, the registers, the program counter
// as an offset into the .nro (the build's SpiderMan2Xbox.map names it) and return addresses found on the stack.
// Uncaught C++ exceptions are written there too. After the dump the process ends as it would have (svcBreak).
#include <switch.h>

#include <fcntl.h>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <typeinfo>

#include "core/log.h"

extern "C" {
alignas(16) u8 __nx_exception_stack[0x8000];
u64 __nx_exception_stack_size = sizeof(__nx_exception_stack);
void __libnx_exception_handler(ThreadExceptionDump* ctx);
extern char _start;   // the module's first byte (libnx crt0)
extern char __end__;  // its end (linker script)
}

namespace {

char g_buf[16384];
size_t g_len = 0;

void Put(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
void Put(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int n = std::vsnprintf(g_buf + g_len, sizeof(g_buf) - g_len, fmt, ap);
    va_end(ap);
    if (n > 0) g_len = std::min(sizeof(g_buf) - 1, g_len + size_t(n));
}

void Save() {
    // plain open/write: the crashing thread may hold a stdio lock
    int fd = open("logs/crash.txt", O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd >= 0) {
        write(fd, g_buf, g_len);
        close(fd);
    }
}

const char* Desc(u32 d) {
    switch (d) {
        case ThreadExceptionDesc_InstructionAbort: return "instruction abort";
        case ThreadExceptionDesc_MisalignedPC: return "misaligned PC";
        case ThreadExceptionDesc_MisalignedSP: return "misaligned SP";
        case ThreadExceptionDesc_SError: return "SError";
        case ThreadExceptionDesc_BadSVC: return "bad SVC";
        case ThreadExceptionDesc_Trap: return "trap (undefined instruction)";
        case ThreadExceptionDesc_Other: return "data abort or other";
        default: return "unknown";
    }
}

[[noreturn]] void Terminate() {
    g_len = 0;
    Put("Spider-Man 2 (Xbox) test: uncaught C++ exception");
    if (std::exception_ptr e = std::current_exception()) {
        try {
            std::rethrow_exception(e);
        } catch (const std::exception& x) {
            Put(": %s (%s)", x.what(), typeid(x).name());
        } catch (...) {
            Put(" of another type");
        }
    }
    Put("\nlog: %s\n", logx::Path().c_str());
    Save();
    LOGE("terminate: uncaught exception, see logs/crash.txt");
    logx::Flush();
    std::abort();
}

const bool g_installed = [] {
    std::set_terminate(Terminate);
    return true;
}();

}  // namespace

extern "C" void __libnx_exception_handler(ThreadExceptionDump* ctx) {
    uintptr_t lo = uintptr_t(&_start), hi = uintptr_t(&__end__);
    auto off = [&](uint64_t a) -> long long { return a >= lo && a < hi ? (long long)(a - lo) : -1; };
    g_len = 0;
    Put("Spider-Man 2 (Xbox) test: crash, %s (desc %X, esr %08X)\n", Desc(ctx->error_desc), ctx->error_desc, ctx->esr);
    Put("module %016lX..%016lX\n", (unsigned long)lo, (unsigned long)hi);
    Put("pc  %016lX  offset %llX\n", (unsigned long)ctx->pc.x, off(ctx->pc.x));
    Put("lr  %016lX  offset %llX\n", (unsigned long)ctx->lr.x, off(ctx->lr.x));
    Put("sp  %016lX  far %016lX\n", (unsigned long)ctx->sp.x, (unsigned long)ctx->far.x);
    for (int i = 0; i < 29; i++) Put("x%-2d %016lX%s", i, (unsigned long)ctx->cpu_gprs[i].x, i % 4 == 3 ? "\n" : "  ");
    Put("\nfp  %016lX\n", (unsigned long)ctx->fp.x);
    Put("log: %s\n", logx::Path().c_str());
    Save();  // before the stack walk, which could fault past the stack's end
    // return addresses into the module among the stack's first 64 KB
    Put("stack:");
    int found = 0;
    const uint64_t* s = reinterpret_cast<const uint64_t*>(ctx->sp.x & ~7ull);
    for (int i = 0; i < 8192 && found < 48; i++) {
        uint64_t v = s[i];
        if (v >= lo + 0x80 && v < hi && !(v & 3)) {
            Put("%s%llX", found % 8 ? " " : "\n  ", off(v));
            found++;
        }
    }
    Put("\nlog: %s\n", logx::Path().c_str());
    Save();
}
