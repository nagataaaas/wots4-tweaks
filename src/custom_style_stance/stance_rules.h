// The style's rules over the cards chosen on all the charts of a slot (stance_charts.h), not
// only the one the game sees: the ability points, a first move's command or a defence move's
// type taken twice (UI 706, 744, and the cards dimmed for it), a stance without attack moves (UI 725), and the moves put
// into the style when it is created.
#pragma once
#include "../game_code.h"

namespace tweaks::custom_style_stance {

// Checks the code and hooks it. The hooks change nothing while the slots have only the game's
// charts. On failure nothing stays hooked or patched.
bool installStanceRules(const wots4::ModContext& context, const GameImage& game, const TweakLog& log);
void uninstallStanceRules(const GameImage& game);
// Dims the chart's free first moves whose command or defence type a chosen card of the slot's
// other charts takes, as the game dims those taken in the chart, and brightens them
// again once nothing takes them. Called before a chart is shown.
void dimCardsTakenElsewhere(void* chart);
// Writes the rules' code patches, or the game's bytes back.
void setStanceRulesEnabled(bool enabled, const TweakLog& log);

}  // namespace tweaks::custom_style_stance
