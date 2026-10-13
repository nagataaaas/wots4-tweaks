// The move select screen of My Style (CreateSectSkillSelect.cpp, vtable 0x995c04) and the data
// that the My Style screens share. Offsets are from the Steam version (PE time stamp 683d42eb).
#pragma once
#include <cstdint>
#include "../game_image.h"

namespace tweaks::custom_style_stance {

constexpr int slotCount = 3;  // A My Style holds up to three stances
constexpr int stanceCount = 13;
constexpr int16_t noStance = -1;

// Shared by the My Style screens (made at 0x5c9cfc, freed at 0x5c9ea0).
struct SharedData {
    static constexpr uint32_t slotStances = 0x48;  // int16 x 3, -1 = empty
    static constexpr uint32_t editedSlot = 0x4e;   // int16
    static constexpr uint32_t slotCharts = 0x5c;   // SkillChartChoice* x 3, the move cards of each slot
};

// The screen that confirms and creates the style (CreateSectTop).
struct TopScreen {
    static constexpr uint32_t shared = 0x34;         // SharedData*
    static constexpr uint32_t messageWindow = 0x1d0;  // The window of a message shown (0x5d3bca)
};

// The move select screen.
struct MoveSelectScreen {
    static constexpr uint32_t layoutList = 0x8;      // What the layouts and animations are loaded from
    static constexpr uint32_t layouts = 0xc;         // Layout* array: 0 the screen, 1.. the menu buttons
    static constexpr uint32_t layoutsPlaced = 0x2c;  // bool, set once the buttons were placed (0x5ca460)
    static constexpr uint32_t shared = 0x34;         // SharedData*
    static constexpr uint32_t stanceTitle = 0x3c;    // wchar_t[64] "text_kamaeName"
    static constexpr uint32_t menuCursor = 0x43c;    // int, the menu button under the cursor
    static constexpr uint32_t shownChart = 0x440;    // SkillChartChoice*, the cards drawn and edited
    static constexpr uint32_t inputStep = 0x444;     // thiscall(pressed, repeated), called each frame
    static constexpr uint32_t choiceWindow = 0x448;  // The choice window waited for, or null
    static constexpr uint32_t dialog = 0x468;        // A confirmation dialog, or null
    static constexpr uint32_t playingAnimation = 0x46c;  // int, -1 when none; input waits for it
};

constexpr uint32_t stanceTitleLength = 64;
constexpr uint32_t dialogOpen = 0x4ac;  // bool in the dialog

// The menus' shared choice window, as Auto-Select reads it (0x5cba9c, 0x5be370).
struct ChoiceWindow {
    static constexpr uint32_t state = 0x38;   // int, 4 once closed
    static constexpr uint32_t chosen = 0xa0;  // int, the chosen choice, -1 when cancelled
};
constexpr int choiceWindowClosed = 4;
constexpr uint32_t layoutX = 0x2c;      // float, where a menu button is placed
constexpr uint32_t layoutY = 0x30;

template <typename T>
T& field(void* object, uint32_t offset) {
    return *reinterpret_cast<T*>(static_cast<uint8_t*>(object) + offset);
}

// A chart of move cards (SkillChartChoice, vtable 0x99b55c).
struct Chart {
    static constexpr uint32_t cardsBegin = 0x4;  // Card* vector
    static constexpr uint32_t cardsEnd = 0x8;
    static constexpr uint32_t cursorCard = 0x68;  // The card under the cursor, or null
};

// A move card (0x25c bytes).
struct Card {
    static constexpr uint32_t state = 0x21c;       // int, CardState
    static constexpr uint32_t parents = 0x22c;     // Card* x 3, the cards that lead to it (0x660f50)
    static constexpr uint32_t otherEntry = 0x24c;  // ComboEntry* the card stands for in place of entry, or null
    static constexpr uint32_t entry = 0x250;       // ComboEntry*
    static constexpr uint32_t command = 0x254;     // uint16, compared for first moves (0x6587e5)
    static constexpr uint32_t cost = 0x256;        // int16, ability points (0x5c9fd1)
};
enum CardState : int { cardFree = 0, cardChosen = 1, cardDisabled = 2 };

// A move of a stance's SkillComboData.
struct ComboEntry {
    static constexpr uint32_t number = 0x0;     // int16, the move's number in the stance
    static constexpr uint32_t skill = 0x2;      // int16, SkillData ID
    static constexpr uint32_t mark = 0x6;       // uint8, not 0 for a first move
    static constexpr uint32_t linkCount = 0x8;  // uint8
    static constexpr uint32_t links = 0xc;      // int16*, numbers of the moves it leads to
};

template <typename Visit>
void forEachCard(void* chart, Visit visit) {
    void* const* card = field<void* const*>(chart, Chart::cardsBegin);
    void* const* end = field<void* const*>(chart, Chart::cardsEnd);
    for (; card < end; ++card) {
        if (*card) visit(*card);
    }
}

inline bool isChosen(void* card) {
    return field<int>(card, Card::state) == cardChosen;
}

// The SkillData ID the card stands for, as 0x657cf0 reads it; -1 for none.
inline int skillOf(void* card) {
    void* entry = field<void*>(card, Card::otherEntry);
    if (!entry) entry = field<void*>(card, Card::entry);
    return entry ? field<int16_t>(entry, ComboEntry::skill) : -1;
}

inline bool isFirstMove(void* card) {
    void* entry = field<void*>(card, Card::entry);
    return entry && field<uint8_t>(entry, ComboEntry::mark) != 0;
}

constexpr int cardParentCount = 3;

// A card no other card leads to, at the top of the chart's tree.
inline bool isTopCard(void* card) {
    void* const* parents = &field<void*>(card, Card::parents);
    for (int index = 0; index < cardParentCount; ++index) {
        if (parents[index]) return false;
    }
    return true;
}

}  // namespace tweaks::custom_style_stance
