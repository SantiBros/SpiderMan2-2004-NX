#include "switch/folders.h"

#include <cctype>
#include <cstdio>
#include <string>
#include <system_error>

#include "core/log.h"

namespace fs = std::filesystem;

namespace sw {

namespace {

// a child folder whatever its case ("4156002b" or "4156002B"), empty when there is none
fs::path FindFolder(const fs::path& parent, const std::string& name) {
    std::error_code ec;
    if (!fs::is_directory(parent, ec)) return {};
    auto lower = [](std::string s) {
        for (char& c : s) c = char(std::tolower(uint8_t(c)));
        return s;
    };
    for (const auto& e : fs::directory_iterator(parent, ec))
        if (e.is_directory(ec) && lower(e.path().filename().string()) == lower(name)) return e.path();
    return {};
}

// everything in from moved into to; false when something stayed behind
bool MoveContents(const fs::path& from, const fs::path& to) {
    std::error_code ec;
    fs::create_directories(to, ec);
    bool ok = true;
    int moved = 0;
    for (const auto& e : fs::directory_iterator(from, ec)) {
        fs::path target = to / e.path().filename();
        if (fs::exists(target, ec)) {
            LOGW("folders: %s is already in %s, the old one stays in %s", e.path().filename().string().c_str(),
                 to.string().c_str(), from.string().c_str());
            ok = false;
            continue;
        }
        fs::rename(e.path(), target, ec);
        if (ec) {
            LOGW("folders: %s could not be moved to %s (%s)", e.path().string().c_str(), to.string().c_str(),
                 ec.message().c_str());
            ok = false;
        } else {
            moved++;
        }
    }
    if (moved) LOGI("folders: %d item(s) moved from %s to %s", moved, from.string().c_str(), to.string().c_str());
    if (ok) fs::remove(from, ec);  // empty now
    return ok;
}

}  // namespace

Folders SetUpFolders(const fs::path& dir, uint32_t title_id) {
    char id[9];
    std::snprintf(id, sizeof id, "%08X", title_id);
    Folders f{dir / "save" / "progress", dir / "save" / "data", dir / "cache"};
    std::error_code ec;
    const fs::path old = dir / "hdd";
    bool old_saves_kept = false;
    if (fs::is_directory(old, ec)) {
        // 0.0.x: the whole hard disk in hdd/. The game's own folders go to save/, the rest becomes cache/
        struct Part {
            const char* drive;
            fs::path* to;
        } parts[] = {{"UDATA", &f.saves}, {"TDATA", &f.title_data}};
        for (Part& p : parts) {
            fs::path from = FindFolder(old / "E" / p.drive, id);
            if (from.empty()) continue;
            if (!MoveContents(from, *p.to)) {
                *p.to = from;  // what couldn't be moved is used where it is
                old_saves_kept = true;
            }
        }
        if (!old_saves_kept && !fs::exists(f.cache, ec)) {
            fs::rename(old, f.cache, ec);
            if (ec)
                LOGW("folders: hdd could not become cache (%s)", ec.message().c_str());
            else
                LOGI("folders: hdd is cache now, the saves are in save/");
        } else if (!old_saves_kept) {
            LOGW("folders: both hdd and cache are there: hdd is no longer used and can be deleted");
        }
    }
    if (fs::exists(dir / "shaders.cache", ec) && !fs::exists(f.cache / "shaders.cache", ec)) {
        fs::create_directories(f.cache, ec);
        fs::rename(dir / "shaders.cache", f.cache / "shaders.cache", ec);
        if (!ec) LOGI("folders: shaders.cache moved into cache/");
    }
    for (const fs::path& p : {f.saves, f.title_data, f.cache}) fs::create_directories(p, ec);
    LOGI("folders: saves in %s, the game's data in %s, cache in %s", f.saves.string().c_str(),
         f.title_data.string().c_str(), f.cache.string().c_str());
    return f;
}

}  // namespace sw
