// The game's two languages: its text's (menus and subtitles) and its voices', each its own.
//
// One default.xbe serves the game's three editions, told apart by their disc's game.ini (0x001F3490): "SKU=Xbox-E"
// English, "SKU=Xbox-Multi" French, German or Spanish (the console's language picks one, French for any other) and
// "SKU=Xbox-It" Italian. The discs hold the text of all five languages, so the port hands the game a game.ini naming
// the edition of the language wanted (kernel::g_language, XC_LANGUAGE: 1 English, 3 German, 4 French, 5 Spanish,
// 6 Italian, set by the platform before boot: the console's, or the settings'), whichever edition the disc is.
//
// The voices are in sound banks of their own for each language (NAME_EN.WBK, NAME_FR.WBK, ...), their lip sync in the
// game's pack for all five. The US disc has only the English banks, the European ones others: French, German and
// Spanish on one, Italian on another. The game names the banks and the lip sync from its language (0x0021B2D0 the
// two letters, 0x0021B320 the whole name); here from the voices' language instead: the one set (SetVoiceLanguage), or
// with Auto the text's, when the copy of the game has its banks, else the copy's own. A bank still missing is opened
// in another language's place, and a movie only one edition has (NAME.XMV, or the 50 Hz NAME_PAL.XMV) in the other's.
#include "game/language.h"

#include <atomic>
#include <cstdlib>
#include <mutex>
#include <string>
#include <vector>

#include "core/log.h"
#include "cpu/x86_ops.h"
#include "kernel/kernel.h"

X86_EXTERN_FUNC(__imp__sub_0021B2D0);
X86_EXTERN_FUNC(__imp__sub_0021B320);

namespace sm2 {

namespace {

constexpr const char* kGameIni = "\\DEVICE\\CDROM0\\DATA\\GAME.INI";
constexpr const char* kStreams = "\\DEVICE\\CDROM0\\DATA\\SOUND\\XBOX\\STREAMS\\STREAMS_VOICE_";

struct Language {
    uint32_t code;       // XC_LANGUAGE
    uint32_t game;       // the game's own: 0 English, 1 pig latin (a test), 2 French, 3 German, 4 Spanish, 5 Italian
    const char* suffix;  // its sound banks'
    const char* name;
    const char* sku;     // the edition that shows its text
};
// in the order a copy's own voices are taken in: English, then the Fr/De/Es disc's first (its French), then Italian
constexpr Language kLanguages[] = {
    {1, 0, "EN", "English", "Xbox-E"},  {4, 2, "FR", "French", "Xbox-Multi"}, {3, 3, "DE", "German", "Xbox-Multi"},
    {5, 4, "ES", "Spanish", "Xbox-Multi"}, {6, 5, "IT", "Italian", "Xbox-It"},
};

const Language* Find(uint32_t code) {
    for (const Language& l : kLanguages)
        if (l.code == code) return &l;
    return nullptr;
}
const char* Name(uint32_t code) {
    const Language* l = Find(code);
    return l ? l->name : "another language";
}

uint32_t g_text = 1;                 // the text's language
uint32_t g_voices_set = 0;           // the voices' setting, 0 Auto
std::atomic<uint32_t> g_on_copy{0};  // a bit (1 << code) per language whose voices are on the copy
std::atomic<uint32_t> g_voices{1};   // the voices' language in use
std::once_flag g_looked;

uint32_t Resolve(uint32_t text, uint32_t set, uint32_t on) {
    uint32_t want = set ? set : text;
    if (!on || (on >> want & 1)) return want;
    for (const Language& l : kLanguages)
        if (on >> l.code & 1) return l.code;
    return want;
}

// once the game runs (the disc is its D: then): which voices the copy has, and so the ones spoken
void LookAtCopy() {
    std::call_once(g_looked, [] {
        uint32_t on = 0;
        std::string names;
        for (const Language& l : kLanguages) {
            if (!kernel::DiscFileExists(std::string(kStreams) + l.suffix + ".WBK")) continue;
            on |= 1u << l.code;
            names += std::string(names.empty() ? "" : ", ") + l.name;
        }
        uint32_t voices = Resolve(g_text, g_voices_set, on);
        g_on_copy = on;
        g_voices = voices;
        LOGI("sm2: voices on this copy of the game: %s; spoken: %s (%s)", names.empty() ? "none found" : names.c_str(), Name(voices),
             !g_voices_set ? (voices == g_text ? "Auto, the text's" : "Auto, the copy's own: not the text's")
             : voices == g_voices_set ? "as set"
                                      : "the copy's own: not the ones set");
    });
}

// game.ini: the SKU of the text's language, the rest as it is
bool SetEdition(std::vector<uint8_t>& bytes) {
    LookAtCopy();
    std::string s(bytes.begin(), bytes.end());
    const std::string want = Find(g_text)->sku;
    size_t at = 0;
    for (;;) {
        at = s.find("SKU=", at);
        if (at == std::string::npos || at == 0 || s[at - 1] == '\n') break;
        at += 4;
    }
    if (at == std::string::npos) {
        // none: the game's own default is the English edition
        size_t strings = s.find("[Strings]");
        size_t line = strings == std::string::npos ? 0 : s.find('\n', strings);
        line = line == std::string::npos ? s.size() : strings == std::string::npos ? 0 : line + 1;
        s.insert(line, (strings == std::string::npos ? "[Strings]\r\n" : "") + std::string("SKU=") + want + "\r\n");
        LOGI("sm2: the disc's game.ini names no edition: the %s one, for the text in %s", want.c_str(), Name(g_text));
    } else {
        size_t from = at + 4, end = s.find_first_of("\r\n", from);
        if (end == std::string::npos) end = s.size();
        std::string disc = s.substr(from, end - from);
        if (disc == want) {
            LOGI("sm2: the disc's edition, %s, shows the text in %s", disc.c_str(), Name(g_text));
            return false;
        }
        s.replace(from, end - from, want);
        LOGI("sm2: the disc's edition is %s: the %s one in its place, for the text in %s", disc.c_str(), want.c_str(), Name(g_text));
    }
    bytes.assign(s.begin(), s.end());
    return true;
}

bool EndsWith(const std::string& s, const std::string& tail) {
    return s.size() >= tail.size() && s.compare(s.size() - tail.size(), tail.size(), tail) == 0;
}

// a disc file the game asks for that isn't there: another language's sound bank, or the other edition's movie
std::string InPlaceOf(const std::string& path) {
    if (EndsWith(path, ".WBK") && path.size() > 7 && path[path.size() - 7] == '_') {
        LookAtCopy();
        const std::string stem = path.substr(0, path.size() - 6);
        std::vector<uint32_t> order = {g_voices.load()};
        for (const Language& l : kLanguages) order.push_back(l.code);
        for (uint32_t code : order) {
            std::string alt = stem + Find(code)->suffix + ".WBK";
            if (alt != path && kernel::DiscFileExists(alt)) return alt;
        }
        return {};
    }
    if (EndsWith(path, "_PAL.XMV")) return path.substr(0, path.size() - 8) + ".XMV";
    if (EndsWith(path, ".XMV")) return path.substr(0, path.size() - 4) + "_PAL.XMV";
    return {};
}

// the game's language number the voices' names are made with
uint32_t VoicesGame() {
    LookAtCopy();
    return Find(g_voices.load())->game;
}

}  // namespace

void SetVoiceLanguage(uint32_t code) { g_voices_set = Find(code) ? code : 0; }

uint32_t VoicesOnCopy() { return g_on_copy.load(); }

uint32_t VoicesFor(uint32_t text, uint32_t set) { return Resolve(Find(text) ? text : 1, Find(set) ? set : 0, g_on_copy.load()); }

// before boot
void SetUpLanguage() {
    uint32_t text = kernel::g_language;
    if (const char* e = std::getenv("XBE_LANGUAGE")) text = uint32_t(std::atoi(e));  // test builds
    if (!Find(text)) text = 1;  // the game's languages
    if (const char* e = std::getenv("XBE_VOICES")) SetVoiceLanguage(uint32_t(std::atoi(e)));
    kernel::g_language = text;
    g_text = text;
    g_voices = Resolve(text, g_voices_set, 0);
    LOGI("sm2: the text in %s, the voices %s%s", Name(text), g_voices_set ? "in " : "Auto",
         g_voices_set ? Name(g_voices_set) : "");
    kernel::RewriteDiscFile(kGameIni, SetEdition);
    kernel::g_missing_disc_file = InPlaceOf;
}

}  // namespace sm2

// the two letters of a language (a sound bank's NAME_EN.WBK, a line's lip sync) and its whole name (ENGLISH, the
// sound's tables): the voices' language for the game's own
X86_FUNC(sub_0021B2D0) {
    const uint32_t at = ctx.esp + 4, set = X86_MEM32(at);
    if (set <= 5) X86_MEM32(at) = sm2::VoicesGame();
    __imp__sub_0021B2D0(ctx, base);
    X86_MEM32(at) = set;
}

X86_FUNC(sub_0021B320) {
    const uint32_t at = ctx.esp + 4, set = X86_MEM32(at);
    if (set <= 5) X86_MEM32(at) = sm2::VoicesGame();
    __imp__sub_0021B320(ctx, base);
    X86_MEM32(at) = set;
}
