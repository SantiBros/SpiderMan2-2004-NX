#include "switch/update.h"

#include <arpa/inet.h>
#include <netdb.h>
#include <sys/socket.h>
#include <switch.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <thread>

#include "core/log.h"
#include "switch/update_core.h"
#include "xbe/code.h"

#ifndef SM2NX_VERSION
#define SM2NX_VERSION "dev"
#endif

namespace sw::update {

namespace {

constexpr const char* kReleases = "https://api.github.com/repos/SantiBros/SpiderMan2-2004-NX/releases?per_page=30";
constexpr const char* kUserAgent = "User-Agent: SpiderMan2Xbox/" SM2NX_VERSION " (Nintendo Switch homebrew)";

std::mutex g_lock;
State g_state = State::Off;
std::string g_row = "Not available", g_help;
Release g_found;
std::atomic<uint32_t> g_changes{0};
std::atomic<bool> g_busy{false}, g_restart{false};
uint8_t g_key[32] = {}, g_sha1[20] = {};
bool g_have_game = false;
std::string g_nro;

void Set(State s, const std::string& row, const std::string& help) {
    {
        std::lock_guard<std::mutex> lock(g_lock);
        g_state = s;
        g_row = row;
        g_help = help;
    }
    g_changes++;
}

std::string Mb(uint64_t bytes) {
    char b[32];
    std::snprintf(b, sizeof b, "%.1f MB", bytes / 1048576.0);
    return b;
}

// a TLS connection through the Switch's own ssl service: its trusted certificates and host name check
class Tls : public Stream {
public:
    ~Tls() override {
        if (conn_ok_) {
            if (ssl_fd_ >= 0) close(ssl_fd_);
            sslConnectionClose(&conn_);
        }
        if (ctx_ok_) sslContextClose(&ctx_);
        if (fd_ >= 0) close(fd_);
    }
    bool Open(const std::string& host, std::string& error) {
        addrinfo hints{}, *res = nullptr;
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_STREAM;
        if (getaddrinfo(host.c_str(), "443", &hints, &res) || !res) {
            error = "could not find " + host + " (is the Switch online?)";
            return false;
        }
        fd_ = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
        bool connected = fd_ >= 0 && connect(fd_, res->ai_addr, res->ai_addrlen) == 0;
        freeaddrinfo(res);
        if (!connected) {
            error = "could not connect to " + host;
            return false;
        }
        Result rc = sslCreateContext(&ctx_, SslVersion_Auto);
        ctx_ok_ = R_SUCCEEDED(rc);
        if (ctx_ok_) {
            rc = sslContextCreateConnection(&ctx_, &conn_);
            conn_ok_ = R_SUCCEEDED(rc);
        }
        if (!conn_ok_) {
            error = "the Switch's TLS could not start (" + std::to_string(R_DESCRIPTION(rc)) + ")";
            return false;
        }
        ssl_fd_ = socketSslConnectionSetSocketDescriptor(&conn_, fd_);
        if (ssl_fd_ < 0 && errno != ENOENT) {
            error = "the Switch's TLS could not take the connection";
            return false;
        }
        rc = sslConnectionSetHostName(&conn_, host.c_str(), uint32_t(host.size()));
        if (R_SUCCEEDED(rc)) rc = sslConnectionDoHandshake(&conn_, nullptr, nullptr, nullptr, 0);
        if (R_FAILED(rc)) {
            error = "no secure connection to " + host + " (" + std::to_string(R_MODULE(rc)) + "-" +
                    std::to_string(R_DESCRIPTION(rc)) + "): is the console's date right?";
            return false;
        }
        return true;
    }
    long Read(void* p, size_t n) override {
        u32 got = 0;
        Result rc = sslConnectionRead(&conn_, p, uint32_t(std::min<size_t>(n, 0x10000)), &got);
        if (R_FAILED(rc)) return got ? long(got) : -1;
        return long(got);
    }
    bool Write(const void* p, size_t n) override {
        const uint8_t* b = static_cast<const uint8_t*>(p);
        while (n) {
            u32 put = 0;
            if (R_FAILED(sslConnectionWrite(&conn_, b, uint32_t(n), &put)) || !put) return false;
            b += put;
            n -= put;
        }
        return true;
    }

private:
    int fd_ = -1, ssl_fd_ = -1;
    SslContext ctx_{};
    SslConnection conn_{};
    bool ctx_ok_ = false, conn_ok_ = false;
};

std::unique_ptr<Stream> ConnectTls(const std::string& host, std::string& error) {
    auto t = std::make_unique<Tls>();
    if (!t->Open(host, error)) return nullptr;
    return t;
}

// the network, the sockets and TLS for one check or download
struct Net {
    bool ok = false, nifm = false, sockets = false, ssl = false;
    std::string error;
    Net() {
        nifm = R_SUCCEEDED(nifmInitialize(NifmServiceType_User));
        NifmInternetConnectionType type;
        u32 strength = 0;
        NifmInternetConnectionStatus status = NifmInternetConnectionStatus(0);
        if (!nifm || R_FAILED(nifmGetInternetConnectionStatus(&type, &strength, &status)) ||
            status != NifmInternetConnectionStatus_Connected) {
            error = "The Switch isn't connected to the internet.";
            return;
        }
        sockets = R_SUCCEEDED(socketInitializeDefault());
        ssl = sockets && R_SUCCEEDED(sslInitialize(3));
        if (!ssl) {
            error = "The Switch's network could not start here.";
            return;
        }
        ok = true;
    }
    ~Net() {
        if (ssl) sslExit();
        if (sockets) socketExit();
        if (nifm) nifmExit();
    }
};

void Check() {
    Set(State::Checking, "Checking...", "Asking GitHub for the newest release.");
    Net net;
    if (!net.ok) {
        Set(State::Failed, "Failed: A to try again", net.error);
        return;
    }
    std::string json, error;
    bool ok = Get(ConnectTls, kReleases, {kUserAgent, "Accept: application/vnd.github+json"},
                  [&](const uint8_t* p, size_t n) {
                      json.append(reinterpret_cast<const char*>(p), n);
                      return json.size() < (8u << 20);
                  },
                  nullptr, error);
    Release r;
    if (!ok || !NewestRelease(json, r)) {
        LOGW("update: the check failed: %s", ok ? "no release with a locked .nro" : error.c_str());
        Set(State::Failed, "Failed: A to try again", ok ? "GitHub listed no release with a locked .nro." : "GitHub could not be asked: " + error + ".");
        return;
    }
    LOGI("update: the newest release is %s (%llu bytes), this is %s", r.version.c_str(), (unsigned long long)r.size, SM2NX_VERSION);
    if (CompareVersions(r.version, SM2NX_VERSION) <= 0) {
        Set(State::UpToDate, "Up to date (" + std::string(SM2NX_VERSION) + ")", "This is the newest version: " + std::string(SM2NX_VERSION) + ".");
        return;
    }
    {
        std::lock_guard<std::mutex> lock(g_lock);
        g_found = r;
    }
    Set(State::Available, r.version + ": A to install",
        "Version " + r.version + " is out (" + (r.size ? Mb(r.size) : std::string("about 34 MB")) +
            "). A downloads it from its GitHub release and installs it; your saves and settings stay.");
}

void Install() {
    Release r;
    {
        std::lock_guard<std::mutex> lock(g_lock);
        r = g_found;
    }
    Set(State::Downloading, "Downloading...", "Downloading " + r.version + " from GitHub.");
    Net net;
    if (!net.ok) {
        Set(State::Available, r.version + ": A to install", net.error);
        return;
    }
    const std::string next = g_nro + ".new", old = g_nro + ".old";
    FILE* f = std::fopen(next.c_str(), "wb");
    if (!f) {
        Set(State::Failed, "Failed: A to try again", "Could not write " + next + ".");
        return;
    }
    Aes256Context aes;
    aes256ContextCreate(&aes, g_key, true);
    Unlocker unlock([&](uint8_t* o, const uint8_t* i) { aes256EncryptBlock(&aes, o, i); }, g_sha1);
    std::vector<uint8_t> out;
    std::string error;
    bool written = true;
    int shown = -1;
    bool ok = Get(ConnectTls, r.url, {kUserAgent, "Accept: application/octet-stream"},
                  [&](const uint8_t* p, size_t n) {
                      out.clear();
                      if (!unlock.Feed(p, n, out, error)) return false;
                      written = written && std::fwrite(out.data(), 1, out.size(), f) == out.size();
                      return written;
                  },
                  [&](uint64_t got, int64_t total) {
                      int pct = total > 0 ? int(got * 100 / uint64_t(total)) : -1;
                      if (pct == shown) return;
                      shown = pct;
                      Set(State::Downloading, pct >= 0 ? "Downloading " + std::to_string(pct) + "%" : "Downloading " + Mb(got),
                          "Downloading " + r.version + " from GitHub and unlocking it with your game's default.xbe.");
                  },
                  error);
    out.clear();
    if (ok) ok = unlock.Finish(out, error);
    if (ok) written = written && std::fwrite(out.data(), 1, out.size(), f) == out.size();
    written = std::fclose(f) == 0 && written;
    if (ok && !written) error = "the SD card could not take the new .nro (is it full?)";
    if (!ok || !written) {
        std::remove(next.c_str());
        LOGW("update: %s not installed: %s", r.version.c_str(), error.c_str());
        Set(State::Failed, "Failed: A to try again", "The update stopped: " + error + ". Nothing was changed.");
        return;
    }
    // this .nro becomes .nro.old, the new one takes its name; put back if that fails
    std::remove(old.c_str());
    if (std::rename(g_nro.c_str(), old.c_str()) || std::rename(next.c_str(), g_nro.c_str())) {
        std::rename(old.c_str(), g_nro.c_str());
        std::remove(next.c_str());
        Set(State::Failed, "Failed: A to try again", "The new .nro could not take the old one's place. Nothing was changed.");
        return;
    }
    LOGI("update: %s installed in %s (%llu bytes), the old one kept as %s", r.version.c_str(), g_nro.c_str(),
         (unsigned long long)unlock.NroSize(), old.c_str());
    Set(State::Installed, r.version + " installed: A to restart",
        r.version + " is installed. A quits the port and starts the new version: what you played since your last save "
                    "is lost, as when you quit. The old version is kept as SpiderMan2Xbox.nro.old.");
}

void Run(void (*job)()) {
    if (g_busy.exchange(true)) return;
    std::thread([job] {
        job();
        g_busy = false;
    }).detach();
}

}  // namespace

void SetGame(const std::vector<uint8_t>& xbe, bool supported) {
    std::vector<uint8_t> code;
    if (!supported || !xbe::CodeBytes(xbe, code)) {
        Set(State::Off, "Not available", "Updates unlock with the supported game code, and this game is another release.");
        return;
    }
    static const char label[] = "SpiderMan2-2004-NX lock 2";  // and its NUL, as tools/lock_nro.py
    Sha256Context sha;
    sha256ContextCreate(&sha);
    sha256ContextUpdate(&sha, label, sizeof label);
    sha256ContextUpdate(&sha, code.data(), code.size());
    sha256ContextGetHash(&sha, g_key);
    sha1CalculateHash(g_sha1, code.data(), code.size());
    g_have_game = true;
    if (!g_nro.empty()) Set(State::Idle, "Check now", "");
}

void SetNro(const std::string& path) {
    if (path.size() < 4 || path.compare(path.size() - 4, 4, ".nro")) {
        Set(State::Off, "Not available", "The port wasn't started from its .nro, so it can't replace it.");
        return;
    }
    g_nro = path;
    if (g_have_game) Set(State::Idle, "Check now", "");
}

State Current() {
    std::lock_guard<std::mutex> lock(g_lock);
    return g_state;
}
std::string RowText() {
    std::lock_guard<std::mutex> lock(g_lock);
    return g_row;
}
std::string HelpText() {
    std::lock_guard<std::mutex> lock(g_lock);
    return g_help;
}
uint32_t Changes() { return g_changes.load(); }

void Act() {
    switch (Current()) {
        case State::Idle:
        case State::UpToDate:
        case State::Failed:
            Run(Check);
            break;
        case State::Available:
            Run(Install);
            break;
        case State::Installed:
            g_restart = true;
            break;
        default:
            break;
    }
}

bool RestartRequested() { return g_restart.load(); }

void StartNext() {
    if (!g_restart || g_nro.empty() || !envHasNextLoad()) return;
    std::string args = "\"" + g_nro + "\"";
    Result rc = envSetNextLoad(g_nro.c_str(), args.c_str());
    LOGI("update: starting %s next (%s)", g_nro.c_str(), R_SUCCEEDED(rc) ? "set" : "refused");
}

}  // namespace sw::update
