#include "core/log.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
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

static std::mutex g_mutex;
static uint64_t g_written = 0, g_synced = 0;  // lines written to the file, and as of the last Sync
static FILE* g_file = nullptr;
static std::string g_path;
static std::string g_last;  // the newest message, for a progress line
static std::deque<std::string> g_recent;  // the newest lines, for a status screen
constexpr size_t kRecent = 16;
static auto g_start = std::chrono::steady_clock::now();

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
}

const std::string& Path() { return g_path; }

void Write(Level lv, const char* fmt, ...) {
    if (!kVerbose && lv < Level::Error) return;
    char msg[4096];
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);
    double t = std::chrono::duration<double>(std::chrono::steady_clock::now() - g_start).count();
    static const char* names[] = {"debug", "info", "warn", "error", "info"};
    auto tid = std::hash<std::thread::id>()(std::this_thread::get_id()) & 0xFFFF;
    std::lock_guard<std::mutex> lock(g_mutex);
    char line[4300];
    int n = kVerbose ? std::snprintf(line, sizeof(line), "[%9.3f] [%s] [t%04zx] %s\n", t, names[int(lv)], size_t(tid), msg)
                     : std::snprintf(line, sizeof(line), "[%9.3f] %s%s\n", t, lv == Level::Error ? "error: " : "", msg);
    n = std::min(n, int(sizeof(line)) - 1);
    g_last.assign(msg);
    char brief[160];
    std::snprintf(brief, sizeof(brief), "%6.1f %s", t, msg);
    g_recent.emplace_back(brief);
    if (g_recent.size() > kRecent) g_recent.pop_front();
    if (g_file) {
        g_written++;
        std::fwrite(line, 1, size_t(n), g_file);
#ifdef __SWITCH__
        std::fflush(g_file);  // a crash leaves every line on the card
#else
        if (lv >= Level::Warn) std::fflush(g_file);
#endif
    }
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

void Flush() {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_file) std::fflush(g_file);
}

void Sync() {
    int fd = -1;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        // nothing new since the last time: the card is left alone (a flush there costs the game's core time)
        if (!g_file || g_written == g_synced) return;
        g_synced = g_written;
        std::fflush(g_file);
        fd = fileno(g_file);
    }
#ifndef _WIN32
    if (fd >= 0) fsync(fd);  // outside the lock: writers carry on meanwhile
#endif
}

int Descriptor() { return g_file ? fileno(g_file) : -1; }

}  // namespace logx
