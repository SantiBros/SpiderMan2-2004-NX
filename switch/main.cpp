// Nintendo Switch entry: checks the launch mode and the game files, then boots the recompiled image.
// Layout on the SD card (the folder of this .nro):
//   SpiderMan2Xbox.nro
//   game/default.xbe and game/data/...   the player's own copy
//   hdd/                                 saves, made on the first run
//   logs/                                sm2_NNN.log, what to send back when something goes wrong
#include <switch.h>

#include <unistd.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

#include "app/app.h"
#include "core/log.h"
#include "image_symbols.h"
#include "kernel/kernel.h"
#include "switch/screen.h"
#include "switch/state.h"

namespace fs = std::filesystem;

extern const char kImageSha1[];
constexpr const char* kVersion = "0.0.5";

namespace {

// a message on the screen until + is pressed
[[noreturn]] void Fail(const char* title, const std::string& detail) {
    sw::Message(title, detail, "Press + to go back to the Homebrew Menu.");
    PadState pad;
    padInitializeDefault(&pad);
    while (appletMainLoop()) {
        sw::Pump();
        padUpdate(&pad);
        if (padGetButtonsDown(&pad) & HidNpadButton_Plus) break;
        svcSleepThread(16'000'000);
    }
    logx::Sync();
    sw::Close();
    std::exit(1);
}

// buttons held as the program starts (the pad needs a few updates to report them)
uint64_t HeldAtStart() {
    PadState pad;
    padInitializeDefault(&pad);
    uint64_t held = 0;
    for (int i = 0; i < 6; i++) {
        padUpdate(&pad);
        held |= padGetButtons(&pad);
        svcSleepThread(16'000'000);
    }
    return held;
}

std::string Folder(const char* argv0) {
    std::string p = argv0 ? argv0 : "";
    size_t slash = p.rfind('/');
    if (slash == std::string::npos) return "sdmc:/switch/SpiderMan2Xbox";
    return p.substr(0, slash);
}

}  // namespace

int main(int argc, char** argv) {
    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    std::string dir = Folder(argc > 0 ? argv[0] : nullptr);
    chdir(dir.c_str());
    sw::Init();
    sw::g_no_sound = HeldAtStart() & HidNpadButton_L;

    AppletType at = appletGetAppletType();
    if (at != AppletType_Application && at != AppletType_SystemApplication)
        Fail("Not enough memory in this mode",
             "This was started from the Album, which gives homebrew only a small part of the Switch's memory.\n"
             "The game needs about 1.3 GB.\n\n"
             "Open the Homebrew Menu by holding R while you start any game, then start this again.");

    std::string game = dir + "/game";
    if (!fs::exists(game + "/" + IMAGE_XBE))
        Fail("Game files not found",
             "Copy your own copy of the game here:\n\n  " + game + "/default.xbe\n  " + game +
                 "/data/...\n\n(the default.xbe and the data folder from your disc)");
    if (!fs::is_directory(game + "/data")) {
        bool flat = fs::exists(game + "/game.ini") || fs::is_directory(game + "/packs") || fs::is_directory(game + "/PACKS");
        Fail("The data folder is missing",
             flat ? "The contents of the data folder are directly in\n\n  " + game +
                        "\n\nMake a folder named data there and move game.ini, movies, packs\nand sound into it, so you "
                        "have:\n\n  " + game + "/default.xbe\n  " + game + "/data/game.ini\n  " + game +
                        "/data/packs/...\n  " + game + "/data/sound/..."
                  : "Copy the data folder from your disc here:\n\n  " + game + "/data\n\n(next to default.xbe, with "
                    "game.ini, movies, packs and sound inside)");
    }

    const std::string title = std::string("Spider-Man 2 (Xbox) - test build ") + kVersion;
    const std::string footer = "Quit: hold + and - together.  Logs: " + dir + "/logs";
    sw::Message(title, "Starting...", footer);
    sw::Pump();

    setenv("GAME_DIR", game.c_str(), 1);
    logx::Init((app::ExeDir() / "logs").string(), app::kGame.log);
    sw::StartWatch();
    u64 total = 0, used = 0;
    svcGetInfo(&total, InfoType_TotalMemorySize, CUR_PROCESS_HANDLE, 0);
    svcGetInfo(&used, InfoType_UsedMemorySize, CUR_PROCESS_HANDLE, 0);
    u32 hos = hosversionGet();
    LOGI("switch: %s, folder %s, applet type %d, firmware %u.%u.%u, memory %llu MB of %llu MB used", kVersion,
         dir.c_str(), int(at), HOSVER_MAJOR(hos), HOSVER_MINOR(hos), HOSVER_MICRO(hos),
         (unsigned long long)(used >> 20), (unsigned long long)(total >> 20));
    u64 cores = 0;
    svcGetInfo(&cores, InfoType_CoreMask, CUR_PROCESS_HANDLE, 0);
    LOGI("switch: core mask %llX, sound %s", (unsigned long long)cores, sw::g_no_sound ? "off (L held)" : "on");

    std::ifstream f(game + "/" + IMAGE_XBE, std::ios::binary);
    std::vector<uint8_t> xbe((std::istreambuf_iterator<char>(f)), {});
    LOGI("switch: %s, %zu bytes, built for SHA-1 %s", IMAGE_XBE, xbe.size(), kImageSha1);
    if (app::kGame.init) app::kGame.init();
    if (!kernel::Boot(std::move(xbe), IMAGE_XBE, game, (app::ExeDir() / "hdd").string()))
        Fail("The game did not start", "See the newest file in " + dir + "/logs");

    // the guest runs on its own threads. This one keeps the applet alive and puts things on screen: until the game
    // draws its first picture, the newest log lines, so a photo says how far it got. It takes no lock a game thread
    // could hold for long, so quitting works even when the game hangs.
    uint64_t last_text = 0;
    while (appletMainLoop() && !sw::QuitRequested()) {
        svcSleepThread(16'000'000);
        uint64_t now = armTicksToNs(armGetSystemTick());
        if (!sw::ShowingGame() && now - last_text > 250'000'000ull) {
            std::string recent;
            if (logx::Recent(22, recent)) {
                std::string cut;  // one screen line each
                size_t start = 0;
                while (start < recent.size()) {
                    size_t end = recent.find('\n', start);
                    if (end == std::string::npos) end = recent.size();
                    cut += recent.substr(start, std::min<size_t>(end - start, 76)) + "\n";
                    start = end + 1;
                }
                sw::Message(title, cut, footer);
                last_text = now;
            }
        }
        sw::Pump();
    }
    LOGI("switch: quitting");
    if (int fd = logx::Descriptor(); fd >= 0) fsync(fd);  // every line is flushed already, no lock needed
    sw::Close();
    std::_Exit(0);
}
