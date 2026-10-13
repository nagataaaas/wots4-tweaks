// The move cards of the other stances of a slot's weapon group. The game keeps one chart of cards
// per slot (SharedData::slotCharts), made for the slot's stance. This tweak keeps one more chart
// per other stance of the group and shows one of them in place of the game's chart while the
// move select screen is on its menu or its cards. Everywhere else the game sees its own charts.
// Cards chosen on our charts stay chosen until the slot's stance changes or the screens close.
#pragma once
#include "my_style_screen.h"

namespace tweaks::custom_style_stance {

// The stances the edited slot can show: those of its weapon group, first to last.
struct StanceChoice {
    int first;
    int last;
    int shown;
};
constexpr int largestGroupSize = 7;
// False when the edited slot's stance belongs to no group whose stances can be mixed (sword 0-6,
// dual swords 8-9).
bool stanceChoiceOf(void* screen, StanceChoice& choice);
// Shows the cards of a stance of the edited slot's weapon group.
void showStance(void* screen, int stance);
// Puts back the game's chart of the edited slot and its stance title. Called before the menu
// is left for anything but the cards, so the game's other steps see only its own charts.
void showGameChart(void* screen);
// Before the shared data is freed (0x5c9ea0): gives the game its charts back, deletes ours.
void releaseCharts(void* shared);

// A chart of a slot and the stance of its cards.
struct StanceChart {
    void* chart;
    int stance;
};
// Every chart that holds cards chosen for the slot: the game's, then the tweak's of the other
// stances. Charts made for a stance the slot no longer has are left out.
int chartsOfSlot(void* shared, int slot, StanceChart (&charts)[stanceCount]);
// The other charts of the slot that `chart` belongs to, and that slot's stance. 0 when the
// chart is no slot's or the slot has no other chart.
int chartsBesides(void* chart, StanceChart (&others)[stanceCount], int& slotStance);

}  // namespace tweaks::custom_style_stance
