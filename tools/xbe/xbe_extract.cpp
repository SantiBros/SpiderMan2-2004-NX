// xbe_extract <disc image> <out.xbe>: default.xbe copied out of an Xbox disc image, through the reader the Switch
// build uses (sdk/runtime/kernel/disc.cpp, image.cpp): XISO or whole disc, plain, split, CCI or CSO.
// Prints the image's kind; exit code 1 with the reason when the file is no usable image.
#include <cstdio>
#include <fstream>
#include <vector>

#include "kernel/disc.h"

int main(int argc, char** argv) {
    if (argc != 3) {
        std::fprintf(stderr, "usage: %s <disc image> <out.xbe>\n", argv[0]);
        return 2;
    }
    kernel::Disc disc;
    std::string error;
    if (!disc.Open(argv[1], error)) {
        std::fprintf(stderr, "%s: %s\n", argv[1], error.c_str());
        return 1;
    }
    std::vector<uint8_t> xbe;
    if (!disc.Load("default.xbe", xbe)) {
        std::fprintf(stderr, "%s: no default.xbe on it\n", argv[1]);
        return 1;
    }
    std::ofstream out(argv[2], std::ios::binary);
    if (!out.write(reinterpret_cast<const char*>(xbe.data()), std::streamsize(xbe.size()))) {
        std::fprintf(stderr, "%s: cannot write it\n", argv[2]);
        return 1;
    }
    std::printf("%s (%s): default.xbe, %zu bytes\n", argv[1], disc.Kind().c_str(), xbe.size());
    return 0;
}
