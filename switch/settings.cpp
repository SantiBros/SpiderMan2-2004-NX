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

// "auto", "stock" or MHz ("1785", "307.2")
uint32_t Hz(const std::string& v) {
    if (v.empty() || v == "auto") return kClockAuto;
    if (v == "stock") return 0;
    double mhz = std::atof(v.c_str());
    return mhz > 0 && mhz < 5000 ? uint32_t(mhz * 1e6 + 0.5) : 0;
}
std::string Mhz(uint32_t hz) {
    if (hz == kClockAuto) return "auto";
    if (!hz) return "stock";
    char b[24];
    if (hz % 1000000)
        std::snprintf(b, sizeof b, "%.1f", hz / 1e6);
    else
        std::snprintf(b, sizeof b, "%u", hz / 1000000);
    return b;
}
const char* ScreenKey(Screen v) {
    switch (v) {
        case Screen::Blur: return "4:3-blur";
        case Screen::Web: return "4:3-web";
        case Screen::Wide: return "16:9";
        default: return "4:3";
    }
}
const char* RumbleKey(Rumble v) {
    switch (v) {
        case Rumble::Off: return "off";
        case Rumble::Low: return "low";
        case Rumble::High: return "high";
        default: return "medium";
    }
}
bool On(const std::string& v, bool def) {
    if (v == "on" || v == "1" || v == "yes" || v == "true") return true;
    if (v == "off" || v == "0" || v == "no" || v == "false") return false;
    return def;
}

}  // namespace

Settings DefaultSettings() { return Settings{}; }

void ClocksFor(const Settings& s, bool docked, uint32_t& cpu, uint32_t& gpu, uint32_t& mem) {
    cpu = s.cpu_hz == kClockAuto ? 1785000000u : s.cpu_hz;
    gpu = s.gpu_hz == kClockAuto ? (docked || s.resolution > 480 ? 768000000u : 460800000u) : s.gpu_hz;
    mem = s.mem_hz == kClockAuto ? 1600000000u : s.mem_hz;
}
Settings& CurrentSettings() { return g_now; }
const Settings& StartSettings() { return g_start; }

void LoadSettings() {
    std::ifstream f(kFile);
    Settings s;
    std::string line;
    uint32_t version = 0;  // settings=N; before 0.2.1 there was none, and stock clocks were the default
    bool stock_clock[3] = {};
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
        } else if (k == "resolution") {
            int n = std::atoi(v.c_str());
            if (n == 480 || n == 720 || n == 1080) s.resolution = uint32_t(n);
        } else if (k == "screen") {
            s.screen = v == "16:9" ? Screen::Wide : v == "4:3-blur" ? Screen::Blur : v == "4:3-web" ? Screen::Web : Screen::Black;
        } else if (k == "upscaling") {
            s.sharp = v == "sharp";
        } else if (k == "anisotropic") {
            int n = std::atoi(v.c_str());
            if (n == 2 || n == 4 || n == 8 || n == 16) s.anisotropy = uint32_t(n);
            else if (v == "off") s.anisotropy = 1;
        } else if (k == "rumble") {
            s.rumble = v == "off" ? Rumble::Off : v == "low" ? Rumble::Low : v == "high" ? Rumble::High : Rumble::Medium;
        } else if (k == "settings") {
            version = uint32_t(std::atoi(v.c_str()));
        } else if (k == "cpu_clock") {
            s.cpu_hz = Hz(v);
            stock_clock[0] = v == "stock";
        } else if (k == "gpu_clock") {
            s.gpu_hz = Hz(v);
            stock_clock[1] = v == "stock";
        } else if (k == "memory_clock") {
            s.mem_hz = Hz(v);
            stock_clock[2] = v == "stock";
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
    if (version < 2 && (stock_clock[0] || stock_clock[1] || stock_clock[2])) {
        // the stock clocks an older version wrote as its default: the automatic ones now
        if (stock_clock[0]) s.cpu_hz = kClockAuto;
        if (stock_clock[1]) s.gpu_hz = kClockAuto;
        if (stock_clock[2]) s.mem_hz = kClockAuto;
        LOGI("settings: the stock clocks of an older settings.ini are now Auto");
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
      << "settings=2\n"
      << "language=" << lang << "        # auto (the console's), en, es, fr, de, it\n"
      << "resolution=" << s.resolution << "p" << (s.resolution < 1000 ? "      " : "     ")
      << "# 480p (the console's), 720p, 1080p\n"
      << "screen=" << ScreenKey(s.screen) << "    # 4:3, 4:3-blur, 4:3-web, 16:9\n"
      << "upscaling=" << (s.sharp ? "sharp" : "smooth") << "  # smooth, sharp\n"
      << "anisotropic=" << (s.anisotropy > 1 ? std::to_string(s.anisotropy) + "x" : std::string("off"))
      << "   # texture filtering: off, 2x, 4x, 8x, 16x\n"
      << "rumble=" << RumbleKey(s.rumble) << "      # off, low, medium, high\n"
      << "cpu_clock=" << Mhz(s.cpu_hz) << "      # auto, stock, or MHz\n"
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
