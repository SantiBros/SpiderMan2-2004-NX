// Nintendo Switch entry: checks the launch mode and finds the game, then boots the recompiled image.
// Layout on the SD card (the folder of this .nro):
//   SpiderMan2Xbox.nro
//   game/                                the player's own copy, one of:
//     a disc image                         .iso .xiso .cci .cso, whole or split (name.1.iso, name.2.iso, ...)
//     default.xbe and data/...             the disc's files
//   save/progress/                       the game's saves (the Xbox's E:\UDATA\<title>), made on the first run
//   save/data/                           the game's own data on the Xbox's hard disk (E:\TDATA\<title>)
//   cache/                               the rest of the Xbox's hard disk and the shader list: safe to delete
//   logs/                                sm2_NNN.log, what to send back when something goes wrong
//   settings.ini                         the debug menu's settings (ZL + ZR + -)
// and a copy of save/ in sdmc:/config/SpiderMan2Xbox/save/, kept up to date (switch/saves)
#include <switch.h>

#include <unistd.h>

#include <chrono>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
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
#include "switch/clocks.h"
#include "switch/folders.h"
#include "switch/menu.h"
#include "switch/rumble.h"
#include "switch/saves.h"
#include "switch/screen.h"
#include "switch/settings.h"
#include "switch/state.h"

namespace fs = std::filesystem;

extern const char kImageSha1[];
// from the VERSION file, through switch/build.sh
#ifndef SM2NX_VERSION
#define SM2NX_VERSION "dev"
#endif
constexpr const char* kVersion = SM2NX_VERSION;

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
    sw::clocks::Exit();
    sw::Close();
    std::exit(1);
}

// a question on the screen: A goes on, + goes back to the Homebrew Menu
bool Ask(const char* title, const std::string& detail) {
    sw::Message(title, detail, "Press A to start it anyway, + to go back to the Homebrew Menu.");
    PadState pad;
    padInitializeDefault(&pad);
    while (appletMainLoop()) {
        sw::Pump();
        padUpdate(&pad);
        uint64_t down = padGetButtonsDown(&pad);
        if (down & HidNpadButton_A) return true;
        if (down & HidNpadButton_Plus) break;
        svcSleepThread(16'000'000);
    }
    logx::Sync();
    sw::clocks::Exit();
    sw::Close();
    std::exit(1);
}

// the console's language as the game's code (XC_LANGUAGE: 1 English, 3 German, 4 French, 5 Spanish, 6 Italian),
// 0 for one the game doesn't have
uint32_t ConsoleLanguage(std::string& name) {
    uint32_t code = 0;
    name = "unknown";
    if (R_FAILED(setInitialize())) return code;
    u64 lc = 0;
    SetLanguage lang;
    if (R_SUCCEEDED(setGetSystemLanguage(&lc)) && R_SUCCEEDED(setMakeLanguage(lc, &lang))) {
        name.assign(reinterpret_cast<const char*>(&lc), strnlen(reinterpret_cast<const char*>(&lc), 8));
        switch (lang) {
            case SetLanguage_ENUS:
            case SetLanguage_ENGB: code = 1; break;
            case SetLanguage_FR:
            case SetLanguage_FRCA: code = 4; break;
            case SetLanguage_DE: code = 3; break;
            case SetLanguage_IT: code = 6; break;
            case SetLanguage_ES:
            case SetLanguage_ES419: code = 5; break;
            default: break;
        }
    }
    setExit();
    return code;
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
    // a message when the port goes to the HOME menu, to give the system its clocks back (sw::clocks)
    appletSetFocusHandlingMode(AppletFocusHandlingMode_SuspendHomeSleepNotify);
    sw::LoadSettings();
    const sw::Settings& settings = sw::StartSettings();
    // held at start, for this start only: L no sound, R the CPU renderer, ZR the GPU renderer on the game's own thread
    uint64_t held = HeldAtStart();
    sw::g_no_sound = held & HidNpadButton_L;
    sw::menu::ApplyPicture(settings);
    sw::Init(settings.gpu_renderer && !(held & HidNpadButton_R));
    gpu::g_render_thread = sw::UsingGl() && settings.render_thread && !(held & HidNpadButton_ZR);

    AppletType at = appletGetAppletType();
    if (at != AppletType_Application && at != AppletType_SystemApplication)
        Fail("Not enough memory in this mode",
             "This was started from the Album, which gives homebrew only a small part of the Switch's memory.\n"
             "The game needs about 1.3 GB.\n\n"
             "Open the Homebrew Menu by holding R while you start any game, then start this again.");

    const std::string title = std::string("Spider-Man 2 (Xbox) for Switch ") + kVersion;
    const std::string footer = "Quit: hold + and - together.  Logs: " + dir + "/logs";
    const std::string start_title = "SPIDER-MAN 2";
    const std::string start_footer = std::string(kVersion) + "     Debug menu: hold ZL + ZR, press -     Quit: hold + and -";
    sw::Loading(start_title, "Looking for the game...", start_footer);
    sw::Pump();

    std::string game = dir + "/game";
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
    LOGI("switch: core mask %llX, sound %s, built for %s SHA-1 %s", (unsigned long long)cores,
         sw::g_no_sound ? "off (L held)" : "on", IMAGE_XBE, kImageSha1);
    LOGI("switch: screen %s, %s", sw::ScreenInfo().c_str(),
         gpu::g_render_thread ? "render thread"
         : !sw::UsingGl()     ? "no render thread"
         : (held & HidNpadButton_ZR) ? "no render thread (ZR held)"
                                     : "no render thread (settings)");
    // the game's language: the settings', or the console's
    std::string console_name;
    uint32_t console = ConsoleLanguage(console_name);
    sw::menu::SetConsoleLanguage(console ? console : 1);
    kernel::g_language = settings.language ? settings.language : console ? console : 1;
    LOGI("switch: console language %s, %s", console_name.c_str(), settings.language ? "the game's set in settings.ini" : "the game follows it");
    LOGI("switch: settings: language %u, %up, screen %d, upscaling %s, anisotropic %ux, rumble %d, clocks CPU %u GPU %u "
         "memory %u MHz (0 stock, 4294 auto), counter %d, volume %u%%, %s renderer, render thread %s, start-up log %s", settings.language,
         settings.resolution, int(settings.screen), settings.sharp ? "sharp" : "smooth", settings.anisotropy, int(settings.rumble),
         settings.cpu_hz / 1000000, settings.gpu_hz / 1000000, settings.mem_hz / 1000000, int(settings.counter),
         settings.volume, settings.gpu_renderer ? "GPU" : "CPU", settings.render_thread ? "on" : "off",
         settings.startup_log ? "on" : "off");

    // the player's copy: in game/, or beside the .nro
    kernel::GameSearch search = kernel::FindGameCopies({game, dir}, IMAGE_XBE, kImageSha1);
    std::string skipped;
    for (size_t i = 0; i < search.skipped.size() && i < 6; i++) skipped += "\n- " + search.skipped[i];
    if (search.copies.empty())
        Fail("Game not found",
             "Put your own copy of the game in\n  " + game +
                 "\nas one of these:\n"
                 "- a disc image: .iso, .xiso, .cci or .cso, also split for a FAT32\n"
                 "  card (name.1.iso, name.2.iso, ...: put all the parts there)\n"
                 "- the disc's files: default.xbe and the data folder" +
                 (skipped.empty() ? std::string() : "\n\nThese files are there but cannot be used:" + skipped));
    kernel::GameCopy& copy = search.copies[0];
    if (!copy.same_release) {
        char id[16];
        std::snprintf(id, sizeof(id), "%08X", copy.title_id);
        if (!copy.same_entry)
            Fail("Another game or version",
                 copy.name + " holds \"" + copy.title + "\" (title " + id +
                     "),\nnot the release of Spider-Man 2 this build was made from.\n\n"
                     "This build runs only the game whose default.xbe has the SHA-1\n  " +
                     kImageSha1 + "\nThis one's is\n  " + copy.sha1);
        if (!Ask("A different release of the game",
                 copy.name + " holds another release of Spider-Man 2 than the one this\n"
                             "build was made from, and will most likely crash.\n\n"
                             "Its default.xbe SHA-1:   " + copy.sha1 + "\nthe build's:            " + kImageSha1))
            return 1;
    }
    if (!copy.disc && !fs::is_directory(copy.folder / "data")) {
        std::string g = copy.folder.string();
        bool flat = fs::exists(copy.folder / "game.ini") || fs::is_directory(copy.folder / "packs") ||
                    fs::is_directory(copy.folder / "PACKS");
        Fail("The data folder is missing",
             flat ? "The contents of the data folder are directly in\n\n  " + g +
                        "\n\nMake a folder named data there and move game.ini, movies, packs\nand sound into it, so you "
                        "have:\n\n  " + g + "/default.xbe\n  " + g + "/data/game.ini\n  " + g + "/data/packs/...\n  " + g +
                        "/data/sound/..."
                  : "Copy the data folder from your disc here:\n\n  " + g + "/data\n\n(next to default.xbe, with "
                    "game.ini, movies, packs and sound inside)");
    }
    LOGI("switch: the game from %s (%s)", copy.name.c_str(), copy.kind.c_str());
    sw::Loading(start_title, "Loading...", start_footer);
    sw::Pump();

    // the saves in save/, the rest of the Xbox's hard disk in cache/ (hdd/ before 0.1.0, moved there)
    sw::Folders folders = sw::SetUpFolders(app::ExeDir(), copy.title_id);
    // an update that replaced this folder took save/ with it: the copy kept in sdmc:/config puts it back
    if (sw::RestoreSaves(folders)) {
        sw::Loading(start_title, "Your saves were put back from their copy. Loading...", start_footer);
        sw::Pump();
    }
    kernel::UseSaveFolders(copy.title_id, folders.saves.string(), folders.title_data.string());

    if (copy.disc) kernel::MountDisc(std::move(copy.disc));
    if (app::kGame.init) app::kGame.init();
    if (!kernel::Boot(std::move(copy.xbe), IMAGE_XBE, copy.folder.string(), folders.cache.string()))
        Fail("The game did not start", "See the newest file in " + dir + "/logs");
    sw::StartSaveBackup(folders);

    // the guest runs on its own threads. This one keeps the applet alive, runs the debug menu and puts things on
    // screen: until the game draws its first picture, "Loading...", or with the start-up log on, the newest log
    // lines, so a photo says how far it got. It takes no lock a game thread could hold for long, so quitting works
    // even when the game hangs.
    uint64_t last_text = 0;
    kernel::NameHostThread("main");  // the menu and the applet, off the game's core (it is above the game's priority)
    while (appletMainLoop() && !sw::QuitRequested()) {
        svcSleepThread(16'000'000);
        sw::menu::Update();
        uint64_t now = armTicksToNs(armGetSystemTick());
        if (settings.startup_log && !sw::ShowingGame() && now - last_text > 250'000'000ull) {
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
    sw::rumble::Stop();
    // what the game was still writing onto the card, then the saves' copy brought up to date
    kernel::FlushOpenFiles();
    sw::BackUpSavesNow();
    sw::clocks::Exit();
    logx::Sync();  // the lines not on the card yet (the watch thread puts them there every 250 ms)
    sw::Close();
    std::_Exit(0);
}
