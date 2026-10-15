// The places for things on the ground, which Dropped Item Limit makes more of when the game starts.
#pragma once
#include <cstdint>
#include <wots4/mod_api.h>
#include "../game_code.h"

namespace tweaks::ground_slots {

constexpr uint32_t gamePlaceCount = 60;

// Checks and claims the game's code. False, with the reason logged, if it is not the known version.
bool install(const wots4::ModContext& context, const GameImage& game, const TweakLog& log);
// Call once, as the game starts: gives the ground placeCount places (a multiple of 30) from then on.
// Does nothing for the game's 60, or if the game has already made its places (logged).
void applyAtStartup(uint32_t placeCount);

}  // namespace tweaks::ground_slots
