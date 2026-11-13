#pragma once
// An xbe's game code: the same in every release built from the same code, whatever the edition (region, language)
// its header and certificate name. Its SHA-1 tells a release a recompiled build runs, and a locked .nro of version
// 2 (tools/lock_nro.py) is unlocked with it. Self-contained, as tools/lock_nro.py and installer/index.html make the
// same bytes:
//   for each section the title loads (all but those inserted as files, flag 8: the dashboard's images, .XTLID), in the
//   header's order: its address, virtual size and raw size (little endian, 4 bytes each), then its raw bytes;
//   then the entry point and the kernel imports' address (4 bytes each, with the retail keys).
#include <cstdint>
#include <cstring>
#include <vector>

namespace xbe {

inline bool CodeBytes(const std::vector<uint8_t>& f, std::vector<uint8_t>& out) {
    out.clear();
    auto u32 = [&](size_t at, uint32_t& v) {
        if (at + 4 > f.size()) return false;
        std::memcpy(&v, f.data() + at, 4);
        return true;
    };
    auto put = [&](uint32_t v) {
        for (int i = 0; i < 4; i++) out.push_back(uint8_t(v >> (8 * i)));
    };
    uint32_t base, count, headers, entry, thunk;
    if (f.size() < 0x178 || std::memcmp(f.data(), "XBEH", 4) || !u32(0x104, base) || !u32(0x11C, count) ||
        !u32(0x120, headers) || !u32(0x128, entry) || !u32(0x158, thunk) || headers < base || count > 256)
        return false;
    for (uint32_t i = 0; i < count; i++) {
        size_t at = size_t(headers - base) + size_t(i) * 56;
        uint32_t flags, va, vsize, raw, rsize;
        if (!u32(at, flags) || !u32(at + 4, va) || !u32(at + 8, vsize) || !u32(at + 12, raw) || !u32(at + 16, rsize)) return false;
        if (flags & 8) continue;
        if (size_t(raw) + rsize > f.size()) return false;
        put(va);
        put(vsize);
        put(rsize);
        out.insert(out.end(), f.begin() + raw, f.begin() + raw + rsize);
    }
    put(entry ^ 0xA8FC57ABu);
    put(thunk ^ 0x5B6D40B6u);
    return true;
}

}  // namespace xbe
