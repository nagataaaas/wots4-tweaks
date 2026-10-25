// The common chart: the defence moves of every stance (parries, Toughen, eating and drinking,
// Immovable Heart) in one chart that every slot can show (user request 2026-10-25).
// The game makes a chart from one stance's SkillComboData; the common chart is made from a
// SkillComboData of the tweak's own, with one move per defence move (SkillData +8), copied from
// a stance that has it. A card can be chosen when the move was learned in any stance.
#pragma once
#include "../game_code.h"
#include "my_style_screen.h"

namespace tweaks::custom_style_stance {

bool installCommonChart(const wots4::ModContext& context, const GameImage& game, const TweakLog& log);
void setCommonChartEnabled(bool enabled, const TweakLog& log);

// Makes a new, empty chart the common chart of the slot, whose stance is slotStance, so that
// the move select's filling (buildShownChart) fills it with the common moves. False when no
// stance has a defence move (the combos are not loaded).
bool prepareCommonChart(void* chart, int slot, int slotStance);
// Called once the slot's common chart is deleted.
void forgetCommonChart(int slot);

// "共通" or "Common", in the language of the game's stance names.
const wchar_t* commonChartName();

}  // namespace tweaks::custom_style_stance
