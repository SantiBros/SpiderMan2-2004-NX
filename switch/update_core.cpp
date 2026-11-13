#include "switch/update_core.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>

namespace sw::update {

namespace {

// a stream read ahead: lines for the headers and chunk sizes, the rest as it comes
class Reader {
public:
    Reader(Stream& s, std::string first) : s_(s), buf_(std::move(first)) {}
    // up to n bytes: 0 at the end
    long Some(uint8_t* out, size_t n) {
        if (pos_ == buf_.size() && !Fill()) return eof_ ? 0 : -1;
        size_t k = std::min(n, buf_.size() - pos_);
        std::memcpy(out, buf_.data() + pos_, k);
        pos_ += k;
        return long(k);
    }
    bool Line(std::string& line) {
        for (;;) {
            size_t nl = buf_.find("\r\n", pos_);
            if (nl != std::string::npos) {
                line = buf_.substr(pos_, nl - pos_);
                pos_ = nl + 2;
                return true;
            }
            if (buf_.size() - pos_ > 4096 || !Fill()) return false;
        }
    }

private:
    bool Fill() {
        if (pos_) {
            buf_.erase(0, pos_);
            pos_ = 0;
        }
        char b[16384];
        long n = s_.Read(b, sizeof b);
        if (n <= 0) {
            eof_ = n == 0;
            return false;
        }
        buf_.append(b, size_t(n));
        return true;
    }
    Stream& s_;
    std::string buf_;
    size_t pos_ = 0;
    bool eof_ = false;
};

std::string Lower(std::string s) {
    for (char& c : s) c = char(std::tolower(uint8_t(c)));
    return s;
}

uint64_t Be64(const uint8_t* p) {
    uint64_t v = 0;
    for (int i = 0; i < 8; i++) v = v << 8 | p[i];
    return v;
}
void PutBe64(uint8_t* p, uint64_t v) {
    for (int i = 7; i >= 0; i--, v >>= 8) p[i] = uint8_t(v);
}

}  // namespace

bool Get(const Connector& connect, const std::string& url, const std::vector<std::string>& headers,
         const std::function<bool(const uint8_t*, size_t)>& sink, const std::function<void(uint64_t, int64_t)>& progress,
         std::string& error) {
    std::string at = url;
    for (int hop = 0; hop < 6; hop++) {
        const std::string scheme = "https://";
        if (at.compare(0, scheme.size(), scheme)) {
            error = "not an https address: " + at;
            return false;
        }
        size_t slash = at.find('/', scheme.size());
        std::string host = at.substr(scheme.size(), slash == std::string::npos ? std::string::npos : slash - scheme.size());
        std::string path = slash == std::string::npos ? "/" : at.substr(slash);
        std::unique_ptr<Stream> s = connect(host, error);
        if (!s) return false;
        std::string req = "GET " + path + " HTTP/1.1\r\nHost: " + host + "\r\n";
        for (const std::string& h : headers) req += h + "\r\n";
        req += "Connection: close\r\n\r\n";
        if (!s->Write(req.data(), req.size())) {
            error = "could not send the request to " + host;
            return false;
        }
        Reader r(*s, std::string());
        std::string line;
        if (!r.Line(line) || line.compare(0, 5, "HTTP/")) {
            error = host + " gave no answer";
            return false;
        }
        size_t sp = line.find(' ');
        int status = sp == std::string::npos ? 0 : std::atoi(line.c_str() + sp + 1);
        std::string location;
        int64_t length = -1;
        bool chunked = false;
        for (;;) {
            if (!r.Line(line)) {
                error = host + "'s answer was cut off";
                return false;
            }
            if (line.empty()) break;
            size_t colon = line.find(':');
            if (colon == std::string::npos) continue;
            std::string name = Lower(line.substr(0, colon)), value = line.substr(colon + 1);
            value.erase(0, value.find_first_not_of(" \t"));
            if (name == "location") location = value;
            else if (name == "content-length") length = std::strtoll(value.c_str(), nullptr, 10);
            else if (name == "transfer-encoding") chunked = Lower(value).find("chunked") != std::string::npos;
        }
        if (status == 301 || status == 302 || status == 303 || status == 307 || status == 308) {
            if (location.empty()) {
                error = host + " redirected nowhere";
                return false;
            }
            at = location[0] == '/' ? scheme + host + location : location;
            continue;
        }
        if (status != 200) {
            error = host + " answered " + std::to_string(status);
            return false;
        }
        uint64_t got = 0;
        uint8_t b[16384];
        auto take = [&](uint64_t want) {  // want bytes of the body, or to the end when ~0
            while (want) {
                long n = r.Some(b, size_t(std::min<uint64_t>(want, sizeof b)));
                if (n == 0 && want == ~0ull) return true;
                if (n <= 0) {
                    error = "the download from " + host + " broke off";
                    return false;
                }
                if (!sink(b, size_t(n))) {
                    if (error.empty()) error = "the download was stopped";
                    return false;
                }
                got += uint64_t(n);
                if (want != ~0ull) want -= uint64_t(n);
                if (progress) progress(got, length);
            }
            return true;
        };
        if (!chunked) return take(length >= 0 ? uint64_t(length) : ~0ull);
        for (;;) {
            if (!r.Line(line)) {
                error = "the download from " + host + " broke off";
                return false;
            }
            uint64_t size = std::strtoull(line.c_str(), nullptr, 16);
            if (!size) return true;  // the trailer isn't needed: the connection closes
            if (!take(size) || !r.Line(line)) return false;
        }
    }
    error = "too many redirects";
    return false;
}

int CompareVersions(const std::string& a, const std::string& b) {
    size_t i = 0, j = 0;
    while (i < a.size() || j < b.size()) {
        long x = 0, y = 0;
        while (i < a.size() && std::isdigit(uint8_t(a[i]))) x = x * 10 + (a[i++] - '0');
        while (j < b.size() && std::isdigit(uint8_t(b[j]))) y = y * 10 + (b[j++] - '0');
        if (x != y) return x < y ? -1 : 1;
        while (i < a.size() && !std::isdigit(uint8_t(a[i]))) i++;
        while (j < b.size() && !std::isdigit(uint8_t(b[j]))) j++;
    }
    return 0;
}

bool NewestRelease(const std::string& json, Release& out) {
    const std::string key = "\"browser_download_url\"", prefix = "SpiderMan2Xbox-", suffix = ".nro.locked";
    bool found = false;
    for (size_t at = json.find(key); at != std::string::npos; at = json.find(key, at + key.size())) {
        size_t q0 = json.find('"', json.find(':', at + key.size()));
        size_t q1 = q0 == std::string::npos ? q0 : json.find('"', q0 + 1);
        if (q1 == std::string::npos) break;
        std::string url = json.substr(q0 + 1, q1 - q0 - 1);
        size_t name = url.rfind('/');
        if (name == std::string::npos || url.size() < suffix.size() || url.compare(url.size() - suffix.size(), suffix.size(), suffix))
            continue;
        name++;
        if (url.compare(name, prefix.size(), prefix)) continue;
        std::string version = url.substr(name + prefix.size(), url.size() - suffix.size() - name - prefix.size());
        if (version.empty() || !std::isdigit(uint8_t(version[0]))) continue;
        if (found && CompareVersions(version, out.version) <= 0) continue;
        out.version = version;
        out.url = url;
        out.size = 0;
        // the asset's "size", before its link in the same object
        size_t obj = json.rfind("\"name\"", at), sz = json.rfind("\"size\"", at);
        if (sz != std::string::npos && (obj == std::string::npos || sz > obj))
            out.size = std::strtoull(json.c_str() + json.find(':', sz) + 1, nullptr, 10);
        found = true;
    }
    return found;
}

Unlocker::Unlocker(std::function<void(uint8_t*, const uint8_t*)> encrypt, const uint8_t sha1[20]) : encrypt_(std::move(encrypt)) {
    std::memcpy(sha1_, sha1, 20);
}

void Unlocker::Begin() {
    // H, and its multiples by 0..15 (as in mbed TLS's GCM)
    uint8_t zero[16] = {}, h[16];
    encrypt_(h, zero);
    uint64_t vh = Be64(h), vl = Be64(h + 8);
    hl_[8] = vl;
    hh_[8] = vh;
    for (int i = 4; i > 0; i >>= 1) {
        uint64_t t = (vl & 1) ? 0xE1000000ull << 32 : 0;
        vl = (vh << 63) | (vl >> 1);
        vh = (vh >> 1) ^ t;
        hl_[i] = vl;
        hh_[i] = vh;
    }
    for (int i = 2; i <= 8; i *= 2)
        for (int j = 1; j < i; j++) {
            hh_[i + j] = hh_[i] ^ hh_[j];
            hl_[i + j] = hl_[i] ^ hl_[j];
        }
    // J0 is the nonce and 1; the .nro's first block is encrypted with J0 + 1
    std::memcpy(j0_, header_.data() + 8, 12);
    j0_[15] = 1;
    std::memcpy(ctr_, j0_, 16);
    for (size_t i = 0; i < 64; i += 16) Ghash(header_.data() + i, 16);
    begun_ = true;
}

void Unlocker::Mult(uint8_t x[16]) const {
    static const uint64_t kLast4[16] = {0x0000, 0x1c20, 0x3840, 0x2460, 0x7080, 0x6ca0, 0x48c0, 0x54e0,
                                        0xe100, 0xfd20, 0xd940, 0xc560, 0x9180, 0x8da0, 0xa9c0, 0xb5e0};
    uint8_t lo = x[15] & 0xF;
    uint64_t zh = hh_[lo], zl = hl_[lo];
    for (int i = 15; i >= 0; i--) {
        lo = x[i] & 0xF;
        uint8_t hi = x[i] >> 4, rem;
        if (i != 15) {
            rem = zl & 0xF;
            zl = (zh << 60) | (zl >> 4);
            zh = (zh >> 4) ^ (kLast4[rem] << 48);
            zh ^= hh_[lo];
            zl ^= hl_[lo];
        }
        rem = zl & 0xF;
        zl = (zh << 60) | (zl >> 4);
        zh = (zh >> 4) ^ (kLast4[rem] << 48);
        zh ^= hh_[hi];
        zl ^= hl_[hi];
    }
    PutBe64(x, zh);
    PutBe64(x + 8, zl);
}

void Unlocker::Ghash(const uint8_t* block, size_t n) {
    for (size_t i = 0; i < n; i++) x_[i] ^= block[i];
    Mult(x_);
}

void Unlocker::Crypt(const uint8_t* c, size_t n, std::vector<uint8_t>& out) {
    Ghash(c, n);
    for (int i = 15; i >= 12 && !++ctr_[i]; i--) {
    }
    uint8_t ks[16];
    encrypt_(ks, ctr_);
    size_t at = out.size();
    out.resize(at + n);
    for (size_t i = 0; i < n; i++) out[at + i] = c[i] ^ ks[i];
    done_ += n;
}

bool Unlocker::Feed(const uint8_t* p, size_t n, std::vector<uint8_t>& out, std::string& error) {
    if (!begun_) {
        size_t k = std::min(n, 64 - header_.size());
        header_.insert(header_.end(), p, p + k);
        p += k;
        n -= k;
        if (header_.size() < 64) return true;
        if (!std::memcmp(header_.data(), "SM2NXLK1", 8)) {
            error = "this release is locked the way of 0.3.1 and before: get it from the installer page";
            return false;
        }
        if (std::memcmp(header_.data(), "SM2NXLK2", 8)) {
            error = "the download is not a locked .nro";
            return false;
        }
        if (std::memcmp(header_.data() + 32, sha1_, 20)) {
            error = "this release needs another game code than your game's";
            return false;
        }
        for (int i = 7; i >= 0; i--) size_ = size_ << 8 | header_[24 + size_t(i)];
        Begin();
    }
    pending_.insert(pending_.end(), p, p + n);
    // all but the last 16 bytes, which may be the tag
    if (pending_.size() > 16) {
        size_t whole = (pending_.size() - 16) / 16 * 16;
        out.reserve(out.size() + whole);
        for (size_t i = 0; i < whole; i += 16) Crypt(pending_.data() + i, 16, out);
        pending_.erase(pending_.begin(), pending_.begin() + long(whole));
    }
    if (done_ > size_) {
        error = "the download is longer than its .nro";
        return false;
    }
    return true;
}

bool Unlocker::Finish(std::vector<uint8_t>& out, std::string& error) {
    if (!begun_ || pending_.size() < 16) {
        error = "the download is cut short";
        return false;
    }
    size_t tail = pending_.size() - 16;
    if (tail) Crypt(pending_.data(), tail, out);
    uint8_t lengths[16];
    PutBe64(lengths, 64 * 8);
    PutBe64(lengths + 8, done_ * 8);
    Ghash(lengths, 16);
    uint8_t tag[16];
    encrypt_(tag, j0_);
    uint8_t diff = 0;
    for (int i = 0; i < 16; i++) diff |= uint8_t(tag[i] ^ x_[i] ^ pending_[tail + size_t(i)]);
    if (diff) {
        error = "the download doesn't match its seal: it was damaged on the way, or your default.xbe differs";
        return false;
    }
    if (done_ != size_) {
        error = "the unlocked .nro has the wrong size";
        return false;
    }
    return true;
}

}  // namespace sw::update
