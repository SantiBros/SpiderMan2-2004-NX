#include "switch/settings.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <sstream>

#include "core/log.h"

namespace sw {

namespace {

Settings g_now, g_start;
constexpr const char* kFile = "settings.ini";

std::string Trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r"), b = s.find_last_not_of(" \t\r");
    return a == std::string::npos ? std::string() : s.substr(a, b - a + 1);
}

struct Lang {
    uint32_t code;
    const char* key;
};
const Lang kLanguages[] = {{0, "auto"}, {1, "en"}, {3, "de"}, {4, "fr"}, {5, "es"}, {6, "it"}};

// "stock" or MHz ("1785", "307.2")
uint32_t Hz(const std::string& v) {
    if (v.empty() || v == "stock") return 0;
    double mhz = std::atof(v.c_str());
    return mhz > 0 && mhz < 5000 ? uint32_t(mhz * 1e6 + 0.5) : 0;
}
std::string Mhz(uint32_t hz) {
    if (!hz) return "stock";
    char b[24];
    if (hz % 1000000)
        std::snprintf(b, sizeof b, "%.1f", hz / 1e6);
    else
        std::snprintf(b, sizeof b, "%u", hz / 1000000);
    return b;
}
bool On(const std::string& v, bool def) {
    if (v == "on" || v == "1" || v == "yes" || v == "true") return true;
    if (v == "off" || v == "0" || v == "no" || v == "false") return false;
    return def;
}

}  // namespace

Settings DefaultSettings() { return Settings{}; }
Settings& CurrentSettings() { return g_now; }
const Settings& StartSettings() { return g_start; }

void LoadSettings() {
    std::ifstream f(kFile);
    Settings s;
    std::string line;
    while (f && std::getline(f, line)) {
        size_t hash = line.find_first_of("#;");
        if (hash != std::string::npos) line.resize(hash);
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string k = Trim(line.substr(0, eq)), v = Trim(line.substr(eq + 1));
        for (char& c : v) c = char(std::tolower(uint8_t(c)));
        if (k == "language") {
            for (const Lang& l : kLanguages)
                if (v == l.key) s.language = l.code;
        } else if (k == "cpu_clock") {
            s.cpu_hz = Hz(v);
        } else if (k == "gpu_clock") {
            s.gpu_hz = Hz(v);
        } else if (k == "memory_clock") {
            s.mem_hz = Hz(v);
        } else if (k == "counter") {
            s.counter = v == "full" ? Counter::Full : v == "fps" ? Counter::Fps : Counter::Off;
        } else if (k == "volume") {
            int n = std::atoi(v.c_str());
            if (n >= 0 && n <= 100) s.volume = uint32_t(n);
        } else if (k == "renderer") {
            s.gpu_renderer = v != "cpu";
        } else if (k == "render_thread") {
            s.render_thread = On(v, true);
        } else if (k == "startup_log") {
            s.startup_log = On(v, false);
        }
    }
    g_now = g_start = s;
}

void SaveSettings() {
    const Settings& s = g_now;
    const char* lang = "auto";
    for (const Lang& l : kLanguages)
        if (s.language == l.code) lang = l.key;
    std::ostringstream o;
    o << "# Spider-Man 2 (Xbox) for Switch: the port's settings, changed in its debug menu (hold ZL + ZR, press -).\n"
         "# Delete this file to go back to the recommended settings.\n"
      << "language=" << lang << "        # auto (the console's), en, es, fr, de, it\n"
      << "cpu_clock=" << Mhz(s.cpu_hz) << "      # stock, or MHz\n"
      << "gpu_clock=" << Mhz(s.gpu_hz) << "\n"
      << "memory_clock=" << Mhz(s.mem_hz) << "\n"
      << "counter=" << (s.counter == Counter::Full ? "full" : s.counter == Counter::Fps ? "fps" : "off")
      << "         # the frame-rate counter: off, fps, full\n"
      << "volume=" << s.volume << "          # percent\n"
      << "renderer=" << (s.gpu_renderer ? "gpu" : "cpu") << "        # gpu, cpu\n"
      << "render_thread=" << (s.render_thread ? "on" : "off") << "\n"
      << "startup_log=" << (s.startup_log ? "on" : "off") << "     # the log on screen while the game starts\n";
    std::string text = o.str();
    std::string tmp = std::string(kFile) + ".new";
    FILE* f = std::fopen(tmp.c_str(), "wb");
    bool ok = f && std::fwrite(text.data(), 1, text.size(), f) == text.size();
    if (f) ok = std::fclose(f) == 0 && ok;
    if (ok) {
        std::remove(kFile);
        ok = std::rename(tmp.c_str(), kFile) == 0;
    }
    if (!ok) LOGW("settings: %s could not be written", kFile);
}

}  // namespace sw
