#pragma once
// Where the player's copy of the game is: the files from the disc (default.xbe with its data beside it), or a disc
// image in any format image.h reads (.iso, .xiso, .cci, .cso, whole or in parts). The copy whose default.xbe is the
// one this build was recompiled from is the one to run: another release would run its own data under this code.
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "kernel/disc.h"

namespace kernel {

struct GameCopy {
    std::filesystem::path folder;  // the game's folder: D: when there is no disc image, else the image's
    std::unique_ptr<Disc> disc;    // the disc image D: is read from, null for a folder
    std::vector<uint8_t> xbe;      // its default.xbe
    std::string name;              // the folder or the image's file name
    std::string kind;              // "folder", "ISO", "CCI in 2 parts, whole disc", ...
    std::string sha1;              // of the xbe, lower case hex
    std::string title;             // the xbe's title
    uint32_t title_id = 0;
    std::string code;              // the SHA-1 of its game code (xbe/code.h), lower case hex
    std::string regions;           // the certificate's: "North America", "Europe and the rest of the world", ...
    uint32_t version = 0;          // the certificate's
    bool same_release = false;  // the game code this build was recompiled from: its xbe, or another edition's of it
    bool other_edition = false; // that code in another edition's xbe (another header and certificate), or an
                                // edition built apart made into the supported xbe by its patch (edition)
    std::string edition;        // that patched edition's name ("Europe (French, German, Spanish)"); xbe is then the
                                // supported one, sha1 and code still the copy's own
    bool same_entry = false;    // an xbe with the recompiled entry point: a near release that may run
};

struct GameSearch {
    std::vector<GameCopy> copies;      // the same release first; folders before images, then by name
    std::vector<std::string> skipped;  // "name: why", files that look like a copy of the game but are not usable
};

// An edition of the game built apart from the supported one (another version of its code) whose disc's data the
// supported code runs: its default.xbe is made into the supported one by a patch (tools/edition_patch.py).
struct EditionPatch {
    const uint8_t* data = nullptr;
    size_t size = 0;
};
// the patch's edition name, and whether it is for this default.xbe (its SHA-1)
std::string EditionName(const EditionPatch& patch);
bool EditionPatchFits(const EditionPatch& patch, const std::vector<uint8_t>& xbe);
// the supported default.xbe made from the edition's: false (and why) for another xbe or a damaged patch
bool ApplyEditionPatch(const EditionPatch& patch, const std::vector<uint8_t>& xbe, std::vector<uint8_t>& out, std::string& error);

// in each folder: default.xbe, then the disc images (a split one counted once, by its first part). sha1: of the xbe
// this build was recompiled from; code: the SHA-1 of its game code, which another edition's xbe may share; editions:
// the patches that make other editions' xbe into it
GameSearch FindGameCopies(const std::vector<std::filesystem::path>& folders, const char* xbe_name, const std::string& sha1,
                          const std::string& code = {}, const std::vector<EditionPatch>& editions = {});

std::string Sha1Hex(const uint8_t* data, size_t n);

}  // namespace kernel
