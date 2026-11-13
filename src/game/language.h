#pragma once
#include <cstdint>

// The game's two languages, its text's and its voices' (src/game/language.cpp). Codes as the Xbox's XC_LANGUAGE:
// 1 English, 3 German, 4 French, 5 Spanish, 6 Italian.
namespace sm2 {

// before boot: the voices' language, 0 Auto (the text's when the player's copy has those voices, else the copy's own)
void SetVoiceLanguage(uint32_t code);
// the languages whose voices the player's copy of the game has, a bit (1 << code) each; 0 until the game opens its disc
uint32_t VoicesOnCopy();
// the voices a setting (0 Auto) gives with that text language, on the copy as far as it is known yet
uint32_t VoicesFor(uint32_t text, uint32_t set);

}  // namespace sm2
