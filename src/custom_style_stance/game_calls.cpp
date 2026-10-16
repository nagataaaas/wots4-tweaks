#include "game_calls.h"
#include <cstddef>
#include <cstdint>
#include "my_style_screen.h"

namespace tweaks::custom_style_stance {

namespace {

constexpr uint32_t chartSize = 0x9c;
constexpr int noCard = -1;

using Allocate = void*(__cdecl*)(uint32_t size, uint32_t flags);
using ConstructChart = void*(__stdcall*)(void* memory);
using DeleteChart = void*(__thiscall*)(void* chart, uint32_t flags);
constexpr uint32_t deleteAndFree = 1;

uintptr_t playAnimationAtOnceAddress;
uintptr_t playAnimationAddress;
uintptr_t playSoundAddress;
uintptr_t placeLayoutAddress;
uintptr_t stanceNameAddress;
uintptr_t buildShownChartAddress;
uintptr_t refreshCardInformationAddress;
Allocate allocate;
ConstructChart constructChart;

// What the choice window is opened with (0x5be410), as Auto-Select fills it at 0x5cbda3.
struct ChoiceWindowSettings {
    uint32_t kind;          // 2: choices only
    float messageScale;     // Used by the kinds with a message
    uint32_t choiceFlag;    // Given to each choice (0x5bdb20); 1 as in Auto-Select
    int8_t cursor;
    const GameTextReference* first;
    const GameTextReference* end;
    const GameTextReference* capacityEnd;
};
static_assert(offsetof(ChoiceWindowSettings, first) == 0x10, "the game reads the choices at +0x10");
constexpr uint32_t choicesOnly = 2;
constexpr uint32_t messageOnly = 4;  // As the style's confirmation (0x5d3b7e)
constexpr uint32_t gameChoiceFlag = 1;
constexpr uint32_t choiceWindowOffset = 0x70;

using ClearChoiceWindow = void(__stdcall*)(void* window);
const uintptr_t* choiceWindowHolder;  // Relocated 0x9ebb8c
ClearChoiceWindow clearChoiceWindow;
uintptr_t fillChoiceWindowAddress;
uintptr_t showChoiceWindowAddress;

constexpr uint32_t comboSize = 36;
// The combo's tree: the combo itself is the head, +4 the root; a node has +8 the side with keys
// not below the one looked for, +0xc the other side, +0x10 its key (int16).
constexpr uint32_t comboRoot = 0x4, nodeLower = 0x8, nodeHigher = 0xc, nodeKey = 0x10;
const uintptr_t* comboHolder;  // Relocated 0x9ebb14

uintptr_t addMoveAddress;
uintptr_t rebuildFirstMovesAddress;
uintptr_t setCardStateAddress;
uintptr_t comboMoveAtAddress;
uintptr_t tableMoveAtAddress;
const uintptr_t* skillDataHolder;  // Relocated 0x9ebb10
uintptr_t skillDataOfAddress;
uintptr_t loadMotionsAddress;
uintptr_t addMotionEntriesAddress;
uintptr_t registerMotionAddress;
using FreeMotionEntries = void(__stdcall*)(MotionEntries* entries);
FreeMotionEntries freeMotionEntriesOf;
uintptr_t slotStanceAddress;
using SkillLookup = int(__cdecl*)(int skill);
SkillLookup defenceIndexOf;
SkillLookup defenceTypeOf;

void* openWindow(uint32_t kind, const GameTextReference* texts, int count, int cursor) {
    const uintptr_t holder = *choiceWindowHolder;
    if (!holder) return nullptr;
    void* window = *reinterpret_cast<void* const*>(holder + choiceWindowOffset);
    if (!window) return nullptr;
    ChoiceWindowSettings settings = {
        kind, 0.0f, gameChoiceFlag, static_cast<int8_t>(cursor), texts, texts + count, texts + count};
    ChoiceWindowSettings* settingsAddress = &settings;
    clearChoiceWindow(window);
    __asm {
        mov ecx, window  // ecx = window, edx = settings
        mov edx, settingsAddress
        call fillChoiceWindowAddress
        mov eax, window  // eax = window
        call showChoiceWindowAddress
    }
    return window;
}

}  // namespace

void setUpGameCalls(const GameImage& game) {
    playAnimationAtOnceAddress = game.address(0x5ba380);
    playAnimationAddress = game.address(0x5ba580);
    playSoundAddress = game.address(0x5a0050);
    placeLayoutAddress = game.address(0x5bb9d0);
    stanceNameAddress = game.address(0x6861b0);
    buildShownChartAddress = game.address(0x5cbef0);
    refreshCardInformationAddress = game.address(0x5cc150);
    allocate = reinterpret_cast<Allocate>(game.address(0x6c7020));
    constructChart = reinterpret_cast<ConstructChart>(game.address(0x657280));
    choiceWindowHolder = reinterpret_cast<const uintptr_t*>(game.address(0x9ebb8c));
    clearChoiceWindow = reinterpret_cast<ClearChoiceWindow>(game.address(0x5bda60));
    fillChoiceWindowAddress = game.address(0x5be410);
    showChoiceWindowAddress = game.address(0x5bddf0);
    comboHolder = reinterpret_cast<const uintptr_t*>(game.address(0x9ebb14));
    addMoveAddress = game.address(0x598df0);
    rebuildFirstMovesAddress = game.address(0x599430);
    setCardStateAddress = game.address(0x65ffa0);
    comboMoveAtAddress = game.address(0x597a60);
    tableMoveAtAddress = game.address(0x599170);
    skillDataHolder = reinterpret_cast<const uintptr_t*>(game.address(0x9ebb10));
    skillDataOfAddress = game.address(0x5980a0);
    loadMotionsAddress = game.address(0x44bb90);
    addMotionEntriesAddress = game.address(0x44bc50);
    registerMotionAddress = game.address(0x454c70);
    freeMotionEntriesOf = reinterpret_cast<FreeMotionEntries>(game.address(0x44bea0));
    slotStanceAddress = game.address(0x486050);
    defenceIndexOf = reinterpret_cast<SkillLookup>(game.address(0x59d9b0));
    defenceTypeOf = reinterpret_cast<SkillLookup>(game.address(0x59da20));
}

// ecx = animation, edi = screen
void playAnimationAtOnce(void* screen, int animation) {
    __asm {
        mov edi, screen
        mov ecx, animation
        call playAnimationAtOnceAddress
    }
}

// eax = animation, ebx = screen
void playAnimation(void* screen, int animation) {
    __asm {
        mov ebx, screen
        mov eax, animation
        call playAnimationAddress
    }
}

enum CursorSound : uint32_t { cursorSound = 0x61 };

// ecx = 0, edx = sound, one float on the stack (0.0), the caller removes it
void playCursorSound() {
    __asm {
        push 0  // 0.0f
        xor ecx, ecx
        mov edx, cursorSound
        call playSoundAddress
        add esp, 4
    }
}

// esi = layout
void placeLayout(void* layout) {
    __asm {
        mov esi, layout
        call placeLayoutAddress
    }
}

// edx = stance; returns eax
const wchar_t* stanceName(int stance) {
    const wchar_t* name;
    __asm {
        mov edx, stance
        call stanceNameAddress
        mov name, eax
    }
    return name;
}

// eax = screen
void buildShownChart(void* screen) {
    __asm {
        mov eax, screen
        call buildShownChartAddress
    }
}

// ecx = card index, one stack argument: the screen (ret 4)
void refreshCardInformation(void* screen) {
    __asm {
        push screen
        mov ecx, noCard
        call refreshCardInformationAddress
    }
}

void* newChart() {
    void* memory = allocate(chartSize, 0);
    return memory ? constructChart(memory) : nullptr;
}

void deleteChart(void* chart) {
    const auto destructor = (*reinterpret_cast<DeleteChart**>(chart))[0];
    destructor(chart, deleteAndFree);
}

const wchar_t* findUiText(const wots4::HostApi* host, uint32_t number) {
    constexpr size_t findGameTextEnd = offsetof(wots4::HostApi, findGameText) + sizeof(host->findGameText);
    if (!host || host->size < findGameTextEnd || !host->findGameText) return nullptr;
    return host->findGameText(number);
}

// The window copies the texts while it is filled, so the choices need not outlive the call.
void* openChoiceWindow(const GameTextReference* choices, int count, int cursor) {
    return openWindow(choicesOnly, choices, count, cursor);
}

void* openMessageWindow(GameTextReference message) {
    return openWindow(messageOnly, &message, 1, 0);
}

void* comboOf(int stance) {
    if (stance < 0 || stance >= stanceCount) return nullptr;
    const auto* level1 = reinterpret_cast<const uintptr_t*>(*comboHolder);
    if (!level1) return nullptr;
    const auto* level2 = reinterpret_cast<const uintptr_t*>(*level1);
    if (!level2 || !*level2) return nullptr;
    return reinterpret_cast<void*>(*level2 + comboSize * stance);
}

bool comboHasNumber(void* combo, int number) {
    void* found = combo;
    for (void* node = field<void*>(combo, comboRoot); node;) {
        if (field<int16_t>(node, nodeKey) >= number) {
            found = node;
            node = field<void*>(node, nodeLower);
        } else {
            node = field<void*>(node, nodeHigher);
        }
    }
    return found != combo && field<int16_t>(found, nodeKey) == number;
}

// ecx = table, eax = &link count; on the stack &number, &SkillData ID, &mark, links (ret 0x10)
bool addMove(void* table, int16_t number, int16_t skill, uint8_t mark, const int16_t* links, int8_t linkCount) {
    int16_t* numberAddress = &number;
    int16_t* skillAddress = &skill;
    uint8_t* markAddress = &mark;
    int8_t* linkCountAddress = &linkCount;
    uint32_t added;
    __asm {
        push links
        push markAddress
        push skillAddress
        push numberAddress
        mov eax, linkCountAddress
        mov ecx, table
        call addMoveAddress
        mov added, eax
    }
    return added != 0;
}

// eax = table
void rebuildFirstMoves(void* table) {
    __asm {
        mov eax, table
        call rebuildFirstMovesAddress
    }
}

// eax = card, ecx = state
void setCardState(void* card, int state) {
    __asm {
        push ebx
        push esi
        push edi
        mov eax, card
        mov ecx, state
        call setCardStateAddress
        pop edi
        pop esi
        pop ebx
    }
}

// esi = combo, ecx = &index
void* comboMoveAt(void* combo, int index) {
    int* indexAddress = &index;
    void* move;
    __asm {
        push esi
        mov esi, combo
        mov ecx, indexAddress
        call comboMoveAtAddress
        mov move, eax
        pop esi
    }
    return move;
}

// eax = table, edi = index
void* tableMoveAt(void* table, int index) {
    void* move;
    __asm {
        push edi
        mov eax, table
        mov edi, index
        call tableMoveAtAddress
        mov move, eax
        pop edi
    }
    return move;
}

// eax = the SkillData holder's object; the ID on the stack (ret 4)
void* skillDataOf(int skill) {
    const uintptr_t holder = skillDataHolder ? *skillDataHolder : 0;
    if (!holder) return nullptr;
    void* skillData;
    __asm {
        push skill
        mov eax, holder
        call skillDataOfAddress
        mov skillData, eax
    }
    return skillData;
}

// ecx = loader; SkillData, &ready on the stack (ret 8)
void loadMotions(void* loader, void* skillData, bool& ready) {
    bool* readyAddress = &ready;
    __asm {
        push readyAddress
        push skillData
        mov ecx, loader
        call loadMotionsAddress
    }
}

// ecx = loader; SkillData, &entries on the stack (ret 8)
void addMotionEntries(void* loader, void* skillData, MotionEntries& entries) {
    MotionEntries* entriesAddress = &entries;
    __asm {
        push entriesAddress
        push skillData
        mov ecx, loader
        call addMotionEntriesAddress
    }
}

// eax = k, ebx = entry; the store on the stack (ret 4)
void registerMotion(void* store, int slotKey, void* entry) {
    __asm {
        push ebx
        push store
        mov eax, slotKey
        mov ebx, entry
        call registerMotionAddress
        pop ebx
    }
}

void freeMotionEntries(MotionEntries& entries) {
    freeMotionEntriesOf(&entries);
}

// eax = parameter
int slotStanceOf(void* parameter) {
    int stance;
    __asm {
        mov eax, parameter
        call slotStanceAddress
        mov stance, eax
    }
    return stance;
}

int defenceIndex(int skill) {
    return defenceIndexOf(skill);
}

int defenceType(int skill) {
    return defenceTypeOf(skill);
}

}  // namespace tweaks::custom_style_stance
