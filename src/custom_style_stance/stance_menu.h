// A fifth button, "Switch Stance", on the menu of the My Style move select screen (Move Select,
// Auto-Select, Copy Stance, Finish). Each button is a layout of its own (wazaFunc.layout) with
// two animations, loaded from the screen's layout list; the tweak gives the screen a copy of the
// list with one more button and makes the menu's loops go over five buttons.
#pragma once
#include "../game_code.h"

namespace tweaks::custom_style_stance {

// Checks the menu's code and hooks it. The hooks act only on screens made with the tweak's list.
bool installStanceMenu(const wots4::ModContext& context, const GameImage& game, const TweakLog& log);
// Makes the next move select screens have the fifth button, or not. The screen is made when the
// My Style screens open, and the options cannot be changed while they are open.
void setStanceMenuEnabled(bool enabled, const TweakLog& log);

}  // namespace tweaks::custom_style_stance
