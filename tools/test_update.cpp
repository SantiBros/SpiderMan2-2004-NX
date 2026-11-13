// The in-app update's own parts (switch/update_core.cpp) on a computer: version order, the newest release in GitHub's
// JSON, HTTP through redirects and chunks, and unlocking a locked .nro as it comes, with OpenSSL standing in for the
// Switch's AES. Run by tools/test_update.sh.
//   test_update <releases.json> <file.nro.locked> <default.xbe> <the .nro> <port of tools/test_update_server.py>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <openssl/evp.h>
#include <openssl/sha.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <random>

#include "switch/update_core.h"
#include "xbe/code.h"

using namespace sw::update;

namespace {

int g_failed = 0;
void Check(bool ok, const std::string& what) {
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", what.c_str());
    if (!ok) g_failed++;
}

std::vector<uint8_t> ReadFile(const char* path) {
    std::ifstream f(path, std::ios::binary);
    return std::vector<uint8_t>(std::istreambuf_iterator<char>(f), {});
}

// plain TCP to the test server, whatever the host
struct Plain : Stream {
    int fd = -1;
    ~Plain() override {
        if (fd >= 0) close(fd);
    }
    long Read(void* p, size_t n) override { return long(::read(fd, p, n)); }
    bool Write(const void* p, size_t n) override { return ::write(fd, p, n) == long(n); }
};

struct Aes {
    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    explicit Aes(const uint8_t key[32]) {
        EVP_EncryptInit_ex(ctx, EVP_aes_256_ecb(), nullptr, key, nullptr);
        EVP_CIPHER_CTX_set_padding(ctx, 0);
    }
    ~Aes() { EVP_CIPHER_CTX_free(ctx); }
    void Block(uint8_t* out, const uint8_t* in) {
        int n = 0;
        EVP_EncryptUpdate(ctx, out, &n, in, 16);
    }
};

// the key and the SHA-1 a locked file of version 2 opens with: from the xbe's game code
void KeyOf(const std::vector<uint8_t>& xbe, uint8_t key[32], uint8_t sha1[20]) {
    static const char label[] = "SpiderMan2-2004-NX lock 2";  // and its NUL: 26 bytes
    std::vector<uint8_t> code;
    xbe::CodeBytes(xbe, code);
    std::vector<uint8_t> in(label, label + sizeof label);
    in.insert(in.end(), code.begin(), code.end());
    SHA256(in.data(), in.size(), key);
    SHA1(code.data(), code.size(), sha1);
}

// the locked file unlocked in pieces of random sizes
bool Unlock(const std::vector<uint8_t>& locked, const std::vector<uint8_t>& xbe, std::vector<uint8_t>& nro, std::string& error,
            unsigned seed) {
    uint8_t key[32], sha1[20];
    KeyOf(xbe, key, sha1);
    Aes aes(key);
    Unlocker u([&](uint8_t* o, const uint8_t* i) { aes.Block(o, i); }, sha1);
    std::mt19937 rng(seed);
    nro.clear();
    for (size_t at = 0; at < locked.size();) {
        size_t n = std::min<size_t>(locked.size() - at, rng() % 40000 + 1);
        if (!u.Feed(locked.data() + at, n, nro, error)) return false;
        at += n;
    }
    return u.Finish(nro, error);
}

}  // namespace

int main(int argc, char** argv) {
    if (argc != 6) {
        std::fprintf(stderr, "usage: test_update <releases.json> <file.nro.locked> <default.xbe> <the .nro> <server port>\n");
        return 2;
    }
    Check(CompareVersions("0.3.10", "0.3.9") > 0, "0.3.10 is newer than 0.3.9");
    Check(CompareVersions("0.3.2", "0.3.2") == 0, "0.3.2 is 0.3.2");
    Check(CompareVersions("0.3", "0.3.0") == 0, "0.3 is 0.3.0");
    Check(CompareVersions("0.2.3", "0.3.0") < 0, "0.2.3 is older than 0.3.0");
    Check(CompareVersions("1.0.0", "0.9.9") > 0, "1.0.0 is newer than 0.9.9");

    std::vector<uint8_t> j = ReadFile(argv[1]);
    std::string json(j.begin(), j.end());
    Release r;
    Check(NewestRelease(json, r), "a release with a locked .nro in GitHub's list");
    std::printf("     newest: %s, %llu bytes, %s\n", r.version.c_str(), (unsigned long long)r.size, r.url.c_str());
    Release s;
    std::string made = R"([{"tag_name":"v0.4.0","assets":[{"name":"SpiderMan2-2004-NX-0.4.0.zip","size":5,"browser_download_url":"https://github.com/x/y/releases/download/v0.4.0/SpiderMan2-2004-NX-0.4.0.zip"}]},)"
                       R"({"assets":[{"name":"SpiderMan2Xbox-0.3.10.nro.locked","size":123,"browser_download_url":"https://github.com/x/y/releases/download/v0.3.10/SpiderMan2Xbox-0.3.10.nro.locked"}]},)"
                       R"({"assets":[{"name":"SpiderMan2Xbox-0.3.9.nro.locked","size":99,"browser_download_url":"https://github.com/x/y/releases/download/v0.3.9/SpiderMan2Xbox-0.3.9.nro.locked"}]}])";
    Check(NewestRelease(made, s) && s.version == "0.3.10" && s.size == 123, "0.3.10 over 0.3.9, a release without a locked .nro skipped");
    Check(!NewestRelease("[]", s), "no release in an empty list");

    std::vector<uint8_t> locked = ReadFile(argv[2]), xbe = ReadFile(argv[3]), want = ReadFile(argv[4]), nro;
    std::string error;
    bool r1 = Unlock(locked, xbe, nro, error, 1) && nro == want;
    Check(r1, "unlocked in random pieces, the same .nro" + (error.empty() ? "" : ": " + error));
    bool r2 = Unlock(locked, xbe, nro, error, 2) && nro == want;
    Check(r2, "again in other pieces");
    std::vector<uint8_t> bad = locked;
    bad[bad.size() / 2] ^= 1;
    error.clear();
    bool r3 = !Unlock(bad, xbe, nro, error, 3);
    Check(r3, "a changed byte is caught: " + error);
    bad = locked;
    bad.back() ^= 0x80;
    error.clear();
    bool r4 = !Unlock(bad, xbe, nro, error, 4);
    Check(r4, "a changed tag is caught: " + error);
    bad.assign(locked.begin(), locked.end() - 1000);
    error.clear();
    bool r5 = !Unlock(bad, xbe, nro, error, 5);
    Check(r5, "a cut file is caught: " + error);
    // another edition: the same game code under another header and certificate
    std::vector<uint8_t> edition = xbe;
    uint32_t cert = 0;
    std::memcpy(&cert, edition.data() + 0x118, 4);
    edition[cert - 0x10000 + 0xA0] ^= 4;                     // its game regions
    std::fill(edition.begin() + 4, edition.begin() + 0x104, 0);  // its signature
    error.clear();
    bool r6 = Unlock(locked, edition, nro, error, 6) && nro == want;
    Check(r6, "another edition of the same game code opens it" + (error.empty() ? "" : ": " + error));
    std::vector<uint8_t> other = xbe;
    other[0x2000] ^= 1;  // in .text
    error.clear();
    bool r9 = !Unlock(locked, other, nro, error, 9);
    Check(r9, "another game code is turned away: " + error);
    {
        std::vector<uint8_t> old = locked;
        std::memcpy(old.data(), "SM2NXLK1", 8);
        error.clear();
        bool r10 = !Unlock(old, xbe, nro, error, 10);
        Check(r10, "a lock of 0.3.1 and before is sent to the installer page: " + error);
    }

    // HTTP: every host is the test server, which redirects, sends chunks and sizes as GitHub's do
    int port = std::atoi(argv[5]);
    Connector local = [port](const std::string&, std::string& err) -> std::unique_ptr<Stream> {
        auto s = std::make_unique<Plain>();
        s->fd = socket(AF_INET, SOCK_STREAM, 0);
        sockaddr_in a{};
        a.sin_family = AF_INET;
        a.sin_port = htons(uint16_t(port));
        a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        if (connect(s->fd, reinterpret_cast<sockaddr*>(&a), sizeof a)) {
            err = "no test server";
            return nullptr;
        }
        return s;
    };
    for (const char* path : {"/sized", "/chunked", "/redirect", "/redirect-relative"}) {
        std::vector<uint8_t> got;
        uint64_t last = 0;
        error.clear();
        bool ok = Get(local, std::string("https://example.test") + path, {"User-Agent: test"},
                      [&](const uint8_t* p, size_t n) {
                          got.insert(got.end(), p, p + n);
                          return true;
                      },
                      [&](uint64_t g, int64_t) { last = g; }, error);
        Check(ok && got == locked && last == locked.size(), std::string("GET ") + path + (error.empty() ? "" : ": " + error));
    }
    error.clear();
    bool r7 = !Get(local, "https://example.test/missing", {}, [](const uint8_t*, size_t) { return true; }, nullptr, error);
    Check(r7, "a 404 is an error: " + error);
    error.clear();
    bool r8 = !Get(local, "https://example.test/loop", {}, [](const uint8_t*, size_t) { return true; }, nullptr, error);
    Check(r8, "endless redirects stop: " + error);
    // the whole way: downloaded through a redirect in chunks and unlocked as it comes
    {
        uint8_t key[32], sha1[20];
        KeyOf(xbe, key, sha1);
        Aes aes(key);
        Unlocker u([&](uint8_t* o, const uint8_t* i) { aes.Block(o, i); }, sha1);
        std::vector<uint8_t> out;
        error.clear();
        bool ok = Get(local, "https://example.test/redirect-chunked", {}, [&](const uint8_t* p, size_t n) { return u.Feed(p, n, out, error); },
                      nullptr, error) &&
                  u.Finish(out, error);
        Check(ok && out == want, "downloaded in chunks through a redirect and unlocked as it came" + (error.empty() ? "" : ": " + error));
    }
    std::printf("%s\n", g_failed ? "SOME TESTS FAILED" : "all tests passed");
    return g_failed ? 1 : 0;
}
