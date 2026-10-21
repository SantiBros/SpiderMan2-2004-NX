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
    bool same_release = false;  // the xbe this build was recompiled from
    bool same_entry = false;    // an xbe with the recompiled entry point: a near release that may run
};

struct GameSearch {
    std::vector<GameCopy> copies;      // the same release first; folders before images, then by name
    std::vector<std::string> skipped;  // "name: why", files that look like a copy of the game but are not usable
};

// in each folder: default.xbe, then the disc images (a split one counted once, by its first part)
GameSearch FindGameCopies(const std::vector<std::filesystem::path>& folders, const char* xbe_name, const std::string& sha1);

std::string Sha1Hex(const uint8_t* data, size_t n);

}  // namespace kernel
