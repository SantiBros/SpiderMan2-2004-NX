#pragma once
#include "switch/folders.h"

// A copy of the saves outside the port's folder, in sdmc:/config/SpiderMan2Xbox/save/ (progress/ and data/, as in
// save/), kept up to date while the game runs. An update that replaces the SpiderMan2Xbox folder, or a port started
// from another folder, would otherwise start with no saves: at start, when save/progress has none and the copy has,
// the copy is put back.
namespace sw {

// logs the saves found; puts the copy back when save/progress has none. True when it did.
bool RestoreSaves(const Folders& f);
// a thread that copies the saves a few seconds after the game last changed them (kernel::SaveChanges)
void StartSaveBackup(const Folders& f);
// the copy brought up to date now if the saves changed since (when the port quits)
void BackUpSavesNow();

}  // namespace sw
