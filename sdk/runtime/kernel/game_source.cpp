#include "kernel/game_source.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <fstream>

#include "core/log.h"
#include "cpu/x86_context.h"
#include "kernel/crypto.h"
#include "kernel/image.h"
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

void Digest(const uint8_t* data, size_t n, uint8_t out[20]) {
    alignas(8) uint8_t ctx[128];
    crypto::ShaInit(ctx);
    crypto::ShaUpdate(ctx, data, n);
    crypto::ShaFinal(ctx, out);
}

uint32_t U32(const uint8_t* p) { return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24; }

void Describe(GameCopy& c, const std::string& sha1, const std::string& code, const std::vector<EditionPatch>& editions) {
    c.sha1 = Sha1Hex(c.xbe.data(), c.xbe.size());
    std::vector<uint8_t> bytes;
    if (xbe::CodeBytes(c.xbe, bytes)) c.code = Sha1Hex(bytes.data(), bytes.size());
    const bool same_xbe = c.sha1 == Lower(sha1);
    c.other_edition = !same_xbe && !code.empty() && c.code == Lower(code);
    c.same_release = same_xbe || c.other_edition;
    // an edition built apart: its xbe made into the supported one, the copy's disc as it is
    for (const EditionPatch& e : editions) {
        if (c.same_release || !EditionPatchFits(e, c.xbe)) continue;
        std::vector<uint8_t> out;
        std::string error;
        if (!ApplyEditionPatch(e, c.xbe, out, error) || (!sha1.empty() && Sha1Hex(out.data(), out.size()) != Lower(sha1))) {
            LOGW("game: %s: the patch for %s did not apply: %s", c.name.c_str(), EditionName(e).c_str(),
                 error.empty() ? "another default.xbe came out" : error.c_str());
            continue;
        }
        c.edition = EditionName(e);
        c.other_edition = c.same_release = true;
        xbe::Image x;
        std::string e2;
        if (x.Parse(c.xbe, e2) && x.cert >= x.base && size_t(x.cert - x.base) + 0xB0 <= c.xbe.size()) {
            c.regions = Regions(x.U32(x.cert - x.base + 0xA0));
            c.version = x.U32(x.cert - x.base + 0xAC);
        }
        c.xbe = std::move(out);
        break;
    }
    xbe::Image x;
    std::string error;
    if (x.Parse(c.xbe, error)) {
        c.title_id = x.title_id;
        c.title = Title(x);
        c.same_entry = x.entry == X86EntryPoint;
        if (c.edition.empty() && x.cert >= x.base && size_t(x.cert - x.base) + 0xB0 <= c.xbe.size()) {
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

// tools/edition_patch.py's: a 128 byte header ("SM2NXPD1", the sizes of the edition's xbe and the supported one, their
// SHA-1s, the size of the LZ4 block once decompressed, the steps' count, the edition's name), then the LZ4 block: the
// steps (x, y, z), the added bytes, the new bytes, as bsdiff's
constexpr size_t kPatchHeader = 128;

std::string EditionName(const EditionPatch& patch) {
    if (patch.size < kPatchHeader) return "?";
    const char* name = reinterpret_cast<const char*>(patch.data + 64);
    return std::string(name, strnlen(name, 64));
}

bool EditionPatchFits(const EditionPatch& patch, const std::vector<uint8_t>& xbe) {
    if (patch.size < kPatchHeader || std::memcmp(patch.data, "SM2NXPD1", 8) || U32(patch.data + 8) != xbe.size()) return false;
    uint8_t sha[20];
    Digest(xbe.data(), xbe.size(), sha);
    return !std::memcmp(sha, patch.data + 16, 20);
}

bool ApplyEditionPatch(const EditionPatch& patch, const std::vector<uint8_t>& src, std::vector<uint8_t>& out, std::string& error) {
    if (!EditionPatchFits(patch, src)) {
        error = "not this default.xbe's patch";
        return false;
    }
    const uint8_t* h = patch.data;
    const size_t out_size = U32(h + 12), body_size = U32(h + 56), count = U32(h + 60);
    std::vector<uint8_t> body(body_size);
    if (body_size < count * 12 || Lz4Decode(h + kPatchHeader, patch.size - kPatchHeader, body.data(), body_size) != int(body_size)) {
        error = "the patch is damaged";
        return false;
    }
    size_t added = count * 12, fresh = added;
    for (size_t i = 0; i < count; i++) fresh += U32(&body[i * 12]);
    out.clear();
    out.reserve(out_size);
    int64_t at = 0;
    for (size_t i = 0; i < count; i++) {
        const int32_t x = int32_t(U32(&body[i * 12])), y = int32_t(U32(&body[i * 12 + 4])), z = int32_t(U32(&body[i * 12 + 8]));
        if (x < 0 || y < 0 || added + size_t(x) > body_size || fresh + size_t(y) > body_size || at < 0 || at + x > int64_t(src.size()) ||
            out.size() + size_t(x) + size_t(y) > out_size) {
            error = "the patch is damaged";
            return false;
        }
        for (int32_t k = 0; k < x; k++) out.push_back(uint8_t(body[added + k] + src[size_t(at) + k]));
        added += size_t(x);
        at += x;
        out.insert(out.end(), body.begin() + fresh, body.begin() + fresh + y);
        fresh += size_t(y);
        at += z;
    }
    uint8_t sha[20];
    Digest(out.data(), out.size(), sha);
    if (out.size() != out_size || std::memcmp(sha, h + 36, 20)) {
        error = "the patch did not make the supported default.xbe";
        return false;
    }
    return true;
}

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
                          const std::string& code, const std::vector<EditionPatch>& editions) {
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
                Describe(c, sha1, code, editions);
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
            Describe(c, sha1, code, editions);
            out.copies.push_back(std::move(c));
        }
    }
    std::stable_sort(out.copies.begin(), out.copies.end(), [](const GameCopy& a, const GameCopy& b) {
        return a.same_release != b.same_release ? a.same_release : a.same_entry && !b.same_entry;
    });
    for (const GameCopy& c : out.copies)
        LOGI("game: %s (%s), %s %s, game code %s, title %08X \"%s\" version %u, regions %s%s", c.name.c_str(), c.kind.c_str(), xbe_name,
             c.sha1.c_str(), c.code.c_str(), c.title_id, c.title.c_str(), c.version, c.regions.c_str(),
             !c.edition.empty() ? (", the " + c.edition + " edition, its default.xbe made into the supported one").c_str()
             : c.other_edition  ? ", another edition of the same game code"
             : c.same_release ? ""
             : c.same_entry   ? ", another release"
                              : ", another game or version");
    for (const std::string& s : out.skipped) LOGW("game: %s", s.c_str());
    return out;
}

}  // namespace kernel
