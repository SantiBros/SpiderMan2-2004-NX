#pragma once
// The containers an Xbox disc image comes in, each read as the plain image it holds:
//   - a plain image (.iso, .xiso), in one file or in parts for FAT32's 4 GB limit: name.1.iso, name.2.iso, ...
//   - CCI (Cerbios, Repackinator): 2048-byte sectors, each LZ4 compressed or stored; a split one is several whole
//     CCI files, name.1.cci, name.2.cci, ..., whose sectors follow one another
//   - CSO version 2 (StellarOS, Repackinator, antangelo/ciso): 2048-byte sectors, LZ4; a split one is one CSO
//     file cut into name.1.cso, name.2.cso, ...
// What the image holds (the game partition alone, or a whole disc with the video partition first) is Disc's.
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace kernel {

class Image {
public:
    virtual ~Image() = default;
    virtual uint64_t Size() const = 0;
    // bytes at a position, from any thread; the count read, short at the end or on an error
    virtual uint32_t Read(uint64_t at, void* out, uint32_t len) const = 0;
    // "ISO", "CCI in 2 parts", for the log and the screen
    const std::string& Kind() const { return kind_; }

protected:
    std::string kind_;
};

// the parts of a split image, name.1.ext, name.2.ext, ... from the first on, when the path names one of them (or
// name.ext with name.1.ext beside it); else the file alone
std::vector<std::filesystem::path> ImageParts(const std::filesystem::path& path);

// the container by its first bytes; null with the reason when the file cannot be read as one
std::unique_ptr<Image> OpenImage(const std::filesystem::path& path, std::string& error);

// an LZ4 block (no frame) into at most cap bytes: the bytes written, -1 for a malformed block
int Lz4Decode(const uint8_t* src, size_t n, uint8_t* dst, size_t cap);

}  // namespace kernel
