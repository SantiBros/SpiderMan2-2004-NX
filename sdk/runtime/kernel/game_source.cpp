#include "kernel/game_source.h"

#include <algorithm>
#include <cctype>
#include <fstream>

#include "core/log.h"
#include "cpu/x86_context.h"
#include "kernel/crypto.h"
#include "xbe/code.h"
#include "xbe/xbe.h"

namespace kernel {

namespace fs = std::filesystem;

namespace {

std::string Lower(std::string s) {
    for (char& c : s) c = char(std::tolower(uint8_t(c)));
    return s;
}

// a file in a folder in any case: the SD card does not mind case, Linux does
fs::path FindIn(const fs::path& dir, const std::string& name) {
    std::error_code ec;
    for (auto& e : fs::directory_iterator(dir, ec))
        if (Lower(e.path().filename().string()) == Lower(name)) return e.path();
    return {};
}

bool ReadHostFile(const fs::path& path, std::vector<uint8_t>& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    out.assign(std::istreambuf_iterator<char>(f), {});
    return true;
}

// the certificate's title, 40 UTF-16 characters at +0xC, kept to ASCII for the screen's font
std::string Title(const xbe::Image& x) {
    if (!x.cert || x.cert < x.base) return {};
    size_t at = size_t(x.cert - x.base) + 0xC;
    std::string t;
    for (size_t i = 0; i < 40 && at + i * 2 + 1 < x.file.size(); i++) {
        uint16_t c = uint16_t(x.file[at + i * 2] | x.file[at + i * 2 + 1] << 8);
        if (!c) break;
        t += c < 0x80 ? char(c) : '?';
    }
    return t;
}

// the certificate's game regions (+0xA0)
std::string Regions(uint32_t r) {
    std::string s;
    auto add = [&](const char* n) { s += std::string(s.empty() ? "" : ", ") + n; };
    if (r & 1) add("North America");
    if (r & 2) add("Japan");
    if (r & 4) add("Europe and the rest of the world");
    if (r & 0x80000000u) add("manufacturing");
    return s.empty() ? "none" : s;
}

void Describe(GameCopy& c, const std::string& sha1, const std::string& code) {
    c.sha1 = Sha1Hex(c.xbe.data(), c.xbe.size());
    std::vector<uint8_t> bytes;
    if (xbe::CodeBytes(c.xbe, bytes)) c.code = Sha1Hex(bytes.data(), bytes.size());
    const bool same_xbe = c.sha1 == Lower(sha1);
    c.other_edition = !same_xbe && !code.empty() && c.code == Lower(code);
    c.same_release = same_xbe || c.other_edition;
    xbe::Image x;
    std::string error;
    if (x.Parse(c.xbe, error)) {
        c.title_id = x.title_id;
        c.title = Title(x);
        c.same_entry = x.entry == X86EntryPoint;
        if (x.cert >= x.base && size_t(x.cert - x.base) + 0xB0 <= c.xbe.size()) {
            c.regions = Regions(x.U32(x.cert - x.base + 0xA0));
            c.version = x.U32(x.cert - x.base + 0xAC);
        }
    }
}

bool IsImage(const std::string& ext) { return ext == ".iso" || ext == ".xiso" || ext == ".cci" || ext == ".cso"; }
bool IsArchive(const std::string& ext) { return ext == ".zip" || ext == ".7z" || ext == ".rar"; }

// name.N.ext with N from 2 to 99 and name.1.ext beside it: a later part of a split image, found through the first
bool LaterPart(const fs::path& p, const std::vector<std::string>& names) {
    std::string stem = p.stem().string();
    size_t dot = stem.rfind('.');
    if (dot == std::string::npos || dot + 1 == stem.size() || stem.size() - dot > 3) return false;
    std::string n = stem.substr(dot + 1);
    if (!std::all_of(n.begin(), n.end(), [](char c) { return std::isdigit(uint8_t(c)); }) || std::stoi(n) < 2) return false;
    std::string first = Lower(stem.substr(0, dot) + ".1" + p.extension().string());
    return std::find(names.begin(), names.end(), first) != names.end();
}

}  // namespace

std::string Sha1Hex(const uint8_t* data, size_t n) {
    alignas(8) uint8_t ctx[128];
    uint8_t digest[20];
    crypto::ShaInit(ctx);
    crypto::ShaUpdate(ctx, data, n);
    crypto::ShaFinal(ctx, digest);
    std::string hex;
    for (uint8_t b : digest) {
        hex += "0123456789abcdef"[b >> 4];
        hex += "0123456789abcdef"[b & 15];
    }
    return hex;
}

GameSearch FindGameCopies(const std::vector<fs::path>& folders, const char* xbe_name, const std::string& sha1,
                          const std::string& code) {
    GameSearch out;
    std::vector<fs::path> seen;
    for (const fs::path& dir : folders) {
        std::error_code ec;
        if (dir.empty() || !fs::is_directory(dir, ec)) continue;
        // compared as text: resolving links needs calls the Switch's C library lacks
        fs::path canon = Lower(dir.lexically_normal().string());
        if (std::find(seen.begin(), seen.end(), canon) != seen.end()) continue;
        seen.push_back(canon);

        // the disc's files
        if (fs::path xbe = FindIn(dir, xbe_name); !xbe.empty()) {
            GameCopy c;
            if (ReadHostFile(xbe, c.xbe)) {
                c.folder = dir;
                c.name = dir.filename().string();
                c.kind = "folder";
                Describe(c, sha1, code);
                out.copies.push_back(std::move(c));
            }
        }

        // disc images, by name
        std::vector<fs::path> files;
        for (auto& e : fs::directory_iterator(dir, ec))
            if (e.is_regular_file(ec)) files.push_back(e.path());
        std::sort(files.begin(), files.end());
        std::vector<std::string> names;
        for (const fs::path& f : files) names.push_back(Lower(f.filename().string()));
        for (const fs::path& f : files) {
            std::string ext = Lower(f.extension().string()), name = f.filename().string();
            if (IsArchive(ext)) {
                out.skipped.push_back(name + ": a compressed archive, extract the disc image or the game's folder from it");
                continue;
            }
            if (!IsImage(ext) || LaterPart(f, names)) continue;
            GameCopy c;
            c.disc = std::make_unique<Disc>();
            std::string error;
            if (!c.disc->Open(f, error)) {
                out.skipped.push_back(name + ": " + error);
                continue;
            }
            if (!c.disc->Load(xbe_name, c.xbe)) {
                out.skipped.push_back(name + ": no " + std::string(xbe_name) + " on it");
                continue;
            }
            c.folder = dir;
            c.name = name;
            c.kind = c.disc->Kind();
            Describe(c, sha1, code);
            out.copies.push_back(std::move(c));
        }
    }
    std::stable_sort(out.copies.begin(), out.copies.end(), [](const GameCopy& a, const GameCopy& b) {
        return a.same_release != b.same_release ? a.same_release : a.same_entry && !b.same_entry;
    });
    for (const GameCopy& c : out.copies)
        LOGI("game: %s (%s), %s %s, game code %s, title %08X \"%s\" version %u, regions %s%s", c.name.c_str(), c.kind.c_str(), xbe_name,
             c.sha1.c_str(), c.code.c_str(), c.title_id, c.title.c_str(), c.version, c.regions.c_str(),
             c.other_edition  ? ", another edition of the same game code"
             : c.same_release ? ""
             : c.same_entry   ? ", another release"
                              : ", another game or version");
    for (const std::string& s : out.skipped) LOGW("game: %s", s.c_str());
    return out;
}

}  // namespace kernel
