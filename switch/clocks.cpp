// Clock rates offered by the debug menu are only those of Nintendo's own performance configurations (at most
// 1785 MHz CPU, 768 MHz GPU, 1600 MHz memory). While custom clocks are in use the exit is locked, so that closing
// the port from the HOME menu first gives the system's clocks back; at the system's clocks it is never locked, so the
// system can always close the port, even when the game hangs. Adapted from the NFS Most Wanted port's.
#include "switch/clocks.h"

#include <switch.h>

#include <cstdio>
#include <mutex>
#include <string>

#include "core/log.h"

namespace sw::clocks {

namespace {

enum { kCpu, kGpu, kMem, kModules };
const PcvModule kPcvModule[kModules] = {PcvModule_CpuBus, PcvModule_GPU, PcvModule_EMC};
const PcvModuleId kPcvModuleId[kModules] = {PcvModuleId_CpuBus, PcvModuleId_GPU, PcvModuleId_EMC};

// the clocks of the system's performance configurations (from sys-clk's apm_profile_table.c): id, CPU, GPU, memory
struct ApmConfig {
    uint32_t id, hz[kModules];
};
const ApmConfig kApmConfigs[] = {
    {0x00010000, {1020000000, 384000000, 1600000000}}, {0x00010001, {1020000000, 768000000, 1600000000}},
    {0x00010002, {1224000000, 691200000, 1600000000}}, {0x00020000, {1020000000, 230400000, 1600000000}},
    {0x00020001, {1020000000, 307200000, 1600000000}}, {0x00020002, {1224000000, 230400000, 1600000000}},
    {0x00020003, {1020000000, 307200000, 1331200000}}, {0x00020004, {1020000000, 384000000, 1331200000}},
    {0x00020005, {1020000000, 307200000, 1065600000}}, {0x00020006, {1020000000, 384000000, 1065600000}},
    {0x92220007, {1020000000, 460800000, 1600000000}}, {0x92220008, {1020000000, 460800000, 1331200000}},
    {0x92220009, {1785000000, 76800000, 1600000000}},  {0x9222000A, {1785000000, 76800000, 1331200000}},
    {0x9222000B, {1020000000, 76800000, 1600000000}},  {0x9222000C, {1020000000, 76800000, 1331200000}},
};
// when the configuration can't be read: the applications' default ones, handheld and docked
const uint32_t kFallback[2][kModules] = {{1020000000, 307200000, 1331200000}, {1020000000, 768000000, 1600000000}};

std::recursive_mutex g_mutex;
bool g_tried = false, g_ok = false, g_clkrst = false;
ClkrstSession g_session[kModules];
Service g_apm;
bool g_apm_ok = false;
uint32_t g_target[kModules] = {};    // 0: the system's
bool g_managed[kModules] = {};       // changed by the port
uint32_t g_settled[kModules] = {};   // the rate the hardware took after the port's last change
bool g_background = false;           // HOME menu, sleep: the system's clocks
bool g_exiting = false;
bool g_force = false;
uint64_t g_next_check = 0;
bool g_exit_locked = false;
AppletHookCookie g_hook;

bool Init() {
    if (g_tried) return g_ok;
    g_tried = true;
    if (hosversionAtLeast(8, 0, 0)) {
        g_ok = R_SUCCEEDED(clkrstInitialize());
        if (g_ok) {
            g_clkrst = true;
            for (int m = 0; m < kModules; m++)
                if (R_FAILED(clkrstOpenSession(&g_session[m], kPcvModuleId[m], 3))) g_ok = false;
        }
    } else {
        g_ok = R_SUCCEEDED(pcvInitialize());
    }
    g_apm_ok = R_SUCCEEDED(smGetService(&g_apm, "apm:sys"));
    if (!g_ok) LOGW("clocks: neither clkrst nor pcv can be used, the clock settings do nothing");
    return g_ok;
}

uint32_t GetHz(int m) {
    uint32_t hz = 0;
    if (g_clkrst)
        clkrstGetClockRate(&g_session[m], &hz);
    else
        pcvGetClockRate(kPcvModule[m], &hz);
    return hz;
}

void SetHz(int m, uint32_t hz) {
    Result rc = g_clkrst ? clkrstSetClockRate(&g_session[m], hz) : pcvSetClockRate(kPcvModule[m], hz);
    if (R_FAILED(rc)) LOGW("clocks: module %d to %u Hz failed (%X)", m, hz, unsigned(rc));
    g_settled[m] = GetHz(m);  // the hardware may round to a rate it has
}

// what the system's current performance configuration wants
uint32_t StockHz(int m) {
    if (g_apm_ok && hosversionAtLeast(9, 0, 0)) {
        uint32_t conf = 0;
        if (R_SUCCEEDED(serviceDispatchOut(&g_apm, 7, conf)))  // GetCurrentPerformanceConfiguration
            for (const ApmConfig& c : kApmConfigs)
                if (c.id == conf) return c.hz[m];
    }
    return kFallback[Docked() ? 1 : 0][m];
}

void Apply() {
    std::lock_guard<std::recursive_mutex> lock(g_mutex);
    if (!Init()) return;
    for (int m = 0; m < kModules; m++) {
        bool custom = g_target[m] && !g_background && !g_exiting;
        uint32_t want = custom ? g_target[m] : g_managed[m] ? StockHz(m) : 0;  // the system's back, once
        if (!want) continue;
        uint32_t cur = GetHz(m);
        if (cur != want && !(g_managed[m] && custom && cur == g_settled[m])) SetHz(m, want);
        g_managed[m] = custom;
    }
}

void Hook(AppletHookType type, void*) {
    switch (type) {
        case AppletHookType_OnFocusState: {
            bool bg = appletGetFocusState() != AppletFocusState_InFocus;
            if (bg != g_background) {
                g_background = bg;
                if (g_target[kCpu] || g_target[kGpu] || g_target[kMem])
                    LOGI("clocks: %s, %s clocks", bg ? "in the background" : "back in front", bg ? "the system's" : "the chosen");
                Apply();
            }
            break;
        }
        case AppletHookType_OnOperationMode:
        case AppletHookType_OnPerformanceMode:
        case AppletHookType_OnResume:
            g_force = true;  // the system has just set its own
            break;
        default:
            break;
    }
}

std::string Text(uint32_t hz) {
    if (!hz) return "the system's";
    char b[24];
    std::snprintf(b, sizeof b, "%.1f MHz", hz / 1e6);
    return b;
}

}  // namespace

bool Supported() {
    std::lock_guard<std::recursive_mutex> lock(g_mutex);
    return Init();
}

void Set(uint32_t cpu, uint32_t gpu, uint32_t mem) {
    static bool hooked = false;
    if (!hooked) {
        hooked = true;
        appletHook(&g_hook, Hook, nullptr);
    }
    bool custom = cpu || gpu || mem;
    if (custom && !g_exit_locked && R_SUCCEEDED(appletLockExit())) g_exit_locked = true;
    {
        std::lock_guard<std::recursive_mutex> lock(g_mutex);
        if (g_target[kCpu] == cpu && g_target[kGpu] == gpu && g_target[kMem] == mem) return;
        g_target[kCpu] = cpu;
        g_target[kGpu] = gpu;
        g_target[kMem] = mem;
    }
    Apply();
    LOGI("clocks: CPU %s, GPU %s, memory %s", Text(cpu).c_str(), Text(gpu).c_str(), Text(mem).c_str());
    // the system's clocks are back: nothing to give back at the exit
    if (!custom && g_exit_locked) {
        g_exit_locked = false;
        appletUnlockExit();
    }
}

bool Get(uint32_t& cpu, uint32_t& gpu, uint32_t& mem) {
    std::lock_guard<std::recursive_mutex> lock(g_mutex);
    if (!Init()) return false;
    cpu = GetHz(kCpu);
    gpu = GetHz(kGpu);
    mem = GetHz(kMem);
    return true;
}

void Tick() {
    uint64_t now = armTicksToNs(armGetSystemTick()) / 1000000;
    if (!g_force && now < g_next_check) return;
    g_next_check = now + 500;
    g_force = false;
    if (g_managed[kCpu] || g_managed[kGpu] || g_managed[kMem] || g_target[kCpu] || g_target[kGpu] || g_target[kMem])
        Apply();
}

void Exit() {
    {
        std::lock_guard<std::recursive_mutex> lock(g_mutex);
        if (g_exiting) return;
        g_exiting = true;
        if (g_ok) Apply();
        if (g_tried) {
            if (g_clkrst) {
                for (auto& s : g_session) clkrstCloseSession(&s);
                clkrstExit();
            } else if (g_ok) {
                pcvExit();
            }
            if (g_apm_ok) serviceClose(&g_apm);
        }
    }
    if (g_exit_locked) {
        g_exit_locked = false;
        appletUnlockExit();
    }
}

bool Docked() { return appletGetOperationMode() == AppletOperationMode_Console; }

}  // namespace sw::clocks
