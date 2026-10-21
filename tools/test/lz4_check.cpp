#include <cstdio>
#include <cstring>
#include <fstream>
#include <vector>
#include "kernel/image.h"
int main(int, char** argv) {
    std::ifstream f(argv[1], std::ios::binary);
    std::vector<uint8_t> d((std::istreambuf_iterator<char>(f)), {});
    size_t at = 0, n = 0, bad = 0;
    while (at + 8 <= d.size()) {
        uint32_t raw, len;
        std::memcpy(&raw, &d[at], 4); std::memcpy(&len, &d[at + 4], 4);
        const uint8_t* block = &d[at + 8]; const uint8_t* want = block + len;
        std::vector<uint8_t> out(raw + 16, 0xEE);
        int got = kernel::Lz4Decode(block, len, out.data(), raw);
        if (got != int(raw) || std::memcmp(out.data(), want, raw)) bad++;
        // a cut block must fail or stop short, never write past the end
        int cut = kernel::Lz4Decode(block, len / 2, out.data(), raw);
        if (cut == int(raw) && len > 8) bad++;
        for (size_t i = raw; i < raw + 16; i++) if (out[i] != 0xEE) bad++;
        at += 8 + len + raw; n++;
    }
    std::printf("%s: %zu blocks, %zu bad\n", bad ? "FAIL" : "OK", n, bad);
    return bad != 0;
}
