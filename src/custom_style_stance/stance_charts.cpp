#include "stance_charts.h"
#include <cstring>
#include "game_calls.h"
#include "my_style_screen.h"
#include "stance_rules.h"

namespace tweaks::custom_style_stance {

namespace {

struct WeaponGroup {
    int16_t first;
    int16_t last;
};
// Sword (Jo-dan .. Shinobi) and dual swords (Dual Arms, Shinobi Dual Arms) (user decision 2026-10-13).
constexpr WeaponGroup mixableGroups[] = {{0, 6}, {8, 9}};

constexpr bool groupsAreSmallEnough() {
    for (const WeaponGroup& group : mixableGroups) {
        if (group.last - group.first + 1 > largestGroupSize) return false;
    }
    return true;
}
static_assert(groupsAreSmallEnough(), "largestGroupSize is the size of the largest group");

struct SlotCharts {
    int16_t slotStance = noStance;  // The stance the charts were made for
    int16_t shownStance = noStance;
    void* gameChart = nullptr;       // The game's chart of the slot, put back when leaving the menu
    void* charts[stanceCount] = {};  // Ours; the slot's own stance has none
};

void* knownShared = nullptr;  // The shared data the slots below belong to
SlotCharts slots[slotCount];

const WeaponGroup* groupOf(int stance) {
    for (const WeaponGroup& group : mixableGroups) {
        if (stance >= group.first && stance <= group.last) return &group;
    }
    return nullptr;
}

void* sharedOf(void* screen) {
    return field<void*>(screen, MoveSelectScreen::shared);
}

int editedSlot(void* shared) {
    return field<int16_t>(shared, SharedData::editedSlot);
}

int16_t& slotStance(void* shared, int slot) {
    return field<int16_t>(shared, SharedData::slotStances + slot * sizeof(int16_t));
}

void*& slotChart(void* shared, int slot) {
    return field<void*>(shared, SharedData::slotCharts + slot * sizeof(void*));
}

void deleteOurCharts(SlotCharts& charts) {
    for (void*& chart : charts.charts) {
        if (!chart) continue;
        deleteChart(chart);
        chart = nullptr;
    }
}

// The charts of the edited slot, emptied when the shared data or the slot's stance changed.
// The slot's stance changes only on the stance select screen, while the game has its own charts
// (showGameChart runs whenever the menu is left), so the slot's chart is the game's then.
SlotCharts* chartsOfEditedSlot(void* screen) {
    void* shared = sharedOf(screen);
    const int slot = editedSlot(shared);
    if (slot < 0 || slot >= slotCount) return nullptr;
    if (shared != knownShared) {
        // The previous shared data was released through releaseCharts.
        for (SlotCharts& charts : slots) charts = SlotCharts();
        knownShared = shared;
    }
    SlotCharts& charts = slots[slot];
    const int16_t stance = slotStance(shared, slot);
    if (charts.slotStance != stance || !charts.gameChart) {
        deleteOurCharts(charts);
        charts.slotStance = stance;
        charts.shownStance = stance;
        charts.gameChart = slotChart(shared, slot);
    }
    return &charts;
}

void setStanceTitle(void* screen, int stance) {
    wchar_t* title = &field<wchar_t>(screen, MoveSelectScreen::stanceTitle);
    const wchar_t* name = stanceName(stance);
    if (!name) return;
    wcsncpy_s(title, stanceTitleLength, name, _TRUNCATE);
}

// Our chart of the stance, made and filled the first time it is shown. The game fills a chart
// for the edited slot's stance (0x5cbef0), so the slot's stance is set to the chart's for that.
void* chartOf(void* screen, SlotCharts& charts, int stance) {
    void*& chart = charts.charts[stance];
    if (chart) return chart;
    chart = newChart();
    if (!chart) return nullptr;
    void* shared = sharedOf(screen);
    int16_t& stanceOfSlot = slotStance(shared, editedSlot(shared));
    void*& shownChart = field<void*>(screen, MoveSelectScreen::shownChart);
    void* const gameChart = shownChart;
    stanceOfSlot = static_cast<int16_t>(stance);
    shownChart = chart;
    buildShownChart(screen);
    stanceOfSlot = charts.slotStance;
    shownChart = gameChart;
    return chart;
}

// Draws and edits the chart in place of the game's one of the edited slot.
void show(void* screen, void* chart, int stance) {
    dimCardsTakenElsewhere(chart);
    void* shared = sharedOf(screen);
    slotChart(shared, editedSlot(shared)) = chart;
    field<void*>(screen, MoveSelectScreen::shownChart) = chart;
    setStanceTitle(screen, stance);
    refreshCardInformation(screen);
}

}  // namespace

bool stanceChoiceOf(void* screen, StanceChoice& choice) {
    SlotCharts* charts = chartsOfEditedSlot(screen);
    if (!charts) return false;
    const WeaponGroup* group = groupOf(charts->slotStance);
    if (!group) return false;
    choice = {group->first, group->last, charts->shownStance};
    return true;
}

void showStance(void* screen, int stance) {
    SlotCharts* charts = chartsOfEditedSlot(screen);
    if (!charts || stance == charts->shownStance) return;
    const WeaponGroup* group = groupOf(charts->slotStance);
    if (!group || stance < group->first || stance > group->last) return;
    void* chart = stance == charts->slotStance ? charts->gameChart : chartOf(screen, *charts, stance);
    if (!chart) return;
    charts->shownStance = static_cast<int16_t>(stance);
    show(screen, chart, stance);
}

void showGameChart(void* screen) {
    SlotCharts* charts = chartsOfEditedSlot(screen);
    if (!charts || charts->shownStance == charts->slotStance) return;
    dimCardsTakenElsewhere(charts->gameChart);
    void* shared = sharedOf(screen);
    slotChart(shared, editedSlot(shared)) = charts->gameChart;
    field<void*>(screen, MoveSelectScreen::shownChart) = charts->gameChart;
    setStanceTitle(screen, charts->slotStance);
    charts->shownStance = charts->slotStance;
}

int chartsOfSlot(void* shared, int slot, StanceChart (&charts)[stanceCount]) {
    if (!shared || slot < 0 || slot >= slotCount) return 0;
    const int stance = slotStance(shared, slot);
    if (stance == noStance) return 0;
    const SlotCharts& ours = slots[slot];
    const bool oursAreCurrent = shared == knownShared && ours.gameChart && ours.slotStance == stance;
    void* gameChart = oursAreCurrent ? ours.gameChart : slotChart(shared, slot);
    if (!gameChart) return 0;
    int count = 0;
    charts[count++] = {gameChart, stance};
    if (!oursAreCurrent) return count;
    for (int other = 0; other < stanceCount; ++other) {
        if (ours.charts[other]) charts[count++] = {ours.charts[other], other};
    }
    return count;
}

int chartsBesides(void* chart, StanceChart (&others)[stanceCount], int& stanceOfSlot) {
    if (!chart || !knownShared) return 0;
    for (int slot = 0; slot < slotCount; ++slot) {
        StanceChart charts[stanceCount];
        const int count = chartsOfSlot(knownShared, slot, charts);
        bool found = false;
        int otherCount = 0;
        for (int index = 0; index < count; ++index) {
            if (charts[index].chart == chart) {
                found = true;
                continue;
            }
            others[otherCount++] = charts[index];
        }
        if (!found) continue;
        stanceOfSlot = slotStance(knownShared, slot);
        return otherCount;
    }
    return 0;
}

void releaseCharts(void* shared) {
    if (!shared || shared != knownShared) return;
    knownShared = nullptr;
    for (int slot = 0; slot < slotCount; ++slot) {
        SlotCharts& charts = slots[slot];
        if (charts.gameChart) slotChart(shared, slot) = charts.gameChart;
        deleteOurCharts(charts);
        charts = SlotCharts();
    }
}

}  // namespace tweaks::custom_style_stance
