// The game's language follows the console's (kernel::g_language, XC_LANGUAGE: 1 English, 3 German, 4 French,
// 5 Spanish, 6 Italian), set by the platform before boot.
//
// The disc holds the game's text in English, French, German, Italian and Spanish, but its game.ini names the
// English SKU ("SKU=Xbox-E"), with which the game shows English whatever the console says. The multi-language
// SKU ("Xbox-Multi") makes it take the console's language, so for another language the port hands the game a
// game.ini saying that. The disc has only the English voices and songs: the other languages' sound banks
// (NAME_ES.WBK and so on) are opened as the English ones (NAME_EN.WBK), so the game speaks English under the
// translated text, as the disc allows.
#include <string>
#include <vector>

#include "core/log.h"
#include "kernel/kernel.h"

namespace sm2 {

namespace {

constexpr const char* kGameIni = "\\DEVICE\\CDROM0\\DATA\\GAME.INI";

// "SKU=Xbox-E" to "SKU=Xbox-Multi", the rest as it is
bool MultiLanguageSku(std::vector<uint8_t>& bytes) {
    std::string s(bytes.begin(), bytes.end());
    const std::string from = "SKU=Xbox-E", to = "SKU=Xbox-Multi";
    size_t at = s.find(from);
    if (at == std::string::npos) return false;
    size_t end = at + from.size();
    if (end < s.size() && s[end] != '\r' && s[end] != '\n') return false;  // another SKU name that starts the same
    s.replace(at, from.size(), to);
    bytes.assign(s.begin(), s.end());
    return true;
}

// a sound bank of another language than English: the English one
std::string EnglishSoundBank(const std::string& device_path) {
    static const char* const kSuffixes[] = {"_ES.WBK", "_FR.WBK", "_DE.WBK", "_IT.WBK"};
    for (const char* suffix : kSuffixes) {
        size_t n = std::char_traits<char>::length(suffix);
        if (device_path.size() > n && device_path.compare(device_path.size() - n, n, suffix) == 0)
            return device_path.substr(0, device_path.size() - n) + "_EN.WBK";
    }
    return {};
}

const char* Name(uint32_t language) {
    switch (language) {
        case 1: return "English";
        case 3: return "German";
        case 4: return "French";
        case 5: return "Spanish";
        case 6: return "Italian";
        default: return "another language";
    }
}

}  // namespace

// before boot
void SetUpLanguage() {
    uint32_t language = kernel::g_language;
    if (language != 3 && language != 4 && language != 5 && language != 6) language = 1;  // the disc's languages
    kernel::g_language = language;
    LOGI("sm2: the game in %s", Name(language));
    if (language == 1) return;
    kernel::RewriteDiscFile(kGameIni, MultiLanguageSku);
    kernel::g_missing_disc_file = EnglishSoundBank;
}

}  // namespace sm2
