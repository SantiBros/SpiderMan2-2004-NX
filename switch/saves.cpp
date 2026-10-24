#include "switch/saves.h"

#include <switch.h>

#include <unistd.h>

#include <atomic>
#include <cstdio>
#include <ctime>
#include <mutex>
#include <set>
#include <string>
#include <system_error>
#include <thread>
#include <vector>

#include "core/log.h"
#include "kernel/kernel.h"

namespace fs = std::filesystem;

namespace sw {

namespace {

const fs::path kBackup = "sdmc:/config/SpiderMan2Xbox/save";

Folders g_folders;
std::mutex g_mirror;  // one copy at a time: the backup thread's, or the one at quit
std::atomic<uint64_t> g_mirrored{~0ull};  // kernel::SaveChanges() as of the last copy

// the save games in a saves folder: its folders (the files beside them are XAPI's, made at every start)
std::vector<std::string> SaveGames(const fs::path& progress) {
    std::vector<std::string> games;
    std::error_code ec;
    for (const auto& e : fs::directory_iterator(progress, ec))
        if (e.is_directory(ec)) games.push_back(e.path().filename().string());
    return games;
}

// files and bytes under a folder
void Measure(const fs::path& dir, int& files, uint64_t& bytes) {
    std::error_code ec;
    for (auto it = fs::recursive_directory_iterator(dir, ec); !ec && it != fs::recursive_directory_iterator(); it.increment(ec))
        if (it->is_regular_file(ec)) {
            files++;
            bytes += it->file_size(ec);
        }
}

std::string Describe(const fs::path& progress) {
    std::vector<std::string> games = SaveGames(progress);
    if (games.empty()) return "none";
    std::string out;
    for (const std::string& g : games) {
        int files = 0;
        uint64_t bytes = 0;
        Measure(progress / g, files, bytes);
        char buf[96];
        std::snprintf(buf, sizeof buf, "%s%s (%d files, %llu KB)", out.empty() ? "" : ", ", g.c_str(), files,
                      (unsigned long long)((bytes + 1023) / 1024));
        out += buf;
    }
    return out;
}

bool ReadAll(const fs::path& p, std::vector<uint8_t>& out) {
    FILE* f = std::fopen(p.string().c_str(), "rb");
    if (!f) return false;
    out.clear();
    uint8_t buf[16384];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) out.insert(out.end(), buf, buf + n);
    bool ok = !std::ferror(f);
    std::fclose(f);
    return ok;
}

// a file's bytes into to, through a temporary file: an interrupted copy never leaves half a file in its place
bool WriteFile(const fs::path& to, const std::vector<uint8_t>& bytes) {
    fs::path tmp = to;
    tmp += ".tmp";
    FILE* f = std::fopen(tmp.string().c_str(), "wb");
    if (!f) return false;
    bool ok = bytes.empty() || std::fwrite(bytes.data(), 1, bytes.size(), f) == bytes.size();
    ok = std::fflush(f) == 0 && ok;
    fsync(fileno(f));
    ok = std::fclose(f) == 0 && ok;
    std::error_code ec;
    if (!ok) {
        fs::remove(tmp, ec);
        return false;
    }
    fs::remove(to, ec);  // the Switch's file system does not rename over a file
    fs::rename(tmp, to, ec);
    return !ec;
}

// from copied into to, file by file where they differ; with prune, what to has that from hasn't goes. Counts the
// files written and the ones that could not be read (open for writing by the game, which the Switch refuses).
void Mirror(const fs::path& from, const fs::path& to, bool prune, int& written, int& skipped) {
    std::error_code ec;
    fs::create_directories(to, ec);
    std::set<std::string> names;
    std::vector<uint8_t> a, b;
    for (const auto& e : fs::directory_iterator(from, ec)) {
        std::string name = e.path().filename().string();
        if (name.size() > 4 && name.compare(name.size() - 4, 4, ".tmp") == 0) continue;
        names.insert(name);
        fs::path target = to / name;
        if (e.is_directory(ec)) {
            Mirror(e.path(), target, prune, written, skipped);
            continue;
        }
        if (!ReadAll(e.path(), a)) {
            skipped++;
            continue;
        }
        if (fs::exists(target, ec) && fs::file_size(target, ec) == a.size() && ReadAll(target, b) && a == b) continue;
        if (WriteFile(target, a))
            written++;
        else
            skipped++;
    }
    if (!prune) return;
    std::vector<fs::path> gone;
    for (const auto& e : fs::directory_iterator(to, ec))
        if (!names.count(e.path().filename().string())) gone.push_back(e.path());
    for (const fs::path& p : gone) fs::remove_all(p, ec);
}

// only the files from has that to hasn't (a restore never overwrites what is there)
void CopyMissing(const fs::path& from, const fs::path& to, int& written) {
    std::error_code ec;
    fs::create_directories(to, ec);
    std::vector<uint8_t> bytes;
    for (const auto& e : fs::directory_iterator(from, ec)) {
        fs::path target = to / e.path().filename();
        if (e.is_directory(ec)) {
            CopyMissing(e.path(), target, written);
        } else if (!fs::exists(target, ec) && ReadAll(e.path(), bytes) && WriteFile(target, bytes)) {
            written++;
        }
    }
}

// the saves into the copy; never while save/progress has no save game (the copy is then all there is)
void BackUp(uint64_t changes) {
    std::lock_guard<std::mutex> lock(g_mirror);
    if (SaveGames(g_folders.saves).empty()) {
        g_mirrored = changes;
        return;
    }
    int written = 0, skipped = 0;
    Mirror(g_folders.saves, kBackup / "progress", true, written, skipped);
    Mirror(g_folders.title_data, kBackup / "data", true, written, skipped);
    // what could not be read is tried again at the next change, or at quit
    g_mirrored = skipped ? ~0ull : changes;
    if (written || skipped)
        LOGI("saves: copy in %s brought up to date, %d file(s) written%s", kBackup.string().c_str(), written,
             skipped ? ", some could not be read yet (the game had them open)" : "");
}

void BackupLoop() {
    kernel::NameHostThread("watch");
    uint64_t seen = kernel::SaveChanges();
    int quiet = 0;
    for (;;) {
        svcSleepThread(2'000'000'000);
        uint64_t now = kernel::SaveChanges();
        quiet = now == seen ? quiet + 1 : 0;
        seen = now;
        // 4 seconds without a change: the game has finished saving (it writes its files about 3 s apart)
        if (quiet >= 2 && now != g_mirrored) BackUp(now);
    }
}

}  // namespace

bool RestoreSaves(const Folders& f) {
    g_folders = f;
    std::vector<std::string> here = SaveGames(f.saves), copy = SaveGames(kBackup / "progress");
    LOGI("saves: in save/progress: %s; the copy in %s: %s", Describe(f.saves).c_str(), kBackup.string().c_str(),
         Describe(kBackup / "progress").c_str());
    if (!here.empty() || copy.empty()) return false;
    int written = 0;
    CopyMissing(kBackup / "progress", f.saves, written);
    CopyMissing(kBackup / "data", f.title_data, written);
    LOGW("saves: save/progress had no saves: %zu save game(s) put back from the copy in %s (%d files)", copy.size(),
         kBackup.string().c_str(), written);
    return true;
}

void StartSaveBackup(const Folders& f) {
    g_folders = f;
    std::thread(BackupLoop).detach();
}

void BackUpSavesNow() {
    if (g_folders.saves.empty()) return;
    uint64_t now = kernel::SaveChanges();
    if (now != g_mirrored) BackUp(now);
}

}  // namespace sw
