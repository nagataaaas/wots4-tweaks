// What a created style holding other stances' moves (stance_rules.h) needs outside the My Style
// screens: the motions of those moves, registered with the stance's own when a character takes
// the style, and the per-stance record of the moves seen and used, kept under the moves' own
// stances and numbers.
#pragma once
#include "../game_code.h"

namespace tweaks::custom_style_stance {

// Checks the code and hooks it. Styles without other stances' moves are left to the game. On
// failure nothing stays hooked.
bool installMixedStyle(const wots4::ModContext& context, const GameImage& game, const TweakLog& log);
void uninstallMixedStyle(const GameImage& game);

}  // namespace tweaks::custom_style_stance
