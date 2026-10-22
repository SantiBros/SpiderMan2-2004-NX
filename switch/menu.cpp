#include "switch/menu.h"

#include <switch.h>

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

#include "core/log.h"
#include "switch/clocks.h"
#include "switch/screen.h"
#include "switch/settings.h"
#include "switch/state.h"

namespace sw::menu {

namespace {

// colours, red in the low byte
constexpr uint32_t kPanel = 0xEC2A170Eu;   // the start screen's dark blue
constexpr uint32_t kRed = 0xFF2E10C8u;     // the game's red
constexpr uint32_t kRedBand = 0x702E10C8u;
constexpr uint32_t kAmber = 0xFF40C8FFu;
constexpr uint32_t kWhite = 0xFFFFFFFFu;
constexpr uint32_t kDim = 0xFFD0D0D0u;
constexpr uint32_t kGray = 0xFF9A9A9Au;
constexpr uint32_t kOrange = 0xFF3399FFu;
constexpr uint32_t kLine = 0x30FFFFFFu;

uint64_t NowMs() { return armTicksToNs(armGetSystemTick()) / 1000000; }

struct Choice {
    uint32_t value;
    std::string text;
    std::string note;  // in orange while it is chosen: what it costs
};
enum class Kind { Setting, Restore, Close };
struct Item {
    Kind kind = Kind::Setting;
    std::string label;
    std::function<uint32_t(const Settings&)> get;
    std::function<void(Settings&, uint32_t)> set;
    std::vector<Choice> choices;
    bool next_start = false;  // takes effect when the game starts again
    bool clock = false;       // the Switch's clocks: only where they can be set
    std::string help;
};

std::atomic<bool> g_open{false};
std::atomic<bool> g_block{false};      // the buttons that closed the menu are still held
std::atomic<bool> g_combo{false};      // ZL and ZR held: - belongs to the menu
uint32_t g_console_language = 1;
int g_sel = 0;
std::vector<Item> g_items;
std::string g_flash;
uint64_t g_flash_until = 0;
bool g_dirty = true;                   // the menu needs drawing again
uint64_t g_next_live = 0;              // the live numbers' next update

// the live numbers, every half a second
struct Live {
    uint64_t at = 0, frames = 0, draws = 0;
    double fps = 0, ms = 0, draws_per_frame = 0;
    uint32_t cpu = 0, gpu = 0, mem = 0;
    bool clocks = false;
    uint64_t used_mb = 0, total_mb = 0;
} g_live;

struct Repeat {
    bool held = false;
    uint64_t next = 0;
    bool Fire(bool down, uint64_t now) {
        if (!down) return held = false;
        if (!held) {
            held = true;
            next = now + 350;
            return true;
        }
        if (now >= next) {
            next = now + 110;
            return true;
        }
        return false;
    }
} g_rep[4];

// where the menu is on screen, for the touch screen
constexpr int kPanelW = 960;
int g_panel_x = 160, g_panel_y = 0, g_panel_h = 0;
struct Row {
    int y0 = 0, y1 = 0;
};
std::vector<Row> g_rows;
int g_value_x0 = 0, g_value_mid = 0;
bool g_touching = false;
int g_touch_x = 0, g_touch_y = 0;

const char* LanguageName(uint32_t code) {
    switch (code) {
        case 1: return "English";
        case 3: return "Deutsch";
        case 4: return "Fran\x87" "ais";
        case 5: return "Espa\xA4" "ol";
        case 6: return "Italiano";
        default: return "English";
    }
}

std::string MhzText(uint32_t hz) {
    char b[24];
    if (hz % 1000000)
        std::snprintf(b, sizeof b, "%.1f MHz", hz / 1e6);
    else
        std::snprintf(b, sizeof b, "%u MHz", hz / 1000000);
    return b;
}

void Build() {
    const bool docked = clocks::Docked();
    g_items.clear();

    Item lang;
    lang.label = "Language";
    lang.get = [](const Settings& s) { return s.language; };
    lang.set = [](Settings& s, uint32_t v) { s.language = v; };
    lang.next_start = true;
    lang.choices = {{0, std::string("Auto (") + LanguageName(g_console_language) + ")", ""},
                    {1, "English", ""},
                    {5, LanguageName(5), "The voices stay in English: the disc has only the English ones."},
                    {4, LanguageName(4), "The voices stay in English: the disc has only the English ones."},
                    {3, LanguageName(3), "The voices stay in English: the disc has only the English ones."},
                    {6, LanguageName(6), "The voices stay in English: the disc has only the English ones."}};
    lang.help = "The game's menus and subtitles. Auto follows the console's language; one the game doesn't have "
                "is English.";
    g_items.push_back(lang);

    const char* above = "Above the console's own clock: more heat and battery use.";
    Item cpu;
    cpu.label = "CPU clock";
    cpu.get = [](const Settings& s) { return s.cpu_hz; };
    cpu.set = [](Settings& s, uint32_t v) { s.cpu_hz = v; };
    cpu.clock = true;
    cpu.choices = {{0, "Stock (1020 MHz)", ""},
                   {1224000000, "1224 MHz", "A clock some Switch games use: a little more battery use."},
                   {1428000000, "1428 MHz", above},
                   {1581000000, "1581 MHz", above},
                   {1785000000, "1785 MHz", above}};
    cpu.help = "The game's translated code runs on the CPU. Raise it when the counter stays under 30 FPS in "
               "busy places.";
    g_items.push_back(cpu);

    Item gpu;
    gpu.label = "GPU clock";
    gpu.get = [](const Settings& s) { return s.gpu_hz; };
    gpu.set = [](Settings& s, uint32_t v) { s.gpu_hz = v; };
    gpu.clock = true;
    const char* hot = docked ? "" : "Above Nintendo's handheld limit: more heat and battery drain.";
    const char* more = docked ? "" : "Above the normal handheld clock: more battery use.";
    const char* low = docked ? "Below the normal docked clock: slower graphics." : "";
    gpu.choices = {{0, docked ? "Stock (768 MHz)" : "Stock (307.2 MHz)", ""},
                   {307200000, "307.2 MHz", low},
                   {384000000, "384 MHz", docked ? low : more},
                   {460800000, "460.8 MHz", docked ? low : more},
                   {614400000, "614.4 MHz", docked ? low : hot},
                   {768000000, "768 MHz", hot}};
    gpu.help = "The Switch's graphics chip. 460.8 MHz is the highest clock Nintendo uses in handheld mode.";
    g_items.push_back(gpu);

    Item mem;
    mem.label = "Memory clock";
    mem.get = [](const Settings& s) { return s.mem_hz; };
    mem.set = [](Settings& s, uint32_t v) { s.mem_hz = v; };
    mem.clock = true;
    mem.choices = {{0, docked ? "Stock (1600 MHz)" : "Stock (1331.2 MHz)", ""},
                   {1331200000, "1331.2 MHz", docked ? "Slower memory: a lower frame rate in busy places." : ""},
                   {1600000000, "1600 MHz", docked ? "" : "Above the normal handheld clock: more battery use."}};
    mem.help = "1600 MHz is the docked memory clock; Nintendo uses it in handheld mode too.";
    g_items.push_back(mem);

    Item counter;
    counter.label = "Frame-rate counter";
    counter.get = [](const Settings& s) { return uint32_t(s.counter); };
    counter.set = [](Settings& s, uint32_t v) { s.counter = Counter(v); };
    counter.choices = {{uint32_t(Counter::Off), "Off", ""},
                       {uint32_t(Counter::Fps), "FPS", ""},
                       {uint32_t(Counter::Full), "Full", ""}};
    counter.help = "In the top left corner, beside the picture. Full adds the time of a frame, the draw calls, the "
                   "clocks and the memory.";
    g_items.push_back(counter);

    Item vol;
    vol.label = "Sound volume";
    vol.get = [](const Settings& s) { return s.volume; };
    vol.set = [](Settings& s, uint32_t v) { s.volume = v; };
    for (uint32_t pct : {100u, 90u, 80u, 70u, 60u, 50u, 25u, 0u})
        vol.choices.push_back({pct, pct ? std::to_string(pct) + "%" : std::string("Off"), ""});
    vol.help = "The game's sound, music and voices, before the console's own volume.";
    g_items.push_back(vol);

    Item ren;
    ren.label = "Renderer";
    ren.get = [](const Settings& s) { return uint32_t(s.gpu_renderer); };
    ren.set = [](Settings& s, uint32_t v) { s.gpu_renderer = v != 0; };
    ren.next_start = true;
    ren.choices = {{1, "GPU", ""},
                   {0, "CPU", "Many times slower: only to see whether a fault in the picture comes from the GPU renderer."}};
    ren.help = "What draws the game. Holding R while the port starts picks the CPU renderer for that start only.";
    g_items.push_back(ren);

    Item rt;
    rt.label = "Render thread";
    rt.get = [](const Settings& s) { return uint32_t(s.render_thread); };
    rt.set = [](Settings& s, uint32_t v) { s.render_thread = v != 0; };
    rt.next_start = true;
    rt.choices = {{1, "On", ""}, {0, "Off", "The game then waits for the graphics driver: a lower frame rate."}};
    rt.help = "The GPU renderer's work on a CPU core of its own, beside the game's. Holding ZR while the port starts "
              "turns it off for that start only.";
    g_items.push_back(rt);

    Item log;
    log.label = "Start-up log";
    log.get = [](const Settings& s) { return uint32_t(s.startup_log); };
    log.set = [](Settings& s, uint32_t v) { s.startup_log = v != 0; };
    log.next_start = true;
    log.choices = {{0, "Off", ""}, {1, "On", ""}};
    log.help = "The newest lines of the log on the start screen while the game loads, instead of 'Loading': for "
               "a photo when the game doesn't start.";
    g_items.push_back(log);

    Item restore;
    restore.kind = Kind::Restore;
    restore.label = "Restore recommended settings";
    restore.help = "The console's language, stock clocks, no counter, full volume, the GPU renderer on its own "
                   "thread, no start-up log.";
    g_items.push_back(restore);

    Item close;
    close.kind = Kind::Close;
    close.label = "Close";
    close.help = "Settings are saved as you change them, in settings.ini beside the .nro.";
    g_items.push_back(close);
}

int Index(const Item& it) {
    uint32_t v = it.get(CurrentSettings());
    for (size_t i = 0; i < it.choices.size(); i++)
        if (it.choices[i].value == v) return int(i);
    return -1;  // a value written into settings.ini by hand
}

// the settings that act at once
void ApplyLive() {
    const Settings& s = CurrentSettings();
    clocks::Set(s.cpu_hz, s.gpu_hz, s.mem_hz);
    g_volume.store(s.volume / 100.0f, std::memory_order_relaxed);
}

void SetIndex(Item& it, int i) {
    i = std::clamp(i, 0, int(it.choices.size()) - 1);
    it.set(CurrentSettings(), it.choices[i].value);
    LOGI("debug menu: %s %s", it.label.c_str(), it.choices[i].text.c_str());
    SaveSettings();
    if (!it.next_start) ApplyLive();
    g_dirty = true;
}

void Step(Item& it, int dir, bool wrap) {
    if (it.kind != Kind::Setting || it.choices.empty()) return;
    int n = int(it.choices.size()), i = Index(it);
    if (i < 0) i = dir > 0 ? -1 : n;
    int j = wrap ? ((i + dir) % n + n) % n : std::clamp(i + dir, 0, n - 1);
    if (j != i) SetIndex(it, j);
}

void Flash(const std::string& s) {
    g_flash = s;
    g_flash_until = NowMs() + 2500;
    g_dirty = true;
}

void Close();

void Activate(Item& it) {
    switch (it.kind) {
        case Kind::Setting:
            Step(it, +1, true);
            break;
        case Kind::Restore:
            CurrentSettings() = DefaultSettings();
            SaveSettings();
            ApplyLive();
            LOGI("debug menu: the recommended settings back");
            Flash("The recommended settings are back.");
            break;
        case Kind::Close:
            Close();
            break;
    }
}

bool RestartPending() {
    const Settings &a = CurrentSettings(), &b = StartSettings();
    return a.language != b.language || a.gpu_renderer != b.gpu_renderer || a.render_thread != b.render_thread ||
           a.startup_log != b.startup_log;
}

std::vector<std::string> Wrap(const std::string& s, size_t chars) {
    std::vector<std::string> lines;
    std::string cur;
    size_t i = 0;
    while (i < s.size()) {
        size_t j = s.find(' ', i);
        if (j == std::string::npos) j = s.size();
        std::string word = s.substr(i, j - i);
        std::string trial = cur.empty() ? word : cur + " " + word;
        if (!cur.empty() && trial.size() > chars) {
            lines.push_back(cur);
            cur = word;
        } else {
            cur = trial;
        }
        i = j + 1;
    }
    if (!cur.empty()) lines.push_back(cur);
    return lines;
}

std::string LiveLine() {
    char b[160];
    std::snprintf(b, sizeof b, "Now: %.1f FPS, %.1f ms a frame, %.0f draws", g_live.fps, g_live.ms,
                  g_live.draws_per_frame);
    return b;
}

void Draw() {
    const int n = int(g_items.size());
    const int row_h = 30, top = 92;
    int separators = 0;
    for (int i = 1; i < n; i++)
        if (g_items[i].kind != Kind::Setting && g_items[i - 1].kind == Kind::Setting) separators++;
    const int help_y0 = top + n * row_h + separators * 12 + 16;
    const int h = help_y0 + 5 * 20 + 56;
    Canvas c;
    c.Reset(kPanelW, uint32_t(h), kPanel);
    c.Rect(0, 0, kPanelW, 4, kRed);
    c.Rect(0, h - 1, kPanelW, h, kLine);
    c.Rect(0, 0, 1, h, kLine);
    c.Rect(kPanelW - 1, 0, kPanelW, h, kLine);
    int x = c.Text(24, 22, "PORT SETTINGS", kWhite, 2);
    c.Text(x + 16, 38, "debug menu", kGray);
    c.Text(24, 62, "Up/Down: choose   Left/Right, A: change   B: close", kGray);

    const int label_x = 28;
    const int value_mid = 690, value_half = 200;
    g_value_x0 = g_panel_x + value_mid - value_half - 24;
    g_value_mid = g_panel_x + value_mid;
    g_rows.assign(n, Row{});
    const bool clocks_ok = clocks::Supported();
    int y = top;
    for (int i = 0; i < n; i++) {
        const Item& it = g_items[i];
        if (i > 0 && it.kind != Kind::Setting && g_items[i - 1].kind == Kind::Setting) {
            c.Rect(20, y + 4, kPanelW - 20, y + 5, kLine);
            y += 12;
        }
        int y0 = y, y1 = y + row_h - 2;
        g_rows[i] = {y0, y1};  // moved onto the screen below
        bool sel = i == g_sel;
        if (sel) {
            c.Rect(12, y0, kPanelW - 12, y1, kRedBand);
            c.Rect(12, y0, 17, y1, kRed);
        }
        int ty = y0 + (row_h - 2 - 16) / 2;
        int lx = c.Text(label_x, ty, it.label, sel ? kWhite : kDim);
        if (it.next_start) c.Text(lx + 16, ty, "next start", kGray);
        else if (it.clock && !clocks_ok) c.Text(lx + 16, ty, "not here", kGray);
        if (it.kind == Kind::Setting) {
            int idx = Index(it);
            std::string v = idx >= 0 ? it.choices[idx].text
                            : it.clock ? MhzText(it.get(CurrentSettings()))
                                       : std::to_string(it.get(CurrentSettings()));
            c.Text(value_mid - TextWidth(v) / 2, ty, v, it.clock && !clocks_ok ? kGray : kWhite);
            if (sel) {
                c.Text(value_mid - value_half - 8, ty, "<", idx != 0 ? kAmber : kGray);
                c.Text(value_mid + value_half - 8, ty, ">", idx != int(it.choices.size()) - 1 ? kAmber : kGray);
            }
        }
        y += row_h;
    }

    // the chosen row's help, then what its value costs
    const Item& cur = g_items[g_sel];
    std::vector<std::pair<std::string, uint32_t>> help;
    std::string text = cur.help;
    if (cur.kind == Kind::Setting) {
        uint32_t def = cur.get(DefaultSettings());
        for (const Choice& ch : cur.choices)
            if (ch.value == def) text += " Recommended: " + ch.text + ".";
    }
    const size_t chars = (kPanelW - 2 * label_x) / 16;
    for (auto& l : Wrap(text, chars)) help.push_back({l, kDim});
    if (cur.kind == Kind::Setting) {
        int idx = Index(cur);
        if (idx >= 0 && !cur.choices[idx].note.empty())
            for (auto& l : Wrap(cur.choices[idx].note, chars)) help.push_back({l, kOrange});
        if (cur.clock && !clocks_ok) help.push_back({"The clocks can't be set here.", kGray});
    }
    if (NowMs() < g_flash_until)
        help.push_back({g_flash, kWhite});
    else if (RestartPending())
        help.push_back({"The \"next start\" settings apply when the port starts again.", kOrange});
    for (size_t i = 0; i < help.size() && i < 5; i++)
        c.Text(label_x, help_y0 + int(i) * 20, help[i].first, help[i].second);

    c.Rect(20, h - 44, kPanelW - 20, h - 43, kLine);
    c.Text(label_x, h - 32, LiveLine(), kGray);
    const char* combo = "ZL + ZR + -";
    c.Text(kPanelW - 24 - TextWidth(combo), h - 32, combo, kGray);

    g_panel_h = h;
    g_panel_y = std::max(0, (720 - h) / 2);
    for (Row& r : g_rows) r.y0 += g_panel_y, r.y1 += g_panel_y;
    SetOverlay(1, c, g_panel_x, g_panel_y);
    g_dirty = false;
}

void Open() {
    if (g_open) return;
    Build();
    g_sel = std::clamp(g_sel, 0, int(g_items.size()) - 1);
    for (auto& r : g_rep) r = Repeat{};
    g_touching = false;
    g_open = true;
    g_dirty = true;
    LOGI("debug menu: open");
}

void Close() {
    if (!g_open) return;
    g_open = false;
    g_block = true;  // until the buttons are let go
    SetOverlay(1, Canvas{}, 0, 0);
    LOGI("debug menu: closed");
}

void UpdateLive(uint64_t now) {
    if (now < g_next_live) return;
    g_next_live = now + 500;
    uint64_t frames = g_stats.frames.load(std::memory_order_relaxed), draws = g_stats.draws.load(std::memory_order_relaxed);
    if (g_live.at) {
        double s = (now - g_live.at) / 1000.0;
        uint64_t f = frames - g_live.frames;
        g_live.fps = f / s;
        g_live.ms = f ? s * 1000.0 / f : 0;
        g_live.draws_per_frame = f ? double(draws - g_live.draws) / f : 0;
    }
    g_live.at = now;
    g_live.frames = frames;
    g_live.draws = draws;
    // the clock services are opened only when something shows the clocks
    Counter mode = CurrentSettings().counter;
    if (g_open || mode == Counter::Full) g_live.clocks = clocks::Get(g_live.cpu, g_live.gpu, g_live.mem);
    u64 total = 0, used = 0;
    svcGetInfo(&total, InfoType_TotalMemorySize, CUR_PROCESS_HANDLE, 0);
    svcGetInfo(&used, InfoType_UsedMemorySize, CUR_PROCESS_HANDLE, 0);
    g_live.used_mb = used >> 20;
    g_live.total_mb = total >> 20;
    if (g_open) g_dirty = true;

    // the counter, in the black bar left of the picture
    if (mode == Counter::Off) {
        SetOverlay(0, Canvas{}, 0, 0);
        return;
    }
    std::vector<std::string> lines;
    char b[32];
    std::snprintf(b, sizeof b, "%.1f FPS", g_live.fps);
    lines.push_back(b);
    if (mode == Counter::Full) {
        std::snprintf(b, sizeof b, "%.1f ms", g_live.ms);
        lines.push_back(b);
        std::snprintf(b, sizeof b, "%.0f draws", g_live.draws_per_frame);
        lines.push_back(b);
        if (g_live.clocks) {
            std::snprintf(b, sizeof b, "CPU %u", g_live.cpu / 1000000);
            lines.push_back(b);
            std::snprintf(b, sizeof b, "GPU %u", g_live.gpu / 1000000);
            lines.push_back(b);
            std::snprintf(b, sizeof b, "MEM %u", g_live.mem / 1000000);
            lines.push_back(b);
        }
        std::snprintf(b, sizeof b, "RAM %lluM", (unsigned long long)g_live.used_mb);
        lines.push_back(b);
    }
    Canvas c;
    c.Reset(152, uint32_t(lines.size() * 20 + 8), 0xA0000000u);
    uint32_t colour = g_live.fps >= 29.0 ? 0xFF60FF60u : g_live.fps >= 20.0 ? kAmber : 0xFF4040FFu;
    for (size_t i = 0; i < lines.size(); i++) c.Text(4, 4 + int(i) * 20, lines[i], i == 0 ? colour : kWhite);
    SetOverlay(0, c, 4, 4);
}

}  // namespace

void SetConsoleLanguage(uint32_t code) { g_console_language = code; }

bool IsOpen() { return g_open; }

bool Blocking(bool& hide_back) {
    hide_back = g_combo.load(std::memory_order_relaxed);
    return g_open.load(std::memory_order_relaxed) || g_block.load(std::memory_order_relaxed);
}

void Update() {
    static PadState pad;
    static bool init = false;
    if (!init) {
        padInitializeDefault(&pad);
        hidInitializeTouchScreen();
        init = true;
        ApplyLive();
    }
    padUpdate(&pad);
    uint64_t held = padGetButtons(&pad), down = padGetButtonsDown(&pad), now = NowMs();
    const uint64_t combo = HidNpadButton_ZL | HidNpadButton_ZR | HidNpadButton_Minus;
    g_combo = (held & (HidNpadButton_ZL | HidNpadButton_ZR)) == (HidNpadButton_ZL | HidNpadButton_ZR);
    // the stick's directions count as buttons in libnx: only real buttons keep the game's pad empty
    const uint64_t sticks = HidNpadButton_StickLLeft | HidNpadButton_StickLUp | HidNpadButton_StickLRight |
                            HidNpadButton_StickLDown | HidNpadButton_StickRLeft | HidNpadButton_StickRUp |
                            HidNpadButton_StickRRight | HidNpadButton_StickRDown;
    if (g_block && !(held & ~sticks)) g_block = false;
    clocks::Tick();
    UpdateLive(now);

    bool combo_now = (held & combo) == combo && (down & combo);
    if (!g_open) {
        if (combo_now) Open();
    } else if (combo_now || (down & (HidNpadButton_B | HidNpadButton_Plus))) {
        Close();
    } else {
        const int n = int(g_items.size());
        HidAnalogStickState l = padGetStickPos(&pad, 0);
        const int t = 20000;
        bool dir[4] = {(held & HidNpadButton_Up) || l.y > t, (held & HidNpadButton_Down) || l.y < -t,
                       (held & HidNpadButton_Left) || l.x < -t, (held & HidNpadButton_Right) || l.x > t};
        if (g_rep[0].Fire(dir[0], now)) g_sel = (g_sel + n - 1) % n, g_dirty = true;
        if (g_rep[1].Fire(dir[1], now)) g_sel = (g_sel + 1) % n, g_dirty = true;
        if (g_rep[2].Fire(dir[2], now)) Step(g_items[g_sel], -1, false);
        if (g_rep[3].Fire(dir[3], now)) Step(g_items[g_sel], +1, false);
        if (down & HidNpadButton_A) Activate(g_items[g_sel]);

        // the touch screen: a row picks it, the left or right half of a value steps it, outside closes
        HidTouchScreenState touch = {};
        bool touched = hidGetTouchScreenStates(&touch, 1) && touch.count > 0;
        if (g_open && touched && !g_touching) {
            g_touching = true;
            g_touch_x = int(touch.touches[0].x);
            g_touch_y = int(touch.touches[0].y);
        } else if (g_open && !touched && g_touching) {
            g_touching = false;
            int x = g_touch_x, y = g_touch_y;
            if (x < g_panel_x || x >= g_panel_x + kPanelW || y < g_panel_y || y >= g_panel_y + g_panel_h) {
                Close();
            } else {
                for (int i = 0; i < int(g_rows.size()) && i < n; i++) {
                    if (y < g_rows[i].y0 || y >= g_rows[i].y1) continue;
                    g_sel = i;
                    g_dirty = true;
                    Item& it = g_items[i];
                    if (it.kind != Kind::Setting)
                        Activate(it);
                    else if (x >= g_value_x0)
                        Step(it, x < g_value_mid ? -1 : +1, false);
                    break;
                }
            }
        }
        if (g_flash_until && now >= g_flash_until) g_flash_until = 0, g_dirty = true;
    }
    if (g_open && g_dirty) Draw();
}

}  // namespace sw::menu
