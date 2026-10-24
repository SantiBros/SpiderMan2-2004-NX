#include "core/log.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <mutex>
#include <deque>
#include <thread>
#include <vector>

#ifndef _WIN32
#include <unistd.h>
#endif

namespace fs = std::filesystem;

namespace logx {

static std::mutex g_mutex;  // the lines: taken by every thread that logs, never held over a write to the file
static std::mutex g_io;     // the file: one writer at a time (the watch thread, a Flush, a quit)
static uint64_t g_written = 0, g_synced = 0;  // lines taken, and as of the last Sync
static FILE* g_file = nullptr;
static std::string g_path;
static std::string g_last;  // the newest message, for a progress line
static std::deque<std::string> g_recent;  // the newest lines, for a status screen
constexpr size_t kRecent = 16;
static auto g_start = std::chrono::steady_clock::now();

// In the background (the Switch, once its watch thread runs) a line only goes into g_pending, and the watch thread
// writes it out: the thread that logs never waits for the SD card. A card that is slow for long fills it up to
// kPendingMax, and the lines after that are only counted.
static bool g_background = false;
static std::string g_pending;
static uint64_t g_dropped = 0;
constexpr size_t kPendingMax = 1 << 20;

// The same message (its format) more than kFloodLines times in kFloodSeconds: the rest of that time it is counted,
// not written, and a line then says how many were left out. Errors are always written. A message written every frame
// (0.2.0's and 0.2.1's 16:9 camera line, thousands of times a minute) can no longer fill the log or the card.
constexpr uint32_t kFloodLines = 100;
constexpr double kFloodSeconds = 10.0;
struct Flood {
    const char* fmt = nullptr;
    double start = 0;
    uint32_t count = 0, skipped = 0;
};
static Flood g_flood[128];

constexpr int kKeep = 30;

// the run number of prefix_NNN.log, 0 for another name
static int RunNumber(const fs::path& file, const std::string& prefix) {
    std::string name = file.filename().string();
    if (file.extension() != ".log" || name.size() <= prefix.size() + 1 || name.compare(0, prefix.size() + 1, prefix + "_"))
        return 0;
    return std::atoi(name.c_str() + prefix.size() + 1);
}

void Init(const std::string& dir, const std::string& prefix, bool append) {
    std::lock_guard<std::mutex> lock(g_mutex);
    std::error_code ec;
    fs::create_directories(dir, ec);
    if (!kVerbose) {
        g_path = (fs::path(dir) / (prefix + ".log")).string();
        g_file = std::fopen(g_path.c_str(), append ? "a" : "w");
        return;
    }
    std::vector<std::pair<int, fs::path>> runs;
    for (const auto& e : fs::directory_iterator(dir, ec))
        if (int n = RunNumber(e.path(), prefix)) runs.emplace_back(n, e.path());
    std::sort(runs.begin(), runs.end());
    for (size_t i = 0; i + kKeep <= runs.size(); i++) fs::remove(runs[i].second, ec);
    char name[256];
    std::snprintf(name, sizeof(name), "%s_%03d.log", prefix.c_str(), runs.empty() ? 1 : runs.back().first + 1);
    g_path = (fs::path(dir) / name).string();
    g_file = std::fopen(g_path.c_str(), "w");
#ifdef __SWITCH__
    // the lines before the watch thread starts go out in big writes too
    if (g_file) std::setvbuf(g_file, nullptr, _IOFBF, 64 * 1024);
#endif
}

const std::string& Path() { return g_path; }

static double Now() { return std::chrono::duration<double>(std::chrono::steady_clock::now() - g_start).count(); }

// a whole line, with g_mutex held: into g_pending in the background, else into the file
static void Put(Level lv, const char* line, int n) {
    if (!g_file) return;
    g_written++;
    if (g_background) {
        if (g_pending.size() + size_t(n) <= kPendingMax)
            g_pending.append(line, size_t(n));
        else
            g_dropped++;
        return;
    }
    std::fwrite(line, 1, size_t(n), g_file);
    if (lv >= Level::Warn) std::fflush(g_file);
}

// a line of the log's own, with g_mutex held
static void PutOwn(double t, const char* msg) {
    char line[512];
    auto tid = std::hash<std::thread::id>()(std::this_thread::get_id()) & 0xFFFF;
    int n = std::snprintf(line, sizeof(line), "[%9.3f] [info] [t%04zx] %s\n", t, size_t(tid), msg);
    n = std::min(n, int(sizeof(line)) - 1);
    Put(Level::Info, line, n);
#ifndef __SWITCH__
    std::fwrite(line, 1, size_t(n), stderr);
#endif
}

// how many lines of a flooding message were left out, with g_mutex held
static void PutSkipped(Flood& f, double t) {
    if (!f.skipped) return;
    char msg[200];
    std::snprintf(msg, sizeof(msg), "log: %u more lines like \"%.80s\" left out (over %u in %.0f s)", f.skipped, f.fmt,
                  kFloodLines, kFloodSeconds);
    f.skipped = 0;
    PutOwn(t, msg);
}

// false when this message is flooding the log, with g_mutex held
static bool FloodAllows(Level lv, const char* fmt, double t) {
    if (lv >= Level::Error) return true;
    Flood& f = g_flood[(reinterpret_cast<uintptr_t>(fmt) >> 3) % (sizeof(g_flood) / sizeof(g_flood[0]))];
    if (f.fmt != fmt || t - f.start >= kFloodSeconds) {
        PutSkipped(f, t);
        f.fmt = fmt;
        f.start = t;
        f.count = 0;
    }
    if (++f.count <= kFloodLines) return true;
    f.skipped++;
    return false;
}

void Write(Level lv, const char* fmt, ...) {
    if (!kVerbose && lv < Level::Error) return;
    double t = Now();
    std::lock_guard<std::mutex> lock(g_mutex);
    if (!FloodAllows(lv, fmt, t)) return;
    char msg[4096];
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);
    static const char* names[] = {"debug", "info", "warn", "error", "info"};
    auto tid = std::hash<std::thread::id>()(std::this_thread::get_id()) & 0xFFFF;
    char line[4300];
    int n = kVerbose ? std::snprintf(line, sizeof(line), "[%9.3f] [%s] [t%04zx] %s\n", t, names[int(lv)], size_t(tid), msg)
                     : std::snprintf(line, sizeof(line), "[%9.3f] %s%s\n", t, lv == Level::Error ? "error: " : "", msg);
    n = std::min(n, int(sizeof(line)) - 1);
    g_last.assign(msg);
    char brief[160];
    std::snprintf(brief, sizeof(brief), "%6.1f %s", t, msg);
    g_recent.emplace_back(brief);
    if (g_recent.size() > kRecent) g_recent.pop_front();
    Put(lv, line, n);
#ifndef __SWITCH__
    std::fwrite(line, 1, size_t(n), stderr);  // on the Switch stderr is the text console, not thread safe
#endif
}

std::string Last() {
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_last;
}

bool Recent(size_t n, std::string& out) {
    std::unique_lock<std::mutex> lock(g_mutex, std::try_to_lock);
    if (!lock) return false;
    out.clear();
    for (size_t i = g_recent.size() > n ? g_recent.size() - n : 0; i < g_recent.size(); i++) {
        if (!out.empty()) out += '\n';
        out += g_recent[i];
    }
    return true;
}

void Background(bool on) {
    std::lock_guard<std::mutex> io(g_io);
    std::lock_guard<std::mutex> lock(g_mutex);
    g_background = on;
    if (!on && g_file && !g_pending.empty()) {
        // the kept lines first, before the ones written at once from now on
        std::fwrite(g_pending.data(), 1, g_pending.size(), g_file);
        g_pending.clear();
    }
}

// the waiting lines into the file and the file's buffer out; true when there was something new
static bool WriteOut() {
    std::string out;
    uint64_t dropped = 0;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        if (!g_file) return false;
        // a message that stopped flooding: how many of it were left out
        double t = Now();
        for (Flood& f : g_flood)
            if (f.skipped && t - f.start >= kFloodSeconds) PutSkipped(f, t);
        if (g_written == g_synced) return false;  // nothing new: the card is left alone
        g_synced = g_written;
        out.swap(g_pending);
        dropped = g_dropped;
        g_dropped = 0;
    }
    // only this thread writes to the file now: the threads that log go on meanwhile
    if (!out.empty()) std::fwrite(out.data(), 1, out.size(), g_file);
    if (dropped) std::fprintf(g_file, "[log] %llu lines left out: the SD card was too slow to take them\n", (unsigned long long)dropped);
    std::fflush(g_file);
    return true;
}

void Flush() {
    std::lock_guard<std::mutex> io(g_io);
    WriteOut();
}

void Sync() {
    std::lock_guard<std::mutex> io(g_io);
    if (!WriteOut()) return;
#ifndef _WIN32
    fsync(fileno(g_file));
#endif
}

void CrashFlush() {
    // best effort, from a crash handler: never waits for a lock (the crashing thread may hold one)
    std::unique_lock<std::mutex> io(g_io, std::try_to_lock);
    if (!io) return;
    std::unique_lock<std::mutex> lock(g_mutex, std::try_to_lock);
    if (!lock || !g_file) return;
    std::fflush(g_file);
    if (!g_pending.empty()) {
#ifndef _WIN32
        if (write(fileno(g_file), g_pending.data(), g_pending.size()) > 0) g_pending.clear();
#endif
    }
}

int Descriptor() { return g_file ? fileno(g_file) : -1; }

}  // namespace logx
