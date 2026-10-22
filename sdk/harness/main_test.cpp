// headless host: <exe> <game folder or disc image> [--instance N], run from the folder that gets logs, hdd and cap
// the game folder may come from GAME_DIR instead; a folder is searched as the Switch build does (kernel/game_source)
// PULSE=1: every 2 s a line naming the kernel or library function each guest thread is in, as the Switch build logs
// RT=1: the renderer on a render thread of its own (gpu::g_render_thread), as the Switch's GPU renderer runs
// TIMESCALE=0.2: the guest's clock at a fifth of real time (kernel::g_time_scale), for recordings: with GL=1 every
// frame then covers a 60th of a second of the game however long it takes to draw
#include <algorithm>
#include <chrono>
#include <map>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

#include "app/app.h"
#include "core/log.h"
#include "gpu/backend.h"
#include "image_symbols.h"
#include "kernel/game_source.h"
#include "kernel/kernel.h"

int main(int argc, char** argv) {
    std::string game;
    for (int i = 1; i < argc; i++) {
        if (!std::strcmp(argv[i], "--instance") && i + 1 < argc) setenv("INSTANCE", argv[++i], 1);
        else game = argv[i];
    }
    // a relaunch into another image comes back without arguments
    if (game.empty() && std::getenv("GAME_DIR")) game = std::getenv("GAME_DIR");
    setenv("GAME_DIR", game.c_str(), 1);
    if (game.empty()) {
        std::fprintf(stderr, "usage: %s <game folder> [--instance N]\n", argv[0]);
        return 2;
    }
    logx::Init((app::ExeDir() / "logs").string(), app::kGame.log);
    app::Load();
    // XBE_LANGUAGE=n: the console language (XC_LANGUAGE: 1 English, 3 German, 4 French, 5 Spanish, 6 Italian)
    if (const char* l = std::getenv("XBE_LANGUAGE")) kernel::g_language = uint32_t(std::atoi(l));
    if (app::kGame.init) app::kGame.init();
    if (app::kGame.window) app::kGame.window(nullptr);
    std::string hdd = app::Instance() > 1 ? "hdd" + std::to_string(app::Instance()) : "hdd";
    std::vector<uint8_t> xbe;
    std::error_code ec;
    if (std::filesystem::is_regular_file(game, ec)) {
        // one disc image, as given
        auto disc = std::make_unique<kernel::Disc>();
        std::string error;
        if (!disc->Open(game, error) || !disc->Load(IMAGE_XBE, xbe)) {
            LOGE("harness: %s: %s", game.c_str(), error.empty() ? "no " IMAGE_XBE : error.c_str());
            return 1;
        }
        LOGI("harness: D: is the disc image %s (%s)", game.c_str(), disc->Kind().c_str());
        kernel::MountDisc(std::move(disc));
        game = std::filesystem::path(game).parent_path().string();
    } else {
        auto search = kernel::FindGameCopies({game}, IMAGE_XBE, "");
        auto it = std::find_if(search.copies.begin(), search.copies.end(), [](const kernel::GameCopy& c) { return c.same_entry; });
        if (it == search.copies.end()) {
            LOGE("harness: no copy of the game in %s", game.c_str());
            return 1;
        }
        LOGI("harness: the game from %s (%s)", it->name.c_str(), it->kind.c_str());
        xbe = std::move(it->xbe);
        if (it->disc) kernel::MountDisc(std::move(it->disc));
    }
    if (const char* rt = std::getenv("RT")) gpu::g_render_thread = std::atoi(rt) != 0;
    if (const char* ts = std::getenv("TIMESCALE")) kernel::g_time_scale = std::clamp(std::atof(ts), 0.01, 1.0);
    if (!kernel::Boot(std::move(xbe), IMAGE_XBE, game, (app::ExeDir() / hdd).string())) return 1;
    const bool pulse = std::getenv("PULSE") != nullptr;
    struct Seen {
        const char* call = nullptr;
        uint32_t calls = 0;
        int still = 0;
    };
    std::map<uint32_t, Seen> seen;
    auto start = std::chrono::steady_clock::now();
    for (int tick = 1;; tick++) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        logx::Flush();
        if (!pulse || tick % 2) continue;
        std::string line;
        char buf[160];
        for (auto& t : kernel::Threads()) {
            X86Context* c = t ? t->ctx : nullptr;
            if (!c) continue;
            const char* call = t->call.load(std::memory_order_relaxed);
            uint32_t calls = t->calls.load(std::memory_order_relaxed);
            Seen& s = seen[t->id];
            s.still = call == s.call && calls == s.calls ? s.still + 1 : 0;
            s.call = call;
            uint32_t made = calls - s.calls;
            s.calls = calls;
            std::snprintf(buf, sizeof(buf), " | %u %s %u calls icall %08X esp %08X%s", t->id, call ? call : "game-code",
                          made, c->last_icall, c->esp, s.still >= 2 ? " STILL" : "");
            line += buf;
        }
        double up = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        LOGI("pulse: %.0f s%s", up, line.c_str());
    }
}
