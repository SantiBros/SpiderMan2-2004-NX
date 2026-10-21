#include "kernel/image.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <map>
#include <mutex>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace kernel {

namespace fs = std::filesystem;

namespace {

constexpr uint32_t kSector = 2048;

uint32_t Le32(const uint8_t* p) { return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24; }
uint64_t Le64(const uint8_t* p) { return uint64_t(Le32(p)) | uint64_t(Le32(p + 4)) << 32; }

std::string Lower(std::string s) {
    for (char& c : s) c = char(std::tolower(uint8_t(c)));
    return s;
}

// one file of the image, read at a position from any thread
class HostFile {
public:
    HostFile() = default;
    HostFile(const HostFile&) = delete;
    HostFile& operator=(const HostFile&) = delete;
    ~HostFile() {
        if (file_ == -1) return;
#ifdef _WIN32
        CloseHandle(HANDLE(file_));
#else
        close(int(file_));
#endif
    }

    bool Open(const fs::path& path) {
#ifdef _WIN32
        HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
        if (h == INVALID_HANDLE_VALUE) return false;
        file_ = intptr_t(h);
        LARGE_INTEGER size;
        if (!GetFileSizeEx(h, &size)) return false;
        size_ = uint64_t(size.QuadPart);
#else
        int fd = open(path.c_str(), O_RDONLY);
        if (fd < 0) return false;
        file_ = fd;
        struct stat st;
        if (fstat(fd, &st) != 0) return false;
        size_ = uint64_t(st.st_size);
#endif
        return true;
    }
    uint64_t Size() const { return size_; }

    // each read carries its own position, so threads reading at once do not queue on one handle
    uint32_t ReadAt(uint64_t at, void* out, uint32_t len) const {
        if (at >= size_) return 0;
        len = uint32_t(std::min<uint64_t>(len, size_ - at));
#ifdef _WIN32
        thread_local HANDLE event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        OVERLAPPED o{};
        o.Offset = DWORD(at);
        o.OffsetHigh = DWORD(at >> 32);
        o.hEvent = event;
        DWORD got = 0;
        if (!ReadFile(HANDLE(file_), out, len, nullptr, &o) && GetLastError() != ERROR_IO_PENDING) return 0;
        if (!GetOverlappedResult(HANDLE(file_), &o, &got, TRUE)) return 0;
        return got;
#elif defined(__SWITCH__)
        // newlib has no pread here: seek and read under one lock per file
        std::lock_guard<std::mutex> lock(lock_);
        if (lseek(int(file_), off_t(at), SEEK_SET) < 0) return 0;
        uint32_t done = 0;
        while (done < len) {
            ssize_t got = read(int(file_), static_cast<uint8_t*>(out) + done, len - done);
            if (got <= 0) break;
            done += uint32_t(got);
        }
        return done;
#else
        uint32_t done = 0;
        while (done < len) {
            ssize_t got = pread(int(file_), static_cast<uint8_t*>(out) + done, len - done, off_t(at + done));
            if (got <= 0) break;
            done += uint32_t(got);
        }
        return done;
#endif
    }

private:
    intptr_t file_ = -1;  // HANDLE or fd
    uint64_t size_ = 0;
#if defined(__SWITCH__)
    mutable std::mutex lock_;
#endif
};

// files one after another as one run of bytes: a plain image, or the parts of a split one
class Joined {
public:
    bool Open(const std::vector<fs::path>& parts, std::string& error) {
        uint64_t start = 0;
        for (const fs::path& p : parts) {
            auto f = std::make_unique<HostFile>();
            if (!f->Open(p)) {
                error = "cannot open " + p.filename().string();
                return false;
            }
            starts_.push_back(start);
            start += f->Size();
            files_.push_back(std::move(f));
        }
        size_ = start;
        return !files_.empty();
    }
    uint64_t Size() const { return size_; }
    uint32_t Read(uint64_t at, void* out, uint32_t len) const {
        uint32_t done = 0;
        while (done < len && at < size_) {
            size_t i = size_t(std::upper_bound(starts_.begin(), starts_.end(), at) - starts_.begin()) - 1;
            uint64_t in = at - starts_[i];
            uint32_t want = uint32_t(std::min<uint64_t>(len - done, files_[i]->Size() - in));
            uint32_t got = files_[i]->ReadAt(in, static_cast<uint8_t*>(out) + done, want);
            done += got;
            at += got;
            if (got < want) break;
        }
        return done;
    }
    size_t Count() const { return files_.size(); }

private:
    std::vector<std::unique_ptr<HostFile>> files_;
    std::vector<uint64_t> starts_;
    uint64_t size_ = 0;
};

std::string Parts(const char* kind, size_t n) { return n > 1 ? std::string(kind) + " in " + std::to_string(n) + " parts" : kind; }

class PlainImage final : public Image {
public:
    bool Open(const std::vector<fs::path>& parts, std::string& error) {
        if (!data_.Open(parts, error)) return false;
        kind_ = Parts("ISO", data_.Count());
        return true;
    }
    uint64_t Size() const override { return data_.Size(); }
    uint32_t Read(uint64_t at, void* out, uint32_t len) const override { return data_.Read(at, out, len); }

private:
    Joined data_;
};

// Sectors behind an index of 32-bit entries, bit 31 a flag, the rest a position in units of 1 << align: sector i
// spans entry i to entry i + 1. Consecutive sectors are read in one go, then decoded one by one.
class SectorImage : public Image {
public:
    uint64_t Size() const override { return size_; }

    uint32_t Read(uint64_t at, void* out, uint32_t len) const override {
        auto* dst = static_cast<uint8_t*>(out);
        uint32_t done = 0;
        thread_local std::vector<uint8_t> span, sector;
        sector.resize(block_);
        while (done < len && at < size_) {
            uint64_t first = at / block_;
            uint32_t within = uint32_t(at % block_);
            const Run& run = RunOf(first);
            // as many sectors as are wanted, in this run, with at most 4 MB to read
            uint64_t last = std::min<uint64_t>({(at + (len - done) - 1) / block_, run.first + run.count - 1, first + 2047});
            uint64_t from = Position(run, first);
            while (last > first && Position(run, last + 1) - from > (4u << 20)) last--;
            uint64_t to = Position(run, last + 1);
            if (to < from || to - from > (64u << 20)) return done;
            span.resize(size_t(to - from) + 8);
            if (run.data->Read(from, span.data(), uint32_t(to - from)) != to - from) return done;
            for (uint64_t s = first; s <= last; s++) {
                uint64_t p = Position(run, s) - from, q = Position(run, s + 1) - from;
                if (q < p) return done;
                uint32_t take = uint32_t(std::min<uint64_t>(block_ - within, len - done));
                bool whole = within == 0 && take == block_;
                uint8_t* target = whole ? dst + done : sector.data();
                if (!Decode(run, s, span.data() + p, uint32_t(q - p), target)) return done;
                if (!whole) std::memcpy(dst + done, sector.data() + within, take);
                done += take;
                at += take;
                within = 0;
            }
        }
        return done;
    }

protected:
    // the sectors of one file: CCI has one per part, CSO one for all
    struct Run {
        const Joined* data;
        std::vector<uint32_t> index;  // count + 1 entries
        uint64_t first = 0, count = 0;
    };
    virtual bool Decode(const Run& run, uint64_t sector, const uint8_t* src, uint32_t n, uint8_t* out) const = 0;

    uint64_t Position(const Run& run, uint64_t sector) const {
        return uint64_t(run.index[size_t(sector - run.first)] & 0x7FFFFFFFu) << align_;
    }
    bool Flag(const Run& run, uint64_t sector) const { return run.index[size_t(sector - run.first)] & 0x80000000u; }
    const Run& RunOf(uint64_t sector) const {
        size_t i = 0;
        while (i + 1 < runs_.size() && sector >= runs_[i + 1].first) i++;
        return runs_[i];
    }
    bool ReadIndex(Run& run, uint64_t offset, std::string& error) {
        std::vector<uint8_t> raw(size_t(run.count + 1) * 4);
        if (run.data->Read(offset, raw.data(), uint32_t(raw.size())) != raw.size()) {
            error = "its index is cut short (a part missing?)";
            return false;
        }
        run.index.resize(size_t(run.count + 1));
        for (size_t i = 0; i < run.index.size(); i++) run.index[i] = Le32(&raw[i * 4]);
        return true;
    }

    std::vector<Run> runs_;
    uint64_t size_ = 0;
    uint32_t block_ = kSector;
    uint32_t align_ = 0;
};

// CCI: header "CCIM", header size 32, image size, index offset, block size 2048, version 1, index alignment 2.
// A flagged sector, or one not 2048 bytes long, is LZ4: a byte with the padding after it, the block, the padding.
class CciImage final : public SectorImage {
public:
    bool Open(const std::vector<fs::path>& parts, std::string& error) {
        for (const fs::path& p : parts) {
            auto f = std::make_unique<Joined>();
            if (!f->Open({p}, error)) return false;
            uint8_t h[32];
            if (f->Read(0, h, 32) != 32 || std::memcmp(h, "CCIM", 4) != 0) {
                error = p.filename().string() + " is not a CCI part";
                return false;
            }
            uint64_t size = Le64(h + 8), index = Le64(h + 16);
            if (Le32(h + 4) != 32 || Le32(h + 24) != kSector || h[28] != 1 || h[29] > 16) {
                error = "its CCI header is of an unknown kind";
                return false;
            }
            align_ = h[29];
            Run run{f.get(), {}, size_ / kSector, size / kSector};
            if (!ReadIndex(run, index, error)) return false;
            size_ += run.count * kSector;
            runs_.push_back(std::move(run));
            files_.push_back(std::move(f));
        }
        kind_ = Parts("CCI", parts.size());
        return true;
    }

private:
    bool Decode(const Run& run, uint64_t sector, const uint8_t* src, uint32_t n, uint8_t* out) const override {
        if (n == kSector && !Flag(run, sector)) {
            std::memcpy(out, src, kSector);
            return true;
        }
        if (n < 1 || src[0] + 1u > n) return false;
        return Lz4Decode(src + 1, n - (src[0] + 1u), out, kSector) == int(kSector);
    }

    std::vector<std::unique_ptr<Joined>> files_;
};

// CSO version 2: header "CISO", header size 24, image size, block size, version 2, index alignment, then the index.
// A flagged sector is the data block of an LZ4 frame: its 32-bit size (bit 31: stored) and the LZ4 block.
class CsoImage final : public SectorImage {
public:
    bool Open(const std::vector<fs::path>& parts, std::string& error) {
        if (!data_.Open(parts, error)) return false;
        uint8_t h[24];
        if (data_.Read(0, h, 24) != 24 || std::memcmp(h, "CISO", 4) != 0) {
            error = "it is not a CSO";
            return false;
        }
        if (h[20] != 2) {
            error = "it is a version " + std::to_string(h[20]) +
                    " CSO (zlib, as for the PSP); only version 2 (LZ4, as Repackinator and ciso make for the Xbox) is read";
            return false;
        }
        block_ = Le32(h + 16);
        if (Le32(h + 4) != 24 || block_ < 512 || block_ > (1u << 16) || (block_ & (block_ - 1)) || h[21] > 16) {
            error = "its CSO header is of an unknown kind";
            return false;
        }
        align_ = h[21];
        Run run{&data_, {}, 0, Le64(h + 8) / block_};
        if (!ReadIndex(run, 24, error)) return false;
        size_ = run.count * block_;
        runs_.push_back(std::move(run));
        kind_ = Parts("CSO", parts.size());
        return true;
    }

private:
    bool Decode(const Run& run, uint64_t sector, const uint8_t* src, uint32_t n, uint8_t* out) const override {
        if (!Flag(run, sector)) {
            if (n < block_) return false;
            std::memcpy(out, src, block_);
            return true;
        }
        if (n < 4) return false;
        uint32_t size = Le32(src);
        if (size & 0x80000000u) {  // a block the frame keeps as it is
            size &= 0x7FFFFFFFu;
            if (size != block_ || size + 4 > n) return false;
            std::memcpy(out, src + 4, block_);
            return true;
        }
        if (size + 4 > n) return false;
        return Lz4Decode(src + 4, size, out, block_) == int(block_);
    }

    Joined data_;
};

}  // namespace

int Lz4Decode(const uint8_t* src, size_t n, uint8_t* dst, size_t cap) {
    const uint8_t* ip = src;
    const uint8_t* const end = src + n;
    uint8_t* op = dst;
    uint8_t* const top = dst + cap;
    auto length = [&](size_t len, bool& ok) {
        if (len != 15) return len;
        uint8_t b;
        do {
            if (ip >= end) {
                ok = false;
                return len;
            }
            b = *ip++;
            len += b;
        } while (b == 255);
        return len;
    };
    while (ip < end) {
        uint8_t token = *ip++;
        bool ok = true;
        size_t lit = length(token >> 4, ok);
        if (!ok || lit > size_t(end - ip) || lit > size_t(top - op)) return -1;
        std::memcpy(op, ip, lit);
        ip += lit;
        op += lit;
        if (ip >= end) break;  // the last sequence holds literals only
        if (end - ip < 2) return -1;
        size_t offset = size_t(ip[0]) | size_t(ip[1]) << 8;
        ip += 2;
        if (offset == 0 || offset > size_t(op - dst)) return -1;
        size_t match = length(token & 15, ok) + 4;
        if (!ok || match > size_t(top - op)) return -1;
        const uint8_t* from = op - offset;
        if (offset >= match) {
            std::memcpy(op, from, match);
            op += match;
        } else {
            while (match--) *op++ = *from++;  // overlapping: a repeat of the last offset bytes
        }
    }
    return int(op - dst);
}

std::vector<fs::path> ImageParts(const fs::path& path) {
    // name.N.ext: N is the part
    std::string file = path.filename().string(), ext = path.extension().string();
    std::string stem = file.substr(0, file.size() - ext.size());
    size_t dot = stem.rfind('.');
    std::string base;
    if (dot != std::string::npos && dot + 1 < stem.size() &&
        std::all_of(stem.begin() + long(dot) + 1, stem.end(), [](char c) { return std::isdigit(uint8_t(c)); }))
        base = stem.substr(0, dot);
    std::error_code ec;
    fs::path dir = path.parent_path();
    // the folder's names in lower case: the parts are matched in any case
    std::map<std::string, fs::path> names;
    for (auto& e : fs::directory_iterator(dir.empty() ? fs::path(".") : dir, ec)) names[Lower(e.path().filename().string())] = e.path();
    if (base.empty()) {
        // name.ext alone, unless name.1.ext is beside it
        if (!names.count(Lower(stem + ".1" + ext))) return {path};
        base = stem;
    }
    std::vector<fs::path> parts;
    for (int i = 1; i < 100; i++) {
        auto it = names.find(Lower(base + "." + std::to_string(i) + ext));
        if (it == names.end()) break;
        parts.push_back(it->second);
    }
    if (parts.empty()) return {path};
    return parts;
}

std::unique_ptr<Image> OpenImage(const fs::path& path, std::string& error) {
    std::vector<fs::path> parts = ImageParts(path);
    uint8_t magic[8] = {};
    {
        HostFile f;
        if (!f.Open(parts[0])) {
            error = "cannot open " + parts[0].filename().string();
            return nullptr;
        }
        if (f.ReadAt(0, magic, 8) != 8) {
            error = "it is too short for a disc image";
            return nullptr;
        }
    }
    // archives are a common mistake: name them
    if (!std::memcmp(magic, "PK\x03\x04", 4) || !std::memcmp(magic, "Rar!", 4) || !std::memcmp(magic, "7z\xBC\xAF\x27\x1C", 6)) {
        error = "it is a compressed archive (zip, rar or 7z): extract the disc image or the game's folder from it first";
        return nullptr;
    }
    if (!std::memcmp(magic, "CCIM", 4)) {
        auto img = std::make_unique<CciImage>();
        if (img->Open(parts, error)) return img;
        return nullptr;
    }
    if (!std::memcmp(magic, "CISO", 4)) {
        auto img = std::make_unique<CsoImage>();
        if (img->Open(parts, error)) return img;
        return nullptr;
    }
    auto img = std::make_unique<PlainImage>();
    if (img->Open(parts, error)) return img;
    return nullptr;
}

}  // namespace kernel
