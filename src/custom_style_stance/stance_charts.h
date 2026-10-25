// The move cards of the other stances of a slot's weapon group, and the common chart of every
// stance's defence moves (common_chart.h). The game keeps one chart of cards per slot
// (SharedData::slotCharts), made for the slot's stance. This tweak keeps one more chart per other
// stance of the group and the common one, and shows one of them in place of the game's chart
// while the move select screen is on its menu or its cards. Everywhere else the game sees its
// own charts. Cards chosen on our charts stay chosen until the slot's stance changes or the
// screens close.
#pragma once
#include "my_style_screen.h"

namespace tweaks::custom_style_stance {

// The charts the edited slot can show, in the order listed: the stances of its weapon group
// (or its stance alone), then the common chart.
constexpr int largestGroupSize = 7;
struct StanceChoice {
    int charts[largestGroupSize + 1];  // A stance, or commonChart
    int count;
    int shown;  // Its place in charts
};
// False when the edited slot has no stance.
bool stanceChoiceOf(void* screen, StanceChoice& choice);
// Shows the cards of a stance of the edited slot's weapon group, or the common chart.
void showStance(void* screen, int chartKind);
// Puts back the game's chart of the edited slot and its stance title. Called before the menu
// is left for anything but the cards, so the game's other steps see only its own charts.
void showGameChart(void* screen);
// Before the shared data is freed (0x5c9ea0): gives the game its charts back, deletes ours.
void releaseCharts(void* shared);
// The name of a stance (the game's) or of the common chart.
const wchar_t* chartName(int chartKind);

// A chart of a slot and what its cards are: a stance, or commonChart.
struct StanceChart {
    void* chart;
    int stance;
};
// Every chart that holds cards chosen for the slot: the game's, then the tweak's of the other
// stances and the common one. Charts made for a stance the slot no longer has are left out.
int chartsOfSlot(void* shared, int slot, StanceChart (&charts)[chartKindCount]);
// The other charts of the slot that `chart` belongs to, and that slot's stance. 0 when the
// chart is no slot's or the slot has no other chart.
int chartsBesides(void* chart, StanceChart (&others)[chartKindCount], int& slotStance);

}  // namespace tweaks::custom_style_stance
