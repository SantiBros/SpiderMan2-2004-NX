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

namespace fs = std::filesystem;

extern const char kImageSha1[];
constexpr const char* kVersion = "0.0.3";

namespace {

// the console text screen for messages before the game draws, waits for + to quit
[[noreturn]] void Fail(const char* title, const std::string& detail) {
    sw::Message(title, detail, "Press + to go back to the Homebrew Menu.");
    PadState pad;
    padInitializeDefault(&pad);
    while (appletMainLoop()) {
        padUpdate(&pad);
        if (padGetButtonsDown(&pad) & HidNpadButton_Plus) break;
        svcSleepThread(16'000'000);
    }
    sw::CloseText();
    std::exit(1);
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

    sw::Message("Spider-Man 2 (Xbox) - test build", "Starting...", "Logs are written to " + dir + "/logs");

    setenv("GAME_DIR", game.c_str(), 1);
    logx::Init((app::ExeDir() / "logs").string(), app::kGame.log);
    u64 total = 0, used = 0;
    svcGetInfo(&total, InfoType_TotalMemorySize, CUR_PROCESS_HANDLE, 0);
    svcGetInfo(&used, InfoType_UsedMemorySize, CUR_PROCESS_HANDLE, 0);
    u32 hos = hosversionGet();
    LOGI("switch: %s, folder %s, applet type %d, firmware %u.%u.%u, memory %llu MB of %llu MB used", kVersion,
         dir.c_str(), int(at), HOSVER_MAJOR(hos), HOSVER_MINOR(hos), HOSVER_MICRO(hos),
         (unsigned long long)(used >> 20), (unsigned long long)(total >> 20));

    std::ifstream f(game + "/" + IMAGE_XBE, std::ios::binary);
    std::vector<uint8_t> xbe((std::istreambuf_iterator<char>(f)), {});
    LOGI("switch: %s, %zu bytes, built for SHA-1 %s", IMAGE_XBE, xbe.size(), kImageSha1);
    if (app::kGame.init) app::kGame.init();
    if (!kernel::Boot(std::move(xbe), IMAGE_XBE, game, (app::ExeDir() / "hdd").string())) {
        logx::Flush();
        Fail("The game did not start", "See the newest file in " + dir + "/logs");
    }

    // the guest runs on its own threads; this one keeps the applet alive and the log on the card, and until the
    // game's first picture shows the newest log line, so a photo says how far it got
    std::string shown;
    while (appletMainLoop() && !sw::QuitRequested()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        logx::Flush();
        std::string last = logx::Last();
        if (last != shown && !sw::ShowingGame()) {
            sw::Message("Spider-Man 2 (Xbox) - test build", "Starting...\n\n" + last.substr(0, 76),
                        "Logs are written to " + dir + "/logs");
            shown = last;
        }
    }
    LOGI("switch: quitting");
    logx::Flush();
    sw::Close();
    std::_Exit(0);
}
