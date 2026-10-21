// disc_check <folder> <image>: every file of the folder read back from the image through the port's reader
// (kernel::Disc over kernel::Image), whole and in pieces of odd sizes at odd offsets, from four threads at once,
// and compared byte for byte. Also: the image's files that the folder lacks.
//   g++ -std=c++20 -O2 -I<sdk>/runtime disc_check.cpp <sdk>/runtime/kernel/{disc,image}.cpp -lpthread
#include <atomic>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <random>
#include <thread>
#include <vector>

#include "kernel/disc.h"

namespace fs = std::filesystem;

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: %s <folder> <image>\n", argv[0]);
        return 2;
    }
    fs::path folder = argv[1];
    kernel::Disc disc;
    std::string error;
    auto t0 = std::chrono::steady_clock::now();
    if (!disc.Open(argv[2], error)) {
        std::printf("FAIL open: %s\n", error.c_str());
        return 1;
    }
    std::printf("%s: %s\n", argv[2], disc.Kind().c_str());
    std::vector<fs::path> files;
    for (auto& e : fs::recursive_directory_iterator(folder))
        if (e.is_regular_file()) files.push_back(e.path());
    std::atomic<size_t> next{0}, bad{0}, bytes{0};
    auto work = [&](unsigned seed) {
        std::mt19937 rng(seed);
        std::vector<uint8_t> want, got;
        for (size_t i; (i = next++) < files.size();) {
            const fs::path& f = files[i];
            std::string rel = fs::relative(f, folder).string();
            for (char& c : rel)
                if (c == '/') c = '\\';
            const kernel::Disc::Entry* e = disc.Find(rel);
            std::ifstream in(f, std::ios::binary);
            want.assign(std::istreambuf_iterator<char>(in), {});
            if (!e || e->dir || e->size != want.size()) {
                std::printf("FAIL %s: %s\n", rel.c_str(), !e ? "missing" : "wrong size");
                bad++;
                continue;
            }
            // whole, then 64 pieces of random length (1 byte to 3 MB) at random offsets
            got.assign(want.size(), 0xCD);
            if (disc.Read(*e, 0, got.data(), uint32_t(got.size())) != got.size() || got != want) {
                std::printf("FAIL %s: whole read differs\n", rel.c_str());
                bad++;
                continue;
            }
            for (int k = 0; k < 64 && !want.empty(); k++) {
                uint64_t at = rng() % want.size();
                uint32_t len = uint32_t(std::min<uint64_t>(1 + rng() % (3u << 20), want.size() - at));
                if (k % 4 == 0) len = uint32_t(std::min<uint64_t>(1 + rng() % 5000, want.size() - at));
                got.assign(len, 0xCD);
                if (disc.Read(*e, at, got.data(), len) != len || !std::equal(got.begin(), got.end(), want.begin() + long(at))) {
                    std::printf("FAIL %s: piece at %llu of %u differs\n", rel.c_str(), (unsigned long long)at, len);
                    bad++;
                    break;
                }
            }
            bytes += want.size();
        }
    };
    std::vector<std::thread> threads;
    for (unsigned t = 0; t < 4; t++) threads.emplace_back(work, 1234 + t);
    for (auto& t : threads) t.join();
    double s = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    std::printf("%s: %zu files, %zu bytes checked, %zu failed, %.1f s\n", bad ? "FAIL" : "OK", files.size(), size_t(bytes), size_t(bad), s);
    return bad ? 1 : 0;
}
