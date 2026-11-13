#pragma once
#include <cstdint>
#include <string>
#include <vector>

// Updates from inside the port: the debug menu's Update row. GitHub is asked for the newest release; a newer one's
// locked .nro is downloaded from the release (as a browser does, so GitHub counts the download), unlocked on the
// console with the game's own default.xbe as the installer page does, and put in place of this .nro, the old one
// kept as SpiderMan2Xbox.nro.old. Then the port starts again with it. Saves and settings are left alone.
namespace sw::update {

enum class State { Off, Idle, Checking, UpToDate, Available, Downloading, Installed, Failed };

// at start: the game's default.xbe (its game code is the key a release's locked .nro opens with: only the supported
// game code's, whatever its edition) and this .nro's path (argv[0])
void SetGame(const std::vector<uint8_t>& xbe, bool supported);
void SetNro(const std::string& path);

State Current();
std::string RowText();   // the menu row's value
std::string HelpText();  // what it is doing or found, for the menu's help
uint32_t Changes();      // goes up when the menu has something new to show

// A on the menu row: check, install what was found, or start the port again with what was installed
void Act();
// the port is to quit and start the installed .nro: StartNext() just before it quits
bool RestartRequested();
void StartNext();

}  // namespace sw::update
