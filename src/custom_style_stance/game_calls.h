// The game's functions this tweak calls. Most take their arguments in registers, so each has a
// small wrapper. Call setUpGameCalls before any of them.
#pragma once
#include <cstdint>
#include <wots4/mod_api.h>
#include "../game_image.h"

namespace tweaks::custom_style_stance {

void setUpGameCalls(const GameImage& game);

// Starts a menu button animation at once (0x5ba380) or blended (0x5ba580).
void playAnimationAtOnce(void* screen, int animation);
void playAnimation(void* screen, int animation);
// The sound of the menu cursor moving (0x5a0050 with sound 0x61, as 0x5cb528 plays it).
void playCursorSound();
// Applies a layout's position (0x5bb9d0).
void placeLayout(void* layout);
// The stance's name in the game's language (0x6861b0, UI 112..125).
const wchar_t* stanceName(int stance);
// Fills screen->shownChart with the cards of the edited slot's stance (0x5cbef0).
void buildShownChart(void* screen);
// Updates the card information beside the menu (0x5cc150 with no card).
void refreshCardInformation(void* screen);
// A new, empty SkillChartChoice (0x6c7020 and 0x657280), or null; and its deletion (vtable 0).
void* newChart();
void deleteChart(void* chart);

// The UI text of the number in the game's language, through the ModLoader; null when the
// ModLoader is too old or the text is not loaded yet.
const wchar_t* findUiText(const wots4::HostApi* host, uint32_t number);

// A game text: the table (5 = UI texts) and the number in it.
struct GameTextReference {
    uint32_t table;
    uint32_t number;
};
// Opens the menus' shared choice window ([[0x9ebb8c]+0x70]) with one choice per text, the cursor
// on `cursor`, as Auto-Select does (0x5cbd40). Returns the window, or null when there is none.
void* openChoiceWindow(const GameTextReference* choices, int count, int cursor);
// Opens the same window with a message, as the style's confirmation does (0x5d3b67).
void* openMessageWindow(GameTextReference message);
// Opens the choice window with texts that are no game texts (0x5bdb20 copies each).
void* openTextChoiceWindow(const wchar_t* const* choices, int count, int cursor);

// A stance's SkillComboData ([[[0x9ebb14]]] + 36 x stance), or null.
void* comboOf(int stance);
// Whether the stance's combo has a move with the number (its tree, as 0x655a50 searches it).
bool comboHasNumber(void* combo, int number);
// Adds a move to a style's table (0x598df0). False when the number is taken.
bool addMove(void* table, int16_t number, int16_t skill, uint8_t mark, const int16_t* links, int8_t linkCount);
// Makes the table's list of first moves again from its moves' marks (0x599430).
void rebuildFirstMoves(void* table);
// Sets a card's state and its look (dimmed when disabled), as the chart does (0x65ffa0). A card
// that cannot be chosen at all (state 3) is left as it is.
void setCardState(void* card, int state);
// The defence move's index, or -1 for a move that is not one (0x59d9b0); and its type (0x59da20).
int defenceIndex(int skill);
int defenceType(int skill);

// The combo's move at an index below its count (combo +0x10), as 0x44b8b0 goes over a stance's
// moves (0x597a60); null past the end.
void* comboMoveAt(void* combo, int index);
// A table's move at an index below its count (table +0x1c), as 0x59a140 goes over a style's
// moves (0x599170); null past the end.
void* tableMoveAt(void* table, int index);

// The SkillData of an ID, or null (0x5980a0 on [0x9ebb10]).
void* skillDataOf(int skill);
// Starts loading what the SkillData's motions need, and clears `ready` while any of it is not
// loaded yet (0x44bb90).
void loadMotions(void* loader, void* skillData, bool& ready);
// What 0x44bc50 makes from loaded SkillData: 16-byte entries, one per motion.
struct MotionEntries {
    uint8_t* begin = nullptr;
    uint8_t* end = nullptr;
    uint8_t* capacity = nullptr;
};
constexpr uint32_t motionEntrySize = 0x10;
void addMotionEntries(void* loader, void* skillData, MotionEntries& entries);
// Registers a motion under the slot's k in a character's motion store; a key already there is
// kept (0x454c70).
void registerMotion(void* store, int slotKey, void* entry);
// Releases the entries and their memory (0x44bea0).
void freeMotionEntries(MotionEntries& entries);

// The stance of the slot a character fights in, from its style (0x486050 on the character's
// parameter), or -1.
int slotStanceOf(void* parameter);

}  // namespace tweaks::custom_style_stance
