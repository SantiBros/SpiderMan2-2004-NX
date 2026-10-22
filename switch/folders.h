#pragma once
#include <cstdint>
#include <filesystem>

// The port's folders beside the .nro, as the NFS Most Wanted port lays them out:
//   save/progress/   the game's saves (the Xbox's E:\UDATA\<title>): what to back up
//   save/data/       what the game keeps for itself on the Xbox's hard disk (E:\TDATA\<title>)
//   cache/           the rest of the Xbox's hard disk (its cache drives) and the shader list: safe to delete
// Versions before 0.1.0 kept everything in hdd/, and shaders.cache beside the .nro: they are moved at the first start.
namespace sw {

struct Folders {
    std::filesystem::path saves, title_data, cache;
};

// never deletes anything: a save that can't be moved stays where it was and is used there
Folders SetUpFolders(const std::filesystem::path& dir, uint32_t title_id);

}  // namespace sw
