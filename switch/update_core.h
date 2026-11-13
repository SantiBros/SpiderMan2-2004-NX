#pragma once
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

// What the in-app update (switch/update.cpp) does that isn't the Switch's own: HTTP over a byte stream, the newest
// release's locked .nro in GitHub's list of releases, and unlocking that .nro as it downloads. Built into the Switch
// port, and on a computer for its tests (tools/test_update.sh).
namespace sw::update {

// a connection to a host's port 443: the Switch's TLS on the console, the tests' own on a computer
struct Stream {
    virtual ~Stream() = default;
    virtual long Read(void* p, size_t n) = 0;  // bytes read, 0 at the end, below 0 an error
    virtual bool Write(const void* p, size_t n) = 0;
};
using Connector = std::function<std::unique_ptr<Stream>(const std::string& host, std::string& error)>;

// GET an https:// address, following up to 5 redirects. Each header line is sent as given ("User-Agent: ..."). The
// body goes to sink as it comes (false from sink stops it), progress gets the bytes so far and the whole size (-1
// when the server doesn't say).
bool Get(const Connector& connect, const std::string& url, const std::vector<std::string>& headers,
         const std::function<bool(const uint8_t*, size_t)>& sink, const std::function<void(uint64_t, int64_t)>& progress,
         std::string& error);

// "0.3.10" against "0.3.9", part by part: below 0, 0 or above 0
int CompareVersions(const std::string& a, const std::string& b);

// a release that has a locked .nro (SpiderMan2Xbox-<version>.nro.locked)
struct Release {
    std::string version;
    std::string url;  // the file's download link on the release: a download GitHub counts
    uint64_t size = 0;
};
// the newest such release in the JSON of GitHub's list of releases (GET /repos/<owner>/<repo>/releases)
bool NewestRelease(const std::string& json, Release& out);

// A locked .nro of version 2 (tools/lock_nro.py, the releases from 0.4.0) unlocked as its bytes come. AES-256-GCM: a
// 64 byte header ("SM2NXLK2", the 12 byte nonce, 4 zero bytes, the .nro's size, the SHA-1 of the game code it needs,
// 12 zero bytes) that is the authenticated data, then the encrypted .nro and its 16 byte tag. Nothing it gives out is
// to be used before Finish says the tag is right.
class Unlocker {
public:
    // encrypt: AES-256 of one 16 byte block under the key, the SHA-256 of "SpiderMan2-2004-NX lock 2\0" and the
    // default.xbe's game code (xbe/code.h); sha1: that game code's SHA-1, which the header must name
    Unlocker(std::function<void(uint8_t* out, const uint8_t* in)> encrypt, const uint8_t sha1[20]);
    // the locked file's next bytes: the .nro's bytes that are known come out appended to out
    bool Feed(const uint8_t* p, size_t n, std::vector<uint8_t>& out, std::string& error);
    // the file's end: its last bytes to out, the tag and the size checked
    bool Finish(std::vector<uint8_t>& out, std::string& error);
    uint64_t NroSize() const { return size_; }

private:
    void Begin();
    void Crypt(const uint8_t* c, size_t n, std::vector<uint8_t>& out);  // n up to 16
    void Ghash(const uint8_t* block, size_t n);                          // n up to 16, zero padded
    void Mult(uint8_t x[16]) const;                                      // x times H in GF(2^128)

    std::function<void(uint8_t*, const uint8_t*)> encrypt_;
    uint8_t sha1_[20];
    std::vector<uint8_t> header_, pending_;
    bool begun_ = false;
    uint64_t size_ = 0, done_ = 0;  // the .nro's size from the header, the encrypted bytes taken in
    uint8_t j0_[16] = {}, ctr_[16] = {}, x_[16] = {};
    uint64_t hh_[16] = {}, hl_[16] = {};  // H's multiples, 4 bits at a time
};

}  // namespace sw::update
